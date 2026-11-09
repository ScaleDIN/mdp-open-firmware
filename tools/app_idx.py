import sys, capstone, collections, pickle, re
d=open(sys.argv[1],'rb').read(); FL=0x08008000
md=capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
ins={}; o=0x150
while o<len(d)-2:
    got=False
    for i in md.disasm(d[o:o+4],FL+o,count=1):
        ins[i.address]=(i.size,i.mnemonic,i.op_str); o+=i.size; got=True
    if not got: o+=2
calls=collections.defaultdict(set); lits=collections.defaultdict(set)
w=lambda a:int.from_bytes(d[a-FL:a-FL+4],'little')
for a,(sz,mn,op) in ins.items():
    if mn in('bl','b.w','b') and op.startswith('#'): calls[int(op[1:],16)].add(a)
    if (mn.startswith('ldr') and '[pc' in op):
        try: t=((a+4)&~3)+int(op.split('#')[-1].rstrip(']'),0)
        except: continue
        if FL<=t<FL+len(d)-3: lits[w(t)].add(a)
    if mn in('adr','addw') and 'pc' in op:
        try: t=((a+4)&~3)+int(op.split('#')[-1],0); lits[t].add(a)
        except: pass
pickle.dump((ins,dict(calls),dict(lits)),open(sys.argv[2],'wb'))
print(len(ins),"insns")
for k in [b'Current Limit Mode',b'Input Current Limit',b'Input current limit',b'Unlimit',b'Normal',b'MAX %s',b'Reverse_Threshold',b'Inp Cur',b'CC OR CV',b'Set Cur']:
    for m in re.finditer(re.escape(k),d):
        a=FL+m.start(); print(f"{k.decode():22} @0x{a:08X} refs={sorted(hex(x) for x in lits.get(a,()))}")
