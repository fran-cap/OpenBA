#pragma once
// ============================================================================
// src/game/Work/FishingWork.h — ENDGAME B.3.8 block WRK
//
// FishingWork — the fishing-dock work: the citizens holding a fishing spot
// (+0x48 array) and the per-spot state (+0x58 array). A REAL class with a
// compiler-generated vtable. Extends Work directly (not AutoPickupWork).
//
// RETAIL (B38_SLOTS.md §6): reflected "FishingWork", parent "Work", ti 0x1401c69b0,
//   create 0x14002a280 (MemAlloc(0x70) + ctor 0x1400eabe0); createInPlace = the
//   imported Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x140161320, 13 slots:
//   [0] 0x14002a2c0 scalar deleting dtor (the dtor 0x1400eaa50 + MemFree on &1)
//   [1] 0x14002a270 TypeName  [2] 0x14002a260 Type  [3] 0x1400ea910 Persist
//   [4] 0x1400e9f20 Execute   [5] = Work (0x1400f7540)  [6] 0x1400e9eb0 OnComplete
//   [7] 0x1400e9e80 OnCancel  [8] 0x1400ea830 Cleanup   [9] 0x1400e9c30 Validate
//   [10] 0x1400ea8b0 GetTarget  [11] 0x140029860 GetPriority (`+0x50 > 0`, shared
//   by 13 tables)  [12] = Work (0x140006b10)
//   FishingComponent's CreateWork (resourceproduce_creatework.cpp, block MI-B) builds
//   the work BY HAND (retail ctor 0x1400eaac0) and stamps 0x140161320; that
//   registration serves FishingWork::Vtable() (B38 D5). The default ctor here builds
//   what the tree's factory built (Work_GenericCtor: base init, leaf fields zeroed).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/Work.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (citizenRefs, spotStates)
#include "object/Internal.h"         // Object::InternalPtr<T>

namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

class CitizenComponent;


class FishingWork final : public Work {
    DECLARE_EXE_TYPE(FishingWork)

public:
    FishingWork();                                                  // (Work_GenericCtor shape)

    // Retail dtor 0x1400eaa50 (reached from slot 0 0x14002a2c0): restamp, then the two
    // arrays' ~Array (+0x58 first, then +0x48), then the Work stamp and ~Internal. The
    // body is empty: reverse declaration order of the members below is that order.
    ~FishingWork() override = default;                              // [0]  0x14002a2c0
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400ea910
    int   Execute(CitizenComponent** citizenIBP, System::Time* time) override;  // [4]  0x1400e9f20
    int   OnComplete(CitizenComponent** param2, ComponentEntity** param3, int arg3) override;  // [6]  0x1400e9eb0
    int   OnCancel(CitizenComponent** param2, ComponentEntity** param3, int arg3) override;    // [7]  0x1400e9e80
    int   Cleanup() override;                                       // [8]  0x1400ea830
    int   Validate() override;                          // [9]  0x1400e9c30
    Profession** GetTarget(Profession** param2) override;                         // [10] 0x1400ea8b0
    int   GetPriority() override;                                   // [11] 0x140029860

    // --- FishingWork fields (+0x48) ---
    // Both arrays are real members: the ctor 0x1400eabe0 zeroes +0x48 / +0x50 / +0x58 /
    // +0x60 as four qwords (0x1400eac3a..0x1400eac46); the dtor 0x1400eaa50 MemFrees
    // +0x58 then zeroes +0x58 / +0x60, then MemFrees +0x48 and zeroes +0x48 / +0x50
    // (0x1400eaa75..0x1400eaa97): ~Array in reverse declaration order.
    // +0x48: the citizens holding a slot (Persist 0x1400ea910 validates each against
    // CitizenComponent); grows through 0x1400032e0 (stride 8, the new tail ZEROED, no
    // element destroy): kSpec_PtrZero, the row the AsArray<Ptr8> view reached.
    using CitizenRefList = Collection::Array<Object::InternalPtr<CitizenComponent>, Collection::DefaultAllocator,
                                             Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    CitizenRefList citizenRefs;  // +0x48 (count +0x50, capacity +0x54)
    // +0x58: per-spot WorkSpotState (0 Available, 1 InUse, 2 NeedsReset); grows through 0x1400021d0
    // (stride 4, the new tail left as found): Array<int32_t>'s trait row.
    Collection::Array<int32_t> spotStates;   // +0x58 (count +0x60, capacity +0x64)
    int32_t  retryCount;         // +0x68: retry counter for fishing spot search
    uint8_t  _pad6C[4];          // +0x6C: padding
};

static_assert(sizeof(FishingWork) == 0x70, "FishingWork is 0x70 (retail create MemAlloc)");
static_assert(offsetof(FishingWork, _pad6C) == 0x6C, "FishingWork::_pad6C");
static_assert(alignof(FishingWork) == 8, "FishingWork is 8-aligned");
static_assert(offsetof(FishingWork, citizenRefs) == 0x48, "FishingWork +0x48");
static_assert(offsetof(FishingWork, citizenRefs) + offsetof(FishingWork::CitizenRefList, _size) == 0x50, "FishingWork +0x50");
static_assert(offsetof(FishingWork, citizenRefs) + offsetof(FishingWork::CitizenRefList, _capacity) == 0x54, "FishingWork +0x54");
static_assert(offsetof(FishingWork, spotStates) == 0x58, "FishingWork +0x58");
static_assert(offsetof(FishingWork, spotStates) + offsetof(Collection::Array<int32_t>, _size) == 0x60, "FishingWork +0x60");
static_assert(offsetof(FishingWork, spotStates) + offsetof(Collection::Array<int32_t>, _capacity) == 0x64, "FishingWork +0x64");
static_assert(offsetof(FishingWork, retryCount) == 0x68, "FishingWork +0x68");
