#include "foes.h"
#include "../core/binary.h"
#include "../core/runtime.h"
#include <algorithm>
#include <cmath>

namespace cp { namespace world {

namespace {

struct Cand {
    void* object;
    std::u16string name;
    float bearing;      // the angle to the right of the view direction, radians
    float dist;         // meters to the player: three identical terminals are otherwise indistinguishable
};

// The angle to a target relative to the player's view: negative left, positive right.
// In the XZ plane (SWG's vertical is Y) - the ring lays out like a screen, not a map.
float bearingOf(const float player[3], const float fwd[3], const float target[3])
{
    const float dx = target[0] - player[0], dz = target[2] - player[2];
    // Right vector in SWG's left-handed system: (fwd.z, -fwd.x).
    const float rx = fwd[2], rz = -fwd[0];
    const float along = dx * fwd[0] + dz * fwd[2];
    const float side = dx * rx + dz * rz;
    return std::atan2(side, along);
}

// What both rings share: names, bearing, keeping the ones nearest the centre of view,
// laying out left to right. They differ only in whom the client handed over.
std::vector<RingItem> build(void* player, void** raw, int n, bool withThreat)
{
    std::vector<RingItem> out;
    float ppos[3], pfwd[3];
    if (!bin::game::positionOf(player, ppos) || !bin::game::forwardOf(player, pfwd)) return out;

    std::vector<Cand> cand;
    cand.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        void* co = bin::game::asClientObject(raw[i]);
        if (!co) continue;
        std::u16string name;
        // The unnamed are skipped: the client writes nothing overhead for them either,
        // and a wedge with no caption cannot be picked meaningfully.
        if (!bin::game::localizedName(co, name) || name.empty()) continue;
        float tpos[3];
        if (!bin::game::positionOf(raw[i], tpos)) continue;
        Cand c; c.object = raw[i]; c.name = name; c.bearing = bearingOf(ppos, pfwd, tpos);
        const float dx = tpos[0] - ppos[0], dy = tpos[1] - ppos[1], dz = tpos[2] - ppos[2];
        c.dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        cand.push_back(c);
    }
    if (cand.empty()) return out;

    // Left to right, as on screen. Over the ceiling we keep the best by both cues at
    // once, bearing and distance. Bearing alone failed: at 64 m a terminal sixty metres
    // straight ahead crowded out one five steps away and slightly to the side (10 Sep
    // 2026, right after the radius went up).
    if (static_cast<int>(cand.size()) > MAX_FOES) {
        float maxDist = 0;
        for (const Cand& c : cand) if (c.dist > maxDist) maxDist = c.dist;
        std::partial_sort(cand.begin(), cand.begin() + MAX_FOES, cand.end(),
                          [maxDist](const Cand& a, const Cand& b) {
                              return ringScore(a.bearing, a.dist, maxDist)
                                   < ringScore(b.bearing, b.dist, maxDist);
                          });
        cand.resize(MAX_FOES);
    }
    std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.bearing < b.bearing; });

    for (const Cand& c : cand) {
        RingItem it;
        it.object = c.object;
        it.label = c.name;
        it.dist = c.dist;
        // Caption colour as the client paints a name overhead: yellow for attackable,
        // red for can-hit-back. A peaceful ring has no hostility, so neutral there.
        if (withThreat) it.threat = bin::game::threatOf(c.object);
        out.push_back(it);
    }
    return out;
}

} // namespace

float ringScore(float bearing, float dist, float maxDist)
{
    const float ang = std::fabs(bearing) / 3.14159265f;          // 0 ahead, 1 behind
    const float far = maxDist > 0 ? dist / maxDist : 0.f;        // 0 point blank, 1 the furthest
    return ang + far;
}


void* nearestTarget(Set set)
{
    void* player = bin::game::playerCreature();
    if (!player) return nullptr;
    float ppos[3];
    if (!bin::game::positionOf(player, ppos)) return nullptr;
    void* raw[64];
    // anythingAround is not "everything": the shared aroundFiltered always throws
    // attackables out, which is right for the rings (LB peaceful, RB targets) and wrong
    // for "nearest of anything" - on 10 Sep 2026 the gesture reported "out of 0 visible"
    // in a field of enemies. So the general set takes both enumerations and picks the
    // nearest of the union.
    const int cap = static_cast<int>(sizeof raw / sizeof raw[0]);
    int n = 0;
    if (set == SET_FOES) n = bin::game::attackableAround(raw, cap);
    else if (set == SET_ALLIES) n = bin::game::peacefulAround(raw, cap, bin::game::targetingRange());
    else {
        n = bin::game::attackableAround(raw, cap);
        if (n < cap) n += bin::game::anythingAround(raw + n, cap - n, bin::game::targetingRange());
    }
    void* best = nullptr; float bestDist = 0;
    for (int i = 0; i < n; ++i) {
        float tpos[3];
        if (!bin::game::positionOf(raw[i], tpos)) continue;
        const float dx = tpos[0] - ppos[0], dy = tpos[1] - ppos[1], dz = tpos[2] - ppos[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!best || d < bestDist) { best = raw[i]; bestDist = d; }
    }
    tracef("nearest target: %p at %.1f m picked out of %d visible", best, best ? bestDist : 0.f, n);
    return best;
}

std::vector<RingItem> foeItems()
{
    void* player = bin::game::playerCreature();
    if (!player) return std::vector<RingItem>();
    void* raw[64];
    const int n = bin::game::attackableAround(raw, static_cast<int>(sizeof raw / sizeof raw[0]));
    if (n <= 0) return std::vector<RingItem>();
    std::vector<RingItem> out = build(player, raw, n, true);
    tracef("target ring: %d visible, %d in the ring", n, static_cast<int>(out.size()));
    return out;
}

std::vector<RingItem> allItems()
{
    void* player = bin::game::playerCreature();
    if (!player) return std::vector<RingItem>();
    void* raw[64];
    const int n = bin::game::anythingAround(raw, static_cast<int>(sizeof raw / sizeof raw[0]),
                                            bin::game::targetingRange());
    if (n <= 0) return std::vector<RingItem>();
    std::vector<RingItem> out = build(player, raw, n, true);
    tracef("everything ring: %d visible, %d in the ring", n, static_cast<int>(out.size()));
    return out;
}

std::vector<RingItem> objectItems()
{
    void* player = bin::game::playerCreature();
    if (!player) return std::vector<RingItem>();
    void* raw[64];
    const int n = bin::game::peacefulAround(raw, static_cast<int>(sizeof raw / sizeof raw[0]), PEACEFUL_RANGE);
    if (n <= 0) return std::vector<RingItem>();
    std::vector<RingItem> out = build(player, raw, n, false);
    tracef("object ring: %d visible within %.0f m, %d in the ring", n, PEACEFUL_RANGE, static_cast<int>(out.size()));
    return out;
}

}} // namespace cp::world
