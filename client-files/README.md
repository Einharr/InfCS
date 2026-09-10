# The files that get installed into the client

Install with the client **closed**:

```
python deploy.py --game "E:\Games\SWG\Dev\SWG Infinity\Test Center"
python deploy.py --game ... --undo      # put it back as it was
```

`deploy.py` makes a `.bak` of the stock files exactly once, simply deletes our own
files on an undo, and for the ones with no `.bak` (which means the stock file came
out of the `.tre`) it removes ours - the client falls back to the `.tre` contents.

## What goes where

| From here | Into the client | Ours or stock |
|---|---|---|
| `ui/ui_consoleport_context.inc` | `ui/` | entirely ours |
| `ui/ui_consoleport_charselect.inc` | `ui/` | entirely ours |
| `ui/ui_ground_hud.inc` | `ui/` | stock plus one `<include>` line |
| `ui/ui_avatar_selection.inc` | `ui/` | stock plus an `<include>` as `AvSel`'s last child |
| `ui/ui_pda_inventory.inc` | `ui/` | stock plus an attachment point |
| `ui/ui_pda_char_sheet.inc` | `ui/` | stock plus an attachment point |
| `input/*.iff` (4 files) | `input/` | stock, regenerated for the module's slot model |
| `texture/consoleport_*.dds` (5 files) | `texture/` | entirely ours |
| `consoleport.ini` | the root (next to `swgemu.exe`) | ours; NOT overwritten on a repeat install |

## Why markup is needed at all when the UI is built from code

Three things cannot be done at runtime, and they are the only reason the `.inc`
files are here:

1. **`SourceResource` on a `UIImage` is applied only by the markup loader.**
   Setting it at runtime on a clone is impossible - the picture keeps the
   template's texture. So `ui_consoleport_context.inc` holds a hidden
   `cpGlyphTemplate` already pointing at our atlas; the module clones it and
   changes only the `SourceRect`.
2. **On the character selection screen `GroundHUD` is not built yet**
   (`/GroundHUD.Toolbar` does not resolve there), so the frame and glyph templates
   for it arrive in a separate `ui_consoleport_charselect.inc`, included into
   `ui_avatar_selection.inc`.
3. **The attachment points**: one `<include>` line in the stock files is all it
   takes for our templates to end up in the tree.

Without the markup the module does not crash: it writes about it into the trace
and works with no frame, by buttons.

## Texture limits, earned through crashes

- **The glyph atlas is strictly 256x256.** 256x320 kills the client while loading
  the texture, 512x512 loads silently empty - the glyphs simply vanish. Hence the
  ceiling of 16 sprites of 64 px.
- **A page's background is painted by `BackgroundTint`, not by `Color`.** `Color`
  has a meaning of its own for a page, and a bright bar through it gives a muddy
  dark-olive edge. For a crisp shape a picture is more reliable: the atlas holds a
  solid opaque white block at (26,8), stretched over the required rectangle and
  painted with `Color` directly.

## The input maps

`input/*.iff` are regenerated for the module's slot model: button `k` -> slot `k`,
the L2 variant -> `8+k`, the R2 one -> `16+k` (the button order is up, left, right,
down, triangle, square, circle, cross). The D-pad arrives as synthetic JOYB 26..29,
the triggers as 30/31, the sticks as 18..25.

Two limits baked into the generator:

- **there are exactly 24 executable toolbar slots** (`CuiActions::toolbarSlot00..23`).
  Cells above that exist and accept an icon, but there is nothing to execute them
  with, and a binding to a command name that does not exist kills the client while
  loading the map;
- the F13 scancode (100) does not reach the client - the D-pad from the keyboard
  has been remapped onto ordinary scancodes.

Regeneration: `python tools/build.py`. Verification: `python tools/validate.py`.
