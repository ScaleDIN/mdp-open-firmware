#include <string.h>
#include "board.h"
#include "mdp.h"
#include "radio.h"

void mdp_frame(uint8_t out[32], uint8_t type, const uint8_t *pl, uint8_t n)
{
    memset(out, 0, 32);
    if (n > 28) n = 28;
    out[0] = type;
    out[1] = n;
    if (n) memcpy(out + 2, pl, n);
    uint8_t c = 0x88;
    for (unsigned i = 0; i < n + 2u; i++) c ^= out[i];
    out[n + 2] = c;
}

bool mdp_valid(const uint8_t f[32])
{
    uint8_t n = f[1];
    if (n > 28) return false;
    uint8_t c = 0x88;
    for (unsigned i = 0; i < n + 2u; i++) c ^= f[i];
    return c == f[n + 2];
}

/* type 7 with no command = status poll. slot = M01 channel index, last byte = "blink" */
void mdp_poll7(uint8_t out[32], const uint8_t id[4], uint8_t slot)
{
    uint8_t p[6] = { id[0], id[1], id[2], id[3], slot, 1 };
    mdp_frame(out, 7, p, sizeof p);
}

void mdp_call_id(uint8_t out[32])
{
    uint8_t m = 'M';
    mdp_frame(out, 5, &m, 1);
}

/* type 6: give the P906 its new address (radio register order) and channel */
void mdp_dispatch(uint8_t out[32], const uint8_t addr[5], uint8_t ch)
{
    uint8_t p[6] = { addr[0], addr[1], addr[2], addr[3], addr[4], ch };
    mdp_frame(out, 6, p, sizeof p);
}

static uint32_t bcd(const uint8_t *p, unsigned n)
{
    uint32_t v = 0;
    while (n--) { v = v * 100u + (*p >> 4) * 10u + (*p & 0xFu); p++; }
    return v;
}

bool mdp_parse7(const uint8_t f[32], mdp_status_t *s)
{
    if (f[0] != 7 || !mdp_valid(f) || (f[1] != 0x16 && f[1] != 0x1C)) return false;
    const uint8_t *p = f + 2;
    s->err = p[0];
    s->locked = p[1];
    s->temp_dC = (uint16_t)bcd(p + 2, 2);         /* "034.0" */
    s->state = p[4];
    s->in_mV = bcd(p + 5, 3);                     /* "025.271" V */
    s->in_mA = bcd(p + 8, 2);                     /* "2.072" A */
    for (int i = 0; i < 4; i++) {
        const uint8_t *a = p + 10 + 3 * i;        /* 12-bit V, 12-bit I */
        s->adc_v[i] = (uint16_t)((a[0] << 4) | (a[1] >> 4));
        s->adc_i[i] = (uint16_t)(((a[1] & 0xF) << 8) | a[2]);
    }
    s->has_set = f[1] == 0x1C;
    if (s->has_set) {
        s->set_mV = bcd(p + 22, 3);
        s->set_mA = bcd(p + 25, 3);
    }
    return true;
}

xch_t mdp_exchange(const uint8_t req[32], uint8_t type, uint8_t resp[32], uint32_t timeout_ms)
{
    memset(resp, 0, 32);
    if (!radio_tx(req)) return XCH_NOACK;
    uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms) {
        if (!radio_rx(resp, timeout_ms - (millis() - t0))) break;
        if (resp[0] == type && mdp_valid(resp)) return XCH_OK;
    }
    return resp[0] ? XCH_BADREPLY : XCH_NOREPLY;
}
