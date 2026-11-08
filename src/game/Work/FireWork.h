#pragma once
// ============================================================================
// src/game/Work/FireWork.h — ENDGAME B.3.8 block WRK
//
// FireWork — the firefighting work a burning building posts: a BST of the
// citizens carrying water (+0x50), their count (+0x58) and the capacity (+0x48).
// A REAL class with a compiler-generated vtable. FireWork extends Work directly,
// NOT AutoPickupWork.
//
// RETAIL (B38_SLOTS.md §6): reflected "FireWork", parent "Work", ti 0x1401c6978,
//   create 0x14002a1e0 (MemAlloc(0x60) + ctor 0x1400e9b50); createInPlace = the
//   imported Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x1401612a0, 13 slots:
//   [0] 0x14002a230 scalar deleting dtor (the dtor 0x1400e9ac0 + MemFree on &1)
//   [1] 0x14002a1d0 TypeName  [2] 0x14002a1c0 Type  [3] 0x1400e9a50 Persist
//   [4] 0x1400e9620 Execute   [5] = Work (0x1400f7540)  [6] 0x1400e9600 OnComplete
//   [7] 0x1400e95f0 OnCancel = `ADD RCX,0x50; JMP 0x140025520` (a member forwarder)
//   [8] 0x1400e99a0 Cleanup   [9] 0x1400e93c0 Validate  [10] 0x1400e9a30 GetTarget
//   [11] 0x14002a220 GetPriority (`+0x58 > 0`)  [12] = Work (0x140006b10)
//   The real producer is the 3-argument ctor 0x1400e9bd0, built BY HAND in
//   firecomponent_ignite.cpp (block CIT), which stamps 0x1401612a0; that
//   registration serves FireWork::Vtable() (B38 D5). The default ctor here builds
//   what the tree's factory built (Work_GenericCtor: the Work base init, the leaf
//   fields zeroed).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/Work.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Set<T>::Node

class CitizenComponent;


class FireWork final : public Work {
    DECLARE_EXE_TYPE(FireWork)

public:
    FireWork();                                                     // (Work_GenericCtor shape)
    // 0x1400e9bd0, the 3-argument ctor (FireComponent::Ignite's, the only
    // producer): the entity-seeded Work base ctor 0x1400f72e0, the table, a
    // DWORD store of `capacity` at +0x48 (the upper dword is not written),
    // +0x50 = 0 (qword), +0x58 = 0 (dword). ENDGAME B.3.9 OBJ: replaces the
    // `??0Internal` door + stamp in FireWork_CtorFull_e9bd0.
    FireWork(ComponentEntity* const* pEntityIBP, uint32_t capacity);

    // retail 0x1400e9ac0 (reached from slot 0 0x14002a230): stamp FireWork, then the
    // +0x50 set's inlined Clear (0x140014c30 on the root's +0x10 / +0x18 children, MemFree
    // of the root, +0x50 = 0 qword, +0x58 = 0 dword), then stamp Work and ~Internal. That
    // is the compiler's chain over the `_citizens` member's ~Set.
    ~FireWork() override = default;                                 // [0]  0x14002a230
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400e9a50
    int   Execute(CitizenComponent** citizenIBP, System::Time* time) override;  // [4]  0x1400e9620
    int   OnComplete(CitizenComponent** citizenIBP, ComponentEntity** arg2, int arg3) override;    // [6]  0x1400e9600
    int   OnCancel(CitizenComponent** citizenIBP, ComponentEntity** arg2, int arg3) override;      // [7]  0x1400e95f0
    int   Cleanup() override;                                       // [8]  0x1400e99a0
    int   Validate() override;                      // [9]  0x1400e93c0
    Profession** GetTarget(Profession** retbuf) override;                         // [10] 0x1400e9a30
    int   GetPriority() override;                                   // [11] 0x14002a220

    // --- FireWork fields (+0x48) ---
    int32_t  _maxFirefighters;           // +0x48: the firefighter capacity (Persist 0x1400e9a50 int&;
                                 //        Execute 0x1400e9620 compares it signed with +0x58)
    uint32_t _pad4C;           // +0x4C: the default ctor zeroes it (a qword store with +0x48);
                                 //        the 3-argument ctor 0x1400e9bd0 leaves it
    // +0x50: Collection::Set<InternalPtr<CitizenComponent>> (the T = 8 node; Cleanup
    // 0x1400e99a0 reads the element's +0x278): the citizens fighting this fire. A real
    // member: the ctor 0x1400e9bd0 zeroes root (qword) and count (dword) under an EH state
    // and the dtor 0x1400e9ac0 runs its Clear. `_citizens._count` (+0x58) is the assigned
    // count (0x14002a220 `CMP [RCX+0x58],EAX; SETG`). The Set's own ctor also zeroes its
    // +0x0c pad (+0x5C), which retail's leaves (known_port_issues row 640).
    using CitizenSet     = Collection::Set<CitizenComponent*>;
    using AssignmentNode = CitizenSet::Node;
    CitizenSet _citizens;
};

static_assert(sizeof(FireWork) == 0x60, "FireWork is 0x60 (retail create MemAlloc)");
static_assert(alignof(FireWork) == 8, "FireWork is 8-aligned");
static_assert(offsetof(FireWork, _maxFirefighters) == 0x48, "FireWork +0x48");
static_assert(offsetof(FireWork, _pad4C) == 0x4c, "FireWork +0x4c");
static_assert(offsetof(FireWork, _citizens) == 0x50, "FireWork +0x50");
static_assert(offsetof(FireWork, _citizens) + offsetof(FireWork::CitizenSet, _count) == 0x58, "FireWork +0x58");
