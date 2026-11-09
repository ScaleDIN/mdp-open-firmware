/* Host renderer: runs the real ui.c/gfx.c against a framebuffer and writes
 * PPM screenshots of a few scenarios. Build/run: sim/render.sh */
#include <stdio.h>
#include <string.h>
#include "../src/ui.h"
#include "../src/gfx.h"

static uint16_t fb[GFX_H][GFX_W];
static int wx0, wy0, wx1, wy1, cx, cy;

void gfx_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    wx0 = x0; wy0 = y0; wx1 = x1; wy1 = y1; cx = x0; cy = y0;
}

void gfx_push(uint16_t c, uint32_t n)
{
    while (n--) {
        if (cx < GFX_W && cy < GFX_H) fb[cy][cx] = c;
        if (++cx > wx1) { cx = wx0; if (++cy > wy1) cy = wy0; }
    }
}

void ui_hal_beep(uint32_t ms) { (void)ms; }

static void shot(const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "sim/out/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H);
    for (int y = 0; y < GFX_H; y++)
        for (int x = 0; x < GFX_W; x++) {
            uint16_t c = fb[y][x];
            unsigned char p[3] = { (unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3) };
            fwrite(p, 1, 3, f);
        }
    fclose(f);
    printf("wrote %s\n", path);
}

static uint32_t now = 1000;
static mdp_status_t st;

static void feed(int n, uint32_t mV, uint32_t mA)
{
    for (int i = 0; i < n; i++) {
        now += 100;
        ui_status(&st, true, mV, mA, now);
    }
}

int main(void)
{
    ui_settings_t s;
    ui_defaults(&s);
    uint8_t id[4] = { 0x0B, 0x3D, 0xC1, 0xC1 };
    ui_init(&s, id);
    ui_link(true);

    /* 1: output on, CV, light load */
    st.state = 2; st.temp_dC = 371; st.in_mV = 19848; st.in_mA = 420;
    st.has_set = true; st.set_mV = 5000; st.set_mA = 500;
    feed(600, 5012, 312);
    ui_input(0, -1, 0, now);               /* cursor to the 1 V digit */
    {   /* barrel jack first, then a 20 V / 5 A USB-C PD contract */
        mdp_pd_t none = { 0 }, usb = { 3, 0, 1, 1, 5, 20000, 5000, 100 };
        ui_pd(&none); ui_draw(now); shot("1z_main_barrel");
        ui_pd(&usb);
    }
    ui_draw(now);
    shot("1_main_cv");

    /* 2: editing (pending value), then CC */
    ui_input(0, 1, 3, now);                /* 0.1 V digit, +0.3 V -> 5.300 pending */
    ui_draw(now);
    shot("2_main_editing");
    st.state = 1; st.set_mV = 12000; st.set_mA = 200;
    now += 3000;
    feed(20, 7843, 200);
    ui_draw(now);
    shot("3_main_cc");

    /* 4: output off */
    st.state = 0;
    feed(10, 1, 1);
    now += 3000;
    ui_draw(now);
    shot("4_main_off");

    /* 5: presets */
    ui_input(2, 0, 0, now);
    ui_input(0, 0, 2, now);
    ui_draw(now);
    shot("5_presets");
    ui_input(16, 0, 0, now);

    /* 6: graph with some history: pulses on a 5 V rail */
    st.state = 2; st.set_mV = 5000; st.set_mA = 1000;
    for (int i = 0; i < 900; i++) {
        uint32_t ma = (i / 50) % 2 ? 640 : 120;
        uint32_t mv = ma > 500 ? 4980 : 5010;
        feed(1, mv, ma + (uint32_t)(i % 7) * 4);
    }
    ui_input(4, 0, 0, now);
    ui_input(0, 0, 1, now);                /* 1 min window */
    ui_draw(now);
    shot("6_graph");
    /* scrolling: the same data one and two samples later must look the same,
     * just shifted, with every step keeping its vertical edge */
    for (int n = 1; n <= 2; n++) {
        feed(1, 5010, 120);
        ui_redraw();
        ui_draw(now);
        char nm[32];
        snprintf(nm, sizeof nm, "6%c_graph_plus%d", 'a' + n - 1, n);
        shot(nm);
    }
    ui_input(0, 0, -1, now);               /* 24 s window */
    ui_input(0, 0, -1, now);               /* 12 s window: pixels-per-sample path */
    ui_redraw();
    ui_draw(now);
    shot("6c_graph_12s");
    /* clear, then 5 s of new data: must sit against the right ("now") edge */
    ui_input(8, 0, 0, now);
    for (int i = 0; i < 50; i++) feed(1, i < 25 ? 5010 : 4990, i < 25 ? 150 : 700);
    ui_draw(now);
    shot("6d_graph_cleared");

    /* 7: menu (opened from the graph), muted; BACK must return to the graph */
    ui_input(16, 0, 0, now);
    ui_input(2, 0, 0, now);                /* toggle Sound -> muted */
    ui_draw(now);
    shot("7_menu_muted");
    ui_input(16, 0, 0, now);
    ui_draw(now);
    shot("7a_menu_back_to_graph");

    /* 8: pairing screen */
    ui_pair_screen("searching...", RGB(90, 200, 255), 27);
    shot("8_pairing");

    /* set commands: replies are short (no setpoints), only polls echo them.
     * accepted: one send is enough; refused: gives up after 9 sends */
    ui_pair_screen(NULL, 0, 0);
    ui_input(4, 0, 0, now);                /* graph -> main */
    st.state = 1;
    for (int accept = 1; accept >= 0; accept--) {
        uint32_t asked = 0;
        int sends = 0, polls = 0;
        now += 3000; st.has_set = true; ui_status(&st, true, 5000, 200, now);
        ui_input(0, 0, 1, now);            /* one detent on the value knob */
        for (int i = 0; i < 40; i++) {
            now += 100;
            ui_cmd_t c;
            if (ui_next_cmd(&c, now)) {
                sends++; asked = c.value;
                mdp_status_t r = st; r.has_set = false; r.set_mV = 0xDEAD; r.set_mA = 0xDEAD;
                ui_cmd_done(&c, true, now);
                ui_status(&r, true, 5000, 200, now);
            } else {
                polls++;
                if (accept && asked) st.set_mV = asked;
                ui_status(&st, true, 5000, 200, now);
            }
        }
        printf("set test accept=%d: sends=%d polls=%d\n", accept, sends, polls);
        if (sends != (accept ? 1 : 9)) { printf("FAIL\n"); return 1; }
    }
    return 0;
}
