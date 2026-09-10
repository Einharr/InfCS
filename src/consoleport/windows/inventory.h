// The inventory window profile (SwgCuiInventory, the /GroundHUD.Inventory page):
//   D-pad - across the item grid and the buttons;  cross - use (a double click on
//   a cell = performDefaultAction) / press a button;  square - the item radial menu
//   (ContextRequest);  triangle - pick up / put down (a move);
//   circle - cancel a move or close (inventoryClose);  L1 - "up" through the
//   container;  R1 - toggle the icon/detail view;  R3 - examine.
#pragma once
#include "../cursor/stack.h"

namespace cp { namespace windows {

class Inventory : public cursor::Window {
public:
    // Measured in game on 8 Sep 2026 by walking the tree: the inventory page hangs off
    // GroundHUD, not the root. The mediator factory's "/Inv" is a template - the live
    // window is called something else.
    explicit Inventory(NodePtr root, const char* path = "GroundHUD.Inventory");
    const char* id() const override { return "inventory"; }
    NodePtr page() const override;
    int priority() const override { return 10; }
    const cursor::ScanRules& rules() const override { return rules_; }
    std::vector<cursor::Hint> hints(const cursor::Context&) const override;
    bool onButton(cursor::Context&, gen::Button, bool pressed) override;
    void onBlur(cursor::Context& c) override;
private:
    bool clickNamed(cursor::Context& c, const char* relPath);
    NodePtr root_; std::string path_; cursor::ScanRules rules_;
};

}} // namespace cp::windows
