# References: where to look for what

The two repositories all the work rests on are **not copied** into the package -
together they take 1.5 GB and both are public. This is only the map: where to
look.

## 1. The SWG client sources

`hackerlank/SWG_Client_Next_Main` - the SWG Masters version for VS2013. The class
layout there matches the Infinity binary, so it is better to check the UI against
the sources than to reconstruct it with a disassembler.

Everything below is relative to `src/`.

**The interface library** (the one that draws the `.inc` files):
`external/3rd/library/ui/`. NOTE: the files in `include/` are one-line stubs, the
real headers and the implementation live in `src/win32/` (168 files) - look there.

- `UIBaseObject.h`, `UIWidget.h`, `UIPage.h`, `UIVolumePage.h`, `UIButton.h`,
  `UIImage.h`, `UIText.h`, `UIComposite.h`, `UIManager.h`
- the property system: `UIPropertyDescriptor.h`, `UIPropertyRegister.h`,
  `UILowerString.h`; vtable slots 11 `SetProperty` / 13 `GetProperty`
- events and reactivity: `UIEventCallback.h`, `UINotification.h`, `UIWatcher.h`,
  `UIActionListener.h`, the effectors `UI*Effector.h`
- the markup scripting language: `UIScriptEngine.h`, `UIRunner.h`
- the `.inc` loader: `UILoader.h`, `UIStandardLoader.h`, `UITemplate.h`
- the rest: `src/shared/{core,property,loader,image,table,boundary}/`

**The client's mediator core**: `engine/client/library/clientUserInterface/`

- `CuiMediator.h/.cpp` - the base of every window, `getCodeDataObject` (0xA68210
  in the binary)
- `CuiManager`, `CuiWorkspace`, `CuiLayer*` (the canvas, the cursor), `CuiIoWin`
  (input into the UI), `CuiActionManager`, `CuiInputNames`,
  `CuiRadialMenuManager`, `CuiMediatorFactory*`

**The game windows**:
`game/client/library/swgClientUserInterface/src/shared/page/`

- `SwgCuiToolbar.h/.cpp` - the toolbar and its panes
- `SwgCuiHud*`, `SwgCuiHudGround`, `SwgCuiHudWindowManagerGround` - the ground HUD
- `SwgCuiDebugInfoPage` - a template for a window with live data
- `SwgCuiMediatorTypes.h`, `SwgCuiMediatorFactorySetup` - the window registry

**Input**: `engine/client/library/clientDirectInput/src/win32/DirectInput.cpp`
(the joystick dispatcher, `submitEvent` = 0x0041F700 in the binary),
`engine/shared/library/sharedInputMap/` (`InputMap.cpp`, `InputMap_Command.cpp` -
the CMD format and the button/axis route), `engine/shared/library/sharedIoWin/`
(`IoWinManager` - the event queue),
`engine/client/library/clientGame/src/shared/scene/GroundScene`,
`.../controller/PlayerCreatureController`.

An important caveat: **the mediator factory lies about the paths of live
windows.** In `SwgCuiMediatorFactorySetup.cpp` the inventory is listed as `/Inv`,
while the live page in game is `GroundHUD.Inventory`. The factory records a
template, not the path of an open window. Walking the tree once and printing the
paths of the visible pages costs less than reading the sources.

The list of files that came up over the course of the work is in
[`../docs/notes/client-src-files-wanted.txt`](../docs/notes/client-src-files-wanted.txt).

## 2. ConsolePort for WoW

`seblindfors/ConsolePort` (master, alive) - the main design reference. It is a Lua
addon, but the architecture ports across one to one: the pad model -> the mapper
-> the input controller -> widgets that react to state. In the root lies
`CLAUDE.md` - the author's own instructions about the code, to be read first. The
wiki is in `Wiki/`.

| Directory | Why we care |
|---------|-----------|
| `ConsolePort/Model/Gamepad/` | pad descriptions (`PlayStation/PlayStation5.lua`, `Xbox`, `Steam/Deck`), `Mapper.lua` - how a physical button becomes an action, `Atlas.lua` - the glyphs |
| `ConsolePort/Controller/` | `Input.lua`, `Bindings.lua`, `Pager.lua` (switching sets by holding - our crossbar), `Movement.lua`, `Radial.lua`, `Targeting.lua`, `Securenav.lua` |
| `ConsolePort/View/Cursors/` | the interface cursor under a pad: `Arrow`, `Crosshair`, `Mime`, `Blocker` |
| `ConsolePort/Widget/` | `Button.lua`, `IndexButton`, `PieMenu`, `HintBar`, `Pools.lua` |
| `ConsolePort/Utils/` | `Animation.lua`, `UIFade.lua`, `Spline.lua`, `Color.lua` - the reactivity comes from here |
| `ConsolePort_Bar/` | `Widget/Cluster/Cluster.lua` + `Skins.lua` - the prototype of our diamond; `Widget/Page/Page.lua` - sets by modifier; `Model/Presets.lua` |
| `ConsolePort_Cursor/` | `View/Cursor.lua`, `Controller/Nudge.lua`, `Stack.lua`, `Scroll.lua` - the model for navigating windows |
| `ConsolePort_Rings/` | `View/Ring/Ring.lua` - the model for the radial menus |
| `ConsolePort_Menu/`, `ConsolePort_World/` | the game menu and the world windows under a pad |
| `ConsolePort_Keyboard/` | the on-screen keyboard from a pad |
| `ConsolePort_Config/` | the settings window, `LoadoutSelector` |

What was taken directly: the cluster geometry (`Const.Cluster.Layout` - main 44,
flyout 32, offsets 9/14), the logic of sets by modifier from `Pager.lua`, the
radial's angles and dead zone from `Radial.lua` (sector 0 at the top, clockwise,
zone 0.5), the hint bar from `HintBar`.

## 3. Other

- **The glyph atlas**: Kenney Input Prompts 1.5, CC0 - the source is in
  [`../assets/`](../assets/), built by `tools/glyphs.py`.
- **`gamecontrollerdb.txt`** (SDL): not in the package. It was used while working
  out the pad layouts and is not needed to run anything here - the numbering the
  module relies on is recorded in `tools/layout.py`.
