"""The gamepad glyph atlas, out of the Kenney Input Prompts set (CC0).

Sprites go into a 256x256 uncompressed 32-bit DDS. No DXT compressor needed: the client
ships uncompressed textures itself (ui_bounty.dds, 64x64, 32bpp), so it takes the
format - the header here is that file's, byte for byte, with the dimensions changed.

The Kenney icons are white on transparent and get repainted the HUD turquoise, so they
do not stand out from the SWG interface.
"""
import os, sys, struct, zipfile
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ZIP = os.path.join(os.path.dirname(HERE), 'assets', 'kenney_input-prompts_1.5.zip')
OUT_TEX = os.path.join(os.path.dirname(HERE), 'client-files', 'texture')

TILE = 32
# Strictly 256x256, confirmed in game 7 Sep 2026: 256x320 kills the client at startup,
# 512x512 loads silently empty. Since it cannot grow, the tile went from 64 to 32 on
# 9 Sep 2026 - 8x8 = 64 places instead of 16, and all sixteen had been used. Nothing is
# lost: the glyphs are drawn at 24-28 px, so the source still beats the target. The
# cursor sprite is gone from the set - the cursor takes its picture from the markup.
COLS = 8
SIZE = TILE * COLS              # the atlas width, 256


def _pow2(v):
    p = 1
    while p < v:
        p *= 2
    return p


def atlas_h(n):
    """The atlas height - always the width.

    The client only takes square power-of-two textures: 256x320 and 512x256 killed it at
    startup (0xC0000005), 256x256 has worked for years.
    """
    need = _pow2(TILE * ((n + COLS - 1) // COLS))
    return max(SIZE, need)
TINT = (0xFF, 0xFF, 0xFF)       # a neutral atlas; the color is set in the markup

# the order in the atlas is fixed: the index determines the SourceRect
ORDER = ['dpad_up', 'dpad_left', 'dpad_right', 'dpad_down',
         'face_top', 'face_left', 'face_right', 'face_bottom',
         'sh_l', 'sh_r', 'tr_l', 'tr_r',
         'dpad',                        # 12: the whole D-pad - the glyph for a cross's center
         'face_all',                    # 13: the four face buttons as one picture, for a cross's center
         'l3', 'r3',                    # 14,15: the stick PRESSES - glyphs above their own cells
         # 16,17: the sticks themselves, deflection - not l3/r3. A radial selection is
         # made by deflecting, so a press icon there would be a lie.
         'stick_l', 'stick_r',
         # 18: solid white fill, stretched into the cursor frame's bars. They used to
         # borrow (26,8) from a neighbouring sprite - dense white at tile 64, empty after
         # the move to 32, and the frame went transparent. Its own tile cannot do that.
         'solid',
         # 19..21: context mode actions. Not in the Kenney set, so drawn here out of
         # simple shapes - at 24 px detail collapses anyway.
         'act_radial',                  # 19: a circle split into sectors - "the target radial menu"
         'act_use',                     # 20: an arrow into a frame - "interact"
         'act_clear']                   # 21: a crossed-out circle - "clear the target"
# Ours, with no file behind them: the face composite and the solid fill.
PROCEDURAL = {'face_all', 'solid', 'act_radial', 'act_use', 'act_clear'}

FAMILIES = {
    'ps': ('PlayStation Series', {
        'dpad_up': 'playstation_dpad_up.png',       'dpad_left': 'playstation_dpad_left.png',
        'dpad_right': 'playstation_dpad_right.png', 'dpad_down': 'playstation_dpad_down.png',
        'face_top': 'playstation_button_triangle.png', 'face_left': 'playstation_button_square.png',
        'face_right': 'playstation_button_circle.png', 'face_bottom': 'playstation_button_cross.png',
        'sh_l': 'playstation_trigger_l1.png', 'sh_r': 'playstation_trigger_r1.png',
        'tr_l': 'playstation_trigger_l2.png', 'tr_r': 'playstation_trigger_r2.png',
        'dpad': 'playstation_dpad.png',
        'l3': 'playstation_button_l3.png', 'r3': 'playstation_button_r3.png',
        'stick_l': 'playstation_stick_l.png', 'stick_r': 'playstation_stick_r.png'}),
    'xbox': ('Xbox Series', {
        'dpad_up': 'xbox_dpad_up.png',       'dpad_left': 'xbox_dpad_left.png',
        'dpad_right': 'xbox_dpad_right.png', 'dpad_down': 'xbox_dpad_down.png',
        'face_top': 'xbox_button_y.png', 'face_left': 'xbox_button_x.png',
        'face_right': 'xbox_button_b.png', 'face_bottom': 'xbox_button_a.png',
        'sh_l': 'xbox_lb.png', 'sh_r': 'xbox_rb.png',
        'tr_l': 'xbox_lt.png', 'tr_r': 'xbox_rt.png',
        'dpad': 'xbox_dpad.png',
        'l3': 'xbox_stick_l_press.png', 'r3': 'xbox_stick_r_press.png',
        'stick_l': 'xbox_stick_l.png', 'stick_r': 'xbox_stick_r.png'}),
    'deck': ('Steam Deck', {
        'dpad_up': 'steamdeck_dpad_up.png',       'dpad_left': 'steamdeck_dpad_left.png',
        'dpad_right': 'steamdeck_dpad_right.png', 'dpad_down': 'steamdeck_dpad_down.png',
        'face_top': 'steamdeck_button_y.png', 'face_left': 'steamdeck_button_x.png',
        'face_right': 'steamdeck_button_b.png', 'face_bottom': 'steamdeck_button_a.png',
        'sh_l': 'steamdeck_button_l1.png', 'sh_r': 'steamdeck_button_r1.png',
        'tr_l': 'steamdeck_button_l2.png', 'tr_r': 'steamdeck_button_r2.png',
        'dpad': 'steamdeck_dpad.png',
        'l3': 'steamdeck_stick_l_press.png', 'r3': 'steamdeck_stick_r_press.png',
        'stick_l': 'steamdeck_stick_l.png', 'stick_r': 'steamdeck_stick_r.png'}),
}


def action_glyphs():
    """The context mode icons, drawn 4x and shrunk - on a 32 px tile the edge tears
    otherwise. They read by silhouette: at 32 px anything thinner than two pixels is gone.
    """
    from PIL import ImageDraw
    SS, N = 4, TILE * 4
    col = TINT + (255,)
    dim = TINT + (110,)
    W = 3 * SS                      # the base line thickness
    out = {}

    # RADIAL: a ring in four sectors, one filled - what the button actually opens.
    radial = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(radial)
    box = [14, 14, N - 14, N - 14]
    d.pieslice(box, -78, -12, fill=col)                     # the active sector
    for a0 in (12, 102, 192):                               # the rest as arcs only
        d.arc(box, a0, a0 + 66, fill=dim, width=W + 2)
    inner = [N // 2 - 20, N // 2 - 20, N // 2 + 20, N // 2 + 20]
    d.ellipse(inner, fill=(0, 0, 0, 0))                     # a hole in the middle
    d.ellipse(inner, outline=col, width=W)
    out['act_radial'] = radial.resize((TILE, TILE), Image.LANCZOS)

    # INTERACT: an open panel with a thick arrow going in. A terminal with a screen and
    # a finger turned into a floppy disk with a tail at 32 px. An arrow reads.
    use = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(use)
    d.line([(N - 34, 18), (18, 18), (18, N - 18), (N - 34, N - 18)], fill=col, width=W + 2)
    d.line([(N - 74, N // 2), (N - 30, N // 2)], fill=col, width=W + 2)
    d.polygon([(N - 44, N // 2 - 18), (N - 44, N // 2 + 18), (N - 14, N // 2)], fill=col)
    out['act_use'] = use.resize((TILE, TILE), Image.LANCZOS)

    # CLEAR TARGET: a circle with a slash - the plainest "no" there is, and round where
    # the cross button is angular.
    clear = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(clear)
    d.ellipse([16, 16, N - 16, N - 16], outline=col, width=W + 2)
    d.line([(34, N - 34), (N - 34, 34)], fill=col, width=W + 2)
    out['act_clear'] = clear.resize((TILE, TILE), Image.LANCZOS)
    return out


def rect(key):
    """The SourceRect in the atlas: left, top, right, bottom."""
    i = ORDER.index(key)
    x, y = (i % COLS) * TILE, (i // COLS) * TILE
    return (x, y, x + TILE, y + TILE)


def tint(im):
    """Repaint a white icon in the HUD color, preserving the alpha."""
    im = im.convert('RGBA')
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a:
                px[x, y] = (r * TINT[0] // 255, g * TINT[1] // 255, b * TINT[2] // 255, a)
    return im


def pointer():
    """The cursor arrow (as in ConsolePort_Cursor): white with a dark outline, with the
    tip in the sprite's top left corner."""
    from PIL import ImageDraw
    im = Image.new('RGBA', (TILE, TILE), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pts = [(6, 4), (6, 50), (18, 39), (27, 58), (37, 53), (28, 35), (44, 35)]
    d.polygon(pts, fill=(255, 255, 255, 255), outline=(20, 24, 28, 255))
    d.line(pts + [pts[0]], fill=(20, 24, 28, 255), width=3, joint='curve')
    d.polygon([(9, 9), (9, 43), (18, 35), (28, 53), (33, 50), (24, 32), (37, 32)], fill=(255, 255, 255, 255))
    return im


def face_cluster(tiles):
    """The four face buttons as one diamond, the way they lie on the pad.

    It is the glyph for the centre of the face cross, the role the whole D-pad icon plays
    on the other one. The Kenney set has no such composite, so it is assembled here.
    """
    im = Image.new('RGBA', (TILE, TILE), (0, 0, 0, 0))
    s = TILE // 2 - 2                      # the size of one face button in the composite
    off = {'face_top': ((TILE - s) // 2, 0),
           'face_left': (0, (TILE - s) // 2),
           'face_right': (TILE - s, (TILE - s) // 2),
           'face_bottom': ((TILE - s) // 2, TILE - s)}
    for key, xy in off.items():
        t = tiles.get(key)
        if t is None:
            continue
        im.paste(t.resize((s, s), Image.LANCZOS), xy, t.resize((s, s), Image.LANCZOS))
    return im


def dds(im):
    """An uncompressed 32bpp DDS. The header was copied from the shipped ui_bounty.dds."""
    w, h = im.size
    linear = w * h * 4
    hdr = b'DDS ' + struct.pack('<7I', 124, 0x000A1007, h, w, linear, 0, 0)
    hdr += b'\0' * 44                                     # dwReserved1[11]
    hdr += struct.pack('<8I', 32, 0x00000041, 0, 32,
                       0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr += struct.pack('<4I', 0x00401008, 0, 0, 0)
    hdr += b'\0' * 4                                      # dwReserved2
    assert len(hdr) == 128, len(hdr)
    px = im.convert('RGBA').tobytes()                      # RGBA
    out = bytearray(len(px))
    out[0::4] = px[2::4]; out[1::4] = px[1::4]             # -> BGRA
    out[2::4] = px[0::4]; out[3::4] = px[3::4]
    return hdr + bytes(out)


def build(family, outline=True):
    """Outline icons: thin lines, HUD aesthetic.

    The atlas stays neutral white and the shade comes from the Image widget's Color
    attribute, so it can be edited without rebuilding the texture. The first attempt baked
    #1CFFFF into the atlas - the colour of the slot itself - and the glyphs vanished into
    the background."""
    folder, names = FAMILIES[family]
    atlas = Image.new('RGBA', (SIZE, atlas_h(len(ORDER))), (0, 0, 0, 0))
    missing = []
    with zipfile.ZipFile(ZIP) as z:
        have = {n.rsplit('/', 1)[-1]: n for n in z.namelist()
                if n.startswith(folder + '/Default/') and n.endswith('.png')}
        tiles = {}
        for key in ORDER:
            if key in PROCEDURAL:
                continue                       # drawn afterwards, out of the assembled ones
            fn = names[key]
            if outline:
                alt = fn.replace('.png', '_outline.png')
                if alt in have:
                    fn = alt
            if fn not in have:
                # not every Kenney set has l3/r3 - skip quietly, the glyph stays empty
                if key not in ('l3', 'r3'):
                    missing.append(fn)
                continue
            with z.open(have[fn]) as fh:
                im = Image.open(fh).convert('RGBA')
            if im.size != (TILE, TILE):
                im = im.resize((TILE, TILE), Image.LANCZOS)
            l, t, _, _ = rect(key)
            im = tint(im)
            tiles[key] = im
            atlas.paste(im, (l, t))
    # The composite uses the FILLED sprites: outlines are thin rings and at 18 px in a
    # cross centre they do not read.
    solid = {}
    with zipfile.ZipFile(ZIP) as z:
        have = {n.rsplit('/', 1)[-1]: n for n in z.namelist()
                if n.startswith(folder + '/Default/') and n.endswith('.png')}
        for key in ('face_top', 'face_left', 'face_right', 'face_bottom'):
            fn = names[key]
            if fn in have:
                with z.open(have[fn]) as fh:
                    im = Image.open(fh).convert('RGBA')
                if im.size != (TILE, TILE):
                    im = im.resize((TILE, TILE), Image.LANCZOS)
                solid[key] = tint(im)
    l, t, _, _ = rect('face_all'); atlas.paste(face_cluster(solid or tiles), (l, t))
    # The solid fill tile - we draw it ourselves, the set has none.
    l, t, r, b = rect('solid')
    atlas.paste(Image.new('RGBA', (TILE, TILE), (255, 255, 255, 255)), (l, t))
    for key, im in action_glyphs().items():
        l, t, _, _ = rect(key)
        atlas.paste(im, (l, t))
    if missing:
        raise SystemExit('no files in the %s set: %s' % (family, missing))
    os.makedirs(OUT_TEX, exist_ok=True)
    # ps takes the plain name, the rest a suffix. The module picks the resource from the
    # pad it recognized, so both files have to be in the game at once.
    p = os.path.join(OUT_TEX, 'consoleport_glyphs.dds' if family == 'ps'
                              else 'consoleport_glyphs_%s.dds' % family)
    open(p, 'wb').write(dds(atlas))
    return p, atlas


if __name__ == '__main__':
    fam = sys.argv[1] if len(sys.argv) > 1 else 'ps'
    p, atlas = build(fam, outline='--filled' not in sys.argv)
    print('set %s -> %s  (%d bytes, %dx%d)' % (fam, os.path.basename(p),
                                                os.path.getsize(p), SIZE, atlas_h(len(ORDER))))
    nz = sum(1 for k in ORDER if atlas.crop(rect(k)).getbbox())
    print('  non-empty sprites: %d of %d' % (nz, len(ORDER)))
    for k in ORDER:
        print('    %-12s SourceRect=%s' % (k, ','.join(map(str, rect(k)))))
