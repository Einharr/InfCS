#include "stack.h"

namespace cp { namespace cursor {

using namespace gen;

bool Stack::update(Context& ctx)
{
    Window* top = nullptr; int bestP = -1;
    for (size_t i = 0; i < windows_.size(); ++i) {
        Window* w = windows_[i].get();
        if (!w->isOpen()) continue;
        int p = w->priority();
        if (p >= bestP) { bestP = p; top = w; }     // on equal priority, the later addition wins
    }
    if (top == focused_) {
        if (focused_) {
            NodePtr pg = focused_->page();
            if (pg && ctx.cursor.root() && !ctx.cursor.root()->sameAs(*pg)) ctx.cursor.setRoot(pg, focused_->rules());
            ctx.hints.set(focused_->hints(ctx));
        }
        return false;
    }
    if (focused_) focused_->onBlur(ctx);
    focused_ = top;
    if (focused_) {
        // The page is taken on its own line: argument evaluation order is unspecified,
        // and the desktop rebuilds its rules inside page() - the other way round the
        // window would get the previous window's rules.
        NodePtr pg = focused_->page();
        ctx.cursor.setRoot(pg, focused_->rules());
        ctx.hints.set(focused_->hints(ctx));
        focused_->onFocus(ctx);
    } else {
        ctx.cursor.clearRoot(); ctx.hints.clear();
    }
    ctx.pad.setUiMode(focused_ != nullptr);
    return true;
}

// Stick deflection -> cursor steps. A stick gives no edge the way the D-pad does, so
// this keeps its own thresholds and repeat: first step at once, the rest after a delay.
// The thresholds differ going in and out, or the cursor jitters at the border.
static int stickDirOf(const input::Stick& s, bool held)
{
    const float on = 0.5f, off = 0.35f, t = held ? off : on;
    const float ax = s.x < 0 ? -s.x : s.x, ay = s.y < 0 ? -s.y : s.y;
    if (ax < t && ay < t) return -1;
    if (ay >= ax) return s.y < 0 ? 0 : 1;          // 0 up, 1 down
    return s.x < 0 ? 2 : 3;                        // 2 left, 3 right
}

void Stack::dispatch(Context& ctx, float dt)
{
    if (!focused_) { stickDir_ = -1; return; }
    input::Pad& p = ctx.pad;
    static const Button dpad[4] = {B_DPAD_UP, B_DPAD_DOWN, B_DPAD_LEFT, B_DPAD_RIGHT};
    static const Dir dirs[4] = {DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT};
    for (int i = 0; i < 4; ++i) {
        if (p.pressed(dpad[i])) { if (!focused_->onButton(ctx, dpad[i], true)) ctx.cursor.input(dirs[i], true); }
        if (p.released(dpad[i])) { if (!focused_->onButton(ctx, dpad[i], false)) ctx.cursor.input(dirs[i], false); }
    }
    // People walk menus with both the D-pad and the sticks, and after a radial - all
    // stick - D-pad only reads as a loss of control. Whichever stick is deflected wins.
    if (!focused_->usesStick()) {
        input::Stick l = p.leftStick(), r = p.rightStick();
        const input::Stick& s = l.len() >= r.len() ? l : r;
        const int d = stickDirOf(s, stickDir_ >= 0);
        if (d < 0) stickDir_ = -1;
        else if (d != stickDir_) { stickDir_ = d; stickWait_ = 0.35f; ctx.cursor.input(dirs[d], true); ctx.cursor.input(dirs[d], false); }
        else if ((stickWait_ -= dt) <= 0) { stickWait_ = 0.12f; ctx.cursor.input(dirs[d], true); ctx.cursor.input(dirs[d], false); }
    } else stickDir_ = -1;
    static const Button rest[] = {B_CROSS, B_CIRCLE, B_SQUARE, B_TRIANGLE, B_L1, B_R1, B_L2, B_R2, B_SHARE, B_OPTIONS, B_L3, B_R3};
    for (Button b : rest) {
        if (p.pressed(b)) focused_->onButton(ctx, b, true);
        if (p.released(b)) focused_->onButton(ctx, b, false);
    }
}

}} // namespace cp::cursor
