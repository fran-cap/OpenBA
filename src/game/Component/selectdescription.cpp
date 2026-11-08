// src/game/Component/selectdescription.cpp
// Clean reimplementation of SelectDescription (entity description block).
//
// SelectDescription (0x48 bytes) — selection-tool descriptor.
// Extends Object::External (0x20 base). Vtable at 0x14015a5f0. Parent "Description".
//   +0x20: ExternalPtr  (default null)
//   +0x28: String       (default empty)
//   +0x30: ExternalPtr  (default null)
//   +0x38: String       (default empty)
//   +0x40: enum/int     (default 0)
//
// Factory  : MemAlloc 0x48 + External ctor + vtable + field defaults (all zero).
// Serialize (vtable +0x28, 0x140055630), stream order:
//   ExternalPtr @ +0x20  (fn_ExternalPtr_Serialize)
//   String      @ +0x28
//   ExternalPtr @ +0x30  (fn_ExternalPtr_Serialize2)
//   String      @ +0x38
//   enum        @ +0x40  (serializer-encoding vtable +0x98 dispatch)
// GetVersion (vtable +0x18, 0x1400556c0): mov eax,1; ret -> version 1.
// Dtor (vtable +0x00, 0x14000bcc0): ~String(+0x38) + ~String(+0x28) +
//   External::~External + free.
//
// Field knowledge harvested from engine/reimpl/selectdescription.cpp.
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (SelectDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x14015a5f0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/SelectDescription.h"
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

// ctor (inlined into create 0x14000bb90 / createInPlace 0x14000bc00):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
SelectDescription::SelectDescription()
    : Description(),
      _spriteSheet(nullptr),
      _stringTable(nullptr),
      _group(0) {}

// [5] 0x140055630: stream order as retail.
bool SelectDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ::Object::SerializeExternal<UI::SpriteSheet>(ser, _spriteSheet);
    _spriteName.Serialize(ser);
    ::Object::SerializeExternal<UI::StringTable>(ser, _stringTable);
    _stringName.Serialize(ser);
    ser.Serialize(_group, nullptr);
    return true;
}

// [6] 0x14000bc60: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* SelectDescription::Reconstruct() {
    this->~SelectDescription();
    return new (static_cast<void*>(this)) SelectDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void SelectDescription::Destruct() { this->~SelectDescription(); }

// [8] 0x140004950 (shared by 19 tables): `MOV dword [RDX],0; MOV RAX,RDX; RET` —
// a data-only description builds no component.
Object::Type* SelectDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = 0;
    return ret;
}

// [9] 0x14000bc00: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* SelectDescription::Construct() {
    return new (static_cast<void*>(this)) SelectDescription();
}

// create 0x14000bb90: MemAlloc(0x48, 8), then the (inlined) ctor.
::Object::Base* SelectDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(SelectDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) SelectDescription());
}
// createInPlace 0x14000bc00.
::Object::Base* SelectDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) SelectDescription());
}

DEFINE_EXE_TYPE(SelectDescription, "Description")
