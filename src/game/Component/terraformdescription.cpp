// src/game/Component/terraformdescription.cpp
// TerraformDescription (entity description block) — terraform/landscaping parameters.
// Extends Object::External (0x20 base). Vtable 0x14015a900. Parent "Description".
// Size 0x38. GetVersion=3.
//
// Factory 0x14014b840-area (MemAlloc 0x38). Ctor defaults: zero @ +0x20, +0x28, +0x30.
// Serialize (vtable +0x28, 0x1400576a0), stream order (interleaved ExtPtr/enum):
//   ExternalPtr @ +0x20  (FUN_14000b7b0 — single-ExternalPtr variant w/ type-check;
//                          stream-identical to FUN_140002340)
//   enum        @ +0x30  (serializer encoding vtable +0x98)
//   ExternalPtr @ +0x28  (FUN_14000b7b0)
//   enum        @ +0x34  (serializer encoding vtable +0x98)
// GetVersion (vtable +0x18, 0x140057730): mov eax,3; ret.
// Decompiled READ-ONLY from Application-x64.exe (Foundation C wave 2).
//
// ENDGAME B.3.8 K1: a REAL class (TerraformDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x14015a900) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/TerraformDescription.h"
#include "Component/TerraformComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include <cstdint>
#include <new>
#include "picking/PickingMesh.h"   // B8 RT2: ::Object::SerializeExternal<Picking::PickingMesh> (the downcast needs it complete)

// ctor (inlined into create 0x14000ccf0 / createInPlace 0x14000cd60):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
TerraformDescription::TerraformDescription()
    : Description(),
      _floorMesh(nullptr),
      _ceilingMesh(nullptr),
      _floorModifyBits(0),
      _ceilingModifyBits(0) {}

// [5] 0x1400576a0: stream order as retail.
bool TerraformDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ::Object::SerializeExternal<Picking::PickingMesh>(ser, _floorMesh);     // 0x14000b7b0
    ser.Serialize(_floorModifyBits, nullptr);
    ::Object::SerializeExternal<Picking::PickingMesh>(ser, _ceilingMesh);   // 0x14000b7b0
    ser.Serialize(_ceilingModifyBits, nullptr);
    return true;
}

// [6] 0x14000cdc0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* TerraformDescription::Reconstruct() {
    this->~TerraformDescription();
    return new (static_cast<void*>(this)) TerraformDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void TerraformDescription::Destruct() { this->~TerraformDescription(); }

// [8] 0x14000ce20: TerraformComponent's type id through the hidden return slot.
Object::Type* TerraformDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = TerraformComponent::TypeId();
    return ret;
}

// [9] 0x14000cd60: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* TerraformDescription::Construct() {
    return new (static_cast<void*>(this)) TerraformDescription();
}

// create 0x14000ccf0: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* TerraformDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TerraformDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) TerraformDescription());
}
// createInPlace 0x14000cd60.
::Object::Base* TerraformDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) TerraformDescription());
}

DEFINE_EXE_TYPE(TerraformDescription, "Description")
