// The UI cursor, after ConsolePort_Cursor: window nodes, D-pad navigation with repeat
// on hold, gestures on the current node through synthetic input, and a highlight cloned
// from markup templates.
#pragma once
#include "navigate.h"
#include "../core/ui.h"
#include "../core/tick.h"

namespace cp { namespace cursor {

struct CursorConfig {
    float repeatFirst = 0.125f;   // UIholdRepeatDelayFirst
    float repeat = 0.125f;        // UIholdRepeatDelay
    float travel = 4.f;           // UItravelTime: the fraction of the path per frame is 1/travel at 60 fps
    float rescanEvery = 0.2f;     // automatic node re-collection (the window was redrawn)
    int pointerSize = 22;         // UIpointerSize
    int pointerOffset = -2;       // UIpointerOffset
    bool wrap = true;
};

class Cursor {
public:
    explicit Cursor(Ui& ui);
    void setConfig(const CursorConfig& c) { cfg_ = c; }
    const CursorConfig& config() const { return cfg_; }

    // the window and the rules
    void setRoot(NodePtr root, const ScanRules& rules);
    void clearRoot();
    NodePtr root() const { return root_; }
    void rescan();
    bool active() const { return root_ != nullptr; }

    // navigation
    bool navigate(Dir d);
    void input(Dir d, bool down);                 // hold with repeat
    void tick(float dt);
    const NodeInfo* current() const { return cur_ >= 0 && cur_ < static_cast<int>(nodes_.size()) ? &nodes_[cur_] : nullptr; }
    int currentIndex() const { return cur_; }
    size_t nodeCount() const { return nodes_.size(); }
    const std::vector<NodeInfo>& nodes() const { return nodes_; }
    bool setCurrent(int idx);
    bool setCurrentNode(const NodePtr& n);

    // gestures on the current node (the coordinates are the node center)
    bool click();                                        // true = UIManager took the click
    bool activate();                                     // press for real: Press on a button, otherwise a click
    void doubleClick();
    bool context();          // ContextRequest (the radial); true = UIManager took the message
    bool rightClick();       // true = UIManager took it
    void scroll(int delta);
    bool dragStart();        // cross held / triangle: start dragging the current node
    bool dragging() const { return dragging_; }
    void dragDrop();         // release on the current node
    void dragCancel();

    // visuals: the overlay parent (the HUD) plus an Image (arrow) and a Page (frame) template
    void attachVisuals(NodePtr overlay, NodePtr imageTemplate, NodePtr pageTemplate);
    void detachVisuals();
    UIPoint pointerPos() const { UIPoint p = {static_cast<int>(px_), static_cast<int>(py_)}; return p; }
    NodePtr pointerNode() const { return pointer_; }
    NodePtr focusNode() const { return focus_; }
    // Force the frame back to the top: it only rises when the node changes, and the
    // client can raise a window later - the game menu radial touches the panel page and
    // lands above it.
    void raiseRing() { ringOver_.reset(); }

private:
    void moveMouseToCurrent();
    void updateVisuals(float dt);

    Ui& ui_; CursorConfig cfg_;
    NodePtr root_; ScanRules rules_;
    std::vector<NodeInfo> nodes_;
    int cur_ = -1; NodePtr curNode_, oldNode_;
    bool held_ = false; Dir heldDir_ = DIR_UP; float repeatTimer_ = 0;
    float rescanTimer_ = 0;
    bool dragging_ = false; NodePtr dragSource_;
    NodePtr overlay_, pointer_, focus_;
    // The frame and the window are both children of the overlay, and the client raises
    // the window on open, so the frame has to be raised after it or it ends up
    // underneath. We move
    NodePtr ringOver_;
    // Four bright bars along the edges rather than a styled frame: the client's own
    // backdrop styles give a soft FILL, and on a green inventory cell that cannot be
    // told from the stock item highlight
    NodePtr ringBar_[4];
    int ringThick_ = 4;
    float px_ = 0, py_ = 0; bool pointerInit_ = false;
};

}} // namespace cp::cursor
