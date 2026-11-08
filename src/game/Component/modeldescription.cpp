// src/game/Component/modeldescription.cpp
// Clean reimplementation of ModelDescription (Foundation C).
//
// ModelDescription (0x40 bytes) — extends Object::External (0x20 base).
// Vtable @ 0x140163c20 (exe symbol PTR_ModelComponent_vslot16_140163c20).
// BaseTypeInfo @ 0x1401c7a88, parent "Description", file version 1.
//
// Field layout (from ModelDescription_Factory @ exe and Serialize @ 0x140124dc0):
//   +0x20  void*  meshGroupArray  (dyn-array of MeshGroup, 0x18 stride)
//   +0x28  int32  meshGroupCount
//   +0x30  int32  lod0            (int)
//   +0x34  int32  lod1            (int)
//   +0x38  bool   castShadow      (bool)
//
// MeshGroup element (stride 0x18):
//   elem+0x00 : External* mesh           (ExternalPtr<GraphicsMesh>; FUN_140035b40)
//   elem+0x08 : void*     materialArray  (dyn-array of External*, 0x08 stride)
//   elem+0x10 : int32     materialCount
//
// Serialize order per MeshGroup (exe 0x140124dc0):
//   1. mesh ExternalPtr (elem+0)         — fn_ExternalPtr_Serialize_GraphicsMesh
//   2. materialCount (int, elem+0x10)
//   3. resize materialArray (elem+8)     — fresh-load: alloc+zero
//   4. serialize each material External* — fn_Array_Serialize_External_GraphicsMesh
//
// Decompiled from Application-x64.exe (READ-ONLY Ghidra).
//
// ENDGAME B.3.8 K1: a REAL class (ModelDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140163c20) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/ModelDescription.h"
#include "Component/ModelComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/exe_bodies.h"   // Reimpl_ExternalPtr_Serialize_GraphicsMesh, Reimpl_Array_Serialize_External_GraphicsMesh
#include <cstdint>
#include <new>
#include "graphics/AnimationGroup.h"   // B8 RT2: ::Object::SerializeExternal<Graphics::AnimationGroup> (the downcast needs it complete)
#include "graphics/GraphicsMesh.h"   // B8 RT2: ::Object::SerializeExternalArray<Graphics::GraphicsMesh> (the downcast needs it complete)

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

// ctor (inlined into create 0x140035930 / createInPlace 0x1400359a0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
ModelDescription::ModelDescription()
    : Description(),
      _meshGroups(nullptr),
      _meshGroupCount(0),
      _meshGroupCap(0),
      _defaultMeshGroup(0),
      _defaultMeshIndex(0),
      _randomMeshGroup(false) {}

// dtor (slot 0 0x140035a70 minus the stamps): frees every MeshGroup's material array, then the group array.
// The compiler then chains ~Description -> ~External.
ModelDescription::~ModelDescription() {
    // Free each MeshGroup's mesh sub-array, then the MeshGroup array.
    ModelMeshGroup* e = _meshGroups;
    int count = _meshGroupCount;
    if (e) {
        for (int i = 0; i < count; ++i) {
            Graphics::GraphicsMesh** mat = e[i].meshes;
            if (mat) System::MemoryAllocator::MemFree(mat);
        }
        System::MemoryAllocator::MemFree(e);
    }
    _meshGroups = nullptr;
    _meshGroupCount = 0;
    _meshGroupCap = 0;
}

// [5] 0x140124dc0: stream order as retail.
bool ModelDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_meshGroupCount, nullptr);   // meshGroupCount
    int count = _meshGroupCount;
    
    bool loading = (static_cast<int>(ser.GetDirection()) == 0);   // IO::Serializer::GetDirection, retail 0x180006210
    ModelMeshGroup* e;
    if (loading) {
        AllocArray(_meshGroups, _meshGroupCount, _meshGroupCap, count);
        e = _meshGroups;
        for (int i = 0; i < count; ++i) {
            e[i].animations   = nullptr;   // ExternalPtr
            e[i].meshes       = nullptr;   // mesh array
            e[i].meshCount    = 0;         // mesh count + cap
            e[i].meshCapacity = 0;
        }
    } else {
        e = _meshGroups;
    }

    for (int i = 0; i < count; ++i, ++e) {
        ::Object::SerializeExternal<Graphics::AnimationGroup>(ser, e->animations);      // elem+0

        ser.Serialize(e->meshCount, nullptr);   // mesh count, elem+0x10
        int matCount = e->meshCount;

        if (loading) {
            AllocArray(e->meshes, e->meshCount, e->meshCapacity, matCount);
            for (int j = 0; j < matCount; ++j) e->meshes[j] = nullptr;
        }
        // serialize the mesh array of External* (elem+8 base)
        ::Object::SerializeExternalArray<Graphics::GraphicsMesh>(ser, matCount, e->meshes);
    }
    ser.Serialize(_defaultMeshGroup, nullptr);
    ser.Serialize(_defaultMeshIndex, nullptr);
    ser.Serialize(_randomMeshGroup, nullptr);
    return true;
}

// [6] 0x140035a00: `this->~T()` (retail: slot 0, flags 0 -- destroy in place), then the ctor again on
// `this`. The hand-built table's Cleanup trampoline already did this.
Object::External* ModelDescription::Reconstruct() {
    this->~ModelDescription();
    return new (static_cast<void*>(this)) ModelDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void ModelDescription::Destruct() { this->~ModelDescription(); }

// [8] 0x140035a60: ModelComponent's type id through the hidden return slot.
Object::Type* ModelDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = ModelComponent::TypeId();
    return ret;
}

// [9] 0x1400359a0: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* ModelDescription::Construct() {
    return new (static_cast<void*>(this)) ModelDescription();
}

// create 0x140035930: MemAlloc(0x40, 8), then the (inlined) ctor.
::Object::Base* ModelDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ModelDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ModelDescription());
}
// createInPlace 0x1400359a0.
::Object::Base* ModelDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ModelDescription());
}

DEFINE_EXE_TYPE(ModelDescription, "Description")
