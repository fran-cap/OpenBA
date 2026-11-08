#pragma once
// ============================================================================
// src/game/Work/ConsumeProduce.h — ENDGAME B.3.8 block WRK
//
// ConsumeProduce — the production work of a ConsumeProduceComponent building
// (blacksmith, tailor, …): the citizens assigned to its work slots (+0x58 array)
// and the per-slot state (+0x68 array). Reflected "ConsumeProduce" (the tree's
// POD was named ConsumeProduceAction). A REAL class with a compiler-generated
// vtable.
//
// RETAIL (B38_SLOTS.md §6): reflected "ConsumeProduce", parent "Work", ti 0x1401c6940,
//   create 0x14002a150 (MemAlloc(0x78) + ctor 0x1400e9340); createInPlace = the
//   imported Internal::CreateInPlaceObject (B8-13: the tree's in-place build kept)
//   table 0x140161220, 13 slots:
//   [0] 0x14002a190 scalar deleting dtor (the dtor 0x1400e9190 + MemFree on &1)
//   [1] 0x14002a140 TypeName  [2] 0x14002a130 Type  [3] 0x1400e9020 Persist
//   [4] 0x1400e7840 Execute   [5] = Work (0x1400f7540)  [6] 0x1400e77c0 OnComplete
//   [7] 0x1400e7780 OnCancel  [8] 0x1400e8f30 Cleanup   [9] 0x1400e7530 Validate
//   [10] 0x1400e8fb0 GetTarget  [11] 0x14002a0f0 GetPriority (`+0x60 > 0`, shared
//   with ClearStorage)  [12] 0x1400e9010 its OWN `RET 0` (not Work's 0x140006b10)
//   The component's CreateWork (resourceproduce_creatework.cpp, block MI-B) builds a
//   ConsumeProduce BY HAND (retail 0x1400e9200) and stamps 0x140161220; that
//   registration serves ConsumeProduce::Vtable() (B38 D5).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Work/Work.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (_slotCitizens, _spotStates)
#include "object/Internal.h"         // Object::InternalPtr<T>

namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

class CitizenComponent;


class ConsumeProduce final : public Work {
    DECLARE_EXE_TYPE(ConsumeProduce)

public:
    ConsumeProduce();                                               // 0x1400e9340

    // 0x1400e9190: frees both arrays (the members' ~Array, after the empty body)
    ~ConsumeProduce() override;                                     // [0]  0x14002a190
    void  Persist(IO::Stream& stream) override;                           // [3]  0x1400e9020
    int   Execute(CitizenComponent** entityIBP, System::Time* context) override;  // [4]  0x1400e7840
    int   OnComplete(CitizenComponent** param2, ComponentEntity** param3, int arg3) override;  // [6]  0x1400e77c0
    int   OnCancel(CitizenComponent** param2, ComponentEntity** param3, int arg3) override;    // [7]  0x1400e7780
    int   Cleanup() override;                                       // [8]  0x1400e8f30
    int   Validate() override;                          // [9]  0x1400e7530
    Profession** GetTarget(Profession** retbuf) override;                         // [10] 0x1400e8fb0
    int   GetPriority() override;                                   // [11] 0x14002a0f0
    void  Slot12(int version) override;                                      // [12] 0x1400e9010 (own RET 0)

    // --- ConsumeProduce fields (+0x48) ---
    uint8_t  _outputBlocked;           // +0x48: bool (serialized)
    uint8_t  _pad49[3];          // +0x49: padding
    int32_t  _completedCycles;           // +0x4C: int (serialized)
    uint32_t _notifyComponentType;           // +0x50: Type id (zeroed, serialized through Object::Type::Serialize; retail
                                 //        reads it only as a dword copy, 0x1400e8d00)
    int32_t  _notifyMessage;           // +0x54: int (serialized)
    // Both arrays are real members: the ctor 0x1400e9340 zeroes +0x58 / +0x60 / +0x68 /
    // +0x70 as four qwords (0x1400e939d..0x1400e93a9); the dtor 0x1400e9190 restamps,
    // MemFrees +0x68 and zeroes +0x68 / +0x70, then MemFrees +0x58 and zeroes +0x58 /
    // +0x60 (0x1400e91b5..0x1400e91d7): ~Array in reverse declaration order.
    // +0x58: the citizens on the work slots (Persist 0x1400e9020 validates each element
    // against CitizenComponent); grows through 0x1400032e0 (stride 8, the new tail ZEROED,
    // no element destroy): kSpec_PtrZero, the row the AsArray<Ptr8> view reached.
    using CitizenRefList = Collection::Array<Object::InternalPtr<CitizenComponent>, Collection::DefaultAllocator,
                                             Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    CitizenRefList _slotCitizens;     // +0x58 (count +0x60, capacity +0x64)
    // +0x68: per output position WorkSpotState (Persist's int-array overload); grows through
    // 0x1400021d0 (stride 4, the new tail left as found): Array<int32_t>'s trait row.
    Collection::Array<int32_t> _spotStates;   // +0x68 (count +0x70, capacity +0x74)
};

static_assert(sizeof(ConsumeProduce) == 0x78, "ConsumeProduce is 0x78 (retail create MemAlloc)");
static_assert(offsetof(ConsumeProduce, _pad49) == 0x49, "ConsumeProduce::_pad49");
static_assert(alignof(ConsumeProduce) == 8, "ConsumeProduce is 8-aligned");
static_assert(offsetof(ConsumeProduce, _outputBlocked) == 0x48, "ConsumeProduce +0x48");
static_assert(offsetof(ConsumeProduce, _completedCycles) == 0x4c, "ConsumeProduce +0x4c");
static_assert(offsetof(ConsumeProduce, _notifyComponentType) == 0x50, "ConsumeProduce +0x50");
static_assert(offsetof(ConsumeProduce, _notifyMessage) == 0x54, "ConsumeProduce +0x54");
static_assert(offsetof(ConsumeProduce, _slotCitizens) == 0x58, "ConsumeProduce +0x58");
static_assert(offsetof(ConsumeProduce, _slotCitizens) + offsetof(ConsumeProduce::CitizenRefList, _size) == 0x60, "ConsumeProduce +0x60");
static_assert(offsetof(ConsumeProduce, _slotCitizens) + offsetof(ConsumeProduce::CitizenRefList, _capacity) == 0x64, "ConsumeProduce +0x64");
static_assert(offsetof(ConsumeProduce, _spotStates) == 0x68, "ConsumeProduce +0x68");
static_assert(offsetof(ConsumeProduce, _spotStates) + offsetof(Collection::Array<int32_t>, _size) == 0x70, "ConsumeProduce +0x70");
static_assert(offsetof(ConsumeProduce, _spotStates) + offsetof(Collection::Array<int32_t>, _capacity) == 0x74, "ConsumeProduce +0x74");
