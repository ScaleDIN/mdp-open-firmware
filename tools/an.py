import sys
d = open(sys.argv[1],'rb').read(); app = open(sys.argv[2],'rb').read()
FL=0x08000000
# page map (2KB pages)
print("page map (. = all FF, # = data):")
row=""
for p in range(0,len(d),2048):
    row += "." if d[p:p+2048]==b'\xff'*2048 else "#"
for i in range(0,len(row),32): print(f"  0x{FL+i*2048:08X} {row[i:i+32]}")
sp,rv = int.from_bytes(d[0:4],'little'), int.from_bytes(d[4:8],'little')
print(f"bootloader SP=0x{sp:08X} reset=0x{rv:08X}")
a = d[0x8000:0x8000+len(app)]
print(f"app vectors in flash: SP=0x{int.from_bytes(a[0:4],'little'):08X} reset=0x{int.from_bytes(a[4:8],'little'):08X}")
diff=[i for i in range(len(app)) if a[i]!=app[i]]
print(f"v3.01 compare: {len(app)} bytes, {len(diff)} differ")
if diff: print("  first diffs at offsets:", [hex(x) for x in diff[:10]], " last:", hex(diff[-1]))
# beyond app end
tail = d[0x8000+len(app):]
nz=[i for i in range(len(tail)) if tail[i]!=0xff]
print(f"after app end: {len(nz)} non-FF bytes" + (f", first at 0x{FL+0x8000+len(app)+nz[0]:08X}" if nz else ""))
