// Target cycles on the bumpers, rings on holding them. The model is the owner's, set
// 9 Sep 2026:
//   LB / RB         previous / next target over ALL creatures
//   L2 + LB/RB      attackables only
//   R2 + LB/RB      non-attackable non-enemies only
//   hold LB 300 ms  the ring of peaceful things around (R2+LB: the group ring)
//   hold RB         the target ring
//
// The cycles go through CuiActionManager::performAction by string id - the same six the
// client registers in CuiCombatManager::install. They need no bindings in the input map
// and are actively harmful there: a binding would fire second on the same press. Hence
// JOYB 4 and 5 hold nothing in tools/layout.py and the L1 layer is gone - a button
// cannot be a modifier and a hold button at once.
//
// A tap fires on RELEASE, unavoidably: before the threshold a tap and a hold are the
// same thing. On a quick press nobody notices.
#pragma once
#include "../input/pad.h"
#include "../cursor/hints.h"
#include <vector>

namespace cp { namespace world {

// The set the cycle runs over; order matches the action table's rows.
enum Set { SET_ALL = 0, SET_FOES, SET_ALLIES, SET_COUNT };

// The client action for (set, direction): forward = RB = Outward, backward = LB =
// Inward. The strings are in the binary and are verified against the exe.
const char* cycleAction(Set set, bool forward);
// Short label for the bar above the crossbar. Latin-1 only - the stock fonts cover
// nothing else.
const char* setLabel(Set set);
Set setOfMod(input::Mod m);

// Which ring is being asked for - button plus trigger: LB peaceful things, R2+LB the
// group, RB the targets.
enum Ring { RING_NONE = 0, RING_PARTY, RING_FOES, RING_OBJECTS, RING_ALL };
Ring ringKind(bool right, Set set);
// Rim colour per ring: the only cue that says which context you are in without
// reading the labels.
const char* ringColor(Ring r);

// What the state machine wants done this frame. Who does it is the runtime's business -
// the machine never touches the client, which is what makes it testable on a mock.
struct Decision {
    const char* cycle = nullptr;   // run this action (0 = nothing to run)
    Ring openRing = RING_NONE;     // which ring to open
    bool applyRing = false;        // apply the selected sector and close
    bool cancelRing = false;       // close, applying nothing
    bool nearest = false;          // select the nearest target (LB+RB)
};

class Targeting {
public:
    struct Config {
        float hold = 0.30f;        // the hold threshold, seconds
        bool  ringLeft = true;     // rings on LB: peaceful things around, with R2 the group
        bool  ringRight = true;    // the target ring on RB
    };
    void setConfig(const Config& c) { cfg_ = c; }
    const Config& config() const { return cfg_; }

    // active = false: a UI window owns the buttons, so the ring closes applying
    // nothing.
    Decision update(const input::Pad& pad, bool active);

    bool ringOpen() const { return ring_ >= 0; }
    gen::Button ringButton() const { return ring_ == 1 ? gen::B_R1 : gen::B_L1; }
    Ring ringNow() const { return ring_ < 0 ? RING_NONE : ringKind(ring_ == 1, taken_[ring_]); }
    Set set() const { return set_; }              // the set RIGHT NOW, for the bar
    Set setTaken(bool right) const { return taken_[right ? 1 : 0]; }

private:
    bool ringAllowed(int i) const { return i == 0 ? cfg_.ringLeft : cfg_.ringRight; }
    Config cfg_;
    // The set is sampled on the PRESS and held until release: otherwise the order in
    // which a trigger and a bumper come up on one frame would decide whom the cycle
    // runs over. "Hold L2, tap RB" works either way.
    Set  taken_[2] = {SET_ALL, SET_ALL};
    Set  set_ = SET_ALL;
    int  ring_ = -1;               // 0 = the ring is held by LB, 1 = RB, -1 = closed
    bool swallow_[2] = {false, false};   // the release has already been handled (cancelled with circle)
    // LB+RB together is its own gesture, and it was free: with a ring open the second
    // button says nothing, and before the threshold both only accumulate a timer.
    bool bothDone_ = false;
};

// Bumper hints for one side. Two bars, either side of the modifier glyphs under the
// bar - where the crossbar already shows held L2/R2, so triggers and bumpers read
// together. right = false is the left bar (LB).
std::vector<cursor::Hint> modeHints(Set set, bool ringOpen, bool right, bool foeRing = false);

// The legend's second line: what holding this button gives in the current set. Empty
// where there is no ring, so the legend promises nothing that does not exist.
std::vector<cursor::Hint> holdHints(Set set, bool right);

}} // namespace cp::world
