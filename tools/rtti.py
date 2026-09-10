"""Finding a vtable by a class RTTI name (MSVC, 32-bit).

The chain: the type descriptor -> the Complete Object Locator -> the pointer to the
COL, which lies 4 bytes before vtable[0].
"""
import sys, struct
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import PE

def find_vtables(p, cls):
    b = p.b
    tag = ('.?AV%s@@' % cls).encode()
    out = []
    i = b.find(tag)
    while i != -1:
        # the type descriptor starts 8 bytes before the name
        td_va = p.off2va(i - 8)
        if td_va:
            pat = struct.pack('<I', td_va)
            j = b.find(pat)
            while j != -1:
                # COL: the pointer to the descriptor lies at offset +12
                col_va = p.off2va(j - 12)
                if col_va:
                    pat2 = struct.pack('<I', col_va)
                    k = b.find(pat2)
                    while k != -1:
                        vt = p.off2va(k + 4)
                        if vt and p.va2off(vt):
                            out.append((td_va, col_va, vt))
                        k = b.find(pat2, k + 1)
                j = b.find(pat, j + 1)
        i = b.find(tag, i + 1)
    return out

def dump(p, vt, n=40):
    b = p.b; o = p.va2off(vt); res = []
    for s in range(n):
        f = struct.unpack_from('<I', b, o + s*4)[0]
        if not p.va2off(f) or f < 0x401000 or f > 0x15dc000: break
        res.append((s, f))
    return res

if __name__ == '__main__':
    p = PE(r"E:\Games\SWG\Live\swgemu.exe")
    for cls in sys.argv[1:]:
        vts = find_vtables(p, cls)
        print(f"=== {cls}: {len(vts)} vtables found")
        for td, col, vt in vts[:3]:
            fns = dump(p, vt)
            print(f"    vtable @{vt:08X}  (COL {col:08X})  {len(fns)} methods")
