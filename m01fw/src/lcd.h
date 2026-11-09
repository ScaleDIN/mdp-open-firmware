#pragma once
#include <stdint.h>

#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_RED    RGB565(255, 0, 0)
#define C_GREEN  RGB565(0, 255, 0)
#define C_BLUE   RGB565(0, 0, 255)
#define C_YELLOW RGB565(255, 220, 0)
#define C_CYAN   RGB565(0, 220, 255)
#define C_GREY   RGB565(90, 90, 90)
#define C_DARK   RGB565(24, 28, 36)

extern uint16_t lcd_w, lcd_h;

uint32_t lcd_init(void);             /* returns ID from 0xD3 (expect 0x9341) */
void lcd_set_rotation(uint8_t madctl);
void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c);
void lcd_text(uint16_t x, uint16_t y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg);
