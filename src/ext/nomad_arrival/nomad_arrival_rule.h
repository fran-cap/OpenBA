// ============================================================================
// src/ext/nomad_arrival/nomad_arrival_rule.h -- fix.nomad_arrival: the pure rule
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §22. Standard C++ only, so
// tests/unit/ext_nomad_arrival_test.cpp links it without the world.
//
// Retail's NomadComponent::TickWalkIn (0x14004f9c0) takes "the action stack is idle" as
// "arrived at the town hall". A nomad whose walk to its meeting point fails (MoveTo::Execute
// returns 2 and the stack clears) is idle where it stands, posts ARRIVED to the hall, and on
// Allow ConvertToCitizen (0x1400501a0) makes it a citizen on that spot: a group stuck behind a
// river becomes citizens behind the river (§19, stranded citizens).
//
// With the row on, an idle nomad counts as arrived only within kNearCells of its meeting
// point. Otherwise the walk is issued once more; if the nomad is idle and still far after
// that, it gives up: it leaves the hall's group and walks back to the map edge it came from,
// as a nomad the town turned away does. A walk that succeeds ends at the meeting point, so a
// game where every walk succeeds plays exactly as retail.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace NomadArrival {

// How close (in cells; one map cell is one world unit) an idle nomad must stand to its meeting
// point to count as arrived. A successful MoveTo ends on the point itself.
constexpr float kNearCells = 4.0f;

enum class Step : uint8_t {
    Arrived,   // retail: post ARRIVED to the hall and loiter
    Retry,     // walk to the meeting point once more
    GiveUp,    // leave the group and walk back to the spawn point
};

// What an idle nomad in the walk-in state does. (dx, dy): its position minus its meeting
// point on the map plane. `retried`: the walk was already issued a second time.
inline Step Decide(bool fixOn, float dx, float dy, bool retried) {
    if (!fixOn) return Step::Arrived;
    if (dx * dx + dy * dy <= kNearCells * kNearCells) return Step::Arrived;
    return retried ? Step::GiveUp : Step::Retry;
}

enum class HallStep : uint8_t {
    None,         // members are still walking in
    AllArrived,   // every remaining member has arrived: the hall moves on to the answer
    Empty,        // nobody is left: the hall goes back to idle
};

// What the hall does once a member gave up and left its group of `remaining` members, of
// which `arrived` have posted ARRIVED.
inline HallStep AfterGiveUp(int32_t arrived, int32_t remaining) {
    if (remaining <= 0) return HallStep::Empty;
    if (arrived >= remaining) return HallStep::AllArrived;
    return HallStep::None;
}

}  // namespace NomadArrival
}  // namespace Ext
