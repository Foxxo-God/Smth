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
#define KEY_UP 64
#define KEY_DOWN 128

static const u16 mint = RGB(20, 31, 23);
static const u16 pale = RGB(25, 31, 26);
static const u16 amber = RGB(31, 22, 10);
static int px, py, face_x, face_y, hero_hp, gold, room, game_state, frame;
static int swing_timer, spell_timer, hurt_timer, enemy_count;
static int enemy_x[4], enemy_y[4], enemy_hp[4], enemy_active[4];
static int bolt_x, bolt_y, bolt_timer;
static u16 previous_keys;
static const int pillar_x[3] = {56, 144, 96};
static const int pillar_y[3] = {48, 96, 116};

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

static void load_room(int index)
{
    static const int spawn_x[3][4] = {{177,186,118,0},{180,192,178,117},{174,0,0,0}};
    static const int spawn_y[3][4] = {{48,119,72,0},{42,82,126,126},{82,0,0,0}};
    int i;
    enemy_count = index == 0 ? 3 : index == 1 ? 4 : 1;
    for (i = 0; i < 4; ++i) {
        enemy_active[i] = i < enemy_count;
        enemy_x[i] = spawn_x[index][i];
        enemy_y[i] = spawn_y[index][i];
        enemy_hp[i] = index == 2 ? 6 : index == 1 ? 2 : 1;
    }
}

static void restart(void)
{
    px = 28; py = 82; face_x = 1; face_y = 0; hero_hp = 6; gold = 0;
    room = 0; swing_timer = 0; spell_timer = 0; hurt_timer = 0; bolt_timer = 0;
    game_state = 1;
    load_room(room);
}

static int solid_at(int x, int y)
{
    int i;
    if (y < 23 || y > 149 || x < 15) return 1;
    if (x > 225 && !(enemy_count == 0 && y >= 65 && y <= 101)) return 1;
    for (i = 0; i < 3; ++i)
        if (x + 5 > pillar_x[i] && x - 5 < pillar_x[i] + 16 &&
            y + 5 > pillar_y[i] && y - 5 < pillar_y[i] + 16) return 1;
    return 0;
}

static void damage_enemy(int index, int amount)
{
    enemy_hp[index] -= amount;
    if (enemy_hp[index] <= 0) {
        enemy_active[index] = 0;
        --enemy_count;
        gold += room == 2 ? 25 : 5 + room * 3;
    }
}

static int abs_value(int value)
{
    return value < 0 ? -value : value;
}

static void melee_attack(void)
{
    int i, dx, dy;
    swing_timer = 10;
    for (i = 0; i < 4; ++i) if (enemy_active[i]) {
        dx = enemy_x[i] - px; dy = enemy_y[i] - py;
        if (face_x && dx * face_x >= 0 && dx * face_x <= 23 && abs_value(dy) <= 12)
            damage_enemy(i, 1);
        else if (face_y && dy * face_y >= 0 && dy * face_y <= 23 && abs_value(dx) <= 12)
            damage_enemy(i, 1);
    }
}

static void cast_spell(void)
{
    int i, dx, dy, distance, best = 72, target = -1;
    spell_timer = 35;
    bolt_x = px + face_x * 68; bolt_y = py + face_y * 68; bolt_timer = 8;
    for (i = 0; i < 4; ++i) if (enemy_active[i]) {
        dx = enemy_x[i] - px; dy = enemy_y[i] - py;
        distance = face_x ? dx * face_x : dy * face_y;
        if (distance >= 0 && distance <= best &&
            (face_x ? abs_value(dy) <= 9 : abs_value(dx) <= 9)) {
            best = distance; target = i;
        }
    }
    if (target >= 0) {
        bolt_x = enemy_x[target]; bolt_y = enemy_y[target];
        damage_enemy(target, 2);
    }
}

static void update(u16 keys)
{
    int i, dx, dy;
    u16 pressed = (u16)(previous_keys & (u16)~keys);
    if (game_state == 0 || game_state == 2 || game_state == 4) {
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

    dx = (keys & KEY_RIGHT ? 1 : 0) - (keys & KEY_LEFT ? 1 : 0);
    dy = (keys & KEY_DOWN ? 1 : 0) - (keys & KEY_UP ? 1 : 0);
    if (dx) face_x = dx, face_y = 0;
    else if (dy) face_x = 0, face_y = dy;
    if (dx && !solid_at(px + dx, py)) px += dx;
    if (dy && !solid_at(px, py + dy)) py += dy;
    if ((pressed & KEY_A) && swing_timer == 0) melee_attack();
    if ((pressed & KEY_B) && spell_timer == 0) cast_spell();
    if (swing_timer > 0) --swing_timer;
    if (spell_timer > 0) --spell_timer;
    if (bolt_timer > 0) --bolt_timer;
    if (hurt_timer > 0) --hurt_timer;

    for (i = 0; i < 4; ++i) if (enemy_active[i]) {
        dx = px - enemy_x[i]; dy = py - enemy_y[i];
        if ((frame + i * 3) % 6 == 0) {
            if (abs_value(dx) > abs_value(dy)) {
                if (dx) enemy_x[i] += dx > 0 ? 1 : -1;
            } else if (dy) enemy_y[i] += dy > 0 ? 1 : -1;
            if (solid_at(enemy_x[i], enemy_y[i])) {
                if (abs_value(dx) > abs_value(dy)) enemy_x[i] -= dx > 0 ? 1 : -1;
                else enemy_y[i] -= dy > 0 ? 1 : -1;
            }
        }
        if (abs_value(dx) < 10 && abs_value(dy) < 10 && hurt_timer == 0) {
            --hero_hp; hurt_timer = 48;
            if (hero_hp <= 0) game_state = 4;
        }
    }
    if (enemy_count == 0 && px > 225 && py >= 65 && py <= 101) {
        if (++room >= 3) game_state = 2;
        else {
            px = 26; py = 82; hero_hp = hero_hp < 6 ? hero_hp + 1 : 6;
            load_room(room);
        }
    }
    previous_keys = keys;
}

static void draw_background(void)
{
    int x, y, i;
    rect(0,0,240,160,RGB(3,7,11));
    for(y=23;y<151;y+=16) for(x=15;x<225;x+=16) {
        rect(x,y,16,16,((x/16+y/16)&1)?RGB(10,13,15):RGB(9,12,14));
        rect(x,y,15,1,RGB(13,16,17)); rect(x,y,1,16,RGB(12,15,16));
    }
    rect(0,0,240,22,RGB(12,15,17)); rect(0,151,240,9,RGB(12,15,17));
    rect(0,22,14,129,RGB(12,15,17));
    rect(226,22,14,42,RGB(12,15,17)); rect(226,102,14,49,RGB(12,15,17));
    rect(225,64,15,39,enemy_count?RGB(19,12,10):RGB(14,10,8));
    rect(229,68,11,31,enemy_count?RGB(8,9,11):RGB(3,7,10));
    for(i=0;i<3;++i) {
        rect(pillar_x[i]-2,pillar_y[i]+12,20,7,RGB(3,6,8));
        rect(pillar_x[i],pillar_y[i]+3,16,15,RGB(18,21,21));
        rect(pillar_x[i]+2,pillar_y[i],12,16,RGB(23,25,24));
        rect(pillar_x[i]+2,pillar_y[i],12,2,RGB(29,28,24));
    }
    for(i=0;i<2;++i) {
        x=i?209:24; rect(x,32,5,8,RGB(23,12,7));
        rect(x+1,29,3,5,RGB(31,17,7)); pixel(x+2,27-(frame&1),RGB(31,23,11));
    }
}

static void draw_hero(void)
{
    int x=px,y=py;
    rect(x-5,y+4,11,5,RGB(3,6,8));
    if(face_x) rect(x-face_x*8-2,y-2,5,4,RGB(24,11,13));
    else rect(x-2,y-face_y*8-2,4,5,RGB(24,11,13));
    rect(x-4,y-5,8,10,mint); rect(x-5,y-3,3,5,mint); rect(x+2,y-3,3,5,mint);
    rect(x-4,y-7,3,4,pale); rect(x+2,y-7,3,4,pale);
    if(face_y<0) rect(x-3,y-5,6,3,RGB(5,12,21));
    else if(face_x<0) rect(x-6,y-3,3,6,RGB(5,12,21));
    else if(face_x>0) rect(x+3,y-3,3,6,RGB(5,12,21));
    else rect(x-3,y+2,6,3,RGB(5,12,21));
    if(face_y) rect(x-2,y+(face_y<0?-4:2),4,1,amber);
    else rect(x+(face_x<0?-4:2),y-2,1,4,amber);
    rect(x-3,y+4,2,2,RGB(5,10,13)); rect(x+2,y+4,2,2,RGB(5,10,13));
    if(swing_timer>3) {
        if(face_x) rect(x+face_x*8-2,y-7,4,14,amber);
        else rect(x-7,y+face_y*8-2,14,4,amber);
    }
    if(hurt_timer>39&&(frame&2)) rect(x-5,y-8,10,14,pale);
}

static void draw_enemy(int i)
{
    int x=enemy_x[i], y=enemy_y[i];
    int boss=room==2;
    u16 skin=boss?RGB(25,8,12):RGB(16,23,12);
    rect(x-6,y+4,boss?15:13,5,RGB(3,6,8));
    rect(x-(boss?7:5),y-(boss?7:5),boss?14:10,boss?12:10,skin);
    rect(x-6,y-6,3,4,boss?RGB(31,13,7):RGB(18,25,13));
    rect(x+3,y-6,3,4,boss?RGB(31,13,7):RGB(18,25,13));
    rect(x-4,y-2,3,2,RGB(31,8,6)); rect(x+2,y-2,3,2,RGB(31,8,6));
    if(boss||enemy_hp[i]>1) {
        rect(x-7,y-11,14,2,RGB(5,6,7));
        rect(x-7,y-11,enemy_hp[i]*2,2,RGB(27,8,8));
    }
}

static void number(int value, int x, int y, u16 color)
{
    char digits[5];
    int count=0;
    do { digits[count++]=(char)('0'+value%10); value/=10; } while(value&&count<4);
    while(count) {
        char digit[2]; digit[0]=digits[--count]; digit[1]=0;
        text(digit,x,y,color,1); x+=4;
    }
}

static void draw_game(void)
{
    int i;
    draw_background();
    for(i=0;i<4;++i) if(enemy_active[i]) draw_enemy(i);
    if(bolt_timer) {
        int x=px,y=py,steps;
        for(steps=0;steps<8;++steps) {
            x+=face_x*7; y+=face_y*7;
            if((face_x&&abs_value(x-px)>abs_value(bolt_x-px))||
               (face_y&&abs_value(y-py)>abs_value(bolt_y-py))) break;
            rect(x-2,y-2,5,5,steps&1?pale:RGB(11,26,31));
        }
        rect(bolt_x-3,bolt_y-3,7,7,pale);
    }
    draw_hero();
    rect(0,0,240,18,RGB(3,7,11));
    text("CRYPT",3,5,amber,1); number(room+1,24,5,pale);
    text("HP",38,5,pale,1);
    for(i=0;i<6;++i) rect(48+i*6,5,4,7,i<hero_hp?RGB(27,9,11):RGB(9,11,12));
    text("G",88,5,amber,1); number(gold,96,5,pale);
    text("FOES",172,5,pale,1); number(enemy_count,192,5,amber);
    rect(0,152,240,8,RGB(3,7,11)); text("A ATTACK B MAGIC",56,154,RGB(17,21,20),1);
}

static void text_center(const char *s, int y, u16 color, int scale)
{
    int length=0;
    while(s[length]) ++length;
    text(s,(240-length*4*scale+scale)/2,y,color,scale);
}

static void draw_overlay(const char *line1, const char *line2, const char *line3)
{
    rect(23,40,194,75,RGB(3,9,15));
    rect(25,42,190,71,RGB(13,16,17));
    text_center(line1,53,pale,2);
    text_center(line2,72,amber,1);
    text_center(line3,94,mint,1);
}

int main(void)
{
    REG_DISPCNT = 0x0403;
    px=28; py=82; face_x=1; face_y=0; game_state=0; enemy_count=1;
    for (;;) {
        u16 keys;
        wait_vblank();
        keys = (u16)(~REG_KEYS) & 0x03ff;
        update(keys);
        ++frame;
        if (game_state == 0) {
            draw_background();
            draw_hero();
            draw_overlay("BYTEBOUND", "CRYPT DELVE", "START TO ENTER");
        } else if (game_state == 3) {
            draw_game(); draw_overlay("PAUSED", "", "START RESUME");
        } else if (game_state == 2) {
            draw_game(); draw_overlay("DUNGEON CLEAR", "GOLD COLLECTED", "START AGAIN");
        } else if (game_state == 4) {
            draw_game(); draw_overlay("YOU FELL", "THE CRYPT WAITS", "START RETRY");
        } else draw_game();
    }
}
