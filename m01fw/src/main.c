/* M01 open firmware: radio link to the P906 + user interface (ui.c). */
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "lcd.h"
#include "gfx.h"
#include "input.h"
#include "radio.h"
#include "mdp.h"
#include "config.h"
#include "ui.h"

/* Address/channel given to the P906 when pairing: same as this unit's stock
 * RX_ADDR.TXT (Addr1=b1:61:a:29:21, Freq1=2416 MHz), in register order. */
static const uint8_t PAIR_ADDR[5] = { 0xB1, 0x61, 0x0A, 0x29, 0x21 };
#define PAIR_CH      16
#define MATCH_CH     78                     /* P906 listens here in "Match RX Addr.." */
static const uint8_t MATCH_ADDR[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
#define PAIR_TIMEOUT_MS 30000
#define LED_SLOT0_COLOR 0x0012              /* stock M01 settings 0x0803C030: slot 0 colour */
#define POLL_MS 100
#define LINK_LOST_FAILS 5

#define COL_INFO RGB(90, 200, 255)
#define COL_OK   RGB(60, 200, 100)
#define COL_BAD  RGB(235, 60, 60)

/* Read back over SWD to check state without looking at the screen. */
struct dbg {
    uint32_t magic;           /* 'M01F' */
    uint32_t lcd_id, core_clock, uptime_ms;
    uint32_t radio_ok, linked, paired, pair_result;
    uint8_t id[4];
    uint32_t polls_ok, polls_fail, cmds_ok, cmds_fail;
    uint32_t cal_ok, out_mV, out_mA;
    mdp_status_t st;
    mdp_cal_t cal;
} volatile dbg __attribute__((used));

static link_cfg_t cfg;
static bool paired;
static mdp_cal_t cal;

void ui_hal_beep(uint32_t ms) { beep(ms); }

static void use_link(void)
{
    radio_set_channel(cfg.channel);
    radio_set_address(cfg.addr);
    memcpy((void *)dbg.id, cfg.id, 4);
}

static bool status_req(uint8_t req[32], mdp_status_t *st)
{
    uint8_t resp[32];
    return mdp_exchange(req, 7, resp, 30) == XCH_OK && mdp_parse7(resp, st);
}

static bool get_cal(void)
{
    uint8_t req[32], resp[32];
    mdp_req9(req, cfg.id, 0, LED_SLOT0_COLOR);
    return mdp_exchange(req, 9, resp, 30) == XCH_OK && mdp_parse9(resp, cfg.id, &cal);
}

/* Input source info, roughly once a second; backs off on a P906 without the patch. */
static void poll_pd(uint32_t now)
{
    static uint32_t next;
    static int misses;
    if ((int32_t)(now - next) < 0) return;
    uint8_t req[32], resp[32];
    mdp_pd_t pd;
    mdp_req_pd(req, cfg.id);
    if (mdp_exchange(req, 0x0D, resp, 30) == XCH_OK && mdp_parse_pd(resp, &pd)) {
        misses = 0;
        ui_pd(&pd);
    } else if (++misses >= 3) {
        ui_pd(NULL);
    }
    next = now + (misses >= 3 ? 10000u : 1000u);
}

/* Stock-compatible Auto Match. Returns true when paired, linked and saved. */
static bool pair(void)
{
    uint8_t req[32], resp[32], id[4];
    ui_pair_screen("searching...", COL_INFO, PAIR_TIMEOUT_MS / 1000);
    keys_pressed();
    radio_set_channel(MATCH_CH);
    radio_set_address(MATCH_ADDR);

    uint32_t t0 = millis();
    int shown = -1;
    bool got = false;
    while (millis() - t0 < PAIR_TIMEOUT_MS) {
        if (keys_pressed() & (1u << KEY_E)) break;
        int left = (int)((PAIR_TIMEOUT_MS - (millis() - t0)) / 1000u);
        if (left != shown) { shown = left; ui_pair_screen("searching...", COL_INFO, left); }
        mdp_call_id(req);
        if (mdp_exchange(req, 5, resp, 30) == XCH_OK && resp[1] == 4) {
            memcpy(id, resp + 2, 4);
            got = true;
            break;
        }
    }
    bool ok = false;
    if (!got) {
        dbg.pair_result = 2;
        ui_pair_screen("no P906 found", COL_BAD, -1);
    } else {
        char b[32];
        snprintf(b, sizeof b, "found %02X%02X%02X%02X", id[0], id[1], id[2], id[3]);
        ui_pair_screen(b, COL_OK, -1);
        bool assigned = false;
        for (int i = 0; i < 40 && !assigned; i++) {
            mdp_dispatch(req, PAIR_ADDR, PAIR_CH);
            assigned = mdp_exchange(req, 6, resp, 30) == XCH_OK;
        }
        if (!assigned) {
            dbg.pair_result = 3;
            ui_pair_screen("P906 didn't confirm", COL_BAD, -1);
        } else {
            link_cfg_t n = cfg;
            memcpy(n.addr, PAIR_ADDR, 5);
            n.channel = PAIR_CH;
            memcpy(n.id, id, 4);
            n.magic = LINK_MAGIC;
            cfg = n;
            use_link();
            mdp_status_t st;
            for (uint32_t t = millis(); !ok && millis() - t < 10000; ) {
                mdp_poll7(req, cfg.id, 0);
                ok = status_req(req, &st);
            }
            if (ok) {
                config_save(&cfg);
                paired = true;
                cal.valid = false;
                ui_set_id(cfg.id);
                dbg.pair_result = 1;
                ui_pair_screen("paired!", COL_OK, -1);
                if (ui_sound_on()) { beep(40); delay_ms(120); beep(40); }
            } else {
                dbg.pair_result = 4;
                ui_pair_screen("no reply on new address", COL_BAD, -1);
            }
        }
    }
    delay_ms(1200);
    if (!ok && paired) use_link();             /* back to the old link */
    ui_pair_screen(NULL, 0, 0);
    return ok;
}

int main(void)
{
    dbg.magic = 0x4630314Du;
    dbg.core_clock = SystemCoreClock;
    dbg.lcd_id = lcd_init();

    paired = config_load(&cfg);
    if (!paired) memset(cfg.id, 0, 4);
    dbg.paired = paired;
    ui_init(&cfg.ui, cfg.id);
    if (ui_sound_on()) beep(40);

    dbg.radio_ok = radio_init(paired ? cfg.channel : PAIR_CH);
    if (paired) use_link();
    ui_link(false);
    if (!dbg.radio_ok) ui_toast("RADIO FAULT", COL_BAD, millis(), 60000);
    else if (!paired) ui_toast("NOT PAIRED: MENU > PAIR WITH P906", COL_BAD, millis(), 60000);

    int32_t last_m = enc_count(ENC_MENU), last_v = enc_count(ENC_VALUE);
    uint32_t next_poll = 0, next_draw = 0;
    int fails = 0;
    for (;;) {
        uint32_t now = millis();
        dbg.uptime_ms = now;

        uint8_t k = keys_pressed();
        int32_t m = enc_count(ENC_MENU), v = enc_count(ENC_VALUE);
        int dm = (int)(m - last_m), dv = (int)(v - last_v);
        last_m = m; last_v = v;
        if (k || dm || dv) { ui_input(k, dm, dv, now); next_draw = now; }

        if ((int32_t)(now - next_poll) >= 0 && dbg.radio_ok) {
            next_poll = now + POLL_MS;
            ui_cmd_t c;
            bool have_cmd = ui_next_cmd(&c, now);
            if (have_cmd && c.kind == UC_PAIR) {
                pair();
                ui_redraw();
                if (!paired) ui_toast("NOT PAIRED", COL_BAD, millis(), 5000);
                continue;
            }
            if (paired) {
                if (!cal.valid) { dbg.cal_ok = get_cal(); if (cal.valid) dbg.cal = cal; }
                uint8_t req[32];
                if (!have_cmd)               mdp_poll7(req, cfg.id, 0);
                else if (c.kind == UC_SET_V) mdp_set_mV(req, cfg.id, 0, c.value);
                else if (c.kind == UC_SET_I) mdp_set_mA(req, cfg.id, 0, c.value);
                else                         mdp_output(req, cfg.id, 0, c.kind == UC_OUT_ON);
                mdp_status_t st;
                bool ok = status_req(req, &st);
                if (have_cmd) { ui_cmd_done(&c, ok, now); if (ok) dbg.cmds_ok++; else dbg.cmds_fail++; }
                if (ok) {
                    fails = 0;
                    dbg.polls_ok++;
                    dbg.st = st;
                    uint32_t mv = cal.valid ? mdp_out_mV(&st, &cal) : 0, ma = cal.valid ? mdp_out_mA(&st, &cal) : 0;
                    dbg.out_mV = mv; dbg.out_mA = ma;
                    ui_status(&st, cal.valid, mv, ma, now);
                    ui_link(true);
                    dbg.linked = 1;
                    GPIOB->ODR ^= 1u << LED_BLUE_PIN;
                    if (!have_cmd) poll_pd(now);
                } else {
                    dbg.polls_fail++;
                    if (++fails >= LINK_LOST_FAILS) { ui_link(false); dbg.linked = 0; }
                }
            }
            if (ui_take_settings(&cfg.ui)) config_save(&cfg);
        }

        if ((int32_t)(now - next_draw) >= 0) {
            next_draw = now + 50;
            ui_draw(now);
        }
    }
}
