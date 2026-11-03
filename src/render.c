/*
 * render.c - the screen of Polonaise GB.
 *
 * The dance floor is the background: one tile per cell, the people drawn onto their floor
 * tile, so the line jumps from cell to cell like on the C64. The map is moved 4 pixels to
 * the right, so the 19 columns of the field sit in the middle of the 20 of the screen, with
 * half a tile of wall on either side. Above the field is the score, below it the song, the
 * line, the combo and a message line.
 *
 * Only what changes is written to the screen: scr[] remembers the tile shown on each cell,
 * and each frame the people are drawn again; cells left by them get their floor back.
 * The LED frame, the DJ and the speakers move by changing their tile data to the beat.
 *
 * Panels (song intro, crash, summary, pause) are drawn into the window, at the bottom of the
 * screen. (Switching the window off at the panel's last line by a raster interrupt came too
 * late on some lines, so the panels end at the bottom.)
 * Floating points, sparkles and the like are sprites.
 */
#include <gb/gb.h>
#include <gb/hardware.h>
#include "render.h"
#include "game.h"
#include "gfx.h"
#include "text.h"
#include "sound.h"
#include <string.h>

#define CH(c) (T_FONT + (c) - 32)

uint8_t lang;
uint16_t ticks;

/* the tile of each cell without people, and the tile shown */
static uint8_t base[CELLS];
static uint8_t scr[CELLS];
/* the address of each cell in the background map */
static uint8_t *cell_addr[CELLS];
/* the frame in which a person was drawn on a cell */
static uint8_t stamp[CELLS];
/* counts the redraws of the people (the value in stamp[] of the cells drawn last) */
static uint8_t people_stamp;
/* the state of the last redraw of the people */
static uint8_t drawn_steps, drawn_phase, drawn_length;
/* the cells with people drawn in the last frame */
static uint8_t prev[MAX_GUESTS + CELLS / 4];
static uint8_t nprev;
/* lit floor: the phase of each cell in the patterns (x * 5 + y * 3) % 23 and, for the
 * summary, (x + y) % 3; the phases lit in the current beat */
static uint8_t light_phase[CELLS];
static uint8_t show_phase[CELLS];
static uint8_t lit23, lit3, beat3, lights_cleared;
/* the phases before the beat, while the lights wait for a frame with time for them */
static uint8_t light_old23, light_old3, lights_pending;
/* the cells sorted by their phases, and where each phase starts */
static uint8_t light_order[CELLS], light_start[24];
static uint8_t show_order[CELLS], show_start[4];
/* the person tile of each look (T_PEOPLE + 4 * one of the drawn looks) */
static uint8_t look_tile[LOOKS];
static uint8_t last_beat, last_step;
/* a shy guest who fled shows a "!" for a while */
static uint8_t flee_mark[MAX_GUESTS];

/* the status lines as last drawn */
static uint32_t hud_score;
static uint8_t hud_lives, hud_song, hud_len, hud_target, hud_combo, hud_bar, hud_msg;
static uint8_t msg_text, msg_frames;
static uint8_t bar_tile[10];

/* floating points */
#define POPUPS 3
typedef struct {
    uint8_t timer, x, y, n;
    uint8_t t[5];
} Popup;
static Popup popups[POPUPS];
static uint8_t next_popup;

static uint8_t panel_on, nspr, last_nspr;

static const uint8_t confetti_x[20] = {
    11, 48, 85, 122, 159, 36, 73, 110, 147, 24, 61, 98, 135, 12, 49, 86, 123, 0, 37, 74
};
static const uint8_t confetti_y[20] = {
    0, 29, 58, 87, 116, 145, 174, 203, 232, 5, 34, 63, 92, 121, 150, 179, 208, 237, 10, 39
};
static const uint8_t confetti_speed[20] = {1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2};
static const int8_t sine[16] = {0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3};

/* ---------------------------------------------------------------- interrupts */

static void sort_phases(const uint8_t *phase, uint8_t n, uint8_t *order, uint8_t *start)
{
    uint8_t p, c, k = 0;

    for (p = 0; p < n; p++) {
        start[p] = k;
        for (c = 0; c < CELLS; c++)
            if (phase[c] == p)
                order[k++] = c;
    }
    start[n] = k;
}

static void init_tables(void)
{
    uint8_t x, y, c = 0, p, q, k, ky = 0;

    for (y = 0, p = 0; y < ROWS; y++) {
        for (x = 0, q = p, k = ky; x < COLS; x++, c++) {
            cell_addr[c] = (uint8_t *)0x9800 + ((uint16_t)(y + FIELD_ROW) << 5) + x;
            light_phase[c] = q;
            show_phase[c] = k;
            q += 5;
            if (q >= 23)
                q -= 23;
            k = k == 2 ? 0 : k + 1;
        }
        p += 3;
        if (p >= 23)
            p -= 23;
        ky = ky == 2 ? 0 : ky + 1;
    }
    sort_phases(light_phase, 23, light_order, light_start);
    sort_phases(show_phase, 3, show_order, show_start);
    look_tile[0] = T_PEOPLE;
    for (c = 1, k = 1; c < LOOKS; c++) {
        look_tile[c] = T_PEOPLE + k * 4;
        k = k == GUEST_LOOKS ? 1 : k + 1;
    }
}

/* tiles whose data changes in the next vertical blank (the animations to the beat) */
#define ANIM_QUEUE 6
static volatile uint8_t anim_n;
static uint8_t anim_tile[ANIM_QUEUE];
static const uint8_t *anim_src[ANIM_QUEUE];

static void vbl_isr(void)
{
    uint8_t k, i, *d;
    const uint8_t *src;

    if (panel_on)
        LCDC_REG |= LCDCF_WINON;
    /* at most four tiles per blank, the rest in the next one */
    for (k = 0; k < 4 && anim_n; k++) {
        anim_n--;
        d = (uint8_t *)0x8000 + ((uint16_t)anim_tile[anim_n] << 4);
        src = anim_src[anim_n];
        for (i = 0; i < 16; i++)
            *d++ = *src++;
    }
}

/* changes the data of a tile in a vertical blank */
static void anim(uint8_t tile, const uint8_t *src)
{
    CRITICAL {
        if (anim_n < ANIM_QUEUE) {
            anim_tile[anim_n] = tile;
            anim_src[anim_n] = src;
            anim_n++;
        }
    }
}


void render_init(void)
{
    DISPLAY_OFF;
    /* all tiles at $8000 (the sprite area), where background and sprites share them */
    set_sprite_data(0, GFX_TILES, gfx_tiles);
    init_tables();
    BGP_REG = 0xe4;
    OBP0_REG = 0xd0;
    OBP1_REG = 0xd0;
    WX_REG = 7;
    WY_REG = 144;
    SCX_REG = 0;
    SCY_REG = 0;
    render_clear();
    CRITICAL {
        add_VBL(vbl_isr);
    }
    set_interrupts(VBL_IFLAG);
    LCDC_REG = LCDCF_ON | LCDCF_WIN9C00 | LCDCF_WINOFF | LCDCF_BG8000 | LCDCF_BG9800 | LCDCF_OBJ8 | LCDCF_OBJON |
               LCDCF_BGON;
}

/* black screen, no panel, no sprites */
void render_clear(void)
{
    panel_close();
    render_hide_sprites();
    DISPLAY_OFF;
    fill_bkg_rect(0, 0, 32, 18, T_SPACE);
    SCX_REG = 0;
    DISPLAY_ON;
}

void render_hide_sprites(void)
{
    uint8_t i;

    for (i = 0; i < 40; i++)
        move_sprite(i, 0, 0);
    last_nspr = 0;
}

/* ---------------------------------------------------------------- text */

/*
 * Writes to the video RAM. The check that it is accessible and the write must not be split
 * by an interrupt (the window switch at the end of a panel), or the write may fall into the
 * time the display reads the RAM and be lost; so the interrupts wait for each byte.
 */
void vset(uint8_t win, uint8_t x, uint8_t y, uint8_t t)
{
    uint8_t *a = (uint8_t *)(win ? 0x9c00 : 0x9800) + ((uint16_t)y << 5) + x;

    CRITICAL {
        while (STAT_REG & STATF_BUSY)
            ;
        *a = t;
    }
}

void vfill(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t t)
{
    uint8_t i, j, *a;

    for (j = 0; j < h; j++) {
        a = (uint8_t *)(win ? 0x9c00 : 0x9800) + ((uint16_t)(y + j) << 5) + x;
        for (i = 0; i < w; i++) {
            CRITICAL {
                while (STAT_REG & STATF_BUSY)
                    ;
                *a = t;
            }
            a++;
        }
    }
}

void vrow(uint8_t x, uint8_t y, uint8_t n, const uint8_t *t)
{
    while (n--)
        vset(0, x++, y, *t++);
}

/* tile data (for the animations) */
void vdata(uint8_t first, uint8_t n, const uint8_t *data)
{
    uint8_t *a = (uint8_t *)0x8000 + ((uint16_t)first << 4);
    uint16_t k = (uint16_t)n << 4;

    while (k--) {
        CRITICAL {
            while (STAT_REG & STATF_BUSY)
                ;
            *a = *data;
        }
        a++;
        data++;
    }
}

#define put vset

static const uint32_t tens[10] = {
    1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL
};

/*
 * The decimal digits of n into buf, most significant first, without leading zeros (at least
 * one digit); returns their number. By subtraction: divisions of 32 bit numbers take
 * thousands of cycles on the Game Boy.
 */
static uint8_t digits_of(uint32_t n, uint8_t *buf)
{
    uint8_t k, d, len = 0;
    uint32_t t;

    for (k = 0; k < 10; k++) {
        t = tens[k];
        for (d = 0; n >= t; d++)
            n -= t;
        if (d || len || k == 9)
            buf[len++] = d;
    }
    return len;
}

static uint8_t num_digits(uint32_t n)
{
    uint8_t buf[10];
    return digits_of(n, buf);
}

/* a number with at least `digits` digits (leading zeros) */
void print_num(uint8_t win, uint8_t x, uint8_t y, uint32_t num, uint8_t digits)
{
    uint8_t buf[10], n = digits_of(num, buf), k;

    for (k = n; k < digits; k++)
        put(win, x++, y, CH('0'));
    for (k = 0; k < n; k++)
        put(win, x++, y, CH('0') + buf[k]);
}

/* the width of a line of a text starting at s */
static uint8_t line_width(const uint8_t *s, uint32_t num)
{
    uint8_t w = 0;
    for (; *s != TXT_END && *s != TXT_NL; s++)
        w += *s == TXT_NUM ? num_digits(num) : 1;
    return w;
}

uint8_t text_lines(uint8_t text)
{
    const uint8_t *s = texts[text][lang];
    uint8_t n = 1;
    for (; *s != TXT_END; s++)
        if (*s == TXT_NL)
            n++;
    return n;
}

static void print_line(uint8_t win, uint8_t x, uint8_t y, const uint8_t *s, uint32_t num)
{
    for (; *s != TXT_END && *s != TXT_NL; s++) {
        if (*s == TXT_NUM) {
            print_num(win, x, y, num, 1);
            x += num_digits(num);
        } else {
            put(win, x++, y, *s);
        }
    }
}

void print(uint8_t win, uint8_t x, uint8_t y, uint8_t text, uint32_t num)
{
    const uint8_t *s = texts[text][lang];
    for (;;) {
        print_line(win, x, y, s, num);
        while (*s != TXT_END && *s != TXT_NL)
            s++;
        if (*s == TXT_END)
            return;
        s++;
        y++;
    }
}

void print_center(uint8_t win, uint8_t y, uint8_t text, uint32_t num)
{
    const uint8_t *s = texts[text][lang];
    for (;;) {
        print_line(win, (20 - line_width(s, num)) / 2, y, s, num);
        while (*s != TXT_END && *s != TXT_NL)
            s++;
        if (*s == TXT_END)
            return;
        s++;
        y++;
    }
}

/* ---------------------------------------------------------------- panels */

void panel_open(uint8_t h)
{
    uint8_t y;

    panel_close();
    render_hide_sprites();
    vfill(1, 0, 0, 20, h, T_SPACE);
    vset(1, 0, 0, T_BOX_TL);
    vfill(1, 1, 0, 18, 1, T_BOX_T);
    vset(1, 19, 0, T_BOX_TR);
    for (y = 1; y < h - 1; y++) {
        vset(1, 0, y, T_BOX_L);
        vset(1, 19, y, T_BOX_R);
    }
    vset(1, 0, h - 1, T_BOX_BL);
    vfill(1, 1, h - 1, 18, 1, T_BOX_B);
    vset(1, 19, h - 1, T_BOX_BR);
    WY_REG = (18 - h) * 8;
}

/* shows the panel once its text is written */
void panel_show(void)
{
    panel_on = 1;
}

void panel_close(void)
{
    panel_on = 0;
    LCDC_REG &= ~LCDCF_WINON;
}

void panel_line(uint8_t y, uint8_t text, uint32_t num)
{
    print_center(1, y, text, num);
}

/* a label on the left and a value with "+" on the right */
void panel_value(uint8_t y, uint8_t text, uint32_t label_num, uint32_t value)
{
    uint8_t d = num_digits(value);
    print(1, 1, y, text, label_num);
    put(1, 18 - d, y, CH('+'));
    print_num(1, 19 - d, y, value, 1);
}

/* ---------------------------------------------------------------- the field */

/* the length of a horizontal run of the same furniture to the left of a cell */
static uint8_t run_index(uint8_t c)
{
    uint8_t i = 0, t = tile[c];
    while (cell_x[c] > 0 && tile[c - 1] == t) {
        c--;
        i++;
    }
    return i;
}

static uint8_t floor_tile(uint8_t c)
{
    uint8_t lit;

    if (g.phase == PHASE_CLEARED)
        lit = show_phase[c] == lit3;
    else
        lit = light_phase[c] == lit23;
    return lit ? T_LIT : T_FLOOR;
}

static uint8_t base_tile(uint8_t c)
{
    uint8_t i;

    switch (tile[c]) {
    case TILE_DJ:
        i = run_index(c);
        return i == 2 ? T_DJ : i == 0 || i == 4 ? T_DJ_END : (i & 1) ? T_DJ_DECK : T_DJ_MIX;
    case TILE_TABLE:
        return T_TABLE;
    case TILE_SPEAKER:
        return T_SPEAKER;
    case TILE_BAR:
        return (c & 1) ? T_BAR_B : T_BAR_A;
    }
    return floor_tile(c);
}

static void show(uint8_t c, uint8_t t)
{
    uint8_t *a;

    if (scr[c] != t) {
        scr[c] = t;
        a = cell_addr[c];
        CRITICAL {
            while (STAT_REG & STATF_BUSY)
                ;
            *a = t;
        }
    }
}

void render_hud_reset(void)
{
    hud_score = 0xffffffffUL;
    hud_lives = hud_song = hud_len = hud_target = hud_combo = hud_bar = hud_msg = 0xff;
    memset(bar_tile, 0xff, sizeof(bar_tile));
    msg_frames = 0;
}

/* the phases of the floor lights and the LED frame for the current beat */
static void beat_phases(void)
{
    uint8_t b = beat;

    while (b >= 23)
        b -= 23;
    lit23 = b ? 23 - b : 0;
    b = beat;
    while (b >= 3)
        b -= 3;
    beat3 = b;
    lit3 = b ? 3 - b : 0;
}

/* draws the whole field of a new song: frame, furniture, floor */
void render_song(void)
{
    uint8_t row[COLS], x, y, c = 0;

    render_hide_sprites();
    /* the whole field at once, with the display off for a moment */
    DISPLAY_OFF;
    SCX_REG = 252;
    beat_phases();
    lights_pending = 0;
    lights_cleared = g.phase == PHASE_CLEARED;
    c = 0;
    for (y = 0; y < ROWS; y++) {
        for (x = 0; x < COLS; x++, c++) {
            base[c] = scr[c] = row[x] = base_tile(c);
            stamp[c] = 0;
        }
        set_bkg_tiles(0, y + FIELD_ROW, COLS, 1, row);
        vset(0, 31, y + FIELD_ROW, T_WALL_L);
        vset(0, 19, y + FIELD_ROW, T_WALL_R);
    }
    for (x = 0, y = 0; x < COLS; x++) {
        row[x] = T_LED0 + y;
        y = y == 2 ? 0 : y + 1;
    }
    set_bkg_tiles(0, FIELD_ROW - 1, COLS, 1, row);
    set_bkg_tiles(0, FIELD_ROW + ROWS, COLS, 1, row);
    vset(0, 19, FIELD_ROW - 1, T_LED1);
    vset(0, 31, FIELD_ROW - 1, T_LED2);
    vset(0, 19, FIELD_ROW + ROWS, T_LED1);
    vset(0, 31, FIELD_ROW + ROWS, T_LED2);
    nprev = 0;
    drawn_phase = 0xff;
    for (x = 0; x < MAX_GUESTS; x++)
        flee_mark[x] = 0;
    for (x = 0; x < POPUPS; x++)
        popups[x].timer = 0;
    last_beat = beat;
    render_hud_reset();
    DISPLAY_ON;
}

/* the floor lights follow the beat */
static void relight(uint8_t c)
{
    uint8_t t;

    if (tile[c] != TILE_FLOOR)
        return;
    t = floor_tile(c);
    if (base[c] == t)
        return;
    base[c] = t;
    if (stamp[c] != people_stamp)
        show(c, t);
}

/* the cells of one phase of the light pattern: order[start[p]] .. order[start[p + 1] - 1] */
static void relight_phase(const uint8_t *order, const uint8_t *start, uint8_t p)
{
    uint8_t k;

    for (k = start[p]; k < start[p + 1]; k++)
        relight(order[k]);
}

/* the floor lights follow the beat: only the cells of the phase going out and of the one
 * coming in change (all cells when the pattern changes, at the summary of a song) */
static void lights(void)
{
    uint8_t old23 = light_old23, old3 = light_old3, c, cleared = g.phase == PHASE_CLEARED;

    if (cleared != lights_cleared) {
        lights_cleared = cleared;
        for (c = 0; c < CELLS; c++)
            relight(c);
    } else if (cleared) {
        relight_phase(show_order, show_start, old3);
        relight_phase(show_order, show_start, lit3);
    } else {
        relight_phase(light_order, light_start, old23);
        relight_phase(light_order, light_start, lit23);
    }
}

/* the line (unless only the guests need it: then the line's cells are only marked) and the guests */
static void people(uint8_t line)
{
    uint8_t i, c, n = 0, tt, t, dance = g.phase == PHASE_CLEARED, dance_beat = (uint8_t)(ticks >> 4);
    Guest *gu;
    const uint8_t *bp = body, *lp = look;
    uint8_t legs = g.steps, len = g.length, ps = people_stamp;

    if (dance) {
        for (i = 0; i < len; i++, legs++) {
            c = *bp++;
            t = look_tile[*lp++] + (((dance_beat + i) & 1) ? 2 : 0) + (legs & 1);
            stamp[c] = ps;
            show(c, t);
            prev[n++] = c;
        }
    } else if (line) {
        for (i = 0; i < len; i++, legs++) {
            c = *bp++;
            stamp[c] = ps;
            show(c, look_tile[*lp++] + (legs & 1));
            prev[n++] = c;
        }
    } else {
        for (i = 0; i < len; i++) {
            c = *bp++;
            stamp[c] = ps;
            prev[n++] = c;
        }
    }
    for (i = 0, gu = guests; i < guest_slots; i++, gu++) {
        if (!gu->active)
            continue;
        c = gu->cell;
        tt = (uint8_t)(ticks / 8) + i * 3 + gu->look;
        if (gu->type == GUEST_STAR) {
            if (gu->age > STAR_TICKS - 2 * TICK_HZ && (ticks & 4))
                continue;           /* blinks before it leaves */
            t = T_STAR + ((tt >> 1) & 1) + ((tt >> 1) & 2);
        } else {
            t = look_tile[gu->look] + ((gu->type == GUEST_SHY ? flee_mark[i] : (tt >> 2) & 1) ? 2 : 0) +
                ((tt >> 1) & 1);
        }
        stamp[c] = people_stamp;
        show(c, t);
        prev[n++] = c;
    }
    nprev = n;
}

/* ---------------------------------------------------------------- sprites */

/* a sprite at screen position x, y (written into the shadow OAM directly: faster) */
static void spr(uint8_t x, uint8_t y, uint8_t t)
{
    volatile OAM_item_t *o;

    if (nspr >= 40)
        return;
    o = &shadow_OAM[nspr++];
    o->y = y + 16;
    o->x = x + 8;
    o->tile = t;
}

static uint8_t cell_sx(uint8_t c)
{
    return cell_x[c] * 8 + 4;
}

static uint8_t cell_sy(uint8_t c)
{
    return (cell_y[c] + FIELD_ROW) * 8;
}

static void popup(uint8_t c, uint16_t points)
{
    Popup *p = &popups[next_popup];
    uint8_t buf[10], n, k;

    next_popup = next_popup == POPUPS - 1 ? 0 : next_popup + 1;
    n = digits_of(points, buf);
    if (n > 4)
        n = 4;
    p->t[0] = (uint8_t)(S_DIGIT + 10);
    for (k = 0; k < n; k++)
        p->t[k + 1] = S_DIGIT + buf[k];
    p->n = n + 1;
    p->x = cell_sx(c) + 2 - p->n * 2;
    if (p->x > 140)
        p->x = 140;
    p->y = cell_sy(c) - 2;
    p->timer = 45;
}

static void effects(void)
{
    uint8_t i, k, c, x, y, a, n;
    Guest *gu;

    nspr = 0;
    if (g.phase == PHASE_CLEARED) {
        for (i = 0; i < 20; i++) {
            x = confetti_x[i];
            y = (uint8_t)((uint8_t)ticks * confetti_speed[i] + confetti_y[i]);
            if (y < 144)
                spr(x, y, (i + (ticks >> 3)) & 1 ? S_CONF1 : S_CONF2);
        }
    }
    if (panel_on) {
        goto done;
    }
    for (i = 0; i < POPUPS; i++) {
        Popup *p = &popups[i];
        if (!p->timer)
            continue;
        p->timer--;
        if (p->timer & 1)
            p->y--;
        for (k = 0; k < p->n; k++)
            spr(p->x + k * 4, p->y, p->t[k]);
    }
    for (i = 0, gu = guests; i < guest_slots; i++, gu++) {
        if (!gu->active)
            continue;
        c = gu->cell;
        x = cell_sx(c);
        y = cell_sy(c);
        if (gu->type == GUEST_STAR) {
            a = (uint8_t)(ticks >> 2) & 15;
            spr(x + sine[(a + 4) & 15], y + (sine[a] >> 1) - 1, (ticks & 8) ? S_SPARK : S_SPARK2);
            spr(x - sine[(a + 4) & 15], y - (sine[a] >> 1) - 1, (ticks & 8) ? S_SPARK2 : S_SPARK);
        } else if (gu->type == GUEST_SHY) {
            if (flee_mark[i])
                spr(x + 5, y - 7, S_BANG);
            else if ((((uint8_t)(ticks >> 4) + i) & 3) == 0)
                spr(x + 5, y - 3, S_SWEAT);
        }
    }
    c = body[0];
    x = cell_sx(c);
    y = cell_sy(c);
    if (g.phase == PHASE_CRASH) {
        for (k = 0; k < 3; k++) {
            a = ((uint8_t)(ticks >> 1) + k * 5) & 15;
            spr(x + (sine[(a + 4) & 15] >> 1), y - 6 + (sine[a] >> 2), S_DIZZY);
        }
    }
    if (g.phase == PHASE_PLAY && !g.moving && (ticks & 16)) {
        for (k = 0; k < 4; k++) {
            n = next_cell(c, k);
            if (n == NO_CELL || tile[n] != TILE_FLOOR || ((occ[n] & OCC_MEMBER) && !(g.length > 2 &&
                                                                                     n == body[g.length - 1])))
                continue;
            if (g.length > 1 && n == body[1])
                continue;
            spr(cell_sx(n), cell_sy(n), S_ARROW_R + k);
        }
    }
done:
    for (i = nspr; i < last_nspr; i++)
        shadow_OAM[i].y = 0;
    last_nspr = nspr;
}

/* ---------------------------------------------------------------- status lines */

void render_message(uint8_t text, uint8_t frames)
{
    msg_text = text;
    msg_frames = frames;
    hud_msg = 0xff;
}

static void hud(void)
{
    uint8_t i, k, m, bar;

    if (hud_score != g.score) {
        if (hud_score == 0xffffffffUL)
            print(0, 0, ROW_TOP, HUD_SCORE, 0);
        hud_score = g.score;
        print_num(0, 7, ROW_TOP, g.score > 9999999UL ? 9999999UL : g.score, 7);
    }
    if (hud_lives != g.lives) {
        for (i = 0; i < LIVES_MAX; i++)
            vset(0, 18 - i, ROW_TOP, i < g.lives ? T_HEART : T_SPACE);
        hud_lives = g.lives;
    }
    if (hud_song != g.song) {
        /* (assigning hud_song first made SDCC 4.4 store the difference) */
        vfill(0, 0, ROW_SONG, 10, 1, T_SPACE);
        print(0, 0, ROW_SONG, HUD_SONG, g.song);
        hud_song = g.song;
    }
    if (hud_len != g.length || hud_target != g.s.target) {
        hud_len = g.length;
        hud_target = g.s.target;
        vset(0, 13, ROW_SONG, T_DANCER);
        print_num(0, 14, ROW_SONG, followers(), 2);
        vset(0, 16, ROW_SONG, CH('/'));
        print_num(0, 17, ROW_SONG, g.s.target, 2);
    }
    /* (the division only every 4 frames) */
    bar = !g.combo ? 0 : (ticks & 3) && hud_bar != 0xff ? hud_bar : (uint8_t)((g.combo_timer + 2) / 3);
    if (hud_combo != g.combo || hud_bar != bar) {
        if (hud_combo != g.combo) {
            vfill(0, 0, ROW_COMBO, 9, 1, T_SPACE);
            if (g.combo > 1) {
                print(0, 0, ROW_COMBO, HUD_COMBO, 0);
                vset(0, 6, ROW_COMBO, T_X);
                print_num(0, 7, ROW_COMBO, g.combo, 1);
            }
        }
        hud_combo = g.combo;
        hud_bar = bar;
        /* only the tiles of the bar that change */
        for (i = 0, k = 0; i < 10; i++, k += 8) {
            m = !g.combo ? T_SPACE : T_BAR0 + (bar > k ? (bar - k > 8 ? 8 : bar - k) : 0);
            if (bar_tile[i] != m) {
                vset(0, 9 + i, ROW_COMBO, m);
                bar_tile[i] = m;
            }
        }
    }
    m = 0xfe;
    if (msg_frames) {
        msg_frames--;
        m = msg_frames ? msg_text : 0xfe;
    } else if (g.phase == PHASE_PLAY && !g.moving) {
        m = (ticks & 32) ? HUD_GO : 0xfe;
    }
    if (m != hud_msg) {
        hud_msg = m;
        vfill(0, 0, ROW_MSG, 20, 1, T_SPACE);
        if (m != 0xfe)
            print(0, (19 - line_width(texts[m][lang], 0)) / 2, ROW_MSG, m, 0);
    }
}

/* ---------------------------------------------------------------- per frame */

/* reacts to the events of the last tick */
void render_events(void)
{
    uint8_t i;
    Event *e;

    for (i = 0; i < g.nevents; i++) {
        e = &g.events[i];
        switch (e->type) {
        case EV_CATCH:
            popup(e->cell, e->points);
            break;
        case EV_FLEE:
            flee_mark[e->index] = 30;
            break;
        case EV_STAR:
            render_message(HUD_STAR, 90);
            break;
        case EV_EXTRA_LIFE:
            render_message(HUD_EXTRA, 120);
            break;
        }
    }
}

void render_frame(void)
{
    uint8_t i, c, redrawn = 0, line;

    ticks++;
    for (i = 0; i < guest_slots; i++)
        if (flee_mark[i])
            flee_mark[i]--;
    /* the people, then the cells they left: after a step or an event, and every 4 frames
     * for the dancing (a redraw of all of them every frame would take too long) */
    line = g.nevents || g.steps != drawn_steps || g.phase != drawn_phase || g.length != drawn_length;
    if (line || (ticks & 3) == 0) {
        uint8_t old[MAX_GUESTS + CELLS / 4], nold = nprev;
        redrawn = 1;
        drawn_steps = g.steps;
        drawn_phase = g.phase;
        drawn_length = g.length;
        for (i = 0; i < nold; i++)
            old[i] = prev[i];
        if (!++people_stamp)
            people_stamp = 1;   /* 0 marks the cells of a new floor */
        people(line);
        for (i = 0; i < nold; i++) {
            c = old[i];
            if (stamp[c] != people_stamp)
                show(c, base[c]);
        }
    }
    if (beat != last_beat) {
        last_beat = beat;
        if (!lights_pending) {
            light_old23 = lit23;
            light_old3 = lit3;
            lights_pending = 1;
        }
        beat_phases();
        anim(T_LED0, anim_led + beat3 * 48);
        anim(T_LED1, anim_led + beat3 * 48 + 16);
        anim(T_LED2, anim_led + beat3 * 48 + 32);
        anim(T_DJ, anim_dj + (beat & 1) * 16);
    }
    if ((music_step & 3) != last_step) {
        last_step = music_step & 3;
        if (last_step < 2)
            anim(T_SPEAKER, anim_speaker + (last_step == 0 ? 16 : 0));
    }
    /* the sprites, the floor lights and the status lines in frames without a redraw of the
     * people (the sprites just stay for a frame) */
    if (!redrawn || g.phase == PHASE_CLEARED)
        effects();
    if (!redrawn) {
        if (lights_pending) {
            lights_pending = 0;
            lights();
        } else {
            hud();
        }
    }
}
