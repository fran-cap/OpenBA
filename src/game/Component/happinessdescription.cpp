// src/game/Component/happinessdescription.cpp
// HappinessDescription (entity description block) — happiness-effect parameters
// (ToolKit: type / radius / idle range). Extends Object::External (0x20 base).
// Vtable 0x140159660. Parent "Description". Size 0x30. GetVersion=3.
//
// Factory 0x14014ad00-area (MemAlloc 0x30). Ctor defaults:
//   int   @ +0x20 = 5     (HappinessType enum)
//   short @ +0x24 = 0     (two bools @ +0x24/+0x25)
//   ptr   @ +0x28 = 0     (+0x28 enum, +0x2c int)
// Serialize (vtable +0x28, 0x14004acb0), stream order:
//   enum @ +0x20  (serializer encoding vtable +0x98)
//   bool @ +0x24
//   bool @ +0x25
//   enum @ +0x28  (serializer encoding vtable +0x98)
//   int  @ +0x2c
// GetVersion (vtable +0x18, 0x14004ad50): mov eax,3; ret.
// Decompiled READ-ONLY from Application-x64.exe (Foundation C wave 2).
//
// ENDGAME B.3.8 K1: a REAL class (HappinessDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140159660) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/HappinessDescription.h"
#include "Component/HappinessComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>

// ctor (inlined into create 0x140005c10 / createInPlace 0x140005c80):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
HappinessDescription::HappinessDescription()
    : Description(),
      _happinessType(5),
      _needsWorkers(false),
      _needsGoods(false),
      _goodsFlags(0),
      _idleRange(0) {}

// [5] 0x14004acb0: stream order as retail.
bool HappinessDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_happinessType, nullptr);
    ser.Serialize(_needsWorkers, nullptr);
    ser.Serialize(_needsGoods, nullptr);
    ser.Serialize(_goodsFlags, nullptr);
    ser.Serialize(_idleRange, nullptr);
    return true;
}

// [6] 0x140005ce0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* HappinessDescription::Reconstruct() {
    this->~HappinessDescription();
    return new (static_cast<void*>(this)) HappinessDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void HappinessDescription::Destruct() { this->~HappinessDescription(); }

// [8] 0x140005d40: HappinessComponent's type id through the hidden return slot.
Object::Type* HappinessDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = HappinessComponent::TypeId();
    return ret;
}

// [9] 0x140005c80: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* HappinessDescription::Construct() {
    return new (static_cast<void*>(this)) HappinessDescription();
}

// create 0x140005c10: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* HappinessDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HappinessDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HappinessDescription());
}
// createInPlace 0x140005c80.
::Object::Base* HappinessDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HappinessDescription());
}

DEFINE_EXE_TYPE(HappinessDescription, "Description")
