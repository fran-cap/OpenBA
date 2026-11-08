#pragma once
// ============================================================================
// src/game/Work/LocationWork.h — ENDGAME B.3.8 block WRK
//
// LocationWork — "come and stand at this building" work (the attendance buildings:
// chapel, tavern, ...): one citizen target held at +0x48. A REAL class with a
// compiler-generated vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "LocationWork", parent "Work", ti 0x1401c6b38,
//   create 0x14002a810 (MemAlloc(0x50) + ctor); createInPlace = the imported
//   Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x1401616a0, 13 slots:
//   [0] 0x14002a850 scalar deleting dtor (stamps, ~Internal, MemFree on &1)
//   [1] TypeName  [2] Type  [3] 0x1400efba0 Persist  [4] 0x1400ef880 Execute
//   [5] = Work (0x1400f7540)  [6] 0x1400ef850 OnComplete  [7] 0x1400ef820 OnCancel
//   [8] 0x1400efaf0 Cleanup  [9] 0x1400ef7c0 Validate  [10] 0x1400efb40 GetTarget
//   [11] 0x1400295a0 GetPriority (`+0x48 != 0`, shared with BuildRoad)
//   [12] 0x1400efae0 — this class's OWN `RET 0` (every other Work table shares
//        0x140006b10)
//   ctor 0x1400efc60 (NOT transcribed — the tree built the object with
//   Work_GenericCtor: the Work base init and the leaf field zeroed; that is kept).
//   AttendanceComponent's state-2 enter (attendancecomponent_states.cpp, block MI-A)
//   builds a LocationWork BY HAND and stamps 0x1401616a0; that registration serves
//   LocationWork::Vtable() (B38 D5).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/Work.h"
#include "Object/exe_type.h"

class CitizenComponent;

class LocationWork final : public Work {
    DECLARE_EXE_TYPE(LocationWork)

public:
    LocationWork();                                                 // (Work_GenericCtor shape, see above)
    // AttendanceComponent's state-2 enter (retail builds it inline): the
    // entity-seeded Work base ctor 0x1400f72e0, then +0x48 = 0. ENDGAME B.3.9
    // OBJ: replaces the `??0Internal` door + hand stamp at that site.
    explicit LocationWork(ComponentEntity* const* pEntityIBP);

    // nothing of its own to free
    ~LocationWork() override = default;                             // [0]  0x14002a850
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400efba0
    int   Execute(CitizenComponent** citizenIBP, System::Time* context) override;  // [4]  0x1400ef880
    int   OnComplete(CitizenComponent** arg1, ComponentEntity** arg2, int arg3) override;    // [6]  0x1400ef850
    int   OnCancel(CitizenComponent** arg1, ComponentEntity** arg2, int arg3) override;      // [7]  0x1400ef820
    int   Cleanup() override;                                       // [8]  0x1400efaf0
    int   Validate() override;                      // [9]  0x1400ef7c0
    Profession** GetTarget(Profession** outPtr) override;                         // [10] 0x1400efb40
    int   GetPriority() override;                                   // [11] 0x1400295a0
    void  Slot12(int version) override;                                      // [12] 0x1400efae0 (own RET 0)

    // --- LocationWork fields (+0x48) ---
    // +0x48: the citizen standing here, an InternalPtr<CitizenComponent> (Persist
    // 0x1400efba0 checks it IsDerivedFrom CitizenComponent; Cleanup 0x1400efaf0 reads
    // its +0x278). Raw storage (H5).
    Object::InternalPtr<CitizenComponent> _assignedCitizen;  // +0x48: zeroed (the assigned citizen)
};

static_assert(sizeof(LocationWork) == 0x50, "LocationWork is 0x50 (retail create MemAlloc)");
static_assert(alignof(LocationWork) == 8, "LocationWork is 8-aligned");
static_assert(offsetof(LocationWork, _assignedCitizen) == 0x48, "LocationWork +0x48");
