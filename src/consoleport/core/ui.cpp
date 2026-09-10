#include "ui.h"
#include "binary.h"

namespace cp {

bool BinaryInput::send(UIMessage& m) { return bin::processMessage(m); }
void BinaryInput::warp(int x, int y) { bin::warpCursor(x, y); }
bool BinaryInput::action(const char* id, const char16_t* p) { return bin::performAction(id, reinterpret_cast<const uint16_t*>(p)); }
uint16_t BinaryInput::key(int which)
{
    switch (which) {
    case K_Escape: return bin::keyEscape();   case K_Enter: return bin::keyEnter();
    case K_Tab: return bin::keyTab();         case K_Space: return bin::keySpace();
    case K_Up: return bin::keyUp();           case K_Down: return bin::keyDown();
    case K_Left: return bin::keyLeft();       case K_Right: return bin::keyRight();
    case K_PageUp: return bin::keyPageUp();   case K_PageDown: return bin::keyPageDown();
    case K_Home: return bin::keyHome();       case K_End: return bin::keyEnd();
    case K_BackSpace: return bin::keyBackSpace();
    }
    return 0;
}

UIMessage Ui::msg(int type, int x, int y)
{
    UIMessage m; m.type = type; m.mouseX = x; m.mouseY = y; return m;
}

// UIManager drops a MouseMove to the same coordinates (UIManager.cpp:754), so
// before a gesture the cursor is first moved away if it is already there.
void Ui::moveTo(int x, int y)
{
    if (lastX_ == x && lastY_ == y) in_.warp(x + 1, y);
    in_.warp(x, y); lastX_ = x; lastY_ = y;
}

bool Ui::click(int x, int y)
{
    moveTo(x, y);
    UIMessage d = msg(abi::MSG_LeftMouseDown, x, y); const bool okD = in_.send(d);
    UIMessage u = msg(abi::MSG_LeftMouseUp, x, y);   const bool okU = in_.send(u);
    return okD || okU;
}

// CuiIoWin.cpp:1083-1105 - the caller synthesizes the double click itself.
void Ui::doubleClick(int x, int y)
{
    moveTo(x, y);
    UIMessage d = msg(abi::MSG_LeftMouseDown, x, y); in_.send(d);
    UIMessage dd = msg(abi::MSG_LeftMouseDoubleClick, x, y); in_.send(dd);
    UIMessage u = msg(abi::MSG_LeftMouseUp, x, y); in_.send(u);
}

bool Ui::rightClick(int x, int y)
{
    moveTo(x, y);
    UIMessage d = msg(abi::MSG_RightMouseDown, x, y); const bool okD = in_.send(d);
    UIMessage u = msg(abi::MSG_RightMouseUp, x, y);   const bool okU = in_.send(u);
    return okD || okU;
}

bool Ui::contextRequest(int x, int y)
{
    moveTo(x, y);
    UIMessage c = msg(abi::MSG_ContextRequest, x, y); return in_.send(c);
}

void Ui::wheel(int x, int y, int delta)
{
    moveTo(x, y);
    UIMessage w = msg(abi::MSG_MouseWheel, x, y); w.data = static_cast<uint16_t>(delta); in_.send(w);
}

void Ui::keyTap(int which)
{
    UIMessage d = msg(abi::MSG_KeyDown, lastX_, lastY_); d.keystroke = in_.key(which); in_.send(d);
    UIMessage u = msg(abi::MSG_KeyUp, lastX_, lastY_);   u.keystroke = in_.key(which); in_.send(u);
}

void Ui::character(char16_t ch)
{
    UIMessage c = msg(abi::MSG_Character, lastX_, lastY_); c.keystroke = static_cast<uint16_t>(ch); in_.send(c);
}

void Ui::dragStart(int x, int y)
{
    moveTo(x, y);
    UIMessage d = msg(abi::MSG_LeftMouseDown, x, y); in_.send(d);
    // UIManager's drag threshold is a few pixels of movement with the button held
    UIMessage m = msg(abi::MSG_MouseMove, x + 6, y + 6); in_.send(m); lastX_ = x + 6; lastY_ = y + 6;
}

void Ui::dragMove(int x, int y)
{
    UIMessage m = msg(abi::MSG_MouseMove, x, y); in_.send(m); lastX_ = x; lastY_ = y;
}

void Ui::dragDrop(int x, int y)
{
    dragMove(x, y);
    UIMessage u = msg(abi::MSG_LeftMouseUp, x, y); in_.send(u);
}

NodePtr rootNode()                    { return BinaryNode::wrap(bin::rootPage()); }
NodePtr nodeByPath(const char* path)  { return BinaryNode::wrap(bin::objectFromPath(path, T_BaseObject)); }
NodePtr focusedLeaf()                 { return BinaryNode::wrap(bin::focusedLeaf()); }
NodePtr contextPage()                 { return BinaryNode::wrap(bin::contextPage()); }

} // namespace cp
