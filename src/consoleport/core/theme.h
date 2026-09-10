// Colours of the client's active theme.
//
// The client paints by NAME: a widget carries PalColor='line1' and
// UIPalette::ApplyPalette expands it into a real colour on load and on every theme
// change. The palette itself is a UIPalette object in the tree, but its entries are
// properties with arbitrary names ('back1', 'header'), and properties are addressed by
// global strings that do not exist in the binary for those names. Nothing can read the
// palette directly.
//
// So we read its output instead: one pass over the tree collecting "entry name -> the
// colour the client has already drawn a widget of that name with". All of it the
// client's own data, the player's own theme, no new ABI.
//
// Why: our widgets are clones of the client's, and a clone brings the template's colour.
// Pal* alone is not enough - the client would only apply it at the next theme change -
// so we set both, Pal* for the link and Color/TextColor for right now.
#pragma once
#include "node.h"

namespace cp { namespace theme {

// Walk the tree and remember the colours. Once on attach; called again it rebuilds the
// cache, e.g. after a theme change in the options.
void scan(const NodePtr& root);

// "#RRGGBB" for a palette entry, never empty: a name missing from the tree falls back
// to the default palette (stock/ui/ui_styles.inc), an unknown one to white.
const char* color(const char* palName);

// How many entries came out of the client - for the debug window.
int known();

// Where to paint: the same entry-to-property pairs the client registers in
// UIPaletteRegistrySetup::install.
enum Role { ROLE_COLOR = 0, ROLE_BG, ROLE_TEXT };

// Paint a widget: the entry name goes into Pal* so the widget follows the theme, and is
// expanded into Color/BackgroundTint/TextColor at the same time - otherwise the clone
// keeps the template's colour until the next theme change, i.e. probably forever.
//
// Given '#RRGGBB' instead of a name, the colour is set as is and Pal* is CLEARED, or the
// palette would repaint it on its next pass. CuiIconManager does the same, dropping
// PalBgTint before its own highlight.
void paint(const NodePtr& n, Role role, const char* palNameOrHex);

}} // namespace cp::theme
