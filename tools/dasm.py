import sys, re; sys.path.insert(0,sys.argv[1])
from pe import PE
import capstone
p=PE(r"E:\Games\SWG\Live\swgemu.exe"); b=p.b
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
def ann(op):
    for m in re.finditer(r'0x([0-9a-f]{6,8})', op):
        va=int(m.group(1),16); o=p.va2off(va)
        if o and 0x11dc000 <= o < 0x1508000:
            s=b[o:o+48].split(b'\0')[0]
            if s and all(32<=c<127 for c in s): return f'   ; "{s.decode()}"'
    return ''
s=int(sys.argv[2],16); e=int(sys.argv[3],16)
off=p.va2off(s)
for i in md.disasm(b[off:off+(e-s)], s):
    print(f"{i.address:08x}  {i.mnemonic:7s} {i.op_str}{ann(i.op_str)}")
