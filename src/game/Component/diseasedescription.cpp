// src/game/Component/diseasedescription.cpp
// DiseaseDescription (entity description block) — disease outbreak parameters.
// Extends Object::External (0x20 base). Vtable 0x140159310. Parent "Description".
// Size 0x48. GetVersion=1.
//
// Factory 0x14014ab80-area (MemAlloc 0x48 + base init + vtable; no explicit field
// inits — fields left as fresh-alloc zero).
// Serialize (vtable +0x28, 0x140048e40), stream order:
//   int   @ +0x20,  int   @ +0x24
//   float @ +0x28,  float @ +0x2c,  float @ +0x30,  float @ +0x34
//   float @ +0x38,  float @ +0x3c
//   int   @ +0x40
// GetVersion (vtable +0x18, 0x140048f20): mov eax,1; ret.
// Decompiled READ-ONLY from Application-x64.exe (Foundation C wave 2).
//
// ENDGAME B.3.8 K1: a REAL class (DiseaseDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140159310) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/DiseaseDescription.h"
#include "Component/DiseaseComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>

// ctor (inlined into create 0x140004e10 / createInPlace 0x140004e70):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
DiseaseDescription::DiseaseDescription()
    : Description(),
      firstMonth(0),
      lastMonth(0),
      yearsIfChanged(0.0f),
      yearsIfSame(0.0f),
      rollMonths(0.0f),
      killMonths(0.0f),
      spreadMonths(0.0f),
      durationMonths(0.0f),
      spreadRadius(0),
      _pad44(0) {}

// [5] 0x140048e40: stream order as retail.
bool DiseaseDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(firstMonth, nullptr);
    ser.Serialize(lastMonth, nullptr);
    ser.Serialize(yearsIfChanged, nullptr);
    ser.Serialize(yearsIfSame, nullptr);
    ser.Serialize(rollMonths, nullptr);
    ser.Serialize(killMonths, nullptr);
    ser.Serialize(spreadMonths, nullptr);
    ser.Serialize(durationMonths, nullptr);
    ser.Serialize(spreadRadius, nullptr);
    return true;
}

// [6] 0x140004ec0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* DiseaseDescription::Reconstruct() {
    this->~DiseaseDescription();
    return new (static_cast<void*>(this)) DiseaseDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void DiseaseDescription::Destruct() { this->~DiseaseDescription(); }

// [8] 0x140004f10: DiseaseComponent's type id through the hidden return slot.
Object::Type* DiseaseDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = DiseaseComponent::TypeId();
    return ret;
}

// [9] 0x140004e70: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* DiseaseDescription::Construct() {
    return new (static_cast<void*>(this)) DiseaseDescription();
}

// create 0x140004e10: MemAlloc(0x48, 8), then the (inlined) ctor.
::Object::Base* DiseaseDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(DiseaseDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) DiseaseDescription());
}
// createInPlace 0x140004e70.
::Object::Base* DiseaseDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) DiseaseDescription());
}

DEFINE_EXE_TYPE(DiseaseDescription, "Description")
