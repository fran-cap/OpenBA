// src/game/Component/livestockdescription.cpp
// LivestockDescription (entity description block) — livestock parameters
// (byproduct / createMonths).
// Extends Object::External (0x20 base). Vtable 0x140163a30. Parent "Description".
// Size 0x30. GetVersion=1.
//
// Factory 0x14014f310-area (MemAlloc 0x30 + vtable + defaults). Defaults:
//   (8)   @ +0x20 = 0    (ExternalPtr byproduct, null)
//   enum  @ +0x28 = 5
//   float @ +0x2c = 1.0f (0x3f800000)
// Serialize (vtable +0x28, 0x140123f60), stream order:
//   ExternalPtr @ +0x20  (FUN_140002340 = fn_ExternalPtr_BuildSerialize)
//   float @ +0x2c
//   enum  @ +0x28        (serializer-encoding vtable +0x98 dispatch, version-gated)
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (LivestockDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140163a30) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/LivestockDescription.h"
#include "Component/LivestockComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include <cstdint>
#include <new>
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x1400354e0 / createInPlace 0x140035550):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
LivestockDescription::LivestockDescription()
    : Description(),
      _byproduct(nullptr),
      _type(5),
      _productionInterval(1.0f) {}

// [5] 0x140123f60: stream order as retail.
bool LivestockDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 1);
    ::Object::SerializeExternal<ComponentDescription>(ser, _byproduct);
    ser.Serialize(_productionInterval, nullptr);
    if (ser.GetVersion() > 0) ser.Serialize(_type, nullptr);   // version-gated, as retail
    return true;
}

// [6] 0x1400355b0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* LivestockDescription::Reconstruct() {
    this->~LivestockDescription();
    return new (static_cast<void*>(this)) LivestockDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void LivestockDescription::Destruct() { this->~LivestockDescription(); }

// [8] 0x140035610: LivestockComponent's type id through the hidden return slot.
Object::Type* LivestockDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = LivestockComponent::TypeId();
    return ret;
}

// [9] 0x140035550: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* LivestockDescription::Construct() {
    return new (static_cast<void*>(this)) LivestockDescription();
}

// create 0x1400354e0: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* LivestockDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(LivestockDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) LivestockDescription());
}
// createInPlace 0x140035550.
::Object::Base* LivestockDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) LivestockDescription());
}

DEFINE_EXE_TYPE(LivestockDescription, "Description")
