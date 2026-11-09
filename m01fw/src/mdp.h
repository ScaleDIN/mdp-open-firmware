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

void mdp_frame(uint8_t out[32], uint8_t type, const uint8_t *payload, uint8_t len);
bool mdp_valid(const uint8_t f[32]);
void mdp_poll7(uint8_t out[32], const uint8_t id[4], uint8_t slot);
void mdp_call_id(uint8_t out[32]);
void mdp_dispatch(uint8_t out[32], const uint8_t addr[5], uint8_t ch);
bool mdp_parse7(const uint8_t f[32], mdp_status_t *s);

/* one request/response exchange: tx, then listen for a reply of `type` */
typedef enum { XCH_OK, XCH_NOACK, XCH_NOREPLY, XCH_BADREPLY } xch_t;
xch_t mdp_exchange(const uint8_t req[32], uint8_t type, uint8_t resp[32], uint32_t timeout_ms);
