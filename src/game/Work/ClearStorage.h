#pragma once
// ============================================================================
// src/game/Work/ClearStorage.h — ENDGAME B.3.8 block WRK
//
// ClearStorage — the "haul everything out of this storage" work (a demolished or
// disabled stockpile): a BST of the citizens assigned (+0x58) and the count of
// items left (+0x60). A REAL class with a compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "ClearStorage", parent "Work", ti 0x1401c6908,
//   create 0x14002a0b0 (MemAlloc(0x68) + ctor); createInPlace = the imported
//   Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x1401611a0, 13 slots:
//   [0] 0x14002a100 scalar deleting dtor (the dtor 0x1400e73b0 + MemFree on &1)
//   [1] 0x14002a0a0 TypeName  [2] 0x14002a090 Type  [3] 0x1400e7350 Persist
//   [4] 0x1400e6c50 Execute   [5] = Work (0x1400f7540)  [6] 0x1400e6c30 OnComplete
//   [7] 0x1400e6c20 OnCancel = `ADD RCX,0x58; JMP 0x140025520` (a member forwarder:
//       the citizen tree's find-remove, B38_SLOTS head fact 2)
//   [8] 0x1400e72a0 Cleanup   [9] 0x1400e69f0 Validate  [10] 0x1400e7330 GetTarget
//   [11] 0x14002a0f0 GetPriority (`+0x60 > 0`, shared with ConsumeProduce)
//   [12] = Work (0x140006b10)
//   ctors 0x1400e7450 / 0x1400e74b0 (NOT transcribed — the tree built the object with
//   Work_GenericCtor: the Work base init and the leaf fields zeroed; that is kept).
//   destroycomponent_states.cpp (block MI-A) builds a ClearStorage BY HAND and stamps
//   0x1401611a0; that registration serves ClearStorage::Vtable() (B38 D5).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/Work.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Set<T>::Node

class CitizenComponent;


class ClearStorage final : public Work {
    DECLARE_EXE_TYPE(ClearStorage)

public:
    ClearStorage();                                                 // (Work_GenericCtor shape, see above)
    // DestroyComponent's state-3 exit (retail builds it inline, 0x14011cfd0):
    // the entity-seeded Work base ctor 0x1400f72e0, then +0x48..+0x60 zero as
    // four qwords. ENDGAME B.3.9 OBJ: replaces the `??0Internal` door + stamp.
    explicit ClearStorage(ComponentEntity* const* pEntityIBP);

    // 0x1400e73b0: the compiler's chain over the two members (set, then +0x48)
    ~ClearStorage() override;                                       // [0]  0x14002a100
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400e7350
    int   Execute(CitizenComponent** citizenAI, System::Time* time) override;  // [4]  0x1400e6c50
    int   OnComplete(CitizenComponent** param2, ComponentEntity** arg2, int arg3) override;  // [6]  0x1400e6c30
    int   OnCancel(CitizenComponent** param2, ComponentEntity** arg2, int arg3) override;    // [7]  0x1400e6c20
    int   Cleanup() override;                                       // [8]  0x1400e72a0
    int   Validate() override;                       // [9]  0x1400e69f0
    Profession** GetTarget(Profession** retbuf) override;                         // [10] 0x1400e7330
    int   GetPriority() override;                                   // [11] 0x14002a0f0

    // --- ClearStorage fields (+0x48) ---
    // +0x48: a Collection::Array {data, count +0x50, capacity +0x54}: the ctors 0x1400e7450 /
    // 0x1400e74b0 zero +0x48 / +0x50 as two qwords and the dtor 0x1400e73b0 frees +0x48 and
    // zeroes both, the inlined ~Array of the other leaves. No slot of the class reads it
    // (Persist 0x1400e7350 streams only the set; Validate 0x1400e69f0 builds its removal list
    // in a local), so the element type is not settled: declared as bytes.
    // +0x58: Collection::Set<InternalPtr<CitizenComponent>> (node 0x28: element, parent +0x08,
    // left +0x10, right +0x18; Cleanup 0x1400e72a0 reads the element's +0x278); its count
    // +0x60 is the items-left / GetPriority count (0x14002a0f0 `> 0`). Real members: the ctors
    // zero the set's root (qword) and count (dword) under an EH state and the dtor clears the
    // set FIRST, then frees +0x48: the reverse of this declaration order. The Set's ctor also
    // zeroes its +0x0c pad (+0x64), which retail's leaves (row 640).
    using CitizenSet     = Collection::Set<CitizenComponent*>;
    using AssignmentNode = CitizenSet::Node;
    Collection::Array<uint8_t> _unreadArray;   // +0x48
    CitizenSet _citizens;                  // +0x58
};

static_assert(sizeof(ClearStorage) == 0x68, "ClearStorage is 0x68 (retail create MemAlloc)");
static_assert(alignof(ClearStorage) == 8, "ClearStorage is 8-aligned");
static_assert(offsetof(ClearStorage, _unreadArray) == 0x48, "ClearStorage +0x48");
static_assert(offsetof(ClearStorage, _unreadArray) + offsetof(Collection::Array<uint8_t>, _size) == 0x50, "ClearStorage +0x50");
static_assert(offsetof(ClearStorage, _citizens) == 0x58, "ClearStorage +0x58");
static_assert(offsetof(ClearStorage, _citizens) + offsetof(ClearStorage::CitizenSet, _count) == 0x60, "ClearStorage +0x60");
