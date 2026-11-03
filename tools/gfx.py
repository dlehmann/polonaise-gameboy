#!/usr/bin/env python3
"""
gfx.py - the graphics of Polonaise GB: all 8x8 tiles in the four shades of the Game Boy.

The background and the sprites share one table of up to 256 tiles (LCDC bit 4 set, tiles at
$8000). The tiles are drawn here from small pictures in text form ('.' white, '-' light gray,
'+' dark gray, '#' black, ' ' transparent or floor) and from the font Press Start 2P of the
browser version (../polonaise-remake).

The figures follow the people of the browser version (front view, hair styles, dresses,
glasses, the leader in a white disco suit), shrunk to one cell of 8x8 pixels: 12 looks with
4 frames each (legs, arms up), drawn onto the floor of the dance floor.

Output: gen/gfx.c, gen/gfx.h (tile numbers), build/tiles.png (a preview of all tiles)

Usage: tools/gfx.py <polonaise-remake dir> <gen dir> [preview.png]
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

SRC = sys.argv[1] if len(sys.argv) > 1 else '../polonaise-remake'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'gen'
PREVIEW = sys.argv[3] if len(sys.argv) > 3 else None

SHADE = {'.': 0, '-': 1, '+': 2, '#': 3}
tiles = []          # list of 64 shades (0..3) each
names = []          # (name, index) for gfx.h
extra = []          # extra C arrays (animation frames): (name, [tiles])


def pic(rows, colors=None, under=None):
    """A tile from 8 strings of 8 characters; ' ' shows `under` (a tile) or is 0."""
    out = []
    for y in range(8):
        row = rows[y] if y < len(rows) else ''
        row = row.ljust(8)
        for x in range(8):
            ch = row[x]
            if colors and ch in colors:
                ch = colors[ch]
            if ch == ' ':
                out.append(under[y * 8 + x] if under else 0)
            else:
                out.append(SHADE[ch])
    return out


def add(name, t):
    """Appends a tile and returns its number."""
    tiles.append(t)
    if name:
        names.append((name, len(tiles) - 1))
    return len(tiles) - 1


def encode(t):
    """2bpp Game Boy tile data: per row the low bits, then the high bits."""
    data = []
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            v = t[y * 8 + x]
            lo |= (v & 1) << (7 - x)
            hi |= (v >> 1) << (7 - x)
        data += [lo, hi]
    return data


# ---------------------------------------------------------------- font
# tiles 0..63: ASCII 32..95, white letters on black; then the umlauts and symbols

font = ImageFont.truetype(os.path.join(SRC, 'public/assets/fonts/PressStart2P-Regular.ttf'), 8)


def glyph(ch, w=8, h=8, dy=0):
    """The pixels of a character of Press Start 2P (1 where set)."""
    im = Image.new('1', (w, h + 8), 0)
    d = ImageDraw.Draw(im)
    d.text((0, dy), ch, font=font, fill=1)
    return [[im.getpixel((x, y)) for x in range(w)] for y in range(h)]


def char_tile(g):
    return [0 if g[y][x] else 3 for y in range(8) for x in range(8)]


for c in range(32, 96):
    add('T_SPACE' if c == 32 else None, char_tile(glyph(chr(c))))
names.append(('T_FONT', 0))

# the umlauts: the letter one row lower would not fit, so the dots sit in row 0 above a squeezed letter
for ch, base in (('AE', 'A'), ('OE', 'O'), ('UE', 'U')):
    g = glyph(base)
    t = [[0] * 8 for _ in range(8)]
    t[0][1] = t[0][2] = t[0][4] = t[0][5] = 1
    # rows 1..6 of the letter (drop its row 1 to keep the shape)
    src = [g[0], g[2], g[3], g[4], g[5], g[6]]
    for y in range(6):
        t[y + 1] = src[y]
    add('T_' + ch, char_tile(t))
add('T_SZ', char_tile(glyph('ß')))

add('T_HEART', pic([
    '########',
    '#.#.####',
    '...-.###',
    '....-###',
    '#..-####',
    '##-#####',
    '########',
    '########']))
add('T_DANCER', pic([
    '###..###',
    '###..###',
    '#.....##',
    '.#...#.#',
    '##...###',
    '##.#.###',
    '#..#..##',
    '########']))
add('T_X', char_tile(glyph('x')))
# the combo bar: 0..8 pixels filled
for n in range(9):
    add('T_BAR%d' % n, pic(['########'] + ['.' * n + '+' * (8 - n)] * 4 + ['########'] * 3))

# the frame of the panels (light lines on black)
add('T_BOX_TL', pic(['########', '########', '########', '###-----', '###-####', '###-####', '###-####',
                     '###-####']))
add('T_BOX_T', pic(['########', '########', '########', '--------', '########', '########', '########',
                    '########']))
add('T_BOX_TR', pic(['########', '########', '########', '-----###', '####-###', '####-###', '####-###',
                     '####-###']))
add('T_BOX_L', pic(['###-####'] * 8))
add('T_BOX_R', pic(['####-###'] * 8))
add('T_BOX_BL', pic(['###-####', '###-####', '###-####', '###-----', '########', '########', '########',
                     '########']))
add('T_BOX_B', pic(['########', '########', '########', '--------', '########', '########', '########',
                    '########']))
add('T_BOX_BR', pic(['####-###', '####-###', '####-###', '-----###', '########', '########', '########',
                     '########']))
# the cursor of the name entry
add('T_CURSOR', pic(['########'] * 6 + ['........', '########']))

# ---------------------------------------------------------------- the dance floor

FLOOR = pic([
    '+++++++#',
    '+++++++#',
    '+++++++#',
    '+++++++#',
    '+++++++#',
    '+++++++#',
    '+++++++#',
    '########'])
LIT = pic([
    '.------#',
    '-------#',
    '-------#',
    '-------#',
    '-------#',
    '-------#',
    '-------#',
    '########'])
add('T_FLOOR', FLOOR)
add('T_LIT', LIT)

# the frame: three LED tiles in turn along the top and bottom rows; the lights run with the beat
LED_SHADES = ['.', '-', '+']


def led(k, phase):
    """LED tile k (0..2) at a phase of the beat: two lights, shade cycling."""
    a = LED_SHADES[(2 * k + phase) % 3]
    b = LED_SHADES[(2 * k + 1 + phase) % 3]
    return pic(['########', '########', '########', '#' + a + a + '##' + b + b + '#',
                '#' + a + a + '##' + b + b + '#', '########', '########', '########'])


for k in range(3):
    add('T_LED%d' % k, led(k, 0))
extra.append(('anim_led', [led(k, p) for p in range(3) for k in range(3)]))
# the side walls: only half of each tile is on screen (the field is moved by 4 pixels)
add('T_WALL_L', pic(['########'] * 3 + ['#####--#', '#####--#'] + ['########'] * 3))
add('T_WALL_R', pic(['########'] * 3 + ['#--#####', '#--#####'] + ['########'] * 3))

# ---------------------------------------------------------------- people

# looks: hair style, hair, skin, shirt, pants, shoes, dress, glasses
LOOKS = [
    # the leader: white disco suit, sunglasses, gold chain
    dict(style='short', hair='#', skin='-', shirt='.', pants='.', shoes='.', glasses=True, chain=True),
    dict(style='long', hair='#', skin='.', shirt='-', pants='#', shoes='#', dress=True),
    dict(style='short', hair='-', skin='.', shirt='#', pants='-', shoes='.'),
    dict(style='curly', hair='#', skin='-', shirt='.', pants='#', shoes='#'),
    dict(style='bun', hair='-', skin='.', shirt='.', pants='#', shoes='#', dress=True),
    dict(style='mohawk', hair='.', skin='-', shirt='#', pants='#', shoes='.'),
    dict(style='cap', hair='.', skin='.', shirt='-', pants='#', shoes='#'),
    dict(style='ponytail', hair='#', skin='-', shirt='#', pants='-', shoes='#'),
    dict(style='long', hair='-', skin='-', shirt='#', pants='.', shoes='#', glasses=True),
    dict(style='short', hair='#', skin='.', shirt='-', pants='-', shoes='#'),
    dict(style='curly', hair='-', skin='.', shirt='#', pants='#', shoes='.', dress=True),
    dict(style='cap', hair='#', skin='-', shirt='.', pants='-', shoes='#'),
    dict(style='bun', hair='#', skin='.', shirt='#', pants='#', shoes='.', glasses=True),
]
GUEST_LOOKS = len(LOOKS) - 1

HAIR = {
    'short': ['  HHH  ', ' H   H '],
    'long': ['  HHH  ', ' H   H ', ' H   H '],
    'curly': [' HHHHH ', 'H     H'],
    'bun': ['  HHH  ', ' H   H '],
    'mohawk': ['   H   '],
    'cap': [' HHHH  ', '     HH'],
    'ponytail': ['  HHH  ', ' H   HH', '      H'],
}


def person(l, legs, arms_up, under):
    """A person of 7 x 8 pixels (the 8th column is the floor's line) on a floor tile."""
    g = [list('       ') for _ in range(8)]

    def put(rows, y0=0):
        for y, r in enumerate(rows):
            for x, ch in enumerate(r):
                if ch != ' ' and 0 <= y0 + y < 8:
                    g[y0 + y][x] = ch

    # head: rows 0..2
    put(['  SSS  ', '  ESE  ', '  SSS  '])
    if l.get('glasses'):
        put(['', ' GGGGG '])
    # body: rows 3..4, arms
    if arms_up:
        put(['S     S', 'T     T', 'T     T', 'TTTTTTT', ' TTTTT '])
    else:
        put(['', '', '', ' TTTTT ', 'TTTTTTT', 'S     S'])
    if l.get('chain'):
        put(['', '', '', '   C   '])
    # legs: rows 5..7
    if l.get('dress'):
        put([' TTTTT ', 'TTTTTTT', ' F  S  ' if legs else '  S  F '], 5)
    else:
        put(['  PPP  ', ' PP P  ' if legs else '  P PP ', ' F   FF' if legs else 'FF   F '], 5)
    put(HAIR[l['style']])
    colors = {'H': l['hair'], 'S': l['skin'], 'E': '#' if l['skin'] != '#' else '.', 'T': l['shirt'],
              'P': l['pants'], 'F': l['shoes'], 'G': '#', 'C': '-'}
    rows = [''.join(colors.get(ch, ch) for ch in r) + ' ' for r in g]
    return pic(rows, under=under)


# T_PEOPLE + look * 4 + (arms up) * 2 + legs
first = None
for i, l in enumerate(LOOKS):
    for frame in range(4):
        t = add(None, person(l, frame & 1, frame >> 1, FLOOR))
        if first is None:
            first = t
names.append(('T_PEOPLE', first))


def outline(t, under):
    """Black outline around the non-floor pixels of t on `under`."""
    out = list(t)
    for y in range(8):
        for x in range(7):
            if t[y * 8 + x] != under[y * 8 + x]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < 7 and 0 <= yy < 8 and t[yy * 8 + xx] != under[yy * 8 + xx]:
                    out[y * 8 + x] = 3
                    break
    return out


# the star guest: a golden person on a lit tile with a black outline
STAR = dict(style='curly', hair='.', skin='.', shirt='.', pants='-', shoes='.')
PLAIN = pic(['       #'] * 7 + ['########'], colors={' ': '+'})
for frame in range(4):
    p = person(STAR, frame & 1, frame >> 1, PLAIN)
    t = add('T_STAR' if frame == 0 else None, outline(p, PLAIN))
    # the lit floor around it
    tiles[t] = [LIT[k] if tiles[t][k] == PLAIN[k] and PLAIN[k] == 2 else tiles[t][k] for k in range(64)]

# ---------------------------------------------------------------- furniture

# the DJ desk: mixer and turntables, the DJ behind the middle one
add('T_DJ_MIX', pic(['+++++++#', '+++++++#', '+++++++#', '########', '--------', '#.#+#.##', '########',
                     '########']))
add('T_DJ_DECK', pic(['+++++++#', '+++++++#', '+++++++#', '########', '-#####-#', '#++.++##', '-#####-#',
                      '########']))
add('T_DJ_END', pic(['+++++++#', '+++++++#', '+++++++#', '########', '--------', '#+#+#+##', '########',
                     '########']))


def dj(up):
    if up:
        return pic(['.#+#+#.#', '-#.#.#-#', '-.###.-#', '########', '--------', '#.#+#.##', '########',
                    '########'])
    return pic(['+#+#+#+#', '+#.#.#+#', '-.###.-#', '########', '--------', '#.#+#.##', '########',
                '########'])


add('T_DJ', dj(False))
extra.append(('anim_dj', [dj(False), dj(True)]))
# a high table with a glass on it
add('T_TABLE', pic([
    '+++++.+#',
    '+++#..##',
    '+######+',
    '#......#',
    '+#----#+',
    '+++#.#+#',
    '+++#.#+#',
    '++#####+']))


def speaker(big):
    cone = ['#++++#', '+#--#+', '+-..-+', '+-..-+', '+#--#+', '#++++#'] if big else \
        ['#++++#', '#+--+#', '#-..-#', '#-..-#', '#+--+#', '#++++#']
    return pic(['#######+', '#-##-##+'] + ['#' + r + '+' for r in cone[1:]] + ['#######+'])


add('T_SPEAKER', speaker(False))
extra.append(('anim_speaker', [speaker(False), speaker(True)]))
add('T_BAR_A', pic([
    '+++.+++#',
    '+++.+++#',
    '++#.#.-#',
    '########',
    '--------',
    '++++++++',
    '++++++++',
    '########']))
add('T_BAR_B', pic([
    '+++++#+#',
    '++++#.##',
    '++++#.##',
    '########',
    '--------',
    '++++++++',
    '++++++++',
    '########']))

# ---------------------------------------------------------------- the logo: POLONAISE in big letters


def big(ch):
    """A letter of 16 x 16 pixels: the font doubled, white above, light gray below, a dark shadow."""
    g = glyph(ch)
    px = [[3] * 16 for _ in range(16)]
    for y in range(8):
        for x in range(8):
            if g[y][x]:
                for yy in range(2):
                    for xx in range(2):
                        sx, sy = x * 2 + xx + 1, y * 2 + yy + 1
                        if sx < 16 and sy < 16 and px[sy][sx] == 3:
                            px[sy][sx] = 2
    for y in range(8):
        for x in range(8):
            if g[y][x]:
                for yy in range(2):
                    for xx in range(2):
                        sy = y * 2 + yy
                        px[sy][x * 2 + xx] = 0 if sy < 7 else 1
    return [[px[ty * 8 + y][tx * 8 + x] for y in range(8) for x in range(8)] for ty in range(2) for tx in range(2)]


logo = {}
for ch in 'POLNAISE':
    q = big(ch)
    logo[ch] = [add('T_BIG_' + ch if k == 0 else None, q[k]) for k in range(4)]

# ---------------------------------------------------------------- sprites (color 0 is transparent)
# sprite palette OBP0: 0 transparent, 1 white, 2 light, 3 black

SPR = {' ': '.', 'w': '-', 'l': '+', 'k': '#'}
DIGITS = {
    '0': ['www', 'wkw', 'wkw', 'wkw', 'www'], '1': [' w ', 'ww ', ' w ', ' w ', 'www'],
    '2': ['www', '  w', 'www', 'w  ', 'www'], '3': ['www', '  w', ' ww', '  w', 'www'],
    '4': ['w w', 'w w', 'www', '  w', '  w'], '5': ['www', 'w  ', 'www', '  w', 'www'],
    '6': ['www', 'w  ', 'www', 'w w', 'www'], '7': ['www', '  w', ' w ', ' w ', ' w '],
    '8': ['www', 'w w', 'www', 'w w', 'www'], '9': ['www', 'w w', 'www', '  w', 'www'],
    '+': ['   ', ' w ', 'www', ' w ', '   '], 'X': ['w w', 'w w', ' w ', 'w w', 'w w'],
}


def small(rows):
    """A small character (3 x 5) with a black outline, in a sprite tile."""
    grid = [[' '] * 8 for _ in range(8)]
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch == 'w':
                grid[y + 1][x + 1] = 'w'
    for y in range(8):
        for x in range(8):
            if grid[y][x] == ' ' and any(0 <= y + dy < 8 and 0 <= x + dx < 8 and grid[y + dy][x + dx] == 'w'
                                         for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                grid[y][x] = 'k'
    return pic([''.join(SPR[c] for c in r) for r in grid])


for i, ch in enumerate('0123456789+X'):
    add('S_DIGIT' if i == 0 else None, small(DIGITS[ch]))
add('S_BANG', pic([''.join(SPR[c] for c in r) for r in [
    ' kk     ', ' kwk    ', ' kwk    ', ' kwk    ', ' kk     ', ' kwk    ', ' kk     ', '        ']]))
add('S_SWEAT', pic([''.join(SPR[c] for c in r) for r in [
    '        ', ' k      ', 'klk     ', 'kwk     ', ' k      ', '        ', '        ', '        ']]))
add('S_SPARK', pic([''.join(SPR[c] for c in r) for r in [
    '        ', '   w    ', '   w    ', ' wwlww  ', '   w    ', '   w    ', '        ', '        ']]))
add('S_SPARK2', pic([''.join(SPR[c] for c in r) for r in [
    '        ', '        ', '  w w   ', '   l    ', '  w w   ', '        ', '        ', '        ']]))
add('S_DIZZY', pic([''.join(SPR[c] for c in r) for r in [
    '   k    ', '  kwk   ', 'kkwwwkk ', ' kwwwk  ', ' kwkwk  ', ' kk kk  ', '        ', '        ']]))
add('S_CONF1', pic([''.join(SPR[c] for c in r) for r in [
    'ww      ', 'ww      ', '        ', '        ', '        ', '        ', '        ', '        ']]))
add('S_CONF2', pic([''.join(SPR[c] for c in r) for r in [
    'l       ', ' l      ', '        ', '        ', '        ', '        ', '        ', '        ']]))
add('S_ARROW_R', pic([''.join(SPR[c] for c in r) for r in [
    '        ', ' kk     ', ' kwk    ', ' kwwk   ', ' kwk    ', ' kk     ', '        ', '        ']]))
add('S_ARROW_D', pic([''.join(SPR[c] for c in r) for r in [
    '        ', '        ', 'kkkkk   ', 'kwwwk   ', ' kwk    ', '  k     ', '        ', '        ']]))
add('S_ARROW_L', pic([''.join(SPR[c] for c in r) for r in [
    '        ', '   kk   ', '  kwk   ', ' kwwk   ', '  kwk   ', '   kk   ', '        ', '        ']]))
add('S_ARROW_U', pic([''.join(SPR[c] for c in r) for r in [
    '        ', '  k     ', ' kwk    ', 'kwwwk   ', 'kkkkk   ', '        ', '        ', '        ']]))

assert len(tiles) <= 256, len(tiles)

# ---------------------------------------------------------------- output

os.makedirs(OUT, exist_ok=True)


def carray(name, data, per=16):
    s = 'const uint8_t %s[%d] = {\n' % (name, len(data))
    for i in range(0, len(data), per):
        s += '    ' + ','.join('0x%02x' % v for v in data[i:i + per]) + ',\n'
    return s + '};\n'


with open(os.path.join(OUT, 'gfx.c'), 'w') as f:
    f.write('/* generated by tools/gfx.py */\n#include <stdint.h>\n#include "gfx.h"\n\n')
    f.write(carray('gfx_tiles', sum((encode(t) for t in tiles), [])))
    for n, ts in extra:
        f.write(carray(n, sum((encode(t) for t in ts), [])))
with open(os.path.join(OUT, 'gfx.h'), 'w') as f:
    f.write('/* generated by tools/gfx.py */\n#ifndef GFX_H\n#define GFX_H\n#include <stdint.h>\n\n')
    f.write('#define GFX_TILES %d\n#define GUEST_LOOKS %d\n' % (len(tiles), GUEST_LOOKS))
    for n, i in names:
        f.write('#define %s %d\n' % (n, i))
    f.write('\nextern const uint8_t gfx_tiles[];\n')
    for n, ts in extra:
        f.write('extern const uint8_t %s[%d];\n' % (n, len(ts) * 16))
    f.write('\n#endif\n')

if PREVIEW:
    pal = [(224, 248, 208), (136, 192, 112), (52, 104, 86), (8, 24, 32)]
    cols = 16
    im = Image.new('RGB', (cols * 9, ((len(tiles) + cols - 1) // cols) * 9), (255, 0, 255))
    for i, t in enumerate(tiles):
        for k, v in enumerate(t):
            im.putpixel(((i % cols) * 9 + k % 8, (i // cols) * 9 + k // 8), pal[v])
    im = im.resize((im.width * 4, im.height * 4), Image.NEAREST)
    im.save(PREVIEW)
print('%d tiles' % len(tiles))
