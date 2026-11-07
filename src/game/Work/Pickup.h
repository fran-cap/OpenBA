#pragma once
// ============================================================================
// src/game/Work/Pickup.h — ENDGAME B.3.8 block WRK
//
// Pickup — "carry this loose pile to storage" work: one citizen at a time
// (status +0x48: 0 unassigned / 1 assigned / 2 completing; citizen +0x50;
// candidate array +0x58). A REAL class with a compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "Pickup", parent "Work", ti 0x1401c6be0,
//   create 0x14002bfc0 (MemAlloc(0x60) + ctor 0x1400f3c00); createInPlace = the
//   imported Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x140161810, 13 slots:
//   [0] 0x14002c010 scalar deleting dtor (stamps, ~Internal, MemFree on &1)
//   [1] TypeName  [2] Type  [3] 0x1400f3aa0 Persist
//   [4] 0x1400f2f00 Execute (Ghidra mislabels it Pickup_Serialize)
//   [5] 0x1400f38f0 Slot5 — this class's own re-target body (it re-dispatches [4])
//   [6] 0x1400f2ec0 OnComplete  [7] 0x1400f2e80 OnCancel  [8] 0x1400f39f0 Cleanup
//   [9] 0x1400f2e20 Validate  [10] 0x1400f3a50 GetTarget  [11] 0x14002c000 GetPriority
//   [12] 0x1400f3a70 — Pickup's OWN 40-byte body: the post-load re-key of the item
//        into the Laborer bucket (WorkScheduler::Rekey 0x1400f8400).
//   RawMaterialComponent's Draw (rawmaterialcomponent.cpp, outside this block) builds a
//   Pickup BY HAND (MemAlloc(0x60), Work_Ctor25) and stamps 0x140161810; that
//   registration serves Pickup::Vtable() (B38 D5).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/Work.h"
#include "Object/exe_type.h"

class CitizenComponent;
class Profession;

// Pickup::status (+0x48): who holds the pile. Execute (0x1400f2f00) claims an Unassigned
// pickup (-> Assigned), OnComplete (0x1400f2ec0) sets Completing, OnCancel (0x1400f2e80) and
// a Validate (0x1400f2e20) failure return it to Unassigned. Values are retail's.
enum class PickupStatus : int32_t {
    Unassigned = 0,
    Assigned   = 1,
    Completing = 2,
};

class Pickup final : public Work {
    DECLARE_EXE_TYPE(Pickup)

public:
    Pickup();                                                       // 0x1400f3c00

    // nothing of its own to free
    ~Pickup() override = default;                                   // [0]  0x14002c010
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400f3aa0
    int   Execute(CitizenComponent** entityIBP, System::Time* context) override;  // [4]  0x1400f2f00
    int   Slot5(CitizenComponent** entityIBP, System::Time* context) override;           // [5]  0x1400f38f0 (re-target)
    int   OnComplete(CitizenComponent** arg1, ComponentEntity** arg2, int arg3) override;    // [6]  0x1400f2ec0
    int   OnCancel(CitizenComponent** arg1, ComponentEntity** arg2, int arg3) override;      // [7]  0x1400f2e80
    int   Cleanup() override;                                       // [8]  0x1400f39f0
    int   Validate() override;                          // [9]  0x1400f2e20
    Profession** GetTarget(Profession** outPtr) override;                         // [10] 0x1400f3a50
    int   GetPriority() override;                                   // [11] 0x14002c000
    void  Slot12(int version) override;                             // [12] 0x1400f3a70

    // --- Pickup fields (+0x48) ---
    PickupStatus status;         // +0x48: PickupStatus, int32_t (Persist
                                 //        0x1400f3aa0 Serialize(int&))
    uint8_t  _pad4C[4];          // +0x4C: padding
    Object::InternalPtr<CitizenComponent> citizenIBP; // +0x50 (Persist validates the
                                 //        cell 0x1401c7158; Cleanup reads its +0x278)
    Profession* candidateList;   // +0x58: ExternalPtr<Profession> (Persist v1+ 0x140018020); the
                                 //        name is the tree's, not what the field holds
};

static_assert(sizeof(Pickup) == 0x60, "Pickup is 0x60 (retail create MemAlloc)");
static_assert(offsetof(Pickup, _pad4C) == 0x4C, "Pickup::_pad4C");
static_assert(alignof(Pickup) == 8, "Pickup is 8-aligned");
static_assert(offsetof(Pickup, status) == 0x48, "Pickup +0x48");
static_assert(sizeof(PickupStatus) == 4, "Pickup::status is an int32_t");
static_assert(offsetof(Pickup, citizenIBP) == 0x50, "Pickup +0x50");
static_assert(offsetof(Pickup, candidateList) == 0x58, "Pickup +0x58");
