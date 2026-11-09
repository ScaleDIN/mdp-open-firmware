/* Milestone 2: pair with the P906 and show its live status (read-only polling).
 * Packets sent: type 5 (call for ID), type 6 (assign address/channel, pairing
 * only), type 7 status polls without a command. Output and set values are
 * never changed. */
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "lcd.h"
#include "input.h"
#include "radio.h"
#include "mdp.h"
#include "config.h"

/* Address/channel given to the P906 when pairing: same as this unit's
 * RX_ADDR.TXT (Addr1=b1:61:a:29:21, Freq1=2416 MHz), kept in register order. */
static const uint8_t PAIR_ADDR[5] = { 0xB1, 0x61, 0x0A, 0x29, 0x21 };
#define PAIR_CH      16
#define MATCH_CH     78                     /* P906 listens here in "Match RX Addr.." */
static const uint8_t MATCH_ADDR[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
#define PAIR_TIMEOUT_MS 30000

/* Read back over SWD to check state without looking at the screen. */
struct dbg {
    uint32_t magic;           /* 'M01F' */
    uint32_t lcd_id;
    uint32_t core_clock;
    uint32_t uptime_ms;
    uint32_t radio_ok;
    uint32_t linked;
    uint32_t cfg_from_flash;
    uint32_t pair_attempts, pair_result;   /* result: 0 none, 1 ok, 2 timeout, 3 no dispatch reply, 4 no poll after */
    uint8_t id[4];
    uint32_t polls_ok, polls_fail, last_fail;
    mdp_status_t st;
    uint8_t last_frame[32];
    uint8_t resp6[32];        /* P906's reply to the address assignment */
    uint32_t pair_order, pair_ms;
} volatile dbg __attribute__((used));

static link_cfg_t cfg;
static char buf[48];

static void line(int row, const char *s, uint16_t fg)
{
    char padded[24];
    snprintf(padded, sizeof padded, "%-20.20s", s);
    lcd_text(0, 32 + row * 20, padded, 2, fg, C_DARK);
}

static void small(int row, const char *s, uint16_t fg)
{
    char padded[48];
    snprintf(padded, sizeof padded, "%-40.40s", s);
    lcd_text(0, 32 + row * 20, padded, 1, fg, C_DARK);
}

static void clear_body(void) { lcd_fill(0, 30, lcd_w, lcd_h - 30, C_DARK); }

static void use_link(void)
{
    radio_set_channel(cfg.channel);
    radio_set_address(cfg.addr);
    memcpy((void *)dbg.id, cfg.id, 4);
}

static bool poll(mdp_status_t *st, xch_t *res)
{
    uint8_t req[32], resp[32];
    mdp_poll7(req, cfg.id, 0);
    xch_t r = mdp_exchange(req, 7, resp, 30);
    if (res) *res = r;
    if (r == XCH_OK && mdp_parse7(resp, st)) {
        memcpy((void *)dbg.last_frame, resp, 32);
        return true;
    }
    return false;
}

/* Stock-compatible Auto Match. Returns true when paired, linked and saved. */
static bool pair(void)
{
    uint8_t req[32], resp[32], id[4];
    dbg.pair_attempts++;
    clear_body();
    line(0, "PAIRING", C_YELLOW);
    small(2, "On the P906: open the menu, page 2/6", C_WHITE);
    small(3, "'TX and RX Addr', and start", C_WHITE);
    small(4, "'Match RX Addr..'. Keep units close.", C_WHITE);
    small(6, "Press E again to cancel.", C_GREY);
    keys_pressed();

    radio_set_channel(MATCH_CH);
    radio_set_address(MATCH_ADDR);

    uint32_t t0 = millis(), shown = 0xFFFFFFFFu;
    bool got = false;
    while (millis() - t0 < PAIR_TIMEOUT_MS) {
        if (keys_pressed() & (1u << KEY_E)) break;
        uint32_t left = (PAIR_TIMEOUT_MS - (millis() - t0)) / 1000u;
        if (left != shown) {
            shown = left;
            snprintf(buf, sizeof buf, "searching... %2lus", (unsigned long)left);
            line(1, buf, C_CYAN);
        }
        mdp_call_id(req);
        if (mdp_exchange(req, 5, resp, 30) == XCH_OK && resp[1] == 4) {
            memcpy(id, resp + 2, 4);
            got = true;
            break;
        }
    }
    if (!got) { dbg.pair_result = 2; return false; }

    snprintf(buf, sizeof buf, "found ID %02X%02X%02X%02X", id[0], id[1], id[2], id[3]);
    line(1, buf, C_GREEN);

    /* assign our address/channel; P906 answers with type 6 */
    bool assigned = false;
    for (int i = 0; i < 40 && !assigned; i++) {
        mdp_dispatch(req, PAIR_ADDR, PAIR_CH);
        assigned = mdp_exchange(req, 6, resp, 30) == XCH_OK;
    }
    if (!assigned) { dbg.pair_result = 3; line(1, "P906 DIDN'T CONFIRM", C_RED); radio_set_channel(cfg.channel); return false; }

    memcpy((void *)dbg.resp6, resp, 32);
    line(1, "assigned, connecting", C_GREEN);

    /* The P906 may store the address byte-reversed relative to the dispatch
     * payload, and may take a while to switch: try both orders for 10 s. */
    cfg.channel = PAIR_CH;
    memcpy(cfg.id, id, 4);
    mdp_status_t st;
    bool ok = false;
    for (uint32_t t = millis(); !ok && millis() - t < 10000; ) {
        for (int o = 0; o < 2 && !ok; o++) {
            for (int i = 0; i < 5; i++) cfg.addr[i] = o ? PAIR_ADDR[4 - i] : PAIR_ADDR[i];
            use_link();
            ok = poll(&st, NULL);
            if (ok) { dbg.pair_order = (uint32_t)o; dbg.pair_ms = millis() - t; }
        }
    }
    if (!ok) { dbg.pair_result = 4; line(1, "NO REPLY ON NEW ADDR", C_RED); return false; }

    config_save(&cfg);
    dbg.pair_result = 1;
    beep(40); delay_ms(120); beep(40);
    return true;
}

static void fmt_milli(char *o, size_t n, uint32_t v)
{
    snprintf(o, n, "%2lu.%03lu", (unsigned long)(v / 1000), (unsigned long)(v % 1000));
}

static void draw_live_frame(void)
{
    clear_body();
    snprintf(buf, sizeof buf, "CH %u  ADDR %02X%02X%02X%02X%02X  ID %02X%02X%02X%02X", cfg.channel,
             cfg.addr[0], cfg.addr[1], cfg.addr[2], cfg.addr[3], cfg.addr[4],
             cfg.id[0], cfg.id[1], cfg.id[2], cfg.id[3]);
    small(0, buf, C_GREY);
    small(11, "E = pair again", C_GREY);
}

int main(void)
{
    dbg.magic = 0x4630314Du;
    dbg.core_clock = SystemCoreClock;
    beep(60);
    dbg.lcd_id = lcd_init();
    lcd_fill(0, 0, lcd_w, lcd_h, C_DARK);
    lcd_fill(0, 0, lcd_w, 28, C_BLUE);
    lcd_text(8, 6, "P906 LINK", 2, C_WHITE, C_BLUE);
    lcd_text(170, 10, "READ-ONLY TEST", 1, C_YELLOW, C_BLUE);

    dbg.cfg_from_flash = config_load(&cfg);
    if (!dbg.cfg_from_flash) {
        memcpy(cfg.addr, PAIR_ADDR, 5);
        cfg.channel = PAIR_CH;
        memset(cfg.id, 0, 4);
    }
    dbg.radio_ok = radio_init(cfg.channel);
    if (!dbg.radio_ok) { line(1, "RADIO SPI FAIL", C_RED); for (;;) { } }
    use_link();

    static const char *const states[] = { "OFF", "CC", "CV", "ON" };
    bool framed = false;
    uint32_t next = 0;
    for (;;) {
        uint32_t now = millis();
        dbg.uptime_ms = now;
        uint8_t k = keys_pressed();
        if (k & (1u << KEY_E)) {
            pair();
            delay_ms(800);
            framed = false;
            use_link();
            continue;
        }
        if (k) beep(15);
        if ((int32_t)(now - next) < 0) continue;
        next = now + 100;

        if (!dbg.cfg_from_flash && !dbg.pair_result) {     /* never paired by us */
            if (!framed) {
                clear_body();
                line(1, "NOT PAIRED", C_RED);
                small(3, "Press E to pair with the P906.", C_WHITE);
                framed = true;
            }
            continue;
        }
        if (!framed) { draw_live_frame(); framed = true; }

        mdp_status_t st;
        xch_t r;
        if (poll(&st, &r)) {
            dbg.linked = 1;
            dbg.polls_ok++;
            dbg.st = st;
            GPIOB->ODR ^= 1u << LED_BLUE_PIN;
            char a[12], b[12];
            line(1, "LINK OK", C_GREEN);
            if (st.has_set) {
                fmt_milli(a, sizeof a, st.set_mV); fmt_milli(b, sizeof b, st.set_mA);
                snprintf(buf, sizeof buf, "SET %sV %sA", a, b);
                line(3, buf, C_CYAN);
            }
            fmt_milli(a, sizeof a, st.in_mV); fmt_milli(b, sizeof b, st.in_mA);
            snprintf(buf, sizeof buf, "IN  %sV %sA", a, b);
            line(4, buf, C_WHITE);
            snprintf(buf, sizeof buf, "OUT %-3s  %2u.%uC", states[st.state & 3],
                     st.temp_dC / 10, st.temp_dC % 10);
            line(5, buf, st.state ? C_YELLOW : C_GREY);
            snprintf(buf, sizeof buf, "ERR %u LOCK %u", st.err, st.locked);
            line(6, buf, st.err ? C_RED : C_GREY);
            snprintf(buf, sizeof buf, "ADC V %4u %4u %4u %4u", st.adc_v[0], st.adc_v[1], st.adc_v[2], st.adc_v[3]);
            small(8, buf, C_GREY);
            snprintf(buf, sizeof buf, "ADC I %4u %4u %4u %4u", st.adc_i[0], st.adc_i[1], st.adc_i[2], st.adc_i[3]);
            small(9, buf, C_GREY);
        } else {
            dbg.linked = 0;
            dbg.polls_fail++;
            dbg.last_fail = r;
            line(1, r == XCH_NOACK ? "NO ACK" : "NO REPLY", C_RED);
        }
        snprintf(buf, sizeof buf, "polls ok %lu  fail %lu",
                 (unsigned long)dbg.polls_ok, (unsigned long)dbg.polls_fail);
        small(10, buf, C_GREY);
    }
}
