# Generators and decoders

None of this is needed in order to run the mod - everything has already been
generated and sits in `src/consoleport/abi/`, `src/consoleport/gen/` and
`client-files/`. It is needed if the layout, the glyphs or the client build are to
change.

## Artifact generators

| Script | What it does | Where it writes |
|---|---|---|
| `abitable.py` | takes the ABI off the exe: addresses, calling conventions, 8-byte prologue signatures, vtable slots, offsets | `src/consoleport/abi/addresses.h`, `abi.json`, `docs/03-abi.md` |
| `gen_props.py` | the 1595 UILowerString globals of the property names | `src/consoleport/abi/props.h` |
| `gen_profiles.py` | the pad profiles, the slot numbers, the colors - from `layout.py` | `src/consoleport/gen/profiles.h` |
| `layout.py` | **the single source of truth about the layout**: which button goes into which slot, the cluster order, the face button colors, the `SLOT_CMD_COUNT=24` limit | read by the rest |
| `build.py` | the input maps in the module's slot model (main k, L2 8+k, R2 16+k) | `client-files/input/*.iff` |
| `diamond.py` | the markup: our context page with the hidden glyph template, and the `<include>` and `OnShow`/`OnHide` lines patched into the stock files. There is no crossbar markup at all - the module builds the cells inside the stock toolbar page at runtime | `client-files/ui/*.inc` |
| `glyphs.py` | the glyph atlas out of Kenney Input Prompts. **Strictly 256x256** - other sizes either kill the client or load empty | `client-files/texture/consoleport_glyphs*.dds` |
| `ringtex.py` | the ring textures: the rim, the backdrop, the wedge | `client-files/texture/consoleport_{ring,rim,wedge}.dds` |

## Validators

| Script | What it checks |
|---|---|
| `validate.py` | the input maps: no bindings to commands that do not exist (such a binding kills the client while loading the map), no double bindings inside a layer, the modifiers and the movement in every layer, no axis bindings left over. One check is red and has been from the start: `NUMPAD1` is taken in a stock scheme |
| `validate_ui.py` | the markup we ship: balanced tags, not one stock widget name lost, every style and font out of the stock set, sane coordinates, and every `SourceRect` inside the 256x256 atlas |
| `fixuis.py`, `uisclean.py` | cleaning up and repairing the `.inc` files after generation |

## Taking the binary apart

| Script | What it does |
|---|---|
| `abigen.py` | the search disassembler: RTTI -> vtable, strings -> xref, capstone. This is how most of the addresses were found |
| `dasm.py`, `pe.py`, `rtti.py` | the primitives: disassembling a region, parsing PE, reading RTTI |
| `mdmp.py` | parsing a client minidump: where it died, which modules |
| `hooklines.txt`, `hookmap.txt` | maps of the addresses found, working notes from the search |

## The client's formats

| Script | What |
|---|---|
| `iff.py`, `iffio.py` | reading and writing IFF - the format of the input maps and of most of the client's data |
| `tre.py` | unpacking a `.tre` (all of `stock/` came out of there) |
| `imap.py` | decoding an input map into text and back |
| `cmdtable.py`, `cmdrec2.py`, `cmds.py`, `cmdtypes.py` | the client's command table: which names exist at all. `cmdrec2.py` is the CMD record after the client's own `InputMap_Command.cpp` - `build.py` and `validate.py` both go through it |
| `uicrc.py`, `tags.py`, `tagscan.py`, `names.py` | name CRCs, widget type tags |

## Data

| File | What |
|---|---|
| `ui_props.txt` | 1594 UI property names with their global addresses - the source for `props.h` |
| `consoleport.ds4.imap.txt`, `consoleport.steam.imap.txt` | a text rendering of the two finished input maps: handy to read by eye at review time |
| `FORMAT.md` | the distribution and input map formats in detail |

## The order when the client build changes

1. `python tools/abitable.py` - the new addresses and signatures;
2. `src\consoleport\build.cmd` - the tests check against the exe **from disk**, so
   a mismatch is visible at once;
3. verify in game: on a mismatch `cp::install` refuses to work at all and writes
   the reason into `cp::status().error`.
