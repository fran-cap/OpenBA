// src/game/Component/workplacedescription.cpp
// Clean reimplementation of WorkPlaceDescription (entity description block).
//
// WorkPlaceDescription (0x30 bytes) — workplace job slots (two Profession refs).
// Extends Object::External (0x20 base). Vtable at 0x140164dc0.
// Parent type "Description".
//
// Factory  : 0x14003a090  (MemAlloc 0x30 + ctor)  fields +0x20=0, +0x28=0
// CreateInPlace: 0x14003a100
// Serialize (vtable +0x28, 0x14012f420):
//   ExternalPtr<Profession> @ +0x20   (FUN_140018020, typeId TypeInfo_Profession)
//   ExternalPtr<Profession> @ +0x28   (FUN_140018020)
//   Both are 8-byte resolved External* fields stored in place.
// GetVersion (vtable +0x18, 0x14012f480): mov eax,2; ret -> file version 2.
// Dtor (vtable +0x00, 0x14003a1c0): vtable reset + External::~External + free
//   (the two +0x20/+0x28 fields are weak External* refs, not owned — no dtor).
//
// The per-field ExternalPtr serialize (FUN_140018020) is
// ::Object::SerializeExternal<Profession> (object/External.h).
//
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (WorkPlaceDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140164dc0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/WorkPlaceDescription.h"
#include "Component/WorkPlaceComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>

#include "Game/exe_bodies.h"
#include "Game/Profession.h"   // B8 RT2: ::Object::SerializeExternal<Profession> (the downcast needs it complete)

// ctor (inlined into create 0x14003a090 / createInPlace 0x14003a100):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
WorkPlaceDescription::WorkPlaceDescription()
    : Description(),
      _profession0(nullptr),
      _profession1(nullptr) {}

// [5] 0x14012f420: stream order as retail.
bool WorkPlaceDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ::Object::SerializeExternal<Profession>(ser, _profession0);
    ::Object::SerializeExternal<Profession>(ser, _profession1);
    return true;
}

// [6] 0x14003a160: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* WorkPlaceDescription::Reconstruct() {
    this->~WorkPlaceDescription();
    return new (static_cast<void*>(this)) WorkPlaceDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void WorkPlaceDescription::Destruct() { this->~WorkPlaceDescription(); }

// [8] 0x14003a1b0: WorkPlaceComponent's type id through the hidden return slot.
Object::Type* WorkPlaceDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = WorkPlaceComponent::TypeId();
    return ret;
}

// [9] 0x14003a100: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* WorkPlaceDescription::Construct() {
    return new (static_cast<void*>(this)) WorkPlaceDescription();
}

// create 0x14003a090: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* WorkPlaceDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(WorkPlaceDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) WorkPlaceDescription());
}
// createInPlace 0x14003a100.
::Object::Base* WorkPlaceDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) WorkPlaceDescription());
}

DEFINE_EXE_TYPE(WorkPlaceDescription, "Description")
