/*
 * game.c - Polonaise game logic, a port of src/game.ts of the browser version (see game.h).
 *
 * Beyond the original: occ[] keeps for every cell whether the line or a guest stands on it,
 * so that the searches of the original (which member, which guest is on a cell) take no time
 * on the Game Boy.
 */
#include "game.h"

Game g;
uint8_t tile[CELLS];
uint8_t occ[CELLS];
uint8_t body[CELLS];
uint8_t look[CELLS];
Guest guests[MAX_GUESTS];
/* the slots in use are below this one (the loops over the guests stop there) */
uint8_t guest_slots;
uint8_t cell_x[CELLS];
uint8_t cell_y[CELLS];

#define FIRST_INTRO_TICKS (20 * TICK_HZ)
#define INTRO_TICKS (4 * TICK_HZ)
#define CRASH_TICKS (3 * TICK_HZ / 2)
#define CLEARED_TICKS (7 * TICK_HZ)
#define GAME_OVER_TICKS (6 * TICK_HZ)
#define SPAWN_DISTANCE 4
#define START_X 9
#define START_Y 9

/*
 * Floor layouts, one per song (repeating): '.' floor, 'D' DJ desk, 'T' high table,
 * 'S' speaker, 'B' bar counter. The start cell and its row stay free.
 */
#define E "..................."
static const char * const layouts[8][ROWS] = {
    {E, E, E, E, E, E, E, E, E, E, E, E},
    {".......DDDDD.......", E, E, E, E, E, E, E, E, E, E, E},
    {".......DDDDD.......", E, E, "....T.........T....", E, E, E, E, "....T.........T....", E, E, E},
    {"SS.....DDDDD.....SS", "SS...............SS", E, E, E, E, E, E, E, E,
     "SS...............SS", "SS...............SS"},
    {".......DDDDD.......", E, E, E, "...T...........T...", E, E, "...T...........T...", E, E, E,
     ".....BBBBBBBBB....."},
    {"SS.....DDDDD.....SS", "SS...............SS", E, E, E, "........TTT........", "........TTT........",
     E, E, E, "SS...............SS", "SS...............SS"},
    {".......DDDDD.......", E, E, ".....B.......B.....", ".....B.......B.....", ".....B.......B.....",
     ".....B.......B.....", ".....B.......B.....", ".....B.......B.....", E, E, E},
    {"SS.....DDDDD.....SS", "SS...............SS", "..T......T......T..", E, E, ".....T.......T.....",
     E, E, E, E, "..T......T......T..", E},
};

/* ticks per step for the songs 1, 2, ...; later songs use STEP_MIN */
static const uint8_t step_ticks[] = {10, 9, 9, 8, 8, 7, 7, 6, 6, 6, 6};
#define STEP_MIN 5
/* shy guests in percent for the songs 1, 2, ...; later songs use the last value */
static const uint8_t shy_percent[] = {0, 25, 30, 35, 40, 45, 50};
#define TARGET_START 8
#define TARGET_STEP 2
#define TARGET_MAX 24
#define GUESTS_MAX 6
static const uint32_t extra_lives[] = {15000, 40000, 80000, 150000};

/* cells for spawn_cell() */
static uint8_t cand[CELLS];

void song_get(uint8_t n, Song *s)
{
    uint16_t t;

    if (n < 1)
        n = 1;
    t = TARGET_START + TARGET_STEP * (uint16_t)(n - 1);
    s->target = t > TARGET_MAX ? TARGET_MAX : t;
    s->step_ticks = n <= sizeof(step_ticks) ? step_ticks[n - 1] : STEP_MIN;
    t = 3 + (n - 1) / 2;
    s->guests = t > GUESTS_MAX ? GUESTS_MAX : t;
    s->shy_percent = shy_percent[(n < sizeof(shy_percent) ? n : sizeof(shy_percent)) - 1];
    s->layout = (n - 1) & 7;
}

uint32_t extra_life_score(uint8_t n)
{
    return n < 4 ? extra_lives[n] : 0;
}

uint16_t intro_ticks(uint8_t song)
{
    return song == 1 ? FIRST_INTRO_TICKS : INTRO_TICKS;
}

uint8_t start_cell(void)
{
    return START_Y * COLS + START_X;
}

uint8_t next_cell(uint8_t cell, uint8_t d)
{
    uint8_t x = cell_x[cell], y = cell_y[cell];

    switch (d) {
    case DIR_RIGHT:
        return x == COLS - 1 ? NO_CELL : cell + 1;
    case DIR_DOWN:
        return y == ROWS - 1 ? NO_CELL : cell + COLS;
    case DIR_LEFT:
        return x == 0 ? NO_CELL : cell - 1;
    case DIR_UP:
        return y == 0 ? NO_CELL : cell - COLS;
    }
    return cell;
}

/* the Manhattan distance of two cells */
static uint8_t distance(uint8_t a, uint8_t b)
{
    uint8_t dx = cell_x[a] > cell_x[b] ? cell_x[a] - cell_x[b] : cell_x[b] - cell_x[a];
    uint8_t dy = cell_y[a] > cell_y[b] ? cell_y[a] - cell_y[b] : cell_y[b] - cell_y[a];
    return dx + dy;
}

/* a pseudo random number (the upper 16 of 24 bits) */
static uint32_t rnd(void)
{
    /* (a 16 bit xorshift: the linear congruential generator of the original needs a 32 bit
     * multiplication, which takes too long here) */
    uint16_t x = (uint16_t)g.rng;

    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    g.rng = x;
    return (uint32_t)x << 8;
}

static void event(uint8_t type, uint8_t cell, uint8_t count, uint8_t index, uint16_t points)
{
    Event *e;

    if (g.nevents >= MAX_EVENTS)
        return;
    e = &g.events[g.nevents++];
    e->type = type;
    e->cell = cell;
    e->count = count;
    e->index = index;
    e->points = points;
}

static void set_phase(uint8_t p)
{
    g.phase = p;
    g.timer = 0;
}

/* a guest leaves the floor */
static void guest_gone(Guest *gu)
{
    gu->active = 0;
    while (guest_slots && !guests[guest_slots - 1].active)
        guest_slots--;
}

/* the slot of the guest on a cell, or -1 */
static int8_t guest_at(uint8_t cell)
{
    uint8_t o = occ[cell];
    return o && !(o & OCC_MEMBER) ? (int8_t)(o - 1) : -1;
}

/* whether a cell is inside the field and empty */
static uint8_t cell_free(uint8_t cell)
{
    return cell != NO_CELL && tile[cell] == TILE_FLOOR && !occ[cell];
}

/* adds points and hands out the extra lives they earn */
static void add_score(uint32_t points)
{
    uint32_t e;

    g.score += points;
    while ((e = extra_life_score(g.extra_lives)) && g.score >= e) {
        g.extra_lives++;
        if (g.lives < LIVES_MAX) {
            g.lives++;
            event(EV_EXTRA_LIFE, body[0], g.lives, 0, 0);
        }
    }
}

/* a random number below n (n <= 256), without a division: random bytes until one fits */
static uint8_t rnd_below(uint16_t n)
{
    uint8_t mask = 255, r;

    while (mask >> 1 >= n - 1 && mask > 1)
        mask >>= 1;
    do
        r = (uint8_t)(rnd() >> 8) & mask;
    while (r >= n);
    return r;
}

/*
 * A random free cell away from the leader and not right in front of it; NO_CELL if none.
 * Unlike the original, which lists all such cells, it tries random cells first (the floor
 * is mostly free): the same choice for the player, but fast enough on the Game Boy.
 */
static uint8_t spawn_cell(void)
{
    uint8_t ahead = g.moving ? next_cell(body[0], g.dir) : NO_CELL, head = body[0];
    uint8_t c, n = 0, k, hx = cell_x[head], hy = cell_y[head], x, y;

    for (k = 0; k < 24; k++) {
        c = rnd_below(CELLS);
        if (tile[c] || occ[c] || c == ahead)
            continue;
        x = cell_x[c];
        y = cell_y[c];
        if ((x > hx ? x - hx : hx - x) + (y > hy ? y - hy : hy - y) >= SPAWN_DISTANCE)
            return c;
    }
    for (c = 0; c < CELLS; c++)
        if (!tile[c] && !occ[c] && c != ahead && distance(c, head) >= SPAWN_DISTANCE)
            cand[n++] = c;
    if (!n)
        for (c = 0; c < CELLS; c++)
            if (!tile[c] && !occ[c] && c != ahead)
                cand[n++] = c;
    if (!n)
        return NO_CELL;
    return cand[rnd_below(n)];
}

/* puts a guest on a cell (with a random look unless one is given); the slot or -1 */
static int8_t add_guest(uint8_t type, uint8_t cell, uint8_t lk)
{
    int8_t slot;
    Guest *gu;

    for (slot = 0; slot < MAX_GUESTS && guests[slot].active; slot++)
        ;
    if (slot == MAX_GUESTS || cell == NO_CELL)
        return -1;
    gu = &guests[slot];
    gu->active = 1;
    gu->type = type;
    gu->cell = cell;
    gu->moved = TICK_HZ;
    gu->age = 0;
    gu->look = lk ? lk : 1 + rnd_below(LOOKS - 1);
    occ[cell] = slot + 1;
    if (slot >= guest_slots)
        guest_slots = slot + 1;
    return slot;
}

/* keeps at least the song's number of guests (without stars) on the floor */
static void fill_guests(void)
{
    uint8_t i, n = 0, t;
    Guest *gu = guests;

    for (i = 0; i < guest_slots; i++, gu++)
        if (gu->active && gu->type != GUEST_STAR)
            n++;
    for (; n < g.s.guests; n++) {
        t = rnd_below(100) < g.s.shy_percent ? GUEST_SHY : GUEST_DANCER;
        if (add_guest(t, spawn_cell(), 0) < 0)
            break;
    }
}

/* sets up the floor of the current song: furniture, the lone leader, fresh guests */
static void start_song(void)
{
    uint8_t x, y, c = 0, i;
    char ch;

    song_get(g.song, &g.s);
    for (y = 0; y < ROWS; y++)
        for (x = 0; x < COLS; x++, c++) {
            ch = layouts[g.s.layout][y][x];
            tile[c] = ch == 'D' ? TILE_DJ : ch == 'T' ? TILE_TABLE : ch == 'S' ? TILE_SPEAKER :
                      ch == 'B' ? TILE_BAR : TILE_FLOOR;
            occ[c] = 0;
        }
    g.length = 1;
    body[0] = start_cell();
    occ[body[0]] = OCC_MEMBER;
    look[0] = 0;
    g.moving = 0;
    g.dir = DIR_RIGHT;
    g.queued = 0;
    g.step_timer = 0;
    for (i = 0; i < MAX_GUESTS; i++)
        guests[i].active = 0;
    guest_slots = 0;
    g.star_timer = STAR_EVERY;
    g.combo = 0;
    g.combo_timer = 0;
    g.song_crashes = 0;
    fill_guests();
    set_phase(PHASE_INTRO);
    event(EV_SONG_START, body[0], g.song, 0, 0);
}

/* the line is long enough: the bonus is added and the summary screen begins */
static void finish_song(void)
{
    uint32_t bonus;

    g.bonus_followers = followers();
    g.bonus_line = POINTS_FOLLOWER * g.bonus_followers;
    g.bonus_song = POINTS_SONG * (uint16_t)g.song;
    g.bonus_perfect = g.song_crashes ? 0 : POINTS_PERFECT;
    bonus = (uint32_t)g.bonus_line + g.bonus_song + g.bonus_perfect;
    add_score(bonus);
    g.moving = 0;
    set_phase(PHASE_CLEARED);
    event(EV_CLEARED, body[0], g.bonus_followers, 0, bonus > 65535 ? 65535 : bonus);
}

/*
 * The leader ran into something: a life and the back half of the line are lost. The
 * guests who let go stay where they are and dance again.
 */
static void crash(void)
{
    uint8_t drop = (followers() + 1) / 2, k, i;

    for (k = 0; k < drop; k++) {
        i = g.length - 1 - k;
        occ[body[i]] = 0;
        add_guest(GUEST_DANCER, body[i], look[i]);
    }
    g.length -= drop;
    g.dropped = drop;
    g.lives--;
    g.song_crashes++;
    g.moving = 0;
    g.queued = 0;
    g.grace = 0;
    g.combo = 0;
    g.combo_timer = 0;
    event(EV_CRASH, body[0], drop, g.lives, 0);
    if (g.lives == 0) {
        set_phase(PHASE_GAME_OVER);
        event(EV_GAME_OVER, body[0], 0, 0, 0);
    } else {
        set_phase(PHASE_CRASH);
    }
}

/* the leader touched a guest: points (times the combo) and a longer combo */
static void catch_guest(uint8_t slot)
{
    Guest *gu = &guests[slot];
    uint16_t points = gu->type == GUEST_STAR ? POINTS_STAR : gu->type == GUEST_SHY ? POINTS_SHY : POINTS_DANCER;

    g.combo = g.combo_timer > 0 ? (g.combo < COMBO_MAX ? g.combo + 1 : COMBO_MAX) : 1;
    g.combo_timer = COMBO_TICKS;
    points *= g.combo;
    guest_gone(gu);
    event(EV_CATCH, gu->cell, g.combo, gu->type, points);
    add_score(points);
}

/* whether the leader's next step onto a cell is blocked; the end of the line moves on, so it
 * does not count (unless the line is so short that the leader would turn around) */
static uint8_t step_blocked(uint8_t cell)
{
    if (cell == NO_CELL || tile[cell] != TILE_FLOOR)
        return 1;
    if (!(occ[cell] & OCC_MEMBER))
        return 0;
    return !(g.length > 2 && cell == body[g.length - 1]);
}

/* one step of the line in its direction (after a buffered turn) */
static void step(void)
{
    uint8_t target, tail, i;
    int8_t slot;

    if (g.queued) {
        uint8_t d = g.queue[0];
        g.queue[0] = g.queue[1];
        g.queued--;
        if (d != g.dir)
            event(EV_TURN, body[0], 0, d, 0);
        g.dir = d;
    }
    target = next_cell(body[0], g.dir);
    if (step_blocked(target)) {
        /* one step of grace: the line stands still and a turn can still save it */
        if (g.grace) {
            crash();
            return;
        }
        g.grace = 1;
        event(EV_GRACE, body[0], 0, g.dir, 0);
        return;
    }
    g.grace = 0;
    slot = guest_at(target);
    tail = body[g.length - 1];
    if (slot >= 0) {
        /* the new member appears at the end of the line, where the last one stood */
        body[g.length] = tail;
        look[g.length] = guests[slot].look;
    } else {
        occ[tail] = 0;
    }
    for (i = g.length - 1; i > 0; i--)
        body[i] = body[i - 1];
    body[0] = target;
    occ[target] = OCC_MEMBER;
    g.steps++;
    if (slot < 0)
        return;
    g.length++;
    catch_guest(slot);
    if (followers() >= g.s.target)
        finish_song();
    else
        fill_guests();
}

/* shy guests close to the leader step to the free neighbour farthest from it (at half the
 * leader's speed); among equally far ones, to the one with the most free neighbours */
static void flee(void)
{
    uint8_t head = body[0], i, d, e, c, dist, best, best_d, nfree, flee_ticks = 2 * g.s.step_ticks;
    int8_t best_free;
    Guest *gu;

    for (i = 0, gu = guests; i < guest_slots; i++, gu++) {
        if (!gu->active || gu->type != GUEST_SHY || gu->moved < flee_ticks)
            continue;
        best_d = distance(gu->cell, head);
        if (best_d > FLEE_DISTANCE)
            continue;
        best = NO_CELL;
        best_free = -1;
        for (d = 0; d < 4; d++) {
            c = next_cell(gu->cell, d);
            if (!cell_free(c))
                continue;
            dist = distance(c, head);
            nfree = 0;
            for (e = 0; e < 4; e++)
                nfree += cell_free(next_cell(c, e));
            if (dist > best_d || (dist == best_d && best != NO_CELL && (int8_t)nfree > best_free)) {
                best = c;
                best_d = dist;
                best_free = nfree;
            }
        }
        if (best == NO_CELL)
            continue;
        occ[gu->cell] = 0;
        gu->cell = best;
        occ[best] = i + 1;
        gu->moved = 0;
        event(EV_FLEE, best, 0, i, 0);
    }
}

/* old stars leave; from song 2 a new one appears now and then while the line moves */
static void stars(void)
{
    uint8_t i, star = 0;
    int8_t slot;
    Guest *gu = guests;

    for (i = 0; i < guest_slots; i++, gu++)
        if (gu->active && gu->type == GUEST_STAR) {
            if (gu->age >= STAR_TICKS) {
                guest_gone(gu);
                occ[gu->cell] = 0;
                event(EV_STAR_GONE, gu->cell, 0, i, 0);
            } else {
                star = 1;
            }
        }
    if (g.song < 2 || !g.moving || --g.star_timer > 0)
        return;
    g.star_timer = STAR_EVERY;
    if (star)
        return;
    slot = add_guest(GUEST_STAR, spawn_cell(), 0);
    if (slot >= 0)
        event(EV_STAR, guests[slot].cell, 0, slot, 0);
}

/* the line waits: a free direction starts it, a blocked one is refused */
static void go(uint8_t d)
{
    uint8_t target = next_cell(body[0], d);

    if (step_blocked(target) || (g.length > 1 && target == body[1])) {
        event(EV_DENY, body[0], 0, d, 0);
        return;
    }
    g.dir = d;
    g.queued = 0;
    g.grace = 0;
    g.moving = 1;
    g.step_timer = 0;
    event(EV_GO, body[0], 0, d, 0);
    step();
}

/* while moving, direction changes are queued for the next steps; a reversal is ignored */
static void turn(uint8_t d)
{
    uint8_t last = g.queued ? g.queue[g.queued - 1] : g.dir;

    if (d == last || (g.length > 1 && d == ((last + 2) & 3)) || g.queued >= TURN_QUEUE)
        return;
    g.queue[g.queued++] = d;
}

/* a tick of the PLAY phase */
static void play(uint8_t d)
{
    uint8_t i;
    Guest *gu = guests;

    for (i = 0; i < guest_slots; i++, gu++) {
        if (gu->active) {
            if (gu->moved < 255)
                gu->moved++;
            if (gu->age < 65535U)
                gu->age++;
        }
    }
    if (!g.moving) {
        if (d != DIR_NONE)
            go(d);
        if (g.phase != PHASE_PLAY)
            return;
        stars();
        return;
    }
    if (d != DIR_NONE)
        turn(d);
    if (g.combo_timer > 0)
        g.combo_timer--;
    if (++g.step_timer >= g.s.step_ticks) {
        g.step_timer = 0;
        step();
        if (g.phase != PHASE_PLAY)
            return;
    }
    flee();
    stars();
}

/* starts a game at song 1; the seed selects the random sequence */
void game_new(uint32_t seed)
{
    uint8_t c = 0, x, y;

    for (y = 0; y < ROWS; y++)
        for (x = 0; x < COLS; x++, c++) {
            cell_x[c] = x;
            cell_y[c] = y;
        }
    g.rng = (uint16_t)(seed ^ seed >> 16) ? (uint16_t)(seed ^ seed >> 16) : 0x3141;
    g.phase = PHASE_INTRO;
    g.timer = 0;
    g.done = 0;
    g.song = 1;
    g.score = 0;
    g.lives = LIVES_START;
    g.extra_lives = 0;
    g.song_crashes = 0;
    g.grace = 0;
    g.steps = 0;
    g.combo = 0;
    g.combo_timer = 0;
    g.dropped = 0;
    g.nevents = 0;
    start_song();
}

/* jumps to another song (for tests) */
void game_start_at(uint8_t song)
{
    g.song = song ? song : 1;
    g.nevents = 0;
    start_song();
}

/*
 * Advances the game by one tick.
 * d: the direction pressed in this tick, or DIR_NONE; a: whether A was pressed in this tick
 */
void game_tick(uint8_t d, uint8_t a)
{
    uint8_t i;
    Guest *gu;

    g.nevents = 0;
    g.timer++;
    switch (g.phase) {
    case PHASE_INTRO:
        if (g.timer >= intro_ticks(g.song) || (g.timer >= SKIP_TICKS && (a || d != DIR_NONE))) {
            set_phase(PHASE_PLAY);
            event(EV_PLAY, body[0], 0, 0, 0);
            if (d != DIR_NONE)
                go(d);
        }
        break;
    case PHASE_PLAY:
        play(d);
        break;
    case PHASE_CRASH:
        for (i = 0, gu = guests; i < guest_slots; i++, gu++)
            if (gu->active && gu->moved < 255)
                gu->moved++;
        if (g.timer >= CRASH_TICKS)
            set_phase(PHASE_PLAY);
        break;
    case PHASE_CLEARED:
        if (g.timer >= CLEARED_TICKS || (g.timer >= SKIP_TICKS && a)) {
            g.song++;
            start_song();
        }
        break;
    case PHASE_GAME_OVER:
        if (g.timer >= GAME_OVER_TICKS)
            g.done = 1;
        break;
    }
}
