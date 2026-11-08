// src/game/Component/createroaddescription.cpp
// Clean reimplementation of CreateRoadDescription (entity description block).
//
// CreateRoadDescription (0x30 bytes) — road-drawing placement tool.
// Extends Object::External (0x20 base). Vtable at 0x140162df0. Parent "Description".
//   +0x20: bool         (default 0)
//   +0x28: ExternalPtr  (created-resource description ref, default null)
//
// Factory  : MemAlloc 0x30 + External ctor + vtable + field defaults.
// Serialize (vtable +0x28, 0x140119c80), stream order:
//   bool        @ +0x20
//   ExternalPtr @ +0x28   (fn_ExternalPtr_Serialize3)
// GetVersion (vtable +0x18, 0x140119ce0): mov eax,2; ret -> version 2.
// Dtor (vtable +0x00, 0x140032e50): External::~External + free.
//
// Field knowledge harvested from engine/reimpl/createroaddescription.cpp.
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (CreateRoadDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140162df0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/CreateRoadDescription.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x140032d30 / createInPlace 0x140032da0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
CreateRoadDescription::CreateRoadDescription()
    : Description(),
      _index(false),
      _created(nullptr) {}

// [5] 0x140119c80: stream order as retail.
bool CreateRoadDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_index, nullptr);
    ::Object::SerializeExternal<ComponentDescription>(ser, _created);
    return true;
}

// [6] 0x140032df0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* CreateRoadDescription::Reconstruct() {
    this->~CreateRoadDescription();
    return new (static_cast<void*>(this)) CreateRoadDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void CreateRoadDescription::Destruct() { this->~CreateRoadDescription(); }

// [8] 0x140004950 (shared by 19 tables): `MOV dword [RDX],0; MOV RAX,RDX; RET` —
// a data-only description builds no component.
Object::Type* CreateRoadDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = 0;
    return ret;
}

// [9] 0x140032da0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* CreateRoadDescription::Construct() {
    return new (static_cast<void*>(this)) CreateRoadDescription();
}

// create 0x140032d30: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* CreateRoadDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CreateRoadDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CreateRoadDescription());
}
// createInPlace 0x140032da0.
::Object::Base* CreateRoadDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CreateRoadDescription());
}

DEFINE_EXE_TYPE(CreateRoadDescription, "Description")
