// src/game/Component/createdragdescription.cpp
// Clean reimplementation of CreateDragDescription (entity description block).
//
// CreateDragDescription (0x40 bytes) — drag-to-build placement tool parameters.
// Extends Object::External (0x20 base). Vtable at 0x140162ca0.
// Parent type "Description".
//
// Factory  : 0x140032970  (MemAlloc 0x40 + ctor 0x140119860)
// CreateInPlace: 0x1400329b0
// Ctor defaults (from CreateDragDescription_Ctor 0x140119860):
//   int  @ +0x20 = 0   (enum: drag axis/shape)
//   bool @ +0x24 = 0
//   int  @ +0x28 = 1
//   int  @ +0x2c = 1
//   int  @ +0x30 = 1
//   int  @ +0x34 = 1
//   void*@ +0x38 = 0   (ExternalRef: created-resource description, weak)
// Serialize (vtable +0x28, 0x140119750), stream order:
//   enum        @ +0x20   (serializer-encoding vtable +0x98 dispatch)
//   int         @ +0x28
//   int         @ +0x2c
//   int         @ +0x30
//   int         @ +0x34
//   bool        @ +0x24
//   ExternalRef @ +0x38   (FUN_140002340)
// GetVersion (vtable +0x18, 0x140119810): mov eax,2; ret -> file version 2.
// Dtor (vtable +0x00, 0x1400329e0): vtable reset + External::~External + free
//   (the +0x38 ExternalRef is a weak pointer, not owned — no per-field dtor).
//
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (CreateDragDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140162ca0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/CreateDragDescription.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>

#include "Game/exe_bodies.h"   // the typed Reimpl_ExternalPtr_Serialize_* wrappers (B4 X7-GAME-A)
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x140032970 / createInPlace 0x1400329b0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
CreateDragDescription::CreateDragDescription()
    : Description(),
      _placeBits(0),
      _ignoreZones(false),
      _maxWidth(1),
      _maxHeight(1),
      _minWidth(1),
      _minHeight(1),
      _allowAndRemove(nullptr) {}

// [5] 0x140119750: stream order as retail.
bool CreateDragDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_placeBits, nullptr);
    ser.Serialize(_maxWidth, nullptr);
    ser.Serialize(_maxHeight, nullptr);
    ser.Serialize(_minWidth, nullptr);
    ser.Serialize(_minHeight, nullptr);
    ser.Serialize(_ignoreZones, nullptr);
    ::Object::SerializeExternal<ComponentDescription>(ser, _allowAndRemove);
    return true;
}

// [6] 0x1400329c0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* CreateDragDescription::Reconstruct() {
    this->~CreateDragDescription();
    return new (static_cast<void*>(this)) CreateDragDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void CreateDragDescription::Destruct() { this->~CreateDragDescription(); }

// [8] 0x140004950 (shared by 19 tables): `MOV dword [RDX],0; MOV RAX,RDX; RET` —
// a data-only description builds no component.
Object::Type* CreateDragDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = 0;
    return ret;
}

// [9] 0x1400329b0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* CreateDragDescription::Construct() {
    return new (static_cast<void*>(this)) CreateDragDescription();
}

// create 0x140032970: MemAlloc(0x40, 8), then the (inlined) ctor.
::Object::Base* CreateDragDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CreateDragDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CreateDragDescription());
}
// createInPlace 0x1400329b0.
::Object::Base* CreateDragDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CreateDragDescription());
}

DEFINE_EXE_TYPE(CreateDragDescription, "Description")
