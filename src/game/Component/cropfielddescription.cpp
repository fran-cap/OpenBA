// src/game/Component/cropfielddescription.cpp
// CropFieldDescription (entity description block) — crop-field farming parameters.
// Extends Object::External (0x20 base). Vtable 0x140162f50. Parent "Description".
// Size 0x28. GetVersion=3.
//
// Factory 0x14014ed10-area (MemAlloc 0x28 + vtable + defaults). Defaults:
//   enum @ +0x20 = 5   (8-byte store -> +0x20=5, +0x24=0)
// Serialize (vtable +0x28, 0x14011b270), stream order:
//   float @ +0x24
//   enum  @ +0x20   (serializer-encoding vtable +0x98 dispatch, version-gated)
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (CropFieldDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140162f50) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/CropFieldDescription.h"
#include "Component/CropFieldComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>

// ctor (inlined into create 0x140032f30 / createInPlace 0x140032f90):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
CropFieldDescription::CropFieldDescription()
    : Description(),
      _type(5),
      _growthPercentOnTend(0.0f) {}

// [5] 0x14011b270: stream order as retail.
bool CropFieldDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 1);
    ser.Serialize(_growthPercentOnTend, nullptr);
    if (ser.GetVersion() > 0) ser.Serialize(_type, nullptr);   // version-gated, as retail
    return true;
}

// [6] 0x140032fe0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* CropFieldDescription::Reconstruct() {
    this->~CropFieldDescription();
    return new (static_cast<void*>(this)) CropFieldDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void CropFieldDescription::Destruct() { this->~CropFieldDescription(); }

// [8] 0x140033030: CropFieldComponent's type id through the hidden return slot.
Object::Type* CropFieldDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = CropFieldComponent::TypeId();
    return ret;
}

// [9] 0x140032f90: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* CropFieldDescription::Construct() {
    return new (static_cast<void*>(this)) CropFieldDescription();
}

// create 0x140032f30: MemAlloc(0x28, 8), then the (inlined) ctor.
::Object::Base* CropFieldDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CropFieldDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CropFieldDescription());
}
// createInPlace 0x140032f90.
::Object::Base* CropFieldDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CropFieldDescription());
}

DEFINE_EXE_TYPE(CropFieldDescription, "Description")
