// src/game/Component/need_queue.h -- the need priority queue of the employment
// allocator (PathGraph::AssignCitizens, retail 0x1400d43b0, citizen_assign_citizens.cpp).
//
// Retail's queue (built from 0x1400d5501) is an AVL set of {need record*, score} nodes.
// The descent takes the +0x18 child when `node.score < score || (node.score == score &&
// need < node.need)`, an exact match is not inserted twice, and the pop (0x1400d5831)
// walks +0x18 to the end: the HIGHEST score, and on equal scores the LOWEST record
// address. The records live in one array in Phase 2 order, so that is the lowest need
// index. A re-score (0x1400d5c50) erases exactly the {old score, need} key and inserts
// the new one. Header-only and free of game types so tests/unit/need_queue_test.cpp
// checks the same ordering the allocator runs.
#pragma once
#include <cstddef>
#include <set>

namespace NeedQueue {

struct Key {
    int         score;
    std::size_t need;   // index into the Phase 2 need array (the record's address order)
};

// begin() is retail's pop.
struct Order {
    bool operator()(const Key& a, const Key& b) const {
        if (a.score != b.score) return a.score > b.score;
        return a.need < b.need;
    }
};

using Queue = std::set<Key, Order>;

}  // namespace NeedQueue
