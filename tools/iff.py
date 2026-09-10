import struct, sys

def rd(b, o, end, depth, out, maxleaf=8):
    while o < end:
        tag = b[o:o+4]; size = struct.unpack_from('>I', b, o+4)[0]; o += 8
        body = b[o:o+size]
        if tag == b'FORM':
            typ = body[:4].decode('latin-1')
            out.append(('  '*depth) + f"FORM {typ} ({size-4} bytes)")
            rd(b, o+4, o+size, depth+1, out, maxleaf)
        else:
            out.append(('  '*depth) + f"{tag.decode('latin-1')} [{size}]")
        o += size
        # SWG IFF: no even-padding

def dump(path, maxleaf=8):
    b = open(path,'rb').read()
    out = []
    rd(b, 0, len(b), 0, out, maxleaf)
    return out

if __name__ == '__main__':
    seen = {}
    for line in dump(sys.argv[1]):
        key = line.strip().split(' [')[0].split(' (')[0]
        ind = len(line) - len(line.lstrip())
        k = (ind, key)
        seen[k] = seen.get(k,0)+1
        if seen[k] <= 3:
            print(line)
        elif seen[k] == 4:
            print(' '*ind + f"... (more {key})")
