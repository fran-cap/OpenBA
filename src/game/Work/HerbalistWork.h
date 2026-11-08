#pragma once
// ============================================================================
// src/game/Work/HerbalistWork.h — ENDGAME B.3.8 block WRK
//
// HerbalistWork — the herbalist's hut work (gather grown herbs in the work
// radius): a BST of the citizens assigned (+0x48, count +0x50). A REAL
// class with a compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "HerbalistWork", parent "AutoPickupWork",
//   create 0x14002a5e0 (MemAlloc(0x60) + ctor); createInPlace = the imported
//   Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x1401615a0, 13 slots:
//   [0] 0x14002a640 scalar deleting dtor  [1] TypeName  [2] Type
//   [3] 0x1400eebb0 Persist  [4] 0x1400ee3b0 Execute  [5] = Work (0x1400f7540)
//   [6] 0x1400ee390 OnComplete  [7] 0x1400ee380 OnCancel = `ADD RCX,0x48; JMP
//   0x140025520` (a member forwarder)  [8] 0x1400eeae0 Cleanup  [9] 0x1400ee150
//   Validate  [10] 0x1400eeb50 GetTarget  [11] 0x140029860 GetPriority (`+0x50 > 0`,
//   shared by 13 tables)  [12] = Work (0x140006b10)
//   HerbalistComponent's CreateWork (resourceproduce_creatework.cpp, block MI-B) builds
//   the work BY HAND (retail 0x14004b160) and stamps 0x1401615a0; that registration
//   serves HerbalistWork::Vtable() (B38 D5). The default ctor here builds what the tree's
//   factory built (Work_GenericCtor: base init, leaf fields zeroed).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/AutoPickupWork.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Set<T>::Node

class CitizenComponent;


class HerbalistWork final : public AutoPickupWork {
    DECLARE_EXE_TYPE(HerbalistWork)

public:
    HerbalistWork();                                                 // (Work_GenericCtor shape)

    // 0x1400eec40: the +0x48 citizen Set's ~Set (its Clear), then the base stamps
    ~HerbalistWork() override;                                      // [0]  0x14002a640
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400eebb0
    int   Execute(CitizenComponent** entityIBP, System::Time* context) override;  // [4]  0x1400ee3b0
    int   OnComplete(CitizenComponent** param2, ComponentEntity** arg2, int arg3) override;  // [6]  0x1400ee390
    int   OnCancel(CitizenComponent** param2, ComponentEntity** arg2, int arg3) override;    // [7]  0x1400ee380
    int   Cleanup() override;                                       // [8]  0x1400eeae0
    int   Validate() override;                          // [9]  0x1400ee150
    Profession** GetTarget(Profession** outPtr) override;                         // [10] 0x1400eeb50
    int   GetPriority() override;                                   // [11] 0x140029860

    // --- HerbalistWork fields (+0x48) ---
    // +0x48: Collection::Set<InternalPtr<CitizenComponent>> (the T = 8 node; Cleanup 0x1400eeae0
    // reads the element's +0x278): the assigned citizens. A real member: the ctor (0x14004b160)
    // zeroes root (qword) and count (dword) under an EH state, the dtor 0x1400eec40 runs its
    // inlined Clear before the AutoPickupWork / Work stamps. `_citizens._count` (+0x50) is
    // the assigned count (0x140029860 `CMP [RCX+0x50],EAX; SETG`). The Set ctor also zeroes
    // its +0x0c pad (+0x54), which retail leaves (known_port_issues row 640).
    using CitizenSet     = Collection::Set<CitizenComponent*>;
    using AssignmentNode = CitizenSet::Node;
    CitizenSet _citizens;
    int32_t  _failedSearchCount;           // +0x58: the failed-search count (Persist 0x1400eebb0 int&;
                                 //        Execute 0x1400ee3b0 compares it signed)
    uint8_t  _pad5C[4];          // +0x5C: padding
};

static_assert(sizeof(HerbalistWork) == 0x60, "HerbalistWork is 0x60 (retail create MemAlloc)");
static_assert(offsetof(HerbalistWork, _pad5C) == 0x5C, "HerbalistWork::_pad5C");
static_assert(alignof(HerbalistWork) == 8, "HerbalistWork is 8-aligned");
static_assert(offsetof(HerbalistWork, _citizens) == 0x48, "HerbalistWork +0x48");
static_assert(offsetof(HerbalistWork, _citizens) + offsetof(HerbalistWork::CitizenSet, _count) == 0x50, "HerbalistWork +0x50");
static_assert(offsetof(HerbalistWork, _failedSearchCount) == 0x58, "HerbalistWork +0x58");
