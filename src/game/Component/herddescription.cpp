// src/game/Component/herddescription.cpp
// HerdDescription (entity description block) — wild-animal herd parameters.
// Extends Object::External (0x20 base). Vtable 0x140163700. Parent "Description".
// Size 0x40. GetVersion=2.
//
// Factory 0x14014f190-area (MemAlloc 0x40 + vtable + zero). Serialize (vtable
// +0x28, 0x1401221e0), stream order:
//   ExternalPtr @ +0x20  (FUN_140002340 = fn_ExternalPtr_BuildSerialize)
//   int   @ +0x28,  int   @ +0x2c
//   float @ +0x30,  float @ +0x34
//   int   @ +0x38,  float @ +0x3c
// Decompiled READ-ONLY from Application-x64.exe.
//
// Part of the PopulationDeer vertical slice (Foundation C keystone).
//
// ENDGAME B.3.8 K1: a REAL class (HerdDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140163700) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/HerdDescription.h"
#include "Component/HerdComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include <cstdint>
#include <new>
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x140034d30 / createInPlace 0x140034d90):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
HerdDescription::HerdDescription()
    : Description(),
      _animal(nullptr),
      _maxSize(0),
      _minSize(0),
      _grazeMinutes(0.0f),
      _grazeRandomMinutes(0.0f),
      _spread(0),
      _growMinutes(0.0f) {}

// [5] 0x1401221e0: stream order as retail.
bool HerdDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ::Object::SerializeExternal<ComponentDescription>(ser, _animal);
    ser.Serialize(_maxSize, nullptr);
    ser.Serialize(_minSize, nullptr);
    ser.Serialize(_grazeMinutes, nullptr);
    ser.Serialize(_grazeRandomMinutes, nullptr);
    ser.Serialize(_spread, nullptr);
    ser.Serialize(_growMinutes, nullptr);
    return true;
}

// [6] 0x140034de0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* HerdDescription::Reconstruct() {
    this->~HerdDescription();
    return new (static_cast<void*>(this)) HerdDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void HerdDescription::Destruct() { this->~HerdDescription(); }

// [8] 0x140034e30: HerdComponent's type id through the hidden return slot.
Object::Type* HerdDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = HerdComponent::TypeId();
    return ret;
}

// [9] 0x140034d90: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* HerdDescription::Construct() {
    return new (static_cast<void*>(this)) HerdDescription();
}

// create 0x140034d30: MemAlloc(0x40, 8), then the (inlined) ctor.
::Object::Base* HerdDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HerdDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HerdDescription());
}
// createInPlace 0x140034d90.
::Object::Base* HerdDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HerdDescription());
}

DEFINE_EXE_TYPE(HerdDescription, "Description")
