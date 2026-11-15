#pragma once
// ============================================================================
// src/game/Action/PathAvl.h — ENDGAME B.4 W1 block ACT-a
//
// The exe's AVL tree primitives over a NODE TYPE, bodies in action_pf_avl.cpp.
// Retail compiled one copy per element width (MSVC monomorphised
// Collection::Set's node code), with the link offsets baked in:
//
//   | node        | element | parent | left | right | balance | height | bodies                                  |
//   |-------------|---------|--------|------|-------|---------|--------|-----------------------------------------|
//   | 0x30 (wide) | 0x10    | +0x10  | +0x18| +0x20 | +0x28   | +0x2A  | 0x14000e450 L, 0x14000e580 R, 0x140013480 |
//   | 0x28        | 0x08    | +0x08  | +0x10| +0x18 | +0x20   | +0x22  | 0x140014c70 L, 0x140014da0 R, 0x14001a8c0 |
//
// The two retail copies are the same algorithm; here each is ONE template over
// the node type (B.4 B13), instantiated in action_pf_avl.cpp for every node the
// tree uses. A node type names `parent`, `left`, `right`, `balance`, `height`.
//
// Height convention (both widths): a child contributes `child->height + 1`, a
// missing child 0; height = max(left, right), balance = right - left.
// ============================================================================

#include <cstdint>
#include <cstddef>

// Rotate `node`'s right child up into its place (0x14000e450 / 0x140014c70).
// Returns the promoted node.
template <class Node> Node* AvlRotateLeft(Node** root, Node* node);
// Rotate `node`'s left child up into its place (0x14000e580 / 0x140014da0).
template <class Node> Node* AvlRotateRight(Node** root, Node* node);
// Walk from *cursor to the root recomputing balance / height and rotating
// (0x140013480 / 0x14001a8c0). *cursor ends null.
template <class Node> void  AvlRebalance(Node** root, Node** cursor);

// The generic nodes of the other blocks' trees (their element is read by no body
// here except the inserts', whose keys are ints).
struct AvlNode30 {                     // 0x30: a 0x10-byte element
    int32_t    key0;                   // +0x00
    int32_t    key1;                   // +0x04
    int32_t    key2;                   // +0x08 payload (the inserts copy it, never compare it)
    uint8_t    _pad0C[4];              // +0x0C
    AvlNode30* parent;                 // +0x10
    AvlNode30* left;                   // +0x18
    AvlNode30* right;                  // +0x20
    int16_t    balance;                // +0x28
    int16_t    height;                 // +0x2A
    uint8_t    _pad2C[4];              // +0x2C
};
static_assert(sizeof(AvlNode30) == 0x30, "AvlNode30 is 0x30");
static_assert(offsetof(AvlNode30, _pad0C) == 0xC, "AvlNode30::_pad0C");
static_assert(offsetof(AvlNode30, _pad2C) == 0x2C, "AvlNode30::_pad2C");
static_assert(offsetof(AvlNode30, key0) == 0x00, "AvlNode30::key0 @ +0x00");
static_assert(offsetof(AvlNode30, key1) == 0x04, "AvlNode30::key1 @ +0x04");
static_assert(offsetof(AvlNode30, key2) == 0x08, "AvlNode30::key2 @ +0x08");
static_assert(offsetof(AvlNode30, parent) == 0x10, "AvlNode30::parent @ +0x10");
static_assert(offsetof(AvlNode30, left) == 0x18, "AvlNode30::left @ +0x18");
static_assert(offsetof(AvlNode30, right) == 0x20, "AvlNode30::right @ +0x20");
static_assert(offsetof(AvlNode30, balance) == 0x28, "AvlNode30::balance @ +0x28");
static_assert(offsetof(AvlNode30, height) == 0x2A, "AvlNode30::height @ +0x2A");

struct AvlNode28 {                     // 0x28: an 8-byte element
    int32_t    key;                    // +0x00
    uint8_t    _pad04[4];              // +0x04
    AvlNode28* parent;                 // +0x08
    AvlNode28* left;                   // +0x10
    AvlNode28* right;                  // +0x18
    int16_t    balance;                // +0x20
    int16_t    height;                 // +0x22
    uint8_t    _pad24[4];              // +0x24
};
static_assert(sizeof(AvlNode28) == 0x28, "AvlNode28 is 0x28");
static_assert(offsetof(AvlNode28, _pad04) == 0x4, "AvlNode28::_pad04");
static_assert(offsetof(AvlNode28, _pad24) == 0x24, "AvlNode28::_pad24");
static_assert(offsetof(AvlNode28, key) == 0x00, "AvlNode28::key @ +0x00");
static_assert(offsetof(AvlNode28, parent) == 0x08, "AvlNode28::parent @ +0x08");
static_assert(offsetof(AvlNode28, left) == 0x10, "AvlNode28::left @ +0x10");
static_assert(offsetof(AvlNode28, right) == 0x18, "AvlNode28::right @ +0x18");
static_assert(offsetof(AvlNode28, balance) == 0x20, "AvlNode28::balance @ +0x20");
static_assert(offsetof(AvlNode28, height) == 0x22, "AvlNode28::height @ +0x22");

// A Collection::Set header as the inserts see it: root, then an int count.
template <class Node>
struct AvlTreeHead {
    Node*   root;                      // +0x00
    int32_t count;                     // +0x08
    uint8_t _pad0C[4];                 // +0x0C
};
static_assert(sizeof(AvlTreeHead<AvlNode30>) == 0x10, "Set header is 0x10");

// Node allocation + the three inserts (MemAlloc nodes, not pooled).
AvlNode28* AvlNewNode28(const int32_t* key, AvlNode28* parent);                   // 0x14001a680
AvlNode30* AvlNewNode30(const int32_t* key, AvlNode30* parent);                   // 0x14001cc40
AvlNode30** AvlInsert30(AvlTreeHead<AvlNode30>* tree, AvlNode30** out, const int32_t* key);   // 0x14001c940
AvlNode28** AvlInsert28(AvlTreeHead<AvlNode28>* tree, AvlNode28** out, const int32_t* key);   // 0x14001a4f0
AvlNode30** PathCostTreeInsert(AvlTreeHead<AvlNode30>* tree, AvlNode30** out, const int32_t* key);   // 0x140025a10
