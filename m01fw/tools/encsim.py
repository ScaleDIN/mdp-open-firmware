"""Replay a captured encoder edge log (see elog.py) through candidate decoders.

usage: python -I encsim.py elog.bin split_ms
Ground truth for the capture: before split_ms = 10 CW clicks, after = 10 CCW clicks.
"""
import struct
import sys

d = open(sys.argv[1], "rb").read()
split = float(sys.argv[2]) * 4
head = struct.unpack_from("<I", d, 0)[0]
ents = struct.unpack_from("<1024I", d, 4)
ev = [(e >> 8, e & 3) for e in (ents[i % 1024] for i in range(max(0, head - 1024), head))
      if ((e >> 4) & 0xF) == 1 and ((e >> 2) & 3) == 0]


def samples():
    """Yield (tick, state) for every 4 kHz tick, reconstructed from edges."""
    t0, s = ev[0]
    for (t, ns) in ev[1:]:
        while t0 < t:
            yield t0, s
            t0 += 1
        s = ns
    for _ in range(400):
        yield t0, s
        t0 += 1


def alg_current(settle=4, mid=2):
    st = None; cand_n = mid_n = 0; saw = False; out = []
    for t, s in samples():
        if st is None:
            if s in (0, 3): st = s
            continue
        if s == st: cand_n = mid_n = 0; saw = False; continue
        if s == 1:
            mid_n += 1
            if mid_n >= mid: saw = True
        else: mid_n = 0
        if s == st ^ 3:
            cand_n += 1
            if cand_n >= settle:
                out.append((t, 1 if ((s == 3) != saw) else -1)); st = s; cand_n = mid_n = 0; saw = False
        else: cand_n = 0
    return out


def alg_lead(deb=8, lead_min=8, rev_ms=0):
    """Debounce A and B separately (deb samples stable). A detent = both debounced
    lines equal again. Direction from which debounced line moved first: A first = CW,
    B first = CCW; if the lead is shorter than lead_min samples treat the edge as
    simultaneous (rise together = CW, fall together = CCW). Optional reversal
    suppression: a step opposite to the previous one within rev_ms is dropped."""
    da = db = None; ca = cb = 0; first = None; t_first = 0
    out = []; last_t = -10**9; last_dir = 0
    for t, s in samples():
        a, b = s >> 1, s & 1
        if da is None: da, db = a, b; continue
        ca = ca + 1 if a != da else 0
        cb = cb + 1 if b != db else 0
        ch = []
        if ca >= deb: da = a; ca = 0; ch.append('A')
        if cb >= deb: db = b; cb = 0; ch.append('B')
        if not ch: continue
        if da != db:                       # one line moved: remember which and when
            if first is None: first, t_first = ch[0], t
            elif first in ch and len(ch) == 1: first = None   # it moved back: cancel
            continue
        # both equal again: a detent (or a cancelled half-move)
        if first is None and len(ch) == 2: lead = None
        elif first is not None: lead = first if (t - t_first) >= lead_min else None
        else: continue
        if first is not None and ((first == 'A' and da == 1 and db == 1 and False)): pass
        rising = da == 1
        if lead is None: dirn = 1 if rising else -1
        else: dirn = 1 if lead == 'A' else -1
        first = None
        if rev_ms and dirn != last_dir and (t - last_t) < rev_ms * 4 and last_dir:
            last_t = t; continue
        out.append((t, dirn)); last_t = t; last_dir = dirn
    return out


def alg_hold(settle=4, hold=16):
    """Detent = settled 00/11 (new state stable `settle` samples). Direction from the
    run of A=0,B=1 that *immediately* precedes the final run of the target state:
    if it held >= `hold` samples the edge was skewed, otherwise simultaneous.
      00->11: skewed = CCW (B rose first), simultaneous = CW
      11->00: skewed = CW  (A fell first), simultaneous = CCW"""
    st = None; run_s = None; run_n = 0; prev_s = None; prev_n = 0; out = []
    for t, s in samples():
        if s == run_s: run_n += 1
        else: prev_s, prev_n = run_s, run_n; run_s, run_n = s, 1
        if st is None:
            if s in (0, 3) and run_n >= settle: st = s
            continue
        if s == st ^ 3 and run_n == settle:
            skew = prev_s == 1 and prev_n >= hold
            out.append((t, (-1 if skew else 1) if s == 3 else (1 if skew else -1)))
            st = s
    return out


def alg_leave(settle=8, hold=12):
    """Detent = settled 00/11. Transition time = from the last moment the state left
    the old detent (any return to it restarts the timer) to the first sample of the
    final run of the new detent. >= `hold` samples = skewed edge, else simultaneous."""
    st = None; run_s = None; run_n = 0; t_leave = None; t_run = 0; out = []
    for t, s in samples():
        if s == run_s: run_n += 1
        else: run_s, run_n, t_run = s, 1, t
        if st is None:
            if s in (0, 3) and run_n >= settle: st = s
            continue
        if s == st: t_leave = None; continue
        if t_leave is None: t_leave = t
        if s == st ^ 3 and run_n == settle:
            skew = (t_run - t_leave) >= hold
            out.append((t, (-1 if skew else 1) if s == 3 else (1 if skew else -1)))
            st = s; t_leave = None
    return out


def run_filter(out, w_ms=100):
    """Opposite step within w_ms of the previous one: if the previous step started a
    run (came after a pause) it was the wrong one, so flip it; otherwise drop the new one."""
    res = []; last_t = -10**9; run_dir = 0; provisional = False
    for t, d in out:
        if t - last_t <= w_ms * 4 and d != run_dir:
            if provisional:
                # undo the wrong step, re-apply it flipped, then apply this one
                res.append((t, 3 * d)); run_dir = d; provisional = False
            last_t = t
            continue
        provisional = (t - last_t) > w_ms * 4
        res.append((t, d)); run_dir = d; last_t = t
    return res


def score(out):
    a = [d for t, d in out if t < split]; b = [d for t, d in out if t >= split]
    return (f"CW run: {a.count(1):2}+ {a.count(-1):2}- | CCW run: {b.count(1):2}+ {b.count(-1):2}-"
            f"  => wrong-direction steps: {a.count(-1) + b.count(1)}")


print("current (settle 1ms, mid 0.5ms):  ", score(alg_current()))
for settle in (4, 8):
    for hold in (2, 4, 8):
        print(f"last-run settle={settle/4:3.1f} hold={hold/4:3.1f}ms:", score(alg_hold(settle, hold)))
for settle in (4, 8, 12):
    for hold in (4, 8, 12, 16, 20):
        print(f"leave    settle={settle/4:3.1f} hold={hold/4:3.1f}ms:", score(alg_leave(settle, hold)))
if len(sys.argv) > 3:
    for t, dirn in alg_leave(int(sys.argv[3]), int(sys.argv[4])):
        print(f"{t/4:10.2f} {dirn:+d}")
