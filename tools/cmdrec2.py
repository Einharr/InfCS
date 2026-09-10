# -*- coding: utf-8 -*-
"""An inputmap CMD record - after the source InputMap_Command.cpp, load_0006.

    name              cstring
    types             uint32   a bit mask, see below
    userDefined       uint8
    repeatStartDelay  uint8
    press  : int32 message, float32 value, cstring str
    repeat : int32 message, float32 value, cstring str
    release: int32 message, float32 value, cstring str
    reset  : int32 message, float32 value, cstring str

An earlier reading assumed two sub-records (DOWN/UP). That happened to fit button
commands and would have fallen apart on axes.

types is a MASK, not an enumeration: "type 3" on almost every ground command is
T_PRESSABLE = T_BUTTON|T_POVHAT, which is why such a command legitimately sits on both
a button and the D-pad. An axis is a bit of its own.

For an axis the client queues the message with arg3 (the axis position) * press value,
so value in the record is a multiplier.
"""
import struct

T_BUTTON, T_POVHAT, T_AXIS, T_SLIDER = 1, 2, 4, 8
T_PRESSABLE = T_BUTTON | T_POVHAT
T_ANY = T_PRESSABLE | T_AXIS | T_SLIDER

MSG_EXEC = 0x0144        # run str as a console command


def _ev(message=0, value=0.0, s=b''):
    return struct.pack('<if', message, value) + s + b'\0'


def make(name, types=T_PRESSABLE, user_defined=0, repeat_delay=0,
         press=None, repeat=None, release=None, reset=None):
    """press/repeat/release/reset - tuples of (message, value, bytes), or None."""
    out = name.encode('latin-1') + b'\0'
    out += struct.pack('<I', types)
    out += bytes((user_defined & 0xFF, repeat_delay & 0xFF))
    for ev in (press, repeat, release, reset):
        out += _ev(*ev) if ev else _ev()
    return out


def parse(rec):
    """Parse a CMD chunk payload. Returns a dict, or None."""
    n = rec.find(b'\0')
    if n < 0:
        return None
    d = {'name': rec[:n].decode('latin-1')}
    o = n + 1
    if o + 6 > len(rec):
        return None
    d['types'] = struct.unpack_from('<I', rec, o)[0]
    d['userDefined'] = rec[o + 4]
    d['repeatStartDelay'] = rec[o + 5]
    o += 6
    for key in ('press', 'repeat', 'release', 'reset'):
        if o + 8 > len(rec):
            return None
        msg, val = struct.unpack_from('<if', rec, o)
        o += 8
        e = rec.find(b'\0', o)
        if e < 0:
            return None
        d[key] = (msg, val, rec[o:e].decode('latin-1'))
        o = e + 1
    d['tail'] = len(rec) - o
    return d


def type_name(t):
    parts = [n for b, n in ((T_BUTTON, 'BUTTON'), (T_POVHAT, 'POVHAT'),
                            (T_AXIS, 'AXIS'), (T_SLIDER, 'SLIDER')) if t & b]
    return '|'.join(parts) or '?'


def walk_cmds(blob):
    """Walk the IFF and yield the payload of every CMD chunk."""
    out = []

    def walk(b, o, end):
        while o < end - 8:
            tag = b[o:o + 4]
            size = struct.unpack_from('>I', b, o + 4)[0]
            o += 8
            if tag == b'FORM':
                walk(b, o + 4, o + size)
            elif tag.strip() == b'CMD':
                out.append(b[o:o + size])
            o += size

    walk(blob, 0, len(blob))
    return out
