// src/game/Component/destroydescription.cpp
// Clean reimplementation of DestroyDescription (entity sub-description).
//
// DestroyDescription (0x38 bytes) — extends Object::External (0x20 base).
// Parent type name: "Description". Vtable @ 0x1401631b0.  GetVersion = 5.
//
// Field layout. The names are the AUTHORED ones — they are the field-name strings in
// the resource compiler (Tools-x64.exe) and the keys used in Template/*.rsc:
//   +0x20: void* _replaceEntity   ExternalPtr<ComponentDescription> (default 0)
//   +0x28: int32 _damagedSkin     (ctor default 1)  rubble MATERIAL-SET index
//   +0x2c: int32 _damagedDecal    (ctor default 1)  rubble DECAL state
//   +0x30: int32 _minDamageLevel  (no retail ctor default; the tree writes 0) rubble MESH-VARIANT floor
// Consumed by the state-6 collapse — see destroycomponent_state6.cpp.
//
// Serialize (FUN_14011de70) stream order:
//   1. ExternalPtr serialize (+0x20)   FUN_140002340
//   2. int (+0x28)
//   3. int (+0x2c)
//   4. int (+0x30)
//
// ⚠ _minDamageLevel IS GARBAGE IN THE SHIPPED DATA — and that is retail-faithful.
// A 2026-08-23 note claimed the third int "reads off the end of the record". It does
// not. Measured on the live path, the record is 61 decompressed bytes and the three
// int reads land at cursor 41/45/49, all in range. Compiling the kit's own sources
// with Tools-x64.exe proves why: Template/Chapel.rsc (which authors only
// _damagedSkin/_damagedDecal) compiles to a record whose _minDamageLevel slot holds
// 0xE1E1E1E1, while Template/Market.rsc (the ONE template in the whole game that
// authors `int _minDamageLevel = 1`) compiles to the same 61-byte record with a 1
// there. Both decompress to 61 bytes; the slot is always present, and the compiler
// fills it with uninitialised memory when the .rsc omits it. Retail's ctor
// (0x140033c30) sets only +0x20/+0x28/+0x2c, so nothing ever overwrites it either.
//
// The consumer is written to survive this: 0x14011d5d0 clamps the stepped-down mesh
// variant to >= 0 FIRST and only then raises it to _minDamageLevel, so a hugely
// negative floor simply never binds and the building collapses to variant 0. Market
// stops at variant 1. Do NOT "fix" the value here — see destroycomponent_state6.cpp.
//
// Decompiled from Application-x64.exe (READ-ONLY Ghidra).
//
// ENDGAME B.3.8 K1: a REAL class (DestroyDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x1401631b0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/DestroyDescription.h"
#include "Component/DestroyComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x140033c30 / createInPlace 0x140033ca0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults. NOT RETAIL: the 0 for _minDamageLevel; retail leaves +0x30
// unwritten (docs/known_port_issues.md row 1427). Serialize always reads it.
DestroyDescription::DestroyDescription()
    : Description(),
      _replaceEntity(nullptr),
      _damagedSkin(1),
      _damagedDecal(1),
      _minDamageLevel(0) {}

// [5] 0x14011de70: stream order as retail.
bool DestroyDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    // 1. _replaceEntity (+0x20). Retail calls FUN_140002340, the
    //    ExternalPtr<ComponentDescription> variant, which gates the resolved object on
    //    IsDerivedFrom(_, DWORD_1401c7318) = ComponentDescription. This used to call
    //    fn_ExternalPtr_Serialize2 (FUN_14000bf40) — byte-identical on the wire, but it
    //    gates on the StringTable type id instead, so a non-null _replaceEntity failed
    //    the cast and was stored as NULL. Only Template/IronMine.rsc and
    //    Template/Quarry.rsc author it (`ComponentDescription _replaceEntity =
    //    "destroyed"`), which is why the swap was invisible for so long.
    ::Object::SerializeExternal<ComponentDescription>(ser, _replaceEntity);
    ser.Serialize(_damagedSkin, nullptr);
    ser.Serialize(_damagedDecal, nullptr);
    ser.Serialize(_minDamageLevel, nullptr);
    return true;
}

// [6] 0x140033d00: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* DestroyDescription::Reconstruct() {
    this->~DestroyDescription();
    return new (static_cast<void*>(this)) DestroyDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void DestroyDescription::Destruct() { this->~DestroyDescription(); }

// [8] 0x140033d60: DestroyComponent's type id through the hidden return slot.
Object::Type* DestroyDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = DestroyComponent::TypeId();
    return ret;
}

// [9] 0x140033ca0: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* DestroyDescription::Construct() {
    return new (static_cast<void*>(this)) DestroyDescription();
}

// create 0x140033c30: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* DestroyDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(DestroyDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) DestroyDescription());
}
// createInPlace 0x140033ca0.
::Object::Base* DestroyDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) DestroyDescription());
}

DEFINE_EXE_TYPE(DestroyDescription, "Description")
