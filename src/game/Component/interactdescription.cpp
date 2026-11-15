// src/game/Component/interactdescription.cpp
// Clean reimplementation of InteractDescription (entity sub-description).
//
// InteractDescription (0x58 bytes) — extends Object::External (0x20 base).
// Parent type name: "Description". Vtable @ 0x140163930.  GetVersion = 1.
//
// Field layout (from Factory InteractDescription_sub_3710 + Serialize
// FUN_140123500 + dtor FUN_140035280):
//   +0x20: void*  _points         ExternalPtr<Pathfinding::PointList> (default 0) —
//          the exe's serializer 0x14000ab20 type-checks it against
//          ?_typeInfo@PointList@Pathfinding@@ (import 0x140151448), B37_RULES 6.2
//   +0x28: { void* data; int count; int cap }   _useNodes   (0x18-stride, default empty)
//   +0x38: { void* data; int count; int cap }   _buildNodes (0x18-stride, default empty)
//   +0x48: { void* data; int count; int cap }   _createNodes(0x18-stride, default empty)
//
// The three node arrays are built in eh_vector_constructor_iterator (3 elements,
// stride 0x10) at +0x28 in the factory, and torn down in the dtor.
//
// Serialize (FUN_140123500):
//   STREAM-ACCURATE part: exactly ONE field is read from the stream —
//     ExternalPtr serialize (+0x20)  FUN_14000ab20 (identical layout to Serialize2).
//   POST-LOAD build (load only, computed — NOT streamed, so it never desyncs the
//   stream cursor): walks the resolved PointList's `_points` array
//   (Pathfinding::PointList::Point, stride 0x18). For each child whose name String == "use" /
//   "build" / "create", builds a 0x18-byte node {offset.xyz (child +0x8/+0xc/+0x10),
//   dir = Vector3::FromYawPitch(yaw=child+0x14, pitch=0)} and appends to the
//   corresponding node array (+0x28 / +0x38 / +0x48).
//
// Decompiled from Application-x64.exe (READ-ONLY Ghidra).
//
// ENDGAME B.3.8 K1: a REAL class (InteractDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140163930) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/InteractDescription.h"
#include "Component/InteractComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/exe_bodies.h"
#include "system/String.h"
#include "math/Math.h"
#include "io/Stream.h"
#include "pathfinding/PointList.h"
#include <cstring>
#include <initializer_list>
#include <cstdint>
#include <new>

namespace {
// Append one node to a node array {data, count, cap}.
// Mirrors FUN_140035360 for the incremental (count+1) growth case.
void AppendNode(InteractDescription::NodeArray* a, const InteractDescription::Node& src) {
    int count = a->count;
    int cap   = a->cap;
    InteractDescription::Node* oldBuf = a->data;
    int newCount = count + 1;
    if (cap < newCount) {
        int newCap = (count != 0) ? newCount * 3 : newCount;  // matches exe growth
        a->cap = newCap;
        InteractDescription::Node* nb = (System::MemoryAllocator::_singleton)
            ? static_cast<InteractDescription::Node*>(System::MemoryAllocator::MemAlloc((uint64_t)newCap * 0x18, 4))
            : nullptr;
        if (oldBuf && nb && count > 0) {
            for (int i = 0; i < count; ++i) nb[i] = oldBuf[i];
        }
        a->data = nb;
        if (oldBuf && oldBuf != nb) System::MemoryAllocator::MemFree(oldBuf);
    }
    a->count = newCount;
    InteractDescription::Node* buf = a->data;
    if (buf) buf[count] = src;
}
}  // namespace

// ctor (inlined into create 0x140035200 / createInPlace 0x140035240):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
InteractDescription::InteractDescription()
    : Description(),
      _points(nullptr) {
    _useNodes = _buildNodes = _createNodes = NodeArray{nullptr, 0, 0};
}

// dtor (slot 0 0x140035280 minus the stamps): frees the three node arrays (FUN_140035280).
// The compiler then chains ~Description -> ~External.
InteractDescription::~InteractDescription() {
    {
        for (NodeArray* a : {&_useNodes, &_buildNodes, &_createNodes}) {
            System::MemoryAllocator::MemFree(a->data);
            a->data = nullptr;
        }
    }
}

// [5] 0x140123500: stream order as retail.
bool InteractDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    // ---- stream-accurate: single ExternalPtr (+0x20) ----
    ::Object::SerializeExternal<Pathfinding::PointList>(ser, _points);
    
    // ---- post-load build (load only; reads from the resolved external) ----
    // Direction flag: retail tests *(int*)(stream + 8) == 0 (FUN_140123500 reads
    // param_2+8 DIRECTLY). The old read `*(serializer+8)` grabbed the serializer's
    // second qword instead of the stream's direction field -> `loading` was always
    // false -> the use/build/create node lists never built -> ChildSum(interact)==0
    // -> d5d0 found no access point -> the eat/food search rejected every barn
    // (Economy Brick M-D root cause #3).
    bool loading = (static_cast<int>(ser.GetDirection()) == 0);   // IO::Serializer::GetDirection, retail 0x180006210 (the stream's +8)
    const Pathfinding::PointList* pointList = _points;
    if (loading && pointList) {
        // Pathfinding::PointList::_points (+0x30; Points() is LEA RAX,[RCX+0x30]).
        const Pathfinding::PointList::Point* children = pointList->_points._data;
        int childCount = pointList->_points._size;
        for (int i = 0; children && i < childCount; ++i) {
            const Pathfinding::PointList::Point& child = children[i];  // {String name, pos[3], yaw}
            float yaw   = child.radius;   // +0x14: the point's yaw (PointList calls it radius)
            float pitch = 0.0f;
            // node = {offset.xyz, dir.xyz}
            InteractDescription::Node node;
            node.offset = Math::Vector3(child.pos.x, child.pos.y, child.pos.z);
            node.direction = Math::Vector3::FromYawPitch(yaw, pitch);

            const System::String& name = child.name;
            if (name == "use") {
                AppendNode(&_useNodes, node);
            } else if (name == "build") {
                AppendNode(&_buildNodes, node);
            } else if (name == "create") {
                AppendNode(&_createNodes, node);
            }
        }
    }
    return true;
}

// [6] 0x140035250: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* InteractDescription::Reconstruct() {
    this->~InteractDescription();
    return new (static_cast<void*>(this)) InteractDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void InteractDescription::Destruct() { this->~InteractDescription(); }

// [8] 0x140035270: InteractComponent's type id through the hidden return slot.
Object::Type* InteractDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = InteractComponent::TypeId();
    return ret;
}

// [9] 0x140035240: placement-new on `this`, the same body as createInPlace (D9 #10).
Object::External* InteractDescription::Construct() {
    return new (static_cast<void*>(this)) InteractDescription();
}

// create 0x140035200: MemAlloc(0x58, 8), then the (inlined) ctor.
::Object::Base* InteractDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(InteractDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) InteractDescription());
}
// createInPlace 0x140035240.
::Object::Base* InteractDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) InteractDescription());
}

DEFINE_EXE_TYPE(InteractDescription, "Description")
