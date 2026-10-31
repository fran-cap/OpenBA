#pragma once
// ============================================================================
// src/ext/alloc_tiebreak/tie_key.h -- fix.alloc_tiebreak: the scratch-tree key
// ============================================================================
// NOT RETAIL (docs/retail_bugs.md §14, docs/extensions/handins/ALLOCTIEBREAK.md).
// The employment allocator (PathGraph::AssignCitizens 0x1400d43b0) and the housing
// re-balancer (PathGraph::ReassignWorkers 0x1400d62a0) build trees keyed by raw
// pointers (CitizenComponent*, Profession*, ResidenceComponent*), so which of two
// equal candidates wins follows heap addresses. A TieKey puts a stable id in front of
// the pointer: with the row off the id is 0 for every key and the order is the
// pointer's, retail's; with it on the id decides, and the pointer only orders two keys
// with the same id (never two live entities, alloc_tiebreak.h).
//
// Header-only and world-free: tests/unit/ext_alloc_tiebreak_test.cpp includes it alone.
// ============================================================================
#include <cstdint>

namespace Ext {
namespace AllocTiebreak {

template <typename T>
struct TieKey {
    int32_t tie;   // 0 with the row off; the stable id with it on
    T*      ptr;

    bool operator<(const TieKey& o) const {
        if (tie != o.tie) return tie < o.tie;
        return ptr < o.ptr;
    }
    bool operator==(const TieKey& o) const { return tie == o.tie && ptr == o.ptr; }
};

}  // namespace AllocTiebreak
}  // namespace Ext
