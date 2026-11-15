#pragma once
// ============================================================================
// src/game/Work/PastureWork.h — ENDGAME B.3.8 block WRK
//
// PastureWork — the pasture herder's work (fill the pen with animals, move the
// overflow to storage, graze): a BST of the citizens assigned (+0x48, count
// +0x50). A REAL class with a compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "PastureWork", parent "AutoPickupWork",
//   create 0x14002bf00 (MemAlloc(0x58) + ctor); createInPlace = the imported
//   Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x1401617a0, 13 slots:
//   [0] 0x14002bf70 scalar deleting dtor (the dtor 0x1400f2ce0 + MemFree on &1)
//   [1] TypeName  [2] Type  [3] 0x1400f2c60 Persist  [4] 0x1400f2370 Execute
//   [5] = Work (0x1400f7540)  [6] 0x1400f2350 OnComplete  [7] 0x1400f22c0 OnCancel
//   (its own body: the find-remove plus clearing the resource's "flagged" bit)
//   [8] 0x1400f2b90 Cleanup  [9] 0x1400f2090 Validate  [10] 0x1400f2c00 GetTarget
//   [11] 0x140029860 GetPriority (`+0x50 > 0`, shared by 13 tables)
//   [12] = Work (0x140006b10)
//   ctors: the default one (the tree's factory built it with Work_GenericCtor: base
//   init, leaf fields zeroed — kept) and the from-entity ctor 0x1400f2d80 that
//   PastureComponent's slot 14 (pasturecomponent_states.cpp, block MI-B) reaches
//   through PastureWork_CreateForEntity. Nothing outside this block reads
//   0x1401617a0 back.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/AutoPickupWork.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Set<T>::Node

class CitizenComponent;


class PastureWork final : public AutoPickupWork {
    DECLARE_EXE_TYPE(PastureWork)

public:
    PastureWork();                                                  // (Work_GenericCtor shape)
    explicit PastureWork(ComponentEntity* const* entityIBP);                          // 0x1400f2d80

    // 0x1400f2ce0: the +0x48 citizen Set's ~Set (its Clear), then the base stamps
    ~PastureWork() override;                                        // [0]  0x14002bf70
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400f2c60
    int   Execute(CitizenComponent** entityIBP, System::Time* context) override;  // [4]  0x1400f2370
    int   OnComplete(CitizenComponent** param2, ComponentEntity** arg2, int arg3) override;  // [6]  0x1400f2350
    int   OnCancel(CitizenComponent** param2, ComponentEntity** param3, int arg3) override;  // [7]  0x1400f22c0
    int   Cleanup() override;                                       // [8]  0x1400f2b90
    int   Validate() override;                          // [9]  0x1400f2090
    Profession** GetTarget(Profession** outPtr) override;                         // [10] 0x1400f2c00
    int   GetPriority() override;                                   // [11] 0x140029860

    // --- PastureWork fields (+0x48) ---
    // +0x48: Collection::Set<InternalPtr<CitizenComponent>> (the T = 8 node; Cleanup 0x1400f2b90
    // reads the element's +0x278): the assigned citizens. A real member: the ctor (0x1400f2d80)
    // zeroes root (qword) and count (dword) under an EH state, the dtor 0x1400f2ce0 runs its
    // inlined Clear before the AutoPickupWork / Work stamps. `_citizens._count` (+0x50) is
    // the assigned count (0x140029860 `CMP [RCX+0x50],EAX; SETG`). The Set ctor leaves
    // its +0x0c pad (+0x54) as found, as retail does (known_port_issues row 640).
    using CitizenSet     = Collection::Set<CitizenComponent*>;
    using AssignmentNode = CitizenSet::Node;
    CitizenSet _citizens;
};

static_assert(sizeof(PastureWork) == 0x58, "PastureWork is 0x58 (retail create MemAlloc)");
static_assert(alignof(PastureWork) == 8, "PastureWork is 8-aligned");
static_assert(offsetof(PastureWork, _citizens) == 0x48, "PastureWork +0x48");
static_assert(offsetof(PastureWork, _citizens) + offsetof(PastureWork::CitizenSet, _count) == 0x50, "PastureWork +0x50");
