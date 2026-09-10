# The client's stock files - for comparison

Extracted from the Test Center client's `.tre` (`tools/tre.py`). Nothing of ours
is here: these are the originals, by which it is visible what exactly was changed
in `client-files/`.

Only what is actually used is kept: the files the generators and the validators
read, and the originals of the files `client-files/` patches. The full `.tre`
extraction is not in the package - `tools/tre.py` unpacks it again in a minute.

| Directory | What |
|---|---|
| `ui/` | the stock markup the shipped files are made from: `ui_ground_hud.inc`, `ui_ground_hud_toolbar.inc` (the toolbar page the module builds the crossbar inside), `ui_avatar_selection.inc`, `ui_pda_inventory.inc`, `ui_pda_char_sheet.inc`, plus the HUD parts and `ui_root.ui`/`ui_styles.inc` the documentation points at. `font/verdana_bold_12.inc` is the glyph coverage the captions are limited by; `ui_pda_dps_meter.inc` is here for one reason only - `validate_ui.py` collects the set of legal styles and fonts out of `stock/ui/*.inc`, and this is the file that witnesses the `bold_11` alias |
| `input/` | the stock `.iff` input maps `build.py` starts from: `inputnames.iff`, `groundinputmap_cmds.iff`, `groundinputmap_swg_modern.iff` (the base layout), plus `groundinputmap_swg.iff`/`swg2.iff` as the untouched originals of the two presets we overwrite |
| `texture/` | `ui_bounty.dds` - the stock uncompressed 32bpp texture that proved no DXT compressor is needed for our atlases |
| `groundinputmap_*.txt`, `spaceinputmap_*.txt`, `ui_inputmap.txt` | the stock input maps **as text** - far easier to read by eye than IFF, and `validate.py` reads them to check that the keys the layout takes are free in every stock scheme |

A quick comparison of ours against stock:

```
diff stock/ui/ui_ground_hud.inc client-files/ui/ui_ground_hud.inc
```

For the changed stock files the difference is a single `<include>` line. The
`ui_consoleport_*.inc` files have no stock counterparts: they are entirely ours.
