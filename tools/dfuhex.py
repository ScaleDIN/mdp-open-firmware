"""Re-layout an Intel HEX file so the P906 DFU bootloader accepts it whatever
order Windows writes the sectors in.

usage: python tools/dfuhex.py in.hex out.hex

Why: the bootloader parses raw sector writes to its virtual FAT12 drive, not
the file. When Windows writes the file data before the directory entry, the
bootloader skips writes below disk offset 0x7A00 and starts parsing there.
A copied file starts at 0x6A00 (the first free cluster), so parsing begins
up to 4 KB into the file, usually mid-line, and the result is .ERR.

What this does:
  * every 512-byte sector starts exactly on a ':' (record lengths chosen
    so each sector is exactly 512 characters)
  * the first 8 sectors (4 KB) hold only records the bootloader ignores
    (type 05 start address, repeated type 04), so skipping any loses nothing
  * data records are at most 16 bytes long (the bootloader's record buffer
    overlaps its state after 20 bytes), start on even addresses and have
    even lengths (the bootloader programs halfwords)
  * CRLF line endings, EOF record at the end
"""
import sys
from intelhex import IntelHex

SECTOR = 512
FILLER_SECTORS = 8
MAXL = 16
CRLF = '\r\n'
EIP = 0x0802CCA9                # app reset handler (stock start-address record)

def rec(typ, addr, data):
    b = bytes([len(data), (addr >> 8) & 0xFF, addr & 0xFF, typ]) + bytes(data)
    return ':' + (b + bytes([(-sum(b)) & 0xFF])).hex().upper() + CRLF

def segments(ih):
    segs = []
    for s, e in ih.segments():
        assert s % 2 == 0 and e % 2 == 0, f'segment {s:#x}-{e:#x} not halfword aligned'
        while s < e:            # split at 64 KB so each piece needs one type-04 record
            cut = min(e, (s & ~0xFFFF) + 0x10000)
            segs.append((s, cut)); s = cut
    return segs

def fill_sector(ih, items, i, off, upper):
    """Return (lines, i, off, upper) filling exactly SECTOR chars, starting at
    stream position (items[i], byte offset off). Prefers long data records."""
    import functools
    ela = lambda u: rec(4, 0, u.to_bytes(2, 'big'))
    @functools.lru_cache(None)
    def go(R, i, off, upper):
        if R == 0: return ()
        if R < 17: return None
        opts = []
        if i < len(items):
            it = items[i]
            if it[0] == 'ela':
                opts.append((ela(it[1]), i + 1, 0, it[1]))
            else:
                a = it[1] + off; left = it[2] - a
                for L in range(min(MAXL, left), 1, -2):
                    ni, noff = (i + 1, 0) if L == left else (i, off + L)
                    opts.append((rec(0, a & 0xFFFF, ih.tobinarray(start=a, size=L)), ni, noff, upper))
        opts.append((ela(upper), i, off, upper))                          # 17-char filler
        opts.append((rec(5, 0, EIP.to_bytes(4, 'big')), i, off, upper))  # 21-char filler
        for line, ni, noff, nu in opts:
            if len(line) <= R:
                r = go(R - len(line), ni, noff, nu)
                if r is not None: return ((line, ni, noff, nu),) + r
        return None
    r = go(SECTOR, i, off, upper)
    assert r, 'cannot fill sector'
    return [x[0] for x in r], r[-1][1], r[-1][2], r[-1][3]

def build(ih):
    items, upper = [], None
    for s, e in segments(ih):
        if s >> 16 != upper:
            upper = s >> 16; items.append(('ela', upper))
        items.append(('data', s, e))
    out = []
    first_upper = items[0][1]
    for _ in range(FILLER_SECTORS):           # ignorable records only
        lines, *_ = fill_sector(ih, [], 0, 0, first_upper)
        out += lines
    i, off, upper = 0, 0, first_upper
    while i < len(items):
        lines, i, off, upper = fill_sector(ih, items, i, off, upper)
        assert len(''.join(lines)) == SECTOR
        out += lines
    out.append(rec(5, 0, EIP.to_bytes(4, 'big')))
    out.append(':00000001FF' + CRLF)
    return ''.join(out)

if __name__ == '__main__':
    ih = IntelHex(sys.argv[1])
    text = build(ih)
    open(sys.argv[2], 'wb').write(text.encode())
    # self-check with a plain parser (intelhex refuses the repeated type-05
    # filler records, so this file is for the DFU drive only)
    img, base = {}, 0
    for ln in text.split(CRLF)[:-1]:
        b = bytes.fromhex(ln[1:]); assert ln[0] == ':' and sum(b) & 0xFF == 0 and b[0] == len(b) - 5
        n, a, t, d = b[0], b[1] << 8 | b[2], b[3], b[4:-1]
        if t == 4: base = int.from_bytes(d, 'big') << 16
        if t == 0:
            assert n <= MAXL and n % 2 == 0 and a % 2 == 0
            for k, v in enumerate(d): assert base + a + k not in img; img[base + a + k] = v
    assert img == {a: ih[a] for s, e in ih.segments() for a in range(s, e)}
    starts = sum(1 for k in range(0, len(text), SECTOR) if text[k] == ':')
    print(f'{sys.argv[2]}: {len(text)} bytes, {len(text) // SECTOR + 1} sectors, '
          f'{starts} sector starts on ":", data identical to input')
