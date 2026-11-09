/* Keys and the two Kailh EN8080 encoders.
 *
 * Encoder physics (measured on this unit, see m01fw/tools/ and notes):
 * detents alternate between A=B=1 and A=B=0. Each detent change is either
 *   - a "common" edge: A and B flip within the same microsecond (it's the shared
 *     common contact making/breaking), so it carries NO direction information, or
 *   - a "skewed" edge: passes through A=0,B=1 for 0.25..20 ms.
 *   CW : 00->11 common, 11->00 skewed (A falls first)
 *   CCW: 11->00 common, 00->11 skewed (B rises first)
 * Contacts bounce at us scale and B can chatter for 200 ms at rest.
 *
 * Decoder:
 *  1. Detent change = new state 00/11 stable for SETTLE. Its "skew time" = from
 *     the moment the state last left the old detent (bounce back restarts it) to
 *     the start of the final run of the new detent.
 *  2. Pair rule: within PAIR_WINDOW of the previous detent change, the one of the
 *     two with the longer skew time is the skewed edge, which fixes the direction
 *     (a relative comparison works even when the skew is only 0.25 ms).
 *  3. After a pause there's no partner: skew time >= HOLD means skewed edge.
 *  4. Run filter (enc_emit) corrects a misread first step of a run.
 * Replayed against three captures (tools/encsim2.py): no uncorrected misreads.
 */
#include "board.h"
#include "input.h"

#define SETTLE_SAMPLES  12   /* new detent stable 3 ms (4 kHz sampling) */
#define HOLD_SAMPLES    6    /* lone edge: >= 1.5 ms skew time = skewed edge */
#define PAIR_WINDOW     600  /* 150 ms: compare with the previous detent change */
#define RUN_WINDOW      400  /* 100 ms: see enc_emit() */
#define KEY_SAMPLES     40   /* 10 ms key debounce */
#define NONE            0xFFFFFFFFu

typedef struct {
    GPIO_TypeDef *pa, *pb;
    uint8_t ia, ib;
    uint8_t settled;          /* 0 or 3, 0xFF = unknown */
    uint8_t run_s;            /* current raw state and how long it has held */
    uint32_t run_n, run_start;
    uint32_t t_leave;         /* when the state last left the settled detent */
    uint32_t prev_t, prev_skew;   /* previous detent change (pair rule) */
    uint8_t prev_to;
    bool have_prev;
    int8_t last_dir;
    uint32_t last_step;       /* run filter state */
    int8_t run_dir;
    bool provisional;
    uint8_t raw;
    volatile int32_t count;
} enc_t;

static enc_t enc[ENC_COUNT] = {
    { .pa = ENC_MENU_A_PORT, .pb = ENC_MENU_B_PORT, .ia = ENC_MENU_A_PIN, .ib = ENC_MENU_B_PIN,
      .settled = 0xFF, .run_s = 0xFF, .t_leave = NONE },
    { .pa = ENC_VAL_A_PORT,  .pb = ENC_VAL_B_PORT,  .ia = ENC_VAL_A_PIN,  .ib = ENC_VAL_B_PIN,
      .settled = 0xFF, .run_s = 0xFF, .t_leave = NONE },
};

static uint8_t key_db, key_cnt[KEY_COUNT];
static volatile uint8_t key_latched;

/* Edge recorder for tuning (read over SWD, decode with tools/elog.py):
 * entry = tick<<8 | enc<<4 | flags<<2 | state; flags 1 = counted CW, 2 = CCW. */
#define ELOG_N 1024
struct elog { uint32_t head; uint32_t e[ELOG_N]; } volatile elog __attribute__((used));
static void elog_put(int idx, uint8_t flags, uint8_t s)
{
    elog.e[elog.head % ELOG_N] = (sys_ticks << 8) | ((uint32_t)idx << 4) | ((uint32_t)flags << 2) | s;
    elog.head++;
}

void input_init(void)
{
    for (int i = 0; i < KEY_COUNT; i++) {
        gpio_set(KEY_PORT, KEY_PIN(i));          /* pull-up */
        gpio_cfg(KEY_PORT, KEY_PIN(i), GPIO_IN_PULL);
    }
    for (int i = 0; i < ENC_COUNT; i++) {
        gpio_set(enc[i].pa, enc[i].ia);
        gpio_cfg(enc[i].pa, enc[i].ia, GPIO_IN_PULL);
        gpio_set(enc[i].pb, enc[i].ib);
        gpio_cfg(enc[i].pb, enc[i].ib, GPIO_IN_PULL);
    }
}

/* Run filter. An opposite step within 100 ms of the previous one is not a real
 * reversal (nobody reverses a detent knob that fast):
 *  - if the previous step started a run (after a pause), it was the misread one:
 *    undo it, re-apply it flipped, apply this one;
 *  - otherwise this step is the misread one: drop it. */
static void enc_emit(enc_t *e, int8_t d, uint32_t now)
{
    if (now - e->last_step <= RUN_WINDOW && d != e->run_dir) {
        if (e->provisional) {
            e->count += 3 * d;
            e->run_dir = d;
            e->provisional = false;
        }
        e->last_step = now;
        return;
    }
    e->provisional = now - e->last_step > RUN_WINDOW;
    e->count += d;
    e->run_dir = d;
    e->last_step = now;
}

/* direction implied if the edge into `to` is the skewed one */
static int8_t skew_dir(uint8_t to) { return to == 3 ? -1 : 1; }

static int8_t classify(enc_t *e, uint8_t to, uint32_t skew, uint32_t now)
{
    if (e->have_prev && now - e->prev_t <= PAIR_WINDOW) {
        if (skew > e->prev_skew) return skew_dir(to);          /* this edge is skewed */
        if (e->prev_skew > skew) return skew_dir(e->prev_to);  /* previous one was */
        if (e->last_dir) return e->last_dir;                   /* tie: keep going */
    }
    return skew >= HOLD_SAMPLES ? skew_dir(to) : (int8_t)-skew_dir(to);
}

static void enc_step(enc_t *e, uint32_t now)
{
    uint8_t s = (uint8_t)((gpio_get(e->pa, e->ia) << 1) | gpio_get(e->pb, e->ib));
    if (s != e->raw) elog_put((int)(e - enc), 0, s);
    e->raw = s;

    if (s == e->run_s) e->run_n++;
    else { e->run_s = s; e->run_n = 1; e->run_start = now; }

    if (e->settled == 0xFF) {                    /* first detent after boot */
        if ((s == 0 || s == 3) && e->run_n >= SETTLE_SAMPLES) e->settled = s;
        return;
    }
    if (s == e->settled) { e->t_leave = NONE; return; }
    if (e->t_leave == NONE) e->t_leave = now;

    if (s == (e->settled ^ 3) && e->run_n == SETTLE_SAMPLES) {
        uint32_t skew = e->run_start - e->t_leave;
        int8_t d = classify(e, s, skew, now);
        enc_emit(e, d, now);
        elog_put((int)(e - enc), d > 0 ? 1 : 2, s);
        e->prev_t = now; e->prev_skew = skew; e->prev_to = s;
        e->have_prev = true; e->last_dir = d;
        e->settled = s;
        e->t_leave = NONE;
    }
}

void input_sample(void)
{
    uint32_t now = sys_ticks;
    for (int i = 0; i < ENC_COUNT; i++) enc_step(&enc[i], now);

    for (int i = 0; i < KEY_COUNT; i++) {
        bool down = !gpio_get(KEY_PORT, KEY_PIN(i));
        bool cur = (key_db >> i) & 1u;
        if (down == cur) { key_cnt[i] = 0; continue; }
        if (++key_cnt[i] >= KEY_SAMPLES) {
            key_cnt[i] = 0;
            key_db ^= (uint8_t)(1u << i);
            if (down) key_latched |= (uint8_t)(1u << i);
        }
    }
}

uint8_t keys_state(void) { return key_db; }

uint8_t keys_pressed(void)
{
    __disable_irq();
    uint8_t k = key_latched;
    key_latched = 0;
    __enable_irq();
    return k;
}

int32_t enc_count(int i) { return enc[i].count; }
uint8_t enc_raw(int i) { return enc[i].raw; }
