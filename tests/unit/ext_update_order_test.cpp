// ============================================================================
// tests/unit/ext_update_order_test.cpp -- fix.update_order's id permutation
// ============================================================================
// src/ext/update_order/update_order.h without the world. The walk snapshot is in
// pointer order (the update tree's key); BuildIdOrder gives the visit order by
// EntityList id. Proved here: the visit order is the id order whatever the address
// layout; a snapshot holding only some ids is ordered by them; an entity with no
// usable id still ticks (last, in snapshot order); and the ids themselves, kept the
// way EntityList keeps them (RegisterEntity appends, RemoveEntity swaps the last into
// the hole), come out the same for one sequence of adds and removes at any addresses.
// ============================================================================
#include "unit.h"
#include "ext/update_order/update_order.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace {

using Ext::UpdateOrder::BuildIdOrder;

struct Ent {
    int32_t _entityId;
};

// The snapshot as the tree walk builds it: the entities in ascending pointer order.
std::vector<Ent*> PointerSorted(std::vector<Ent*> v) {
    std::sort(v.begin(), v.end(), std::less<Ent*>());
    return v;
}

// The ids in visit order.
std::vector<int32_t> VisitIds(const std::vector<Ent*>& snap, const std::vector<uint32_t>& order) {
    std::vector<int32_t> ids;
    for (uint32_t i : order) ids.push_back(snap[i]->_entityId);
    return ids;
}

// EntityList's id keeping (RegisterEntity 0x1400c48a0 / RemoveEntity 0x1400c38e0): the
// master array, the id = the slot, a removal swaps the last entity into the hole.
struct Master {
    std::vector<Ent*> slots;
    void Register(Ent* e) {
        e->_entityId = static_cast<int32_t>(slots.size());
        slots.push_back(e);
    }
    void Remove(Ent* e) {
        const int32_t last = static_cast<int32_t>(slots.size()) - 1;
        const int32_t idx = e->_entityId;
        if (idx != last) {
            slots[static_cast<size_t>(idx)] = slots[static_cast<size_t>(last)];
            slots[static_cast<size_t>(idx)]->_entityId = idx;
        }
        slots.pop_back();
    }
};

}  // namespace

UNIT_TEST(ext_update_order_visits_by_id_at_any_layout) {
    // The same five entities at two address layouts: the snapshots differ, the visit does not.
    Ent ascending[5] = { {0}, {1}, {2}, {3}, {4} };
    Ent scrambled[5] = { {3}, {0}, {4}, {2}, {1} };
    std::vector<int32_t> visits[2];
    Ent* layouts[2] = { ascending, scrambled };
    for (int l = 0; l < 2; ++l) {
        std::vector<Ent*> all;
        for (int k = 0; k < 5; ++k) all.push_back(&layouts[l][k]);
        const std::vector<Ent*> snap = PointerSorted(all);
        std::vector<uint32_t> order;
        std::vector<int32_t> slot;
        CHECK_EQ(BuildIdOrder(snap.data(), snap.size(), 5, order, slot), (size_t)0);
        CHECK_EQ(order.size(), snap.size());
        visits[l] = VisitIds(snap, order);
    }
    const std::vector<int32_t> expect = { 0, 1, 2, 3, 4 };
    CHECK(visits[0] == expect);
    CHECK(visits[1] == expect);
}

UNIT_TEST(ext_update_order_subset_of_ids) {
    // The update tree holds only the ticking entities: ids with gaps, out of a master of 10.
    Ent e[4] = { {7}, {2}, {9}, {4} };
    const std::vector<Ent*> snap = PointerSorted({ &e[0], &e[1], &e[2], &e[3] });
    std::vector<uint32_t> order;
    std::vector<int32_t> slot;
    CHECK_EQ(BuildIdOrder(snap.data(), snap.size(), 10, order, slot), (size_t)0);
    const std::vector<int32_t> expect = { 2, 4, 7, 9 };
    CHECK(VisitIds(snap, order) == expect);
    // Every snapshot index exactly once: a permutation.
    std::vector<uint32_t> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    for (size_t i = 0; i < sorted.size(); ++i) CHECK_EQ(sorted[i], (uint32_t)i);
}

UNIT_TEST(ext_update_order_unusable_ids_tick_last) {
    // -1 (never registered), past the master count, and a duplicate: none is dropped.
    Ent e[5] = { {3}, {-1}, {1}, {12}, {3} };
    const std::vector<Ent*> snap = PointerSorted({ &e[0], &e[1], &e[2], &e[3], &e[4] });
    std::vector<uint32_t> order;
    std::vector<int32_t> slot;
    CHECK_EQ(BuildIdOrder(snap.data(), snap.size(), 5, order, slot), (size_t)3);
    CHECK_EQ(order.size(), snap.size());
    // The placed ones first, by id; then the rest in snapshot (pointer) order.
    CHECK_EQ(snap[order[0]]->_entityId, 1);
    CHECK_EQ(snap[order[1]]->_entityId, 3);
    for (size_t j = 3; j < order.size(); ++j) CHECK(order[j - 1] < order[j]);
    std::vector<uint32_t> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    for (size_t i = 0; i < sorted.size(); ++i) CHECK_EQ(sorted[i], (uint32_t)i);
}

UNIT_TEST(ext_update_order_empty_and_reuse) {
    std::vector<uint32_t> order = { 9, 9 };
    std::vector<int32_t> slot = { 5 };
    CHECK_EQ(BuildIdOrder<Ent>(nullptr, 0, 0, order, slot), (size_t)0);
    CHECK(order.empty());
    // The scratch from a larger call does not leak into a smaller one.
    Ent big[3] = { {2}, {0}, {1} };
    const std::vector<Ent*> s1 = PointerSorted({ &big[0], &big[1], &big[2] });
    BuildIdOrder(s1.data(), s1.size(), 3, order, slot);
    Ent small[1] = { {0} };
    Ent* s2[1] = { &small[0] };
    CHECK_EQ(BuildIdOrder(s2, 1, 1, order, slot), (size_t)0);
    CHECK_EQ(order.size(), (size_t)1);
    CHECK_EQ(order[0], (uint32_t)0);
}

UNIT_TEST(ext_update_order_ids_follow_the_add_remove_sequence) {
    // One sequence of registrations and removals at two address layouts gives every
    // entity the same id, and so the same visit order, though the ids move on removal.
    constexpr int kN = 8;
    std::vector<std::vector<int32_t>> runs;
    for (int layout = 0; layout < 2; ++layout) {
        std::vector<Ent> storage(kN);
        // layout 0: entity k at storage[k]; layout 1: at storage[kN - 1 - k] (the reverse address order).
        auto ent = [&](int k) { return &storage[static_cast<size_t>(layout == 0 ? k : kN - 1 - k)]; };
        Master m;
        for (int k = 0; k < 6; ++k) m.Register(ent(k));
        m.Remove(ent(1));     // entity 5 takes id 1
        m.Register(ent(6));   // id 5
        m.Remove(ent(5));     // entity 6 moves from id 5 to id 1
        m.Register(ent(7));   // id 5
        std::vector<int32_t> byEntity;
        for (int k : { 0, 2, 3, 4, 6, 7 }) byEntity.push_back(ent(k)->_entityId);
        runs.push_back(byEntity);
        // The visit order of the live ones is the master array's order.
        std::vector<Ent*> live;
        for (int k : { 0, 2, 3, 4, 6, 7 }) live.push_back(ent(k));
        const std::vector<Ent*> snap = PointerSorted(live);
        std::vector<uint32_t> order;
        std::vector<int32_t> slot;
        BuildIdOrder(snap.data(), snap.size(), static_cast<int32_t>(m.slots.size()), order, slot);
        for (size_t j = 0; j < order.size(); ++j) CHECK(snap[order[j]] == m.slots[j]);
    }
    CHECK(runs[0] == runs[1]);
    const std::vector<int32_t> expect = { 0, 2, 3, 4, 1, 5 };
    CHECK(runs[0] == expect);
}
