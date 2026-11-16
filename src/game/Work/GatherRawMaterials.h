#pragma once
// ============================================================================
// src/game/Work/GatherRawMaterials.h — ENDGAME B.3.8 block WRK
//
// GatherRawMaterials — the "haul the materials a construction site needs" work:
// per-material amounts present (+0x48 array) and in flight (+0x58 array), and a
// BST of the citizens hauling (+0x68, count +0x70). A REAL class with a
// compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "GatherRawMaterials", parent "Work",
//   create 0x14002a3c0 (MemAlloc(0x78) + ctor 0x1400ec8a0); createInPlace = the
//   imported Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x140161420, 13 slots:
//   [0] 0x14002a410 scalar deleting dtor (the dtor 0x1400ec650 + MemFree on &1)
//   [1] TypeName  [2] Type  [3] 0x1400ec560 Persist  [4] Execute
//   [5] = Work (0x1400f7540)  [6] OnComplete  [7] OnCancel  [8] Cleanup  [9] Validate
//   [10] 0x1400ec510 GetTarget  [11] 0x14002a400 GetPriority (`+0x70 > 0`)
//   [12] = Work (0x140006b10)
//   ctors: the default one (the tree built it with Work_GenericCtor: base init, leaf
//   fields zeroed — kept) and the site ctor 0x1400ec710 (entity-seeded, sizes the two
//   arrays from the site's BuildDescription; gatherrawmaterials_ctor.cpp), which
//   BuildComponent's state 1 (buildcomponent_state1.cpp) calls by name.
//   Nothing outside this block stamps 0x140161420.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/Work.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Set<T>::Node

class CitizenComponent;


class GatherRawMaterials final : public Work {
    DECLARE_EXE_TYPE(GatherRawMaterials)

public:
    GatherRawMaterials();                                           // (Work_GenericCtor shape)
    explicit GatherRawMaterials(ComponentEntity* const* buildEntityIBP);              // 0x1400ec710 (site ctor)

    // 0x1400ec650: the compiler's chain over the three members (set, +0x58, +0x48)
    ~GatherRawMaterials() override;                                 // [0]  0x14002a410
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400ec560
    int   Execute(CitizenComponent** entityIBP, System::Time* context) override;  // [4]
    int   OnComplete(CitizenComponent** param2, ComponentEntity** param3, int arg3) override;  // [6]
    int   OnCancel(CitizenComponent** param2, ComponentEntity** param3, int arg3) override;    // [7]
    int   Cleanup() override;                                       // [8]
    int   Validate() override;                          // [9]
    Profession** GetTarget(Profession** retbuf) override;                         // [10] 0x1400ec510
    int   GetPriority() override;                                   // [11] 0x14002a400

    // --- GatherRawMaterials fields (+0x48) ---
    // +0x48 / +0x58: two Collection::Array<int32> {data, size, capacity}, one element per
    // BuildDescription cost (the site ctor 0x1400ec710 sizes both through 0x1400021d0, which
    // leaves a grown tail as found: Array<int32_t>'s trait-derived spec has no init arm;
    // Persist 0x1400ec560 bulk-serializes them as int). +0x68: Collection::Set<InternalPtr<
    // CitizenComponent>> (the T = 8 node; Cleanup 0x1400ec480 reads the element's +0x278):
    // the citizens hauling. Real members: the ctors 0x1400ec8a0 / 0x1400ec710 zero +0x48..
    // +0x60 as four qwords and the set's root (qword) and count (dword) under an EH state;
    // the dtor 0x1400ec650 clears the set FIRST, then frees +0x58 (data = 0, +0x60 = 0 qword),
    // then +0x48 (data = 0, +0x50 = 0 qword): the reverse of this declaration order. The
    // Set's ctor leaves its +0x0c pad (+0x74) as found, as retail's does (row 640).
    using CitizenSet     = Collection::Set<CitizenComponent*>;
    using AssignmentNode = CitizenSet::Node;
    Collection::Array<int32_t> _present;    // +0x48: per material, the amount present
    Collection::Array<int32_t> _inFlight;   // +0x58: per material, the amount in flight
    CitizenSet _citizens;                   // +0x68: count +0x70 (GetPriority 0x14002a400 `> 0`)
};

static_assert(sizeof(GatherRawMaterials) == 0x78, "GatherRawMaterials is 0x78 (retail create MemAlloc)");
static_assert(alignof(GatherRawMaterials) == 8, "GatherRawMaterials is 8-aligned");
static_assert(offsetof(GatherRawMaterials, _present) == 0x48, "GatherRawMaterials +0x48");
static_assert(offsetof(GatherRawMaterials, _present) + offsetof(Collection::Array<int32_t>, _size) == 0x50, "GatherRawMaterials +0x50");
static_assert(offsetof(GatherRawMaterials, _present) + offsetof(Collection::Array<int32_t>, _capacity) == 0x54, "GatherRawMaterials +0x54");
static_assert(offsetof(GatherRawMaterials, _inFlight) == 0x58, "GatherRawMaterials +0x58");
static_assert(offsetof(GatherRawMaterials, _inFlight) + offsetof(Collection::Array<int32_t>, _size) == 0x60, "GatherRawMaterials +0x60");
static_assert(offsetof(GatherRawMaterials, _inFlight) + offsetof(Collection::Array<int32_t>, _capacity) == 0x64, "GatherRawMaterials +0x64");
static_assert(offsetof(GatherRawMaterials, _citizens) == 0x68, "GatherRawMaterials +0x68");
static_assert(offsetof(GatherRawMaterials, _citizens) + offsetof(GatherRawMaterials::CitizenSet, _count) == 0x70, "GatherRawMaterials +0x70");
