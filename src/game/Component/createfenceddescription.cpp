// src/game/Component/createfenceddescription.cpp
// Clean reimplementation of CreateFencedDescription (entity description block).
//
// CreateFencedDescription (0x78 bytes) — fence/wall placement tool.
// Extends Object::External (0x20 base). Vtable at 0x1401591a0. Parent "Description".
//   +0x20: enum (int)
//   +0x28: ExternalPtr[6] (inline fixed array, 6 * 8 bytes -> +0x28..+0x58)
//   +0x58: int   +0x5c: int   +0x60: int   +0x64: int
//   +0x68: bool
//   +0x70: ExternalPtr (single)
//
// Factory  : MemAlloc 0x78 + External ctor + vtable + field defaults (zero).
// Serialize (vtable +0x28, 0x1400478f0 CreateFencedDescription_Slot5), stream order:
//   enum  @ +0x20  (encoding vtable +0x98, 3-arg variant: (encoding, field, 0))
//   ExternalRefArray(count=6) @ +0x28  (FUN_1400049c0 / fn_ExternalRefArray_Serialize)
//   int   @ +0x58
//   int   @ +0x5c
//   int   @ +0x60
//   int   @ +0x64
//   bool  @ +0x68
//   ExternalPtr @ +0x70  (FUN_140002340 / Reimpl_ExternalPtr_Serialize_StringTable)
// GetVersion (vtable +0x18, 0x1400479c0): mov eax,3; ret -> version 3.
// Dtor (vtable +0x00, 0x140004960): External::~External + free.
//
// NOTE: fn_ExternalRefArray_Serialize -> exe FUN_1400049c0, null in standalone until
// 0x1400049c0 is registered (REPORTED). param4 is a dead init -> nullptr safe.
//
// Field knowledge harvested from engine/reimpl/createfenceddescription.cpp.
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (CreateFencedDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x1401591a0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/CreateFencedDescription.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x1400048e0 / createInPlace 0x140004920):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
CreateFencedDescription::CreateFencedDescription()
    : Description(),
      _placeBits(0),
      _maxWidth(0),
      _maxHeight(0),
      _minWidth(0),
      _minHeight(0),
      _centerBuilding(false),
      _allowAndRemove(nullptr) {
    for (auto& r : _sections) r = nullptr;
}

// [5] 0x1400478f0: stream order as retail.
bool CreateFencedDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_placeBits, nullptr);
    ::Object::SerializeExternalArray<ComponentDescription>(ser, 6, _sections);   // ExtPtr[6] +0x28 (0x1400049c0)
    ser.Serialize(_maxWidth, nullptr);
    ser.Serialize(_maxHeight, nullptr);
    ser.Serialize(_minWidth, nullptr);
    ser.Serialize(_minHeight, nullptr);
    ser.Serialize(_centerBuilding, nullptr);
    ::Object::SerializeExternal<ComponentDescription>(ser, _allowAndRemove);
    return true;
}

// [6] 0x140004930: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* CreateFencedDescription::Reconstruct() {
    this->~CreateFencedDescription();
    return new (static_cast<void*>(this)) CreateFencedDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void CreateFencedDescription::Destruct() { this->~CreateFencedDescription(); }

// [8] 0x140004950 (shared by 19 tables): `MOV dword [RDX],0; MOV RAX,RDX; RET` —
// a data-only description builds no component.
Object::Type* CreateFencedDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = 0;
    return ret;
}

// [9] 0x140004920: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* CreateFencedDescription::Construct() {
    return new (static_cast<void*>(this)) CreateFencedDescription();
}

// create 0x1400048e0: MemAlloc(0x78, 8), then the (inlined) ctor.
::Object::Base* CreateFencedDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CreateFencedDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CreateFencedDescription());
}
// createInPlace 0x140004920.
::Object::Base* CreateFencedDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CreateFencedDescription());
}

DEFINE_EXE_TYPE(CreateFencedDescription, "Description")
