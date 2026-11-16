#pragma once
// ============================================================================
// src/engine/collection/set_avl_e24.h — Wave 9 lane INPUT
//   The `Collection::Set` AVL node header for the **0x18-byte element**
//   instantiation, and the five unexported bodies MSVC emitted for it.
// ============================================================================
//
// WHY A SECOND FILE NEXT TO set_avl.h
// ----------------------------------
// `set_avl.h` (lane VID2) describes the SAME algorithm for the 0x10-byte
// element instantiation. It is not reusable here and the reason is the one
// PLAN.md §4.3 risk 1 names in as many words: **node pointer offsets are a
// function of sizeof(T), so MSVC emits a separate insert/find/rebalance per
// instantiation.** The element sits FIRST in the node, so the whole header
// moves:
//
//   | field   | E16 (set_avl.h) | E24 (here) |
//   |---------|-----------------|------------|
//   | element | +0x00 (0x10)    | +0x00 (0x18) |
//   | parent  | +0x10           | +0x18 |
//   | left    | +0x18           | +0x20 |
//   | right   | +0x20           | +0x28 |
//   | balance | +0x28           | +0x30 |
//   | height  | +0x2a           | +0x32 |
//   | sizeof  | 0x30            | 0x38  |
//
// and so does every address:
//
//   | body           | E16         | E24         |
//   |----------------|-------------|-------------|
//   | node alloc     | 0x18000a5b0 | 0x180023060 |
//   | retrace        | 0x18000adc0 | 0x180023170 |
//   | rotate left    | 0x18000b040 | 0x180023570 |
//   | rotate right   | 0x18000b170 | 0x1800236a0 |
//   | destroy subtree| (not ported)| 0x180023130 |
//   | erase          | 0x18000aba0 | (no door)   |
//
// Two DIFFERENT node layouts behind one shared walk is a defect (it finds only
// the root key), and it is why these are
// separate types with separate `static_assert`s rather than a template
// parameter threaded through a header another lane already landed.
//
// THE ELEMENT IS OPAQUE AND THERE IS NO sizeof(element) CLAIM BEYOND 0x18.
// None of the five bodies reads it; the ONLY thing that pins 0x18 is the node
// header offsets they do read, plus `0x180023060` copying exactly three qwords.
// `Input::KeyMap`'s `Binding` (src/engine/input/Input.h) is 0x18 and is the
// only instantiation our tree reaches today, but MSVC folds one copy per
// matching header, so the same addresses may serve other 0x18-byte elements.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace rtsetavl24 {

// `balance` is right_height - left_height, `height` is
// max(left_height, right_height) counting a null child as -1 (retail computes
// `child->height + 1` or 0). Both 16-bit, and retail's arithmetic on them is
// 16-bit throughout — see Recompute in the .cpp.
struct Node {
    uint64_t element[3];      // +0x00  opaque to the core; the node ctor
                              //   0x1800230cb..dd copies it as three qwords
    Node*    parent;          // +0x18
    Node*    left;            // +0x20
    Node*    right;           // +0x28
    int16_t  balance;         // +0x30
    int16_t  height;          // +0x32
    uint32_t pad34;           // +0x34  covered by 0x180023060's dword store at +0x30
};
static_assert(offsetof(Node, parent)  == 0x18, "Set24::Node::parent  @ +0x18");
static_assert(offsetof(Node, left)    == 0x20, "Set24::Node::left    @ +0x20");
static_assert(offsetof(Node, right)   == 0x28, "Set24::Node::right   @ +0x28");
static_assert(offsetof(Node, balance) == 0x30, "Set24::Node::balance @ +0x30");
static_assert(offsetof(Node, height)  == 0x32, "Set24::Node::height  @ +0x32");
static_assert(sizeof(Node)            == 0x38, "0x180023060 allocates 0x38");

// The container header. `Input::KeyMap` embeds it at +0x08 so that `_root`
// lands at KeyMap+0x08 and `_count` at KeyMap+0x10 — which is exactly what
// src/engine/input/Input.h already records, pinned independently by every
// ported KeyMap member. `0x180053440` (the KeyMap deleting destructor) zeroes
// `[RBX]` and `[RBX+8]` with RBX = this+8, which pins the two offsets inside
// the Set itself.
struct Set {
    Node*   root;    // +0x00
    int32_t count;   // +0x08
    int32_t pad0c;   // +0x0c
};
static_assert(offsetof(Set, root)  == 0x00, "Set24::root  @ +0x00");
static_assert(offsetof(Set, count) == 0x08, "Set24::count @ +0x08");

// The five ported bodies, at the addresses in the table above.
Node* RotateLeft    (Set* set, Node* n);
Node* RotateRight   (Set* set, Node* n);
void  Retrace       (Set* set, Node** slot);
void  DestroySubtree(Set* set, Node* n);
Node* AllocNode     (void* unused, const void* src, Node* parent);

// The 0x180009c30 boundary: two parameters, the FIRST IS IGNORED by the callee.
// (0x180009c30's body is rtalloc::FreeLockedIgnoringSelf in
// src/engine/system/allocator.cpp; see set_avl.cpp's "THE FIFTH HELPER" banner.)
void  SetNodeFree(void* ignored, void* node);

// Instrumentation. `DestroySubtree` has no ours-vs-theirs oracle (see the
// .cpp), so it is checked on a COUNT of the nodes its walk actually reaches,
// which KeyMap's deleting dtor (keymap_save.cpp) reads from outside the body.
extern unsigned g_destroyVisits;

}  // namespace rtsetavl24
