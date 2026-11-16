// src/game/Component/radiusdescription.cpp
// Clean reimplementation of RadiusDescription (entity description block).
//
// RadiusDescription (0x30 bytes) — radius-of-effect descriptor.
// Extends Object::External (0x20 base). Vtable at 0x14015a580. Parent "Description".
//   +0x20: ExternalPtr  (default null)
//   +0x28: int          (radius, default 0x1e = 30)
//
// Factory  : MemAlloc 0x30 + External ctor + vtable + field defaults.
// Serialize (vtable +0x28, 0x140055530), stream order:
//   int         @ +0x28
//   ExternalPtr @ +0x20   (fn_ExternalPtr_Serialize3)
// GetVersion (vtable +0x18, 0x140055590): mov eax,1; ret -> version 1.
// Dtor (vtable +0x00, 0x14000bb10): External::~External + free.
//
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (RadiusDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x14015a580) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/RadiusDescription.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include <cstdint>
#include <new>
#include "graphics/Material.h"   // B8 RT2: ::Object::SerializeExternal<Graphics::MaterialInstance> (the downcast needs it complete)

// ctor (inlined into create 0x14000b9e0 / createInPlace 0x14000ba50):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
RadiusDescription::RadiusDescription()
    : Description(),
      _material(nullptr),
      _radius(0x1e) {}

// [5] 0x140055530: stream order as retail.
bool RadiusDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_radius, nullptr);
    ::Object::SerializeExternal<Graphics::MaterialInstance>(ser, _material);   // 0x140007140
    return true;
}

// [6] 0x14000bab0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* RadiusDescription::Reconstruct() {
    this->~RadiusDescription();
    return new (static_cast<void*>(this)) RadiusDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void RadiusDescription::Destruct() { this->~RadiusDescription(); }

// [8] 0x140004950 (shared by 19 tables): `MOV dword [RDX],0; MOV RAX,RDX; RET` —
// a data-only description builds no component.
Object::Type* RadiusDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = 0;
    return ret;
}

// [9] 0x14000ba50: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* RadiusDescription::Construct() {
    return new (static_cast<void*>(this)) RadiusDescription();
}

// create 0x14000b9e0: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* RadiusDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(RadiusDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) RadiusDescription());
}
// createInPlace 0x14000ba50.
::Object::Base* RadiusDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) RadiusDescription());
}

DEFINE_EXE_TYPE(RadiusDescription, "Description")
