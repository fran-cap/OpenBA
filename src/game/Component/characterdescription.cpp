// src/game/Component/characterdescription.cpp
// Clean reimplementation of CharacterDescription (Object::External "Description").
//
// CharacterDescription (0x48 bytes) — extends Object::External (0x20 base).
// Vtable at 0x1401623f0.  GetVersion = 6.
//
// Field layout (from ctor FUN_140109f10 + Serialize 0x140109dd0):
//   +0x20 float  speedScale        (default 0.25  = 0x3e800000)
//   +0x24 float  _headingOffset    (default 0.0;  high dword of the 8-byte ctor write at +0x20)
//   +0x28 float  _turnFilter       (default 0.85  = 0x3f59999a)
//   +0x2c float  _defaultAnimationBlendTime (default 0.1   = 0x3dcccccd)
//   +0x30 float                    (default 1.0)
//   +0x34 float                    (default 1.0)
//   +0x38 float                    (default 1.0)
//   +0x3c bool                     (default 0)
//   +0x40 float                    (default 0.0)
//   +0x44 float                    (default -10.0 = 0xc1200000)
//
// Serialize stream order (exact, from 0x140109dd0):
//   +0x20,+0x24,+0x28,+0x2c,+0x30,+0x34,+0x38 (float), +0x3c (bool),
//   +0x40,+0x44 (float).
//
// Decompiled from Application-x64.exe (read-only Ghidra recon).
//
// ENDGAME B.3.8 K1: a REAL class (CharacterDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x1401623f0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/CharacterDescription.h"
#include "Component/CharacterComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstring>
#include <cstdint>
#include <new>

namespace {
// The retail ctor stores raw IEEE bit patterns; this keeps them exact.
inline float FloatBits(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }
}  // namespace

// ctor (inlined into create 0x14002f9c0 / createInPlace 0x14002fa00):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
CharacterDescription::CharacterDescription()
    : Description(),
      _radius(0.25f),
      _headingOffset(0.0f),
      _turnFilter(FloatBits(0x3f59999a)),
      _defaultAnimationBlendTime(FloatBits(0x3dcccccd)),
      _slowSpeed(1.0f),
      _fastSpeed(1.0f),
      _fastestSpeed(1.0f),
      _forceHeight(false),
      _height(0.0f),
      _minHeight(-10.0f) {}

// [5] 0x140109dd0: stream order as retail.
bool CharacterDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_radius, nullptr);
    ser.Serialize(_headingOffset, nullptr);
    ser.Serialize(_turnFilter, nullptr);
    ser.Serialize(_defaultAnimationBlendTime, nullptr);
    ser.Serialize(_slowSpeed, nullptr);
    ser.Serialize(_fastSpeed, nullptr);
    ser.Serialize(_fastestSpeed, nullptr);
    ser.Serialize(_forceHeight, nullptr);
    ser.Serialize(_height, nullptr);
    ser.Serialize(_minHeight, nullptr);
    return true;
}

// [6] 0x14002fa10: `this->~T()` (retail: slot 0, flags 0 -- destroy in place), then the ctor again on
// `this`. The hand-built table's Cleanup trampoline already did this.
Object::External* CharacterDescription::Reconstruct() {
    this->~CharacterDescription();
    return new (static_cast<void*>(this)) CharacterDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void CharacterDescription::Destruct() { this->~CharacterDescription(); }

// [8] 0x14002fa30: CharacterComponent's type id through the hidden return slot.
Object::Type* CharacterDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = CharacterComponent::TypeId();
    return ret;
}

// [9] 0x14002fa00: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* CharacterDescription::Construct() {
    return new (static_cast<void*>(this)) CharacterDescription();
}

// create 0x14002f9c0: MemAlloc(0x48, 8), then the (inlined) ctor.
::Object::Base* CharacterDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CharacterDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CharacterDescription());
}
// createInPlace 0x14002fa00.
::Object::Base* CharacterDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CharacterDescription());
}

DEFINE_EXE_TYPE(CharacterDescription, "Description")
