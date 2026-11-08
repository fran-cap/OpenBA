// src/game/Component/populationdescription.cpp
// PopulationDescription (entity description block) — wild-population spawner
// parameters (e.g. deer herds). Extends Object::External. Vtable 0x14015a3e0.
// Parent "Description". Size 0x58. GetVersion=3.
//
// Ctor defaults (PopulationDescription_sub_4940):
//   String @ +0x20 = 0 (empty)
//   int    @ +0x28 = 8,   int @ +0x2c = 0x20
//   enum   @ +0x30 = 0xe1f, enum @ +0x34 = 0
//   bool   @ +0x38 = 1
//   int    @ +0x3c = 4,   int @ +0x40 = 1,   int @ +0x44 = 1
//   bool   @ +0x48 = 0
//   ExternalPtr @ +0x50 = 0
// Serialize (vtable +0x28, 0x1400547e0), stream order:
//   int@0x28, int@0x2c, enum@0x30 (vtbl+0x98), enum@0x34 (vtbl+0x98),
//   bool@0x38, int@0x3c, int@0x40, int@0x44, bool@0x48,
//   ExternalPtr@0x50 (FUN_140002340), String@0x20.
// Decompiled READ-ONLY from Application-x64.exe.
//
// Part of the PopulationDeer vertical slice (Foundation C keystone).
//
// ENDGAME B.3.8 K1: a REAL class (PopulationDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x14015a3e0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/PopulationDescription.h"
#include "Component/PopulationComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include <cstdint>
#include <new>
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)
#include "Game/PathType.h"                    // PathBits::Walk (the default habitat)

// ctor (inlined into create 0x14000afb0 / createInPlace 0x14000aff0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
PopulationDescription::PopulationDescription()
    : Description(),
      _habitatBlock(8),
      _herdBlock(0x20),
      _habitatTerrain(Bits(PathBits::Walk)),
      _blockingTerrain(0),
      _blockedByEntity(true),
      _habitatDensity(4),
      _densityGrowth(1),
      _populationDecreasePerHarvest(1),
      _rescan(false),
      _herdTemplate(nullptr) {}

// [5] 0x1400547e0: stream order as retail.
bool PopulationDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_habitatBlock, nullptr);
    ser.Serialize(_herdBlock, nullptr);
    ser.Serialize(_habitatTerrain, nullptr);
    ser.Serialize(_blockingTerrain, nullptr);
    ser.Serialize(_blockedByEntity, nullptr);
    ser.Serialize(_habitatDensity, nullptr);
    ser.Serialize(_densityGrowth, nullptr);
    ser.Serialize(_populationDecreasePerHarvest, nullptr);
    ser.Serialize(_rescan, nullptr);
    ::Object::SerializeExternal<ComponentDescription>(ser, _herdTemplate);   // 0x140002340
    _name.Serialize(ser);
    return true;
}

// [6] 0x14000b000: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* PopulationDescription::Reconstruct() {
    this->~PopulationDescription();
    return new (static_cast<void*>(this)) PopulationDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void PopulationDescription::Destruct() { this->~PopulationDescription(); }

// [8] 0x14000b020: PopulationComponent's type id through the hidden return slot.
Object::Type* PopulationDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = PopulationComponent::TypeId();
    return ret;
}

// [9] 0x14000aff0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* PopulationDescription::Construct() {
    return new (static_cast<void*>(this)) PopulationDescription();
}

// create 0x14000afb0: MemAlloc(0x58, 8), then the (inlined) ctor.
::Object::Base* PopulationDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PopulationDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PopulationDescription());
}
// createInPlace 0x14000aff0.
::Object::Base* PopulationDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PopulationDescription());
}

DEFINE_EXE_TYPE(PopulationDescription, "Description")
