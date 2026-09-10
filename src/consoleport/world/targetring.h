// The target and group ring. It sits OUTSIDE cursor::Stack, and that is the decision
// worth explaining.
//
// The stack already has a ring (GameBar) and sector selection on the right stick, but
// focus in the stack turns on Pad::uiMode, which zeroes the whole device buffer -
// movement included. Fine for a menu, wrong for a combat ring: the character would
// freeze for the whole selection. So the ring stands alone and suppresses narrowly,
// Pad::suppressCamera() on the right stick only.
//
// The look, agreed 9 Sep 2026 off a mockup:
//   backdrop  a soft dark disc, mutes the 3D world under the labels
//   rim       a thin turquoise ring at the radius - without it this reads as labels in
//             a circle, with it as a radial menu
//   wedge     one sprite rotated by sector/N of a turn (world/RingDecor, shared with
//             the game menu radial)
//   labels    frame plus caption, coloured by hostility as the client colours a name
//             overhead
//   middle    the name of the selected one
//
// Selection happens ON HOVER, by the owner's call (9 Sep 2026): the target is set once
// the stick has rested on a sector for HOVER_MS, and release just closes the ring. The
// delay is not cosmetic - without it dragging across half the ring would send a target
// change per sector passed. Circle restores the target from before it opened.
#pragma once
#include "../core/node.h"
#include "../core/ui.h"
#include "../core/binary.h"
#include "../input/pad.h"
#include "ringdecor.h"
#include <string>
#include <vector>

namespace cp { namespace world {

struct RingItem {
    // An item is either a client action (the group ring: targetSelf, targetGroup0..7)
    // or an object (the target ring). For an object, selecting is one setLookAtTarget -
    // what a left click does.
    const char* action = nullptr;
    void* object = nullptr;
    std::u16string label;      // the caption; LATIN ONLY (the client's fonts)
    bin::game::Threat threat = bin::game::THREAT_NEUTRAL;
    float dist = -1.f;         // meters; < 0 means do not show it (action items)
};

// Us and the group mates: names out of the group's own vector, in targetGroupMember's
// order. Solo leaves one item - targeting yourself still makes sense.
std::vector<RingItem> partyItems();

class TargetRing {
public:
    // overlay: the HUD overlay. frameTemplate: a Page for a label, textTemplate a Text
    // for its caption, imageTemplate an Image off OUR texture - a clone of anything else
    // cannot be given SourceResource at runtime (the crossbar's trap). Without
    // imageTemplate the ring works, minus the backdrop and the wedge.
    bool attach(NodePtr overlay, NodePtr frameTemplate, NodePtr textTemplate, NodePtr imageTemplate = NodePtr());
    void detach();
    bool attached() const { return overlay_ != nullptr; }

    // An empty list still shows a ring: the player has to see that the hold worked and
    // there is nobody around, not that something broke. emptyLabel goes in the middle
    // instead of a name. Set the rim colour BEFORE open - the ring is built at once and
    // repainting means rebuilding the sprites.
    void setRimColor(const char* c) { rimColor_ = c; }
    // title names the ring ("Targets", "Party", "Objects"), written in the middle above
    // the selected name - the labels alone do not say which of the four is open.
    void open(const std::vector<RingItem>& items, const char16_t* title,
              const char16_t* emptyLabel = u"Nothing here");
    bool isEmpty() const { return items_.empty(); }
    void close();
    bool isOpen() const { return open_; }
    int  itemWidth() const { return itemW_; }
    int  radius() const { return radius_; }


    // Every frame while open: sector from the stick, wedge rotation, highlight, and the
    // selection once the delay has passed.
    void update(const input::Pad& pad, float dt, Ui& ui);
    int  sector() const { return sector_; }
    int  appliedSector() const { return applied_; }
    float wedgeTurn() const { return decor_.turn(); }

    // Button released. The selection already happened on hover, this only closes.
    // Returns true if anything was selected while it was up.
    bool apply(Ui& ui);
    // Circle, or release inside the dead zone: put the old target back and close.
    void cancel(Ui& ui);

    // tests
    size_t itemCount() const { return items_.size(); }
    NodePtr itemFrame(size_t i) const { return i < frames_.size() ? frames_[i] : NodePtr(); }
    NodePtr wedge() const { return decor_.wedge(); }

private:
    const char* rimColor_ = nullptr;     // the rim by the ring's context; 0 = the default
    void build();
    void teardown();
    void place();
    void paint();
    bool applyItem(Ui& ui, int index);
    void makeTitle(int cx, int cy);
    void refreshDistances();        // distances change as you move - so do the captions
    void setItemText(size_t i);

    NodePtr overlay_, frameTpl_, textTpl_, imageTpl_, hub_, hubTitle_;
    std::u16string title_;
    RingDecor decor_;
    std::vector<RingItem> items_;
    std::vector<NodePtr> frames_, texts_;
    bool open_ = false;
    int sector_ = -1, painted_ = -2, itemW_ = 68, radius_ = 170;
    float distMs_ = 0.f;            // when the distances were last recomputed
    int applied_ = -1;              // which sector has already been applied - we do not send it twice
    float hoverMs_ = 0.f;           // how long the stick has stood on the current sector
    void* savedTarget_ = nullptr;   // the target from before the ring opened, for the cancel
    bool hadTarget_ = false;
};

}} // namespace cp::world
