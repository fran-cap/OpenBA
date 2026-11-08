// src/game/Component/rawmaterialdescription.cpp
// RawMaterialDescription (entity description block) — resource/material type.
// Extends Object::External (0x20 base). Vtable 0x1401641f0. Parent "Description".
// Size 0x60. GetVersion=8 (slot +0x18 @ 0x140129630: mov eax,8; ret).
//
// Ctor defaults (RawMaterialDescription_CopyCtor):
//   ExternalPtr @ +0x20 = 0, ExternalPtr @ +0x28 = 0   (sprite / icon refs)
//   String      @ +0x30 = 0, String @ +0x38 = 0        (name / plural)
//   int         @ +0x40 = 1, +0x44 = 1, +0x48 = 1       (flags / stack / etc.)
//   float       @ +0x50 = 0.33 (0x3ea8f5c3)
//   byte/bool   @ +0x58 = 0
//   int/enum    @ +0x5c = 0
// Serialize (vtable +0x28, 0x140129520), stream order:
//   ExternalPtr @ +0x20  (FUN_14000bd30 -> fn_ExternalPtr_BuildSerialize)
//   ExternalPtr @ +0x28  (FUN_14000bf40 -> fn_ExternalPtr_BuildSerialize)
//   String      @ +0x30
//   String      @ +0x38
//   int @ +0x40, int @ +0x44, int @ +0x48, int @ +0x4c
//   enum @ +0x5c (encoding vcall +0x98)
//   float @ +0x50
//   int @ +0x54
//   bool @ +0x58
// ToolKit fields: RawMaterialFlags / stack / value / weight.
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (RawMaterialDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x1401641f0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/RawMaterialDescription.h"
#include "Component/RawMaterialComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include <cstring>
#include <cstdint>
#include <new>
#include "ui/SpriteSheet.h"   // B8 RT2: ::Object::SerializeExternal<UI::SpriteSheet> (the downcast needs it complete)
#include "ui/StringTable.h"   // B8 RT2: ::Object::SerializeExternal<UI::StringTable> (the downcast needs it complete)

namespace {
// The retail ctor stores raw IEEE bit patterns; this keeps them exact.
inline float FloatBits(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }
}  // namespace

// ctor (inlined into create 0x1400372d0 / createInPlace 0x140037310):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
RawMaterialDescription::RawMaterialDescription()
    : Description(),
      _spriteSheet(nullptr),
      _stringTable(nullptr),
      _lowCreateCount(1),
      _highCreateCount(1),
      _weight(1),
      _createChance(0),
      _carryScale(FloatBits(0x3ea8f5c3)),
      _useValue(0),
      _preferred(false),
      _flags(0) {}

// [5] 0x140129520: stream order as retail.
bool RawMaterialDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ::Object::SerializeExternal<UI::SpriteSheet>(ser, _spriteSheet);
    ::Object::SerializeExternal<UI::StringTable>(ser, _stringTable);
    _name.Serialize(ser);
    _nameSmall.Serialize(ser);
    ser.Serialize(_lowCreateCount, nullptr);
    ser.Serialize(_highCreateCount, nullptr);
    ser.Serialize(_weight, nullptr);
    ser.Serialize(_createChance, nullptr);
    ser.Serialize(_flags, nullptr);
    ser.Serialize(_carryScale, nullptr);
    ser.Serialize(_useValue, nullptr);
    ser.Serialize(_preferred, nullptr);
    return true;
}

// [6] 0x140037320: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* RawMaterialDescription::Reconstruct() {
    this->~RawMaterialDescription();
    return new (static_cast<void*>(this)) RawMaterialDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void RawMaterialDescription::Destruct() { this->~RawMaterialDescription(); }

// [8] 0x140037340: RawMaterialComponent's type id through the hidden return slot.
Object::Type* RawMaterialDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = RawMaterialComponent::TypeId();
    return ret;
}

// [9] 0x140037310: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* RawMaterialDescription::Construct() {
    return new (static_cast<void*>(this)) RawMaterialDescription();
}

// create 0x1400372d0: MemAlloc(0x60, 8), then the (inlined) ctor.
::Object::Base* RawMaterialDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(RawMaterialDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) RawMaterialDescription());
}
// createInPlace 0x140037310.
::Object::Base* RawMaterialDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) RawMaterialDescription());
}

DEFINE_EXE_TYPE(RawMaterialDescription, "Description")
