// src/game/Component/foresterdescription.cpp
// ForesterDescription (entity description block) — forester lodge parameters.
// Extends Object::External (0x20 base). Vtable 0x140163510. Parent "Description".
// Size 0x30. GetVersion=2 (slot +0x18 @ 0x14011fb00: mov eax,2; ret).
//
// Ctor defaults (ForesterDescription_Factory):
//   ExternalPtr @ +0x20 = 0   (tree/sapling material ref)
//   int/enum    @ +0x28 = 0
// Serialize (vtable +0x28, 0x14011fa80) — Serializer ctor with version=1, stream order:
//   ExternalPtr @ +0x20  (FUN_140002340 -> fn_ExternalPtr_BuildSerialize)
//   if (serializer.GetVersion() > 0): enum @ +0x28 (encoding vcall +0x98)
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (ForesterDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140163510) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/ForesterDescription.h"
#include "Component/ForesterComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x1400348e0 / createInPlace 0x140034950):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
ForesterDescription::ForesterDescription()
    : Description(),
      _naturalResource(nullptr),
      _type(0) {}

// [5] 0x14011fa80: stream order as retail.
bool ForesterDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 1);
    ::Object::SerializeExternal<ComponentDescription>(ser, _naturalResource);
    if (ser.GetVersion() > 0) ser.Serialize(_type, nullptr);   // version-gated, as retail
    return true;
}

// [6] 0x1400349b0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* ForesterDescription::Reconstruct() {
    this->~ForesterDescription();
    return new (static_cast<void*>(this)) ForesterDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void ForesterDescription::Destruct() { this->~ForesterDescription(); }

// [8] 0x140034a00: ForesterComponent's type id through the hidden return slot.
Object::Type* ForesterDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = ForesterComponent::TypeId();
    return ret;
}

// [9] 0x140034950: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* ForesterDescription::Construct() {
    return new (static_cast<void*>(this)) ForesterDescription();
}

// create 0x1400348e0: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* ForesterDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ForesterDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ForesterDescription());
}
// createInPlace 0x140034950.
::Object::Base* ForesterDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ForesterDescription());
}

DEFINE_EXE_TYPE(ForesterDescription, "Description")
