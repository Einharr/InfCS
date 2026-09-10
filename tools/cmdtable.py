# -*- coding: utf-8 -*-
"""Editing the inputmap command table the way the client reads it.

The fact that matters, from InputMap_Command.cpp (load_0006_all):

    while (iff.enterForm(TAG_CATE, true))
    {
        read TAG_NAME
        numCmds = iff.getNumberOfBlocksLeft()
        for i in numCmds: cmd->load_0006(iff)
    }

CMD records are read only from inside a CATE form. Chunks appended to the end of FORM
0006 are invisible - the loop never reaches them. That is why our hand-made commands
could not be found by name, the axis bindings got cmd=NULL and went silent, while
bindings onto stock commands worked.

The stock table has eight categories and one of them, 'custom', is empty. That is the
one meant for user commands, so that is where we append.

    FORM CMDS { FORM 0006 { FORM CATE { NAME, CMD, CMD, ... } ... } }
An insert grows three nested forms: CATE, 0006 and CMDS.
"""
import struct


def _sz(b, o):
    return struct.unpack_from('>I', b, o)[0]


def _set_sz(b, o, v):
    struct.pack_into('>I', b, o, v)


def categories(blob):
    """[(name, the CATE form offset, its size, the command count)] in file order."""
    out = []
    inner_end = 8 + _sz(blob, 4)
    o = 24                                   # into FORM CMDS -> FORM 0006
    while o < inner_end - 8:
        tag, s = blob[o:o + 4], _sz(blob, o + 4)
        if tag == b'FORM' and blob[o + 8:o + 12] == b'CATE':
            p, end, name, n = o + 12, o + 8 + s, None, 0
            while p < end - 8:
                t2, s2 = blob[p:p + 4], _sz(blob, p + 4)
                if t2 == b'NAME':
                    name = blob[p + 8:p + 8 + s2].split(b'\0')[0].decode('latin-1')
                elif t2.strip() == b'CMD':
                    n += 1
                p += 8 + s2
            out.append((name, o, s, n))
        o += 8 + s
    return out


def add_commands(blob, records, category='custom'):
    """Insert CMD records inside the named category. records is a list of bytes."""
    b = bytearray(blob)
    cats = categories(bytes(b))
    match = [c for c in cats if c[0] == category]
    if not match:
        raise ValueError('category %r not found; there are: %s'
                         % (category, [c[0] for c in cats]))
    _, cat_off, cat_size, _ = match[0]

    add = b''.join(b'CMD ' + struct.pack('>I', len(r)) + r for r in records)
    insert_at = cat_off + 8 + cat_size       # right past the end of the CATE form

    b[insert_at:insert_at] = add
    # the sizes of the three nested forms: CATE, then 0006, then CMDS
    _set_sz(b, cat_off + 4, cat_size + len(add))
    _set_sz(b, 16, _sz(b, 16) + len(add))    # FORM 0006
    _set_sz(b, 4, _sz(b, 4) + len(add))      # FORM CMDS
    return bytes(b)


def read_like_client(blob):
    """Repeats load_0006_all: commands only count inside a CATE.

    Returns [(category, name, payload)] - what findCommandByName will see.
    """
    seen = []
    inner_end = 8 + _sz(blob, 4)
    o = 24
    while o < inner_end - 8:
        tag, s = blob[o:o + 4], _sz(blob, o + 4)
        if not (tag == b'FORM' and blob[o + 8:o + 12] == b'CATE'):
            o += 8 + s
            continue
        p, end, cat = o + 12, o + 8 + s, None
        while p < end - 8:
            t2, s2 = blob[p:p + 4], _sz(blob, p + 4)
            if t2 == b'NAME':
                cat = blob[p + 8:p + 8 + s2].split(b'\0')[0].decode('latin-1')
            elif t2.strip() == b'CMD':
                payload = blob[p + 8:p + 8 + s2]
                seen.append((cat, payload.split(b'\0')[0].decode('latin-1'), payload))
            p += 8 + s2
        o += 8 + s
    return seen
