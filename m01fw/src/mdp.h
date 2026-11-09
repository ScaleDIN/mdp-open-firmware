#pragma once
/* M01 <-> P906 packet protocol (see notes/m01-hardware.md, mdp_commander).
 * Frame: type, len, payload[len], xor  (xor seeded with 0x88), in a 32-byte radio payload. */
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t err, locked, state;          /* state: 0 off, 1 CC, 2 CV, 3 on */
    uint16_t temp_dC;                    /* 0.1 degC */
    uint32_t in_mV, in_mA;
    uint16_t adc_v[4], adc_i[4];         /* raw 12-bit samples */
    bool has_set;
    uint32_t set_mV, set_mA;
} mdp_status_t;

typedef struct {                         /* from the type 9 reply */
    bool valid;
    uint16_t hv_zero, hv_gain, hc_zero, hc_gain;
} mdp_cal_t;

void mdp_frame(uint8_t out[32], uint8_t type, const uint8_t *payload, uint8_t len);
/* type 9: sets the P906 LED colour (RGB565, big-endian on air) and returns calibration */
void mdp_req9(uint8_t out[32], const uint8_t id[4], uint8_t slot, uint16_t led_color);
bool mdp_parse9(const uint8_t f[32], const uint8_t id[4], mdp_cal_t *c);
/* calibrated output from a status reply (average of its 4 ADC samples) */
uint32_t mdp_out_mV(const mdp_status_t *s, const mdp_cal_t *c);
uint32_t mdp_out_mA(const mdp_status_t *s, const mdp_cal_t *c);
/* type 7 commands; the P906 answers each with a status reply */
void mdp_set_mV(uint8_t out[32], const uint8_t id[4], uint8_t slot, uint32_t mV);
void mdp_set_mA(uint8_t out[32], const uint8_t id[4], uint8_t slot, uint32_t mA);
void mdp_output(uint8_t out[32], const uint8_t id[4], uint8_t slot, bool on);
#define MDP_MAX_MV 30000u
#define MDP_MAX_MA 10000u
bool mdp_valid(const uint8_t f[32]);
void mdp_poll7(uint8_t out[32], const uint8_t id[4], uint8_t slot);
void mdp_call_id(uint8_t out[32]);
void mdp_dispatch(uint8_t out[32], const uint8_t addr[5], uint8_t ch);
bool mdp_parse7(const uint8_t f[32], mdp_status_t *s);

/* type 0x0D: input source info. Only P906 firmware built with tools/pdfix.py
 * answers it (version string 3.01PD2); stock firmware does not. */
typedef struct {
    uint8_t pd_state, txq, type, sent, pdos;  /* type: 1 fixed, 2 PPS; pdos 0 = no PD source */
    uint16_t mV, mA, max_W;                   /* contract; adaptor's best fixed PDO */
} mdp_pd_t;
void mdp_req_pd(uint8_t out[32], const uint8_t id[4]);
bool mdp_parse_pd(const uint8_t f[32], mdp_pd_t *p);

/* one request/response exchange: tx, then listen for a reply of `type` */
typedef enum { XCH_OK, XCH_NOACK, XCH_NOREPLY, XCH_BADREPLY } xch_t;
xch_t mdp_exchange(const uint8_t req[32], uint8_t type, uint8_t resp[32], uint32_t timeout_ms);
