// src/game/Component/storagedescription.cpp
// Clean reimplementation of StorageDescription (Foundation C).
//
// StorageDescription (0x40 bytes) — extends Object::External (0x20 base).
// Vtable @ 0x1401647a0 (exe symbol PTR_StorageComponent_vslot16_1401647a0).
// BaseTypeInfo @ 0x1401c7eb0, parent "Description", file version 2.
//
// Field layout (from StorageDescription_Factory @ exe and Serialize @ 0x14012c9a0):
//   +0x20  bool   flagA               (default 0)
//   +0x21  bool   flagB               (default 0)
//   +0x24  int32  pickupValue         (default 0)
//   +0x28  int32  rawMaterialFlags    (enum; serialized via serializer vtable+0x98)
//   +0x30  Volume* volumeArray        (dyn-array of 0x10-stride entries)
//   +0x38  int32  volumeCount
//
// The +0x30 array element layout (stride 0x10):
//   elem+0x00 : External* ref  (ExternalPtr; serialized via Object::SerializeExternal<ComponentDescription>, 0x140002340)
//   elem+0x08 : int32          (serialized as int)
//
// Decompiled from Application-x64.exe (READ-ONLY Ghidra).
//
// ENDGAME B.3.8 K1: a REAL class (StorageDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x1401647a0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/StorageDescription.h"
#include "Component/StorageComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include <cstdint>
#include <new>
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x140038bb0 / createInPlace 0x140038c20):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
StorageDescription::StorageDescription()
    : Description(),
      _flagA(false),
      _flagB(false),
      _pickupValue(0),
      _rawMaterialFlags(0),
      _volumes(nullptr),
      _volumeCount(0),
      _volumeCap(0) {}

// dtor (slot 0 0x140038cf0 minus the stamps): frees the volume array.
// The compiler then chains ~Description -> ~External.
StorageDescription::~StorageDescription() {
    if (_volumes) System::MemoryAllocator::MemFree(_volumes);
    _volumes = nullptr;
    _volumeCount = 0;
    _volumeCap = 0;
}

// [5] 0x14012c9a0: stream order as retail.
bool StorageDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_rawMaterialFlags, nullptr);   // rawMaterialFlags (enum, uint door)
    ser.Serialize(_flagA, nullptr);
    ser.Serialize(_flagB, nullptr);
    ser.Serialize(_pickupValue, nullptr);
    
    ser.Serialize(_volumeCount, nullptr);   // volumeCount
    int count = _volumeCount;
    
    bool loading = (static_cast<int>(ser.GetDirection()) == 0);   // IO::Serializer::GetDirection, retail 0x180006210
    Volume* e;
    if (loading) {
        // The tree's load: count = capacity = n, allocate exactly n (8-aligned), zero
        // each element. (Retail calls the array's SetSize 0x14000da70 here.)
        _volumeCount = count;
        _volumeCap = count;
        _volumes = (count > 0)
                   ? static_cast<Volume*>(System::MemoryAllocator::MemAlloc((uint64_t)count * 0x10u, 8))
                   : nullptr;
        e = _volumes;
        for (int i = 0; i < count; ++i) {
            e[i]._resource = nullptr;   // External* ref
            e[i]._count    = 0;         // int (+pad: H7, the tree's one qword store)
            e[i]._pad0C    = 0;
        }
    } else {
        e = _volumes;
    }
    for (int i = 0; i < count; ++i, ++e) {
        ser.Serialize(e->_count, nullptr);                                  // int field, elem+8
        ::Object::SerializeExternal<ComponentDescription>(ser, e->_resource);      // External* ref (elem+0), 0x140002340
    }
    return true;
}

// [6] 0x140038c80: `this->~T()` (retail: slot 0, flags 0 -- destroy in place), then the ctor again on
// `this`. The hand-built table's Cleanup trampoline already did this.
Object::External* StorageDescription::Reconstruct() {
    this->~StorageDescription();
    return new (static_cast<void*>(this)) StorageDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void StorageDescription::Destruct() { this->~StorageDescription(); }

// [8] 0x140038ce0: StorageComponent's type id through the hidden return slot.
Object::Type* StorageDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = StorageComponent::TypeId();
    return ret;
}

// [9] 0x140038c20: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* StorageDescription::Construct() {
    return new (static_cast<void*>(this)) StorageDescription();
}

// create 0x140038bb0: MemAlloc(0x40, 8), then the (inlined) ctor.
::Object::Base* StorageDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(StorageDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) StorageDescription());
}
// createInPlace 0x140038c20.
::Object::Base* StorageDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) StorageDescription());
}

DEFINE_EXE_TYPE(StorageDescription, "Description")
