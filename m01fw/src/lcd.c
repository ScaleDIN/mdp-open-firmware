/* ILI9341 (Z280IT002 module) on FSMC bank 1 NE1, 16-bit 8080 bus, RS = A18. */
#include "board.h"
#include "lcd.h"
#include "gfx.h"

#define LCD_CMD (*(volatile uint16_t *)0x6007FFFEu)   /* A18 = 0 */
#define LCD_DAT (*(volatile uint16_t *)0x60080000u)   /* A18 = 1 */

uint16_t lcd_w = 320, lcd_h = 240;

static void cmd(uint8_t c) { LCD_CMD = c; }
static void dat(uint8_t d) { LCD_DAT = d; }

static void fsmc_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_FSMCEN;

    /* PD0,1,4,5,7,8,9,10,13,14,15 and PE7..15 as AF push-pull */
    static const uint8_t pd[] = { 0, 1, 4, 5, 7, 8, 9, 10, 13, 14, 15 };
    for (unsigned i = 0; i < sizeof pd; i++) gpio_cfg(GPIOD, pd[i], GPIO_AF_PP);
    for (unsigned p = 7; p <= 15; p++) gpio_cfg(GPIOE, p, GPIO_AF_PP);

    /* SRAM mode A, 16 bit, write enable, separate (extended) write timing.
     * HCLK 72 MHz = 13.9 ns.
     * Read:  ADDSET 1, DATAST 32 (~460 ns, ILI9341 tRC for GRAM is 450 ns)
     * Write: ADDSET 1, DATAST 4  (~70 ns cycle, ILI9341 tWC min 66 ns) */
    FSMC_Bank1->BTCR[0] = FSMC_BCRx_MBKEN | FSMC_BCRx_MWID_0 | FSMC_BCRx_WREN | FSMC_BCRx_EXTMOD;
    FSMC_Bank1->BTCR[1] = (1u << 0) | (32u << 8);
    FSMC_Bank1E->BWTR[0] = (1u << 0) | (4u << 8);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    cmd(0x2A); dat(x0 >> 8); dat(x0 & 0xFF); dat(x1 >> 8); dat(x1 & 0xFF);
    cmd(0x2B); dat(y0 >> 8); dat(y0 & 0xFF); dat(y1 >> 8); dat(y1 & 0xFF);
    cmd(0x2C);
}

void lcd_set_rotation(uint8_t madctl)
{
    cmd(0x36); dat(madctl);
    if (madctl & 0x20) { lcd_w = 320; lcd_h = 240; }   /* MV: landscape */
    else               { lcd_w = 240; lcd_h = 320; }
}

static const uint8_t init_seq[] = {
    /* cmd, n, data... ; standard ILI9341 power-up values */
    0xEF, 3, 0x03, 0x80, 0x02,
    0xCF, 3, 0x00, 0xC1, 0x30,
    0xED, 4, 0x64, 0x03, 0x12, 0x81,
    0xE8, 3, 0x85, 0x00, 0x78,
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
    0xF7, 1, 0x20,
    0xEA, 2, 0x00, 0x00,
    0xC0, 1, 0x23,          /* power control 1 */
    0xC1, 1, 0x10,          /* power control 2 */
    0xC5, 2, 0x3E, 0x28,    /* VCOM 1 */
    0xC7, 1, 0x86,          /* VCOM 2 */
    0x3A, 1, 0x55,          /* 16 bpp */
    0xB1, 2, 0x00, 0x18,    /* frame rate 79 Hz */
    0xB6, 3, 0x08, 0x82, 0x27,
    0xF2, 1, 0x00,
    0x26, 1, 0x01,
    0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    0x00
};

uint32_t lcd_init(void)
{
    fsmc_init();

    gpio_clr(LCD_BL_PORT, LCD_BL_PIN);
    gpio_cfg(LCD_BL_PORT, LCD_BL_PIN, GPIO_OUT_PP_2M);
    gpio_cfg(LCD_RST_PORT, LCD_RST_PIN, GPIO_OUT_PP_2M);
    gpio_clr(LCD_RST_PORT, LCD_RST_PIN);
    delay_ms(20);
    gpio_set(LCD_RST_PORT, LCD_RST_PIN);
    delay_ms(120);

    cmd(0xD3);                       /* read ID4: dummy, 0x00, 0x93, 0x41 */
    (void)LCD_DAT; (void)LCD_DAT;
    uint32_t id = (uint32_t)(LCD_DAT & 0xFF) << 8;
    id |= LCD_DAT & 0xFF;

    cmd(0x01); delay_ms(120);        /* software reset */
    for (const uint8_t *p = init_seq; *p; ) {
        cmd(*p++);
        uint8_t n = *p++;
        while (n--) dat(*p++);
    }
    lcd_set_rotation(0xE8);          /* landscape: MY | MX | MV | BGR (0x28 showed upside down) */
    cmd(0x11); delay_ms(120);        /* sleep out */
    cmd(0x29);                       /* display on */

    lcd_fill(0, 0, lcd_w, lcd_h, C_BLACK);
    gpio_set(LCD_BL_PORT, LCD_BL_PIN);
    return id;
}

void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    if (x >= lcd_w || y >= lcd_h) return;
    if (w > lcd_w - x) w = lcd_w - x;   /* clip: the panel wraps out-of-window writes */
    if (h > lcd_h - y) h = lcd_h - y;
    if (!w || !h) return;
    set_window(x, y, x + w - 1, y + h - 1);
    for (uint32_t n = (uint32_t)w * h; n; n--) LCD_DAT = c;
}

/* gfx.h backend */
void gfx_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) { set_window(x0, y0, x1, y1); }

void gfx_push(uint16_t c, uint32_t n)
{
    while (n--) LCD_DAT = c;
}
