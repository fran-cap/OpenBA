// src/game/Component/highlightdescription.cpp
// Clean reimplementation of HighlightDescription (Foundation C).
//
// HighlightDescription (0x40 bytes) — extends Object::External (0x20 base).
// Vtable @ 0x140159a10 (exe symbol PTR_HighlightComponent_vslot16_140159a10).
// BaseTypeInfo @ 0x1401c4448, parent "Description", file version 2.
//
// Field layout (from HighlightDescription_Factory @ exe and Serialize @ 0x14004c9d0):
//   +0x20  void*  groupArray   (dyn-array, 0x10 stride)
//   +0x28  int32  groupCount
//   +0x30  External* ref0       (ExternalPtr; FUN_140007140)
//   +0x38  External* ref1       (ExternalPtr; FUN_140007140)
//
// Group element (stride 0x10):
//   elem+0x00 : void*  refArray   (dyn-array of External*, 0x08 stride)
//   elem+0x08 : int32  refCount
//
// Serialize order per group (exe 0x14004c9d0):
//   1. refCount (int, elem+8)
//   2. resize refArray (elem+0)            — fresh-load alloc+zero
//   3. serialize each External* ref        — fn_Array_Serialize_External_GraphicsMesh
// Tail: ref0 ExternalPtr (+0x30), ref1 ExternalPtr (+0x38) via fn_ExternalPtr_Serialize3.
//
// Decompiled from Application-x64.exe (READ-ONLY Ghidra).
//
// ENDGAME B.3.8 K1: a REAL class (HighlightDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140159a10) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/HighlightDescription.h"
#include "Component/HighlightComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/exe_bodies.h"   // Reimpl_Array_Serialize_External_GraphicsMesh, Reimpl_ExternalPtr_Serialize_Typed_7140
#include <cstdint>
#include <new>
#include "graphics/Material.h"   // B8 RT2: ::Object::SerializeExternal<Graphics::MaterialInstance> (the downcast needs it complete)
#include "graphics/Mesh.h"   // B8 RT2: ::Object::SerializeExternalArray<Graphics::EdgeMesh> (the downcast needs it complete)

namespace {
// {data, count, cap}: set count = cap = n and allocate n elements (8-aligned).
template <class T>
T* AllocArray(T*& data, int32_t& size, int32_t& capacity, int count) {
    size = count;
    capacity = count;
    data = (count > 0 && System::MemoryAllocator::_singleton)
           ? static_cast<T*>(System::MemoryAllocator::MemAlloc((uint64_t)count * (uint32_t)sizeof(T), 8))
           : nullptr;
    return data;
}
}  // namespace

// ctor (inlined into create 0x140006ea0 / createInPlace 0x140006f10):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
HighlightDescription::HighlightDescription()
    : Description(),
      _groups(nullptr),
      _groupCount(0),
      _groupCap(0),
      _maskMaterial(nullptr),
      _edgeMaterial(nullptr) {}

// dtor (slot 0 0x140006fe0 minus the stamps): frees every group's ref array, then the group array.
// The compiler then chains ~Description -> ~External.
HighlightDescription::~HighlightDescription() {
    HighlightGroup* e = _groups;
    int count = _groupCount;
    if (e) {
        for (int i = 0; i < count; ++i) {
            Graphics::EdgeMesh** ref = e[i].refs;
            if (ref) System::MemoryAllocator::MemFree(ref);
        }
        System::MemoryAllocator::MemFree(e);
    }
    _groups = nullptr;
    _groupCount = 0;
    _groupCap = 0;
}

// [5] 0x14004c9d0: stream order as retail.
bool HighlightDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_groupCount, nullptr);   // groupCount
    int count = _groupCount;
    
    bool loading = (static_cast<int>(ser.GetDirection()) == 0);   // IO::Serializer::GetDirection, retail 0x180006210
    HighlightGroup* e;
    if (loading) {
        e = AllocArray(_groups, _groupCount, _groupCap, count);
        for (int i = 0; i < count; ++i) {
            e[i].refs        = nullptr;   // refArray
            e[i].refCount    = 0;         // refCount + cap
            e[i].refCapacity = 0;
        }
    } else {
        e = _groups;
    }

    for (int i = 0; i < count; ++i, ++e) {
        ser.Serialize(e->refCount, nullptr);   // refCount (elem+8)
        int refCount = e->refCount;
        if (loading) {
            Graphics::EdgeMesh** r = AllocArray(e->refs, e->refCount, e->refCapacity, refCount);
            for (int j = 0; j < refCount; ++j) r[j] = nullptr;
        }
        ::Object::SerializeExternalArray<Graphics::EdgeMesh>(ser, refCount, e->refs);   // 0x140007580 (0x14004ca7d)
    }
    ::Object::SerializeExternal<Graphics::MaterialInstance>(ser, _maskMaterial);
    ::Object::SerializeExternal<Graphics::MaterialInstance>(ser, _edgeMaterial);
    return true;
}

// [6] 0x140006f70: `this->~T()` (retail: slot 0, flags 0 -- destroy in place), then the ctor again on
// `this`. The hand-built table's Cleanup trampoline already did this.
Object::External* HighlightDescription::Reconstruct() {
    this->~HighlightDescription();
    return new (static_cast<void*>(this)) HighlightDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void HighlightDescription::Destruct() { this->~HighlightDescription(); }

// [8] 0x140006fd0: HighlightComponent's type id through the hidden return slot.
Object::Type* HighlightDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = HighlightComponent::TypeId();
    return ret;
}

// [9] 0x140006f10: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* HighlightDescription::Construct() {
    return new (static_cast<void*>(this)) HighlightDescription();
}

// create 0x140006ea0: MemAlloc(0x40, 8), then the (inlined) ctor.
::Object::Base* HighlightDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HighlightDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HighlightDescription());
}
// createInPlace 0x140006f10.
::Object::Base* HighlightDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HighlightDescription());
}

DEFINE_EXE_TYPE(HighlightDescription, "Description")
