// src/game/Component/citizencomponent_serialize.cpp
// Slot-3 (+0x18) stream Serialize for CitizenComponent.
//
//   CitizenComponent::Serialize   retail 0x140114160   object size 0x290
//
// Save/load wave (2026-08-27). Byte-faithful port of the 306-instruction retail
// body. Field list (Serializer version 3):
//   +0x28  System::String            name
//   +0x38  int                       (unnamed)
//   +0x3c..0x50  6 floats            (hunger/thirst/etc. accumulators)
//   +0x54  uint                      (flag bits)
//   +0x58  Time                      work timer
//   +0x60  Time                      (unnamed timer)
//   +0x68  ExternalPtr<Disease?>     helper_0790, validated 0x1401c5948
//   +0x70  Time
//   +0x78  Time
//   +0x80  float
//   +0x88  uint                      (flag bits)
//   +0x90  Time
//   +0x98/+0xa0  ExternalPtr<ComponentDescription>[count]  action-stack-like array,
//                load-side resize via FUN_140001bb0 (ported below as
//                ResizeCitizenExternalArray), elements via the ALREADY-PORTED
//                Reimpl_ExternalPtrArray_Serialize (serialize_externals_array.cpp;
//                same exe address FUN_1400049c0, validated ComponentDescription).
//   +0x1f0 ExternalPtr<Profession?>  helper_140018020 (called twice; same body,
//                validated TypeInfo_Profession 0x1401c5a98)
//   +0xac  int
//   +0x1d8 int
//   +0xa8  int                       (version > 0)
//   +0x1e4 int
//   +0x1e8 Time
//   +0x1e0 int                       (version > 1)
//   +0xb0  MeshInstance              via the Encoding-object virtual call retail
//                emits inline; equivalent to the already-resolved
//                Graphics::MeshInstance::Serialize dispatch slot 0x140152990
//                (see modelcomponent.cpp) called as (self+0xb0, ser).
//   +0x30  int
//   +0x238 IBP<ComponentEntity>      validated 0x1401c5d70
//   +0x240 int
//   +0x250/+600(0x258)/+0x260/+0x268/+0x270  IBP<CitizenComponent>  each validated
//                0x1401c7158 (CitizenComponent's own type id — GRADUATED default-on,
//                see project_citid_graduated memory)
//   +0x248 IBP<ComponentEntity>      validated 0x1401c5d70
//   +0x1f8 ExternalPtr<Profession?>  helper_140018020 again
//   +0x208 IBP                      validated 0x1401c3f40 (same raw cell used for
//                +0x228/+0x230 below — Ghidra emits the identical dword for all
//                three sites; resolved once, used generically per the house
//                pattern rather than guessing a C++ name for it)
//   +0x210 int
//   +0x218 Time                     (version > 2)
//   +0x220 bool
//   +0x228 IBP                      validated 0x1401c3f40
//   +0x230 IBP                      validated 0x1401c3f40
//   +0x278 IBP<CharacterComponent>  validated 0x1401c7078
//   +0x280 IBP<StatusIconComponent> validated 0x1401c7e08
//   +0x288 ExternalPtr<CitizenDescription> helper_0990, validated 0x1401c7190
//   +0x34  int
//
// Sub-callees:
//   * Component::Persist               — called qualified (component.cpp)
//   * ExternalPtrSerializeGeneric       — already ported (Game/serialize_externals.cpp),
//                                          reused for helper_0790/helper_0990/helper_140018020
//                                          (all three are the SAME ExternalPtr<T>::Serialize
//                                          template body, differing only by validation type)
//   * Reimpl_ExternalPtrArray_Serialize — already ported (serialize_externals_array.cpp),
//                                          exact match for FUN_1400049c0
//   * FUN_140001bb0 (array-header resize) — ported below (ResizeCitizenExternalArray);
//                                          not previously registered anywhere in the repo.
//   * Graphics::MeshInstance::Serialize — dispatch slot 0x140152990, already resolved
//                                          in modelcomponent.cpp; resolved again locally
//                                          here (same house pattern as elsewhere).
// All scalar/Time/Bool/IBP primitives come from the shared engine_funcs.h globals
// already initialized at startup.

// graphics/*.h FIRST: they pull src/engine/volume/Volume.h, whose global-scope
// `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once <windows.h> has
// been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Mesh.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "system/String.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include "io/Stream.h"       // B4 P2 CIT-4: Stream::GetDirection
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/CitizenComponent.h"
#include "Game/Disease.h"                       // ENDGAME B.3.9 C1: 0x1401c5948 cell class
#include "Game/Profession.h"                    // ENDGAME B.3.9 C1: 0x1401c5a98 cell class
#include "Entity/ComponentEntity.h"             // ENDGAME B.3.9 C1: 0x1401c5d70 cell class
#include "Component/AttendanceComponent.h"      // ENDGAME B.3.9 C1: 0x1401c3f40 cell class
#include "Component/CharacterComponent.h"       // ENDGAME B.3.9 C1: 0x1401c7078 cell class
#include "Component/CitizenDescription.h"       // ENDGAME B.3.9 C1: 0x1401c7190 cell class
#include "Component/StatusIconComponent.h"      // ENDGAME B.3.9 C1: 0x1401c7e08 cell class
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternalArray<ComponentDescription> (the downcast needs it complete)


namespace {

// FUN_140001bb0 — resize an 8-byte-element array given its {data@0, count@8(int),
// capacity@0xc(int)} header (here the citizen's consumedTypes triple), preserving
// existing elements and zero-filling new ones. Byte-faithful port (retail grows by
// 3x on a +1 append, matching the std::vector-ish growth policy used throughout
// this codebase's other resize helpers). Not previously ported/registered anywhere
// else in the repo.
void ResizeCitizenExternalArray(ComponentDescription**& data, int32_t& count,
                                int32_t& capacity, int newCount) {
    int oldCount = count;
    if (newCount == oldCount) return;

    ComponentDescription** oldPtr = data;
    count = newCount;

    if (capacity < newCount) {
        capacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0)
            capacity = newCount * 3;
        data = static_cast<ComponentDescription**>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(capacity) * 8, 8));
    }

    int startIdx = (oldPtr == data) ? oldCount : 0;
    ComponentDescription** newArr = data;
    ComponentDescription** oldArr = oldPtr;
    for (int i = startIdx; i < newCount; ++i) {
        if (!newArr) break;
        if (i < oldCount) {
            newArr[i] = oldArr ? oldArr[i] : nullptr;
        } else {
            newArr[i] = nullptr;
        }
    }

    if (oldPtr != data)
        System::MemoryAllocator::MemFree(oldPtr);
}

// One InternalPtr<T> member (CitizenComponent.h): InternalBasePtr::Serialize, then
// the loaded object is nulled when it is not a `tid`.
void SerializeValidatedIBP(Object::InternalBasePtr* ibp, IO::Serializer& ser, uint32_t tid) {
    ibp->Serialize(ser);
    Object::Internal* obj = ibp->Get();
    const bool checked = (obj && tid);
    const bool derived = checked ? Object::IsDerivedFromNullSafe(obj, tid) : true;
    if (checked && !derived)
        *ibp = Object::InternalBasePtr();

}

} // namespace

// [3] 0x140114160.
// The System::String / Collection::Array members are handed to their serializers
// by name (CitizenComponent.h).
void CitizenComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0

    alignas(8) uint8_t ser[24];
    IO::Serializer* s = ::new (static_cast<void*>(ser)) IO::Serializer(stream, 3);

    name.Serialize(*s);
    s->Serialize(gender, nullptr);
    s->Serialize(age, nullptr);
    s->Serialize(maxAge, nullptr);
    s->Serialize(hunger, nullptr);
    s->Serialize(coldness, nullptr);
    s->Serialize(pregnancyTimer, nullptr);
    s->Serialize(health, nullptr);
    s->Serialize(statusFlags, nullptr);
    foodTimer.Serialize(*s);
    field_0x60.Serialize(*s);

    ::Object::SerializeExternal<Disease>(*s, disease);   // 0x140030790 (B8 RT2)

    field_0x70.Serialize(*s);
    field_0x78.Serialize(*s);
    s->Serialize(happiness, nullptr);
    s->Serialize(happinessFlags, nullptr);
    happinessTimer.Serialize(*s);

    int arrCount = consumedTypes._size;
    s->Serialize(arrCount, nullptr);
    if (stream.GetDirection() == IO::kReading)   // Stream +0x08 == 0
        ResizeCitizenExternalArray(consumedTypes._data, consumedTypes._size, consumedTypes._capacity, arrCount);
    ::Object::SerializeExternalArray<ComponentDescription>(*s, arrCount, consumedTypes._data);   // 0x1400049c0

    ::Object::SerializeExternal<Profession>(*s, profession);   // 0x140018020 (B8 RT2)

    s->Serialize(field_0xAC, nullptr);
    s->Serialize(toolId, nullptr);
    {
        int v = s->GetVersion();
        if (v > 0) s->Serialize(field_0xA8, nullptr);
    }
    s->Serialize(clothingLevel, nullptr);
    clothingTimer.Serialize(*s);
    {
        int v = s->GetVersion();
        if (v > 1) s->Serialize(field_0x1E0, nullptr);
    }

    // +0xb0 is a PLAIN UINT, not a MeshInstance (save/load wave 2026-08-28).
    // Retail's inlined `CALL [encodingVtbl+0x98]` here is Serialize(uint&) —
    // the identical slot the +0x54/+0x88 uints above go through; the real
    // MeshInstance sub-object starts at +0xB8 and this stream never touches
    // it. The previous MeshInstance-dispatch call fed a raw uint's address as
    // a MeshInstance "this" and corrupted the round-trip from +0xb0 onward
    // (wrong citizen clothing after load was the visible symptom).
    s->Serialize(reinterpret_cast<uint32_t&>(lastErrand), nullptr);

    s->Serialize(field_0x30, nullptr);

    SerializeValidatedIBP(&home, *s, ComponentEntity::TypeId());
    s->Serialize(homeSlot, nullptr);
    SerializeValidatedIBP(&spouse, *s, CitizenComponent::TypeId());
    SerializeValidatedIBP(&_parent, *s, CitizenComponent::TypeId());
    SerializeValidatedIBP(&_firstChild, *s, CitizenComponent::TypeId());
    SerializeValidatedIBP(&_nextSibling, *s, CitizenComponent::TypeId());
    SerializeValidatedIBP(&_prevSibling, *s, CitizenComponent::TypeId());
    SerializeValidatedIBP(&workplace, *s, ComponentEntity::TypeId());

    ::Object::SerializeExternal<Profession>(*s, pendingProfession);   // 0x140018020 (B8 RT2)

    SerializeValidatedIBP(&school, *s, AttendanceComponent::TypeId());
    s->Serialize(education, nullptr);
    {
        int v = s->GetVersion();
        if (v > 2) schoolTime.Serialize(*s);
    }
    s->Serialize(field_0x220, nullptr);
    SerializeValidatedIBP(&careBuilding, *s, AttendanceComponent::TypeId());
    SerializeValidatedIBP(&field_0x230, *s, AttendanceComponent::TypeId());
    SerializeValidatedIBP(&character, *s, CharacterComponent::TypeId());
    SerializeValidatedIBP(&statusIcon, *s, StatusIconComponent::TypeId());

    ::Object::SerializeExternal<CitizenDescription>(*s, description);   // 0x140030990 (B8 RT2)

    s->Serialize(field_0x34, nullptr);


    s->~Serializer();
}
