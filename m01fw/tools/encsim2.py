"""Replay 4 kHz edge logs (elog format, see elog.py) through the pair decoder.

usage: python -I encsim2.py elog.bin enc_index [split_ms]
Prints the decoded step string per run (runs separated by > 300 ms pauses).
If split_ms is given, scores: before = all CW, after = all CCW.
"""
import struct
import sys

SETTLE, HOLD, PAIR_W, RUN_W = 12, 6, 600, 400   # samples at 4 kHz


def load(path, enc):
    d = open(path, "rb").read()
    head = struct.unpack_from("<I", d, 0)[0]
    ents = struct.unpack_from("<1024I", d, 4)
    return [(e >> 8, e & 3) for e in (ents[i % 1024] for i in range(max(0, head - 1024), head))
            if ((e >> 4) & 0xF) == enc and ((e >> 2) & 3) == 0]


def samples(ev):
    t0, s = ev[0]
    for t, ns in ev[1:]:
        while t0 < t:
            yield t0, s
            t0 += 1
        s = ns
    for _ in range(400):
        yield t0, s
        t0 += 1


def transitions(ev):
    """(t, from, to, dur) per detent change; dur = last leave -> start of final run."""
    st = None; run_s = None; run_n = 0; run_start = 0; t_leave = None
    for t, s in samples(ev):
        if s == run_s: run_n += 1
        else: run_s, run_n, run_start = s, 1, t
        if st is None:
            if s in (0, 3) and run_n >= SETTLE: st = s
            continue
        if s == st: t_leave = None; continue
        if t_leave is None: t_leave = t
        if s == st ^ 3 and run_n == SETTLE:
            yield t, st, s, run_start - t_leave
            st = s; t_leave = None


def skew_dir(to_state):
    """direction implied if the edge INTO to_state is the skewed one"""
    return -1 if to_state == 3 else 1          # 00->11 skew = CCW, 11->00 skew = CW


def decode(ev, pair=True):
    out = []; prev = None; last_dir = 0
    for t, a, b, dur in transitions(ev):
        if pair and prev and t - prev[0] <= PAIR_W:
            pt, pb, pdur = prev
            if dur > pdur: d = skew_dir(b)           # this edge is the skewed one
            elif pdur > dur: d = skew_dir(pb)        # previous edge was the skewed one
            else: d = last_dir or (skew_dir(b) if dur >= HOLD else -skew_dir(b))
        else:
            d = skew_dir(b) if dur >= HOLD else -skew_dir(b)
        out.append((t, d)); prev = (t, b, dur); last_dir = d
    return out


def run_filter(out):
    res = []; last_t = -10**9; run_dir = 0; prov = False
    for t, d in out:
        if t - last_t <= RUN_W and d != run_dir:
            if prov: res.append((t, 3 * d)); run_dir = d; prov = False
            last_t = t
            continue
        prov = t - last_t > RUN_W
        res.append((t, d)); run_dir = d; last_t = t
    return res


def runs(out):
    s = []; line = ""; last = None
    for t, d in out:
        if last is not None and t - last > 1200: s.append(line); line = ""
        line += {1: "+", -1: "-", 3: "+++", -3: "---"}.get(d, "?")
        last = t
    s.append(line)
    return s


ev = load(sys.argv[1], int(sys.argv[2]))
for name, out in (("abs 3ms", decode(ev, False)), ("pair", decode(ev, True)),
                  ("pair+filter", run_filter(decode(ev, True)))):
    print(f"{name:12}", " | ".join(runs(out)))
    if len(sys.argv) > 3:
        sp = float(sys.argv[3]) * 4
        a = [d for t, d in out if t < sp]; b = [d for t, d in out if t >= sp]
        print(f"{'':12} CW run net {sum(a):+d} (wrong {sum(1 for x in a if x < 0)}), "
              f"CCW run net {sum(b):+d} (wrong {sum(1 for x in b if x > 0)})")
