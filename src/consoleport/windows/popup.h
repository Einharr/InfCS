// UIManager's context widgets (the popup page): UIPopupMenu is a list of buttons,
// navigation up and down, cross presses, circle closes (Escape); UIRadialMenu is a
// sector by the right stick (Radial.lua: ANGLE_IDX_ONE = 90 degrees, deadzone 0.5),
// cross presses the sector (the digit '1'+i - the radial presses the button itself), circle closes.
#pragma once
#include "../cursor/stack.h"
#include <functional>
#include <vector>

namespace cp { namespace windows {

class Popup : public cursor::Window {
public:
    typedef std::function<NodePtr()> PageFn;              // the context page (contextPage) or 0
    explicit Popup(PageFn fn);
    const char* id() const override { return "popup"; }
    NodePtr page() const override { return fn_(); }
    int priority() const override { return 100; }
    const cursor::ScanRules& rules() const override { return rules_; }
    std::vector<cursor::Hint> hints(const cursor::Context&) const override;
    bool onButton(cursor::Context&, gen::Button, bool pressed) override;
    void onTick(cursor::Context&, float dt) override;
    bool usesStick() const override { return true; }   // it turns the sectors with the stick itself
    void onFocus(cursor::Context&) override;
    // radial: the sector index from the stick (-1 = in the dead zone)
    static int sectorOf(float x, float y, int count, float deadzone = 0.5f);
    // The item comes from where the buttons actually sit round the menu centre: the
    // client places them at its own angles, not by splitting the circle evenly, so even
    // sectors miss.
    static int nearestTo(const std::vector<NodePtr>& items, UIPoint centre,
                         float x, float y, float deadzone = 0.5f);
    NodePtr radial() const;                                // the UIRadialMenu on the page, if any
    // A dropdown's list. UIComboBox::PerformPopup puts a 'TransientComboPopup' page
    // into the context widget with a 'TransientComboList' inside, and focuses the list
    // itself. UIList walks the rows on arrows, so the job is to get the D-pad to it -
    // scanning for widgets is pointless, there is only one and nowhere to go.
    NodePtr comboList() const;
    int hoverSector() const { return sector_; }
private:
    PageFn fn_; cursor::ScanRules rules_; int sector_ = -1;
    int listHeld_ = 0;                                     // -1 up, +1 down, 0 released
    float listRepeat_ = 0;
};

}} // namespace cp::windows
