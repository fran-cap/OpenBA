// src/game/Component/floordescription.cpp
// FloorDescription (entity description block) — floor parameters.
// Extends Object::External (0x20 base). Vtable 0x1401633e0. Parent "Description".
// Size 0x30. GetVersion=1.
//
// Factory 0x14014f010-area (MemAlloc 0x30 + vtable + defaults). Defaults:
//   (8)  @ +0x20 = 0   (ExternalPtr, null)
//   bool @ +0x28 = 0
// Serialize (vtable +0x28, 0x14011f680), stream order:
//   ExternalPtr @ +0x20  (exe helper FUN_14000b7b0 — the reference-mode
//     External-pointer serialize; distinct from FUN_140002340)
//   bool @ +0x28
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (FloorDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x1401633e0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/FloorDescription.h"
#include "Component/FloorComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "picking/PickingMesh.h"   // B8 RT2: ::Object::SerializeExternal<Picking::PickingMesh> (the downcast needs it complete)

// ctor (inlined into create 0x140034660 / createInPlace 0x1400346d0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
FloorDescription::FloorDescription()
    : Description(),
      _mesh(nullptr),
      _addOnCreate(false) {}

// [5] 0x14011f680: stream order as retail.
bool FloorDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    // +0x20 ExternalPtr through exe 0x14000b7b0 (::Object::SerializeExternal<Picking::PickingMesh>).
    ::Object::SerializeExternal<Picking::PickingMesh>(ser, _mesh);
    ser.Serialize(_addOnCreate, nullptr);
    return true;
}

// [6] 0x140034720: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* FloorDescription::Reconstruct() {
    this->~FloorDescription();
    return new (static_cast<void*>(this)) FloorDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void FloorDescription::Destruct() { this->~FloorDescription(); }

// [8] 0x140034780: FloorComponent's type id through the hidden return slot.
Object::Type* FloorDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = FloorComponent::TypeId();
    return ret;
}

// [9] 0x1400346d0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* FloorDescription::Construct() {
    return new (static_cast<void*>(this)) FloorDescription();
}

// create 0x140034660: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* FloorDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FloorDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) FloorDescription());
}
// createInPlace 0x1400346d0.
::Object::Base* FloorDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) FloorDescription());
}

DEFINE_EXE_TYPE(FloorDescription, "Description")
