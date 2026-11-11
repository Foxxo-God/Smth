import struct
import sys

SHT_NOBITS = 8
SHT_SYMTAB = 2
SHT_REL = 9
SHN_UNDEF = 0
SHN_ABS = 0xFFF1
R_ARM_ABS32 = 2
R_ARM_CALL = 28
R_ARM_JUMP24 = 29
ROM_BASE = 0x08000000
EWRAM_BASE = 0x02000000

class ElfObject:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        header = struct.unpack_from("<16sHHIIIIIHHHHHH", self.data)
        shoff, shentsize, shnum, shstrndx = header[6], header[11], header[12], header[13]
        raw_sections = [struct.unpack_from("<IIIIIIIIII", self.data, shoff + i * shentsize) for i in range(shnum)]
        shstr = self.data[raw_sections[shstrndx][4]:raw_sections[shstrndx][4] + raw_sections[shstrndx][5]]
        self.sections = []
        for raw in raw_sections:
            name_offset, kind, flags, address, offset, size, link, info, alignment, entsize = raw
            end = shstr.find(b"\0", name_offset)
            name = shstr[name_offset:end].decode() if name_offset < len(shstr) else ""
            content = bytearray(size) if kind == SHT_NOBITS else bytearray(self.data[offset:offset + size])
            self.sections.append({"name": name, "type": kind, "offset": offset, "size": size, "link": link, "info": info, "align": max(1, alignment), "data": content})
        self.symbols = {}
        self.relocations = []
        for index, section in enumerate(self.sections):
            if section["type"] == SHT_SYMTAB:
                strings = self.sections[section["link"]]
                string_data = strings["data"]
                symbols = []
                count = section["size"] // 16
                for i in range(count):
                    nameoff, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", self.data, section["offset"] + i * 16)
                    end = string_data.find(0, nameoff)
                    name = string_data[nameoff:end].decode() if nameoff < len(string_data) else ""
                    symbols.append({"name": name, "value": value, "bind": info >> 4, "shndx": shndx})
                self.symbols[index] = symbols
            elif section["type"] == SHT_REL:
                self.relocations.append((index, section["info"], section["link"], section["offset"], section["size"]))


def align(value, boundary):
    return (value + boundary - 1) & -boundary


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: build_gba.py startup.o game.o output.gba")
    startup, game = [ElfObject(path) for path in sys.argv[1:3]]
    objects = [startup, game]
    sections = {}
    cursor = ROM_BASE
    layout = [(startup, ".gbaheader", ROM_BASE), (startup, ".text.startup", ROM_BASE + 0xC0)]
    cursor = ROM_BASE + 0xC0 + startup.sections[next(i for i,s in enumerate(startup.sections) if s["name"] == ".text.startup")]["size"]
    for name in (".text", ".rodata.str1.1", ".rodata"):
        section = next((s for s in game.sections if s["name"] == name), None)
        if section:
            cursor = align(cursor, section["align"])
            layout.append((game, name, cursor))
            cursor += section["size"]
    for obj, name, address in layout:
        index = next(i for i,s in enumerate(obj.sections) if s["name"] == name)
        sections[(id(obj), index)] = address
    for obj in objects:
        for index, section in enumerate(obj.sections):
            if section["name"] == ".bss":
                sections[(id(obj), index)] = EWRAM_BASE

    globals_by_name = {}
    for obj in objects:
        for symbols in obj.symbols.values():
            for symbol in symbols:
                if symbol["name"] and symbol["bind"] in (1, 2) and symbol["shndx"] != SHN_UNDEF:
                    globals_by_name[symbol["name"]] = (obj, symbol)
    synthetic = {"__bss_start__": EWRAM_BASE, "__bss_end__": EWRAM_BASE + next(s["size"] for s in game.sections if s["name"] == ".bss")}

    def symbol_address(obj, symbol):
        if symbol["name"] in synthetic:
            return synthetic[symbol["name"]]
        if symbol["shndx"] == SHN_ABS:
            return symbol["value"]
        if symbol["shndx"] == SHN_UNDEF:
            if symbol["name"] not in globals_by_name:
                raise ValueError("unresolved symbol: " + symbol["name"])
            owner, definition = globals_by_name[symbol["name"]]
            return symbol_address(owner, definition)
        key = (id(obj), symbol["shndx"])
        if key not in sections:
            raise ValueError("unplaced section " + obj.sections[symbol["shndx"]]["name"])
        return sections[key] + symbol["value"]

    for obj in objects:
        for _, target_index, symtab_index, rel_offset, rel_size in obj.relocations:
            if (id(obj), target_index) not in sections:
                continue
            target = obj.sections[target_index]
            symbols = obj.symbols[symtab_index]
            for offset in range(rel_offset, rel_offset + rel_size, 8):
                place_offset, info = struct.unpack_from("<II", obj.data, offset)
                symbol_index, kind = info >> 8, info & 0xFF
                symbol = symbols[symbol_index]
                place = sections[(id(obj), target_index)] + place_offset
                if kind == R_ARM_ABS32:
                    addend = struct.unpack_from("<I", target["data"], place_offset)[0]
                    struct.pack_into("<I", target["data"], place_offset, (symbol_address(obj, symbol) + addend) & 0xFFFFFFFF)
                elif kind in (R_ARM_CALL, R_ARM_JUMP24):
                    instruction = struct.unpack_from("<I", target["data"], place_offset)[0]
                    addend = instruction & 0x00FFFFFF
                    if addend & 0x00800000:
                        addend -= 0x01000000
                    addend <<= 2
                    displacement = symbol_address(obj, symbol) + addend - place
                    if displacement & 3 or not (-0x02000000 <= displacement < 0x02000000):
                        raise ValueError("ARM branch out of range")
                    instruction = (instruction & 0xFF000000) | ((displacement >> 2) & 0x00FFFFFF)
                    struct.pack_into("<I", target["data"], place_offset, instruction)
                else:
                    raise ValueError("unsupported ARM relocation: " + str(kind))

    image = bytearray(b"\xff" * max(0x8000, 1 << (cursor - ROM_BASE - 1).bit_length()))
    for obj, name, address in layout:
        index = next(i for i,s in enumerate(obj.sections) if s["name"] == name)
        section = obj.sections[index]
        if name == ".gbaheader" and section["size"] != 0xC0:
            raise ValueError("GBA header must be exactly 192 bytes")
        start = address - ROM_BASE
        image[start:start + section["size"]] = section["data"]
    image[0xBD] = (-sum(image[0xA0:0xBD]) - 0x19) & 0xFF
    with open(sys.argv[3], "wb") as output:
        output.write(image)
    print("Wrote %s (%d bytes)" % (sys.argv[3], len(image)))


if __name__ == "__main__":
    main()
