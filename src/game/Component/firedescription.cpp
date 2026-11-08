// src/game/Component/firedescription.cpp
// FireDescription (entity description block) — fire disaster/spread parameters.
// Extends Object::External (0x20 base). Vtable 0x140159400. Parent "Description".
// Size 0x50. GetVersion=4.
//
// Factory 0x14014ac00-area (MemAlloc 0x50 + ctor sub_9d60). Ctor defaults:
//   float @ +0x20 = 1.0,  (+0x24 = 0)
//   int   @ +0x28 = 1,    (+0x2c = 0)
//   float @ +0x30 = 0.25, float @ +0x34 = 0.5
//   int   @ +0x38 = 8,    int   @ +0x3c = 100,  int @ +0x40 = 50 (0x32)
//   float @ +0x44 = 1.0,  float @ +0x48 = 1.0
// Serialize (vtable +0x28, 0x140049c00), stream order (note non-monotone):
//   float @ +0x20,  float @ +0x24
//   int   @ +0x28,  int   @ +0x2c
//   float @ +0x30,  float @ +0x34
//   int   @ +0x3c
//   float @ +0x44,  float @ +0x48
//   int   @ +0x38,  int   @ +0x40
// GetVersion (vtable +0x18, 0x140049d10): mov eax,4; ret.
// Decompiled READ-ONLY from Application-x64.exe (Foundation C wave 2).
//
// ENDGAME B.3.8 K1: a REAL class (FireDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140159400) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/FireDescription.h"
#include "Component/FireComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>

// ctor (inlined into create 0x140005640 / createInPlace 0x140005680):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
FireDescription::FireDescription()
    : Description(),
      burnBase(1.0f),
      burnScale(0.0f),
      particleEmitter(1),
      audioEmitter(0),
      spreadInterval(0.25f),
      spreadChance(0.5f),
      dangerRadius(8),
      bucketsToOut(100),
      maxFirefighters(50),
      igniteChance{1.0f, 1.0f},
      _pad4c(0) {}

// [5] 0x140049c00: stream order as retail.
bool FireDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(burnBase, nullptr);
    ser.Serialize(burnScale, nullptr);
    ser.Serialize(particleEmitter, nullptr);
    ser.Serialize(audioEmitter, nullptr);
    ser.Serialize(spreadInterval, nullptr);
    ser.Serialize(spreadChance, nullptr);
    ser.Serialize(bucketsToOut, nullptr);
    ser.Serialize(igniteChance[0], nullptr);
    ser.Serialize(igniteChance[1], nullptr);
    ser.Serialize(dangerRadius, nullptr);
    ser.Serialize(maxFirefighters, nullptr);
    return true;
}

// [6] 0x140005690: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* FireDescription::Reconstruct() {
    this->~FireDescription();
    return new (static_cast<void*>(this)) FireDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void FireDescription::Destruct() { this->~FireDescription(); }

// [8] 0x1400056b0: FireComponent's type id through the hidden return slot.
Object::Type* FireDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = FireComponent::TypeId();
    return ret;
}

// [9] 0x140005680: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* FireDescription::Construct() {
    return new (static_cast<void*>(this)) FireDescription();
}

// create 0x140005640: MemAlloc(0x50, 8), then the (inlined) ctor.
::Object::Base* FireDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FireDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) FireDescription());
}
// createInPlace 0x140005680.
::Object::Base* FireDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) FireDescription());
}

DEFINE_EXE_TYPE(FireDescription, "Description")
