// src/game/Component/attendancedescription.cpp
// AttendanceDescription (entity description block) — gathering/attendance parameters.
// Extends Object::External (0x20 base). Vtable 0x140158d70. Parent "Description".
// Size 0x28. GetVersion=1.
//
// Factory 0x14014a740-area (MemAlloc 0x28). Ctor default: int @ +0x20 = 0xf (15).
// Serialize (vtable +0x28, 0x1400446b0), stream order:  int @ +0x20.
// GetVersion (vtable +0x18, 0x140044700): mov eax,1; ret.
// Decompiled READ-ONLY from Application-x64.exe (Foundation C wave 2).
//
// ENDGAME B.3.8 K1: a REAL class (AttendanceDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140158d70) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/AttendanceDescription.h"
#include "Component/AttendanceComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>

// ctor (inlined into create 0x1400026f0 / createInPlace 0x140002750):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
AttendanceDescription::AttendanceDescription()
    : Description(),
      _maxCitizens(0xf) {}

// [5] 0x1400446b0: stream order as retail.
bool AttendanceDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_maxCitizens, nullptr);
    return true;
}

// [6] 0x1400027a0: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* AttendanceDescription::Reconstruct() {
    this->~AttendanceDescription();
    return new (static_cast<void*>(this)) AttendanceDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void AttendanceDescription::Destruct() { this->~AttendanceDescription(); }

// [8] 0x1400027f0: AttendanceComponent's type id through the hidden return slot.
Object::Type* AttendanceDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = AttendanceComponent::TypeId();
    return ret;
}

// [9] 0x140002750: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* AttendanceDescription::Construct() {
    return new (static_cast<void*>(this)) AttendanceDescription();
}

// create 0x1400026f0: MemAlloc(0x28, 8), then the (inlined) ctor.
::Object::Base* AttendanceDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AttendanceDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AttendanceDescription());
}
// createInPlace 0x140002750.
::Object::Base* AttendanceDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AttendanceDescription());
}

DEFINE_EXE_TYPE(AttendanceDescription, "Description")
