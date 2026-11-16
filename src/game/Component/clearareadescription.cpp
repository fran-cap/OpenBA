// src/game/Component/clearareadescription.cpp
// Clean reimplementation of ClearAreaDescription (entity sub-description).
//
// ClearAreaDescription (0x38 bytes) — extends Object::External (0x20 base).
// Parent type name: "Description". Vtable @ 0x140162760.  GetVersion = 6.
//
// Field layout (from Factory + Serialize FUN_1401180f0 + dtor FUN_140031060):
//   +0x20: ComponentDescription* _target  ExternalPtr<ComponentDescription> (default 0)
//   +0x28: bool  _clearTrees          (default 1)
//   +0x29: bool  _clearGround          (default 0)
//   +0x2a: bool  _clearBuildings          (default 1)
//   +0x2b: bool  _priority          (default 0)
//   +0x2c: bool  _regrowTerrain          (default 1)
//   +0x2d: bool  _instant          (default 0)
//   +0x30: int32 _value          (default -1)
//
// Serialize (FUN_1401180f0) stream order:
//   1. ExternalPtr serialize (+0x20)        FUN_140002340
//   2..7. bool x6 (+0x28..+0x2d)
//   8. int (+0x30)
//
// Decompiled from Application-x64.exe (READ-ONLY Ghidra).
//
// ENDGAME B.3.8 K1: a REAL class (ClearAreaDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140162760) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/ClearAreaDescription.h"
#include "Component/ClearAreaComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x140030f00 / createInPlace 0x140030f80):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
ClearAreaDescription::ClearAreaDescription()
    : Description(),
      _target(nullptr),
      _clearTrees(true),
      _clearGround(false),
      _clearBuildings(true),
      _priority(false),
      _regrowTerrain(true),
      _instant(false),
      _value(-1) {}

// [5] 0x1401180f0: stream order as retail.
bool ClearAreaDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    // 1. ExternalPtr (+0x20). Same stream layout as the StringTable ExternalPtr
    //    helper (FUN_14000bf40); only the validated typeId differs (ComponentDescription's,
    //    through Object::SerializeExternal).
    ::Object::SerializeExternal<ComponentDescription>(ser, _target);
    ser.Serialize(_clearTrees, nullptr);
    ser.Serialize(_clearGround, nullptr);
    ser.Serialize(_clearBuildings, nullptr);
    ser.Serialize(_priority, nullptr);
    ser.Serialize(_regrowTerrain, nullptr);
    ser.Serialize(_instant, nullptr);
    ser.Serialize(_value, nullptr);
    return true;
}

// [6] 0x140030fe0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* ClearAreaDescription::Reconstruct() {
    this->~ClearAreaDescription();
    return new (static_cast<void*>(this)) ClearAreaDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void ClearAreaDescription::Destruct() { this->~ClearAreaDescription(); }

// [8] 0x140031050: ClearAreaComponent's type id through the hidden return slot.
Object::Type* ClearAreaDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = ClearAreaComponent::TypeId();
    return ret;
}

// [9] 0x140030f80: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* ClearAreaDescription::Construct() {
    return new (static_cast<void*>(this)) ClearAreaDescription();
}

// create 0x140030f00: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* ClearAreaDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ClearAreaDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ClearAreaDescription());
}
// createInPlace 0x140030f80.
::Object::Base* ClearAreaDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ClearAreaDescription());
}

DEFINE_EXE_TYPE(ClearAreaDescription, "Description")
