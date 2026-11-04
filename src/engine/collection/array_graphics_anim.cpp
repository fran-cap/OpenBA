// ============================================================================
// src/engine/collection/array_graphics_anim.cpp
// The seventeen Collection::Array<T> instantiations underneath the Graphics
// animation / skeleton cluster (cutover closure batch 3, 2026-09-12).
// ============================================================================
//
// WHY A NEW FILE AND NOT MORE ROWS IN array.cpp / array_destroy.cpp
// -----------------------------------------------------------------
// It is the same two algorithms those files already own, and this file calls
// THEIR bodies — `rtcoll::SetSizeCore`, `rtcoll::ArrayDtorImpl`,
// `rtcoll::FreeInnerArraysCore`, `rtcoll::ReleaseStringsCore` — through
// array_internal.h / array_destroy_internal.h rather than restating any of
// them. Nothing here is a second copy of a Collection body; what is new is
// seventeen (address, spec) pairs and one job the other two files do not have
// an entry shape for, `Array<T>::operator=` for a NON-pointer element type.
//
// array_destroy.cpp's banner asks, in as many words, that an address be
// grepped across both files before a Collection helper is ported here. All
// seventeen were; none is registered in either. Three of them are ALREADY
// SPEC'D there and unregistered ("the eight ported but deliberately
// unregistered ... register it the day a caller appears"), and this batch is
// that day for two of them:
//     0x18005bce0  kD_InnerArr3_48   the AnimationState element destroy
//     0x180014a40  kD_Str_48         the GraphicsMesh::Node element destroy
// so they are reached here through the SPEC ROWS those files export, not
// through a re-derivation.
//
// ---------------------------------------------------------------------------
// THE FOUR RECORD TYPES, AND WHERE EACH LAYOUT COMES FROM
// ---------------------------------------------------------------------------
// src/engine/graphics/AnimationManager.h, pinned by the six serializers in
// src/engine/graphics/animationstate.cpp. In one line each:
//
//   AnimationManager::Instance   0x30  {ExternalPtr, int, int, Array, Array}
//   AnimationState               0x48  {bool, Array, Vector3, Array, Array}
//   AnimationState::Layer        0x38  {ExternalPtr, int, int, float, 3x Time, bool}
//   GraphicsMesh::Node           0x48  {char*, int, Transform, Transform, int}
//   InterpolatedState<Transform> 0x38  two 0x1c Math::Transform
//   Math::Transform              0x1c  (already array.cpp's kSpec_Transform)
//
// ---------------------------------------------------------------------------
// THE ONE THING IN HERE THAT LOOKS LIKE A TRANSCRIPTION ERROR AND IS NOT
// ---------------------------------------------------------------------------
// `kInitMeshNode` does NOT write +0x44, and `kInitAnimLayer` does not write
// +0x10 (the playback speed) or +0x31..+0x37. Those are retail's holes: the
// `else` arm of each fill loop stores exactly the qwords listed beside it below
// and nothing more. Helpfully zeroing them would be unfaithful AND would make an
// oracle that compared them fail 100% of calls
// (reference_oracle_must_not_compare_unwritten_bytes.md). Every caller in this
// cluster overwrites the speed before reading it — AnimationState::
// PlayAnimation writes it on the line after it grows the array — which is
// presumably why MSVC was allowed to elide the stores. Retail's fill loop also
// skips the node's parent index at +0x08; the port zeroes that one, because the
// node write path serializes it (review F-049, the NOT RETAIL note at
// MeshNodeInit below).
//
// ALLOCATOR BOUNDARY: none of its own. `MemAlloc` / `MemFree` are array.cpp's
// two accessors, and the lane-E dependency its banner states applies here
// unchanged.
// ============================================================================

#include <cstdint>
#include <cstring>

#include "collection/Collection.h"
#include "array_internal.h"
#include "array_destroy_internal.h"
#include "graphics/AnimationState.h"   // B3R RT-VG: the Rt_* declarations
#include "graphics/AnimationManager.h" // B.4: AnimationManager::Instance
#include "graphics/GraphicsMesh.h"     // B.4: GraphicsMesh::Node

// The three element copy constructors live with the class they belong to, in
// src/engine/graphics/animationstate.cpp. They are reached by name and not by
// address so that this file has no dependency on registration ORDER.
extern "C" void* RtGraphics_MeshNode_CopyCtor(void* dst, const void* src);
extern "C" void* RtGraphics_AnimState_CopyCtor(void* dst, const void* src);
extern "C" void* RtGraphics_AnimInstance_CopyCtor(void* dst, const void* src);

namespace rtcoll {
namespace gfxanim {

// ---------------------------------------------------------------------------
// Element assignment
// ---------------------------------------------------------------------------
// The two byte-copy forms are written as the exact qword / dword sequences
// retail emits rather than as a memcpy, because the COUNT is the evidence for
// the stride and a memcpy would erase it from the source.

// 0x18001b700's inner loop and 0x18005c8a0's grow arm: seven qwords.
void Assign38Q(uint8_t* d, const uint8_t* s) { AssignAs<uint64_t, &CopyWords<uint64_t, 7>>(d, s); }

// 0x18001b810's inner loop and 0x18005ac50's grow arm: fourteen dwords. The
// SAME 0x38 stride as Assign38Q above and a DIFFERENT instantiation — the
// element is two 0x1c Math::Transforms, whose alignment is 4, and retail
// asks the allocator for 4 here and for 8 there. Merging the two by stride
// would change the alignment argument of every allocation this array makes.
void Assign38D(uint8_t* d, const uint8_t* s) { AssignAs<uint32_t, &CopyWords<uint32_t, 14>>(d, s); }

void AssignMeshNode(uint8_t* d, const uint8_t* s)     { RtGraphics_MeshNode_CopyCtor(d, s); }
void AssignAnimState(uint8_t* d, const uint8_t* s)    { RtGraphics_AnimState_CopyCtor(d, s); }
void AssignAnimInstance(uint8_t* d, const uint8_t* s) { RtGraphics_AnimInstance_CopyCtor(d, s); }

// ---------------------------------------------------------------------------
// New-tail initialisation — one function per grow arm, each transcribing the
// stores of the `else` limb of that address's fill loop, in retail's order.
// ---------------------------------------------------------------------------

// 0x18005aa30. Two identity Math::Transforms; +0x08 and +0x44 NOT written.
// The identity Math::Transform as retail stores it: a qword, a dword, then the
// qword 0x3f800000 that is rotation.w = 1.0f AND translation.x = 0 (H7), then
// a qword over translation.y / .z.
static void IdentityTransform(Math::Transform& t) {
    t.rotation.x    = 0.0f;
    t.rotation.y    = 0.0f;
    t.rotation.z    = 0.0f;
    t.rotation.w    = 1.0f;
    t.translation.x = 0.0f;
    t.translation.y = 0.0f;
    t.translation.z = 0.0f;
}
static void MeshNodeInit(Graphics::GraphicsMesh::Node* n) {
    n->_name._data = nullptr;         // +0x00
    // NOT RETAIL (review F-049, undefined behaviour): retail does not write +0x08 (the
    // parent index), and Node::Serialize's write path (RtGraphics_SerializeMeshNodes) and the
    // copy ctor then read an indeterminate int, which C++ leaves undefined; the bytes written
    // were heap garbage. Zeroed here. Every reader sets it first (the read path serializes
    // it in), so a loaded mesh is unchanged.
    n->_parent = 0;                   // +0x08
    IdentityTransform(n->_local);     // +0x0c .. +0x27
    IdentityTransform(n->_bind);      // +0x28 .. +0x43
    // +0x44 NOT written
}
void InitMeshNode(uint8_t* p) { InitAs<Graphics::GraphicsMesh::Node, &MeshNodeInit>(p); }

// 0x18005b1e0. A zeroed InstancedAnimationState: the AnimationState's bool and
// Array header, then the Vector3 and the two other Array headers, all zero.
// +0x01..+0x07 not written.
static void AnimStateInit(Graphics::InstancedAnimationState* a) {
    a->_state._instanced        = false;  // the bool; +0x01..+0x07 not written
    a->_state._layers._data     = nullptr;
    a->_state._layers._size     = 0;      // one qword (H7)
    a->_state._layers._capacity = 0;
    a->_rootOffset.x        = 0.0f;   // one qword (H7)
    a->_rootOffset.y        = 0.0f;
    a->_rootOffset.z        = 0.0f;   // dword
    a->_pose._data          = nullptr;
    a->_pose._size          = 0;
    a->_pose._capacity      = 0;
    a->_nodes._data         = nullptr;
    a->_nodes._size         = 0;
    a->_nodes._capacity     = 0;
}
void InitAnimState(uint8_t* p) { InitAs<Graphics::InstancedAnimationState, &AnimStateInit>(p); }

// 0x18005c8a0. The -1 is ONE qword covering BOTH ints at +0x08 and +0x0c, i.e.
// clipIndex = -1 and handle = -1. +0x10 (speed) and +0x31..+0x37 not written.
static void AnimLayerInit(Graphics::AnimationState::Layer* l) {
    l->_group                 = nullptr;
    l->_clipIndex             = -1;   // ONE qword of -1 over both ints (H7)
    l->_handle                = -1;
    // +0x10 (speed) not written
    l->_time._ticks           = 0;
    l->_fadeLength._ticks     = 0;
    l->_fadeRemaining._ticks  = 0;
    l->_restarted             = false;
}
void InitAnimLayer(uint8_t* p) { InitAs<Graphics::AnimationState::Layer, &AnimLayerInit>(p); }

// 0x18005c3d0 / 0x18005be30's no-fill arm. Covers the whole 0x30.
static void AnimInstanceInit(Graphics::AnimationManager::Instance* i) {
    i->_group            = nullptr;
    i->_animIndex        = -1;
    i->_refCount         = 0;
    i->_nodes._data      = nullptr;
    i->_nodes._size      = 0;         // one qword (H7)
    i->_nodes._capacity  = 0;
    i->_states._data     = nullptr;
    i->_states._size     = 0;         // one qword (H7)
    i->_states._capacity = 0;
}
void InitAnimInstance(uint8_t* p) { InitAs<Graphics::AnimationManager::Instance, &AnimInstanceInit>(p); }

// 0x18005ac50. Two identity Math::Transforms back to back; covers the whole
// 0x38.
using InterpTransform = Graphics::InterpolatedState<Math::Transform>;
static void InterpTransformInit(InterpTransform* s) {
    IdentityTransform(s->_current);   // +0x00 .. +0x1b
    IdentityTransform(s->_previous);  // +0x1c .. +0x37
}
void InitInterpTransform(uint8_t* p) { InitAs<InterpTransform, &InterpTransformInit>(p); }

// ---------------------------------------------------------------------------
// Element destruction
// ---------------------------------------------------------------------------
// Two of the three are array_destroy.cpp's own rows, used through its Core
// bodies. The third, 0x18005c550, is the "eleventh helper" its banner names as
// "a RECURSIVE destroy that calls one of each body before freeing" — the only
// one of the eleven that has never had a caller until this batch.

void DestroyMeshNodeRange(void*, uint8_t* base, int count) {
    ReleaseStringsCore(base, count, &kD_Str_48);        // 0x180014a40
}

void DestroyAnimStateRange(void*, uint8_t* base, int count) {
    FreeInnerArraysCore(base, count, &kD_InnerArr3_48); // 0x18005bce0
}

// 0x18005c550 — Array<AnimationManager::Instance>'s element destroy. Per
// element, in RETAIL'S ORDER (the states array BEFORE the nodes array, which
// is the same out-of-order visit the serializer makes):
//
//   FUN_18005bce0(elem->_states._data, elem->_states._size);   ; per-state
//   MemFree(elem->_states._data); zero its 16-byte header
//   FUN_180014a40(elem->_nodes._data,  elem->_nodes._size);    ; per-node
//   MemFree(elem->_nodes._data);  zero its 16-byte header
//
// The zeroing is FOUR dword stores over the header, unconditional, including
// when the buffer was already null — exactly Body A's shape one level up.
static void InstanceDestroy(Graphics::AnimationManager::Instance* base, int count) {
    if (count <= 0) return;
    const int64_t n = static_cast<int64_t>(count);
    for (int64_t i = 0; i < n; ++i) {
        Graphics::AnimationManager::Instance* e = base + i;

        auto& states = e->_states;
        FreeInnerArraysCore(reinterpret_cast<uint8_t*>(states._data), states._size, &kD_InnerArr3_48);  // CAST-OK: elem_stride
        if (states._data) MemFree(states._data);
        states._data = nullptr; states._size = 0; states._capacity = 0;

        Graphics::GraphicsMesh::NodeArray& nodes = e->_nodes;
        ReleaseStringsCore(reinterpret_cast<uint8_t*>(nodes._data), nodes._size, &kD_Str_48);  // CAST-OK: elem_stride
        if (nodes._data) MemFree(nodes._data);
        nodes._data = nullptr; nodes._size = 0; nodes._capacity = 0;
    }
}
void DestroyInstanceRange(void* ignoredThis, uint8_t* base, int count) {
    DestroyAs<Graphics::AnimationManager::Instance, &InstanceDestroy>(ignoredThis, base, count);
}

// ---------------------------------------------------------------------------
// The specs
// ---------------------------------------------------------------------------
// Field order is array_internal.h's:
//   name, stride, align, assign, init, destroy, byteCopy,
//   assignUnwritten{Lo,Hi}, initUnwritten{Lo,Hi}, seedZero{Lo,Hi}
//
// The `initUnwritten` window can hold ONE hole and two of these rows have two.
// Where that happens the LARGER hole is declared and the other is named in the
// comment; nothing reads these fields today (no probe case is added by this
// batch), and the honest record is the comment either way.

constexpr ArraySpec kSpec_MeshNode = {
    "Array<GraphicsMesh::Node>", 0x48, 8,
    &AssignMeshNode, &InitMeshNode, &DestroyMeshNodeRange, false,
    0, 0,          // the copy ctor writes +0x00..+0x44 (it skips +0x45..+0x47)
    0x44, 0x48,    // init skips +0x44..+0x47 (retail also skips the parent index: zeroed, F-049)
    0x00, 0x08 };  // the name slot is refcounted — a probe must seed it null

constexpr ArraySpec kSpec_AnimState = {
    "Array<Graphics::AnimationState>", 0x48, 8,
    &AssignAnimState, &InitAnimState, &DestroyAnimStateRange, false,
    0, 0,
    0x01, 0x08,
    0x08, 0x48 };  // three inner Array headers; every one is dereferenced

constexpr ArraySpec kSpec_AnimLayer = {
    "Array<Graphics::AnimationState::Layer>", 0x38, 8,
    &Assign38Q, &InitAnimLayer, nullptr, true,
    0, 0,
    0x10, 0x18,    // init skips the speed; it ALSO skips +0x31..+0x37
    0, 0 };

constexpr ArraySpec kSpec_AnimInstance = {
    "Array<Graphics::AnimationManager::Instance>", 0x30, 8,
    &AssignAnimInstance, &InitAnimInstance, &DestroyInstanceRange, false,
    0, 0,
    0, 0,
    0x10, 0x30 };  // two inner Array headers

constexpr ArraySpec kSpec_InterpTransform = {
    "Array<Graphics::InterpolatedState<Math::Transform>>", 0x38, 4,
    &Assign38D, &InitInterpTransform, nullptr, true,
    0, 0,
    0, 0,
    0, 0 };

// Compile-time checks on the transcription, in array_destroy.cpp's spirit: an
// init hole has to lie inside its own stride, or the window names bytes of the
// NEXT element.
static_assert(kSpec_MeshNode.initUnwrittenHi  <= kSpec_MeshNode.stride,  "MeshNode hole");
static_assert(kSpec_AnimState.initUnwrittenHi <= kSpec_AnimState.stride, "AnimState hole");
static_assert(kSpec_AnimLayer.initUnwrittenHi <= kSpec_AnimLayer.stride, "AnimLayer hole");
static_assert(kSpec_MeshNode.seedZeroHi       <= kSpec_MeshNode.stride,  "MeshNode seed");
static_assert(kSpec_AnimState.seedZeroHi      <= kSpec_AnimState.stride, "AnimState seed");
static_assert(kSpec_AnimInstance.seedZeroHi   <= kSpec_AnimInstance.stride, "Instance seed");

// J4 -- Collection::Array<T>::operator= for the four non-pointer rows here is
// array_destroy.cpp's ArrayAssignCore over the row (the typed `Assign` of each
// alias below). B.4 Z MIRROR2 GFX: the file-local copy of that body and its two
// J1 clear functions had no caller left and are deleted.

}  // namespace gfxanim
}  // namespace rtcoll

// ============================================================================
// The registered entry points. One free function per ADDRESS; the address is in
// the name so a crash log or a census line names it directly.
// ============================================================================
// B3R RT-VG: namespace rtcoll (external linkage), was an anonymous namespace --
// the graphics readers call these by name (types/graphics/AnimationState.h).
namespace rtcoll {

using namespace rtcoll::gfxanim;

// ---- J1 / J2: ~Array ------------------------------------------------------
// B.4 Y RT: typed. `NodeArray`'s ~Array IS 0x180014090: kSpec_MeshNode's
// destroy (ReleaseStringsCore over kD_Str_48, CALL 0x180014a40) when the size
// is positive, then the J1 free-and-zero.
void* Rt_ArrayDtor_180014090(Graphics::GraphicsMesh::NodeArray* a) { a->~Array(); return nullptr; }
// B.4 Z RT2: typed. `AnimationStateArray`'s ~Array IS 0x18005bbb0:
// kSpec_AnimState's destroy (FreeInnerArraysCore over kD_InnerArr3_48, CALL
// 0x18005bce0, which returns at once for a size <= 0) then the J1 free-and-zero.
void* Rt_ArrayDtor_18005bbb0(Graphics::AnimationStateArray* a) { a->~Array(); return nullptr; }

// ---- the three per-element destroy helpers --------------------------------
// Retail's arity, including the `this` every one of them spills to the shadow
// space and never reads (array_destroy.cpp's "ignoredThis" note applies).

// ---- J3: SetSize ----------------------------------------------------------
void Rt_SetSize_18005aa30(Graphics::GraphicsMesh::NodeArray* a, int n) {   // Array<GraphicsMesh::Node>
    a->SetSize(n);                                 // kSpec_MeshNode
}
void Rt_SetSize_18005b1e0(Graphics::AnimationStateArray* a, int n) {   // Array<AnimationState>
    a->SetSize(n);                                 // kSpec_AnimState
}
void Rt_SetSize_18005c8a0(Graphics::AnimationState::LayerArray* a, int n) {   // Array<AnimationState::Layer>
    a->SetSize(n);                                 // kSpec_AnimLayer
}
void Rt_SetSize_18005ac50(Graphics::InterpTransformArray* a, int n) {   // Array<InterpolatedState<Transform>>
    a->SetSize(n);                                 // kSpec_InterpTransform
}
void Rt_SetSize_18005c3d0(void* a, int n) {   // Array<Instance>, no fill
    rtcoll::AsArray<&kSpec_AnimInstance>(a)->SetSize(n);
}
// The one THREE-argument row in this batch. Its new elements are copy-assigned
// from `fill` instead of default-constructed, which is what distinguishes it
// from 0x18005c3d0 above; the rest of the body is the same instantiation.
void Rt_SetSize_18005be30(void* a, int n, const void* fill) {
    rtcoll::AsArray<&kSpec_AnimInstance>(a)->SetSizeFill(n, fill);
}

// ---- J4: operator= --------------------------------------------------------
// B.4 Y RT: typed. `NodeArray::Assign` is the same J4 body (array_destroy.cpp's
// ArrayAssignCore: self-test, clear = the ~Array above, grow, then
// kSpec_MeshNode's assign = the copy ctor 0x1800127f0 per element) with the
// row's stride 0x48 and alignment 8.
Graphics::GraphicsMesh::NodeArray* Rt_ArrayAssign_180013960(Graphics::GraphicsMesh::NodeArray* d,
                                                            const Graphics::GraphicsMesh::NodeArray* s) {
    d->Assign(*s);
    return d;
}
// B.4 Z RT2: typed. `AnimationStateArray::Assign` is the same J4 body
// (array_destroy.cpp's ArrayAssignCore: self-test, clear = the ~Array above,
// grow, then kSpec_AnimState's assign = the copy ctor 0x18005b660 per element)
// with the row's stride 0x48 and alignment 8.
Graphics::AnimationStateArray* Rt_ArrayAssign_18005b130(Graphics::AnimationStateArray* d,
                                                        const Graphics::AnimationStateArray* s) {
    d->Assign(*s);
    return d;
}
// B.4 Z MIRROR2 GFX: typed. `AnimationState::LayerArray::Assign` is the same J4
// body (array_destroy.cpp's ArrayAssignCore: self-test, clear = the row's
// ~Array = J1 -- what Clear_18001c480 was --, grow at the row's stride 0x38 and
// alignment 8, then the row's assign Assign38Q per element).
Graphics::AnimationState::LayerArray* Rt_ArrayAssign_18001b700(
        Graphics::AnimationState::LayerArray* d,
        const Graphics::AnimationState::LayerArray* s) {   // Array<AnimationState::Layer>
    d->Assign(*s);
    return d;
}
// B.4 Z MIRROR2 GFX: typed. `InterpTransformArray::Assign` is the same J4 body
// (array_destroy.cpp's ArrayAssignCore: self-test, J1 clear -- what
// Clear_18001c550 was --, grow at the row's stride 0x38 and alignment 4, then
// the row's Assign38D per element).
Graphics::InterpTransformArray* Rt_ArrayAssign_18001b810(
        Graphics::InterpTransformArray* d,
        const Graphics::InterpTransformArray* s) {   // Array<InterpolatedState<Transform>>
    d->Assign(*s);
    return d;
}

}  // namespace rtcoll

