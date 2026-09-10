import struct
class PE:
    def __init__(self, path):
        self.b = open(path,'rb').read(); b=self.b
        pe = struct.unpack_from('<I', b, 0x3c)[0]
        assert b[pe:pe+4]==b'PE\0\0'
        nsec = struct.unpack_from('<H', b, pe+6)[0]
        optsz = struct.unpack_from('<H', b, pe+20)[0]
        self.image_base = struct.unpack_from('<I', b, pe+24+28)[0]
        so = pe+24+optsz
        self.secs=[]
        for i in range(nsec):
            o=so+i*40
            name=b[o:o+8].rstrip(b'\0').decode('latin-1')
            vsz,va,rsz,ptr = struct.unpack_from('<IIII', b, o+8)
            self.secs.append((name,va,vsz,ptr,rsz))
    def off2va(self, off):
        for n,va,vsz,ptr,rsz in self.secs:
            if ptr <= off < ptr+rsz: return self.image_base+va+(off-ptr)
        return None
    def va2off(self, va):
        r = va - self.image_base
        for n,va_,vsz,ptr,rsz in self.secs:
            if va_ <= r < va_+max(vsz,rsz): return ptr+(r-va_)
        return None
