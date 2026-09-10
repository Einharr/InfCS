# -*- coding: utf-8 -*-
"""The search disassembler: vtables out of RTTI, slots checked against anchors.

The Infinity binary (stage.119798) is older than the sources, so a vtable layout can
never be taken from the headers - only derived from the binary and pinned to anchors:
    UIBaseObject: slot 0 IsA, 11 SetProperty(ULS), 13 GetProperty(ULS), 19 GetChild

    python abigen.py dump  CLASS [CLASS...]     # a class's full vtable
    python abigen.py diff  CLASS [CLASS...]     # slots overridden relative to the first
    python abigen.py typename CLASS             # what slot 1 returns - the anchor
"""
import os, sys, struct, re
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from pe import PE
import capstone

EXE = r"E:\Games\SWG\Dev\SWG Infinity\Test Center\swgemu.exe"
_p = None


def pe():
    global _p
    if _p is None:
        _p = PE(EXE)
    return _p


def text_range():
    p = pe()
    for n, va, vsz, ptr, rsz in p.secs:
        if n == '.text':
            return p.image_base + va, p.image_base + va + vsz
    raise RuntimeError('.text')


def rdata_ptr_ok(va):
    return pe().va2off(va) is not None


def u32(va):
    p = pe(); o = p.va2off(va)
    return struct.unpack_from('<I', p.b, o)[0] if o is not None else None


# ------------------------------------------------------------ RTTI
def vtables(cls):
    """[(vtable_va, col_va, offset_in_object)] - every vtable of the class by RTTI (MSVC x86).
    COL: +0 signature, +4 offset (the subobject offset), +8 cdOffset, +12 pTypeDescriptor."""
    p = pe(); b = p.b
    tag = ('.?AV%s@@' % cls).encode()
    out = []
    i = b.find(tag)
    while i != -1:
        td_va = p.off2va(i - 8)
        if td_va:
            pat = struct.pack('<I', td_va)
            j = b.find(pat)
            while j != -1:
                col_va = p.off2va(j - 12)
                if col_va and u32(col_va) == 0:              # signature 0
                    pat2 = struct.pack('<I', col_va)
                    k = b.find(pat2)
                    while k != -1:
                        vt = p.off2va(k + 4)
                        if vt and p.va2off(vt):
                            out.append((vt, col_va, u32(col_va + 4)))
                        k = b.find(pat2, k + 1)
                j = b.find(pat, j + 1)
        i = b.find(tag, i + 1)
    # unique, by subobject offset
    seen = {}
    for vt, col, off in out:
        seen.setdefault(off, (vt, col))
    return sorted(((off, vt, col) for off, (vt, col) in seen.items()))


def vtable_entries(vt):
    """The vtable functions up to the first non-.text pointer (or up to the next COL pointer)."""
    p = pe(); t0, t1 = text_range()
    res = []
    va = vt
    while True:
        f = u32(va)
        if f is None or not (t0 <= f < t1):
            break
        res.append(f)
        va += 4
        # the next vtable begins after the COL pointer: if u32(va) points into rdata at a COL with signature 0
        nxt = u32(va)
        if nxt is not None and rdata_ptr_ok(nxt) and u32(nxt) == 0 and (t0 > nxt or nxt >= t1):
            break
    return res


def primary(cls):
    vts = vtables(cls)
    for off, vt, col in vts:
        if off == 0:
            return vt
    return None


def typename_of(cls):
    """Slot 1 on UIBaseObject descendants is GetTypeName: mov eax, offset "Name"; ret."""
    vt = primary(cls)
    fn = vtable_entries(vt)[1]
    p = pe(); o = p.va2off(fn)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for ins in md.disasm(p.b[o:o + 16], fn):
        if ins.mnemonic == 'mov' and ins.op_str.startswith('eax, 0x'):
            sva = int(ins.op_str.split('0x')[1], 16)
            so = p.va2off(sva)
            if so:
                return p.b[so:so + 32].split(b'\0')[0].decode('latin-1')
    return None


def dump(classes):
    for cls in classes:
        vts = vtables(cls)
        print('=== %s' % cls)
        for off, vt, col in vts:
            fns = vtable_entries(vt)
            print('  vtable @%08X  subobject +%d  %d methods' % (vt, off, len(fns)))
            if off == 0:
                for i, f in enumerate(fns):
                    print('    %3d  %08X' % (i, f))


def diff(classes):
    base = classes[0]
    tabs = {c: vtable_entries(primary(c)) for c in classes}
    n = max(len(t) for t in tabs.values())
    print('slot ' + ' '.join('%-10s' % c[:10] for c in classes))
    for i in range(n):
        row = []
        b = tabs[base][i] if i < len(tabs[base]) else None
        for c in classes:
            t = tabs[c]
            if i >= len(t): row.append('%-10s' % '-'); continue
            row.append('%-10s' % ('%08X' % t[i] if (c == base or t[i] != b) else '.'))
        print('%3d  %s' % (i, ' '.join(row)))


# ------------------------------------------------------------ xref
_XREF = None


def xrefs_build():
    """A rough .text scan: call/jmp rel32 and 32-bit immediates (push imm32, mov eax,[imm],
    mov r,[imm], cmp, lea, ...). No disassembler, so it is fast; false hits die on a
    range check."""
    global _XREF
    if _XREF is not None:
        return _XREF
    import collections
    p = pe(); b = p.b; t0, t1 = text_range()
    o0 = p.va2off(t0); o1 = o0 + (t1 - t0)
    lo, hi = p.image_base, p.image_base + 0x1800000
    calls = collections.defaultdict(list)     # target -> [call sites]
    imms = collections.defaultdict(list)      # imm32 -> [sites]
    i = o0
    while i < o1 - 5:
        op = b[i]
        if op == 0xE8 or op == 0xE9:
            rel = struct.unpack_from('<i', b, i + 1)[0]
            src = p.image_base + (i - o0) + (t0 - p.image_base)
            tgt = src + 5 + rel
            if t0 <= tgt < t1:
                calls[tgt].append(src)
        imm = struct.unpack_from('<I', b, i + 1)[0]
        if lo <= imm < hi and op in (0x68, 0xA1, 0xA3, 0xB8, 0xB9, 0xBA, 0xBB, 0xBE, 0xBF, 0x05, 0x3D, 0x2D):
            imms[imm].append(p.image_base + (i - o0) + (t0 - p.image_base))
        if op in (0x8B, 0x89, 0x8D, 0x3B, 0x39, 0xFF, 0xC7, 0x83, 0x80, 0xA0, 0xA2, 0x0F, 0xD9, 0xDD, 0xC6):
            imm2 = struct.unpack_from('<I', b, i + 2)[0]
            if lo <= imm2 < hi:
                imms[imm2].append(p.image_base + (i - o0) + (t0 - p.image_base))
        i += 1
    _XREF = (calls, imms)
    return _XREF


def string_va(s):
    """The VA of the start of the literal containing s (code references point at the literal's start)."""
    p = pe(); b = p.b
    i = b.find(s.encode('latin-1'))
    if i == -1:
        return None
    j = b.rfind(bytes([0]), 0, i) + 1
    return p.off2va(j)


def refs_to(va):
    calls, imms = xrefs_build()
    return sorted(set(calls.get(va, []))), sorted(set(imms.get(va, [])))


def func_start(va, limit=0x4000):
    """Function start: back to the nearest 'push ebp; mov ebp,esp', or past a ret+padding."""
    p = pe(); o = p.va2off(va); b = p.b
    i = o
    while i > o - limit:
        if b[i] == 0x55 and b[i + 1] == 0x8B and b[i + 2] == 0xEC and b[i - 1] in (0xC3, 0xCC, 0x90) or (b[i - 1] == 0xC2 and False):
            return p.off2va(i)
        if b[i - 1] == 0xCC and b[i - 2] == 0xCC and b[i] != 0xCC:
            return p.off2va(i)
        i -= 1
    return None


def disasm(va, n=12, ann_strings=True):
    p = pe(); md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    o = p.va2off(va); out = []
    for ins in md.disasm(p.b[o:o + n * 8], va):
        note = ''
        if ann_strings:
            for m in re.finditer(r'0x([0-9a-f]{6,8})', ins.op_str):
                sva = int(m.group(1), 16); so = p.va2off(sva)
                if so and sva >= 0x1800000:
                    s = p.b[so:so + 40].split(b'\0')[0]
                    if len(s) >= 3 and all(32 <= c < 127 for c in s):
                        note = '  ; "%s"' % s.decode()
        out.append('%08x %-6s %s%s' % (ins.address, ins.mnemonic, ins.op_str, note))
        if len(out) >= n:
            break
    return out


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'dump': dump(sys.argv[2:])
    elif cmd == 'diff': diff(sys.argv[2:])
    elif cmd == 'typename':
        for c in sys.argv[2:]: print(c, '->', typename_of(c))


