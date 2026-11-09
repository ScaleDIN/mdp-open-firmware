#include <string.h>
#include "gfx.h"

void gfx_fill(int x, int y, int w, int h, uint16_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_W) w = GFX_W - x;
    if (y + h > GFX_H) h = GFX_H - y;
    if (w <= 0 || h <= 0) return;
    gfx_window((uint16_t)x, (uint16_t)y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    gfx_push(c, (uint32_t)w * (uint32_t)h);
}

void gfx_rect(int x, int y, int w, int h, uint16_t c)
{
    gfx_fill(x, y, w, 1, c);
    gfx_fill(x, y + h - 1, w, 1, c);
    gfx_fill(x, y, 1, h, c);
    gfx_fill(x + w - 1, y, 1, h, c);
}

static uint16_t blend(uint16_t fg, uint16_t bg, unsigned a)   /* a = 0..15 */
{
    unsigned r = ((fg >> 11) * a + (bg >> 11) * (15 - a) + 7) / 15;
    unsigned g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (15 - a) + 7) / 15;
    unsigned b = ((fg & 31) * a + (bg & 31) * (15 - a) + 7) / 15;
    return (uint16_t)(r << 11 | g << 5 | b);
}

static void glyph(const font_t *f, int x, int y, char ch, const uint16_t pal[16])
{
    if (x < 0 || y < 0 || x + f->w > GFX_W || y + f->h > GFX_H) return;
    uint8_t idx = f->map[(uint8_t)ch & 0x7F];
    gfx_window((uint16_t)x, (uint16_t)y, (uint16_t)(x + f->w - 1), (uint16_t)(y + f->h - 1));
    if (idx == 0xFF) { gfx_push(pal[0], (uint32_t)f->w * f->h); return; }
    const uint8_t *p = f->bits + (uint32_t)idx * f->glyph_bytes;
    uint32_t n = (uint32_t)f->w * f->h;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t v = p[i >> 1];
        gfx_push(pal[(i & 1) ? (v & 15) : (v >> 4)], 1);
    }
}

int gfx_text(const font_t *f, int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    uint16_t pal[16];
    for (unsigned a = 0; a < 16; a++) pal[a] = blend(fg, bg, a);
    for (; *s; s++, x += f->w) glyph(f, x, y, *s, pal);
    return x;
}

int gfx_text_w(const font_t *f, const char *s) { return (int)strlen(s) * f->w; }

void gfx_field(const font_t *f, int x, int y, int cells, const char *s, uint16_t fg, uint16_t bg)
{
    char b[48];
    int n = (int)strlen(s);
    if (cells > (int)sizeof b - 1) cells = sizeof b - 1;
    for (int i = 0; i < cells; i++) b[i] = i < n ? s[i] : ' ';
    b[cells] = 0;
    gfx_text(f, x, y, b, fg, bg);
}

void gfx_vline_clip(int x, int y0, int y1, int ytop, int ybot, uint16_t c)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < ytop) y0 = ytop;
    if (y1 > ybot) y1 = ybot;
    if (y1 >= y0) gfx_fill(x, y0, 1, y1 - y0 + 1, c);
}
