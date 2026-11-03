#!/usr/bin/env python3
"""
autoplay.py - plays Polonaise GB in PyBoy by itself, for tests: it reads the game state
from the RAM (addresses from build/polonaise.map), steers the line to the nearest guest on
a shortest free path, and takes screenshots now and then.

Environment: PROF=1 also prints a histogram of the lines (of 154 per frame) from the game tick
to the wait for the next frame; DUMP=1 dumps the tile maps at the end; SAVE=1 keeps the
cartridge RAM (high score list) in <rom>.ram.

Usage: tools/autoplay.py <rom> <out dir> [frames] [mistakes per 1000 steps]
"""
import collections
import os
import random
import re
import sys

from pyboy import PyBoy

ROM = sys.argv[1]
OUT = sys.argv[2]
FRAMES = int(sys.argv[3]) if len(sys.argv) > 3 else 20000
MISTAKES = int(sys.argv[4]) if len(sys.argv) > 4 else 0
COLS, ROWS = 19, 12
# screenshots every EVERY frames, and at the frames listed in AT (comma separated)
EVERY = int(os.environ.get('EVERY', 500))
AT = {int(v) for v in os.environ.get('AT', '').split(',') if v}
# every frame from GIF=start:end as g<frame>.png (for an animation)
GIF = [int(v) for v in os.environ.get('GIF', '0:0').split(':')]
os.makedirs(OUT, exist_ok=True)

sym = {}
for line in open(os.path.join(os.path.dirname(ROM), 'polonaise.map')):
    for m in re.finditer(r'([0-9A-F]{8})\s+(_\w+)', line):
        sym[m.group(2)] = int(m.group(1), 16)


def addr(name):
    return sym[name]


G, BODY, OCC, TILE = addr('_g'), addr('_body'), addr('_occ'), addr('_tile')
pyboy = PyBoy(ROM, window='null')
mem = pyboy.memory
rng = random.Random(1)
KEYS = ['right', 'down', 'left', 'up']
DX, DY = [1, 0, -1, 0], [0, 1, 0, -1]


def state():
    return dict(phase=mem[G + 4], song=mem[G + 8], score=int.from_bytes(bytes(mem[G + 9:G + 13]), 'little'),
                lives=mem[G + 13], length=mem[G + 21], moving=mem[G + 22], dir=mem[G + 23], queued=mem[G + 26])


def plan(st):
    """The direction of the first step to the nearest guest."""
    body = [mem[BODY + i] for i in range(st['length'])]
    tail = body[-1] if st['length'] > 2 else None
    head = body[0]
    prev = {head: None}
    q = collections.deque([head])
    while q:
        c = q.popleft()
        o = mem[OCC + c]
        if c != head and o and not o & 0x80:
            while prev[c] != head:
                c = prev[c]
            for d in range(4):
                x, y = head % COLS + DX[d], head // COLS + DY[d]
                if 0 <= x < COLS and 0 <= y < ROWS and y * COLS + x == c:
                    return d
        x, y = c % COLS, c // COLS
        for d in range(4):
            nx, ny = x + DX[d], y + DY[d]
            if not (0 <= nx < COLS and 0 <= ny < ROWS):
                continue
            n = ny * COLS + nx
            if n in prev or mem[TILE + n]:
                continue
            if mem[OCC + n] & 0x80 and n != tail:
                continue
            prev[n] = c
            q.append(n)
    return None


shots = 0
work = collections.Counter()
if os.environ.get('PROF'):
    t0 = [0]
    def at_tick(ctx):
        t0[0] = pyboy._cycles() // 456
    def at_vsync(ctx):
        if t0[0] and mem[G + 4] == 1:
            work[(pyboy._cycles() // 456 - t0[0]) // 10 * 10] += 1
        t0[0] = 0
    pyboy.hook_register(0, sym['_game_tic'], at_tick, None)
    pyboy.hook_register(0, sym['_vsync'], at_vsync, None)
lost = collections.Counter()
last_timer = None
held = None
waiting = 0
per_song = collections.defaultdict(lambda: [0, 0])
# the speed of the music per phase: 16ths played per frame, 2 per 15 frames at full speed
MUSIC = addr('_beat') - 1         # music_step (the map cuts the names after 8 characters)
per_phase = collections.defaultdict(lambda: [0, 0])
last_step = None
for f in range(FRAMES):
    if held:
        pyboy.button_release(held)
        held = None
    st = state()
    timer = mem[G + 5] | mem[G + 6] << 8
    if last_timer is not None and st['phase'] == 1 and timer >= last_timer:
        lost[timer - last_timer] += 1
        per_song[st['song']][0] += 1
        per_song[st['song']][1] += timer - last_timer
    last_timer = timer
    step = mem[MUSIC]
    if last_step is not None and st['phase'] < 4:
        per_phase[st['phase']][0] += 1
        per_phase[st['phase']][1] += (step - last_step) & 0xff
    last_step = step
    if f % 600 == 0:
        print(f, st, flush=True)
    if GIF[0] <= f < GIF[1]:
        pyboy.screen.image.convert('RGB').save(os.path.join(OUT, 'g%06d.png' % f))
    if f % EVERY == 0 or (st['phase'] in (2, 3, 4) and f % 60 == 0) or f in AT:
        pyboy.screen.image.convert('RGB').save(os.path.join(OUT, 'f%06d.png' % f))
    # title screen and panels: press start / A from time to time
    if f % 97 == 50:
        held = 'a'
    if f % 400 == 200 and st['phase'] != 1:
        held = 'start'
    if st['phase'] == 1 and not st['queued'] and f % 2 == 0:
        d = plan(st)
        if MISTAKES and rng.randrange(1000) < MISTAKES:
            d = rng.randrange(4)
        # standing for long (no way found): any direction
        waiting = waiting + 1 if not st['moving'] else 0
        if waiting > 60:
            d = rng.randrange(4)
        if d is not None and (d != st['dir'] or not st['moving']):
            held = KEYS[d]
    if held:
        pyboy.button_press(held)
    pyboy.tick()
print('end', state())
print('ticks per frame in PLAY:', sorted(lost.items()))
print('speed per song:', ' '.join('%d:%.0f%%' % (k, 100.0 * v[1] / v[0]) for k, v in sorted(per_song.items())))
print('music speed per phase:', ' '.join('%d:%.0f%%' % (k, 100.0 * v[1] / v[0] * 15 / 2)
                                         for k, v in sorted(per_phase.items()) if v[0] > 30))
if work:
    print('lines from game_tick to vsync:', sorted(work.items()))
if os.environ.get('DUMP'):
    for base, name in ((0x9800, 'bg'), (0x9c00, 'win')):
        print(name)
        for y in range(18):
            print('%2d' % y, ' '.join('%02x' % mem[base + y * 32 + x] for x in range(20)))
    print('LCDC %02x STAT %02x LYC %d WY %d' % (mem[0xff40], mem[0xff41], mem[0xff45], mem[0xff4a]))
    pyboy.screen.image.convert('RGB').resize((480, 432), 0).save(os.path.join(OUT, 'dump.png'))
pyboy.stop(save=bool(os.environ.get('SAVE')))
