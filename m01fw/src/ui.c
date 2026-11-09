/* M01 user interface. See ui.h. Screen 320x240.
 *
 * Global: A = output on/off.  Knobs: menu (left) and value (right).
 * MAIN    : menu knob moves the digit cursor over set V / set I, value knob
 *           changes that digit (applied live). B presets, C graph, D reset
 *           session counters, E menu.
 * PRESETS : knobs select a slot, B recall, C save current set values, E back.
 * GRAPH   : value knob = time window (12 s .. 10 min, works on stored history),
 *           menu knob = scroll back, B hold/run, C main, D clear, E menu.
 * MENU    : knobs select, B select/toggle, E back to the page it was opened from.
 *
 * The status bar shows the P906's input V / A / W on every page, and a strip
 * on the right edge (x >= ST_X) names what the two knobs do on this page.
 */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "gfx.h"
#include "fonts.h"

/* ---- colours ---- */
#define C_BG     RGB(10, 12, 16)
#define C_PANEL  RGB(26, 30, 38)
#define C_LINE   RGB(48, 54, 66)
#define C_TEXT   RGB(235, 238, 242)
#define C_DIM    RGB(120, 128, 140)
#define C_FAINT  RGB(70, 76, 88)
#define C_VOLT   RGB(255, 206, 64)
#define C_AMP    RGB(90, 200, 255)
#define C_WATT   RGB(255, 140, 90)
#define C_OK     RGB(60, 200, 100)
#define C_BAD    RGB(235, 60, 60)
#define C_SEL    RGB(40, 90, 170)
#define C_EDIT   RGB(255, 255, 255)

/* ---- layout ---- */
#define BAR_H    20
#define KEYS_Y   214
#define KEYS_H   (GFX_H - KEYS_Y)
#define ST_X     288              /* knob legend strip: x 288..319, y 24..~190 */
#define CW       (ST_X - 4)       /* right edge of page content */

typedef enum { SCR_MAIN, SCR_PRESETS, SCR_GRAPH, SCR_MENU } screen_t;

static ui_settings_t set;
static uint8_t dev_id[4];
static screen_t scr, menu_from;
static bool full;                 /* full redraw pending */
static bool linked, have_st, cal_ok;
static mdp_status_t st;
static mdp_pd_t pd;
static bool have_pd;
static uint32_t out_mV, out_mA;

/* setpoints: what we want vs what the P906 reports */
static int32_t want_mV = -1, want_mA = -1;
static bool edit_v, edit_i;       /* local edit not yet confirmed */
static uint32_t last_edit_ms;
static int cursor = 2;            /* 0..4 = V digits (10,1,.1,.01,.001), 5..9 = I digits */
static int want_out = -1;         /* -1 none, 0 off, 1 on */
static uint32_t out_cmd_ms;
static bool pair_req;
static int v_tries, i_tries;      /* sends of the current edit */
static bool verify_set;           /* a set was sent: poll next, only polls report setpoints */

/* session counters (since last reset), accumulated while the output is on */
static uint64_t sess_mAms, sess_mWms;
static uint32_t sess_on_ms, sess_peak_mA, last_status_ms;
static uint8_t last_state;

/* toast message */
static char toast[40];
static uint16_t toast_col;
static uint32_t toast_until;

/* history for the graph. Sample numbers start at a large base so a window
 * can always reach back past the oldest sample: the graph is anchored to the
 * right edge (newest sample = "now") even before the history fills up. */
#define HIST_BASE (UI_HIST * 100u)
static uint16_t hv[UI_HIST], hi[UI_HIST];
static uint32_t h_head = HIST_BASE, h_count;
static bool g_hold;
static uint32_t g_hold_head;
static int g_win = 1;             /* index into WINDOWS */
static int g_back;                /* scroll back, in 1/4 windows */
/* samples; multiples/divisors of the 240-px plot width so each window is exact */
static const uint16_t WINDOWS[] = { 120, 240, 480, 1200, 2400, 6000 };
static const char *const WIN_NAME[] = { "12s", "24s", "48s", "2m", "4m", "10m" };

static int preset_sel, menu_sel;
static bool settings_dirty;

static void beep(uint32_t ms) { if (!set.muted) ui_hal_beep(ms); }
bool ui_sound_on(void) { return !set.muted; }

static void show_toast(const char *s, uint16_t c, uint32_t now, uint32_t ms)
{
    snprintf(toast, sizeof toast, "%s", s);
    toast_col = c;
    toast_until = now + ms;
}

void ui_defaults(ui_settings_t *s)
{
    memset(s, 0, sizeof *s);
    static const ui_preset_t d[UI_PRESETS] = {
        { 3300, 500 }, { 5000, 1000 }, { 9000, 1000 }, { 12000, 1000 }, { 0xFFFF, 0 }, { 0xFFFF, 0 } };
    memcpy(s->preset, d, sizeof d);
    s->beep_cc = 1;
    s->key_click = 1;
}

void ui_init(const ui_settings_t *s, const uint8_t id[4])
{
    set = *s;
    memcpy(dev_id, id, 4);
    full = true;
}

void ui_redraw(void) { full = true; }
void ui_toast(const char *s, uint16_t c, uint32_t now, uint32_t ms) { show_toast(s, c, now, ms); }
void ui_set_id(const uint8_t id[4]) { memcpy(dev_id, id, 4); }
void ui_link(bool l) { linked = l; }
void ui_pd(const mdp_pd_t *p) { have_pd = p != NULL; if (p) pd = *p; }

void ui_hist_push(uint16_t mV, uint16_t mA)
{
    hv[h_head % UI_HIST] = mV;
    hi[h_head % UI_HIST] = mA;
    h_head++;
    if (h_count < UI_HIST) h_count++;
}

void ui_status(const mdp_status_t *s, bool cal, uint32_t mV, uint32_t mA, uint32_t now)
{
    /* replies to set commands are short and carry no setpoints: keep the last known */
    uint32_t keep_mV = st.set_mV, keep_mA = st.set_mA;
    st = *s;
    if (!s->has_set) { st.set_mV = keep_mV; st.set_mA = keep_mA; }
    cal_ok = cal;
    out_mV = mV;
    out_mA = mA;
    have_st = true;

    /* follow the P906's setpoints unless we're editing them */
    if (s->has_set) {
        if (want_mV < 0 || (!edit_v && now - last_edit_ms > 1500)) want_mV = (int32_t)s->set_mV;
        if (want_mA < 0 || (!edit_i && now - last_edit_ms > 1500)) want_mA = (int32_t)s->set_mA;
        if (edit_v && (int32_t)s->set_mV == want_mV) edit_v = false;
        if (edit_i && (int32_t)s->set_mA == want_mA) edit_i = false;
    }
    if (want_out >= 0 && ((s->state != 0) == (want_out == 1))) want_out = -1;

    /* session + history */
    uint32_t dt = last_status_ms ? now - last_status_ms : 0;
    if (dt > 1000) dt = 100;
    last_status_ms = now;
    if (s->state) {
        sess_on_ms += dt;
        sess_mAms += (uint64_t)mA * dt;
        sess_mWms += (uint64_t)mV * mA / 1000u * dt;
        if (mA > sess_peak_mA) sess_peak_mA = mA;
    }
    ui_hist_push((uint16_t)(s->state ? mV : 0), (uint16_t)(s->state ? mA : 0));

    if (s->state == 1 && last_state != 1) {          /* entered current limit */
        show_toast("CURRENT LIMIT (CC)", C_BAD, now, 2500);
        if (set.beep_cc) beep(150);
    }
    if (s->err && toast_until < now) show_toast("P906 REPORTS AN ERROR", C_BAD, now, 2000);
    last_state = s->state;
}

/* ---- setpoint editing ---- */
static const int32_t STEP[10] = { 10000, 1000, 100, 10, 1, 10000, 1000, 100, 10, 1 };

static void edit(int d, uint32_t now)
{
    if (want_mV < 0) return;
    if (cursor < 5) {
        want_mV += d * STEP[cursor];
        if (want_mV < 0) want_mV = 0;
        if (want_mV > (int32_t)MDP_MAX_MV) want_mV = MDP_MAX_MV;
        edit_v = true; v_tries = 0;
    } else {
        want_mA += d * STEP[cursor];
        if (want_mA < 0) want_mA = 0;
        if (want_mA > (int32_t)MDP_MAX_MA) want_mA = MDP_MAX_MA;
        edit_i = true; i_tries = 0;
    }
    last_edit_ms = now;
}

static void reset_session(void) { sess_mAms = sess_mWms = 0; sess_on_ms = sess_peak_mA = 0; }

void ui_input(uint8_t keys, int dmenu, int dvalue, uint32_t now)
{
    if (keys && set.key_click) beep(8);
    if (keys & 1) {                                   /* A: output, everywhere */
        if (have_st && want_out < 0) { want_out = st.state ? 0 : 1; out_cmd_ms = 0; }
    }
    switch (scr) {
    case SCR_MAIN:
        if (dmenu) { cursor += dmenu; if (cursor < 0) cursor = 0; if (cursor > 9) cursor = 9; }
        if (dvalue) edit(dvalue, now);
        if (keys & 2) { scr = SCR_PRESETS; full = true; }
        if (keys & 4) { scr = SCR_GRAPH; full = true; }
        if (keys & 8) { reset_session(); show_toast("SESSION COUNTERS RESET", C_DIM, now, 1500); }
        if (keys & 16) { menu_from = scr; scr = SCR_MENU; full = true; }
        break;
    case SCR_PRESETS: {
        int d = dmenu + dvalue;
        if (d) { preset_sel = (preset_sel + d % UI_PRESETS + UI_PRESETS) % UI_PRESETS; }
        ui_preset_t *p = &set.preset[preset_sel];
        if ((keys & 2) && p->mV != 0xFFFF) {          /* recall */
            want_mV = p->mV; want_mA = p->mA; edit_v = edit_i = true; v_tries = i_tries = 0; last_edit_ms = now - 1000;
            char b[40];
            snprintf(b, sizeof b, "PRESET %d RECALLED", preset_sel + 1);
            show_toast(b, C_OK, now, 1500);
            scr = SCR_MAIN; full = true;
        }
        if ((keys & 4) && want_mV >= 0) {             /* save */
            p->mV = (uint16_t)want_mV; p->mA = (uint16_t)want_mA;
            settings_dirty = true;
            char b[40];
            snprintf(b, sizeof b, "SAVED TO PRESET %d", preset_sel + 1);
            show_toast(b, C_OK, now, 1500);
        }
        if (keys & 16) { scr = SCR_MAIN; full = true; }
        break;
    }
    case SCR_GRAPH:
        if (dvalue) { g_win += dvalue; if (g_win < 0) g_win = 0; if (g_win > 5) g_win = 5; }
        if (dmenu) { g_back -= dmenu; if (g_back < 0) g_back = 0; if (g_back > 36) g_back = 36; }
        if (keys & 2) { g_hold = !g_hold; g_hold_head = h_head; }
        if (keys & 4) { scr = SCR_MAIN; full = true; }
        if (keys & 8) { h_count = 0; g_back = 0; g_hold = false; full = true; }   /* clear */
        if (keys & 16) { menu_from = scr; scr = SCR_MENU; full = true; }
        break;
    case SCR_MENU: {
        int d = dmenu + dvalue;
        if (d) { menu_sel += d; if (menu_sel < 0) menu_sel = 0; if (menu_sel > 4) menu_sel = 4; }
        if (keys & 2) {
            if (menu_sel == 0) { set.muted = !set.muted; settings_dirty = true; if (!set.muted) beep(30); }
            if (menu_sel == 1) { set.beep_cc = !set.beep_cc; settings_dirty = true; }
            if (menu_sel == 2) { set.key_click = !set.key_click; settings_dirty = true; }
            if (menu_sel == 3) pair_req = true;
        }
        if (keys & 16) { scr = menu_from; full = true; }
        break;
    }
    }
}

bool ui_next_cmd(ui_cmd_t *c, uint32_t now)
{
    if (pair_req) { pair_req = false; c->kind = UC_PAIR; c->value = 0; return true; }
    if (want_out >= 0 && now - out_cmd_ms > 400) {
        c->kind = want_out ? UC_OUT_ON : UC_OUT_OFF; c->value = 0;
        return true;
    }
    if (verify_set) { verify_set = false; return false; }
    if (edit_v && now - last_edit_ms > 120 && (!have_st || (int32_t)st.set_mV != want_mV)) {
        c->kind = UC_SET_V; c->value = (uint32_t)want_mV; return true;
    }
    if (edit_i && now - last_edit_ms > 120 && (!have_st || (int32_t)st.set_mA != want_mA)) {
        c->kind = UC_SET_I; c->value = (uint32_t)want_mA; return true;
    }
    return false;
}

void ui_cmd_done(const ui_cmd_t *c, bool ok, uint32_t now)
{
    if (c->kind == UC_OUT_ON || c->kind == UC_OUT_OFF) {
        out_cmd_ms = now;
        if (!ok) show_toast("NO REPLY FROM P906", C_BAD, now, 1500);
    }
    if ((c->kind == UC_SET_V || c->kind == UC_SET_I) && ok) verify_set = true;
    if (c->kind == UC_SET_V && ++v_tries > 8) { edit_v = false; show_toast("P906 DID NOT ACCEPT VOLTAGE", C_BAD, now, 2000); }
    if (c->kind == UC_SET_I && ++i_tries > 8) { edit_i = false; show_toast("P906 DID NOT ACCEPT CURRENT", C_BAD, now, 2000); }
}

bool ui_take_settings(ui_settings_t *out)
{
    if (!settings_dirty) return false;
    settings_dirty = false;
    *out = set;
    return true;
}

/* ================= drawing ================= */

/* cached fields: redraw only when the text/colour changes */
enum { F_LINK, F_IN, F_TEMP, F_V, F_A, F_W, F_BADGE, F_SETV, F_SETI, F_SESS, F_TOAST, F_KEYS, F_MUTE,
       F_LEG, F_GV, F_GA, F_GW, F_SRC, F_N };
static char cache[F_N][64];

static bool changed(int f, const char *s)
{
    if (!full && strcmp(cache[f], s) == 0) return false;
    snprintf(cache[f], sizeof cache[f], "%s", s);
    return true;
}

static void milli(char *o, size_t n, uint32_t v) { snprintf(o, n, "%2lu.%03lu", (unsigned long)(v / 1000), (unsigned long)(v % 1000)); }

/* speaker icon, 16x14 at (x,y); crossed out when muted */
static void draw_speaker(int x, int y, bool muted)
{
    gfx_fill(x, y, 18, 16, C_PANEL);
    uint16_t c = muted ? C_BAD : C_DIM;
    gfx_fill(x + 1, y + 5, 3, 6, c);                  /* magnet */
    for (int i = 0; i < 5; i++) gfx_fill(x + 4 + i, y + 5 - i, 1, 6 + 2 * i, c);   /* cone */
    if (muted) {
        for (int i = 0; i < 6; i++) { gfx_fill(x + 11 + i, y + 5 + i, 1, 1, c); gfx_fill(x + 16 - i, y + 5 + i, 1, 1, c); }
    } else {
        gfx_fill(x + 11, y + 6, 1, 4, c);
        gfx_fill(x + 13, y + 4, 1, 8, c);
        gfx_fill(x + 15, y + 2, 1, 12, c);
    }
}

static void draw_bar(const char *title)
{
    char b[44];
    if (full) gfx_fill(0, 0, GFX_W, BAR_H, C_PANEL);
    snprintf(b, sizeof b, "%s%s", linked ? "\x01" : "\x02", title);
    if (changed(F_LINK, b)) {
        gfx_fill(4, 6, 8, 8, linked ? C_OK : C_BAD);
        gfx_field(&font_sml, 16, 2, 7, linked ? title : "NO LINK", linked ? C_TEXT : C_BAD, C_PANEL);
    }
    if (have_st) {
        /* P906 input (barrel or USB-C): "IN 19.85V 0.42A 8.3W" */
        uint32_t mW = (uint32_t)((uint64_t)st.in_mV * st.in_mA / 1000u);
        char w[12];
        if (mW < 100000) snprintf(w, sizeof w, "%lu.%luW", (unsigned long)(mW / 1000), (unsigned long)(mW % 1000 / 100));
        else snprintf(w, sizeof w, "%luW", (unsigned long)(mW / 1000));
        snprintf(b, sizeof b, "%s %2lu.%02luV %lu.%02luA %s", !have_pd ? "IN" : pd.pdos ? "PD" : "DC",
                 (unsigned long)(st.in_mV / 1000), (unsigned long)(st.in_mV % 1000 / 10),
                 (unsigned long)(st.in_mA / 1000), (unsigned long)(st.in_mA % 1000 / 10), w);
        if (changed(F_IN, b)) gfx_field(&font_sml, 74, 2, 21, b, C_DIM, C_PANEL);
        snprintf(b, sizeof b, "%2u.%u" DEG "C", st.temp_dC / 10, st.temp_dC % 10);
        if (changed(F_TEMP, b)) gfx_field(&font_sml, 244, 2, 6, b, st.temp_dC > 600 ? C_BAD : C_DIM, C_PANEL);
    }
    snprintf(b, sizeof b, "%d", set.muted);
    if (changed(F_MUTE, b)) draw_speaker(GFX_W - 22, 2, set.muted);
}

static void draw_keys(const char *const lab[5])
{
    char b[44];
    bool on = have_st && st.state;
    snprintf(b, sizeof b, "%d%s|%s|%s|%s|%s", on, lab[0], lab[1], lab[2], lab[3], lab[4]);
    if (!changed(F_KEYS, b)) return;
    gfx_fill(0, KEYS_Y, GFX_W, KEYS_H, C_BG);
    for (int i = 0; i < 5; i++) {
        int x = i * 64;
        uint16_t bg = (i == 0 && on) ? C_OK : C_PANEL;
        gfx_fill(x + 2, KEYS_Y + 2, 60, KEYS_H - 4, bg);
        int w = gfx_text_w(&font_sml, lab[i]);
        gfx_text(&font_sml, x + 32 - w / 2, KEYS_Y + 5, lab[i], (i == 0 && on) ? C_BG : C_TEXT, bg);
    }
}

/* knob legend on the right edge: two boxes (L = menu knob, R = value knob),
 * each with up to two 4-char lines; optional extra lines below (graph state) */
static void draw_legend(const char *l1, const char *l2, const char *r1, const char *r2,
                        const char *x1, const char *x2, uint16_t xcol)
{
    char b[64];
    snprintf(b, sizeof b, "%s|%s|%s|%s|%s|%s|%u", l1, l2, r1, r2, x1, x2, xcol);
    if (!changed(F_LEG, b)) return;
    const char *const lab[2][2] = { { l1, l2 }, { r1, r2 } };
    for (int k = 0; k < 2; k++) {
        int y = 24 + k * 60;
        gfx_fill(ST_X, y, GFX_W - ST_X, 56, C_PANEL);
        gfx_fill(ST_X, y, GFX_W - ST_X, 14, C_LINE);
        gfx_text(&font_sml, ST_X + 12, y - 1, k ? "R" : "L", C_DIM, C_LINE);
        for (int j = 0; j < 2; j++) {
            int w = gfx_text_w(&font_sml, lab[k][j]);
            gfx_text(&font_sml, ST_X + 16 - w / 2, y + 18 + j * 17, lab[k][j], C_TEXT, C_PANEL);
        }
    }
    if (!*x1 && !*x2) return;          /* main page: the session line runs full width below */
    gfx_field(&font_sml, ST_X, 146, 4, x1, xcol, C_BG);
    gfx_field(&font_sml, ST_X, 164, 4, x2, xcol, C_BG);
}

static void draw_toast(uint32_t now)
{
    const char *s = now < toast_until ? toast : "";
    if (!changed(F_TOAST, s)) return;
    gfx_fill(0, 186, GFX_W, 24, C_BG);
    if (*s) {
        int w = gfx_text_w(&font_sml, s);
        gfx_fill(GFX_W / 2 - w / 2 - 8, 188, w + 16, 20, toast_col);
        gfx_text(&font_sml, GFX_W / 2 - w / 2, 190, s, C_BG, toast_col);
    }
}

/* set value with the digit cursor drawn as a highlighted cell */
static void draw_set(int f, int x, int y, int32_t val, bool editing, int cur_pos, uint16_t col)
{
    char s[12], key[44];
    milli(s, sizeof s, (uint32_t)(val < 0 ? 0 : val));
    if (val < 0) strcpy(s, "--.---");
    snprintf(key, sizeof key, "%s|%d|%d", s, editing, cur_pos);
    if (!changed(f, key)) return;
    static const int pos[5] = { 0, 1, 3, 4, 5 };
    for (int i = 0; i < 6; i++) {
        char c[2] = { s[i], 0 };
        bool hl = cur_pos >= 0 && pos[cur_pos] == i;
        gfx_text(&font_med, x + i * font_med.w, y, c, editing ? C_EDIT : col, hl ? C_SEL : C_BG);
    }
}

static void draw_main(uint32_t now)
{
    char b[64], v[12];
    draw_bar("P906");
    if (full) {
        gfx_fill(0, BAR_H, GFX_W, KEYS_Y - BAR_H, C_BG);
        gfx_fill(8, 128, CW - 8, 1, C_LINE);
        gfx_text(&font_sml, 8, 140, "SET", C_DIM, C_BG);
        gfx_text(&font_med, 40 + 6 * font_med.w + 2, 136, "V", C_DIM, C_BG);
        gfx_text(&font_med, 176 + 6 * font_med.w + 2, 136, "A", C_DIM, C_BG);
    }
    bool on = have_st && st.state;
    uint16_t vc = on ? C_VOLT : C_FAINT, ac = on ? C_AMP : C_FAINT;

    milli(v, sizeof v, cal_ok ? out_mV : 0);
    snprintf(b, sizeof b, "%s%d", v, on);
    if (changed(F_V, b)) { gfx_text(&font_big, 8, 28, v, vc, C_BG); gfx_text(&font_med, 214, 46, "V", vc, C_BG); }
    milli(v, sizeof v, cal_ok ? out_mA : 0);
    snprintf(b, sizeof b, "%s%d", v, on);
    if (changed(F_A, b)) { gfx_text(&font_big, 8, 76, v, ac, C_BG); gfx_text(&font_med, 214, 94, "A", ac, C_BG); }

    /* mode badge */
    const char *mode = !have_st ? "--" : !st.state ? "OFF" : st.state == 1 ? "CC" : "CV";
    uint16_t mc = !have_st || !st.state ? C_FAINT : st.state == 1 ? C_BAD : C_OK;
    if (changed(F_BADGE, mode)) {
        gfx_fill(232, 30, CW - 232, 34, mc);
        int w = gfx_text_w(&font_med, mode);
        gfx_text(&font_med, (232 + CW) / 2 - w / 2, 36, mode, st.state ? C_BG : C_TEXT, mc);
    }
    /* output power: 4 medium cells ("9.99", "99.9", " 150") with the unit below */
    uint32_t mW = on && cal_ok ? (uint32_t)((uint64_t)out_mV * out_mA / 1000u) : 0;
    char w[16];
    if (mW < 10000) snprintf(w, sizeof w, "%lu.%02lu", (unsigned long)(mW / 1000), (unsigned long)(mW % 1000 / 10));
    else if (mW < 100000) snprintf(w, sizeof w, "%lu.%lu", (unsigned long)(mW / 1000), (unsigned long)(mW % 1000 / 100));
    else snprintf(w, sizeof w, "%lu", (unsigned long)(mW / 1000));
    snprintf(b, sizeof b, "%4s%d", w, on);
    if (changed(F_W, b)) {
        uint16_t wc = on ? C_WATT : C_FAINT;
        b[4] = 0;
        gfx_text(&font_med, CW - 4 * font_med.w, 76, b, wc, C_BG);
        gfx_text(&font_med, CW - font_med.w, 98, "W", wc, C_BG);
    }

    draw_set(F_SETV, 40, 136, want_mV, edit_v, cursor < 5 ? cursor : -1, C_VOLT);
    draw_set(F_SETI, 176, 136, want_mA, edit_i, cursor >= 5 ? cursor - 5 : -1, C_AMP);

    /* session line */
    uint32_t s = sess_on_ms / 1000;
    uint32_t mAh10 = (uint32_t)(sess_mAms / 360000u);          /* 0.1 mAh */
    uint32_t mWh = (uint32_t)(sess_mWms / 3600000u);
    char pk[12];
    milli(pk, sizeof pk, sess_peak_mA);
    snprintf(b, sizeof b, "%02lu:%02lu:%02lu %5lu.%lumAh %5lumWh pk%sA",
             (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60),
             (unsigned long)(mAh10 / 10), (unsigned long)(mAh10 % 10), (unsigned long)mWh, pk);
    if (changed(F_SESS, b)) gfx_field(&font_sml, 0, 166, 40, b, C_DIM, C_BG);

    draw_legend("MOVE", "DGT", "ADJ", "VAL", "", "", C_DIM);
    draw_toast(now);
    /* input source, in the toast strip while no toast is up. The P906 can't
     * sense the barrel jack: "no PD source" is all it knows. */
    b[0] = 0;
    if (have_pd && now >= toast_until) {
        if (!pd.pdos) snprintf(b, sizeof b, "No USB-PD: barrel jack or non-PD USB");
        else if (!pd.sent || pd.txq) snprintf(b, sizeof b, "USB-C PD: negotiating...");
        else {
            int n = snprintf(b, sizeof b, "USB-C %s %u.%uV %u.%02uA", pd.type == 2 ? "PPS" : "PD",
                             pd.mV / 1000u, pd.mV % 1000u / 100u, pd.mA / 1000u, pd.mA % 1000u / 10u);
            if (pd.max_W) snprintf(b + n, sizeof b - (size_t)n, "  adaptor %uW", pd.max_W);
        }
    }
    if (changed(F_SRC, b)) {
        if (b[0]) gfx_field(&font_sml, 8, 190, 38, b, C_DIM, C_BG);
        else if (now >= toast_until) gfx_fill(0, 186, GFX_W, 24, C_BG);
    }
    static const char *const lab[5] = { "OUTPUT", "PRESETS", "GRAPH", "RESET", "MENU" };
    draw_keys(lab);
}

static void draw_presets(uint32_t now)
{
    char b[44], v[12], a[12];
    draw_bar("PRESETS");
    if (full) gfx_fill(0, BAR_H, GFX_W, KEYS_Y - BAR_H, C_BG);
    static int last_sel = -1;
    static ui_preset_t last[UI_PRESETS];
    for (int i = 0; i < UI_PRESETS; i++) {
        const ui_preset_t *p = &set.preset[i];
        if (!full && last_sel == preset_sel && memcmp(&last[i], p, sizeof *p) == 0) continue;
        bool sel = i == preset_sel;
        uint16_t bg = sel ? C_SEL : C_BG;
        int y = BAR_H + 4 + i * 27;
        gfx_fill(4, y, CW - 4, 26, bg);
        snprintf(b, sizeof b, "%d", i + 1);
        gfx_text(&font_med, 12, y + 2, b, C_DIM, bg);
        if (p->mV == 0xFFFF) {
            gfx_text(&font_med, 60, y + 2, "empty", C_FAINT, bg);
        } else {
            milli(v, sizeof v, p->mV); milli(a, sizeof a, p->mA);
            snprintf(b, sizeof b, "%sV", v);
            gfx_text(&font_med, 60, y + 2, b, C_VOLT, bg);
            snprintf(b, sizeof b, "%sA", a);
            gfx_text(&font_med, 190, y + 2, b, C_AMP, bg);
        }
    }
    memcpy(last, set.preset, sizeof last);
    last_sel = preset_sel;
    draw_legend("SEL", "SLOT", "SEL", "SLOT", "", "", C_DIM);
    draw_toast(now);
    static const char *const lab[5] = { "OUTPUT", "RECALL", "SAVE", "", "BACK" };
    draw_keys(lab);
}

static void draw_menu(uint32_t now)
{
    char b[44];
    draw_bar("MENU");
    if (full) gfx_fill(0, BAR_H, GFX_W, KEYS_Y - BAR_H, C_BG);
    static int last_sel = -1;
    static ui_settings_t last;
    if (full || last_sel != menu_sel || memcmp(&last, &set, sizeof set) != 0) {
        const char *items[5];
        char i0[32], i1[32], i2[32], i4[40];
        snprintf(i0, sizeof i0, "Sound          %s", set.muted ? "MUTED" : "ON");
        snprintf(i1, sizeof i1, "Beep on CC     %s", set.beep_cc ? "ON" : "OFF");
        snprintf(i2, sizeof i2, "Key click      %s", set.key_click ? "ON" : "OFF");
        snprintf(i4, sizeof i4, "P906 ID %02X%02X%02X%02X", dev_id[0], dev_id[1], dev_id[2], dev_id[3]);
        items[0] = i0; items[1] = i1; items[2] = i2; items[3] = "Pair with P906..."; items[4] = i4;
        for (int i = 0; i < 5; i++) {
            bool sel = i == menu_sel;
            uint16_t bg = sel ? C_SEL : C_BG;
            int y = BAR_H + 6 + i * 30;
            gfx_fill(4, y, CW - 4, 28, bg);
            gfx_text(&font_med, 14, y + 3, items[i], i == 4 ? C_DIM : C_TEXT, bg);
        }
        last_sel = menu_sel;
        last = set;
    }
    (void)b;
    draw_legend("SEL", "ITEM", "SEL", "ITEM", "", "", C_DIM);
    draw_toast(now);
    static const char *const lab[5] = { "OUTPUT", "SELECT", "", "", "BACK" };
    draw_keys(lab);
}

/* ---- graph ---- */
#define G_X0 44
#define G_X1 CW                    /* 240 px plot */
#define G_Y0 (BAR_H + 26)
#define G_Y1 (KEYS_Y - 22)

static uint32_t nice_ceil(uint32_t v, uint32_t min)
{
    static const uint32_t steps[] = { 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 30000 };
    if (v < min) v = min;
    v = v + v / 10;
    for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; i++) if (steps[i] >= v) return steps[i];
    return 30000;
}

static void scale_label(char *o, size_t n, uint32_t m, char unit)
{
    if (m >= 1000) snprintf(o, n, "%lu.%lu%c", (unsigned long)(m / 1000), (unsigned long)(m % 1000 / 100), unit);
    else snprintf(o, n, "%lum%c", (unsigned long)m, unit);
}

static void draw_graph(uint32_t now)
{
    char b[48], sv[12], si[12];
    draw_bar("GRAPH");
    if (full) gfx_fill(0, BAR_H, GFX_W, KEYS_Y - BAR_H, C_BG);

    /* Columns are locked to whole samples so the picture scrolls cleanly:
     * long windows: an integer number of samples per column (spc), with the
     * buckets aligned to absolute sample numbers; short windows: an integer
     * number of pixels per sample (pps). */
    const uint32_t W = G_X1 - G_X0;
    uint32_t win = WINDOWS[g_win];
    uint32_t spc = 1, pps = 1;
    if (win >= W) spc = (win + W / 2) / W; else pps = (W + win / 2) / win;   /* nearest whole */
    if (pps * (W / pps) > W) pps--;
    uint32_t span = (W / pps) * spc;                          /* samples shown */
    uint32_t head = g_hold ? g_hold_head : h_head;
    uint32_t back = (uint32_t)g_back * span / 4;
    uint32_t end = head > back ? head - back : 0;             /* exclusive */
    end -= end % spc;
    uint32_t oldest = h_head > h_count ? h_head - h_count : 0;
    uint32_t start = end - span;                              /* > 0 thanks to HIST_BASE */

    uint32_t vmax = 0, imax = 0;
    for (uint32_t k = start < oldest ? oldest : start; k < end; k++) {
        if (hv[k % UI_HIST] > vmax) vmax = hv[k % UI_HIST];
        if (hi[k % UI_HIST] > imax) imax = hi[k % UI_HIST];
    }
    vmax = nice_ceil(vmax, 100);
    imax = nice_ceil(imax, 10);

    scale_label(sv, sizeof sv, vmax, 'V');
    scale_label(si, sizeof si, imax, 'A');

    /* live readout (always the present, even when held or scrolled back) */
    {
        bool on = have_st && st.state;
        char v[12];
        milli(v, sizeof v, on && cal_ok ? out_mV : 0);
        snprintf(b, sizeof b, "%sV%d", v, on);
        if (changed(F_GV, b)) { b[7] = 0; gfx_text(&font_med, 4, BAR_H + 1, b, on ? C_VOLT : C_FAINT, C_BG); }
        milli(v, sizeof v, on && cal_ok ? out_mA : 0);
        snprintf(b, sizeof b, "%sA%d", v, on);
        if (changed(F_GA, b)) { b[7] = 0; gfx_text(&font_med, 98, BAR_H + 1, b, on ? C_AMP : C_FAINT, C_BG); }
        uint32_t mW = on && cal_ok ? (uint32_t)((uint64_t)out_mV * out_mA / 1000u) : 0;
        char w[16];
        if (mW < 100000) snprintf(w, sizeof w, "%lu.%02luW", (unsigned long)(mW / 1000), (unsigned long)(mW % 1000 / 10));
        else snprintf(w, sizeof w, "%lu.%luW", (unsigned long)(mW / 1000), (unsigned long)(mW % 1000 / 100));
        snprintf(b, sizeof b, "%6s%d", w, on);
        if (changed(F_GW, b)) { b[6] = 0; gfx_text(&font_med, CW - 6 * font_med.w, BAR_H + 1, b, on ? C_WATT : C_FAINT, C_BG); }
    }
    draw_legend("SCRL", "BACK", "ZOOM", "TIME", WIN_NAME[g_win],
                g_hold ? "HOLD" : g_back ? "PAST" : "LIVE", g_hold ? C_BAD : g_back ? C_VOLT : C_OK);
    /* time axis: left edge, middle, right edge (relative to now) */
    {
        uint32_t t_end = (h_head - end) / 10, t_mid = t_end + span / 20, t_start = t_end + span / 10;
        char t0[12], t1[12], t2[12];
        snprintf(t0, sizeof t0, "-%lus", (unsigned long)t_start);
        snprintf(t1, sizeof t1, "-%lus", (unsigned long)t_mid);
        if (t_end) snprintf(t2, sizeof t2, "-%lus", (unsigned long)t_end); else strcpy(t2, "now");
        gfx_fill(G_X0 - 4, G_Y1 + 3, GFX_W - G_X0 + 4, 16, C_BG);
        gfx_text(&font_sml, G_X0, G_Y1 + 3, t0, C_DIM, C_BG);
        gfx_text(&font_sml, (G_X0 + G_X1) / 2 - gfx_text_w(&font_sml, t1) / 2, G_Y1 + 3, t1, C_DIM, C_BG);
        gfx_text(&font_sml, G_X1 - gfx_text_w(&font_sml, t2), G_Y1 + 3, t2, C_DIM, C_BG);
    }
    gfx_field(&font_sml, 2, G_Y0 - 2, 5, sv, C_VOLT, C_BG);
    gfx_field(&font_sml, 2, G_Y0 + 14, 5, si, C_AMP, C_BG);
    gfx_field(&font_sml, 2, G_Y1 - 14, 5, "0", C_DIM, C_BG);

    const int H = G_Y1 - G_Y0;
    for (uint32_t x = 0; x < W; x++) {
        uint32_t a = start + (x / pps) * spc, z = a + spc;        /* this column's samples */
        int col = G_X0 + (int)x;
        gfx_fill(col, G_Y0, 1, H + 1, (x % 60 == 0) ? C_PANEL : C_BG);
        for (int g = 0; g <= 4; g++) gfx_fill(col, G_Y0 + g * H / 4, 1, 1, C_LINE);
        if (z > end || a < oldest) continue;
        /* the first column of each sample group also covers the previous
         * sample, so steps between groups always get their vertical edge */
        uint32_t k0 = (x % pps == 0 && a > oldest) ? a - 1 : a;
        uint16_t v0 = 0xFFFF, v1 = 0, i0 = 0xFFFF, i1 = 0;
        for (uint32_t k = k0; k < z; k++) {
            uint16_t v = hv[k % UI_HIST], i = hi[k % UI_HIST];
            if (v < v0) v0 = v;
            if (v > v1) v1 = v;
            if (i < i0) i0 = i;
            if (i > i1) i1 = i;
        }
        int yv0 = G_Y1 - (int)((uint32_t)v0 * (uint32_t)H / vmax), yv1 = G_Y1 - (int)((uint32_t)v1 * (uint32_t)H / vmax);
        int yi0 = G_Y1 - (int)((uint32_t)i0 * (uint32_t)H / imax), yi1 = G_Y1 - (int)((uint32_t)i1 * (uint32_t)H / imax);
        gfx_vline_clip(col, yi0, yi1, G_Y0, G_Y1, C_AMP);
        gfx_vline_clip(col, yv0, yv1, G_Y0, G_Y1, C_VOLT);
    }
    gfx_fill(G_X0 - 1, G_Y0, 1, H + 1, C_LINE);
    gfx_fill(G_X0, G_Y1 + 1, (int)W, 1, C_LINE);
    /* no toasts here: their strip overlaps the time axis */
    static const char *const lab_run[5] = { "OUTPUT", "HOLD", "MAIN", "CLEAR", "MENU" };
    static const char *const lab_hold[5] = { "OUTPUT", "RUN", "MAIN", "CLEAR", "MENU" };
    draw_keys(g_hold ? lab_hold : lab_run);
}

void ui_draw(uint32_t now)
{
    static screen_t last = (screen_t)-1;
    if (scr != last) { full = true; last = scr; }
    switch (scr) {
    case SCR_MAIN:    draw_main(now); break;
    case SCR_PRESETS: draw_presets(now); break;
    case SCR_GRAPH: {
        static uint32_t last_g;
        if (full || now - last_g >= 200) { last_g = now; draw_graph(now); }
        break;
    }
    case SCR_MENU:    draw_menu(now); break;
    }
    full = false;
}

void ui_pair_screen(const char *status, uint16_t status_color, int secs_left)
{
    static bool drawn;
    if (status == NULL) { drawn = false; full = true; return; }   /* leave */
    if (!drawn) {
        gfx_fill(0, 0, GFX_W, GFX_H, C_BG);
        gfx_fill(0, 0, GFX_W, BAR_H, C_PANEL);
        gfx_text(&font_sml, 8, 2, "PAIR WITH P906", C_TEXT, C_PANEL);
        gfx_text(&font_sml, 8, 40, "On the P906: open the menu, page 2/6", C_TEXT, C_BG);
        gfx_text(&font_sml, 8, 58, "\"TX and RX Addr\", and start", C_TEXT, C_BG);
        gfx_text(&font_sml, 8, 76, "\"Match RX Addr..\". Keep units close.", C_TEXT, C_BG);
        gfx_text(&font_sml, 8, 180, "Press E to cancel.", C_DIM, C_BG);
        drawn = true;
    }
    gfx_field(&font_med, 8, 112, 22, status, status_color, C_BG);
    char b[16] = "";
    if (secs_left >= 0) snprintf(b, sizeof b, "%2ds", secs_left);
    gfx_field(&font_med, 260, 112, 4, b, C_DIM, C_BG);
}
