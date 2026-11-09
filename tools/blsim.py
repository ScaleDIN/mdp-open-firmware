# usage: python tools/blsim.py dump/p906_flash_after.bin file.hex [...]
# Simulate the P906 bootloader .hex parser (0x080009EC) byte for byte.
import sys
def run(data, flash):
    st=dict(state=0); S=0; L=0; cs=0; addr=0; tmp=0; typ=0; base=0; buf=bytearray(256); i=0
    hexv=lambda c: int(chr(c),16)
    pages_erased=set()
    for pos,c in enumerate(data):
        try:
            if S==0:
                if c!=0x3a: return f'ERR: expected ":" at byte {pos}'
                S=1
            elif S==1: L=hexv(c)<<4; i=0; S=2
            elif S==2: L+=hexv(c); cs=L; S=3
            elif S==3: addr=hexv(c)<<4; S=4
            elif S==4: addr=(addr+hexv(c)); cs=(cs+addr)&0xff; addr<<=8; S=5
            elif S==5: tmp=hexv(c)<<4; S=6
            elif S==6: tmp+=hexv(c); addr+=tmp; cs=(cs+tmp)&0xff; S=7
            elif S==7: typ=hexv(c)<<4; S=8
            elif S==8:
                typ+=hexv(c); cs=(cs+typ)&0xff; S=9 if L else 11
            elif S==9: buf[i]=hexv(c)<<4; S=10
            elif S==10:
                buf[i]+=hexv(c); cs=(cs+buf[i])&0xff; i+=1; S=9 if i<L else 11
            elif S==11: cs=(cs+(hexv(c)<<4))&0xff; S=12
            elif S==12:
                cs=(cs+hexv(c))&0xff
                if cs: return f'ERR: checksum at byte {pos}'
                S=13
            elif S==13:
                if c!=0x0d: return f'ERR: expected CR at byte {pos}'
                S=14
            elif S==14:
                if c!=0x0a: return f'ERR: expected LF at byte {pos}'
                S=0
                if typ==4: base=(buf[0]<<24)|(buf[1]<<16)
                if typ==1: return 'RDY'
                if typ==0:
                    a=base+addr
                    if a<0x08008000: return 'NOT'
                    if a>=0x08040000: return f'ERR: address {a:#x}'
                    for k in range(0,L,2):
                        x=a+k
                        if (x & 0x7ff)==0:
                            for p in range(x,x+0x800): flash[p]=0xff
                        hw=buf[k] | ((buf[k+1] if k+1<L else 0xff)<<8)
                        cur=flash[x]|(flash[x+1]<<8)
                        if cur!=0xffff and hw!=0: return f'ERR: program {x:#x} not erased (line ending byte {pos})'
                        flash[x]=hw&0xff; flash[x+1]=hw>>8
        except ValueError:
            return f'ERR: bad hex char {chr(c)!r} at byte {pos}'
    return 'no EOF'
for f in sys.argv[2:]:
    fl=bytearray(open(sys.argv[1],'rb').read())
    print(f, '->', run(open(f,'rb').read(), {k+0x08000000:v for k,v in enumerate(fl)}))
