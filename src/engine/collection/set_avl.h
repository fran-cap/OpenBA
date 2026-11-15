// ============================================================================
// src/engine/collection/set_avl.h — Wave 9 lane VID2
//   The node header the four unexported `Collection::Set` AVL bodies operate
//   on, shared between set_avl.cpp, the files that call `Retrace` / `Erase`
//   (message_callbacks.cpp, set_core.cpp, avl_trees.cpp, set_avl_pathid.cpp,
//   device_startup.cpp) and the owners of a set with this node layout
//   (Application.h's `_messageCallbacks` among them).
// ============================================================================
//
// It exists so every caller drives the SAME `Retrace`/`Erase` against the SAME
// node layout — a file that re-declared either would be working on a copy of
// the layout rather than the port's.
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace rtsetavl {

// ---------------------------------------------------------------------------
// The node header.
//
// PINNED BY THREE INDEPENDENT BODIES, which is why it is not guessed:
//
//   * 0x18000a5b0 (node alloc) allocates 0x30 bytes and writes the element at
//     +0x00/+0x08, then zeroes +0x10, +0x18, +0x20 and the DWORD at +0x28.
//   * 0x18000adc0 / 0x18000b040 / 0x18000b170 read `_parent` at +0x10, `_left`
//     at +0x18, `_right` at +0x20, and the two 16-bit words at +0x28 (balance)
//     and +0x2a (height).
//   * 0x18009fa60 / 0x18009fa20 / 0x1800f83f0 all descend by comparing the
//     UNSIGNED dword at +0x00 and following +0x18 (less) or +0x20 (greater).
//
// THE ELEMENT IS DELIBERATELY OPAQUE AND THERE IS NO sizeof ASSERT. These four
// bodies never touch it, and MSVC folded one copy of each for every
// `Collection::Set` instantiation whose node header matches — the same address
// may serve several element types. Asserting 0x30 here would bake in the
// message-callback instantiation's size and be wrong for the others while
// looking authoritative.
//
// `balance` is `right_height - left_height`, `height` is
// `max(left_height, right_height)` counting a null child as -1 (retail computes
// `child->height + 1` or 0). Both are 16-BIT and retail's arithmetic on them is
// 16-bit throughout; see set_avl.cpp's Recompute.
// ---------------------------------------------------------------------------
struct Node {
    uint8_t  element[0x10];   // +0x00  element; NEVER read by anything here
    Node*    parent;          // +0x10
    Node*    left;            // +0x18
    Node*    right;           // +0x20
    int16_t  balance;         // +0x28  right_height - left_height
    int16_t  height;          // +0x2a
    uint32_t pad2c;           // +0x2c  zeroed by 0x18000a5b0's dword store
};
static_assert(offsetof(Node, parent)  == 0x10, "Set::Node::parent  @ +0x10");
static_assert(offsetof(Node, left)    == 0x18, "Set::Node::left    @ +0x18");
static_assert(offsetof(Node, right)   == 0x20, "Set::Node::right   @ +0x20");
static_assert(offsetof(Node, balance) == 0x28, "Set::Node::balance @ +0x28");
static_assert(offsetof(Node, height)  == 0x2a, "Set::Node::height  @ +0x2a");

// The ONE typed view of a node's element for the set's owners (B.4): the
// caller names the element type its own instantiation stores in the 0x10
// bytes at +0x00 (e.g. Application::MessageCallbackEntry) and reads it by
// field. The core above never reads the element; this is its only hand-off.
template <class E>
const E& ElementOf(const Node* n) {
    static_assert(sizeof(E) <= sizeof(n->element), "the element fits the node's 0x10 bytes");
    return *reinterpret_cast<const E*>(n->element);   // the erased set core's one element view (rtsetavl::ElementOf)  // CAST-OK: elem_stride
}

// ---------------------------------------------------------------------------
// The container header.
//
// `RegisterMessageCallback` takes `LEA RDI,[RCX+0x18]` as the Set and then
// `INC dword ptr [RDI+0x8]` as the count, so the count is at Set+0x08 and the
// root at Set+0x00. The erase decrements the same dword (0x18000abae).
// ---------------------------------------------------------------------------
struct Set {
    Node*   root;    // +0x00
    int32_t count;   // +0x08
    int32_t pad0c;   // +0x0c
};
static_assert(offsetof(Set, root)  == 0x00, "Set::root  @ +0x00");
static_assert(offsetof(Set, count) == 0x08, "Set::count @ +0x08");

// The four ported bodies, at 0x18000b040 / 0x18000b170 / 0x18000adc0 /
// 0x18000aba0 respectively.
Node* RotateLeft (Set* set, Node* n);
Node* RotateRight(Set* set, Node* n);
void  Retrace    (Set* set, Node** slot);
void  Erase      (Set* set, Node* z);

// The 0x180009c30 boundary: two parameters, the first IGNORED by the callee.
void  SetNodeFree(void* ignored, void* node);

}  // namespace rtsetavl
