// src/game/Component/residencedescription.cpp
// Clean reimplementation of ResidenceDescription (entity description block).
//
// ResidenceDescription (0x30 bytes) — housing parameters for a residence.
// Extends Object::External (0x20 base). Vtable at 0x140164350.
// Parent type "Description".  (ToolKit: ResidenceDescription = housing.)
//
// Factory  : 0x1400374a0  (MemAlloc 0x30 + ctor)
// CreateInPlace: 0x140037500  (the exe ctor only sets the vtable; the four
//   serialized fields at +0x20..+0x2c are left uninitialised by the exe and
//   filled by Serialize on load. We zero them for safety.)
// Serialize (vtable +0x28, 0x14012a3d0), stream order:
//   int   @ +0x20   maxFamilies — family-slot count; ResidenceComponent's
//                    Startup resizes the slot array (+0x50/+0x58) to this.
//                    Boarding house = maxFamilies > 1; wood/stone house = 1.
//                    No special boarding-house code exists in retail.
//   int   @ +0x24   maxResidentsPerFamily — read by the slot-full test
//                    0x14012a4f0 (residencecomponent.cpp): slot is full when
//                    occupantCount >= this.
//   float @ +0x28   coldThreshold — temp below which the house burns fuel
//                    (stone houses ship lower values in data = less fuel)
//   float @ +0x2c   heatInterval — fuel-consumption period (heat tick)
// GetVersion (vtable +0x18, 0x14012a460): mov eax,2; ret -> file version 2.
// Dtor (vtable +0x00, 0x1400375b0): vtable reset + External::~External + free
//   (no owned String/array fields).
//
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (ResidenceDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140164350) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/ResidenceDescription.h"
#include "Component/ResidenceComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>

// ctor (inlined into create 0x1400374a0 / createInPlace 0x140037500):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
ResidenceDescription::ResidenceDescription()
    : Description(),
      _maxFamilies(0),
      _maxResidentsPerFamily(0),
      _coldThreshold(0.0f),
      _heatInterval(0.0f) {}

// [5] 0x14012a3d0: stream order as retail.
bool ResidenceDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_maxFamilies, nullptr);
    ser.Serialize(_maxResidentsPerFamily, nullptr);
    ser.Serialize(_coldThreshold, nullptr);
    ser.Serialize(_heatInterval, nullptr);
    return true;
}

// [6] 0x140037550: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* ResidenceDescription::Reconstruct() {
    this->~ResidenceDescription();
    return new (static_cast<void*>(this)) ResidenceDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void ResidenceDescription::Destruct() { this->~ResidenceDescription(); }

// [8] 0x1400375a0: ResidenceComponent's type id through the hidden return slot.
Object::Type* ResidenceDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = ResidenceComponent::TypeId();
    return ret;
}

// [9] 0x140037500: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* ResidenceDescription::Construct() {
    return new (static_cast<void*>(this)) ResidenceDescription();
}

// create 0x1400374a0: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* ResidenceDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ResidenceDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ResidenceDescription());
}
// createInPlace 0x140037500.
::Object::Base* ResidenceDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ResidenceDescription());
}

DEFINE_EXE_TYPE(ResidenceDescription, "Description")
