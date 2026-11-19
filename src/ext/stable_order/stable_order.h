#pragma once
// ============================================================================
// src/ext/stable_order/stable_order.h -- fix.stable_order (fix tier, default ON)
// ============================================================================
// NOT RETAIL (docs/retail_bugs.md §36, docs/extensions/handins/STABLEORDER.md).
// Retail's EntityList::Update (0x1400c4090) ticks every entity of its update tree
// (+0x68, a Set<InternalPtr<ComponentEntity>> whose key is the entity pointer,
// compared unsigned: 0x1400c38e0) in key order, so the order in which citizens
// tick, and so which of them receives which draw of the world Random, follows
// where the allocator placed each entity. Two runs of one seed, or two loads of one
// save, are two towns from the first tick.
//
// With the row on the walk visits the same entities in the order of their EntityList
// id (ComponentEntity::_entityId, +0x18):
//   * EntityList::RegisterEntity (0x1400c48a0) stamps it with the master array's
//     count before the append; RemoveEntity (0x1400c38e0) moves the last entity into
//     the hole and re-stamps it. So `masterArray[e->_entityId] == e` always holds, the
//     ids are dense in [0, master count), unique at any moment, and set only by the
//     sequence of registrations and removals, which the sim decides (with the row on,
//     the removal list is filled in id order: STABLEORDER.md §2).
//   * an id CHANGES during an entity's life (the swap on a removal), so the order is
//     recomputed whenever one moves, not only when the tree changes.
//   * EntityList::Serialize (0x1400c44a0) writes every entity's id and reads it back:
//     a load restores the ids, and so the order.
//
// The tree itself is untouched (retail's key, retail's AVL): the walk's flat
// snapshot (TREE_SNAPSHOT, entitylist.cpp) stays in pointer order, so WALKCACHE's
// and PERFXTBL's pointer lookups keep working, and the row adds a permutation of
// the snapshot's indices sorted by id (BuildIdOrder below). The walk visits
// snapshot[order[j]].
//
// Header-only and world-free: tests/unit/ext_stable_order_test.cpp drives it with
// plain structs (any type with an int32_t `_entityId`).
// ============================================================================
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Ext {
namespace StableOrder {

// The permutation of snapshot indices that visits `snap[0..n)` by ascending
// `_entityId`. `idLimit` is the master array's count: every registered entity's id
// lies in [0, idLimit). `slot` is scratch the caller keeps between calls (one int32
// per id). Ids are unique, so a counting placement is exact: O(n + idLimit), no
// compare sort.
//
// An entity whose id is outside [0, idLimit), or a second entity on an id already
// taken (neither happens while the master array's invariant holds), is not dropped:
// it goes after the placed ones, in snapshot (pointer) order. The return value is
// how many took that fallback (0 in a sound list).
template <class Ent>
inline size_t BuildIdOrder(Ent* const* snap, size_t n, int32_t idLimit, std::vector<uint32_t>& order,
                           std::vector<int32_t>& slot) {
    order.clear();
    order.reserve(n);
    const size_t limit = idLimit > 0 ? static_cast<size_t>(idLimit) : 0;
    slot.assign(limit, -1);
    size_t misplaced = 0;
    for (size_t i = 0; i < n; ++i) {
        if (i + 8 < n) __builtin_prefetch(&snap[i + 8]->_entityId);
        const int32_t id = snap[i]->_entityId;
        if (id >= 0 && static_cast<size_t>(id) < limit && slot[static_cast<size_t>(id)] < 0) {
            slot[static_cast<size_t>(id)] = static_cast<int32_t>(i);
        } else {
            ++misplaced;
        }
    }
    for (size_t id = 0; id < limit; ++id) {
        const int32_t i = slot[id];
        if (i >= 0) order.push_back(static_cast<uint32_t>(i));
    }
    if (misplaced != 0) {
        // The fallback: the snapshot entries the placement did not take, in snapshot order.
        std::vector<uint8_t> placed(n, 0);
        for (uint32_t i : order) placed[i] = 1;
        for (size_t i = 0; i < n; ++i) {
            if (!placed[i]) order.push_back(static_cast<uint32_t>(i));
        }
    }
    return misplaced;
}

// MoveTo::Enter (0x1400c8970): a mover walks at the follow speed only behind a nearby
// character "after" it, and retail's "after" is a higher ADDRESS (`mover < other`,
// unsigned). With the row on, "after" is a higher entity id; equal ids (never two live
// entities) fall back to the address. `moverId` / `otherId` are the two characters'
// entity ids, read by the seam only with the row on.
template <class T>
inline bool FollowsAfter(const T* mover, int32_t moverId, const T* other, int32_t otherId) {
    if (moverId != otherId) return moverId < otherId;
    return mover < other;
}

// CitizenAI's leisure state (0x14010a560) gathers the reachable buildings around a citizen
// into a scratch Set<ComponentEntity*> (the "home box", keyed by pointer) and takes the
// Rand(count)-th of its in-order walk: the same draw names a different building in two
// runs. With the row on the seam takes the `pick`-th key in (entity id, pointer) order
// instead. `Node` is a Collection::Set node (element, left, right); `Ent` has `_entityId`.
// Returns nullptr when the set holds `pick` keys or fewer.
template <class Node>
inline auto NthById(const Node* root, uint32_t pick) -> decltype(root->element) {
    using Ent = decltype(root->element);
    std::vector<Ent> keys;
    std::vector<const Node*> stack;
    for (const Node* n = root; n != nullptr || !stack.empty();) {
        while (n != nullptr) {
            stack.push_back(n);
            n = n->left;
        }
        n = stack.back();
        stack.pop_back();
        keys.push_back(n->element);
        n = n->right;
    }
    if (pick >= keys.size()) return nullptr;
    std::sort(keys.begin(), keys.end(), [](Ent a, Ent b) {
        if (a->_entityId != b->_entityId) return a->_entityId < b->_entityId;
        return a < b;
    });
    return keys[pick];
}

}  // namespace StableOrder
}  // namespace Ext
