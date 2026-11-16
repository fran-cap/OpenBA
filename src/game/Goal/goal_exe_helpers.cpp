// src/game/Goal/goal_exe_helpers.cpp
// Clean reimplementation of the StatGoal lazy ExternalPtr resolver
// (FUN_1400265b0), ported to close the standalone Goal code-gap.
//
// Decompiled from Application-x64.exe via Ghidra.

#include "goal_exe_helpers.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "collection/Collection.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

// ============================================================
// StatGoal ExternalPtr resolver (FUN_1400265b0)
// ============================================================
// Identical in shape to ResolveExternalByName_Impl (NamedExternalRef::ResolveDescription): a lazy
// ExternalPtr<T>::Resolve. The input points at an ExternalPtr struct:
//   +0x00: String (8 bytes) — resource name
//   +0x08: int32  — cached generation counter (vs ExternalFactory+0x08)
//   +0x10: void*  — cached External* result
// If the cached generation matches the factory's, the cached pointer is valid.
// Otherwise it converts the name String to a Filename, calls
// ExternalFactory::Find, validates the result against the ExternalPtr's
// expected type (TypeInfo at *0x140152870), caches it (or null), and updates
// the generation.
//
// Row 84 (B7 RES), retail 0x1400265b0: the factory singleton is read with no test
// (0x1400265cc-0x1400265d6); the miss is FIND-ONLY: Filename(String) 0x140151d68, the
// import 0x140152460 is ?Find@ExternalFactory (no load, no name normalisation); the
// generation is read twice, before the compare (0x1400265d6) and again for the store
// (0x140026630). The tree's FindOrLoad of a normalised name on a miss is gone.
void NamedExternalRef::Resolve() {
    ::Object::ExternalFactory* efactory = ::Object::ExternalFactory::_singleton;

    int32_t refGen     = generation;
    int32_t factoryGen = efactory->_generation;
    if (refGen == factoryGen) return;

    System::Filename filename(name);
    ::Object::External* found = efactory->FindAs<::Object::External>(filename);

    ::Object::External* result = nullptr;
    if (found) {
        // Retail reads the type id through the import slot 0x140152870
        // (?_typeInfo@External@Object@@). ENDGAME B.3.9 TR: the class accessor; the
        // slot row (reg_unreg_lane1.cpp) is gone with its last reader.
        if (::Object::IsDerivedFromNullSafe(found, ::Object::External::TypeId())) {
            result = found;
        }
    }

    resolved = result;
    // 0x140026630: the generation is re-read from the factory for the store.
    generation = efactory->_generation;

}

// ============================================================
// Reference-triple array resize (FUN_14001b340)
// ============================================================
// Callers: BuildingGoal::Serialize (0x1400d76a0) and MapGenerator::Serialize
// (0x1400a2980), both on the LOAD leg only (serializer m_encoding == 0).
// B4 Y GAMEA: the resize is `NamedExternalRefArray::SetSize` (the row below);
// the forwarder Goal_RefArray_Resize is gone with the raw triple.
//
// Same primitive as EventArray_Grow in src/game/Game/eventlog.cpp — including
// the identical triple-capacity heuristic on a single-element grow — but with a
// 0x18 stride, over the ExternalPtr triple that NamedExternalRef::Resolve and
// ResolveExternalByName_Impl operate on:
//   +0x00 System::String (resource name)   +0x08 int32 cached generation
//   +0x10 void* cached External*
//
// ENDGAME B.3.2 step 2: the body below WAS a hand-written copy of
// `rtcoll::SetSizeCore`. It is now one `Collection::Array<>::SetSize` driven by
// this instantiation's element ops, spelled as an `ArraySpec` row exactly like
// the rows src/engine/collection/array.cpp carries. Compared arm for
// arm against SetSizeCore before the swap: exact-equal early return, `_size`
// stored before the capacity test, treble-on-single-append-from-non-empty,
// copy-construct the preserved prefix only when the block moved, init the new
// tail, destroy the truncated tail in place / the whole old buffer on a realloc,
// then free. Two differences, both deliberate:
//   * the allocation now goes through `?MemAlloc@MemoryAllocator@System@@`
//     (0x1800c7a50, the LOCKING static) instead of the unlocked instance
//     `Allocate` (0x1800c6e60) this body reached through `fn_MemAlloc`. Same
//     allocator and same heap (src/engine/system/allocator.cpp:676), plus the
//     critical section retail's own Collection code takes.
//   * the old body returned early when the allocation came back null;
//     SetSizeCore instead skips a null element address per iteration. Both are
//     out-of-memory-only arms and neither is reachable while MemAlloc works.
namespace {

// The element: {System::String name @+0x00, int32 generation @+0x08,
//               External* cached @+0x10}, stride 0x18, align 8.
// +0x0c..+0x0f is written by NEITHER arm, which — together with the -1
// generation seed — is why this row cannot be derived from a C++ type's traits.
// The element operations take the element; rtcoll::AssignAs / InitAs / DestroyAs
// (src/engine/collection/Collection.h) are the one place the core's stepped
// byte address becomes it.
void AssignGoalRefEntry(NamedExternalRef* dst, const NamedExternalRef* src) {
    new (&dst->name) System::String(src->name);      // +0x00
    dst->generation = src->generation;               // +0x08
    dst->resolved = src->resolved;                   // +0x10
}

// ⚠ The tail SEED VALUES are load-bearing: a newly added entry gets generation
// **-1**, not 0. Zero is a legitimate ExternalFactory generation, so a 0 seed
// would make the lazy resolver take its "cache still valid" early-out while the
// pointer is still null — the entry would silently never resolve. Faithful to
// retail, which stores 0xffffffff explicitly.
void InitGoalRefEntry(NamedExternalRef* e) {
    e->name._data = nullptr;                         // +0x00
    e->generation = -1;                              // +0x08 generation -1
    e->resolved = nullptr;                           // +0x10
}

void DestroyGoalRefEntry(NamedExternalRef* elems, int count) {
    for (int i = 0; i < count; ++i)
        elems[i].name.~String();
}

// stays explicit: a String copy-ctor `assign` (byteCopy=false) with a real
// `destroy`, the load-bearing -1 generation seed in `init`, and an unwritten
// extent at [0x0c,0x10).
}  // namespace
const rtcoll::ArraySpec GoalRows::kSpec_GoalRefEntry = {   // exe 0x14001b340 / 0x14001b030
    "Array<{String,int32,External*}>", 0x18, 8,
    &rtcoll::AssignAs<NamedExternalRef, &AssignGoalRefEntry>,
    &rtcoll::InitAs<NamedExternalRef, &InitGoalRefEntry>,
    &rtcoll::DestroyAs<NamedExternalRef, &DestroyGoalRefEntry>, false,
    /*assign unwritten*/ 0x0c, 0x10,
    /*init   unwritten*/ 0x0c, 0x10,
    /*seed zero       */ 0,    0 };
