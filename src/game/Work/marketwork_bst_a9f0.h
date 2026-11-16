// src/game/Work/marketwork_bst_a9f0.h
// MarketWork AVL/BST insert cluster — 0x14002a9f0 -> 0x14002ab60 -> 0x14002ae50.
//
// Declarations only; see marketwork_bst_a9f0.cpp for the offset-by-offset
// verification notes against the Application-x64.exe disassembly.
//
// Node layout (0x38 bytes, MarketWork's extended BST node):
//   +0x00 key        InternalBasePtr (8 bytes)
//   +0x08 data.ptr   payload dynamic array {ptr@+0x08, count@+0x10(int), cap@+0x14(int)}
//   +0x10 data.count/cap
//   +0x18 parent
//   +0x20 left
//   +0x28 right
//   +0x30 balance    int32 (AVL balance factor / height — zeroed on create)
//
// Tree header layout (as used by 0x14002ab60):
//   +0x00 root       node*
//   +0x08 count      int32
#pragma once

#include <cstdint>
#include "Work/MarketWork.h"   // MarketWork::Claim / ClaimNode / CarryList

extern "C" {

// 0x14002ae50 — allocate + initialise one 0x38-byte market BST node.
//   unused [RCX]  : spilled to its home slot and never read (see .cpp).
//   src    [RDX]  : {key@+0x00, payloadArray@+0x08} source pair.
//   parent [R8]   : node stored at +0x18.
// Returns the new node, or nullptr if MemAlloc failed.
MarketWork::ClaimNode* MarketBst_ae50(MarketWork::ClaimTree* unusedTree, MarketWork::Claim* src, MarketWork::ClaimNode* parent);

// 0x14002ab60 — ordered insert-if-absent into the market BST, then AVL retrace.
//   tree [RCX] : tree header {root@+0x00, count@+0x08(int32)}
//   out  [RDX] : out-param; receives the node (existing or newly created)
//   src  [R8]  : {key@+0x00, payloadArray@+0x08}; *src is the unsigned sort key
// Returns `out`.
MarketWork::ClaimNode** MarketBst_ab60(MarketWork::ClaimTree* tree, MarketWork::ClaimNode** out, MarketWork::Claim* src);

// 0x14002a9f0 — insert (key, cloned payload array) into the market BST.
//   tree [RCX] : tree header
//   out  [RDX] : out-param forwarded to MarketBst_ab60
//   key  [R8]  : pointer to the 8-byte InternalBasePtr key to insert
//   vec  [R9]  : pointer to the payload dynamic array to deep-copy
// Returns `out`.
MarketWork::ClaimNode** MarketBst_a9f0(MarketWork::ClaimTree* tree, MarketWork::ClaimNode** out, CitizenComponent** key, MarketWork::CarryList* vec);

} // extern "C"
