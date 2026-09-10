import struct, sys
def walk(b, o, end, path, cb):
    while o < end:
        tag = b[o:o+4]; size = struct.unpack_from('>I', b, o+4)[0]; o += 8
        if tag == b'FORM': walk(b, o+4, o+size, path+[b[o:o+4].decode('latin-1').strip()], cb)
        else: cb(list(path), tag.decode('latin-1').strip(), b[o:o+size])
        o += size
b = open(sys.argv[1],'rb').read()
cat = [None]; rows=[]
def cb(path, tag, data):
    if tag == 'NAME': cat[0] = data.split(b'\0')[0].decode('latin-1')
    elif tag == 'CMD':
        n = data.index(b'\0'); name = data[:n].decode('latin-1'); rest = data[n+1:]
        rows.append((cat[0], name, rest))
walk(b,0,len(b),[],cb)
print(f"total commands: {len(rows)}")
import collections
print("sizes:", collections.Counter(len(r[2]) for r in rows))
if len(sys.argv)>2 and sys.argv[2]=='full':
    for c,n,r in rows: print(f"{c:22s} {n:48s} {r.hex(' ')}")
else:
    c=collections.Counter(r[0] for r in rows)
    for k,v in c.most_common(): print(f"  {v:4d}  {k}")
