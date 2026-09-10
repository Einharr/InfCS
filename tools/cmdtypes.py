import struct, sys, collections
def walk(b,o,end,path,cb):
    while o<end:
        tag=b[o:o+4]; size=struct.unpack_from('>I',b,o+4)[0]; o+=8
        if tag==b'FORM': walk(b,o+4,o+size,path+[b[o:o+4].decode('latin-1').strip()],cb)
        else: cb(list(path),tag.decode('latin-1').strip(),b[o:o+size])
        o+=size
for p in sys.argv[1:]:
    b=open(p,'rb').read(); rows=[]
    def cb(path,tag,data):
        if tag=='CMD':
            n=data.index(b'\0'); rows.append((data[:n].decode('latin-1'), struct.unpack_from('<I',data,n+1)[0]))
    walk(b,0,len(b),[],cb)
    c=collections.Counter(t for _,t in rows)
    print(f"\n{p.split('/')[-1]}: {len(rows)} cmds, types {dict(sorted(c.items()))}")
    for t in sorted(c):
        if c[t] <= 12:
            print(f"   type {t}: " + ', '.join(n for n,tt in rows if tt==t))
        else:
            print(f"   type {t}: ({c[t]} commands) e.g. " + ', '.join(n for n,tt in rows if tt==t)[:100])
