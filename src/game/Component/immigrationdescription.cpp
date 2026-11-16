// src/game/Component/immigrationdescription.cpp
// ImmigrationDescription (entity description block) — immigration/arrival parameters.
// Extends Object::External (0x20 base). Vtable 0x140159bc0. Parent "Description".
// Size 0x68. GetVersion=2.
//
// Factory 0x14014b000-area (MemAlloc 0x68 + ctor sub_e9a0). Ctor defaults:
//   ExternalPtr @ +0x20 = 0
//   int         @ +0x28 = 0x50 (80)
//   External*-array @ +0x30 = 0 (count @ +0x38, cap @ +0x3c)
//   float @ +0x40 = 0.05  (0x3d4ccccd),  float @ +0x44 = 0.15 (0x3e19999a)
//   float @ +0x48 = 24.0  (0x41c00000),  float @ +0x4c = 48.0 (0x42400000)
//   int   @ +0x50 = 1,  int @ +0x54 = 3,  int @ +0x58 = 7,  int @ +0x5c = 10
//   float @ +0x60 = 1.0,  int @ +0x64 = 6
// Serialize (vtable +0x28, 0x14004e7f0), stream order:
//   ExternalPtr @ +0x20  (FUN_140002340 = fn_ExternalPtr_BuildSerialize)
//   int         @ +0x28
//   int count   @ +0x38 -> resize +0x30 array -> per-element External FindOrLoad
//                          (FUN_1400049c0 == Desc_LoadExternalArray)
//   float @ +0x40,  float @ +0x44,  float @ +0x48,  float @ +0x4c
//   int   @ +0x50,  int   @ +0x54
//   float @ +0x60
//   int   @ +0x64
// GetVersion (vtable +0x18, 0x14004e930): mov eax,2; ret.
// Decompiled READ-ONLY from Application-x64.exe (Foundation C wave 2).
//
// ENDGAME B.3.8 K1: a REAL class (ImmigrationDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140159bc0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/ImmigrationDescription.h"
#include "Component/ImmigrationComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "Component/ComponentDescription.h"   // the SerializeExternalArray<ComponentDescription> instantiation
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/exe_bodies.h"
#include <cstdint>
#include <new>
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)



// ctor (inlined into create 0x140007a80 / createInPlace 0x140007ac0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
ImmigrationDescription::ImmigrationDescription()
    : Description(),
      _nomad(nullptr),
      _minPopulation(0x50),
      _requiredBuildings(nullptr),
      _requiredBuildingCount(0),
      _requiredBuildingCap(0),
      _groupFractionMin(0.05f),
      _groupFractionMax(0.15f),
      _waitMonths(24.0f),
      _waitRandomMonths(48.0f),
      _arrivalMonthFirst(1),
      _arrivalMonthLast(3),
      _leaveMonth(7),
      _spawnEdgeDistance(10),
      _diseaseChance(1.0f),
      _diseaseMonths(6) {}

// dtor (slot 0 0x140007b00 minus the stamps): frees the External* array buffer.
// The compiler then chains ~Description -> ~External.
ImmigrationDescription::~ImmigrationDescription() {
    if (_requiredBuildings) System::MemoryAllocator::MemFree(_requiredBuildings);
    _requiredBuildings = nullptr;
}

// [5] 0x14004e7f0: stream order as retail.
bool ImmigrationDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ::Object::SerializeExternal<ComponentDescription>(ser, _nomad);
    ser.Serialize(_minPopulation, nullptr);
    // External*-array @ +0x30 (count @ +0x38, cap @ +0x3c, stride 8)
    ser.Serialize(_requiredBuildingCount, nullptr);
    int n = _requiredBuildingCount;
    if (static_cast<int>(ser.GetDirection()) == 0 && n > 0 && System::MemoryAllocator::_singleton) {
        ComponentDescription** buf =
            static_cast<ComponentDescription**>(System::MemoryAllocator::MemAlloc((uint64_t)n * 8, 8));
        _requiredBuildings = buf;
        _requiredBuildingCap = n;
        if (buf) for (int i = 0; i < n; ++i) buf[i] = nullptr;
    }
    // 0x14004e875 CALL 0x1400049c0: the ComponentDescription instantiation (cell 0x1401c7318).
    Object::SerializeExternalArray<ComponentDescription>(ser, n, _requiredBuildings);
    ser.Serialize(_groupFractionMin, nullptr);
    ser.Serialize(_groupFractionMax, nullptr);
    ser.Serialize(_waitMonths, nullptr);
    ser.Serialize(_waitRandomMonths, nullptr);
    ser.Serialize(_arrivalMonthFirst, nullptr);
    ser.Serialize(_arrivalMonthLast, nullptr);
    ser.Serialize(_diseaseChance, nullptr);
    ser.Serialize(_diseaseMonths, nullptr);
    return true;
}

// [6] 0x140007ad0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* ImmigrationDescription::Reconstruct() {
    this->~ImmigrationDescription();
    return new (static_cast<void*>(this)) ImmigrationDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void ImmigrationDescription::Destruct() { this->~ImmigrationDescription(); }

// [8] 0x140007af0: ImmigrationComponent's type id through the hidden return slot.
Object::Type* ImmigrationDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = ImmigrationComponent::TypeId();
    return ret;
}

// [9] 0x140007ac0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* ImmigrationDescription::Construct() {
    return new (static_cast<void*>(this)) ImmigrationDescription();
}

// create 0x140007a80: MemAlloc(0x68, 8), then the (inlined) ctor.
::Object::Base* ImmigrationDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ImmigrationDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ImmigrationDescription());
}
// createInPlace 0x140007ac0.
::Object::Base* ImmigrationDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ImmigrationDescription());
}

DEFINE_EXE_TYPE(ImmigrationDescription, "Description")
