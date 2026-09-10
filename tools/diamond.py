# -*- coding: utf-8 -*-
"""The markup that goes into the client: our context page and the stock files that
point at it.

There is no crossbar markup here. NOTE (6 Sep 2026, confirmed in game): the binary's
UIVolumePage::Pack ignores DoNotPackChildren - cells get packed onto a grid whatever
the file says, so the diamonds cannot come from markup at all. The module builds them
at runtime inside the stock toolbar page (src/consoleport/crossbar/), and all the
files have to do is exist as attachment points:

  ui_consoleport_context.inc  our page in the HUD: the debug rows, and the hidden
                              glyph template the module clones (SourceResource is
                              resolved by the markup loader and cannot be set on a
                              clone at runtime)
  ui_ground_hud.inc           stock plus one <include>, plus OnShow/OnHide on the
                              combat state icon
  ui_pda_inventory.inc        stock plus OnShow/OnHide on the window root
  ui_pda_char_sheet.inc       the same

    python diamond.py        # -> client-files/ui/*.inc
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
ROOT = os.path.dirname(HERE)
DIST = os.path.join(ROOT, 'client-files', 'ui')
STOCK_UI = os.path.join(ROOT, 'stock', 'ui')
SRC_HUD = os.path.join(STOCK_UI, 'ui_ground_hud.inc')

TAB = chr(9)
FRAME = '/Styles.New.fatFrameInside_palW.rs_default'
ATLAS = 'consoleport_glyphs'


def debug_page(layout='diamonds / hook model'):
    """The context debug window: what the mod "understands" about the windows. Filled in by the OnShow/OnHide scripts."""
    # The first lines are driven by the markup's UI scripts (the client's windows), the
    # rest by the module itself through setLocalText: a script cannot reach the pad and target state.
    rows = [('lInvL', 'inventory', 'lInv', 'closed'), ('lCombatL', 'combat', 'lCombat', 'no'),
            ('lSheetL', 'char sheet', 'lSheet', 'closed'),
            ('lModeL', 'pad mode', 'lMode', 'game'), ('lLayoutL', 'layout', 'lLayout', layout),
            ('lM3L', 'M3 (L2+R2)', 'lM3', 'pane+1 while held'),
            ('lSetL', 'cycle set', 'lSet', '-'),
            ('lHoldL', 'hold', 'lHold', '-'),
            ('lRingL', 'ring', 'lRing', 'closed'),
            ('lTargetL', 'target', 'lTarget', 'none'),
            ('lCtxL', 'context', 'lCtx', 'off'),
            ('lThemeL', 'theme', 'lTheme', '-'),
            ('lPadL', 'pad', 'lPad', '-')]
    ROW = 21
    H = 52 + ROW * len(rows)
    t = [TAB * 2 + "<Page", TAB * 3 + "GetsInput='false'", TAB * 3 + "Location='24,260'", TAB * 3 + "Name='cpDebug'",
         TAB * 3 + "OpacityRelativeMin='1.00'",
         TAB * 3 + "PackLocation='nfn,nfn'", TAB * 3 + "PackSize='f,f'", TAB * 3 + "ScrollExtent='430,%d'" % H,
         TAB * 3 + "Size='430,%d'" % H, TAB * 2 + ">",
         ]
    # The backdrop goes LAST. In the client UI the child order is the draw order and the
    # FIRST child ends up ON TOP: while bg stood first it lay as a slab over the lines
    # and the window read as a murky smear.
    bg = [TAB * 3 + "<Page", TAB * 4 + "BackgroundOpacity='0.93'", TAB * 4 + "GetsInput='false'", TAB * 4 + "Location='0,0'",
          TAB * 4 + "Name='bg'", TAB * 4 + "PackLocation='nfn,nfn'", TAB * 4 + "PackSize='f,f'",
          TAB * 4 + "RStyleDefault='%s'" % FRAME, TAB * 4 + "ScrollExtent='430,%d'" % H, TAB * 4 + "Size='430,%d'" % H, TAB * 3 + "/>"]

    def text(name, x, y, w, s, color, font='bold_12'):
        return [TAB * 3 + "<Text", TAB * 4 + "Font='%s'" % font, TAB * 4 + "GetsInput='false'", TAB * 4 + "OpacityRelativeMin='1.00'",
                TAB * 4 + "Location='%d,%d'" % (x, y), TAB * 4 + "Name='%s'" % name, TAB * 4 + "PackLocation='nfn,nfn'",
                TAB * 4 + "PackSize='f,f'", TAB * 4 + "ScrollExtent='%d,20'" % w, TAB * 4 + "Size='%d,20'" % w,
                TAB * 4 + "TextColor='%s'" % color, TAB * 3 + ">%s</Text>" % s]
    t += text('title', 12, 6, 400, 'CONSOLEPORT', '#1CFFFF', 'bold_16')
    for i, (ln, label, vn, val) in enumerate(rows):
        y = 32 + ROW * i
        # The label is muted and the value white and a little larger: both used to be
        # equally faint and the window did not read.
        t += text(ln, 12, y, 104, label, '#6E8B94', 'bold_11') + text(vn, 122, y, 296, val, '#E8F4F8', 'bold_12')
    # The glyph template for the hook module. SourceResource on a UIImage is resolved by
    # THE MARKUP LOADER; setting it at runtime on a clone is impossible - the picture
    # stays the template's. So we keep one hidden picture on our atlas here: the module
    # clones it and changes only the SourceRect, which on a UIImage is real.
    t += [TAB * 3 + "<Image", TAB * 4 + "AbsorbsInput='false'", TAB * 4 + "Color='#FFFFFF'",
          TAB * 4 + "GetsInput='false'", TAB * 4 + "Location='0,0'", TAB * 4 + "Name='cpGlyphTemplate'",
          TAB * 4 + "OpacityRelativeMin='1.00'", TAB * 4 + "PackLocation='nfn,nfn'", TAB * 4 + "PackSize='f,f'",
          TAB * 4 + "ScrollExtent='64,64'", TAB * 4 + "Size='64,64'",
          TAB * 4 + "SourceRect='0,0,64,64'", TAB * 4 + "SourceResource='%s'" % ATLAS,
          TAB * 4 + "Visible='false'", TAB * 3 + "/>"]
    t += bg
    t.append(TAB * 2 + "</Page>")
    return chr(10).join(t) + chr(10)


def script(target, value, ups, at_hud=False):
    """A UIScriptEngine line: lhs -> SetProperty on the context. Upwards only the
    'parent.' prefix works (SetProperty handles it recursively, UIBaseObject.cpp),
    downwards 'a.b.c' through the children (UIPage::SetProperty). The '/' in a script is
    the division operator, so an absolute path cannot be written that way. ups is how
    many parents up to the page where GroundHUD lies (at_hud=False) or GroundHUD itself (True)."""
    prefix = 'parent.' * ups + ('' if at_hud else 'GroundHUD.')
    return "%scpDebug.%s.LocalText=%s" % (prefix, target, value)


def ground_hud():
    """The stock HUD plus one include. The toolbar page stays the stock/QoL one - the
    module does the crossbar geometry inside it - so all we add is our context page."""
    t = open(SRC_HUD, encoding='latin-1').read()
    a = '<include>ui_ground_hud_toolbar.inc</include>'
    assert t.count(a) == 1
    t = t.replace(a, a + chr(10) + TAB * 2 + '<include>ui_consoleport_context.inc</include>')
    # combat: the combat state icon in the player status GroundHUD.MFDStatus.composite.vs.i
    # (SwgCuiStatusGround shows and hides it with SetVisible -> OnShow/OnHide);
    # from the icon up to GroundHUD there are four parents
    m = re.search(r"(\t+)<Image\n(?:\1\t[^\n]*\n)*?\1\tName='i'\n(?:\1\t[^\n]*\n)*?\1\tStyle='/Styles\.Icon\.state\.combat'\n", t)
    assert m, 'the combat state icon was not found'
    blk = m.group(0); ind = m.group(1) + '\t'
    blk2 = blk.replace(ind + "Name='i'\n", ind + "Name='i'\n" + ind + "OnHide='%s'\n" % script('lCombat', 'no', 4, True)
                       + ind + "OnShow='%s'\n" % script('lCombat', 'YES', 4, True), 1)
    t = t.replace(blk, blk2, 1)
    assert "OnShow='parent.parent.parent.parent.cpDebug.lCombat" in t
    return t


def window_override(fname, page_name, target):
    """a copy of a stock window with OnShow/OnHide on the window's root page"""
    t = open(os.path.join(STOCK_UI, fname), encoding='latin-1').read()
    key = "\t\t\tName='%s'\n" % page_name
    assert t.count(key) == 1, (fname, page_name, t.count(key))
    # the PDA windows lie in root.PDA.<window>: two parents up to root, where GroundHUD is a child
    t = t.replace(key, key + "\t\t\tOnHide='%s'\n\t\t\tOnShow='%s'\n" % (script(target, 'closed', 2), script(target, 'OPEN', 2)), 1)
    return t

if __name__ == '__main__':
    os.makedirs(DIST, exist_ok=True)
    open(os.path.join(DIST, 'ui_consoleport_context.inc'), 'w', encoding='latin-1').write(debug_page())
    open(os.path.join(DIST, 'ui_ground_hud.inc'), 'w', encoding='latin-1').write(ground_hud())
    open(os.path.join(DIST, 'ui_pda_inventory.inc'), 'w', encoding='latin-1').write(window_override('ui_pda_inventory.inc', 'Inventory', 'lInv'))
    open(os.path.join(DIST, 'ui_pda_char_sheet.inc'), 'w', encoding='latin-1').write(window_override('ui_pda_char_sheet.inc', 'CharacterSheet', 'lSheet'))
    for f in sorted(os.listdir(DIST)):
        p = os.path.join(DIST, f); print('  %-34s %7d B' % (f, os.path.getsize(p)))
