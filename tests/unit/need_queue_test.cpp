// ============================================================================
// tests/unit/need_queue_test.cpp -- the employment allocator's need priority queue
// ============================================================================
// src/game/Component/need_queue.h is the ordering retail's AVL set gives the queue in
// PathGraph::AssignCitizens (0x1400d43b0): the highest score pops first, and on equal
// scores the LOWEST need index (the need record's address in its one array). A re-score
// erases the exact {old score, need} key. std::multimap, which the port used before,
// popped the most recently inserted of equal keys instead.
// ============================================================================
#include "unit.h"
#include "Component/need_queue.h"

#include <cstddef>
#include <vector>

namespace {

std::vector<std::size_t> PopAll(NeedQueue::Queue q) {
    std::vector<std::size_t> order;
    while (!q.empty()) {
        order.push_back(q.begin()->need);
        q.erase(q.begin());
    }
    return order;
}

}  // namespace

UNIT_TEST(need_queue_highest_score_first) {
    NeedQueue::Queue q;
    q.insert(NeedQueue::Key{ 10, 0 });
    q.insert(NeedQueue::Key{ 30, 1 });
    q.insert(NeedQueue::Key{ -5, 2 });
    q.insert(NeedQueue::Key{ 20, 3 });
    const std::vector<std::size_t> want = { 1, 3, 0, 2 };
    CHECK(PopAll(q) == want);
}

UNIT_TEST(need_queue_ties_pop_lowest_index) {
    // Inserted in index order, as Phase 4 does: equal scores come out lowest index
    // first (a multimap's prev(end()) gave 3, 2, 1).
    NeedQueue::Queue q;
    q.insert(NeedQueue::Key{ 7, 1 });
    q.insert(NeedQueue::Key{ 7, 2 });
    q.insert(NeedQueue::Key{ 7, 3 });
    q.insert(NeedQueue::Key{ 9, 4 });
    const std::vector<std::size_t> want = { 4, 1, 2, 3 };
    CHECK(PopAll(q) == want);
}

UNIT_TEST(need_queue_reinsert_keeps_index_order) {
    // A re-scored need goes back under its index, not behind the others of its score.
    NeedQueue::Queue q;
    q.insert(NeedQueue::Key{ 5, 0 });
    q.insert(NeedQueue::Key{ 5, 1 });
    q.insert(NeedQueue::Key{ 8, 2 });
    CHECK(q.erase(NeedQueue::Key{ 8, 2 }) == 1);   // exact-key erase (0x1400d5c50)
    q.insert(NeedQueue::Key{ 5, 2 });
    CHECK(q.erase(NeedQueue::Key{ 5, 0 }) == 1);
    q.insert(NeedQueue::Key{ 5, 0 });
    const std::vector<std::size_t> want = { 0, 1, 2 };
    CHECK(PopAll(q) == want);
}

UNIT_TEST(need_queue_exact_match_not_duplicated) {
    NeedQueue::Queue q;
    q.insert(NeedQueue::Key{ 4, 6 });
    q.insert(NeedQueue::Key{ 4, 6 });
    CHECK(q.size() == 1);
    CHECK(q.erase(NeedQueue::Key{ 3, 6 }) == 0);   // a different score is a different key
    CHECK(q.size() == 1);
}
