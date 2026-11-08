// src/game/Component/createbridgedescription.cpp
// Clean reimplementation of CreateBridgeDescription (entity description block).
//
// CreateBridgeDescription (0x60 bytes) — bridge-building placement tool.
// Extends Object::External (0x20 base). Vtable at 0x140162c30. Parent "Description".
//   +0x20: enum   +0x24: enum   +0x28: enum   +0x2c: enum   +0x30: enum
//   +0x34: int
//   +0x38: ExternalPtr[5] (inline fixed array, 5 * 8 bytes -> +0x38..+0x60)
//
// Factory  : MemAlloc 0x60 + External ctor + vtable. (Ctor zero-inits all fields.)
// Serialize (vtable +0x28, 0x1401195d0 CreateBridgeDescription_Slot5), stream order:
//   enum  @ +0x20  (encoding vtable +0x98)
//   enum  @ +0x28  (encoding vtable +0x98)
//   enum  @ +0x2c  (encoding vtable +0x98)
//   enum  @ +0x24  (encoding vtable +0x98)
//   enum  @ +0x30  (encoding vtable +0x98)
//   int   @ +0x34
//   ExternalRefArray(count=5) @ +0x38  (FUN_1400049c0 / fn_ExternalRefArray_Serialize)
// GetVersion (vtable +0x18, 0x1401196a0): mov eax,2; ret -> version 2.
// Dtor (vtable +0x00, 0x1400328f0): External::~External + free.
//
// NOTE: fn_ExternalRefArray_Serialize resolves to exe FUN_1400049c0 — null in
// standalone until 0x1400049c0 is registered (REPORTED to orchestrator). The 4th
// param (param4) is a dead init inside FUN_1400049c0, so nullptr is safe.
//
// Field knowledge harvested from engine/reimpl/createbridgedescription.cpp.
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (CreateBridgeDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140162c30) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/CreateBridgeDescription.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternalArray<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x140032880 / createInPlace 0x1400328c0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
CreateBridgeDescription::CreateBridgeDescription()
    : Description(),
      _placeBits(0),
      _invalidBits(0),
      _startEndBits(0),
      _requiredBits(0),
      _edgeBits(0),
      _edgeWidth(0) {
    for (auto& r : _sections) r = nullptr;
}

// [5] 0x1401195d0: stream order as retail.
bool CreateBridgeDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_placeBits, nullptr);
    ser.Serialize(_startEndBits, nullptr);
    ser.Serialize(_requiredBits, nullptr);
    ser.Serialize(_invalidBits, nullptr);
    ser.Serialize(_edgeBits, nullptr);
    ser.Serialize(_edgeWidth, nullptr);
    ::Object::SerializeExternalArray<ComponentDescription>(ser, 5, _sections);   // ExtPtr[5] +0x38 (0x1400049c0)
    return true;
}

// [6] 0x1400328d0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* CreateBridgeDescription::Reconstruct() {
    this->~CreateBridgeDescription();
    return new (static_cast<void*>(this)) CreateBridgeDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void CreateBridgeDescription::Destruct() { this->~CreateBridgeDescription(); }

// [8] 0x140004950 (shared by 19 tables): `MOV dword [RDX],0; MOV RAX,RDX; RET` —
// a data-only description builds no component.
Object::Type* CreateBridgeDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = 0;
    return ret;
}

// [9] 0x1400328c0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* CreateBridgeDescription::Construct() {
    return new (static_cast<void*>(this)) CreateBridgeDescription();
}

// create 0x140032880: MemAlloc(0x60, 8), then the (inlined) ctor.
::Object::Base* CreateBridgeDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CreateBridgeDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CreateBridgeDescription());
}
// createInPlace 0x1400328c0.
::Object::Base* CreateBridgeDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CreateBridgeDescription());
}

DEFINE_EXE_TYPE(CreateBridgeDescription, "Description")
