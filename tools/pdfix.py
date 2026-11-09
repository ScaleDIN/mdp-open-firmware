"""Build MDP_P906_v3.01_pdfix.hex: USB-PD compatibility fixes for P906 firmware v3.01.

usage: python tools/pdfix.py MDP_P906_v3.01.hex out.hex

Patches (see notes in README):
  1. CC attach detect accepts any Rp (BC_LVL >= 1) instead of only >= 1.5 A Rp.
  2. FUSB302 Control3 = 0x07: hardware auto-retry (3x) when no GoodCRC comes back.
  3. Receive dispatcher hook: answers Soft_Reset (Accept + renegotiate),
     Get_Sink_Cap (5 V 3 A + 20 V 5 A), Get_Source_Cap / swaps (Reject or
     Not_Supported), unknown PD3 messages and VDM requests (Not_Supported),
     Wait (retry the request after ~150 ms), Reject (unblock the request queue).
  4. FUSB302 interrupt hook: sends the Request straight from the interrupt
     after Source_Capabilities instead of waiting for the main loop, and
     clears InterruptA/B so a latched RETRYFAIL can't hold INT_N low.
  5. Main-loop PD tick runs with the FUSB302 EXTI (IRQ40) masked, so the
     interrupt can't break a bit-banged I2C transfer in progress.
New code lives in unused flash at 0x0802D100 (app ends at 0x0802D0E8,
calibration starts at 0x0803D000).
"""
import sys, capstone
from intelhex import IntelHex

CAVE = 0x0802D100

# ---- firmware v3.01 addresses ----------------------------------------------
TX_RAW    = 0x0802173A   # tx(buf, len): needs len+8 bytes of buffer
READ_REG  = 0x080216EC   # read FUSB302 register -> byte
DISPATCH  = 0x0801EBC0   # original rx message dispatcher(msg*)
ISR_BODY  = 0x0801EC60   # original EXTI15 body: read FIFO + dispatch
PD_TICK   = 0x0801EDC8   # PD state machine tick(inc_ms)
ST        = 0x200051C4   # +0 type,+4 mV,+8 mA (last request) +0x18 msgID +0x1c state +0x1e flag
TXQ       = 0x200052B4   # +0 tx pending,+4 time,+8 def type,+0xc def mV,+0x10 def mA
NOW_MS    = 0x200053B8   # 1 ms counter (wraps at 1,000,000)
NVIC      = 0xE000E100

# ---- tiny Thumb assembler ---------------------------------------------------
R = {f'r{i}': i for i in range(13)}; R.update(sp=13, lr=14, pc=15)
COND = dict(eq=0, ne=1, hs=2, lo=3, mi=4, pl=5, hi=8, ls=9, ge=10, lt=11, gt=12, le=13)

class Asm:
    def __init__(s, org):
        s.org, s.items, s.labels, s.lits = org, [], {}, []
    def L(s, name): s.items.append(('label', name))
    def __getattr__(s, op):
        return lambda *a: s.items.append((op, a))
    def lit(s, val):
        if val not in s.lits: s.lits.append(val)
        return ('lit', val)

    def size(s, op, a):
        if op == 'label': return 0
        if op in ('bl', 'bw', 'dsb', 'isb', 'ubfx', 'ldrw_lit'): return 4
        return 2

    def build(s):
        # pass 1: addresses
        pc = s.org
        for op, a in s.items:
            if op == 'label': s.labels[a] = pc
            pc += s.size(op, a)
        if s.lits and pc & 2: s.items.append(('nop', ())); pc += 2
        litaddr = {}
        for v in s.lits: litaddr[v] = pc; pc += 4
        # pass 2: encode
        out = bytearray(); pc = s.org
        h = lambda x: out.extend(x.to_bytes(2, 'little'))
        tgt = lambda x: s.labels[x] if isinstance(x, str) else x
        for op, a in s.items:
            if op == 'label': continue
            if op == 'nop': h(0xBF00)
            elif op == 'bx_lr': h(0x4770)
            elif op == 'push' or op == 'pop':
                m = 0
                for r in a:
                    if r in ('lr', 'pc'): m |= 0x100
                    else: m |= 1 << R[r]
                h((0xB400 if op == 'push' else 0xBC00) | m)
            elif op == 'movs': h(0x2000 | R[a[0]] << 8 | a[1])
            elif op == 'cmp':
                if isinstance(a[1], int): h(0x2800 | R[a[0]] << 8 | a[1])
                else: h(0x4280 | R[a[1]] << 3 | R[a[0]])
            elif op == 'adds': h(0x3000 | R[a[0]] << 8 | a[1])
            elif op == 'subs': h(0x3800 | R[a[0]] << 8 | a[1])
            elif op == 'lsls': h(0x0000 | a[2] << 6 | R[a[1]] << 3 | R[a[0]])
            elif op == 'lsrs': h(0x0800 | a[2] << 6 | R[a[1]] << 3 | R[a[0]])
            elif op == 'orrs': h(0x4300 | R[a[1]] << 3 | R[a[0]])
            elif op == 'adds_r': h(0x1800 | R[a[2]] << 6 | R[a[1]] << 3 | R[a[0]])
            elif op == 'mov':
                d, m = R[a[0]], R[a[1]]; h(0x4600 | (d >> 3) << 7 | m << 3 | (d & 7))
            elif op in ('str', 'ldr', 'strb', 'ldrb', 'strh', 'ldrh'):
                t, n, imm = R[a[0]], R[a[1]], a[2]
                base, sc = dict(str=(0x6000, 4), ldr=(0x6800, 4), strb=(0x7000, 1),
                                ldrb=(0x7800, 1), strh=(0x8000, 2), ldrh=(0x8800, 2))[op]
                assert imm % sc == 0 and imm // sc < 32, (op, a)
                h(base | (imm // sc) << 6 | n << 3 | t)
            elif op == 'ldr_lit':
                t, v = R[a[0]], a[1]
                off = litaddr[v] - ((pc + 4) & ~3); assert 0 <= off < 1024 and off % 4 == 0
                h(0x4800 | t << 8 | off // 4)
            elif op == 'add_sp':   # add rd, sp, #imm
                h(0xA800 | R[a[0]] << 8 | a[1] // 4)
            elif op == 'sub_sp': h(0xB080 | a[0] // 4)
            elif op == 'inc_sp': h(0xB000 | a[0] // 4)
            elif op == 'b':
                off = tgt(a[0]) - (pc + 4); assert -2048 <= off < 2048
                h(0xE000 | (off >> 1) & 0x7FF)
            elif op == 'bc':
                off = tgt(a[1]) - (pc + 4); assert -256 <= off < 256, (a, off)
                h(0xD000 | COND[a[0]] << 8 | (off >> 1) & 0xFF)
            elif op in ('bl', 'bw'):
                off = tgt(a[0]) - (pc + 4); out.extend(enc_bl(off, op == 'bl'))
            elif op == 'dsb': h(0xF3BF); h(0x8F4F)
            elif op == 'isb': h(0xF3BF); h(0x8F6F)
            elif op == 'ubfx':
                d, n, lsb, w = R[a[0]], R[a[1]], a[2], a[3]
                h(0xF3C0 | n); h((lsb >> 2) << 12 | d << 8 | (lsb & 3) << 6 | (w - 1))
            else: raise ValueError(op)
            pc += s.size(op, a)
        if len(out) + s.org != pc: raise AssertionError('size mismatch')
        for v in s.lits: out.extend(v.to_bytes(4, 'little'))
        return bytes(out)

def enc_bl(off, link=True):
    assert -(1 << 24) <= off < (1 << 24) and off % 2 == 0
    imm = (off >> 1) & 0xFFFFFF
    S = (imm >> 23) & 1; I1 = (imm >> 22) & 1; I2 = (imm >> 21) & 1
    J1 = (~I1 ^ S) & 1; J2 = (~I2 ^ S) & 1
    hw1 = 0xF000 | S << 10 | (imm >> 11) & 0x3FF
    hw2 = (0xD000 if link else 0x9000) | J1 << 13 | J2 << 11 | imm & 0x7FF
    return hw1.to_bytes(2, 'little') + hw2.to_bytes(2, 'little')

# ---- new code ----------------------------------------------------------------
a = Asm(CAVE)
a.L('tick')                         # main-loop PD tick, FUSB302 IRQ masked
a.push('r4', 'lr')
a.ldr_lit('r4', NVIC + 0x84); a.movs('r1', 1); a.lsls('r1', 'r1', 8)
a.str('r1', 'r4', 0)             # ICER1 bit8 = IRQ40 (EXTI15_10)
a.dsb(); a.isb()
a.bl(PD_TICK)
a.ldr_lit('r4', NVIC + 0x04); a.movs('r1', 1); a.lsls('r1', 'r1', 8)
a.str('r1', 'r4', 0)             # ISER1
a.pop('r4', 'pc')

a.L('delay')                        # ~1 ms busy wait (ISR safe, no timers)
a.ldr_lit('r0', 24000)
a.L('dl'); a.subs('r0', 1); a.bc('ne', 'dl')
a.bx_lr()

a.L('isr')                          # FUSB302 EXTI body
a.push('r4', 'lr')
a.bl(ISR_BODY)
a.ldr_lit('r4', ST)
a.ldrb('r0', 'r4', 0x1C); a.cmp('r0', 2); a.bc('ne', 'isr_clr')
a.ldrh('r0', 'r4', 0x1E); a.cmp('r0', 1); a.bc('ne', 'isr_clr')
a.bl('delay')                       # let the auto GoodCRC finish first
a.movs('r0', 0); a.bl(PD_TICK)      # send the Request now
a.L('isr_clr')
a.movs('r0', 0x3E); a.bl(READ_REG)  # clear InterruptA (RETRYFAIL etc.)
a.movs('r0', 0x3F); a.bl(READ_REG)  # clear InterruptB
a.pop('r4', 'pc')

# dispatcher hook: r0 = msg (u16 header, then data objects)
a.L('disp')
a.push('r4', 'r5', 'r6', 'r7', 'lr')
a.sub_sp(36)                        # 32-byte tx buffer (+4 keeps 8-byte align)
a.mov('r4', 'r0')
a.ldrh('r5', 'r4', 0)               # r5 = header
a.ubfx('r6', 'r5', 6, 2)            # r6 = spec revision, capped at 3.0 (2)
a.cmp('r6', 2); a.bc('ls', 'rev_ok'); a.movs('r6', 2)
a.L('rev_ok')
a.ubfx('r7', 'r5', 0, 5)            # r7 = message type
a.lsrs('r0', 'r5', 15); a.bc('ne', 'ext')
a.ubfx('r0', 'r5', 12, 3); a.cmp('r0', 0); a.bc('ne', 'data')
# -- control messages
a.cmp('r7', 4);  a.bc('eq', 'c_reject')
a.cmp('r7', 12); a.bc('eq', 'c_wait')
a.cmp('r7', 13); a.bc('eq', 'c_softreset')
a.cmp('r7', 8);  a.bc('eq', 'c_getsinkcap')
a.cmp('r7', 7);  a.bc('eq', 'reply_ns_or_rej')   # Get_Source_Cap: we are sink-only
a.cmp('r7', 9);  a.bc('lo', 'orig')
a.cmp('r7', 11); a.bc('ls', 'reply_rej')         # DR/PR/VCONN_Swap
a.cmp('r7', 14); a.bc('eq', 'reply_ns_or_rej')   # Data_Reset
a.cmp('r7', 0x11); a.bc('hs', 'reply_ns_or_rej') # Get_*_Ext, Get_Status, FR_Swap, ...
a.b('orig')
a.L('data')
a.cmp('r7', 1);  a.bc('eq', 'orig')              # Source_Capabilities
a.cmp('r7', 10); a.bc('eq', 'orig')              # EPR_Mode
a.cmp('r7', 3);  a.bc('eq', 'done')              # BIST: ignore
a.cmp('r7', 6);  a.bc('eq', 'done')              # Alert: ignore
a.cmp('r7', 15); a.bc('ne', 'reply_ns_pd3')
a.ldrh('r0', 'r4', 2)                            # VDM header low half
a.lsls('r1', 'r0', 16); a.bc('pl', 'reply_ns_pd3')  # unstructured VDM
a.lsrs('r1', 'r0', 6); a.lsls('r1', 'r1', 30)    # command type bits 7:6
a.bc('ne', 'done')                               # ACK/NAK/BUSY: no reply
a.b('reply_ns_pd3')
a.L('ext')
a.cmp('r7', 0x10); a.bc('eq', 'orig')            # Extended_Control (EPR KeepAlive_Ack)
a.cmp('r7', 0x11); a.bc('eq', 'orig')            # EPR_Source_Capabilities
a.b('reply_ns_pd3')

a.L('orig')
a.mov('r0', 'r4'); a.bl(DISPATCH)
a.b('out')
a.L('done')
a.movs('r0', 0)
a.L('out')
a.inc_sp(36)
a.pop('r4', 'r5', 'r6', 'r7', 'pc')

a.L('c_reject')                     # our Request was rejected: unblock tx queue
a.ldr_lit('r1', TXQ); a.movs('r0', 0); a.strb('r0', 'r1', 0)
a.b('done')

a.L('c_wait')                       # retry the same request after ~150 ms
a.ldr_lit('r1', TXQ); a.ldr_lit('r2', ST)
a.movs('r0', 0); a.strb('r0', 'r1', 0)
a.ldrb('r0', 'r2', 0); a.strb('r0', 'r1', 8)
a.ldr('r0', 'r2', 8); a.str('r0', 'r1', 0x10)
a.ldr('r0', 'r2', 4); a.str('r0', 'r1', 0x0C)
a.ldr_lit('r0', NOW_MS); a.ldr('r0', 'r0', 0); a.adds('r0', 150)
a.ldr_lit('r3', 999000); a.cmp('r0', 'r3'); a.bc('lo', 'w_ok'); a.movs('r0', 0)
a.L('w_ok')
a.str('r0', 'r1', 4)
a.b('done')

a.L('c_softreset')                  # Accept, reset MessageID, renegotiate
a.ldr_lit('r1', TXQ); a.movs('r0', 0)
a.strb('r0', 'r1', 0); a.str('r0', 'r1', 0x0C); a.str('r0', 'r1', 0x10)
a.ldr_lit('r2', ST); a.strb('r0', 'r2', 0x18)
a.ldrb('r3', 'r2', 0x1C); a.cmp('r3', 0); a.bc('eq', 'sr_send')
a.movs('r3', 1); a.strb('r3', 'r2', 0x1C)       # state 1: wait for Source_Caps
a.strh('r0', 'r2', 0x1E)
a.L('sr_send')
a.movs('r0', 3); a.b('send_ctrl')               # Accept

a.L('c_getsinkcap')                 # Sink_Capabilities: 5 V 3 A, 20 V 5 A
a.movs('r0', 0x20); a.lsls('r0', 'r0', 8)       # numobj 2 <<12 | type 4
a.adds('r0', 4)
a.bl('mkhdr')                                    # r0 = full header, msgID bumped
a.add_sp('r1', 0)
a.strh('r0', 'r1', 0)
a.ldr_lit('r0', 0x1001912C)                      # 5 V 3 A, Higher Capability
a.strh('r0', 'r1', 2); a.lsrs('r0', 'r0', 16); a.strh('r0', 'r1', 4)
a.ldr_lit('r0', 0x000641F4)                      # 20 V 5 A
a.strh('r0', 'r1', 6); a.lsrs('r0', 'r0', 16); a.strh('r0', 'r1', 8)
a.bl('delay')
a.add_sp('r0', 0); a.movs('r1', 10); a.bl(TX_RAW)
a.b('done')

a.L('reply_ns_pd3')                 # PD3: Not_Supported; PD2: stay silent
a.cmp('r6', 2); a.bc('lo', 'done')
a.movs('r0', 0x10); a.b('send_ctrl')
a.L('reply_ns_or_rej')              # PD3: Not_Supported; PD2: Reject
a.cmp('r6', 2); a.bc('lo', 'reply_rej')
a.movs('r0', 0x10); a.b('send_ctrl')
a.L('reply_rej')
a.movs('r0', 4)
a.L('send_ctrl')                    # r0 = control message type
a.bl('mkhdr')
a.add_sp('r1', 0); a.strh('r0', 'r1', 0)
a.bl('delay')
a.add_sp('r0', 0); a.movs('r1', 2); a.bl(TX_RAW)
a.b('done')

a.L('mkhdr')                        # r0 |= rev<<6 | msgID<<9 ; msgID++ (uses r6)
a.lsls('r1', 'r6', 6); a.orrs('r0', 'r1')
a.ldr_lit('r2', ST); a.ldrb('r3', 'r2', 0x18)
a.lsls('r1', 'r3', 29); a.lsrs('r1', 'r1', 20); a.orrs('r0', 'r1')   # (id&7)<<9
a.adds('r3', 1); a.lsls('r3', 'r3', 29); a.lsrs('r3', 'r3', 29)
a.strb('r3', 'r2', 0x18)
a.bx_lr()

# resolve literal placeholders: Asm.lit isn't used inline; register literals used above
for v in (NVIC + 0x84, NVIC + 0x04, 24000, ST, TXQ, NOW_MS, 999000, 0x1001912C, 0x000641F4):
    a.lit(v)
code = a.build()
L = a.labels

# ---- patches to existing code --------------------------------------------------
def thumb_bl(at, target, link=True): return enc_bl(target - (at + 4), link)
PATCHES = [
    # addr, old, new, what
    (0x080219F8, '0228', '0128', 'CC detect: BC_LVL >= 1 (any Rp)'),
    (0x08021A16, '0621', '0721', 'FUSB302 Control3: AUTO_RETRY on, 3 retries'),
    (0x0801EC7E, 'fff79fff', thumb_bl(0x0801EC7E, L['disp']).hex(), 'rx dispatcher -> cave'),
    (0x08021966, 'fdf77bb9', thumb_bl(0x08021966, L['isr'], link=False).hex(), 'EXTI body -> cave'),
    (0x0801EE94, 'c9ed0108', (L['tick'] | 1).to_bytes(4, 'little').hex(), 'PD tick pointer -> cave'),
    # faster negotiation
    (0x08021A42, '2221', '2621', 'Switches1 CC2: AUTO_CRC on from init (was after 1st caps)'),
    (0x08021A80, '2121', '2521', 'Switches1 CC1: AUTO_CRC on from init'),
    (0x0801EC2E, '04d0', '00bf', 'accept the first Source_Caps (state 0 -> 2)'),
    (0x0801ECD0, '41f28830', '44f62060', 'first request 20 V instead of 5 V'),
    # prefer a fixed PDO over PPS / AVS (PDO picker at 0x0801E33A): skip the
    # APDO when a fixed PDO already matches the target voltage exactly
    (0x0801E384, 'b542', 'a642', 'AVS: cmp best, target (was cmp max, best)'),
    (0x0801E386, '0bd3', '0bd0', 'AVS: skip if best == target'),
    (0x0801E3DA, 'b542', 'a642', 'PPS: cmp best, target (was cmp max, best)'),
    (0x0801E3DC, 'e0d3', 'e0d0', 'PPS: skip if best == target'),
    # after PS_RDY, go to the PPS keep-alive state only if the contract is PPS
    (0x0801ED02, '0178', '2178', 'state 3: test last request type, not "source has PPS"'),
    (0x0801ED3C, '80b240f6b83188423edb28466084a26861680220fff767fb36e0',
     None, 'state 8: re-request every ~2 s only when the contract is PPS'),
    # version shown on the device info screen
    (0x08019D7C, b'3.01\0\0\0\0'.hex(), b'3.01PD\0\0'.hex(), 'version string "3.01" -> "3.01PD"'),
]

# state 8 (PPS keep-alive) rewritten in place, same 26 bytes:
#   if (counter >= 2048 ms && last request was PPS) { counter = 0; re-request }
k = Asm(0x0801ED3C)
k.lsrs('r1', 'r0', 11); k.bc('eq', 0x0801EDC4)
k.ldrb('r1', 'r4', 0); k.cmp('r1', 2); k.bc('ne', 0x0801EDC4)
k.strh('r5', 'r4', 0x22)               # r5 == 0 here
k.ldr('r2', 'r4', 8); k.ldr('r1', 'r4', 4); k.movs('r0', 2)
k.bl(0x0801E422)
k.b(0x0801EDC4)
k.b(0x0801EDC4)                         # pad (unreachable)
kcode = k.build()
PATCHES = [(a, o, kcode.hex() if n is None else n, w) for a, o, n, w in PATCHES]
assert len(kcode) == 26

def main(src, dst):
    ih = IntelHex(src)
    assert ih.maxaddr() < CAVE and all(ih[CAVE + i] == 0xFF for i in range(len(code)))
    for addr, old, new, what in PATCHES:
        cur = bytes(ih[addr + i] for i in range(len(old) // 2)).hex()
        assert cur == old, f'{addr:#x}: expected {old}, found {cur}'
        for i, b in enumerate(bytes.fromhex(new)): ih[addr + i] = b
        print(f'{addr:#010x} {old} -> {new}  {what}')
    for i, b in enumerate(code): ih[CAVE + i] = b
    print(f'cave {CAVE:#010x}..{CAVE + len(code):#010x} ({len(code)} bytes)')
    # Same layout as Miniware's file: data, start-address record, EOF, all CRLF
    # (the bootloader's DFU .hex parser needs CRLF line endings).
    import io
    start = ih.start_addr; ih.start_addr = None
    buf = io.StringIO(); ih.write_hex_file(buf, write_start_addr=False)
    lines = [l for l in buf.getvalue().splitlines() if l]
    assert lines[-1] == ':00000001FF'
    if start:
        eip = start['EIP']; rec = [4, 0, 0, 5] + list(eip.to_bytes(4, 'big'))
        lines.insert(-1, ':' + bytes(rec + [(-sum(rec)) & 0xFF]).hex().upper())
    with open(dst, 'wb') as f:
        f.write(('\r\n'.join(lines) + '\r\n').encode())
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
    names = {v: k for k, v in L.items()}
    for addr, old, *_ in PATCHES:
        b = bytes(ih[addr + i] for i in range(max(4, len(old) // 2)))
        print('  ', *(f'{i.address:#x}: {i.mnemonic} {i.op_str}' for i in md.disasm(b, addr, count=1)))
    if '-v' in sys.argv:
        for i in md.disasm(code, CAVE):
            if i.address in names: print(f'{names[i.address]}:')
            print(f'  {i.address:#010x}: {i.bytes.hex():10} {i.mnemonic} {i.op_str}')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
