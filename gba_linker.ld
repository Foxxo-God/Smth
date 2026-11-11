OUTPUT_ARCH(arm)
ENTRY(gba_entry)
MEMORY
{
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 32M
    EWRAM (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
}
SECTIONS
{
    .gbaheader 0x08000000 : { KEEP(*(.gbaheader)) } > ROM
    .text 0x080000C0 : { *(.text.startup) *(.text*) *(.rodata*) } > ROM
    .bss 0x02000000 (NOLOAD) :
    {
        __bss_start__ = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > EWRAM
    /DISCARD/ : { *(.comment*) *(.ARM.attributes*) *(.eh_frame*) }
}
