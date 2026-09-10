// The command browser window profile (SwgCuiCommandBrowser, the
// /GroundHUD.CommandBrowser page - the name was measured in game 9 Sep 2026 by walking the desktop).
//
// There is no dragging from a pad, so a command reaches the bar another way:
//   square  BIND - the window waits for a button or a combo, and the button plus
//           whatever triggers are held becomes a "set + slot" pair through the same
//           CLUSTERS table the crossbar uses (no triggers = main, L2, R2, both).
//   circle  cancel the binding; outside that mode, close the window.
#pragma once
#include "../cursor/stack.h"
#include "../crossbar/crossbar.h"
#include <string>
#include <vector>

namespace cp { namespace windows {

class Commands : public cursor::Window {
public:
    Commands(NodePtr root, crossbar::Crossbar* bar, const char* path = "GroundHUD.CommandBrowser");
    const char* id() const override { return "commands"; }
    NodePtr page() const override;
    int priority() const override { return 10; }
    const cursor::ScanRules& rules() const override { return rules_; }
    std::vector<cursor::Hint> hints(const cursor::Context&) const override;
    bool onButton(cursor::Context&, gen::Button, bool pressed) override;
    void onBlur(cursor::Context&) override;
    bool binding() const { return binding_; }
    // Button plus held triggers -> set and slot. false if it is not one of the ten
    // cluster buttons: combos are built on those only.
    static bool resolve(gen::Button b, bool l2, bool r2, int& pane, int& slot);
    // What the binding prompt shows. The runtime centres it - only the runtime knows
    // the HUD size.
    static std::vector<cursor::Hint> promptHints();
private:
    NodePtr root_; crossbar::Crossbar* bar_; std::string path_;
    cursor::ScanRules rules_; bool binding_ = false;
};

}} // namespace cp::windows
