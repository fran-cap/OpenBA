// src/game/Component/valuedescription.cpp
// ValueDescription (entity description block) — resource value/trade parameters.
// Extends Object::External (0x20 base). Vtable 0x14015add0. Parent "Description".
// Size 0x50. GetVersion=3 (slot +0x18 @ 0x14005ce10: mov eax,3; ret).
//
// Ctor defaults (ValueDescription_Factory @ 0x14000faa0):
//   ExternalPtr @ +0x20 = 0   (sprite/icon ref)
//   String      @ +0x28 = 0   (name)
//   ExternalPtr @ +0x30 = 0
//   String      @ +0x38 = 0
//   int         @ +0x40 = 0
//   int         @ +0x44 = 100 (baseValue)
//   int         @ +0x48 = 0   (weight) [factory writes 8 bytes @+0x44 = 100]
// Serialize (vtable +0x28, 0x14005cd60), stream order:
//   ExternalPtr @ +0x20  (FUN_14000bd30 -> Reimpl_ExternalPtr_Serialize_StringTable)
//   String      @ +0x28
//   ExternalPtr @ +0x30  (FUN_14000bf40 -> Reimpl_ExternalPtr_Serialize_StringTable)
//   String      @ +0x38
//   int         @ +0x40
//   int         @ +0x44
//   int         @ +0x48
// ToolKit fields: sprite / name / baseValue / weight.
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (ValueDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x14015add0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/ValueDescription.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include <cstdint>
#include <new>
#include "ui/SpriteSheet.h"   // B8 RT2: ::Object::SerializeExternal<UI::SpriteSheet> (the downcast needs it complete)
#include "ui/StringTable.h"   // B8 RT2: ::Object::SerializeExternal<UI::StringTable> (the downcast needs it complete)

// ctor (inlined into create 0x14000faa0 / createInPlace 0x14000fb10):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
ValueDescription::ValueDescription()
    : Description(),
      _sprite(nullptr),
      _stringTable(nullptr),
      _baseValue(0),
      _stackCount(100),
      _minStackCount(0) {}

// [5] 0x14005cd60: stream order as retail.
bool ValueDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ::Object::SerializeExternal<UI::SpriteSheet>(ser, _sprite);
    _name.Serialize(ser);
    ::Object::SerializeExternal<UI::StringTable>(ser, _stringTable);
    _stringName.Serialize(ser);
    ser.Serialize(_baseValue, nullptr);
    ser.Serialize(_stackCount, nullptr);
    ser.Serialize(_minStackCount, nullptr);
    return true;
}

// [6] 0x14000fb80: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* ValueDescription::Reconstruct() {
    this->~ValueDescription();
    return new (static_cast<void*>(this)) ValueDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void ValueDescription::Destruct() { this->~ValueDescription(); }

// [8] 0x140004950 (shared by 19 tables): `MOV dword [RDX],0; MOV RAX,RDX; RET` —
// a data-only description builds no component.
Object::Type* ValueDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = 0;
    return ret;
}

// [9] 0x14000fb10: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* ValueDescription::Construct() {
    return new (static_cast<void*>(this)) ValueDescription();
}

// create 0x14000faa0: MemAlloc(0x50, 8), then the (inlined) ctor.
::Object::Base* ValueDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ValueDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ValueDescription());
}
// createInPlace 0x14000fb10.
::Object::Base* ValueDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ValueDescription());
}

DEFINE_EXE_TYPE(ValueDescription, "Description")
