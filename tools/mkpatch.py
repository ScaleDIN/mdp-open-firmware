import sys, capstone
src, out = sys.argv[1], sys.argv[2]
PATCH_ADDR = 0x0801ED92
OLD = bytes.fromhex('44f62060')   # movw r0, #0x4e20  (20000 mV)
imm = 28000                        # 0x6D60 -> 28 V
hw1 = 0xF240 | (((imm>>11)&1)<<10) | (imm>>12)
hw2 = (((imm>>8)&7)<<12) | (imm&0xff)
NEW = hw1.to_bytes(2,'little') + hw2.to_bytes(2,'little')
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
print("old:", [f"{i.mnemonic} {i.op_str}" for i in md.disasm(OLD, PATCH_ADDR)])
print("new:", [f"{i.mnemonic} {i.op_str}" for i in md.disasm(NEW, PATCH_ADDR)])
lines = open(src, newline='').read().split('\r\n')
base = 0; done = 0; outl = []
for ln in lines:
    if ln.startswith(':'):
        b = bytearray(bytes.fromhex(ln[1:])); n, a, t = b[0], (b[1]<<8)|b[2], b[3]
        if t == 4: base = ((b[4]<<8)|b[5]) << 16
        if t == 0 and base+a <= PATCH_ADDR and PATCH_ADDR+4 <= base+a+n:
            o = 4 + PATCH_ADDR - (base+a)
            assert bytes(b[o:o+4]) == OLD, "unexpected original bytes"
            b[o:o+4] = NEW; b[-1] = (-sum(b[:-1])) & 0xff; done += 1
            ln = ':' + b.hex().upper()
    outl.append(ln)
assert done == 1
open(out, 'w', newline='').write('\r\n'.join(outl))
print("patched record written:", out)
