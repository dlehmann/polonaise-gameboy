/*
 * main.c - Polonaise GB: title screen, game, name entry.
 *
 * One game tick per frame (the Game Boy shows 59.7 frames per second, the game counts 60).
 * Controls: D-pad steers, A continues, START pauses (SELECT in the pause ends the game),
 * SELECT on the title screen switches between German and English.
 */
#include <gb/gb.h>
#include <gb/hardware.h>
#include <string.h>
#include "game.h"
#include "render.h"
#include "sound.h"
#include "save.h"
#include "gfx.h"
#include "text.h"

#define CH(c) (T_FONT + (c) - 32)

static uint8_t keys, pressed;
static uint16_t frames;
/* the result of the last game for the title screen: score, and whether it is a record */
static uint32_t last_score;
static uint8_t last_record, last_played;

static void frame(void)
{
    uint8_t k;

    vsync();
    sound_tick();
    k = joypad();
    pressed = k & ~keys;
    keys = k;
    frames++;
}

/* ---------------------------------------------------------------- title screen */

static const uint8_t logo[] = {T_BIG_P, T_BIG_O, T_BIG_L, T_BIG_O, T_BIG_N, T_BIG_A, T_BIG_I, T_BIG_S, T_BIG_E};

static void draw_logo(void)
{
    uint8_t i, t;

    for (i = 0; i < sizeof(logo); i++) {
        t = logo[i];
        vset(0, 1 + i * 2, 1, t);
        vset(0, 2 + i * 2, 1, t + 1);
        vset(0, 1 + i * 2, 2, t + 2);
        vset(0, 2 + i * 2, 2, t + 3);
    }
}

/* a little polonaise walking over a strip of dance floor */
static void draw_conga(uint8_t row, uint8_t pos)
{
    uint8_t buf[20], x, k;
    int8_t c;

    for (x = 0; x < 20; x++)
        buf[x] = T_FLOOR;
    for (k = 0; k < 8; k++) {
        c = (int8_t)pos - k;
        if (c < 0)
            c += 30;
        if (c < 20)
            buf[c] = T_PEOPLE + (k ? 1 + (k * 5) % GUEST_LOOKS : 0) * 4 + ((pos + k) & 1);
    }
    vrow(0, row, 20, buf);
}

static void draw_scores(void)
{
    uint8_t i, k;

    print_center(0, 4, TITLE_SCORES, 0);
    if (!scores[0].score) {
        print_center(0, 8, TITLE_NOSCORES, 0);
        return;
    }
    for (i = 0; i < SCORES && scores[i].score; i++) {
        print_num(0, 0, 6 + i, i + 1, 2);
        if (i < 9)
            vset(0, 0, 6 + i, T_SPACE);
        for (k = 0; k < NAME_LEN; k++)
            vset(0, 3 + k, 6 + i, scores[i].name[k]);
        print_num(0, 12, 6 + i, scores[i].score, 7);
    }
}

static void title_page(uint8_t page)
{
    vfill(0, 0, 4, 20, 14, T_SPACE);
    if (page) {
        draw_scores();
    } else {
        print_center(0, 4, TITLE_TAGLINE, 0);
        if (last_played)
            print_center(0, 12, last_record ? TITLE_RECORD : TITLE_SCORE, last_score);
    }
    print_center(0, 17, TITLE_LANG, 0);
}

static void title(void)
{
    uint8_t page = last_played && last_record ? 1 : 0;
    uint16_t t = 0;

    render_clear();
    draw_logo();
    title_page(page);
    music_start();
    for (;;) {
        frame();
        t++;
        if (t % 360 == 0) {
            page ^= 1;
            title_page(page);
        }
        if (!page && t % 12 == 0)
            draw_conga(9, (t / 12) % 30);
        if ((t & 31) == 0)
            print_center(0, 16, TITLE_START, 0);
        else if ((t & 31) == 24)
            vfill(0, 0, 16, 20, 1, T_SPACE);
        if (pressed & J_SELECT) {
            lang ^= 1;
            save_store();
            sfx_play(SFX_KEY);
            title_page(page);
        }
        if (pressed & (J_START | J_A)) {
            sfx_play(SFX_SONG_START);
            return;
        }
    }
}

/* ---------------------------------------------------------------- the game */

/* the panel for the current phase (after the pause, too) */
static void phase_panel(void)
{
    switch (g.phase) {
    case PHASE_INTRO:
        if (g.song == 1) {
            panel_open(18);
            panel_line(1, INTRO_TITLE, g.song);
            panel_line(2, INTRO_GOAL, g.s.target);
            print(1, 1, 5, HELP, 0);
        } else {
            panel_open(7);
            panel_line(1, INTRO_TITLE, g.song);
            panel_line(2, INTRO_GOAL, g.s.target);
            panel_line(4, INTRO_FASTER, 0);
        }
        break;
    case PHASE_CRASH:
        panel_open(g.dropped ? 6 : 4);
        panel_line(1, CRASH_TITLE, 0);
        panel_line(2, g.lives == 1 ? CRASH_LIVES1 : CRASH_LIVES, g.lives);
        if (g.dropped)
            panel_line(3, g.dropped == 1 ? CRASH_DROPPED1 : CRASH_DROPPED, g.dropped);
        break;
    case PHASE_CLEARED:
        panel_open(13);
        panel_line(1, CLEARED_TITLE, g.song);
        panel_value(3, CLEARED_LINE, 0, g.bonus_line);
        panel_value(4, CLEARED_SONG, g.song, g.bonus_song);
        if (g.bonus_perfect)
            panel_value(5, CLEARED_PERFECT, 0, g.bonus_perfect);
        print(1, 1, 7, CLEARED_TOTAL, 0);
        print_num(1, 11, 7, g.score > 9999999UL ? 9999999UL : g.score, 7);
        {
            Song s;
            song_get(g.song + 1, &s);
            panel_line(9, CLEARED_NEXT, g.song + 1);
            panel_line(10, CLEARED_INFO, s.target);
        }
        panel_line(11, CONTINUE, 0);
        break;
    case PHASE_GAME_OVER:
        panel_open(6);
        panel_line(1, GAMEOVER_TITLE, 0);
        panel_line(3, GAMEOVER_SCORE, g.score);
        panel_line(4, GAMEOVER_SONG, g.song);
        break;
    default:
        panel_close();
        return;
    }
    panel_show();
}

static uint8_t catch_sfx(const Event *e)
{
    if (e->index == GUEST_STAR)
        return SFX_CATCH_STAR;
    return SFX_CATCH1 + (e->count > 5 ? 4 : e->count - 1);
}

static void sounds(void)
{
    uint8_t i;
    Event *e;

    for (i = 0; i < g.nevents; i++) {
        e = &g.events[i];
        switch (e->type) {
        case EV_TURN: sfx_play(SFX_TURN); break;
        case EV_GO: sfx_play(SFX_GO); break;
        case EV_DENY: sfx_play(SFX_DENY); break;
        case EV_CATCH: sfx_play(catch_sfx(e)); break;
        case EV_FLEE: sfx_play(SFX_FLEE); break;
        case EV_STAR: sfx_play(SFX_STAR); break;
        case EV_STAR_GONE: sfx_play(SFX_STAR_GONE); break;
        case EV_CRASH: sfx_play(SFX_CRASH); break;
        case EV_EXTRA_LIFE: sfx_play(SFX_EXTRA_LIFE); break;
        case EV_SONG_START: sfx_play(SFX_SONG_START); break;
        case EV_CLEARED: sfx_play(SFX_CLEARED); break;
        case EV_GAME_OVER:
            music_stop();
            sfx_play(SFX_GAME_OVER);
            break;
        }
    }
}

static uint8_t direction(void)
{
    if (pressed & J_RIGHT)
        return DIR_RIGHT;
    if (pressed & J_LEFT)
        return DIR_LEFT;
    if (pressed & J_UP)
        return DIR_UP;
    if (pressed & J_DOWN)
        return DIR_DOWN;
    return DIR_NONE;
}

/* the pause; returns 1 if the game is to end */
static uint8_t pause(void)
{
    music_stop();
    sfx_play(SFX_PAUSE);
    render_hide_sprites();
    panel_open(6);
    panel_line(1, PAUSE_TITLE, 0);
    panel_line(3, PAUSE_RESUME, 0);
    panel_line(4, PAUSE_QUIT, 0);
    panel_show();
    for (;;) {
        frame();
        if (pressed & J_SELECT) {
            panel_close();
            return 1;
        }
        if (pressed & (J_START | J_A))
            break;
    }
    sfx_play(SFX_PAUSE);
    music_resume();
    phase_panel();
    return 0;
}

static void play(void)
{
    uint8_t phase, song;

    game_new(((uint32_t)frames << 8 | DIV_REG) * 2654435761UL);
    render_clear();
    render_song();
    phase_panel();
    music_start();
    for (;;) {
        frame();
        if ((pressed & J_START) && g.phase <= PHASE_CRASH) {
            if (pause())
                break;
            continue;
        }
        phase = g.phase;
        song = g.song;
        game_tick(direction(), (pressed & J_A) != 0);
        sounds();
        if (g.song != song)
            render_song();
        if (g.phase != phase || g.song != song)
            phase_panel();
        render_events();
        render_frame();
        if (g.done)
            break;
    }
    music_stop();
}

/* ---------------------------------------------------------------- name entry */

static const uint8_t charset[] = {
    CH(' '), CH('A'), CH('B'), CH('C'), CH('D'), CH('E'), CH('F'), CH('G'), CH('H'), CH('I'), CH('J'), CH('K'),
    CH('L'), CH('M'), CH('N'), CH('O'), CH('P'), CH('Q'), CH('R'), CH('S'), CH('T'), CH('U'), CH('V'), CH('W'),
    CH('X'), CH('Y'), CH('Z'), T_AE, T_OE, T_UE, CH('0'), CH('1'), CH('2'), CH('3'), CH('4'), CH('5'), CH('6'),
    CH('7'), CH('8'), CH('9'), CH('-'), CH('.'), CH('!'),
};

static uint8_t char_index(uint8_t t)
{
    uint8_t i;
    for (i = 0; i < sizeof(charset); i++)
        if (charset[i] == t)
            return i;
    return 0;
}

#define NAME_X 6
#define NAME_Y 9

static void entry(uint8_t rank, uint32_t score, uint8_t song)
{
    uint8_t name[NAME_LEN], pos = 0, i, k, held = 0, empty;

    memcpy(name, last_name, NAME_LEN);
    render_clear();
    print_center(0, 2, ENTRY_TITLE, 0);
    print_center(0, 4, ENTRY_RANK, rank + 1);
    print_center(0, 5, ENTRY_POINTS, score);
    print_center(0, 7, ENTRY_PROMPT, 0);
    print_center(0, 13, ENTRY_HELP, 0);
    for (;;) {
        for (i = 0; i < NAME_LEN; i++) {
            vset(0, NAME_X + i, NAME_Y, name[i]);
            vset(0, NAME_X + i, NAME_Y + 1, i == pos && (frames & 16) ? T_CURSOR : T_SPACE);
        }
        frame();
        if (pressed & J_START)
            break;
        k = char_index(name[pos]);
        /* up and down repeat while held */
        if (keys & (J_UP | J_DOWN)) {
            if (pressed & (J_UP | J_DOWN))
                held = 0;
            if (held == 0 || (held >= 20 && (held & 3) == 0)) {
                if (keys & J_UP)
                    k = k + 1 == sizeof(charset) ? 0 : k + 1;
                else
                    k = k ? k - 1 : sizeof(charset) - 1;
                name[pos] = charset[k];
                sfx_play(SFX_KEY);
            }
            held++;
        }
        if ((pressed & (J_RIGHT | J_A)) && pos < NAME_LEN - 1) {
            pos++;
            sfx_play(SFX_KEY);
        }
        if ((pressed & (J_LEFT | J_B)) && pos > 0) {
            pos--;
            sfx_play(SFX_KEY);
        }
    }
    empty = 1;
    for (i = 0; i < NAME_LEN; i++)
        if (name[i] != T_SPACE)
            empty = 0;
    if (empty) {
        name[0] = name[1] = name[2] = CH('?');
    } else {
        memcpy(last_name, name, NAME_LEN);
    }
    sfx_play(SFX_CLEARED);
    score_insert(rank, name, score, song);
}

void main(void)
{
    uint8_t rank;

    render_init();
    sound_init();
    save_load();
    for (;;) {
        title();
        play();
        last_score = g.score;
        last_played = 1;
        rank = score_rank(g.score);
        last_record = rank == 0;
        if (rank < SCORES)
            entry(rank, g.score, g.song);
    }
}
