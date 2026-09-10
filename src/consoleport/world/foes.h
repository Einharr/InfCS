// The lists behind the rings: attackable targets (RB) and peaceful things around
// (R2+LB). Two conditions set by the owner on 9 Sep 2026, and how they are met:
//
//   1) NO EXTRA NETWORK FOOTPRINT. Building a list asks the server nothing - the client
//      already holds the objects and the names. The only message is the selection
//      itself: one setLookAtTarget per press, the same as a left click.
//
//   2) NO ADVANTAGE OVER THE STOCK LAYOUT. The set is exactly the Tab cycle's: the same
//      ClientWorld::findObjectsInRange over the targeting radius, the same
//      targetIsAttackable predicate (frustum, corpses, untargettable). Not one object
//      wider. The label is the string the client draws overhead. The order is left to
//      right across the screen - no sorting by health, threat or distance, because that
//      is data the player cannot see. And a ceiling of MAX_FOES wedges: a ring points
//      at a target, it is not a survey of the battlefield.
#pragma once
#include "targetring.h"
#include "targeting.h"
#include <vector>

namespace cp { namespace world {

// Twelve, like the hours on a clock: about as many sectors as a thumb can tell apart.
// The radius is computed separately (TargetRing::place) or the labels would touch.
static const int MAX_FOES = 12;

// Ring items out of what the client already sees. Empty with no player, no targets or
// no ABI; the mock build returns whatever the test fed it.
std::vector<RingItem> foeItems();

// Peaceful things: terminals, vendors, containers, dropped items. Own radius - the
// client's targeting range is about 128 m and there is no point dragging all of that
// into a ring. Raised from 25 to 64 m on 10 Sep 2026 by request: at 25 half the
// terminals in a city were out of reach. The cost is that more candidates turn up than
// there are wedges, and the extras are cut by bearing - what stays is the closest to
// the centre of view, so in a crowd the ring shows what you are looking at.
static const float PEACEFUL_RANGE = 64.f;
std::vector<RingItem> objectItems();

// How good a candidate is, lower is better. Both cues normalised to 0..1 so they are
// comparable: bearing |b|/pi (0 straight ahead, 1 behind) and dist/maxDist (0 point
// blank, 1 the furthest candidate). Distance is normalised against the furthest one in
// THIS call, not a constant - the object ring has its own radius and the target ring the
// client's. maxDist <= 0 leaves bearing alone.
float ringScore(float bearing, float dist, float maxDist);

// Everything around, attackable and peaceful alike. L2 + LB.
std::vector<RingItem> allItems();

// The nearest target, by distance rather than by cycle - the client's cycles go round
// a ring about the player and never pick "nearest". The distance is computed for the
// rings anyway, so the minimum is free. Same sets as everywhere else: LT enemies, RT
// peaceful, no trigger everything. 0 means nobody around.
void* nearestTarget(Set set);


}} // namespace cp::world
