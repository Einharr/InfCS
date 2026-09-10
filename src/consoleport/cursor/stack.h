// The window stack (ConsolePort's Stack): which profile windows are visible and which
// has focus. A window appearing turns UI mode on and gives the pad to the cursor;
// closing gives it back to the game. Focus is the last visible one in priority order,
// with context popups and the radial always on top.
#pragma once
#include "cursor.h"
#include "hints.h"
#include "../input/pad.h"
#include <memory>
#include <string>
#include <vector>

namespace cp { namespace cursor {

// What a window gets from the runtime
struct Context {
    Cursor& cursor; Ui& ui; input::Pad& pad; HintBar& hints;
};

class Window {
public:
    virtual ~Window() {}
    virtual const char* id() const = 0;
    virtual NodePtr page() const = 0;                       // the window page (0 = none)
    virtual bool isOpen() const { NodePtr p = page(); return p && p->willDraw(); }
    virtual int priority() const { return 0; }               // higher is closer to focus
    virtual const ScanRules& rules() const = 0;
    virtual std::vector<Hint> hints(const Context&) const = 0;
    virtual void onFocus(Context&) {}
    virtual void onBlur(Context&) {}
    // a pad button (edge, press or release); return true if it was handled
    virtual bool onButton(Context&, gen::Button, bool pressed) = 0;
    virtual void onTick(Context&, float) {}
    // The window reads the stick itself (a radial turns its sectors with it), and then
    // the stack must not turn the same deflection into cursor steps a second
    virtual bool usesStick() const { return false; }
};

class Stack {
public:
    void add(std::unique_ptr<Window> w) { windows_.push_back(std::move(w)); }
    // The tick: recompute visibility and focus; return true if the focus changed
    bool update(Context& ctx);
    Window* focused() const { return focused_; }
    bool uiMode() const { return focused_ != nullptr; }
    // Hand the buttons to the focused window; the D-pad goes to the cursor. dt is for
    // the sticks: a deflection is not an edge and needs its own delay and repeat.
    void dispatch(Context& ctx, float dt);
    size_t size() const { return windows_.size(); }
private:
    std::vector<std::unique_ptr<Window>> windows_;
    Window* focused_ = nullptr;
    int stickDir_ = -1; float stickWait_ = 0;   // where the stick is deflected and how long until the next step
};

}} // namespace cp::cursor
