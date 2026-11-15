// src/game/Component/nomaddescription.cpp
// NomadDescription (entity description block) — nomad arrival parameters.
// Extends Object::External (0x20 base). Vtable 0x14015a0b0. Parent "Description".
// Size 0x38. GetVersion=2.
//
// Factory 0x14014b340-area (MemAlloc 0x38). Ctor defaults: ExternalPtr fields
//   @ +0x20, +0x28, +0x30 all = 0.
// Serialize (vtable +0x28, 0x14004ff20), stream order:
//   ExternalPtr @ +0x20  (FUN_140002340 = fn_ExternalPtr_BuildSerialize)
//   ExternalPtr @ +0x28  (FUN_140007140 — single-ExternalPtr variant w/ type-check;
//                          stream-identical to fn_ExternalPtr_BuildSerialize)
//   ExternalPtr @ +0x30  (FUN_140007140)
// GetVersion (vtable +0x18, 0x14004ff90): mov eax,2; ret.
// Decompiled READ-ONLY from Application-x64.exe (Foundation C wave 2).
//
// ENDGAME B.3.8 K1: a REAL class (NomadDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x14015a0b0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/NomadDescription.h"
#include "Component/NomadComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include <cstdint>
#include <new>
#include "graphics/Material.h"   // B8 RT2: ::Object::SerializeExternal<Graphics::MaterialInstance> (the downcast needs it complete)
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x140009090 / createInPlace 0x140009100):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
NomadDescription::NomadDescription()
    : Description(),
      _citizen(nullptr),
      _materialGender0(nullptr),
      _materialGender1(nullptr) {}

// [5] 0x14004ff20: stream order as retail.
bool NomadDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ::Object::SerializeExternal<ComponentDescription>(ser, _citizen);
    ::Object::SerializeExternal<Graphics::MaterialInstance>(ser, _materialGender0);   // 0x140007140 MaterialInstance (0x14004ff58)
    ::Object::SerializeExternal<Graphics::MaterialInstance>(ser, _materialGender1);   // 0x140007140 MaterialInstance (0x14004ff66)
    return true;
}

// [6] 0x140009160: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* NomadDescription::Reconstruct() {
    this->~NomadDescription();
    return new (static_cast<void*>(this)) NomadDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void NomadDescription::Destruct() { this->~NomadDescription(); }

// [8] 0x1400091c0: NomadComponent's type id through the hidden return slot.
Object::Type* NomadDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = NomadComponent::TypeId();
    return ret;
}

// [9] 0x140009100: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* NomadDescription::Construct() {
    return new (static_cast<void*>(this)) NomadDescription();
}

// create 0x140009090: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* NomadDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(NomadDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) NomadDescription());
}
// createInPlace 0x140009100.
::Object::Base* NomadDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) NomadDescription());
}

DEFINE_EXE_TYPE(NomadDescription, "Description")
