import pickle,sys,re
ins,calls,lits=pickle.load(open(sys.argv[1],'rb'))
d=open(sys.argv[2],'rb').read(); FL=0x08008000
w=lambda a:int.from_bytes(d[a-FL:a-FL+4],'little')
def s_at(a):
    if not(FL<=a<FL+len(d)): return None
    m=re.match(rb'[\x20-\x7e]{3,40}',d[a-FL:]); return m.group().decode() if m else None
for x in sorted(k for k in ins if int(sys.argv[3],16)<=k<int(sys.argv[4],16)):
    sz,mn,op=ins[x]; note=""
    if '[pc' in op or (mn=='adr'):
        try:
            t=((x+4)&~3)+int(op.split('#')[-1].rstrip(']'),0)
            v=w(t) if mn.startswith('ldr') else t
            note=f"   ; =0x{v:08X}"; s=s_at(v)
            if s: note+=f' "{s}"'
        except: pass
    print(f"{x:08x}: {mn:7} {op}{note}")
