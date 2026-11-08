// src/game/Component/fishingdescription.cpp
// FishingDescription (entity description block) — fishing dock parameters.
// Extends Object::External (0x20 base). Vtable 0x1401632e0. Parent "Description".
// Size 0x30. GetVersion=4 (slot +0x18 @ 0x14011f200: mov eax,4; ret).
//
// Ctor defaults (FishingDescription_Factory):
//   ExternalPtr @ +0x20 = 0      (fish material ref)
//   int/enum    @ +0x28 = 5
//   int/enum    @ +0x2c = 0x180  (384)
// Serialize (vtable +0x28, 0x14011f170) — Serializer ctor with version=1, stream order:
//   ExternalPtr @ +0x20  (FUN_140002340 -> fn_ExternalPtr_BuildSerialize)
//   enum        @ +0x2c  (serializer encoding vcall +0x98)
//   if (serializer.GetVersion() > 0): enum @ +0x28 (encoding vcall +0x98)
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (FishingDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x1401632e0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/FishingDescription.h"
#include "Component/FishingComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x1400343c0 / createInPlace 0x140034430):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
FishingDescription::FishingDescription()
    : Description(),
      _fish(nullptr),
      _resourceLimit(5),
      _terrainMask(0x180) {}

// [5] 0x14011f170: stream order as retail.
bool FishingDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 1);
    ::Object::SerializeExternal<ComponentDescription>(ser, _fish);
    ser.Serialize(_terrainMask, nullptr);
    if (ser.GetVersion() > 0) ser.Serialize(_resourceLimit, nullptr);   // version-gated, as retail
    return true;
}

// [6] 0x140034490: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* FishingDescription::Reconstruct() {
    this->~FishingDescription();
    return new (static_cast<void*>(this)) FishingDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void FishingDescription::Destruct() { this->~FishingDescription(); }

// [8] 0x1400344f0: FishingComponent's type id through the hidden return slot.
Object::Type* FishingDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = FishingComponent::TypeId();
    return ret;
}

// [9] 0x140034430: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* FishingDescription::Construct() {
    return new (static_cast<void*>(this)) FishingDescription();
}

// create 0x1400343c0: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* FishingDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FishingDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) FishingDescription());
}
// createInPlace 0x140034430.
::Object::Base* FishingDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) FishingDescription());
}

DEFINE_EXE_TYPE(FishingDescription, "Description")
