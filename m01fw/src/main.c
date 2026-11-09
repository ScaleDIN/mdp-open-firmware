/* Milestone 1: bring-up test screen (LCD, keys, encoders, LED, buzzer). */
#include <stdio.h>
#include "board.h"
#include "lcd.h"
#include "input.h"

/* Read back over SWD to check the board without looking at it. */
struct dbg {
    uint32_t magic;       /* 'M01F' */
    uint32_t lcd_id;
    uint32_t core_clock;
    uint32_t uptime_ms;
    uint32_t keys;
    int32_t enc_menu, enc_value;
    uint32_t enc_raw;
} volatile dbg __attribute__((used));

static void draw_static(void)
{
    lcd_fill(0, 0, lcd_w, lcd_h, C_DARK);
    lcd_fill(0, 0, lcd_w, 28, C_BLUE);
    lcd_text(8, 6, "M01 OPEN FW", 2, C_WHITE, C_BLUE);
    lcd_text(200, 10, "BRING-UP", 1, C_YELLOW, C_BLUE);
    /* orientation markers */
    lcd_fill(0, 28, 12, 12, C_RED);
    lcd_text(14, 30, "TOP-LEFT", 1, C_RED, C_DARK);
    lcd_fill(lcd_w - 12, lcd_h - 12, 12, 12, C_GREEN);
    /* colour bars */
    static const uint16_t bars[] = { C_RED, C_GREEN, C_BLUE, C_WHITE, C_YELLOW, C_CYAN };
    for (int i = 0; i < 6; i++) lcd_fill(16 + i * 40, 200, 40, 24, bars[i]);
    lcd_text(16, 48, "KEYS", 2, C_GREY, C_DARK);
    lcd_text(16, 112, "MENU ENC", 1, C_GREY, C_DARK);
    lcd_text(170, 112, "VALUE ENC", 1, C_GREY, C_DARK);
}

int main(void)
{
    dbg.magic = 0x4630314Du;
    dbg.core_clock = SystemCoreClock;
    beep(60);
    dbg.lcd_id = lcd_init();
    draw_static();

    char buf[32];
    snprintf(buf, sizeof buf, "LCD ID %04lX  %luMHz", (unsigned long)dbg.lcd_id,
             (unsigned long)(SystemCoreClock / 1000000u));
    lcd_text(16, 176, buf, 1, C_GREY, C_DARK);

    uint8_t last_keys = 0xFF;
    int32_t last_m = 1, last_v = 1;
    uint32_t next_blink = 0;

    for (;;) {
        uint32_t now = millis();
        dbg.uptime_ms = now;
        uint8_t k = keys_state();
        int32_t m = enc_count(ENC_MENU), v = enc_count(ENC_VALUE);
        dbg.keys = k; dbg.enc_menu = m; dbg.enc_value = v;
        dbg.enc_raw = (uint32_t)(enc_raw(ENC_MENU) << 4) | enc_raw(ENC_VALUE);

        if (keys_pressed()) beep(15);

        if (k != last_keys) {
            last_keys = k;
            for (int i = 0; i < KEY_COUNT; i++) {
                bool on = (k >> i) & 1;
                char s[2] = { (char)('A' + i), 0 };
                uint16_t x = 16 + i * 56;
                lcd_fill(x, 72, 48, 32, on ? C_YELLOW : C_GREY);
                lcd_text(x + 16, 80, s, 2, C_BLACK, on ? C_YELLOW : C_GREY);
            }
        }
        if (m != last_m || v != last_v) {
            if (m != last_m || v != last_v) beep(3);
            last_m = m; last_v = v;
            snprintf(buf, sizeof buf, "%+5ld", (long)m);
            lcd_text(16, 128, buf, 3, C_CYAN, C_DARK);    /* 5 chars x 24 px */
            snprintf(buf, sizeof buf, "%+5ld", (long)v);
            lcd_text(170, 128, buf, 3, C_CYAN, C_DARK);
        }
        if ((int32_t)(now - next_blink) >= 0) {   /* blue heartbeat */
            next_blink = now + 500;
            GPIOB->ODR ^= 1u << LED_BLUE_PIN;
        }
    }
}
