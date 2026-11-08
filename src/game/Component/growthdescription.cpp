// src/game/Component/growthdescription.cpp
// GrowthDescription (entity description block) — plant-growth parameters
// (temperature / soil / seasons).
// Extends Object::External (0x20 base). Vtable 0x140163610. Parent "Description".
// Size 0x68. GetVersion=3.
//
// Factory 0x14014f110-area (MemAlloc 0x68 + ctor sub_0b50). Ctor defaults:
//   float @ +0x20 = -1.0f      (0xbf800000)
//   int   @ +0x24 = 0
//   float @ +0x28 = 1.0f
//   bool  @ +0x2c = 0
//   (8)   @ +0x30 = 0
//   bool  @ +0x38 = 0
//   float @ +0x3c = 1.0f
//   float @ +0x40 = 1.0f
//   bool  @ +0x44 = 0
//   (8)   @ +0x48 = 0
//   (8)   @ +0x50 = 0
//   int   @ +0x58 = 3
//   int   @ +0x5c = 3   (8-byte store -> +0x60=0)
//   bool  @ +0x64 = 0
// Serialize (vtable +0x28, 0x140120980), stream order (note non-linear field order):
//   float@+0x20, float@+0x24, float@+0x28, bool@+0x2c, float@+0x30, float@+0x34,
//   bool@+0x38, float@+0x3c, float@+0x40, bool@+0x44, int@+0x4c, int@+0x50,
//   int@+0x48, float@+0x54, int@+0x58, int@+0x5c, float@+0x60, bool@+0x64
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (GrowthDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140163610) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/GrowthDescription.h"
#include "Component/GrowthComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>

// ctor (inlined into create 0x140034b70 / createInPlace 0x140034bb0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
GrowthDescription::GrowthDescription()
    : Description(),
      _maxGrowth(-1.0f),
      _maxGrowthTolerance(0.0f),
      _growthInMonths(1.0f),
      _temperatureDependent(false),
      _growthTemp(0.0f),
      _growthTempRange(0.0f),
      _soilDependent(false),
      _soilQualityChangeOnRemove(1.0f),
      _soilQualityChangeOnDecay(1.0f),
      _autoSeed(false),
      _seedChance(0),
      _seedDistance(0),
      _seedTimeMonths(0),
      _growthForSeeding(0.0f),
      _maxNeighbors(3),
      _maxNeighborsSelf(3),
      _minScale(0.0f),
      _zScale(false) {}

// [5] 0x140120980: stream order as retail.
bool GrowthDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_maxGrowth, nullptr);
    ser.Serialize(_maxGrowthTolerance, nullptr);
    ser.Serialize(_growthInMonths, nullptr);
    ser.Serialize(_temperatureDependent, nullptr);
    ser.Serialize(_growthTemp, nullptr);
    ser.Serialize(_growthTempRange, nullptr);
    ser.Serialize(_soilDependent, nullptr);
    ser.Serialize(_soilQualityChangeOnRemove, nullptr);
    ser.Serialize(_soilQualityChangeOnDecay, nullptr);
    ser.Serialize(_autoSeed, nullptr);
    ser.Serialize(_seedDistance, nullptr);
    ser.Serialize(_seedTimeMonths, nullptr);
    ser.Serialize(_seedChance, nullptr);
    ser.Serialize(_growthForSeeding, nullptr);
    ser.Serialize(_maxNeighbors, nullptr);
    ser.Serialize(_maxNeighborsSelf, nullptr);
    ser.Serialize(_minScale, nullptr);
    ser.Serialize(_zScale, nullptr);
    return true;
}

// [6] 0x140034bc0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* GrowthDescription::Reconstruct() {
    this->~GrowthDescription();
    return new (static_cast<void*>(this)) GrowthDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void GrowthDescription::Destruct() { this->~GrowthDescription(); }

// [8] 0x140034be0: GrowthComponent's type id through the hidden return slot.
Object::Type* GrowthDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = GrowthComponent::TypeId();
    return ret;
}

// [9] 0x140034bb0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* GrowthDescription::Construct() {
    return new (static_cast<void*>(this)) GrowthDescription();
}

// create 0x140034b70: MemAlloc(0x68, 8), then the (inlined) ctor.
::Object::Base* GrowthDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(GrowthDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) GrowthDescription());
}
// createInPlace 0x140034bb0.
::Object::Base* GrowthDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) GrowthDescription());
}

DEFINE_EXE_TYPE(GrowthDescription, "Description")
