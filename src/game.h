/*
 * game.h - Polonaise game logic: a conga line on a disco dance floor, steered like Snake.
 *
 * A port of src/game.ts of the browser version (itself a port of game.c of the arcade
 * cabinet): the same time base of 60 ticks per second (one per frame on the Game Boy), the
 * same songs, floors, points, extra lives and random number generator. It does not touch
 * the hardware; tick() takes the input of one tick and reports what happened as events.
 *
 * The field is a grid of COLS x ROWS cells, addressed by a cell index (y * COLS + x) that
 * fits in a byte; NO_CELL stands for "outside".
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#define TICK_HZ 60
#define SKIP_TICKS TICK_HZ
#define COLS 19
#define ROWS 12
#define CELLS (COLS * ROWS)
#define NO_CELL 255
#define MAX_GUESTS 32
#define TURN_QUEUE 2
#define LOOKS 64

#define LIVES_START 3
#define LIVES_MAX 5
#define COMBO_TICKS (4 * TICK_HZ)
#define COMBO_MAX 5
#define STAR_TICKS (8 * TICK_HZ)
#define STAR_EVERY (15 * TICK_HZ)
#define FLEE_DISTANCE 3

#define POINTS_DANCER 100
#define POINTS_SHY 200
#define POINTS_STAR 500
#define POINTS_FOLLOWER 100
#define POINTS_SONG 1000
#define POINTS_PERFECT 2000

/* directions; (d + 2) & 3 is the reverse */
#define DIR_RIGHT 0
#define DIR_DOWN 1
#define DIR_LEFT 2
#define DIR_UP 3
#define DIR_NONE 4

/* what stands on a cell */
#define TILE_FLOOR 0
#define TILE_DJ 1
#define TILE_TABLE 2
#define TILE_SPEAKER 3
#define TILE_BAR 4

#define GUEST_DANCER 0
#define GUEST_SHY 1
#define GUEST_STAR 2

#define PHASE_INTRO 0
#define PHASE_PLAY 1
#define PHASE_CRASH 2
#define PHASE_CLEARED 3
#define PHASE_GAME_OVER 4

/* events */
#define EV_TURN 0
#define EV_GO 1
#define EV_DENY 2
#define EV_GRACE 3
#define EV_CATCH 4
#define EV_FLEE 5
#define EV_STAR 6
#define EV_STAR_GONE 7
#define EV_CRASH 8
#define EV_EXTRA_LIFE 9
#define EV_SONG_START 10
#define EV_PLAY 11
#define EV_CLEARED 12
#define EV_GAME_OVER 13

#define MAX_EVENTS 8

/* occupation of a cell: the line, or a guest (slot + 1 in the low bits) */
#define OCC_MEMBER 0x80
#define OCC_GUEST 0x3f

/*
 * Something that happened in a tick. For CATCH: count is the combo multiplier, index the
 * guest type, points the points; for CRASH: count the guests who let go, index the lives left.
 */
typedef struct {
    uint8_t type;
    uint8_t cell;
    uint8_t count;
    uint8_t index;
    uint16_t points;
} Event;

typedef struct {
    uint8_t active;
    uint8_t type;
    uint8_t cell;
    uint8_t look;
    /* ticks since the last step (saturating), since it appeared */
    uint8_t moved;
    uint16_t age;
    /* (8 bytes: guests[i] needs no multiplication) */
    uint8_t pad;
} Guest;

typedef struct {
    uint8_t target;
    uint8_t step_ticks;
    uint8_t guests;
    uint8_t shy_percent;
    uint8_t layout;
} Song;

typedef struct {
    uint32_t rng;
    uint8_t phase;
    uint16_t timer;
    uint8_t done;
    uint8_t song;
    uint32_t score;
    uint8_t lives;
    uint8_t extra_lives;
    uint8_t song_crashes;
    Song s;
    uint8_t length;
    uint8_t moving;
    uint8_t dir;
    uint8_t queue[TURN_QUEUE];
    uint8_t queued;
    uint8_t step_timer;
    uint8_t grace;
    /* steps taken in this run (for the walking animation) */
    uint8_t steps;
    uint16_t star_timer;
    uint8_t combo;
    uint16_t combo_timer;
    uint8_t dropped;
    uint16_t bonus_followers;
    uint16_t bonus_line;
    uint16_t bonus_song;
    uint16_t bonus_perfect;
    uint8_t nevents;
    Event events[MAX_EVENTS];
} Game;

extern Game g;
/* the furniture per cell */
extern uint8_t tile[CELLS];
/* OCC_MEMBER or the guest slot + 1 per cell */
extern uint8_t occ[CELLS];
/* the cells of the line, body[0] is the leader, and the looks */
extern uint8_t body[CELLS];
extern uint8_t look[CELLS];
extern Guest guests[MAX_GUESTS];
extern uint8_t guest_slots;
/* the column and row of each cell */
extern uint8_t cell_x[CELLS];
extern uint8_t cell_y[CELLS];

void game_new(uint32_t seed);
void game_start_at(uint8_t song);
void game_tick(uint8_t d, uint8_t a);
void song_get(uint8_t n, Song *s);
uint32_t extra_life_score(uint8_t n);
uint16_t intro_ticks(uint8_t song);
uint8_t start_cell(void);
uint8_t next_cell(uint8_t cell, uint8_t d);
#define followers() (g.length - 1)

#endif
