"""SWG IMAP (inputmap) .iff  <->  text.  Round-trip exact."""
import struct, sys, os

DEV = ['KEYS','MOSB','JOYB','JOYX','JOYS','POV ']   # MAP sub-forms, parser order
SDEV = ['KEY ','MOSB','JOYB']                        # SHFT sub-forms, parser order

def chunks(b, o, end):
    while o < end:
        tag = b[o:o+4]; size = struct.unpack_from('>I', b, o+4)[0]
        yield tag.decode('latin-1'), o+8, size
        o += 8 + size

def decompile(path):
    b = open(path,'rb').read()
    L = []
    (t, o, n), = list(chunks(b, 0, len(b)))
    assert t == 'FORM' and b[o:o+4] == b'IMAP'
    (t, o, n), = list(chunks(b, o+4, o+n))
    ver = b[o:o+4].decode('latin-1'); L.append(f"version {ver}")
    for t, so, sn in chunks(b, o+4, o+n):
        typ = b[so:so+4].decode('latin-1')
        if typ == 'CMDS':
            for ct, co, cn in chunks(b, so+4, so+sn):
                L.append("cmds " + b[co:co+cn].split(b'\0')[0].decode('latin-1'))
        elif typ == 'SHFT':
            for dt, do_, dn in chunks(b, so+4, so+sn):
                dev = b[do_:do_+4].decode('latin-1')
                for _, eo, en in chunks(b, do_+4, do_+dn):
                    code, bit = struct.unpack_from('<II', b, eo)
                    L.append(f"shift {dev.strip():4s} 0x{code:02X} 0x{bit:02X} {b[eo+8]}")
        elif typ == 'MAP ':
            for dt, do_, dn in chunks(b, so+4, so+sn):
                if dt == 'INFO':
                    st = struct.unpack_from('<I', b, do_)[0]
                    L.append(f"layer 0x{st:02X} {b[do_+4]}")
                else:
                    dev = b[do_:do_+4].decode('latin-1')
                    if dn == 4: L.append(f"  {dev.strip():4s} -")
                    for _, eo, en in chunks(b, do_+4, do_+dn):
                        idx = struct.unpack_from('<I', b, eo)[0]
                        cmd = b[eo+4:eo+en].split(b'\0')[0].decode('latin-1')
                        L.append(f"  {dev.strip():4s} {idx:3d} {cmd}")
    return '\n'.join(L) + '\n'

def form(typ, payload):
    return b'FORM' + struct.pack('>I', 4+len(payload)) + typ.encode('latin-1').ljust(4) + payload
def chunk(tag, payload):
    return tag.encode('latin-1').ljust(4) + struct.pack('>I', len(payload)) + payload

def compile_(text):
    ver='0006'; cmds=None; shifts=[]; layers=[]
    for line in text.splitlines():
        s=line.strip()
        if not s: continue
        w=s.split()
        if w[0]=='version': ver=w[1]
        elif w[0]=='cmds': cmds=w[1]
        elif w[0]=='shift': shifts.append((w[1], int(w[2],0), int(w[3],0), int(w[4])))
        elif w[0]=='layer': layers.append((int(w[1],0), int(w[2]), []))
        elif len(w) == 2 and w[1] == '-': layers[-1][2].append((w[0], None, None))
        else: layers[-1][2].append((w[0], int(w[1]), w[2]))
    body=b''
    if cmds is not None:
        body += form('CMDS', chunk('NAME', cmds.encode()+b'\0'))
    if shifts:
        p=b''
        for dev in SDEV:
            e=[x for x in shifts if x[0].strip()==dev.strip()]
            if e: p += form(dev, b''.join(chunk('DATA', struct.pack('<IIB',c,bit,f)) for _,c,bit,f in e))
        body += form('SHFT', p)
    for st, flag, ents in layers:
        p = chunk('INFO', struct.pack('<IB', st, flag))
        for dev in DEV:
            e=[x for x in ents if x[0].strip()==dev.strip()]
            if e: p += form(dev, b''.join(chunk('DATA', struct.pack('<I',i)+c.encode()+b'\0') for _,i,c in e if i is not None))
        body += form('MAP ', p)
    return form('IMAP', form(ver, body))

if __name__ == '__main__':
    if sys.argv[1]=='d': sys.stdout.write(decompile(sys.argv[2]))
    elif sys.argv[1]=='c': open(sys.argv[3],'wb').write(compile_(open(sys.argv[2]).read()))
    elif sys.argv[1]=='test':
        ok=bad=0
        for p in sys.argv[2:]:
            if os.path.getsize(p)==0: continue
            orig=open(p,'rb').read()
            try: rebuilt=compile_(decompile(p))
            except Exception as e: print(f"  ERR  {os.path.basename(p)}: {e}"); bad+=1; continue
            if rebuilt==orig: ok+=1; print(f"  OK   {os.path.basename(p)} ({len(orig)} B)")
            else: bad+=1; print(f"  DIFF {os.path.basename(p)}: {len(orig)} -> {len(rebuilt)}")
        print(f"\nround-trip: {ok} identical, {bad} failed")
