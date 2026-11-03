/*
 * render.h - the screen of Polonaise GB: dance floor, status lines, panels and effects.
 */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

/* the field starts in map row 2; the map is moved 4 pixels to the right (SCX = -4) */
#define FIELD_ROW 2
#define ROW_TOP 0
#define ROW_SONG 15
#define ROW_COMBO 16
#define ROW_MSG 17

extern uint8_t lang;
extern uint16_t ticks;

void render_init(void);
void render_clear(void);
void render_song(void);
void render_frame(void);
void render_events(void);
void render_hud_reset(void);
void render_hide_sprites(void);
void render_message(uint8_t text, uint8_t frames);

/* safe writes to the video RAM while the display is on */
void vset(uint8_t win, uint8_t x, uint8_t y, uint8_t t);
void vfill(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t t);
void vrow(uint8_t x, uint8_t y, uint8_t n, const uint8_t *t);
void vdata(uint8_t first, uint8_t n, const uint8_t *data);

/* text output: on the background (win = 0) or in the window (win = 1) */
void print(uint8_t win, uint8_t x, uint8_t y, uint8_t text, uint32_t num);
void print_center(uint8_t win, uint8_t y, uint8_t text, uint32_t num);
void print_num(uint8_t win, uint8_t x, uint8_t y, uint32_t num, uint8_t digits);
uint8_t text_lines(uint8_t text);

/* panels: a box of h rows in the window at the bottom of the screen */
void panel_open(uint8_t h);
void panel_show(void);
void panel_close(void);
void panel_line(uint8_t y, uint8_t text, uint32_t num);
void panel_value(uint8_t y, uint8_t text, uint32_t label_num, uint32_t value);

#endif
