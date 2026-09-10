import sys, collections
sys.path.insert(0,sys.argv[1])
from iff import dump
c = collections.Counter()
for p in sys.argv[2:]:
    for line in dump(p):
        s=line.strip()
        c[s.split(' [')[0].split(' (')[0]] += 1
for k,v in c.most_common():
    print(f"{v:6d}  {k}")
