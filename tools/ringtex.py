# -*- coding: utf-8 -*-
"""The ring textures: backdrop, rim and highlight wedge - three separate files.

All three are white with alpha; the shade comes from the widget's Color property. That
is also why they cannot share a file: a widget has one colour for the whole texture,
and the backdrop has to be dark while the rim is turquoise. Uncompressed 32-bit DDS
with the header from glyphs.py, like the atlas.

Strictly 256x256: 512 loads silently empty, 256x320 kills the client at startup
(7 Sep 2026). Stretching to the working ~450 px softens the edges, which here is
exactly what is wanted.

    python ringtex.py        # -> client-files/texture/consoleport_{ring,rim,wedge}.dds
"""
import math
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from glyphs import dds

OUT_TEX = os.path.join(os.path.dirname(HERE), 'client-files', 'texture')
SIZE = 256
SS = 4                      # supersampling: the edges are computed at 1024 and shrunk

RIM_PX = 2.5                # the rim thickness in TEXTURE pixels (about 3 on screen)
WEDGE_SPREAD = 44.0         # the wedge spread in degrees: it suits both 8 and 9 sectors
# Fractions of the texture radius. The wedge has to reach the labels, not fade before
# them: its widget is wider than the ring (2*r*1.18) and the labels sit at 0.85 of that
# radius, so the bright zone runs to 0.86 and fades only at the edge.
WEDGE_IN, WEDGE_FULL, WEDGE_FADE = 0.22, 0.55, 0.88


def _smoothstep(a, b, x):
    if b <= a:
        return 0.0 if x < a else 1.0
    t = (x - a) / (b - a)
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return t * t * (3.0 - 2.0 * t)


def ring_texture():
    """The backdrop: a soft disc fading towards the edge. Tinted dark."""
    n = SIZE * SS
    img = Image.new('RGBA', (n, n), (255, 255, 255, 0))
    px = img.load()
    c = (n - 1) / 2.0
    for y in range(n):
        dy = y - c
        for x in range(n):
            dx = x - c
            r = math.hypot(dx, dy) / c          # 0 at the center, 1 at the edge of the square
            if r > 1.0:
                continue
            # Flat in the middle, zero at the edge: it mutes the 3D world under the
            # labels without cutting a hard circle into the frame.
            a = 0.85 * (1.0 - _smoothstep(0.42, 1.0, r))
            px[x, y] = (255, 255, 255, int(round(a * 255)))
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def rim_texture():
    """The rim: a thin ring at the very edge of the texture. Tinted the HUD turquoise."""
    n = SIZE * SS
    img = Image.new('RGBA', (n, n), (255, 255, 255, 0))
    px = img.load()
    c = (n - 1) / 2.0
    rim_r = 0.94 * c                    # a little inwards, so the feathering is not clipped by the edge
    rim_w = RIM_PX * SS / 2.0
    for y in range(n):
        dy = y - c
        for x in range(n):
            dx = x - c
            d = abs(math.hypot(dx, dy) - rim_r)
            if d > rim_w * 3.0:
                continue
            a = 0.85 * (1.0 - _smoothstep(rim_w, rim_w * 3.0, d))
            px[x, y] = (255, 255, 255, int(round(a * 255)))
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def wedge_texture():
    """The highlight wedge: a WEDGE_SPREAD degree sector pointing up; the client rotates it.

    Soft on all three edges - along the angle so the boundary does not cut the eye, along
    the radius so it breaks off neither at the core nor at the rim.
    """
    n = SIZE * SS
    img = Image.new('RGBA', (n, n), (255, 255, 255, 0))
    px = img.load()
    c = (n - 1) / 2.0
    half = math.radians(WEDGE_SPREAD) / 2.0
    feather = math.radians(WEDGE_SPREAD) * 0.38     # the feathering along the angle
    for y in range(n):
        dy = y - c
        for x in range(n):
            dx = x - c
            r = math.hypot(dx, dy) / c
            if r > 1.0 or r < 0.02:
                continue
            ang = math.atan2(dx, -dy)               # 0 up, clockwise
            a_ang = 1.0 - _smoothstep(half, half + feather, abs(ang))
            if a_ang <= 0.0:
                continue
            # Along the radius: it kindles from the core and fades at the outer edge.
            a_rad = _smoothstep(WEDGE_IN, WEDGE_FULL, r) * (1.0 - _smoothstep(WEDGE_FADE, 1.0, r))
            # Denser towards the label: a ray to the chosen item, not a slab.
            a = 0.72 * a_ang * a_rad * (0.30 + 0.70 * r)
            if a <= 0.0:
                continue
            px[x, y] = (255, 255, 255, int(round(a * 255)))
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def main():
    os.makedirs(OUT_TEX, exist_ok=True)
    made = (('consoleport_ring', ring_texture()),
            ('consoleport_rim', rim_texture()),
            ('consoleport_wedge', wedge_texture()))
    for name, im in made:
        p = os.path.join(OUT_TEX, name + '.dds')
        open(p, 'wb').write(dds(im))
        print('%-28s %d x %d  %d B' % (name + '.dds', im.size[0], im.size[1], os.path.getsize(p)))


if __name__ == '__main__':
    main()
