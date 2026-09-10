"""The CRC key of a UI property name. The algorithm was taken from the UILowerString
constructor at 0x10e51a0: crc=0xFFFFFFFF; crc=(crc<<8)^tab[(crc>>24)^tolower(c)]; crc=~crc"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import PE

_p = PE(r"E:\Games\SWG\Live\swgemu.exe")
_o = _p.va2off(0x15F9790)
TAB = list(struct.unpack_from('<256I', _p.b, _o))

def crc(name):
    c = 0xFFFFFFFF
    for ch in name.lower():
        c = ((c << 8) & 0xFFFFFFFF) ^ TAB[((c >> 24) ^ ord(ch)) & 0xFF]
    return (~c) & 0xFFFFFFFF

if __name__ == '__main__':
    print("the CRC table: the first 4 are", [hex(x) for x in TAB[:4]])
    # the check: the computed CRC has to match the value in the ready-made global
    props = {}
    for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ui_props.txt'), encoding='utf-8'):
        n, a = l.split(); props[n] = int(a, 16)
    ok = bad = 0
    for n in ('Visible', 'Color', 'Location', 'Size', 'SourceRect', 'LocalText', 'Opacity'):
        g = props[n]
        stored = struct.unpack_from('<I', _p.b, _p.va2off(g))[0]
        mine = crc(n)
        m = 'MATCH' if stored == mine else 'no'
        if stored == mine: ok += 1
        else: bad += 1
        print(f"  {n:14s} in memory 0x{stored:08X}   computed 0x{mine:08X}   {m}")
    print(f"\nmatched {ok}, diverged {bad}")
