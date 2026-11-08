// src/game/Component/fruitdescription.cpp
// FruitDescription (entity description block) — fruit/produce parameters.
// Extends Object::External (0x20 base). Vtable 0x1401594d0. Parent "Description".
// Size 0x38. GetVersion=1.
//
// Factory 0x14014ac80-area (MemAlloc 0x38 + vtable + defaults). Defaults:
//   float @ +0x20 = 1.0f  (0x3f800000, 8-byte store -> +0x24=0)
//   int   @ +0x28 = 0
//   (8)   @ +0x30 = 0   (ExternalPtr, null)
// Serialize (vtable +0x28, 0x14004a1e0), stream order:
//   float @ +0x20
//   float @ +0x24
//   float @ +0x28
//   ExternalPtr @ +0x30  (FUN_140002340 = fn_ExternalPtr_BuildSerialize)
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (FruitDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x1401594d0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/FruitDescription.h"
#include "Component/FruitComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x140005980 / createInPlace 0x1400059f0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
FruitDescription::FruitDescription()
    : Description(),
      _ripenSeasons(1.0f),
      _optimalTemp(0.0f),
      _tempRange(0.0f),
      _rawMaterial(nullptr) {}

// [5] 0x14004a1e0: stream order as retail.
bool FruitDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_ripenSeasons, nullptr);
    ser.Serialize(_optimalTemp, nullptr);
    ser.Serialize(_tempRange, nullptr);
    ::Object::SerializeExternal<ComponentDescription>(ser, _rawMaterial);
    return true;
}

// [6] 0x140005a50: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* FruitDescription::Reconstruct() {
    this->~FruitDescription();
    return new (static_cast<void*>(this)) FruitDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void FruitDescription::Destruct() { this->~FruitDescription(); }

// [8] 0x140005ab0: FruitComponent's type id through the hidden return slot.
Object::Type* FruitDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = FruitComponent::TypeId();
    return ret;
}

// [9] 0x1400059f0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* FruitDescription::Construct() {
    return new (static_cast<void*>(this)) FruitDescription();
}

// create 0x140005980: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* FruitDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FruitDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) FruitDescription());
}
// createInPlace 0x1400059f0.
::Object::Base* FruitDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) FruitDescription());
}

DEFINE_EXE_TYPE(FruitDescription, "Description")
