#!/usr/bin/env python3
"""
art.py - pictures for the web page from the artwork of the C64 version of Polonaise: the mirror
ball and the club poster from its header picture (../polonaise/docs/hero.webp).

Output: docs/img/ball.webp, docs/img/poster.webp

Usage: tools/art.py <polonaise dir>
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

C64 = sys.argv[1] if len(sys.argv) > 1 else '../polonaise'
OUT = 'docs/img'

hero = Image.open(os.path.join(C64, 'docs/hero.webp')).convert('RGB')

# the mirror ball, round, with a soft edge
cx, cy, r = 800, 148, 90
ball = hero.crop((cx - r, cy - r, cx + r, cy + r)).convert('RGBA')
mask = Image.new('L', (r * 8, r * 8), 0)
ImageDraw.Draw(mask).ellipse((10, 10, r * 8 - 10, r * 8 - 10), fill=255)
mask = mask.resize((r * 2, r * 2), Image.LANCZOS).filter(ImageFilter.GaussianBlur(0.6))
ball.putalpha(mask)
ball.save(os.path.join(OUT, 'ball.webp'), quality=90, method=6)

hero.save(os.path.join(OUT, 'poster.webp'), quality=82, method=6)
print('ok')
