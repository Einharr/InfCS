import struct, sys

def walk(b, o, end, path, cb):
    while o < end:
        tag = b[o:o+4]; size = struct.unpack_from('>I', b, o+4)[0]; o += 8
        if tag == b'FORM':
            walk(b, o+4, o+size, path + [b[o:o+4].decode('latin-1').strip()], cb)
        else:
            cb(path, tag.decode('latin-1').strip(), b[o:o+size])
        o += size

b = open(sys.argv[1],'rb').read()
groups = {}
def cb(path, tag, data):
    grp = path[-1]
    groups.setdefault(grp, []).append((tag, data))
walk(b, 0, len(b), [], cb)
for grp, items in groups.items():
    print(f"\n### {grp}  ({len(items)} entries)")
    for tag, d in items:
        if tag == 'CODE' and len(d) >= 5:
            code = struct.unpack_from('<I', d)[0]
            name = d[4:].split(b'\0')[0].decode('latin-1')
            print(f"   {code:5d}  {name}")
        else:
            print(f"   {tag} raw {d[:32].hex()}")
