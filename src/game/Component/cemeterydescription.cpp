// src/game/Component/cemeterydescription.cpp
// CemeteryDescription (entity description block) — cemetery building parameters.
// Extends Object::External (0x20 base). Vtable 0x140159130. Parent "Description".
// Size 0x28. GetVersion=1.
//
// Factory 0x14014a9c0-area (MemAlloc 0x28 + base init + vtable + zero @+0x20).
// Ctor default: ExternalPtr @ +0x20 = 0.
// Serialize (vtable +0x28, 0x140047800), stream order:
//   ExternalPtr @ +0x20  (FUN_140002340 = fn_ExternalPtr_BuildSerialize)
// GetVersion (vtable +0x18, 0x140047850): mov eax,1; ret.
// Decompiled READ-ONLY from Application-x64.exe (Foundation C wave 2).
//
// ENDGAME B.3.8 K1: a REAL class (CemeteryDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140159130) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/CemeteryDescription.h"
#include "Component/CemeteryComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x1400040b0 / createInPlace 0x140004110):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
CemeteryDescription::CemeteryDescription()
    : Description(),
      _tombstone(nullptr) {}

// [5] 0x140047800: stream order as retail.
bool CemeteryDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ::Object::SerializeExternal<ComponentDescription>(ser, _tombstone);
    return true;
}

// [6] 0x140004160: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* CemeteryDescription::Reconstruct() {
    this->~CemeteryDescription();
    return new (static_cast<void*>(this)) CemeteryDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void CemeteryDescription::Destruct() { this->~CemeteryDescription(); }

// [8] 0x1400041b0: CemeteryComponent's type id through the hidden return slot.
Object::Type* CemeteryDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = CemeteryComponent::TypeId();
    return ret;
}

// [9] 0x140004110: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* CemeteryDescription::Construct() {
    return new (static_cast<void*>(this)) CemeteryDescription();
}

// create 0x1400040b0: MemAlloc(0x28, 8), then the (inlined) ctor.
::Object::Base* CemeteryDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CemeteryDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CemeteryDescription());
}
// createInPlace 0x140004110.
::Object::Base* CemeteryDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CemeteryDescription());
}

DEFINE_EXE_TYPE(CemeteryDescription, "Description")
