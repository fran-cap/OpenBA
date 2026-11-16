// src/game/Component/quarrydescription.cpp
// QuarryDescription (entity description block) — quarry extraction parameters.
// Extends Object::External (0x20 base). Vtable 0x14015a510. Parent "Description".
// Size 0x30. GetVersion=4 (slot +0x18 @ 0x140055390: mov eax,4; ret).
//
// Ctor defaults (QuarryDescription_Factory):
//   ExternalPtr @ +0x20 = 0        (floor mesh ref)
//   float       @ +0x28 = -1.5  (0xbfc00000)  (min depth)
//   float       @ +0x2c = -7.0  (0xc0e00000)  (max depth)
// Serialize (vtable +0x28, 0x140055320), stream order:
//   float       @ +0x2c
//   float       @ +0x28
//   ExternalPtr @ +0x20  (FUN_14000b7b0 -> fn_ExternalPtr_BuildSerialize)
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (QuarryDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x14015a510) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/QuarryDescription.h"
#include "Component/QuarryComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include <cstdint>
#include <new>
#include "picking/PickingMesh.h"   // B8 RT2: ::Object::SerializeExternal<Picking::PickingMesh> (the downcast needs it complete)

// ctor (inlined into create 0x14000b430 / createInPlace 0x14000b4a0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
QuarryDescription::QuarryDescription()
    : Description(),
      _floorMesh(nullptr),
      _initialDepth(-1.5f),
      _maxDepth(-7.0f) {}

// [5] 0x140055320: stream order as retail.
bool QuarryDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_maxDepth, nullptr);
    ser.Serialize(_initialDepth, nullptr);
    ::Object::SerializeExternal<Picking::PickingMesh>(ser, _floorMesh);   // 0x14000b7b0
    return true;
}

// [6] 0x14000b500: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* QuarryDescription::Reconstruct() {
    this->~QuarryDescription();
    return new (static_cast<void*>(this)) QuarryDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void QuarryDescription::Destruct() { this->~QuarryDescription(); }

// [8] 0x14000b560: QuarryComponent's type id through the hidden return slot.
Object::Type* QuarryDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = QuarryComponent::TypeId();
    return ret;
}

// [9] 0x14000b4a0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* QuarryDescription::Construct() {
    return new (static_cast<void*>(this)) QuarryDescription();
}

// create 0x14000b430: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* QuarryDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(QuarryDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) QuarryDescription());
}
// createInPlace 0x14000b4a0.
::Object::Base* QuarryDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) QuarryDescription());
}

DEFINE_EXE_TYPE(QuarryDescription, "Description")
