import pickle,sys,bisect
ins,calls,lits=pickle.load(open(sys.argv[1],'rb'))
addrs=sorted(ins)
starts=sorted(set([a for a in addrs if ins[a][1].startswith('push')]))
fs=lambda a: starts[bisect.bisect_right(starts,a)-1]
for v in sys.argv[2:]:
    v=int(v,16); r=sorted(lits.get(v,()))
    print(f"0x{v:08X}: {len(r)} refs in funcs {sorted(set(hex(fs(x)) for x in r))}")
    print("   sites:", [hex(x) for x in r])
