# -*- coding: utf-8 -*-
"""A minimal minidump decoder: the module list plus the exception record.

The goal is to name the module the fault address lies in, so as to tell whether it is
ours (cpinject.dll / exe code inside our calls) or a system driver DLL.
"""
import struct, sys, os

def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u64(b, o): return struct.unpack_from('<Q', b, o)[0]

def parse(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'MDMP', 'not a minidump'
    nstreams = u32(d, 8); rva = u32(d, 12)
    streams = {}
    for i in range(nstreams):
        o = rva + i*12
        stype = u32(d, o); size = u32(d, o+4); sloc = u32(d, o+8)
        streams[stype] = (size, sloc)
    mods = []
    if 4 in streams:  # ModuleListStream
        size, o = streams[4]
        n = u32(d, o); p = o + 4
        for i in range(n):
            base = u64(d, p); msize = u32(d, p+8)
            name_rva = u32(d, p+0x14)
            ln = u32(d, name_rva)
            name = d[name_rva+4:name_rva+4+ln].decode('utf-16-le', 'replace')
            mods.append((base, msize, os.path.basename(name)))
            p += 108  # sizeof(MINIDUMP_MODULE)
    exc = None
    if 6 in streams:  # ExceptionStream
        size, o = streams[6]
        tid = u32(d, o)
        er = o + 8
        code = u32(d, er); flags = u32(d, er+4)
        addr = u64(d, er+16)
        nparm = u32(d, er+24)
        params = [u64(d, er+32+8*k) for k in range(min(nparm, 15))]
        exc = (tid, code, addr, params)
    return mods, exc

def which(mods, addr):
    for base, size, name in mods:
        if base <= addr < base + size:
            return name, addr - base
    return None, None

def threads(path):
    """The thread list: tid -> (the stack range, a slice of the stack, the context)."""
    d = open(path, 'rb').read()
    nstreams = u32(d, 8); rva = u32(d, 12)
    streams = {}
    for i in range(nstreams):
        o = rva + i*12
        streams[u32(d, o)] = (u32(d, o+4), u32(d, o+8))
    out = {}
    if 3 in streams:
        size, o = streams[3]
        n = u32(d, o); p = o + 4
        for i in range(n):
            tid = u32(d, p)
            stack_start = u64(d, p+24)
            stack_size = u32(d, p+32); stack_rva = u32(d, p+36)
            ctx_size = u32(d, p+40); ctx_rva = u32(d, p+44)
            ctx = d[ctx_rva:ctx_rva+ctx_size]
            stack = d[stack_rva:stack_rva+stack_size]
            out[tid] = (stack_start, stack, ctx)
            p += 48
    return out


def stackwalk(path, tid, mods):
    ths = threads(path)
    if tid not in ths:
        print('thread %d not found (there are: %s)' % (tid, list(ths)[:5])); return
    start, stack, ctx = ths[tid]
    # CONTEXT_i386: Ebp 184, Eip 188, Esp 200
    eip = struct.unpack_from('<I', ctx, 188)[0]
    esp = struct.unpack_from('<I', ctx, 200)[0]
    ebp = struct.unpack_from('<I', ctx, 184)[0]
    print('EIP %08X  ESP %08X  EBP %08X  stack %08X..%08X' % (eip, esp, ebp, start, start+len(stack)))
    name, off = which(mods, eip)
    print('EIP in %s+0x%X' % (name, off) if name else 'EIP outside the modules')
    print('--- the return addresses on the stack (top down) ---')
    lo = max(esp, start); hi = start + len(stack)
    shown = 0
    for a in range(lo, hi, 4):
        v = struct.unpack_from('<I', stack, a-start)[0]
        nm, o = which(mods, v)
        if nm and nm.lower() in ('swgemu.exe', 'cpinject.dll'):
            print('  %08X: %08X  %s+0x%X' % (a, v, nm, o))
            shown += 1
            if shown >= 40: break


if __name__ == '__main__':
    path = sys.argv[1]
    mods, exc = parse(path)
    if len(sys.argv) > 2 and sys.argv[2] == '--ctx':
        ths = threads(path); tid = exc[0]; start, stack, ctx = ths[tid]
        print('ctx len', len(ctx), 'looking for EIP', hex(exc[2]))
        for o in range(0, len(ctx)-4, 4):
            v = struct.unpack_from('<I', ctx, o)[0]
            if v == (exc[2] & 0xFFFFFFFF): print('  EIP found at offset', o)
        print('stack range %08X..%08X' % (start, start+len(stack)))
        sys.exit(0)
    if len(sys.argv) > 2 and sys.argv[2] == '--stack':
        if exc: stackwalk(path, exc[0], mods)
        sys.exit(0)
    if exc:
        tid, code, addr, params = exc
        print('exception code %08X  addr %016X  tid %d  params %s' % (code, addr, tid, ['%X'%x for x in params]))
        name, off = which(mods, addr)
        print('faulting module: %s + 0x%X' % (name, off) if name else 'faulting addr in NO listed module (addr %016X)' % addr)
    print('--- modules (%d) ---' % len(mods))
    for base, size, name in sorted(mods):
        print('%016X..%016X  %s' % (base, base+size, name))
