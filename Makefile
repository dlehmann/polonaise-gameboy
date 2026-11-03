# Polonaise GB - builds build/polonaise.gb
LCC = tools/gbdk/bin/lcc
PY = tools/venv/bin/python
SRC = ../polonaise-remake

GEN = gen/gfx.c gen/text.c gen/music.c
CSRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h) gen/gfx.h gen/text.h gen/music.h
# MBC1 with battery-backed RAM (8 KB) for the high score list
CFLAGS = -Isrc -Igen -Wa-l -Wl-m -Wl-yt0x03 -Wl-ya1 -Wm-yn"POLONAISE"

all: build/polonaise.gb

gen/gfx.c gen/gfx.h &: tools/gfx.py
	mkdir -p build
	$(PY) tools/gfx.py $(SRC) gen build/tiles.png

gen/text.c gen/text.h &: tools/text.py gen/gfx.h
	$(PY) tools/text.py gen

gen/music.c gen/music.h &: tools/music.py
	$(PY) tools/music.py gen

build/polonaise.gb: $(CSRC) $(GEN) $(HDR)
	mkdir -p build
	$(LCC) $(CFLAGS) -o $@ $(CSRC) $(GEN)
	cp $@ polonaise.gb
	cp $@ docs/polonaise.gb

# the screenshots and the animation for the README and the web page (docs/img)
shots: build/polonaise.gb
	$(PY) tools/shots.py build/polonaise.gb

clean:
	rm -rf build gen polonaise.gb

.PHONY: all clean shots
