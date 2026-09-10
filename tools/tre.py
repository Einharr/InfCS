import struct, zlib, sys, os

HDR = struct.Struct('<4s4s7I')

def decomp(data, comp, usize):
    if comp == 0:
        return data
    return zlib.decompress(data)

class Tre:
    def __init__(self, path):
        self.path = path
        f = open(path,'rb'); self.f = f
        magic, ver, nfiles, tocOff, tocComp, tocCSize, nameComp, nameCSize, nameUSize = HDR.unpack(f.read(HDR.size))
        assert magic == b'EERT', magic
        self.ver = ver
        self.n = nfiles
        f.seek(tocOff)
        toc = decomp(f.read(tocCSize), tocComp, nfiles*24)
        names = decomp(f.read(nameCSize), nameComp, nameUSize)
        self.entries = []
        for i in range(nfiles):
            chk, usize, off, comp, csize, noff = struct.unpack_from('<6I', toc, i*24)
            end = names.index(b'\0', noff)
            name = names[noff:end].decode('latin-1')
            self.entries.append((name, off, comp, csize, usize))

    def read(self, e):
        name, off, comp, csize, usize = e
        self.f.seek(off)
        return decomp(self.f.read(csize), comp, usize)

if __name__ == '__main__':
    mode = sys.argv[1]
    if mode == 'ls':
        for p in sys.argv[2:]:
            t = Tre(p)
            for e in t.entries:
                print(f"{os.path.basename(p)}\t{e[0]}\t{e[4]}")
    elif mode == 'get':
        pat, outdir = sys.argv[2], sys.argv[3]
        for p in sys.argv[4:]:
            t = Tre(p)
            for e in t.entries:
                if pat in e[0]:
                    dst = os.path.join(outdir, e[0].replace(chr(92),'/'))
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    open(dst,'wb').write(t.read(e))
                    print("extracted", e[0], e[4], "bytes from", os.path.basename(p))
