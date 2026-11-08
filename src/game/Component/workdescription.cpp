// src/game/Component/workdescription.cpp
// Clean reimplementation of WorkDescription (entity description block).
//
// WorkDescription (0x38 bytes) — work-cost parameters for a job.
// Extends Object::External (0x20 base). Vtable at 0x140164c60.
// Parent type "Description".  (ToolKit: WorkDescription = work cost.)
//
// Factory  : 0x140039ed0  (MemAlloc 0x38 + ctor sub_e9b0 0x14012e9b0)
// CreateInPlace: 0x140039f10
// Ctor defaults (from WorkDescription_sub_e9b0):
//   bool @ +0x20 = 0
//   int  @ +0x24 = 0
//   int  @ +0x28 = 1
//   int  @ +0x2c = 100
//   int  @ +0x30 = 1
//   bool @ +0x34 = 0
// Serialize (vtable +0x28, 0x14012e8b0) — NOTE the non-monotonic field order:
//   int  @ +0x24
//   int  @ +0x28
//   int  @ +0x2c
//   bool @ +0x34
//   int  @ +0x30
//   bool @ +0x20
// GetVersion (vtable +0x18, 0x14012e960): mov eax,4; ret -> file version 4.
// Dtor (vtable +0x00, 0x140039f50): vtable reset + External::~External + free.
//
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (WorkDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140164c60) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/WorkDescription.h"
#include "Component/WorkComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <new>

// ctor (inlined into create 0x140039ed0 / createInPlace 0x140039f10):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
WorkDescription::WorkDescription()
    : Description(),
      _allowCountChange(false),
      _defaultWorkers(0),
      _minWorkerCount(1),
      _maxWorkerCount(100),
      _maxArea(1),
      _scaleWorkersWithSize(false) {}

// [5] 0x14012e8b0: stream order as retail.
bool WorkDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_defaultWorkers, nullptr);
    ser.Serialize(_minWorkerCount, nullptr);
    ser.Serialize(_maxWorkerCount, nullptr);
    ser.Serialize(_scaleWorkersWithSize, nullptr);
    ser.Serialize(_maxArea, nullptr);
    ser.Serialize(_allowCountChange, nullptr);
    return true;
}

// [6] 0x140039f20: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* WorkDescription::Reconstruct() {
    this->~WorkDescription();
    return new (static_cast<void*>(this)) WorkDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void WorkDescription::Destruct() { this->~WorkDescription(); }

// [8] 0x140039f40: WorkComponent's type id through the hidden return slot.
Object::Type* WorkDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = WorkComponent::TypeId();
    return ret;
}

// [9] 0x140039f10: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* WorkDescription::Construct() {
    return new (static_cast<void*>(this)) WorkDescription();
}

// create 0x140039ed0: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* WorkDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(WorkDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) WorkDescription());
}
// createInPlace 0x140039f10.
::Object::Base* WorkDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) WorkDescription());
}

DEFINE_EXE_TYPE(WorkDescription, "Description")
