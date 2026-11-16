#pragma once
// ============================================================================
// src/engine/object/external_set.h — Wave 9 lane RES
//   The `Collection::Set<ResourceEntry>` bodies MSVC emitted for
//   `Object::ExternalFactory::_resources`, plus `ExternalFactory::Clean`.
// ============================================================================
//
// WHAT THIS IS, AND WHY IT IS A THIRD 0x18-ELEMENT FILE
// -----------------------------------------------------
// `ExternalFactory::_resources` is a `Collection::Set` whose element is the
// 0x18-byte `ResourceEntry` (src/engine/object/External.h). Its node header
// therefore lands at parent +0x18 / left +0x20 / right +0x28 / balance +0x30 /
// height +0x32, sizeof 0x38 — **byte-for-byte the layout lane INPUT already
// pinned in `src/engine/collection/set_avl_e24.h`**, which is why this file
// reuses `rtsetavl24::Node` and `rtsetavl24::Set` rather than declaring a
// fourth copy of the same five offsets.
//
// The LAYOUT is shared. The ADDRESSES are not. MSVC emitted a SECOND, separate
// copy of the whole family for this instantiation:
//
//   | body              | E24 / KeyMap (lane INPUT) | ResourceEntry (here) |
//   |-------------------|---------------------------|----------------------|
//   | rotate left       | 0x180023570               | 0x18002d860 |
//   | rotate right      | 0x1800236a0               | 0x18002d990 |
//   | retrace           | 0x180023170               | 0x18002d370 |
//   | node alloc        | 0x180023060               | 0x18002e740 |
//   | pair ctor         | (folded into the alloc)   | 0x18002e6e0 |
//   | insert core       | (no door in that lane)    | 0x18002ce60 |
//   | insert            | (no door in that lane)    | 0x18002ca00 |
//   | erase             | (no door in that lane)    | 0x18002cf60 |
//   | find              | (no door in that lane)    | 0x18002dd70 |
//   | element+node free | (no door in that lane)    | 0x18002ee40 |
//
// Each row above is transcribed separately. Where two rows
// genuinely share bytes that is recorded in the .cpp WITH the comparison, never
// assumed — avl_trees.cpp set that precedent and gives the reason: this family
// has already produced four near-misses that passed green runs and differed
// only in an alignment immediate, a shift, or a trailing call.
//
// WHY THE ELEMENT IS *NOT* OPAQUE HERE, UNLIKE set_avl_e24.h
// -----------------------------------------------------------
// Lane INPUT could leave the element as `uint8_t[0x18]` because none of its
// five bodies reads it. Five bodies here DO:
//   * `Find` / `InsertCore` compare `element.path._data` with `_stricmp`;
//   * `PairCtor` copy-constructs the `Filename` at +0x00 and copies +0x08 and
//     +0x10 as raw qwords;
//   * `Erase` and `FreeEntryNode` RELEASE the `Filename` at +0x00 before the
//     node is freed.
// So this file uses the real `RtObject::ResourceEntry` and the layout claim is
// the one External.h already `static_assert`s.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "../collection/set_avl_e24.h"
#include "object/External.h"

namespace rtresset {

using rtsetavl24::Node;
using rtsetavl24::Set;

// The node's element, spelled as the type External.h already pins. The cast is
// safe by construction: `Node::element` is `uint8_t[0x18]` at +0x00 and
// `sizeof(ResourceEntry) == 0x18` is asserted in External.h.
inline RtObject::ResourceEntry* Elem(Node* n) {
    return reinterpret_cast<RtObject::ResourceEntry*>(n->element);  // CAST-OK: elem_stride
}
inline const RtObject::ResourceEntry* Elem(const Node* n) {
    return reinterpret_cast<const RtObject::ResourceEntry*>(n->element);  // CAST-OK: elem_stride
}

// The 16-byte tail `0x18002ca00` copies out of its FOURTH argument: everything
// in a ResourceEntry after the key. Declared as its own type because the caller
// (`ExternalFactory::Add`, 0x1800be570) builds it on the stack SEPARATELY from
// the key and hands the two in different registers (R8 = key, R9 = this).
struct EntryValue {
    ::Object::External* external;    // +0x00 -> ResourceEntry::external
    uint32_t            usageMask;   // +0x08 -> ResourceEntry::usageMask
    uint32_t            referenceId; // +0x0c -> ResourceEntry::referenceId
};
static_assert(sizeof(EntryValue) == 0x10, "0x18002ca00 copies exactly two qwords");

// `Collection::Array<Filename>` as `Clean` builds it on its own stack. Same
// three fields as every other Array in this engine; pinned here by
// `0x18002dff0` reading `[self]`, `[self+8]` and `[self+0xc]`.
struct FilenameArray {
    System::Filename*   data;      // +0x00 — raw storage; ArrayResize constructs, ArrayDestroyRange destroys
    int32_t             size;      // +0x08
    int32_t             capacity;  // +0x0c
};
static_assert(offsetof(FilenameArray, size)     == 0x08, "Array::_size @ +0x08");
static_assert(offsetof(FilenameArray, capacity) == 0x0c, "Array::_capacity @ +0x0c");

// ---------------------------------------------------------------------------
// EFFECTIVENESS COUNTERS — branches taken INSIDE the bodies under test.
//
// WAVE9.md's standing rule, learned three times: count branches taken inside
// the function under test, not proxies observed from outside it. A green run on
// a population that never rotated, never erased a two-child node and never hit
// an equal key says nothing — and a large `calls=` is the same trap one level
// up, because it counts calls ENTERED, not calls that DID WORK.
//
// Plain unconditional increments. The call volume is one insert per resource
// (~22.6k over a full menu-to-gameplay run, lane FNHOOK's
// `[ResLoad] census: FindOrLoad=22591`), and a conditional would need a load
// anyway.
// ---------------------------------------------------------------------------
struct ProbeCounters {
    // insert
    uint64_t rootInsert, descLeft, descRight, equalHit;
    uint64_t cmpLt, cmpEq, cmpGt;         // comparator outcome distribution
    uint64_t nullSearchKey, nullNodeKey;  // the two asymmetric null arms
    // rebalance
    uint64_t rotL, rotR, preRotL, preRotR, retraceIters;
    // erase
    uint64_t erase0Child, erase1Child, erase2Child, erasePredSplice, eraseRootSplice;
    // Clean
    uint64_t cleanVisited, cleanEvicted, cleanBitCleared, cleanArrayGrow;
};
extern ProbeCounters g_rc;

// ---- the ported bodies ----------------------------------------------------
Node*  RotateLeft   (Set* set, Node* n);                                  // 0x18002d860
Node*  RotateRight  (Set* set, Node* n);                                  // 0x18002d990
void   Retrace      (Set* set, Node** slot);                              // 0x18002d370
Node*  PairCtor     (Node* node, const RtObject::ResourceEntry* src,
                     Node* parent);                                       // 0x18002e6e0
Node*  AllocNode    (void* ignored, const RtObject::ResourceEntry* src,
                     Node* parent);                                       // 0x18002e740
Node** InsertCore   (Set* set, Node** out,
                     const RtObject::ResourceEntry* pair);                // 0x18002ce60
Node** Insert       (Set* set, Node** out, const System::Filename* key,
                     const EntryValue* value);                            // 0x18002ca00
void   FreeEntryNode(void* ignored, Node* node);                          // 0x18002ee40
void   Erase        (Set* set, Node* z);                                  // 0x18002cf60
Node*  Find         (const Set* set, const System::Filename* key);        // 0x18002dd70

// ---- NOT RETAIL (block PATHINDEX): the hash index beside the tree ----------
// src/engine/collection/fold_index.h has the exactness argument. InsertCore and
// Erase keep it in step; `IndexedFind` answers for `set` when it can (true, and
// `*out` is what `Find`'s descent returns for `key`) and declines otherwise
// (false: the caller walks). `IndexClear` is for a set whose nodes were freed
// wholesale (~ExternalFactory).
bool   IndexedFind (const void* set, int32_t count, const char* key, Node** out);
void   IndexClear  (const void* set);

// ---- NOT RETAIL (block VISITPASS): which node holds an External --------------
// src/engine/collection/ptr_index.h has the claim; external_visit.cpp is the
// reader. `VisitIndexFind` answers for `set` when it can (true; `*out` is the
// node whose element holds `ext`, or null when none does, and `*verified` that
// row's flag) and declines otherwise (false: the caller walks). The flag is
// external_visit.cpp's "the path walk for this object lands on its own row".
bool   VisitIndexFind(const void* set, int32_t count, const ::Object::External* ext, Node** out, bool* verified);
bool   VisitIndexSetVerified(const void* set, const ::Object::External* ext);
void   VisitIndexStats(uint64_t* size, bool* broken, uint64_t* duplicates);

void   ArrayDestroyRange(void* ignored, System::Filename* first, int count); // 0x18002e3d0
void   ArrayResize      (FilenameArray* self, int newSize,
                         const System::Filename* fill);                       // 0x18002dff0

}  // namespace rtresset

namespace RtObject {
// ?Clean@ExternalFactory@Object@@QEAAXXZ   retail 0x1800be310
void RtObject_ExternalFactory_Clean(ExternalFactory* self);
// Add's HIT arm (1800be68b..1800be6c7) on a node already in hand: OR the clean
// bit in, visit on a first touch (external_add.cpp; block VISITPASS reaches it).
// `viaAdd` (NOT RETAIL) is true from Add's descent, false from the visit's shortcut.
void RtObject_ExternalFactory_AddHit(ExternalFactory* self, rtresset::Node* node, ::Object::External* external,
                                     bool viaAdd);
// NOT RETAIL in shape (docs/extensions/handins/VISITREPLAY.md): the hit arm's two halves. `ArmMark`
// ORs the clean bit in (with the arm's counters) and returns whether it was clear; `VisitRow` is the
// first touch's reference visit.
bool RtObject_ExternalFactory_ArmMark(ExternalFactory* self, rtresset::Node* node);
void RtObject_ExternalFactory_VisitRow(ExternalFactory* self, rtresset::Node* node, ::Object::External* external);
// NOT RETAIL (docs/extensions/handins/VISITREPLAY.md): `Add(found)` with found's row in hand
// (external_add.cpp has the argument). `AddDirect` is its test plus Add's counters: true = run the
// arm on `node`, false = run Add.
bool RtObject_ExternalFactory_AddDirect(::Object::External* found, rtresset::Node* node);
void RtObject_ExternalFactory_AddFound(ExternalFactory* self, ::Object::External* found, rtresset::Node* node);
// NOT RETAIL, print-only: the per-bracket census of rows a first touch marked (external_add.cpp).
void VisitCensusPass(const char* what, const char* loopName, int bit);
}  // namespace RtObject
