#pragma once
// ============================================================================
// src/engine/object/reference_set.h — Wave 11 follow-up 3, lane AVL
//   The `Collection::Set<ReferenceEntry>` bodies MSVC emitted for
//   `Object::ExternalFactory::_references`.
// ============================================================================
//
// WHAT THIS IS
// ------------
// `ExternalFactory::_references` is a `Collection::Set` whose element is the
// 0x10-byte `ReferenceEntry` (src/engine/object/External.h): a `uint32 id`
// key at +0x00 and a `Filename path` value at +0x08. Its node header therefore
// lands at parent +0x10 / left +0x18 / right +0x20 / balance +0x28 /
// height +0x2a, sizeof 0x30 — the layout `src/engine/collection/set_avl.h`
// already pins from three independent bodies, which is why this file reuses
// `rtsetavl::Node` / `rtsetavl::Set` rather than declaring a fourth copy of the
// same five offsets.
//
// The LAYOUT is shared with lane VID2's family. The ADDRESSES are not, and
// neither is the COMPARATOR: this instantiation descends on a signed 32-bit
// integer compare of the id, where every other Set family in this tree
// descends on `_stricmp` of a `Filename`. MSVC emitted a separate copy of the
// whole family:
//
//   | body         | VID2 (set_avl.cpp) | ResourceEntry (external_set) | ReferenceEntry (here) |
//   |--------------|--------------------|------------------------------|-----------------------|
//   | rotate left  | 0x18000b040        | 0x18002d860                  | 0x18002dac0 |
//   | rotate right | 0x18000b170        | 0x18002d990                  | 0x18002dbf0 |
//   | retrace      | 0x18000adc0        | 0x18002d370                  | 0x18002d4b0 |
//   | pair ctor    | (in the alloc)     | 0x18002e6e0                  | 0x18002e570 |
//   | node alloc   | (no door)          | 0x18002e740                  | 0x18002e5d0 |
//   | insert core  | (no door)          | 0x18002ce60                  | 0x18002d180 |
//   | insert       | (no door)          | 0x18002ca00                  | 0x18002cb90 |
//   | subtree free | 0x180008340        | 0x18002eea0 (factory_dtors)  | 0x18002ebf0 (factory_dtors) |
//
// (set_avl.h's prose lists its four addresses in a different order from
// set_avl.cpp's body headers. The .cpp is right — 0x18000adc0 is the RETRACE,
// 0x18000b170 the rotate RIGHT — and that was checked by disassembling both
// rather than by reading either comment.)
//
// **The last row is why this lane wrote seven bodies and not eight.** The brief
// that opened it named TWO missing bodies — the insert `0x18002cb90` and the
// destroy-whole-tree `0x18002ebf0` — but `0x18002ebf0` has been ported since
// Wave 11 lane OBJLIFE, as `RtObjFactoryDtor::
// ReferenceSubtreeFree` in `src/engine/object/factory_dtors.cpp`. A census
// `unported` row has four meanings and "a body the tree already has" is one of
// them; the grep took a minute and saved a duplicate registration, which is
// last-binding-wins and would silently have made one of the two bodies dead
// code.
//
// THE DESTROY/FREE SHAPE, ESTABLISHED FROM THE DISASSEMBLY, NOT FROM A RECIPE
// ---------------------------------------------------------------------------
// The brief warned that the three Object subtree frees genuinely differ and
// that "port these the same way" produces a leak and a double free in one
// commit. That warning is about `0x18002ebf0`, and it had already been acted
// on: factory_dtors.cpp releases `node + 0x08` there — the ReferenceEntry's
// `path`, which is the VALUE — while avl_trees.cpp's `SubtreeFree`
// (0x180008340) is the same 0x30-byte node releasing `+0x00` instead, with an
// explicit warning that a symmetric release corrupts the heap. Both readings
// were re-checked against the disassembly for this lane and both stand.
//
// Nothing in THIS file destroys a tree. The seven bodies here are the INSERT
// side, and the only release any of them performs is `Insert`'s own
// (0x18002cb90 @ 0x18002cbda): it drops the reference its 0x10-byte stack
// temporary took, on EVERY path including the equal-key path where nothing was
// inserted. That release is not symmetric with anything and is not optional —
// skipping it on the equal-key path leaks one `Filename` reference per repeated
// id, the same shape `external_set.cpp`'s `Insert` calls out.
//
// WHY THE ELEMENT IS NOT OPAQUE HERE, UNLIKE set_avl.h
// ----------------------------------------------------
// Lane VID2 could leave the element as `uint8_t[0x10]` because none of its four
// bodies reads it. Three bodies here DO: `InsertCore` compares `element.id` as
// a SIGNED int32, `PairCtor` copies the id as a bare DWORD and copy-constructs
// the `Filename`, and `Insert` builds and releases a `ReferenceEntry` on its
// own stack. So this file uses the real `RtObject::ReferenceEntry` and the
// layout claim is the one External.h already `static_assert`s.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "../collection/set_avl.h"
#include "object/External.h"

namespace rtrefset {

using rtsetavl::Node;
using rtsetavl::Set;

// The node's element, spelled as the type External.h already pins. The cast is
// safe by construction: `Node::element` is `uint8_t[0x10]` at +0x00 and
// `sizeof(ReferenceEntry) == 0x10` is asserted in External.h.
inline RtObject::ReferenceEntry* Elem(Node* n) {
    return reinterpret_cast<RtObject::ReferenceEntry*>(n->element);  // CAST-OK: elem_stride
}
inline const RtObject::ReferenceEntry* Elem(const Node* n) {
    return reinterpret_cast<const RtObject::ReferenceEntry*>(n->element);  // CAST-OK: elem_stride
}

// ---------------------------------------------------------------------------
// EFFECTIVENESS COUNTERS — branches taken INSIDE the bodies under test.
//
// WAVE9.md's standing rule: count branches taken inside the function under
// test, not proxies observed from outside it. A green run on a population that
// never rotated and never hit an equal key says nothing, and a large `calls=`
// is the same trap one level up because it counts calls ENTERED, not calls that
// DID WORK.
// ---------------------------------------------------------------------------
struct ProbeCounters {
    uint64_t rootInsert, descLeft, descRight, equalHit;
    uint64_t cmpLt, cmpEq, cmpGt;
    uint64_t rotL, rotR, preRotL, preRotR, retraceIters;
    uint64_t allocFail;
};
extern ProbeCounters g_fc;

// ---- the ported bodies ----------------------------------------------------
Node*  RotateLeft (Set* set, Node* n);                                    // 0x18002dac0
Node*  RotateRight(Set* set, Node* n);                                    // 0x18002dbf0
void   Retrace    (Set* set, Node** slot);                                // 0x18002d4b0
Node*  PairCtor   (Node* node, const RtObject::ReferenceEntry* src,
                   Node* parent);                                         // 0x18002e570
Node*  AllocNode  (void* ignored, const RtObject::ReferenceEntry* src,
                   Node* parent);                                         // 0x18002e5d0
Node** InsertCore (Set* set, Node** out,
                   const RtObject::ReferenceEntry* pair);                 // 0x18002d180
Node** Insert     (Set* set, Node** out, const uint32_t* key,
                   const System::Filename* value);                        // 0x18002cb90

}  // namespace rtrefset
