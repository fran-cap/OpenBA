// src/game/Work/work_leaf_market.cpp
// Wave (Game-gap / Work-leaf) — Market/Trade leaf helpers.
//
// This is an exe sub-helper called by name from
#include "Work/exe_bodies.h"
// src/game/Work/marketwork.cpp (declared in Work/MarketWork.h). This file provides its
// faithful 1:1 reimplementation.
//
// SCOPE: this file contains ONLY the recursive tree cleanup helper 0x14002af40 (the
// sibling 0x140014b00 is Avl38FreeSubtree, Player/exe_bodies.h). Its callees are:
//   - recursive self-calls (self-contained)
//   - System::MemoryAllocator::MemFree (a Runtime export).
//
// Bodies are unconditional so this compiles as-is in the standalone exe.

#include "system/MemoryAllocator.h"
#include "collection/Collection.h"
#include "Work/MarketWork.h"   // MarketWork::ClaimNode
#include <cstdint>

// ============================================================
// 0x14002af40 — recursive market-tree node cleanup (frees node + its data buffer)
// ============================================================
// __fastcall FUN_14002af40(allocCtx param_1 [RCX], void* node [RDX], ...)
// Post-order recursion: free left child (node+0x20), free right child (node+0x28),
// then MemFree the data buffer at node+0x08, zero node+0x08 and node+0x10, and
// finally MemFree the node itself.  param_1 (the alloc context in RCX) is threaded
// through the recursion but is unused: the frees call System::MemoryAllocator::MemFree,
// which the original calls via dispatch slot [0x140152b68].
// Verified offset-by-offset against disassembly @0x14002af40.
// The node is MarketWork's claim-tree node (Collection::Set<MarketWork::Claim>::Node, 0x38:
// the citizen, then the claim's carry list {data, count, capacity}).
void WorkLeaf_MarketTreeCleanup_2af40(MarketWork::ClaimNode* const* ctx, MarketWork::ClaimNode* node) {
    (void)ctx;
    if (node == nullptr) return;
    MarketWork::ClaimNode* n = node;
    WorkLeaf_MarketTreeCleanup_2af40(ctx, n->left);  // left
    WorkLeaf_MarketTreeCleanup_2af40(ctx, n->right); // right
    System::MemoryAllocator::MemFree(n->element.carries.data); // data buffer
    n->element.carries.data = nullptr;
    n->element.carries.count = 0;      // retail: one qword store over count and capacity
    n->element.carries.capacity = 0;
    System::MemoryAllocator::MemFree(node); // node
}
