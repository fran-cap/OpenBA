// ============================================================================
// tests/unit/ext_alloc_tiebreak_test.cpp -- fix.alloc_tiebreak's scratch-tree key
// ============================================================================
// src/ext/alloc_tiebreak/tie_key.h without the world: with every id 0 (the row
// off) a tree of TieKeys walks in pointer order, retail's; with ids it walks in id
// order whatever the addresses, and two keys with one id stay distinct.
// ============================================================================
#include "unit.h"
#include "ext/alloc_tiebreak/tie_key.h"

#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace {

using Ext::AllocTiebreak::TieKey;

struct Thing {
    int32_t id;
};

// The ids of a set's keys, in walk order.
std::vector<int32_t> WalkIds(const std::set<TieKey<Thing>>& tree) {
    std::vector<int32_t> ids;
    for (const TieKey<Thing>& k : tree) ids.push_back(k.ptr->id);
    return ids;
}

}  // namespace

UNIT_TEST(ext_alloc_tiebreak_off_is_pointer_order) {
    Thing things[4] = { {3}, {1}, {0}, {2} };
    std::set<TieKey<Thing>> tree;
    for (Thing& t : things) tree.insert(TieKey<Thing>{ 0, &t });
    std::set<Thing*> pointers;
    for (Thing& t : things) pointers.insert(&t);
    CHECK_EQ(tree.size(), pointers.size());
    auto p = pointers.begin();
    for (const TieKey<Thing>& k : tree) {
        CHECK(k.ptr == *p);
        ++p;
    }
}

UNIT_TEST(ext_alloc_tiebreak_on_ignores_addresses) {
    // The same four entities at two different address layouts walk in one order.
    Thing ascending[4] = { {0}, {1}, {2}, {3} };
    Thing descending[4] = { {3}, {2}, {1}, {0} };
    std::set<TieKey<Thing>> a;
    std::set<TieKey<Thing>> b;
    for (Thing& t : ascending) a.insert(TieKey<Thing>{ t.id, &t });
    for (Thing& t : descending) b.insert(TieKey<Thing>{ t.id, &t });
    const std::vector<int32_t> expect = { 0, 1, 2, 3 };
    CHECK(WalkIds(a) == expect);
    CHECK(WalkIds(b) == expect);
}

UNIT_TEST(ext_alloc_tiebreak_equal_ids_stay_distinct) {
    // An unregistered entity's id is -1: two of them are two keys, ordered by pointer.
    Thing things[2] = { {-1}, {-1} };
    std::set<TieKey<Thing>> tree;
    tree.insert(TieKey<Thing>{ -1, &things[0] });
    tree.insert(TieKey<Thing>{ -1, &things[1] });
    CHECK_EQ(tree.size(), static_cast<size_t>(2));
    CHECK(tree.begin()->ptr == (&things[0] < &things[1] ? &things[0] : &things[1]));
}

UNIT_TEST(ext_alloc_tiebreak_key_is_a_strict_order) {
    Thing x{ 5 };
    Thing y{ 5 };
    const TieKey<Thing> kx{ 5, &x };
    const TieKey<Thing> ky{ 5, &y };
    const TieKey<Thing> kz{ 4, &y };
    CHECK(!(kx < kx));
    CHECK((kx < ky) != (ky < kx));
    CHECK(kz < kx);
    CHECK(kz < ky);
    CHECK(kx == (TieKey<Thing>{ 5, &x }));
    CHECK(!(kx == ky));
}

UNIT_TEST(ext_alloc_tiebreak_map_begin_is_lowest_rank) {
    // Phase 5B takes the profession map's begin(): with ranks, the lowest list index.
    Thing late{ 0 };
    Thing early{ 0 };
    std::map<TieKey<Thing>, int> counts;
    counts[TieKey<Thing>{ 7, &late }] = 2;
    counts[TieKey<Thing>{ 1, &early }] = 1;
    CHECK(counts.begin()->first.ptr == &early);
    CHECK_EQ(counts.begin()->second, 1);
}
