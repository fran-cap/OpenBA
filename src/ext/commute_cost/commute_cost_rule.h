// ============================================================================
// src/ext/commute_cost/commute_cost_rule.h -- fix.commute_cost: the pure rule
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §6. Standard C++ only, so
// tests/unit/ext_commute_cost_test.cpp links it without the world.
//
// PathGraph::RegionPairCost (retail 0x1400d6060, src/game/Component/citizen_assign_citizens.cpp)
// answers the employment allocator's commute queries from the region-pair cost tree. When the
// pair is not in the tree it computes the cost (0x140008830), stores it (0x140025a10) and then
// returns R15D, which only the found arm writes (0x1400d6213): the first query of a new pair
// reports 0, a free commute. With the row on, the not-found arm returns the cost it just
// stored, read the way the found arm reads a stored cost (negative = unreachable), so the first
// query of a pair answers what every later query of it answers.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace CommuteCost {

// RegionPairCost's "unreachable" answer (the CMOVS constant at 0x1400d61dc).
constexpr int kUnreachable = 0x1000000;

// The not-found arm's answer for the cost `stored` it just put in the tree. Off: retail's 0.
inline int MissCost(bool fixOn, int32_t stored) {
    if (!fixOn) return 0;
    return stored < 0 ? kUnreachable : stored;
}

}  // namespace CommuteCost
}  // namespace Ext
