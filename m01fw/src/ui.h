#pragma once
/* M01 user interface. Hardware-independent: it gets status/input and returns
 * commands; drawing goes through gfx.h (device LCD or host screenshots). */
#include <stdint.h>
#include <stdbool.h>
#include "mdp.h"

#define UI_PRESETS 6

typedef struct { uint16_t mV, mA; } ui_preset_t;   /* mV == 0xFFFF: empty slot */

typedef struct {
    ui_preset_t preset[UI_PRESETS];
    uint8_t muted;            /* master: silences every beep */
    uint8_t beep_cc;          /* beep when the output enters current limit */
    uint8_t key_click;
    uint8_t pad;              /* old flash pages: unknown; don't reuse */
    /* added later: old pages read 0xFF here, ui_init maps anything but 0/1 to 0 */
    uint8_t wheel_up_inc;     /* value wheel rolled up (towards the rear) increases */
    uint8_t confirm_set;      /* wheel edits of V/I wait for APPLY */
} ui_settings_t;

bool ui_sound_on(void);       /* for beeps outside the UI (boot, pairing) */

typedef enum { UC_NONE, UC_SET_V, UC_SET_I, UC_OUT_ON, UC_OUT_OFF, UC_PAIR } ui_cmd_kind_t;
typedef struct { ui_cmd_kind_t kind; uint32_t value; } ui_cmd_t;

void ui_defaults(ui_settings_t *s);
void ui_init(const ui_settings_t *s, const uint8_t id[4]);
void ui_link(bool linked);
void ui_pd(const mdp_pd_t *pd);   /* input source info; NULL = P906 doesn't report it */
/* new status from the P906 (call for every successful poll) */
void ui_status(const mdp_status_t *st, bool cal_ok, uint32_t out_mV, uint32_t out_mA, uint32_t now_ms);
/* keys: bit per newly pressed key (A..E); knob deltas in detents */
void ui_input(uint8_t keys, int dmenu, int dvalue, uint32_t now_ms);
void ui_draw(uint32_t now_ms);
bool ui_next_cmd(ui_cmd_t *c, uint32_t now_ms);   /* next radio command, if any */
void ui_cmd_done(const ui_cmd_t *c, bool ok, uint32_t now_ms);
bool ui_take_settings(ui_settings_t *out);        /* true once after settings change */
void ui_redraw(void);                             /* force a full redraw */
void ui_toast(const char *s, uint16_t color, uint32_t now_ms, uint32_t ms);
void ui_set_id(const uint8_t id[4]);

/* pairing screen, drawn by the pairing routine in main.c */
void ui_pair_screen(const char *status, uint16_t status_color, int secs_left);

/* platform hooks */
void ui_hal_beep(uint32_t ms);

/* exposed for the host renderer */
#define UI_HIST 6000                               /* 10 min at 10 Hz */
void ui_hist_push(uint16_t mV, uint16_t mA);
