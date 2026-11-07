// ============================================================================
// src/engine/graphics/animationstate.cpp
// Graphics::AnimationState / AnimationManager / Animation — the serializable
// half of the skeleton-animation cluster (cutover closure batch 3, 2026-09-12).
// ============================================================================
//
// WHAT IS IN HERE, with retail addresses. Four are exported; the rest are
// template instantiations and per-record helpers with no name in either build,
// registered by address.
//
//   0x1800127f0  ??0Node@GraphicsMesh@Graphics@@QEAA@AEBU012@@Z   copy ctor
//   0x18005b660  (unexported)  Graphics::AnimationState copy ctor
//   0x18005b700  (unexported)  AnimationManager::Instance copy ctor
//
//   0x180059820  (unexported)  Array<Math::Transform>::Serialize
//   0x18005c230  (unexported)  Array<InterpolatedState<Transform>>::Serialize
//   0x18005c120  (unexported)  Array<GraphicsMesh::Node>::Serialize
//   0x18005cb40  (unexported)  Array<AnimationState::Layer>::Serialize
//   0x18005c2c0  (unexported)  Array<AnimationState>::Serialize
//   0x18005c660  (unexported)  Array<AnimationManager::Instance>::Serialize
//
//   0x1800fe250  ?Serialize@AnimationState@Graphics@@QEAAXAEAVSerializer@IO@@@Z
//   0x1800fd930  ?ResetAnimationState@AnimationState@Graphics@@QEAAXXZ
//   0x1800fb8e0  ?GetPose@AnimationManager@Graphics@@QEBAXHPEAVTransform@Math@@H@Z
//   0x1800faca0  ?GetPoseAtTime@Animation@Graphics@@QEBAXAEBVTime@System@@0PEAVTransform@Math@@H@Z
//
// The Array<T> instantiations these bodies call live in
// src/engine/collection/array_graphics_anim.cpp, with the Collection family
// they belong to.
//
// ---------------------------------------------------------------------------
// THE INSTANCE HANDLE, because three bodies here decode it and none of them
// says what it is
// ---------------------------------------------------------------------------
// `AnimationState::Layer::handle` (+0x0c) is a packed pair, or -1 for "this
// layer is not instanced". `GetInstanceId` builds it as `instanceIndex << 8 |
// frameIndex` on its last line; `GetPose` and `ResetAnimationState` take it
// apart with `(h >> 8) & 0xff` and `h & 0xff`. So there is a hard ceiling of
// 256 cached instances and 256 frames per instanced clip, and the layer stores
// no more than one byte of each. That ceiling is retail's, not ours.
//
// ---------------------------------------------------------------------------
// THE HIDDEN RETURN SLOT, and the one place it applies here
// ---------------------------------------------------------------------------
// `Math::Quaternion::Lerp` (0x18001d500) returns a Quaternion BY VALUE, so
// MSVC passes the destination buffer in RDX and shifts the real arguments
// right: RCX = the FROM quaternion, RDX = the return buffer, R8 = the TO
// quaternion, R9 = &t. The typedef below carries that slot. Getting it wrong
// is a wild write into whatever the caller had in RDX
// (reference_hidden_retslot_wild_write.md); it is spelled the same way
// src/engine/graphics/mesh.cpp:1801 and scene.cpp:366 already spell it, so
// there is one shape to get right and not three.
//
// PORTABILITY: no <windows.h>, per PLAN §4.1 decision 1.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>                 // placement new, for the Node copy-ctor door

#include "system/String.h"
#include "collection/Collection.h"
#include "graphics/AnimationManager.h"
#include "graphics/GraphicsMesh.h"   // ENDGAME B.3.5 block G
// ENDGAME B.3.5 block H1: the real `Graphics::Animation` / `AnimationState` /
// `AnimationManager` classes, and `System::Time` for the two tick parameters
// `Animation::GetPoseAtTime` takes by reference. Value.h is what drags the ONE
// global `struct Time` (engine/types/common.h) into this translation unit; no
// other header here declares one, so there is nothing for it to collide with.
#include "graphics/Animation.h"
#include "graphics/AnimationState.h"
#include "system/Value.h"
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "io/Serializer.h"
#include "io/Stream.h"             // B.4: IO::Stream::_direction (IsReading)
#include "object/External.h"         // Object::SerializeExternal<AnimationGroup>
#include "math/Math.h"               // Math::Quaternion::Lerp
#include "system/MemoryAllocator.h"
#include "graphics/AnimationGroup.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include "system/string_internal.h"                  // B.4: RtString_ReleaseInline (~Node)
#include "../picking/scene_grid.h"               // rtpickgrid::TransformArrayAssign

using Graphics::AnimationManager;
using Graphics::AnimationState;
using Graphics::GraphicsMesh;
using Graphics::InterpolatedState;

namespace {

// ---------------------------------------------------------------------------
// The neighbours these bodies call, by name (B3R). The Collection::Array<T>
// instantiations have no name in either shipped build: rtcoll::Rt_SetSize_* /
// Rt_ArrayAssign_* (array_graphics_anim.cpp, declared in AnimationState.h),
// rtcoll::RtColl_ArraySetSize_180059140 (Array<Math::Transform>::SetSize),
// rtpickgrid::TransformArrayAssign (0x18001b360, Array<Math::Transform>::
// operator=). ExternalPtr<AnimationGroup>::Serialize is
// RtObject_ExternalPtrSerialize_AnimationGroup (0x18005b840) and
// Math::Quaternion::Lerp (0x18001d500) is the member.
// ---------------------------------------------------------------------------
using rtpickgrid::TransformArrayAssign;

// ?_secondsPerTick@Timer@System@@0NA — a DOUBLE. Every tick<->seconds
// conversion in this file goes through it, exactly as retail's inlined
// `CVTSI2SD / MULSD [rip+_secondsPerTick]` does.
double SecondsPerTick() {
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    return ::System::Timer::_secondsPerTick;
}

// `stream->direction == 0` is IO::Stream::Direction::Reading. Every "if we are
// loading, resize the destination first" guard below is retail's
// `MOV RAX,[RDI]; CMP dword ptr [RAX+8],0; JNZ` -- the inlined
// `?GetDirection@Serializer@IO@@`.
bool IsReading(const IO::Serializer& ser) {
    return ser._stream->_direction == IO::kReading;
}

// ExternalPtr<AnimationGroup>::Serialize (0x18005b840). LEFT: the object
// block's entry point still takes the `RtObject::External**` / `rt::io::
// Serializer*` PODs (src/engine/object/External.h:1046).
void SerializeGroupPtr(Graphics::AnimationGroup*& group, IO::Serializer& ser) {
    Object::SerializeExternal<Graphics::AnimationGroup>(ser, group);
}

}  // namespace

// ============================================================================
// The three element copy constructors
// ============================================================================

// 0x1800127f0 — ??0Node@GraphicsMesh@Graphics@@QEAA@AEBU012@@Z.
//
// A refcounting copy of the node name followed by a flat fifteen-dword copy of
// everything from +0x08 to +0x44. The name slot is stored ZERO first and only
// then given the source's pointer, which is what makes this safe to run into
// raw MemAlloc memory — and is why Array<Node>::operator= can call it directly
// on a buffer it has just allocated.
//
// *** +0x44..+0x47 IS NOT COPIED. *** The last dword the loop writes is at
// +0x40, so the copy covers +0x08..+0x43 and the tail dword at +0x44 keeps
// whatever the destination held. Zeroing it here would be unfaithful and would
// make a byte comparator fail on bytes neither side writes.
//
// ENDGAME B.3.5 block G: BODY MOVED to `Graphics::GraphicsMesh::Node`'s copy
// constructor, at the bottom of this file's `namespace Graphics` block.
//
// B.4: the element op array_graphics_anim.cpp's spec row calls with untyped
// element pointers (`rtcoll::ElemAssign`); that signature is the collection
// block's, so the two element views stay here (floor: elem_stride).
extern "C" void* RtGraphics_MeshNode_CopyCtor(void* dstv, const void* srcv) {
    return new (dstv) GraphicsMesh::Node(*static_cast<const GraphicsMesh::Node*>(srcv));   // the erased container core's element callback (void* dst / src): elem_stride  // CAST-OK: elem_stride
}

// 0x18005b660 — Graphics::AnimationState's copy constructor.
//
// The three Array members are ZEROED and then copy-ASSIGNED, in that order.
// The zeroing is not tidiness: `operator=` opens by destroying the
// destination, so handing it the garbage that is in raw MemAlloc memory would
// free a garbage pointer. (Same reasoning array.cpp records for the
// GraphSeries element assign.)
//
// B.4: the same stores, by name. Each Array header is zeroed as the two qword
// stores retail makes (`_data`, then `_size` + `_capacity` — written here as
// the two named dwords, H7), then assigned. The `void*` pair is the element-op
// signature of the collection block's spec row (floor: elem_stride).
extern "C" void* RtGraphics_AnimState_CopyCtor(void* dstv, const void* srcv) {
    Graphics::InstancedAnimationState* d = static_cast<Graphics::InstancedAnimationState*>(dstv);   // the erased container core's element callback (void* dst / src): elem_stride  // CAST-OK: elem_stride
    const Graphics::InstancedAnimationState* s = static_cast<const Graphics::InstancedAnimationState*>(srcv);   // the erased container core's element callback (void* dst / src): elem_stride  // CAST-OK: elem_stride

    // AnimationState's copy ctor 0x1800168d0, inlined by retail.
    d->_state._instanced = s->_state._instanced;

    d->_state._layers._data     = nullptr;
    d->_state._layers._size     = 0;
    d->_state._layers._capacity = 0;
    rtcoll::Rt_ArrayAssign_18001b700(&d->_state._layers, &s->_state._layers);    // CALL 0x18001b700

    d->_rootOffset.x = s->_rootOffset.x;                           // three dwords
    d->_rootOffset.y = s->_rootOffset.y;
    d->_rootOffset.z = s->_rootOffset.z;

    d->_pose._data     = nullptr;
    d->_pose._size     = 0;
    d->_pose._capacity = 0;
    TransformArrayAssign(&d->_pose, &s->_pose);                    // CALL 0x18001b360

    d->_nodes._data     = nullptr;
    d->_nodes._size     = 0;
    d->_nodes._capacity = 0;
    rtcoll::Rt_ArrayAssign_18001b810(&d->_nodes, &s->_nodes);      // CALL 0x18001b810
    return d;
}

// 0x18005b700 — AnimationManager::Instance's copy constructor.
//
// The ExternalPtr at +0x00 is copied as a RAW QWORD with no reference taken.
// That is retail's, and it is consistent with the rest of the class: the
// instance table is a cache keyed by the pointer, and the reference that keeps
// the group alive is the LAYER's, not the cache entry's.
extern "C" void* RtGraphics_AnimInstance_CopyCtor(void* dstv, const void* srcv) {
    AnimationManager::Instance* d = static_cast<AnimationManager::Instance*>(dstv);   // element op of a collection spec row takes void* (the type-erased collection core: one spec-row signature serves every element type)  // CAST-OK: elem_stride
    const AnimationManager::Instance* s =
        static_cast<const AnimationManager::Instance*>(srcv);   // element op of a collection spec row takes void* (the type-erased collection core: one spec-row signature serves every element type)  // CAST-OK: elem_stride

    d->_group     = s->_group;
    d->_animIndex = s->_animIndex;
    d->_refCount  = s->_refCount;

    d->_nodes._data     = nullptr;
    d->_nodes._size     = 0;
    d->_nodes._capacity = 0;
    rtcoll::Rt_ArrayAssign_180013960(&d->_nodes, &s->_nodes);      // CALL 0x180013960

    d->_states._data     = nullptr;
    d->_states._size     = 0;
    d->_states._capacity = 0;
    rtcoll::Rt_ArrayAssign_18005b130(&d->_states, &s->_states);    // CALL 0x18005b130
    return d;
}

// ============================================================================
// The six serializers
// ============================================================================
//
// Five take retail's `(Serializer*, int count, T* base)` element-loop shape;
// the sixth (0x180059820) takes `(Array*, Serializer*)` and does the count and
// the resize itself. The asymmetry is retail's and is reproduced.
//
// Every one of them reads the DESTINATION sizes back out of the array header
// AFTER the resize, never from the local it just deserialized — which is why
// `count` is passed on rather than re-read.
//
// B.4: each takes the element type retail's instantiation names, and reaches
// the ReaderWriter's SerializeInt (+0xA0), SerializeFloat (+0x80),
// SerializeBool (+0x78) and SerializeFloatArray (+0x28) slots through the real
// `IO::Serializer`'s `_readerWriter`, as retail's inlined copies do.

// 0x180059820 — Array<Math::Transform>::Serialize. Element 0x1c: a quaternion
// (four floats at +0x00) and a Vector3 (three floats at +0x10).
void RtGraphics_SerializeTransformArray(Collection::Array<Math::Transform>* a,
                                        IO::Serializer& ser) {
    IO::ReaderWriter* rw = ser._readerWriter;

    int32_t n = a->_size;
    rw->SerializeInt(n, nullptr);
    if (IsReading(ser)) rtcoll::RtColl_ArraySetSize_180059140(a, n);        // CALL 0x180059140

    Math::Transform* p = a->_data;
    for (int32_t i = 0; i < n; ++i, ++p) {
        rw->SerializeFloatArray(4, &p->rotation.x, nullptr);
        rw->SerializeFloatArray(3, &p->translation.x, nullptr);
    }
}

// 0x18005c230 — Array<InterpolatedState<Transform>>::Serialize's element loop.
// Two 0x1c Math::Transforms back to back.
static void RtGraphics_SerializeInterpTransforms(IO::Serializer& ser, int count,
                                                 InterpolatedState<Math::Transform>* p) {
    if (count <= 0) return;
    IO::ReaderWriter* rw = ser._readerWriter;
    for (int i = 0; i < count; ++i, ++p) {
        rw->SerializeFloatArray(4, &p->_current.rotation.x, nullptr);
        rw->SerializeFloatArray(3, &p->_current.translation.x, nullptr);
        rw->SerializeFloatArray(4, &p->_previous.rotation.x, nullptr);
        rw->SerializeFloatArray(3, &p->_previous.translation.x, nullptr);
    }
}

// 0x18005c120 — Array<GraphicsMesh::Node>::Serialize's element loop.
// {String name, int parent, Transform local, Transform bind}, stride 0x48.
// The last three floats are at +0x38 (read out of `MOV R8,RBX` at 0x18005c1ad,
// where RBX is elem+0x38 — Ghidra's decompiler drops that operand).
void RtGraphics_SerializeMeshNodes(IO::Serializer& ser, int count, GraphicsMesh::Node* p) {
    if (count <= 0) return;
    IO::ReaderWriter* rw = ser._readerWriter;
    for (int i = 0; i < count; ++i, ++p) {
        p->_name.Serialize(ser);
        rw->SerializeInt(p->_parent, nullptr);
        rw->SerializeFloatArray(4, &p->_local.rotation.x, nullptr);
        rw->SerializeFloatArray(3, &p->_local.translation.x, nullptr);
        rw->SerializeFloatArray(4, &p->_bind.rotation.x, nullptr);
        rw->SerializeFloatArray(3, &p->_bind.translation.x, nullptr);
    }
}

// 0x18005cb40 — Array<AnimationState::Layer>::Serialize's element loop.
//
// NOTE THE ORDER: the int at +0x0c (the packed instance handle) is written
// SIXTH, after the three Times, not second next to the clip index. Reordering
// it to match the declaration would change the byte layout of every save.
//
// The three int64 Times go through `IO::Serializer::Serialize(int64_t&)`
// (0x180035000, the ReaderWriter's +0x90 slot), as block H1 spelled them.
static void RtGraphics_SerializeAnimLayers(IO::Serializer& ser, int count,
                                           AnimationState::Layer* p) {
    if (count <= 0) return;
    IO::ReaderWriter* rw = ser._readerWriter;
    for (int i = 0; i < count; ++i, ++p) {
        SerializeGroupPtr(p->_group, ser);                 // ExternalPtr<AnimationGroup>
        rw->SerializeInt(p->_clipIndex, nullptr);          // clip index
        rw->SerializeFloat(p->_speed, nullptr);            // speed / weight
        ser.Serialize(p->_time._ticks, nullptr);                // time
        ser.Serialize(p->_fadeLength._ticks, nullptr);          // fade length
        ser.Serialize(p->_fadeRemaining._ticks, nullptr);       // fade remaining
        rw->SerializeInt(p->_handle, nullptr);             // packed instance handle
        rw->SerializeBool(p->_restarted, nullptr);         // restarted
    }
}

// 0x18005c2c0 — Array<AnimationState>::Serialize's element loop.
//
// Visits the layer array, the bool, the node array, the root offset and the
// pose array, in that order. The bool at +0x00 lands BETWEEN the two arrays,
// which is why it is not hoisted to the top.
static void RtGraphics_SerializeAnimStates(IO::Serializer& ser, int count,
                                           Graphics::InstancedAnimationState* p) {
    if (count <= 0) return;
    IO::ReaderWriter* rw = ser._readerWriter;
    for (int i = 0; i < count; ++i, ++p) {
        // AnimationState::Serialize (0x1800fe250), inlined by retail.
        int32_t n = p->_state._layers._size;
        rw->SerializeInt(n, nullptr);
        if (IsReading(ser)) rtcoll::Rt_SetSize_18005c8a0(&p->_state._layers, n);            // CALL 0x18005c8a0
        RtGraphics_SerializeAnimLayers(ser, n, p->_state._layers._data);

        rw->SerializeBool(p->_state._instanced, nullptr);

        n = p->_nodes._size;
        rw->SerializeInt(n, nullptr);
        if (IsReading(ser)) rtcoll::Rt_SetSize_18005ac50(&p->_nodes, n);            // CALL 0x18005ac50
        RtGraphics_SerializeInterpTransforms(ser, n, p->_nodes._data);

        rw->SerializeFloatArray(3, &p->_rootOffset.x, nullptr);   // _rootOffset

        RtGraphics_SerializeTransformArray(&p->_pose, ser);     // CALL 0x180059820
    }
}

// 0x18005c660 — Array<AnimationManager::Instance>::Serialize's element loop.
// The STATES array is visited before the NODES array; see AnimationManager.h.
void RtGraphics_SerializeAnimInstances(IO::Serializer& ser, int count,
                                       AnimationManager::Instance* p) {
    if (count <= 0) return;
    IO::ReaderWriter* rw = ser._readerWriter;
    for (int i = 0; i < count; ++i, ++p) {
        SerializeGroupPtr(p->_group, ser);
        rw->SerializeInt(p->_animIndex, nullptr);          // clip index
        rw->SerializeInt(p->_refCount, nullptr);           // reference count

        int32_t n = p->_states._size;
        rw->SerializeInt(n, nullptr);
        if (IsReading(ser)) rtcoll::Rt_SetSize_18005b1e0(&p->_states, n);            // CALL 0x18005b1e0
        RtGraphics_SerializeAnimStates(ser, n, p->_states._data);

        n = p->_nodes._size;
        rw->SerializeInt(n, nullptr);
        if (IsReading(ser)) rtcoll::Rt_SetSize_18005aa30(&p->_nodes, n);             // CALL 0x18005aa30
        RtGraphics_SerializeMeshNodes(ser, n, p->_nodes._data);
    }
}

// ============================================================================
// ?Serialize@AnimationState@Graphics@@QEAAXAEAVSerializer@IO@@@Z  (0x1800fe250)
// ============================================================================

// ============================================================================
// ?ResetAnimationState@AnimationState@Graphics@@QEAAXXZ  (0x1800fd930)
// ============================================================================

// ============================================================================
// ?GetPose@AnimationManager@Graphics@@QEBAXHPEAVTransform@Math@@H@Z (0x1800fb8e0)
// ============================================================================

// ============================================================================
// ?GetPoseAtTime@Animation@Graphics@@QEBAXAEBVTime@System@@0PEAVTransform@Math@@H@Z
// (0x1800faca0)
// ============================================================================

// ############################################################################
// ENDGAME B.3.5 step 5, block H1 — the member bodies.
//
// Their own `namespace Graphics { }` block, after this file's anonymous
// namespace has closed, because a member definition has to appear in a namespace
// ENCLOSING the class and `Graphics` / `RtGraphics` are siblings (B34_BRIEFS
// section A.2). The anonymous namespace's helpers (`RwInt`, `IsReading`,
// `SetSize*`, `RtAddr`, `AnimationManagerSingleton`, `SecondsPerTick`,
// `QuaternionLerp`) are reachable unqualified: they are members of the global
// namespace's unnamed namespace, in this same translation unit.
//
// FOUR BODIES MOVED HERE, and the doors above keep their mangled names, their
// retail addresses and their registrations. None of these three classes is
// polymorphic, so the `Class::` on each delegation is uniformity rather than
// recursion-avoidance (B34_BRIEFS A.1) — it is spelled anyway.
//
// GREP HAZARD: a bare `AnimationState` / `AnimationManager` / `Animation` in
// here binds the CLASS. The PODs this file also uses — `AnimStateRec`,
// `AnimInstanceRec`, `AnimationManagerView` — are spelled `RtGraphics::` inside
// this block.
// ############################################################################

namespace Graphics {

// ============================================================================
// ?Serialize@AnimationState@Graphics@@QEAAXAEAVSerializer@IO@@@Z  (0x1800fe250)
//
// *** IT SERIALIZES ONLY THE LAYERS AND THE BOOL. *** `_pose`, `_nodes` and
// `_rootOffset` are NOT written here. That is retail's and it is consistent:
// those three are derived state that `Update` recomputes on the next tick, and
// this entry point is the one a `MeshInstance`'s own Serialize reaches. The
// ARRAY form (`RtGraphics_SerializeAnimStates`, 0x18005c2c0) is the
// AnimationManager's instance CACHE, which has to come back whole because
// nothing rebuilds it.
//
// The last call is a TAIL JUMP through the ReaderWriter vtable in retail
// (Ghidra prints "Could not recover jumptable" at 0x1800fe2c3); a call is the
// same thing without the tail merge.
//
// The int count and the bool go through the ReaderWriter's SerializeInt (+0xA0)
// and SerializeBool (+0x78), by name since B3X W0.
// ============================================================================
void AnimationState::Serialize(IO::Serializer& s) {
    IO::ReaderWriter* rw = s._readerWriter;

    int32_t n = _layers._size;
    rw->SerializeInt(n, nullptr);
    if (IsReading(s)) rtcoll::Rt_SetSize_18005c8a0(&_layers, n);
    RtGraphics_SerializeAnimLayers(s, n, _layers._data);
    rw->SerializeBool(_instanced, nullptr);

    // NOT RETAIL (UB, review F-098): retail reads each layer's packed handle
    // `(instance << 8) | state` off the save and never compares it with the
    // AnimationManager's tables, which the save restored before any entity. A
    // handle past either table made ResetAnimationState write `--_refCount`
    // outside `_instances` (heap overflow) and every pose read index past it.
    // Such a layer is read as one with no shared instance (handle -1, which every
    // reader already takes as "sample the clip"). A valid save is unchanged.
    if (IsReading(s)) {
        const AnimationManager* const mgr = AnimationManager::_singleton;
        for (int32_t i = 0; i < _layers._size; ++i) {
            Layer& layer = _layers._data[i];
            const int32_t handle = layer._handle;
            if (handle < 0)
                continue;
            const int32_t instance = AnimationManager::HandleInstance(handle);
            const int32_t state = AnimationManager::HandleState(handle);
            if (mgr == nullptr || instance >= mgr->_instances._size ||
                state >= mgr->_instances._data[instance]._states._size)
                layer._handle = -1;
        }
    }
}

// ============================================================================
// ?ResetAnimationState@AnimationState@Graphics@@QEAAXXZ  (0x1800fd930)
//
// Drop every layer's reference on the shared instance it was using, then free
// the layer array outright. The free is the J1 `~Array` body INLINED — retail
// does the Enter/Free/Leave itself here rather than calling 0x18001c480 — so it
// is written out rather than routed through the Collection port, which is what
// makes it byte-faithful and not merely equivalent.
//
// A REFERENCE THAT REACHES ZERO DOES NOT FREE THE INSTANCE RECORD. It blanks it
// in place (group = null, clip index = -1, both arrays resized to 0) and leaves
// it in the table, which is what lets `GetInstanceId` reuse the slot: its first
// pass looks for `group == 0` and remembers the first such index.
//
// Every deref is UNGUARDED, exactly as retail's is.
// ============================================================================
void AnimationState::ResetAnimationState() {
    for (int32_t i = 0; i < _layers._size; ++i) {
        const int32_t handle = _layers._data[i]._handle;
        if (handle < 0) continue;                        // TEST ECX,ECX / JS

        AnimationManager::Instance* inst =
            &AnimationManager::_singleton->_instances._data[AnimationManager::HandleInstance(handle)];
        if (--inst->_refCount != 0) continue;

        inst->_group     = nullptr;
        inst->_animIndex = -1;
        rtcoll::Rt_SetSize_18005aa30(&inst->_nodes, 0);                // CALL 0x18005aa30
        rtcoll::Rt_SetSize_18005b1e0(&inst->_states, 0);               // CALL 0x18005b1e0
    }

    // The inlined ~Array. `MemFree` is null-guarded inside 0x1800c7ad0, which is
    // instruction for instruction what retail inlines here.
    if (_layers._data != nullptr)
        System::MemoryAllocator::MemFree(_layers._data);
    _layers._data     = nullptr;
    _layers._size     = 0;
    _layers._capacity = 0;
}

// ============================================================================
// ?GetPose@AnimationManager@Graphics@@QEBAXHPEAVTransform@Math@@H@Z (0x1800fb8e0)
//
// Copy `count` baked `Math::Transform`s out of the cached instance the packed
// handle names. `(handle >> 8) & 0xff` is the instance index and `handle & 0xff`
// the frame — the 256/256 ceiling is retail's, not ours.
//
// UNGUARDED ON EVERY STEP: a bad handle indexes off the end of the instance
// table. That is retail's, so it stays that way (CLAUDE.md: retail's unguarded
// derefs are load-bearing).
//
// The copy is a `Math::Transform` assignment: the class is trivially copyable
// (retail's own copy is seven dword moves, 0x180014fd0), so the assignment is a
// bit copy — a float-register round trip is never involved.
// ============================================================================
void AnimationManager::GetPose(int handle, Math::Transform* out, int count) const {
    const Instance& inst = _instances._data[HandleInstance(handle)];
    const Math::Transform* pose = inst._states._data[HandleState(handle)]._pose._data;

    for (int i = 0; i < count; ++i)
        out[i] = pose[i];
}

// ?GetPoseAtTime@Animation@Graphics@@QEBAXAEBVTime@System@@0PEAVTransform@Math@@H@Z
// retail 0x1800faca0. The banner above the door carries the porting argument;
// every field is a named member of `Graphics::Animation`, whose layout is pinned
// in src/engine/graphics/Animation.h against the same instructions.
void Animation::GetPoseAtTime(const System::Time& timeRef, const System::Time& deltaRef,
                              Math::Transform* out, int /*unused*/) const {
    const int64_t* const time  = &timeRef._ticks;
    const int64_t* const delta = &deltaRef._ticks;

    const int32_t loopFrame  = _loopFrame;
    const int32_t frameFirst = _startFrame;
    const int32_t frameLast  = _endFrame;
    const int32_t frameCount = _frameCount;
    const int32_t boneCount  = _boneCount;
    const uint8_t  looping   = _looping ? 1u : 0u;
    const float    rate      = _frameRate;
    const Math::Transform* keys = _keys._data;

    // Where a loop restarts, as a frame index. Zero when the clip has no loop
    // frame at all — and note that THIS is the short-circuit, not a test on the
    // looping flag: a looping clip with loopFrame == 0 restarts at frame 0.
    //
    // NOT RETAIL (UB, review F-121): retail converts the loop fraction with no
    // range test and divides by `frameCount - loopStart` with no zero test. A
    // looping clip whose loop frame is its end frame has loopStart == frameCount
    // (IDIV by zero, the process dies); start == end makes the fraction NaN or
    // infinite (an out-of-range float->int conversion); a loop frame before the
    // start or past the end gives key indexes outside `_keys`. A loop start
    // outside [0, frameCount) now holds the last frame, as a non-looping clip
    // does. Every valid clip keeps retail's arithmetic exactly. A clip with no
    // frames never wraps (its span is zero) and never reads a key, below.
    bool wraps = looping != 0 && frameCount > 0;
    int32_t loopStart = 0;
    if (loopFrame != 0) {
        const float at = (static_cast<float>(loopFrame - frameFirst) /
                          static_cast<float>(frameLast - frameFirst)) * static_cast<float>(frameCount);
        if (at > -1.0f && at < static_cast<float>(frameCount))
            loopStart = static_cast<int32_t>(at);
        else
            wraps = false;
    }

    float cursor = static_cast<float>(static_cast<double>(*time) * SecondsPerTick()) * rate;
    // NOT RETAIL (UB, review F-121): a negative time from a save gave a negative
    // key index, and a time past INT32_MAX frames (or a NaN rate) an out-of-range
    // conversion. Both now sample frame 0.
    if (!(cursor > -1.0f && cursor < 2147483648.0f))
        cursor = 0.0f;
    const int32_t whole = static_cast<int32_t>(cursor);

    int32_t f0 = whole;
    if (f0 >= frameCount)
        f0 = wraps ? (loopStart + whole % (frameCount - loopStart)) : (frameCount - 1);
    int32_t f1 = f0 + 1;
    if (f1 >= frameCount)
        f1 = wraps ? (f1 % (frameCount - loopStart) + loopStart) : (frameCount - 1);

    const float frac = cursor - static_cast<float>(whole);

    if (frameCount <= 0) {
        // NOT RETAIL (UB, review F-121): a clip with no frames has no key to read
        // (retail reads `keys - boneCount`); the pose is left as the caller had it.
    } else if (f0 == f1 || frac == 0.0f) {
        const Math::Transform* src = keys + boneCount * f0;
        for (int32_t i = 0; i < boneCount; ++i)
            out[i] = src[i];
    } else {
        const Math::Transform* a = keys + boneCount * f0;
        const Math::Transform* b = keys + boneCount * f1;
        for (int32_t i = 0; i < boneCount; ++i) {
            const float ax = a[i].translation.x;
            const float ay = a[i].translation.y;
            const float az = a[i].translation.z;
            const float bx = b[i].translation.x;
            const float by = b[i].translation.y;
            const float bz = b[i].translation.z;

            // Math::Quaternion::Lerp (0x18001d500) by value.
            out[i].rotation = a[i].rotation.Lerp(b[i].rotation, Math::Scalar(frac));

            out[i].translation.x = ax + (bx - ax) * frac;
            out[i].translation.y = ay + (by - ay) * frac;
            out[i].translation.z = az + (bz - az) * frac;
        }
    }

    const float scale = static_cast<float>(static_cast<double>(*delta) * SecondsPerTick()) * rate;
    out->rotation.x = 0.0f;
    out->rotation.y = 0.0f;
    out->rotation.z = 0.0f;
    out->rotation.w = 1.0f;                                    // DAT_18016169c
    out->translation.x *= scale;
    out->translation.y *= scale;
    out->translation.z *= scale;
}

// ---------------------------------------------------------------------------
// ENDGAME B.3.5 step 5, block G - `Graphics::GraphicsMesh::Node`'s copy
// constructor, the ONE `GraphicsMesh` fragment in this file (B35_FILEMAP.md:97).
//   ??0Node@GraphicsMesh@Graphics@@QEAA@AEBU012@@Z      retail 0x1800127f0
//
// The name slot is stored ZERO FIRST and only then given the source's pointer:
// that is what makes this safe to run into raw `MemAlloc` memory, and it is why
// retail's `Array<Node>::operator=` calls it directly on a buffer it has just
// allocated. A member-initialiser list would do the same thing, but the two
// stores are written out in retail's order so the sequence stays visible.
//
// THE THREE ASSIGNMENTS ARE RETAIL'S FIFTEEN-DWORD FLAT COPY, exactly:
// `_parent` (4) + `_local` (0x1c) + `_bind` (0x1c) = 0x3c bytes spanning
// +0x08..+0x43, which is `for (off = 0x08; off <= 0x40; off += 4)`. `_pad44` is
// NOT assigned, which is retail's own behaviour and not an omission.
// ---------------------------------------------------------------------------
// B.4 Y RT: `_name` is the real String; its copy ctor 0x180006cb0 is the same
// zero-then-AddRef pair.
GraphicsMesh::Node::Node(const Node& src) : _name(src._name) {
    _parent = src._parent;
    _local  = src._local;
    _bind   = src._bind;
}

}  // namespace Graphics

