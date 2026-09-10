#include "targeting.h"
#include "../crossbar/crossbar.h"

namespace cp { namespace world {

using namespace gen;

const char* ringColor(Ring r)
{
    // Palette entry names, not shades of our own, so the rim follows the theme. The
    // ones taken are the client's own markers of meaning: bad, good, plain, attention.
    switch (r) {
    case RING_FOES:    return "IconNegative";   // hostile - the same the client paints minuses with
    case RING_PARTY:   return "IconPositive";   // friendlies
    case RING_OBJECTS: return "icondefault";    // neutral things around: an ordinary icon color
    default:           return "contrast1";      // "anything at all" - an accent, so as not to confuse it with the three above
    }
}

const char* cycleAction(Set set, bool forward)
{
    // The client's own breakdown (CuiCombatManager.cpp, 9 Sep 2026):
    //   cycleTargets{Outward,Inward}         targetIsAttackable
    //   ...All                               targetIsCreature, any creature
    //   ...Friendly                          a creature, not attackable, not an enemy
    // Outward runs away from the current target, Inward towards it. Non-creatures -
    // terminals and the like - are in none of the cycles: all three filters want a
    // CreatureObject. Those can only be taken with the cursor (targetAtCursor).
    static const char* const TAB[SET_COUNT][2] = {
        {"cycleTargetsInwardAll",      "cycleTargetsOutwardAll"},
        {"cycleTargetInward",          "cycleTargetOutward"},
        {"cycleTargetsInwardFriendly", "cycleTargetsOutwardFriendly"},
    };
    if (set < 0 || set >= SET_COUNT) set = SET_ALL;
    return TAB[set][forward ? 1 : 0];
}

const char* setLabel(Set set)
{
    switch (set) {
    case SET_FOES:   return "FOE";
    case SET_ALLIES: return "ALLY";
    default:         return "ALL";
    }
}

Set setOfMod(input::Mod m)
{
    switch (m) {
    case input::MOD_L2: return SET_FOES;
    case input::MOD_R2: return SET_ALLIES;
    // The crossbar gives M3 a pane of its own; for targeting it counts as no modifier,
    // and the client has no separate set for it anyway.
    default:            return SET_ALL;
    }
}

Ring ringKind(bool right, Set set)
{
    // The layout as of 9 Sep 2026:
    //   LB       peaceful things - terminals, vendors, containers. Wanted most often,
    //            so no trigger.
    //   R2 + LB  the group; R2 means friendlies in the cycles too.
    //   RB       the things people hit, in any set.
    //   L2 + LB  everything at once.
    if (right) return RING_FOES;
    if (set == SET_ALLIES) return RING_PARTY;
    if (set == SET_ALL) return RING_OBJECTS;
    // L2 + LB is everything at once. Of limited use, but it was the free pair.
    return RING_ALL;
}

Decision Targeting::update(const input::Pad& pad, bool active)
{
    Decision d;
    if (!active) {
        if (ring_ >= 0) { d.cancelRing = true; ring_ = -1; }
        swallow_[0] = swallow_[1] = false;
        return d;
    }

    set_ = setOfMod(input::activeMod(pad));

    // Circle closes the ring without applying, and the later release of the holding
    // button must not apply the sector after the fact.
    if (ring_ >= 0 && pad.pressed(B_CIRCLE)) {
        swallow_[ring_] = true; ring_ = -1; d.cancelRing = true;
    }

    // LB+RB is the nearest target. The client has no "nearest" action, only cycles, so
    // we do it ourselves - the distances are computed for the rings anyway.
    const bool bothDown = pad.down(B_L1) && pad.down(B_R1);
    if (!bothDown) bothDone_ = false;
    else if (ring_ < 0 && !bothDone_) {
        bothDone_ = true;
        swallow_[0] = swallow_[1] = true;      // the release must not also produce a cycle
        d.nearest = true;                      // honestly by distance, not by a cycle
        return d;
    }

    for (int i = 0; i < 2; ++i) {
        const Button b = i == 1 ? B_R1 : B_L1;

        if (pad.pressed(b)) { taken_[i] = set_; swallow_[i] = false; }

        // A ring opens on its own button only, and only if the other one has not
        // already taken it.
        const Ring kind = ringKind(i == 1, taken_[i]);
        // No ring while both are held - that is a different gesture.
        if (ring_ < 0 && !swallow_[i] && !bothDown && ringAllowed(i) && kind != RING_NONE
            && pad.down(b) && pad.heldSeconds(b) >= cfg_.hold) {
            ring_ = i; d.openRing = kind;
        }

        if (pad.released(b)) {
            if (swallow_[i]) { swallow_[i] = false; }
            else if (ring_ == i) { ring_ = -1; d.applyRing = true; }
            else if (ring_ < 0) { d.cycle = cycleAction(taken_[i], i == 1); }
            // The other button is holding a ring, so this one stays silent: the
            // selection matters more than an accidental cycle.
        }
    }
    return d;
}

std::vector<cursor::Hint> holdHints(Set set, bool right)
{
    std::vector<cursor::Hint> h;
    const char16_t* what = nullptr;
    switch (ringKind(right, set)) {
    case RING_PARTY:   what = u"Hold Party"; break;
    case RING_FOES:    what = u"Hold Targets"; break;
    case RING_OBJECTS: what = u"Hold Objects"; break;
    case RING_ALL:     what = u"Hold All"; break;
    default: return h;
    }
    h.push_back(cursor::Hint{right ? B_R1 : B_L1, what,
                             right ? crossbar::Glyph::SH_R : crossbar::Glyph::SH_L});
    return h;
}

std::vector<cursor::Hint> modeHints(Set set, bool ringOpen, bool right, bool foeRing)
{
    std::vector<cursor::Hint> h;
    if (ringOpen) {
        // Ring hints: select on the left, cancel on the right, on the bumpers' own
        // sides so the eye does not hunt for them.
        if (right) h.push_back(cursor::Hint{B_CIRCLE, u"Cancel"});
        else       h.push_back(cursor::Hint{B_R3, foeRing ? u"Target" : u"Party", crossbar::Glyph::STICK_R});
        return h;
    }
    // "Prev FOE" / "Next ALL": the set is in the caption, so no separate mode line is
    // needed. The bumper glyphs are in the atlas (SH_L / SH_R).
    std::u16string t = right ? u"Next " : u"Prev ";
    for (const char* p = setLabel(set); *p; ++p) t.push_back(static_cast<char16_t>(*p));
    h.push_back(cursor::Hint{right ? B_R1 : B_L1, t,
                             right ? crossbar::Glyph::SH_R : crossbar::Glyph::SH_L});
    return h;
}

}} // namespace cp::world
