typedef unsigned short u16;
typedef unsigned int u32;

#define REG_DISPCNT (*(volatile u16 *)0x04000000)
#define REG_VCOUNT  (*(volatile u16 *)0x04000006)
#define REG_KEYS    (*(volatile u16 *)0x04000130)
#define VRAM        ((volatile u16 *)0x06000000)
#define RGB(r,g,b)  ((u16)((r) | ((g) << 5) | ((b) << 10)))
#define SCREEN_W 240
#define SCREEN_H 160
#define KEY_A 1
#define KEY_B 2
#define KEY_START 8
#define KEY_RIGHT 16
#define KEY_LEFT 32

static const u16 sky = RGB(8, 20, 27);
static const u16 mint = RGB(20, 31, 23);
static const u16 pale = RGB(25, 31, 26);
static const u16 amber = RGB(31, 22, 10);
static int px, py, vx, vy, grounded, facing, dash_frames, dash_cooldown;
static int game_state, chip_count, camera_x, frame;
static u32 chip_flags;
static u16 previous_keys;

static const int platforms[][3] = {
    {0, 144, 560}, {34, 115, 44}, {100, 92, 48}, {177, 115, 48},
    {246, 91, 48}, {319, 112, 50}, {390, 87, 47}, {466, 112, 44}
};
static const int hazards[][2] = {{78,136},{151,136},{221,136},{294,136},{368,136},{441,136}};
static const int crystals[][2] = {{49,104},{118,81},{194,104},{263,80},{337,101},{405,76},{482,101},{25,131},{279,131},{420,131}};

static void pixel(int x, int y, u16 color)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H)
        VRAM[y * SCREEN_W + x] = color;
}

static void rect(int x, int y, int w, int h, u16 color)
{
    int yy, xx;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    for (yy = y; yy < y + h; ++yy)
        for (xx = x; xx < x + w; ++xx)
            VRAM[yy * SCREEN_W + xx] = color;
}

static u16 glyph(char c, int row)
{
    static const unsigned char letters[36][5] = {
        {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},
        {7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},
        {5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{5,7,7,7,5},{2,5,5,5,2},
        {6,5,6,4,4},{2,5,5,3,1},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
        {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},
        {7,1,2,4,7},{2,5,5,5,2},{1,3,1,1,7},{6,1,2,4,7},{6,1,2,1,6},
        {5,5,7,1,1},{7,4,6,1,6},{3,4,6,5,2},{7,1,2,2,2},{2,5,2,5,2},
        {2,5,7,1,6}
    };
    int index;
    if (c >= 'A' && c <= 'Z') index = c - 'A';
    else if (c >= '0' && c <= '9') index = 26 + c - '0';
    else return 0;
    return letters[index][row];
}

static void text(const char *s, int x, int y, u16 color, int scale)
{
    int col, row;
    while (*s) {
        for (row = 0; row < 5; ++row) {
            unsigned char bits = (unsigned char)glyph(*s, row);
            for (col = 0; col < 3; ++col)
                if (bits & (4 >> col)) rect(x + col * scale, y + row * scale, scale, scale, color);
        }
        x += 4 * scale;
        ++s;
    }
}

static void wait_vblank(void)
{
    while (REG_VCOUNT >= 160) { }
    while (REG_VCOUNT < 160) { }
}

static void restart(void)
{
    px = 12; py = 132; vx = 0; vy = 0; grounded = 0; facing = 1;
    dash_frames = 0; dash_cooldown = 0; chip_count = 0; chip_flags = 0;
    camera_x = 0; game_state = 1;
}

static void collide_platforms(int old_y)
{
    int i, x, y, w;
    grounded = 0;
    for (i = 0; i < 8; ++i) {
        x = platforms[i][0]; y = platforms[i][1]; w = platforms[i][2];
        if (px + 9 > x && px < x + w && py + 12 > y && py < y + 8) {
            if (vy >= 0 && old_y + 12 <= y + 3) { py = y - 12; vy = 0; grounded = 1; }
            else if (vy < 0 && old_y >= y + 8) { py = y + 8; vy = 0; }
            else if (vx > 0) px = x - 9;
            else if (vx < 0) px = x + w;
        }
    }
}

static void update(u16 keys)
{
    int old_y = py, i;
    int pressed = (u16)(previous_keys & (u16)~keys);
    if (game_state == 0) {
        if (pressed & (KEY_START | KEY_A)) restart();
        previous_keys = keys;
        return;
    }
    if (pressed & KEY_START) {
        game_state = game_state == 3 ? 1 : 3;
        previous_keys = keys;
        return;
    }
    if (game_state == 3) { previous_keys = keys; return; }
    if (game_state == 2) {
        if (pressed & (KEY_START | KEY_A)) { game_state = 0; }
        previous_keys = keys;
        return;
    }
    if (pressed & KEY_A && grounded) { vy = -5; grounded = 0; }
    if ((pressed & KEY_B) && dash_cooldown == 0) {
        dash_frames = 10; dash_cooldown = 48; vx = facing * 4;
    }
    if (dash_frames > 0) --dash_frames;
    else {
        if (!(keys & KEY_LEFT)) {
            if (keys & KEY_RIGHT) { vx += 1; facing = 1; }
            else vx = vx * 3 / 4;
        } else { vx -= 1; facing = -1; }
        if (vx > 2) vx = 2;
        if (vx < -2) vx = -2;
        if (vy < 5) ++vy;
    }
    if (dash_cooldown > 0) --dash_cooldown;
    px += vx;
    if (px < 0) px = 0;
    collide_platforms(py);
    py += vy;
    collide_platforms(old_y);
    if (px > 548) px = 548;
    for (i = 0; i < 6; ++i) {
        if (px + 9 > hazards[i][0] && px < hazards[i][0] + 12 && py + 12 > 136 && py < 144) {
            px = 12; py = 132; vx = 0; vy = 0;
        }
    }
    for (i = 0; i < 10; ++i) {
        if (!(chip_flags & (1u << i)) && px + 9 > crystals[i][0] - 3 && px < crystals[i][0] + 8 && py + 12 > crystals[i][1] - 4 && py < crystals[i][1] + 8) {
            chip_flags |= 1u << i;
            ++chip_count;
        }
    }
    camera_x = px - 110;
    if (camera_x < 0) camera_x = 0;
    if (camera_x > 320) camera_x = 320;
    if (px >= 535) game_state = 2;
    if (py > 170) { px = 12; py = 132; vy = 0; }
    previous_keys = keys;
}

static void draw_background(void)
{
    int i;
    rect(0,0,240,160,sky);
    rect(0,80,240,30,RGB(7,17,24));
    for (i = 0; i < 18; ++i) {
        int x = (i * 43 - camera_x / 4 + 300) % 240;
        int y = 12 + (i * 29) % 57;
        pixel(x,y,RGB(16,25,27));
        pixel(x+1,y,RGB(16,25,27));
    }
    rect(0,122,240,22,RGB(8,15,20));
    for (i = 0; i < 10; ++i) {
        int x = (i * 31 - camera_x / 2 + 300) % 250;
        rect(x,112 + (i%3)*4,9,24,RGB(9,18,23));
        rect(x+2,116 + (i%3)*4,2,2,RGB(14,26,26));
    }
}

static void draw_hero(void)
{
    int x = px - camera_x, y = py;
    rect(x+1,y+7,3,5,RGB(27,10,17));
    rect(x-1,y+10,3,2,amber);
    rect(x+2,y,2,5,pale); rect(x+7,y,2,5,pale);
    rect(x+1,y+5,8,7,mint);
    rect(x,y+8,2,3,RGB(12,24,23)); rect(x+8,y+8,2,3,RGB(12,24,23));
    rect(x+2,y+3,7,5,RGB(4,11,21));
    rect(x+3,y+4,5,3,RGB(14,25,28));
    rect(x+4,y+5,3,1,RGB(31,29,18));
    rect(x+2,y+11,3,1,RGB(4,10,16)); rect(x+7,y+11,3,1,RGB(4,10,16));
    if (dash_frames > 0) { rect(x-5,y+5,4,2,pale); rect(x-9,y+8,4,1,RGB(13,25,28)); }
}

static void draw_game(void)
{
    int i, x, y, w;
    draw_background();
    for (i = 0; i < 8; ++i) {
        x = platforms[i][0] - camera_x; y = platforms[i][1]; w = platforms[i][2];
        rect(x,y,w,8,RGB(8,14,17)); rect(x,y,w,2,RGB(17,26,18));
        rect(x,y+2,w,6,RGB(12,20,20));
        for (int j = 5; j < w; j += 13) rect(x+j,y+4,2,2,RGB(15,24,19));
    }
    for (i = 0; i < 6; ++i) {
        x = hazards[i][0] - camera_x;
        rect(x,141,12,3,RGB(20,8,13));
        for (int j = 0; j < 3; ++j) {
            rect(x+j*4+1,137,2,4,RGB(31,11,14));
            pixel(x+j*4+1,136,RGB(31,18,13));
        }
    }
    for (i = 0; i < 10; ++i) if (!(chip_flags & (1u << i))) {
        x = crystals[i][0] - camera_x; y = crystals[i][1] + ((frame >> 3) & 1);
        rect(x+2,y,3,7,RGB(14,28,21)); rect(x,y+2,7,3,RGB(18,30,22));
        pixel(x+3,y+2,RGB(31,30,19));
    }
    draw_hero();
    rect(0,0,240,12,RGB(3,8,14));
    text("BYTEBOUND",5,3,pale,1);
    text("CHIPS",139,3,amber,1);
    for (i = 0; i < 10; ++i) rect(163+i*6,4,4,5,(chip_flags & (1u<<i)) ? mint : RGB(8,14,17));
}

static void draw_overlay(const char *line1, const char *line2, const char *line3)
{
    rect(23,40,194,75,RGB(3,9,15));
    rect(25,42,190,71,RGB(8,16,21));
    text(line1, 120-(int)(line1[0] ? 1 : 0), 52, pale, 2);
    text(line2, 76, 70, amber, 1);
    text(line3, 54, 91, mint, 1);
}

int main(void)
{
    REG_DISPCNT = 0x0403;
    px=12; py=132; facing=1; game_state=0;
    for (;;) {
        u16 keys;
        wait_vblank();
        keys = (u16)(~REG_KEYS) & 0x03ff;
        update(keys);
        ++frame;
        if (game_state == 0) {
            draw_background();
            draw_hero();
            draw_overlay("BYTEBOUND", "NEON BURROW", "PRESS START");
        } else if (game_state == 3) {
            draw_game(); draw_overlay("PAUSED", "", "START RESUME");
        } else if (game_state == 2) {
            draw_game(); draw_overlay("SIGNAL OK", "FOX GO HOME", "START RETRY");
        } else draw_game();
    }
}
