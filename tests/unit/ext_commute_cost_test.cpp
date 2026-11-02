// ============================================================================
// tests/unit/ext_commute_cost_test.cpp -- fix.commute_cost's not-found answer
// ============================================================================
// src/ext/commute_cost/commute_cost_rule.h without the world: off, a region pair's first query
// answers retail's 0; on, it answers the stored cost exactly as the found arm reads it later
// (a negative stored cost is unreachable).
// ============================================================================
#include "unit.h"
#include "ext/commute_cost/commute_cost_rule.h"

#include <cstdint>
#include <initializer_list>

using namespace Ext::CommuteCost;

namespace {

// RegionPairCost's found arm (0x1400d6213 / CMOVS 0x1400d61dc): the stored cost, negative = unreachable.
int FoundArm(int32_t stored) { return stored < 0 ? kUnreachable : stored; }

}  // namespace

UNIT_TEST(ext_commute_cost_off_answers_retail_zero) {
    CHECK_EQ(MissCost(false, 37), 0);
    CHECK_EQ(MissCost(false, -1), 0);
    CHECK_EQ(MissCost(false, 0), 0);
}

UNIT_TEST(ext_commute_cost_on_answers_what_the_found_arm_answers) {
    for (int32_t stored : {0, 1, 2, 37, 4096, 0x0fffffff, -1, -5}) {
        CHECK_EQ(MissCost(true, stored), FoundArm(stored));
    }
    CHECK_EQ(MissCost(true, 37), 37);
    CHECK_EQ(MissCost(true, -1), kUnreachable);
}
