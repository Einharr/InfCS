import sys
b=open(sys.argv[1],'rb').read()
tags=['SHFT','MAP ','KEYS','KEY ','MOSB','JOYB','JOYX','JOYS','JOYA','JOYH','POV ','IMAP','CMDS','INFO','DATA','CATE','MBTS','EQIV','INMS']
hits={}
for t in tags:
    pat=bytes(reversed(t.encode()))   # little-endian immediate
    offs=[]; i=b.find(pat)
    while i!=-1 and len(offs)<40:
        offs.append(i); i=b.find(pat,i+1)
    hits[t]=offs
for t in tags:
    print(f"{t!r:8s} {len(hits[t]):3d}  " + ' '.join(f"{o:x}" for o in hits[t][:14]))
