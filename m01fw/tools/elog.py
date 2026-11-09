"""Decode the encoder edge log uploaded from RAM (`elog` symbol, 4 + 4096 bytes).

usage: python -I elog.py elog.bin
Each entry: tick(4 kHz)<<8 | enc<<4 | flags<<2 | state(A<<1|B); flags 1=CW count, 2=CCW count.
"""
import struct
import sys

d = open(sys.argv[1], "rb").read()
head = struct.unpack_from("<I", d, 0)[0]
N = 1024
ents = struct.unpack_from(f"<{N}I", d, 4)
idx = range(max(0, head - N), head)
rows = [ents[i % N] for i in idx]
names = ("MENU", "VALUE")
last_t = {}
print(f"{head} entries logged, showing {len(rows)}")
for e in rows:
    t, enc, fl, s = e >> 8, (e >> 4) & 0xF, (e >> 2) & 3, e & 3
    dt = (t - last_t.get(enc, t)) * 0.25
    last_t[enc] = t
    tag = {1: "  <== +1 (CW)", 2: "  <== -1 (CCW)"}.get(fl, "")
    print(f"{t * 0.25:10.2f} ms  +{dt:9.2f}  {names[enc] if enc < 2 else enc:5}  AB={s >> 1}{s & 1}{tag}")
