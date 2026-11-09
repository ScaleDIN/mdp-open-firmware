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

void mdp_req9(uint8_t out[32], const uint8_t id[4], uint8_t slot, uint16_t color)
{
    uint8_t p[8] = { id[0], id[1], id[2], id[3], slot, 1, (uint8_t)(color >> 8), (uint8_t)color };
    mdp_frame(out, 9, p, sizeof p);
}

bool mdp_parse9(const uint8_t f[32], const uint8_t id[4], mdp_cal_t *c)
{
    if (f[0] != 9 || f[1] != 14 || !mdp_valid(f)) return false;
    const uint8_t *p = f + 2;
    if (memcmp(p, id, 4) != 0 || p[13] != 2) return false;
    c->hv_zero = (uint16_t)(p[5] << 8 | p[6]);
    c->hv_gain = (uint16_t)(p[7] << 8 | p[8]);
    c->hc_zero = (uint16_t)(p[9] << 8 | p[10]);
    c->hc_gain = (uint16_t)(p[11] << 8 | p[12]);
    c->valid = true;
    return true;
}

/* mdp_commander: V = (adc*16 - zero) * gain / 1e5 mV; I = (adc*4 - zero) * gain / 1e5 * 2 mA */
uint32_t mdp_out_mV(const mdp_status_t *s, const mdp_cal_t *c)
{
    int64_t sum = 0;
    for (int i = 0; i < 4; i++) sum += (int64_t)s->adc_v[i] * 16 - c->hv_zero;
    int64_t v = (sum * c->hv_gain + 200000) / 400000;    /* /4 samples, /1e5, rounded */
    return v < 0 ? 0 : (uint32_t)v;
}

uint32_t mdp_out_mA(const mdp_status_t *s, const mdp_cal_t *c)
{
    int64_t sum = 0;
    for (int i = 0; i < 4; i++) sum += (int64_t)s->adc_i[i] * 4 - c->hc_zero;
    int64_t v = (sum * c->hc_gain * 2 + 200000) / 400000;
    return v < 0 ? 0 : (uint32_t)v;
}

/* 12.345 -> 01 23 45 (6 packed BCD digits, 3 decimals) */
static void put_bcd6(uint8_t *o, uint32_t milli)
{
    uint8_t d[6];
    for (int i = 5; i >= 0; i--) { d[i] = (uint8_t)(milli % 10); milli /= 10; }
    for (int i = 0; i < 3; i++) o[i] = (uint8_t)(d[2 * i] << 4 | d[2 * i + 1]);
}

static void cmd7(uint8_t out[32], const uint8_t id[4], uint8_t slot, const uint8_t *c, uint8_t n)
{
    uint8_t p[11] = { id[0], id[1], id[2], id[3], slot, 1 };
    memcpy(p + 6, c, n);
    mdp_frame(out, 7, p, (uint8_t)(6 + n));
}

void mdp_set_mV(uint8_t out[32], const uint8_t id[4], uint8_t slot, uint32_t mV)
{
    uint8_t c[5] = { 0x03, 0x03 };
    put_bcd6(c + 2, mV > MDP_MAX_MV ? MDP_MAX_MV : mV);
    cmd7(out, id, slot, c, sizeof c);
}

void mdp_set_mA(uint8_t out[32], const uint8_t id[4], uint8_t slot, uint32_t mA)
{
    uint8_t c[5] = { 0x02, 0x03 };
    put_bcd6(c + 2, mA > MDP_MAX_MA ? MDP_MAX_MA : mA);
    cmd7(out, id, slot, c, sizeof c);
}

void mdp_output(uint8_t out[32], const uint8_t id[4], uint8_t slot, bool on)
{
    uint8_t c[3] = { 0x0C, 0x00, on ? 1 : 0 };
    cmd7(out, id, slot, c, sizeof c);
}

static uint32_t bcd(const uint8_t *p, unsigned n)
{
    uint32_t v = 0;
    while (n--) { v = v * 100u + (*p >> 4) * 10u + (*p & 0xFu); p++; }
    return v;
}

void mdp_req_pd(uint8_t out[32], const uint8_t id[4])
{
    mdp_frame(out, 0x0D, id, 4);
}

bool mdp_parse_pd(const uint8_t f[32], mdp_pd_t *o)
{
    if (f[0] != 0x0D || f[1] != 12 || !mdp_valid(f) || f[2] != 1) return false;
    const uint8_t *p = f + 3;
    o->pd_state = p[0]; o->txq = p[1]; o->type = p[2]; o->sent = p[3]; o->pdos = p[4];
    o->mV = (uint16_t)(p[5] | p[6] << 8);
    o->mA = (uint16_t)(p[7] | p[8] << 8);
    o->max_W = (uint16_t)(p[9] | p[10] << 8);
    return true;
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
