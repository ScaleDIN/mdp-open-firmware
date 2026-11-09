#pragma once
/* Hardware-independent drawing on top of a "window + pixel stream" backend.
 * Device backend: lcd.c (ILI9341). Host backend: sim/host_lcd.c (PNG screenshots). */
#include <stdint.h>
#include <stdbool.h>

#define GFX_W 320
#define GFX_H 240

#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

typedef struct {
    uint8_t w, h;              /* fixed cell size */
    uint16_t glyph_bytes;      /* 4 bpp, 2 pixels per byte, row-major */
    const uint8_t *map;        /* ASCII -> glyph index, 0xFF = missing */
    const uint8_t *bits;
} font_t;

/* backend */
void gfx_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void gfx_push(uint16_t c, uint32_t n);

void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_rect(int x, int y, int w, int h, uint16_t c);         /* 1 px outline */
/* text in fixed cells, anti-aliased against a solid bg; returns x after the text */
int gfx_text(const font_t *f, int x, int y, const char *s, uint16_t fg, uint16_t bg);
int gfx_text_w(const font_t *f, const char *s);
/* text padded/truncated to exactly `cells` characters (stable layout, no flicker) */
void gfx_field(const font_t *f, int x, int y, int cells, const char *s, uint16_t fg, uint16_t bg);
void gfx_vline_clip(int x, int y0, int y1, int ytop, int ybot, uint16_t c);
