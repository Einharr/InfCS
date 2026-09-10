"""Structural checks of the recomposed markup.

The key one is the fourth: the client brings widgets to life by the names in the
CodeData blocks. If a rearrangement loses such a name, the element silently stops
updating, and in game that looks like "it just does not work". We check that every
name from CodeData is still present in the tree.
"""
import os, re, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)

SRC = os.path.join(os.path.dirname(HERE), 'stock', 'ui')
DIST = os.path.join(os.path.dirname(HERE), 'client-files', 'ui')

# --- the .inc line grammar: one element header per line, attributes one per line ---
OPEN = re.compile(r'^<[A-Za-z][A-Za-z0-9_]*$')
CLOSE = re.compile(r'^</[A-Za-z][A-Za-z0-9_]*>$')
INLINE = re.compile(r'^>.*</[A-Za-z][A-Za-z0-9_]*>$')   # '></Tag>' and '>text</Tag>'
ATTR = re.compile(r"^(\s*)([A-Za-z][A-Za-z0-9_.]*)='([^']*)'$")
COORD_ATTRS = {'Location', 'Size', 'MinimumSize', 'MaximumSize', 'ScrollExtent',
               'MinimumScrollExtent', 'CellSize', 'CellPadding', 'SizeIncrement',
               'IconMaxSize', 'Margin', 'IconMargin'}
NUMS = re.compile(r'^-?\d+(,-?\d+)+$')


def tag_end(ls, i):
    """The end of an element header: the index of the terminator line and the line itself."""
    j = i
    while j < len(ls):
        s = ls[j].strip()
        if s == '/>' or s == '>' or INLINE.match(s):
            return j, s
        j += 1
    raise ValueError('an unclosed header starting at line %d' % i)
ok, bad = [], []
def chk(c, m): (ok if c else bad).append(m)

def attrs(text, name):
    return set(re.findall(r"%s='([^']*)'" % name, text))

def codedata_names(text):
    """The attribute values inside Data Name='CodeData' blocks - the contract with the C++."""
    out = set()
    ls = text.splitlines()
    for i, l in enumerate(ls):
        if l.strip() != "Name='CodeData'":
            continue
        j = i
        while j > 0 and not OPEN.match(ls[j].strip()):
            j -= 1
        e, _ = tag_end(ls, j)
        for k in range(j + 1, e):
            m = ATTR.match(ls[k].strip())
            if m and m.group(2) not in ('Name', 'Size', 'Location'):
                v = m.group(3)
                if re.match(r'^[A-Za-z][A-Za-z0-9_.]*$', v):
                    out.add(v.split('.')[-1])
    return out

# dictionaries of what is allowed, collected from the STOCK markup
stock_styles, stock_fonts = set(), set()
for p in glob.glob(os.path.join(SRC, '*.inc')):
    if 'skinned' in p:
        continue
    t = open(p, encoding='latin-1').read()
    stock_styles |= attrs(t, 'RStyleDefault') | attrs(t, 'Style')
    stock_fonts |= attrs(t, 'Font')

outs = sorted(glob.glob(os.path.join(DIST, '*.inc')))
chk(len(outs) >= 6, "markup output files: %d" % len(outs))

for p in outs:
    n = os.path.basename(p)
    t = open(p, encoding='latin-1').read()
    ls = t.splitlines()

    op = sum(1 for l in ls if OPEN.match(l.strip()))
    cl = sum(1 for l in ls if CLOSE.match(l.strip()) or INLINE.match(l.strip()))
    sc = sum(1 for l in ls if l.strip() == '/>')
    chk(op == cl + sc, "%s: the tags are balanced (%d opened = %d closed + %d self-closed)" % (n, op, cl, sc))

    names = {x.lower() for x in attrs(t, 'Name')}
    sp = os.path.join(SRC, n)
    if os.path.exists(sp):
        stock_names = {x.lower() for x in attrs(open(sp, encoding='latin-1').read(), 'Name')}
        lost = sorted(stock_names - names)
        chk(not lost, "%s: not one widget name was lost (%d)" % (n, len(stock_names))
            + (" | LOST: %s" % lost[:6] if lost else ""))

    st = (attrs(t, 'RStyleDefault') | attrs(t, 'Style')) - stock_styles
    chk(not st, "%s: every style is from the stock set" % n + (" | NOT STOCK: %s" % sorted(st)[:4] if st else ""))

    ft = attrs(t, 'Font') - stock_fonts
    chk(not ft, "%s: every font is from the stock set" % n + (" | NOT STOCK: %s" % sorted(ft) if ft else ""))

    neg = [v for a in COORD_ATTRS for v in attrs(t, a)
           if NUMS.match(v) and any(int(x) < -64 or int(x) > 20000 for x in v.split(','))]
    chk(not neg, "%s: the coordinates are within reason" % n + (" | ODD: %s" % neg[:4] if neg else ""))


# --- the glyph atlas and the references to it ---
import struct
TEX = os.path.join(os.path.dirname(HERE), 'client-files', 'texture')
atlas = os.path.join(TEX, 'consoleport_glyphs.dds')
if os.path.exists(atlas):
    b = open(atlas, 'rb').read()
    ah, aw = struct.unpack_from('<II', b, 12)
    pf = struct.unpack_from('<2I', b, 80)
    chk(b[:4] == b'DDS ' and len(b) - 128 == aw * ah * 4,
        "atlas: DDS %dx%d, uncompressed 32bpp, the size adds up" % (aw, ah))
    chk(pf[0] & 4 == 0, "the atlas has no FourCC - no compressor is required")
    oob = []
    for p2 in outs:
        t2 = open(p2, encoding='latin-1').read()
        # only the Images that reference OUR atlas: the radar has textures of its own
        pat = r"SourceRect=.(\d+),(\d+),(\d+),(\d+).[^<]*?SourceResource=.consoleport_glyphs."
        for r in re.findall(pat, t2):
            l, t3, rr, bb = map(int, r)
            if not (0 <= l < rr <= aw and 0 <= t3 < bb <= ah):
                oob.append(r)
    chk(not oob, "every SourceRect lies inside the atlas" + (" | OUTSIDE: %s" % oob[:3] if oob else ""))
else:
    chk(False, "the glyph atlas is not built")


print("\n".join("  OK   " + m for m in ok))
if bad:
    print("\n".join("  FAIL " + m for m in bad))
print("\n%d markup checks passed, %d failed" % (len(ok), len(bad)))
sys.exit(1 if bad else 0)
