#pragma once
// ============================================================================
// src/game/Work/HuntWork.h — ENDGAME B.3.8 block WRK
//
// HuntWork — the hunting cabin work (hunt the game animals in the work
// radius): a BST of the citizens assigned (+0x48, count +0x50). A REAL
// class with a compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "HuntWork", parent "AutoPickupWork",
//   create 0x14002a690 (MemAlloc(0x58) + ctor); createInPlace = the imported
//   Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x140161620, 13 slots:
//   [0] 0x14002a700 scalar deleting dtor  [1] TypeName  [2] Type
//   [3] 0x1400ef600 Persist  [4] 0x1400eeff0 Execute  [5] = Work (0x1400f7540)
//   [6] 0x1400eefd0 OnComplete  [7] 0x1400eefc0 OnCancel = `ADD RCX,0x48; JMP
//   0x140025520` (a member forwarder)  [8] 0x1400ef530 Cleanup  [9] 0x1400eed90
//   Validate  [10] 0x1400ef5a0 GetTarget  [11] 0x140029860 GetPriority (`+0x50 > 0`,
//   shared by 13 tables)  [12] = Work (0x140006b10)
//   HuntComponent's CreateWork (resourceproduce_creatework.cpp, block MI-B) builds
//   the work (retail 0x1401226a0) with the default ctor here and Work::InitFromEntity.
//   The default ctor here builds what the tree's
//   factory built (Work_GenericCtor: base init, leaf fields zeroed).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/AutoPickupWork.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Set<T>::Node

class CitizenComponent;

class HuntWork final : public AutoPickupWork {
    DECLARE_EXE_TYPE(HuntWork)

public:
    HuntWork();                                                 // (Work_GenericCtor shape)

    // 0x1400ef680: the +0x48 citizen Set's ~Set (its Clear), then the base stamps
    ~HuntWork() override;                                           // [0]  0x14002a700
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400ef600
    int   Execute(CitizenComponent** entityIBP, System::Time* context) override;  // [4]  0x1400eeff0
    int   OnComplete(CitizenComponent** param2, ComponentEntity** arg2, int arg3) override;  // [6]  0x1400eefd0
    int   OnCancel(CitizenComponent** param2, ComponentEntity** arg2, int arg3) override;    // [7]  0x1400eefc0
    int   Cleanup() override;                                       // [8]  0x1400ef530
    int   Validate() override;                          // [9]  0x1400eed90
    Profession** GetTarget(Profession** outPtr) override;                         // [10] 0x1400ef5a0
    int   GetPriority() override;                                   // [11] 0x140029860

    // --- HuntWork fields (+0x48) ---
    // +0x48: Collection::Set<InternalPtr<CitizenComponent>> (the T = 8 node; Cleanup 0x1400ef530
    // reads the element's +0x278): the assigned citizens. A real member: the ctor (0x1401226a0)
    // zeroes root (qword) and count (dword) under an EH state, the dtor 0x1400ef680 runs its
    // inlined Clear before the AutoPickupWork / Work stamps. `_citizens._count` (+0x50) is
    // the assigned count (0x140029860 `CMP [RCX+0x50],EAX; SETG`). The Set ctor leaves
    // its +0x0c pad (+0x54) as found, as retail does (known_port_issues row 640).
    using CitizenSet     = Collection::Set<CitizenComponent*>;
    using AssignmentNode = CitizenSet::Node;
    CitizenSet _citizens;
};

static_assert(sizeof(HuntWork) == 0x58, "HuntWork is 0x58 (retail create MemAlloc)");
static_assert(alignof(HuntWork) == 8, "HuntWork is 8-aligned");
static_assert(offsetof(HuntWork, _citizens) == 0x48, "HuntWork +0x48");
static_assert(offsetof(HuntWork, _citizens) + offsetof(HuntWork::CitizenSet, _count) == 0x50, "HuntWork +0x50");
