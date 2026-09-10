// The facade over the client: the widget tree, plus the synthetic input the client
// already accepts (UIManager::ProcessMessage, CuiIoWin::warpCursor,
// CuiActionManager::performAction). Nothing it does not do itself.
#pragma once
#include "node.h"
#include "../abi/addresses.h"
#include <string>

namespace cp {

// Input sink: the real client (bin::*) or the mock in the tests.
class InputSink {
public:
    virtual ~InputSink() {}
    virtual bool send(UIMessage& m) = 0;                 // UIManager::ProcessMessage
    virtual void warp(int x, int y) = 0;                 // CuiIoWin::warpCursor (cursor + MouseMove)
    virtual bool action(const char* id, const char16_t* params = nullptr) = 0; // CuiActionManager::performAction
    virtual uint16_t key(int which) = 0;                 // UIMessage::* values (see Key)
};

// Order matters: the index goes into InputSink::key and the test mock. Append only.
enum Key { K_Escape, K_Enter, K_Tab, K_Space, K_Up, K_Down, K_Left, K_Right, K_PageUp, K_PageDown,
           K_Home, K_End, K_BackSpace };

class BinaryInput : public InputSink {
public:
    bool send(UIMessage& m) override;
    void warp(int x, int y) override;
    bool action(const char* id, const char16_t* params) override;
    uint16_t key(int which) override;
};

// High-level gestures. All coordinates are screen (world) UI pixels.
class Ui {
public:
    explicit Ui(InputSink& in) : in_(in) {}
    void moveTo(int x, int y);                           // warp + MouseMove
    bool click(int x, int y);                            // LeftMouseDown/Up; true = UIManager took the message
    void doubleClick(int x, int y);                      // Down, DoubleClick, Up - like CuiIoWin
    bool rightClick(int x, int y);                       // RightMouseDown/Up; true = UIManager took it
    bool contextRequest(int x, int y);                   // ContextRequest (radial menu); true = UIManager took it
    void wheel(int x, int y, int delta);                 // MouseWheel
    void keyTap(int which);                              // KeyDown + KeyUp
    void character(char16_t ch);                         // Character (radial digits)
    void dragStart(int x, int y);                        // LeftMouseDown + move: the client decides DragStart itself
    void dragMove(int x, int y);
    void dragDrop(int x, int y);                         // LeftMouseUp on the target
    void escape() { keyTap(K_Escape); }
    bool action(const char* id, const char16_t* params = nullptr) { return in_.action(id, params); }
    InputSink& sink() { return in_; }
private:
    UIMessage msg(int type, int x, int y);
    InputSink& in_;
    int lastX_ = -1, lastY_ = -1;
};

// Tree root and lookup by path (in the client - through UIManager).
NodePtr rootNode();
NodePtr nodeByPath(const char* absolutePath);
NodePtr focusedLeaf();
NodePtr contextPage();                                   // the popup/radial page, if one is open

} // namespace cp
