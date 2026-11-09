import sys
def load(p):
    raw=open(p,'rb').read(); mem={}; base=0; bad=0
    for ln in raw.decode().split('\r\n'):
        if not ln: continue
        b=bytes.fromhex(ln[1:])
        if sum(b)&0xff: bad+=1
        n,a,t=b[0],(b[1]<<8)|b[2],b[3]
        if t==4: base=((b[4]<<8)|b[5])<<16
        elif t==0:
            for i in range(n): mem[base+a+i]=b[4+i]
    return mem,bad,raw.count(b'\r\n'),raw.count(b'\n')
o,_,_,_=load(sys.argv[1]); p,bp,crlf,lf=load(sys.argv[2])
diff=[a for a in o if o[a]!=p.get(a)]
print(f"checksum errors: {bp}; CRLF {crlf} of {lf} line ends; same addresses: {set(o)==set(p)}")
print("changed bytes:", [f"0x{a:08X}: {o[a]:02X}->{p[a]:02X}" for a in sorted(diff)])
lo=min(p); g=lambda x:int.from_bytes(bytes(p[x+i] for i in range(4)),'little')
print(f"range 0x{lo:08X}-0x{max(p):08X}, SP=0x{g(lo):08X} reset=0x{g(lo+4):08X}")
