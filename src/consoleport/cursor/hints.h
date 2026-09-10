// The hint bar (ConsolePort's HintBar): "button glyph + text" for the active window,
// built from Image/Text clones in the HUD overlay.
//
// Latin-1 only - the client's stock fonts cover nothing else, and the markup has the
// same limit.
//
// The bar rides with the window: moveTo() shifts what is already built instead of
// rebuilding, so it can move every frame.
#pragma once
#include "../core/node.h"
#include "../gen/profiles.h"
#include <string>
#include <vector>

namespace cp { namespace cursor {

// glyph = -1 lets Glyph::forButton pick the picture from the button; anything else is a
// direct atlas key, for hints that are not about a button - a radial sector is picked by
// DEFLECTING the stick, so the glyph is STICK_R, not R3.
struct Hint { gen::Button button; std::u16string text; int glyph = -1; };

// "[dpad][left stick][right stick] Navigate" - people use both sticks, so the legend
// says both. Empty text draws the glyph alone.
void navigateHints(std::vector<Hint>& out);

class HintBar {
public:
    void attach(NodePtr overlay, NodePtr imageTemplate, NodePtr textTemplate, int x, int y);
    // Two bars can be on the HUD at once - window hints and the mode bar - and their
    // clone names have to differ: the client collapses same-named children.
    void setPrefix(const char* p) { prefix_ = p; }
    void detach();
    void set(const std::vector<Hint>& hints);
    void clear() { set(std::vector<Hint>()); }
    void tick(float dt);
    void moveTo(int x, int y);          // shift a finished bar under the window
    void raise();                       // raise our widgets above their siblings under the parent
    // A backdrop turns the bar from a hint line into a small window. The binding prompt
    // needs it: "press a button" mid-screen with no frame reads as a stray caption.
    // 0 = no backdrop, an ordinary legend.
    void setBackdrop(NodePtr pageTemplate, int pad = 14);
    const std::vector<Hint>& hints() const { return hints_; }
    bool visible() const { return !hints_.empty(); }
    int width() const { return width_; }
    // Width without the trailing margin - the left targeting bar aligns by its right
    // edge against the modifier glyphs.
    int contentWidth() const { return width_ > pad_ ? width_ - pad_ : width_; }
    int glyphSize() const { return glyph_; }
private:
    struct Item { NodePtr glyph, text; };
    void rebuild();
    NodePtr overlay_, imageTpl_, textTpl_, backdropTpl_, backdrop_;
    int backdropPad_ = 0;
    std::vector<Hint> hints_; std::vector<Item> items_;
    int x_ = 0, y_ = 0, width_ = 0;
    int glyph_ = 20, gap_ = 6, charW_ = 7, pad_ = 14;
    const char* prefix_ = "cpHint";
};

}} // namespace cp::cursor
