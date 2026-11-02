// ============================================================================
// tests/unit/ext_unresolved_seeds_test.cpp -- fix.unresolved_seeds
// ============================================================================
// src/ext/unresolved_seeds/unresolved_seeds.h on its own: the load seam drops the null entries
// 0x1400049c0 stored for resources that no longer load, keeping the order of the rest (the panels'
// combo indexes the list by position). The registry row is checked in ext_registry_test.cpp.
// docs/retail_bugs.md section 21
// ============================================================================
#include "unit.h"
#include "ext/unresolved_seeds/unresolved_seeds.h"

#include <cstdint>

namespace {
struct Desc {
    int id;
};
}  // namespace

UNIT_TEST(ext_unresolved_seeds_drop_nulls) {
    Desc a{1};
    Desc b{2};
    Desc c{3};

    // Nothing to drop: unchanged.
    Desc* full[3] = {&a, &b, &c};
    CHECK_EQ(Ext::UnresolvedSeeds::DropNulls(full, 3), 3);
    CHECK(full[0] == &a && full[1] == &b && full[2] == &c);

    // Nulls at the front, middle and end go; the order of the rest is kept.
    Desc* mixed[6] = {nullptr, &a, nullptr, &b, &c, nullptr};
    CHECK_EQ(Ext::UnresolvedSeeds::DropNulls(mixed, 6), 3);
    CHECK(mixed[0] == &a && mixed[1] == &b && mixed[2] == &c);

    // All null, and empty.
    Desc* none[2] = {nullptr, nullptr};
    CHECK_EQ(Ext::UnresolvedSeeds::DropNulls(none, 2), 0);
    CHECK_EQ(Ext::UnresolvedSeeds::DropNulls(none, 0), 0);
}
