// src/game/Component/pickingdescription.cpp
// Clean reimplementation of PickingDescription (Foundation C).
//
// PickingDescription (0x40 bytes) — extends Object::External (0x20 base).
// Vtable @ 0x1401640c0 (exe symbol PTR_PickingComponent_vslot16_1401640c0).
// BaseTypeInfo @ 0x1401c7c48, parent "Description", file version 4.
//
// Field layout (from PickingDescription_Factory @ exe and Serialize @ 0x140128d00):
//   +0x20  void*  groupArray   (dyn-array, 0x10 stride)
//   +0x28  int32  groupCount
//   +0x30  External* _material (ExternalPtr<Graphics::MaterialInstance>; FUN_140007140)
//   +0x38  bool   enabled
//
// Group element (stride 0x10):
//   elem+0x00 : void*  refArray   (dyn-array of External*, 0x08 stride)
//   elem+0x08 : int32  refCount
//
// Serialize order per group (exe 0x140128d00):
//   1. refCount (int, elem+8)
//   2. resize refArray (elem+0)            — fresh-load alloc+zero
//   3. serialize each External* ref        — 0x140037000, the PickingMesh array instance
// Tail: _material ExternalPtr (+0x30) via ::Object::SerializeExternal, then bool (+0x38).
//
// Decompiled from Application-x64.exe (READ-ONLY Ghidra).
//
// ENDGAME B.3.8 K1: a REAL class (PickingDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x1401640c0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/PickingDescription.h"
#include "Component/PickingComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "picking/PickingMesh.h"
#include "system/MemoryAllocator.h"
#include <cstdint>
#include <new>
#include "graphics/Material.h"   // B8 RT2: ::Object::SerializeExternal<Graphics::MaterialInstance> (the downcast needs it complete)

// ctor (inlined into create 0x140036e00 / createInPlace 0x140036e70):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
PickingDescription::PickingDescription()
    : Description(),
      _groups(nullptr),
      _groupCount(0),
      _groupCap(0),
      _material(nullptr),
      _enabled(false) {}

// dtor (slot 0 0x140036f40 minus the stamps): frees every group's ref array, then the group array.
// The compiler then chains ~Description -> ~External.
PickingDescription::~PickingDescription() {
    Group* e = _groups;
    int count = _groupCount;
    if (e) {
        for (int i = 0; i < count; ++i) {
            Picking::PickingMesh** ref = e[i].meshes;
            if (ref) System::MemoryAllocator::MemFree(ref);
        }
        System::MemoryAllocator::MemFree(e);
    }
    _groups = nullptr;
    _groupCount = 0;
    _groupCap = 0;
}

// [5] 0x140128d00: stream order as retail.
bool PickingDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_groupCount, nullptr);   // groupCount
    int count = _groupCount;

    bool loading = (static_cast<int>(ser.GetDirection()) == 0);   // IO::Serializer::GetDirection, retail 0x180006210
    Group* e;
    if (loading) {
        // count = capacity = n, a fresh n-element block (8-aligned), every group zeroed.
        _groupCount = count;
        _groupCap = count;
        _groups = (count > 0)
                  ? static_cast<Group*>(System::MemoryAllocator::MemAlloc((uint64_t)count * (uint32_t)sizeof(Group), 8))
                  : nullptr;
        e = _groups;
        for (int i = 0; i < count; ++i) {
            e[i].meshes = nullptr;
            e[i].meshCount = 0;
            e[i].meshCapacity = 0;
        }
    } else {
        e = _groups;
    }

    for (int i = 0; i < count; ++i, ++e) {
        ser.Serialize(e->meshCount, nullptr);
        int refCount = e->meshCount;
        if (loading) {
            e->meshCount = refCount;
            e->meshCapacity = refCount;
            e->meshes = (refCount > 0)
                        ? static_cast<Picking::PickingMesh**>(System::MemoryAllocator::MemAlloc((uint64_t)refCount * 8u, 8))
                        : nullptr;
            for (int j = 0; j < refCount; ++j) e->meshes[j] = nullptr;
        }
        // 0x140037000: Array<ExternalPtr<Picking::PickingMesh>>::Serialize (PickingMesh's _typeInfo,
        // import 0x140151468, read at 0x140037087 / 0x1400371f0; B8 RT2).
        ::Object::SerializeExternalArray<Picking::PickingMesh>(ser, refCount, e->meshes);
    }
    ::Object::SerializeExternal<Graphics::MaterialInstance>(ser, _material);   // 0x140007140
    ser.Serialize(_enabled, nullptr);
    return true;
}

// [6] 0x140036ed0: `this->~T()` (retail: slot 0, flags 0 -- destroy in place), then the ctor again on
// `this`. The hand-built table's Cleanup trampoline already did this.
Object::External* PickingDescription::Reconstruct() {
    this->~PickingDescription();
    return new (static_cast<void*>(this)) PickingDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void PickingDescription::Destruct() { this->~PickingDescription(); }

// [8] 0x140036f30: PickingComponent's type id through the hidden return slot.
Object::Type* PickingDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = PickingComponent::TypeId();
    return ret;
}

// [9] 0x140036e70: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* PickingDescription::Construct() {
    return new (static_cast<void*>(this)) PickingDescription();
}

// create 0x140036e00: MemAlloc(0x40, 8), then the (inlined) ctor.
::Object::Base* PickingDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PickingDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PickingDescription());
}
// createInPlace 0x140036e70.
::Object::Base* PickingDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PickingDescription());
}

DEFINE_EXE_TYPE(PickingDescription, "Description")
