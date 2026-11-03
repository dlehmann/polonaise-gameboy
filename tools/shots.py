#!/usr/bin/env python3
"""
shots.py - the screenshots and the animation of Polonaise GB for the README and the web page,
in the green of the original Game Boy screen, at twice the size.

It plays the ROM in PyBoy: the title screens by a script of button presses, the game by
tools/autoplay.py (a good player, and one that makes many mistakes, for the crash, the game
over and the name entry). Both are deterministic, so the frames below always show the same.

Output: docs/img/*.png, docs/img/gameplay.gif

Usage: tools/shots.py <rom>
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image
from pyboy import PyBoy

ROM = sys.argv[1] if len(sys.argv) > 1 else 'build/polonaise.gb'
OUT = 'docs/img'
PY = sys.executable
SCALE = 2
# the shades of PyBoy -> the greens of the DMG screen
GREEN = {(255, 255, 255): (0xc4, 0xcf, 0xa1), (153, 153, 153): (0x8b, 0x95, 0x6d),
         (85, 85, 85): (0x4d, 0x53, 0x3c), (0, 0, 0): (0x1f, 0x1f, 0x1f)}

# the good player: name -> frame
GOOD = {'cleared': 1800, 'intro': 1920, 'song4': 3480, 'song6': 5610, 'song7': 6840}
GIF = (5400, 5700)
# the player with many mistakes (600 of 1000 steps)
BAD = {'crash': 860, 'pause': 1020, 'gameover': 1300, 'entry': 1660, 'scores': 1820, 'help': 1940}


def green(im):
    im = im.convert('RGB')
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            px[x, y] = GREEN.get(px[x, y], px[x, y])
    return im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)


def save(im, name):
    green(im).save(os.path.join(OUT, name + '.png'), optimize=True)
    print(name)


def titles():
    """The title screen in German and English."""
    p = PyBoy(ROM, window='null')
    p.tick(150, True)
    save(p.screen.image, 'title')
    p.button('select', 2)
    p.tick(60, True)
    save(p.screen.image, 'title-en')
    p.stop(save=False)


def autoplay(frames, mistakes, shots, gif=None):
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, EVERY='1000000', AT=','.join(str(f) for f in shots.values()))
        if gif:
            env['GIF'] = '%d:%d' % gif
        subprocess.run([PY, 'tools/autoplay.py', ROM, tmp, str(frames), str(mistakes)], env=env, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for name, f in shots.items():
            save(Image.open(os.path.join(tmp, 'f%06d.png' % f)), name)
        if gif:
            frames = [green(Image.open(os.path.join(tmp, 'g%06d.png' % f))) for f in range(gif[0], gif[1], 2)]
            pal = [frames[0].quantize(colors=4)]
            frames = [fr.quantize(palette=pal[0]) for fr in frames]
            frames[0].save(os.path.join(OUT, 'gameplay.gif'), save_all=True, append_images=frames[1:],
                           duration=33, loop=0, optimize=True)
            print('gameplay.gif')


os.makedirs(OUT, exist_ok=True)
titles()
autoplay(max(GOOD.values()) + 1, 0, GOOD, GIF)
autoplay(max(BAD.values()) + 1, 600, BAD)
