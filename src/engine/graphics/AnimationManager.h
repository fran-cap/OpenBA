#pragma once
// ============================================================================
// src/engine/graphics/AnimationManager.h
// Graphics::AnimationManager and the two records its arrays hold, plus the
// three Array<T> instantiations underneath them.
// ============================================================================
//
// SOURCE OF EVERY OFFSET BELOW. Unlike AnimationState.h next door — which had
// to read one body's disassembly because Ghidra holds two overlapping function
// bodies around 0x1800fdb20 — this file's layout is pinned by a SERIALIZER,
// which is the strongest evidence this project gets for a record layout: a
// serializer names the TYPE of every field it touches (through the
// IO::ReaderWriter vtable slot it calls) and visits them in declaration order.
//
//   0x18005c660   Array<AnimationManager::Instance>::Serialize  stride 0x30
//   0x18005c2c0   Array<AnimationState>::Serialize              stride 0x48
//   0x18005cb40   Array<AnimationState::Layer>::Serialize       stride 0x38
//   0x18005c120   Array<GraphicsMesh::Node>::Serialize          stride 0x48
//   0x18005c230   Array<InterpolatedState<Transform>>::Serialize stride 0x38
//   0x180059820   Array<Math::Transform>::Serialize             stride 0x1c
//
// Cross-checked against four independent readers of the same records:
//   * ?GetPose@AnimationManager@Graphics@@ (0x1800fb8e0) walks
//     `manager->_instances._data + ((id >> 8) & 0xff) * 0x30`, loads the
//     instance's +0x20 field as a buffer and indexes it by
//     `(id & 0xff) * 0x48`, then reads THAT record's +0x28 as a buffer of
//     0x1c-byte Transforms. That fixes the Instance stride (0x30), the
//     AnimationState stride (0x48), Instance::_states at +0x20 and
//     AnimationState::_pose at +0x28 all at once.
//   * ?ResetAnimationState@AnimationState@Graphics@@ (0x1800fd930) and
//     ?PlayAnimation@AnimationState@Graphics@@ (0x1800fdf50) both decrement
//     `instance + 0x0c` and, when it reaches zero, store 0 at +0x00,
//     -1 at +0x08 and SetSize(instance+0x10, 0) / SetSize(instance+0x20, 0).
//   * the per-element destroy helper 0x18005bce0 is already a row in
//     src/engine/collection/array_destroy.cpp as `kD_InnerArr3_48` with
//     offsets {0x38, 0x28, 0x08} at stride 0x48 — three inner Arrays inside a
//     0x48 record, which is exactly the AnimationState declared below.
//   * the copy constructors 0x18005b700 (Instance) and 0x18005b660
//     (AnimationState) write the same fields in the same order.
//
// ---------------------------------------------------------------------------
// WHAT THIS FILE CORRECTS IN AnimationState.h
// ---------------------------------------------------------------------------
// AnimationState.h (Wave 27 lane GFXBIG) declares AnimationState +0x28 and
// +0x38 as two `void*` named `interpB` / `interpA`, "the two
// InterpolatedState<Math::Transform>* the caller passes as Update's 4th and
// 5th parameters". The POINTER VALUE it describes is right and the TYPE is
// wrong: both are 16-byte `Collection::Array` headers, so what the caller
// passes is the address of an Array, not an InterpolatedState. See the
// correction note left in AnimationState.h itself.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "../collection/Collection.h"
#include "../object/External.h"   // Object::ExternalPtr<T>, for GetInstanceId
#include "AnimationState.h"        // B.4: Array<AnimationState>, Array<GraphicsMesh::Node>

// (kSpec_AnimState, the row of `Array<Graphics::AnimationState>`, is declared
// in AnimationState.h with its `AnimationStateArray` alias.)
namespace rtcoll { namespace gfxanim {
// B.4 wave Y (RT-GFX): the row of `Array<AnimationManager::Instance>` (SetSize
// 0x18005c3d0 / 0x18005be30, copy ctor 0x18005b700, element destroy 0x18005c550
// that ~Array 0x18005bf60 runs), so `AnimationManager::_instances` names it.
extern const ArraySpec kSpec_AnimInstance;
} }

// Forward declarations only. Tags match the real declarations (RULE B4b-5).
namespace System { class Time; }
namespace Math { class Transform; class Vector3; }
namespace IO { class Serializer; }
namespace Graphics { class AnimationGroup; }

namespace RtGraphics {

// ---------------------------------------------------------------------------
// Graphics::AnimationState — 0x48 bytes, the full layout.
//
// AnimationState.h declares the same record from AnimationState::Update's side
// and names the fields Update reads; this declaration is the SERIALIZER's view
// and is the one the Array<AnimationState> machinery in
// src/engine/collection/array_graphics_anim.cpp uses. They agree on every
// offset; the two differences are that this one knows +0x28 / +0x38 are Array
// headers and that it does not claim to know what +0x24 / +0x30 / +0x40 are
// (they are the `_capacity` halves of the two Array triples).
//
// Serialized by 0x18005c2c0 in this order: the layer array (count as int, then
// the 0x38 records), +0x00 as a bool, the node array (count as int, then the
// 0x38 InterpolatedState records), the Vector3 at +0x18 as three floats, then
// the pose array through 0x180059820.
// ---------------------------------------------------------------------------
struct AnimStateRec {
    uint8_t  _needsUniqueCopy;                   // +0x00
    uint8_t  _pad01[7];                          // +0x01
    Collection::Array<uint8_t> _layers;          // +0x08  stride 0x38
    float    _rootOffset[3];                     // +0x18  Math::Vector3
    int32_t  _pad24;                             // +0x24
    Collection::Array<uint8_t> _pose;            // +0x28  Array<Math::Transform>, stride 0x1c
    Collection::Array<uint8_t> _nodes;           // +0x38  Array<InterpolatedState<Transform>>, 0x38
};
static_assert(sizeof(AnimStateRec) == 0x48, "Graphics::AnimationState is 0x48 bytes");
static_assert(offsetof(AnimStateRec, _needsUniqueCopy) == 0x0, "AnimStateRec::_needsUniqueCopy");
static_assert(offsetof(AnimStateRec, _pad01) == 0x1, "AnimStateRec::_pad01");
static_assert(offsetof(AnimStateRec, _pad24) == 0x24, "AnimStateRec::_pad24");
static_assert(offsetof(AnimStateRec, _layers)     == 0x08, "AnimationState::_layers @ +0x08");
static_assert(offsetof(AnimStateRec, _rootOffset) == 0x18, "AnimationState::_rootOffset @ +0x18");
static_assert(offsetof(AnimStateRec, _pose)       == 0x28, "AnimationState::_pose @ +0x28");
static_assert(offsetof(AnimStateRec, _nodes)      == 0x38, "AnimationState::_nodes @ +0x38");

// ---------------------------------------------------------------------------
// Graphics::AnimationManager::Instance — 0x30 bytes.
//
// One cached, shared skeleton animation: the group/clip it plays, a reference
// count, the bind-pose node list it was built for, and one AnimationState per
// frame of the clip (SetSize(instance->_states, clip->_frameCount)).
//
// Serialized by 0x18005c660: ExternalPtr at +0x00, two ints at +0x08 / +0x0c,
// then the STATES array (+0x20 / count +0x28) BEFORE the nodes array
// (+0x10 / count +0x18). The out-of-order visit is retail's and is reproduced.
// ---------------------------------------------------------------------------
struct AnimInstanceRec {
    void*    _group;                             // +0x00  ExternalPtr<AnimationGroup>
    int32_t  _animIndex;                         // +0x08  init'd to -1 by SetSize's new-tail arm
    int32_t  _refCount;                          // +0x0c  GetInstanceId ++, ResetAnimationState --
    Collection::Array<uint8_t> _nodes;           // +0x10  Array<GraphicsMesh::Node>, stride 0x48
    Collection::Array<uint8_t> _states;          // +0x20  Array<AnimationState>,     stride 0x48
};
static_assert(sizeof(AnimInstanceRec) == 0x30, "AnimationManager::Instance is 0x30 bytes");
static_assert(offsetof(AnimInstanceRec, _group) == 0x0, "AnimInstanceRec::_group");
static_assert(offsetof(AnimInstanceRec, _animIndex) == 0x08, "Instance::_animIndex @ +0x08");
static_assert(offsetof(AnimInstanceRec, _refCount)  == 0x0c, "Instance::_refCount @ +0x0c");
static_assert(offsetof(AnimInstanceRec, _nodes)     == 0x10, "Instance::_nodes @ +0x10");
static_assert(offsetof(AnimInstanceRec, _states)    == 0x20, "Instance::_states @ +0x20");

// ---------------------------------------------------------------------------
// Graphics::AnimationManager — only the one field every body here reads.
//
// `?GetPose@AnimationManager@Graphics@@` opens with
// `MOV RAX,[RCX]` / `... + 0x20 + idx*0x30`, and GetInstanceId reads the count
// at `this + 0x08` and the buffer at `this + 0x00`. The class is larger; this
// is a read-only overlay of its head, not a definition.
// ---------------------------------------------------------------------------
struct AnimationManagerView {
    Collection::Array<uint8_t> _instances;       // +0x00  Array<Instance>, stride 0x30
};
static_assert(offsetof(AnimationManagerView, _instances) == 0x00,
              "AnimationManager::_instances @ +0x00");

// ---------------------------------------------------------------------------
// Graphics::Animation — the clip record, stride 0x70.
//
// Only the fields `?GetPoseAtTime@Animation@Graphics@@` (0x1800faca0) reads are
// named, plus the two `?PlayAnimation@AnimationState@Graphics@@` reads. The
// record is 0x70 bytes and nothing else about it is claimed here.
//
// *** THIS DISAGREES WITH AnimationState.h's `AnimationClipPrefix` ON TWO
// NAMES, and GetPoseAtTime is the body that settles both. *** That header calls
// +0x48 `loopScale` and +0x50 `frameRate`. GetPoseAtTime multiplies the elapsed
// SECONDS by +0x44 to get a frame cursor (so +0x44 is the rate, in frames per
// second) and compares that cursor against +0x50 to decide whether the clip has
// run out (so +0x50 is the FRAME COUNT, not a rate). AnimationState.h's own
// note that +0x50 is "read as an INT and cvtdq2ps'd to float" is consistent
// with a count and not with a stored rate.
// ---------------------------------------------------------------------------
// Declared as OFFSET CONSTANTS rather than as a struct: the record is 0x70
// bytes, only these ten fields have a named reader, and a struct would have to
// invent names for the holes between them.
//   +0x2c  bool  "this clip is instanced"  (PlayAnimation gates GetInstanceId)
//   +0x30  int   first frame of the loop fraction's numerator
//   +0x34  int   last frame  (frameLast - frameFirst is the denominator)
//   +0x3c  bool  looping
//   +0x40  int   loop start frame; 0 short-circuits the restart offset to 0
//   +0x44  float frames per second
//   +0x4c  int   bone count       (`MOV ECX,[RBX+0x4c]` at 0x1800fad9b)
//   +0x50  int   frame count      (`MOV ...,[RBX+0x50]`)
//   +0x58  int64 duration in ticks
//   +0x60  Math::Transform*       the key buffer, boneCount * frameCount keys
//                                 of 0x1c bytes (`MOV RAX,[RBX+0x60]` 0x1800fade4)
namespace anim_clip {
    constexpr int kInstanced   = 0x2c;
    constexpr int kFrameFirst  = 0x30;
    constexpr int kFrameLast   = 0x34;
    constexpr int kLooping     = 0x3c;
    constexpr int kLoopFrame   = 0x40;
    constexpr int kFrameRate   = 0x44;
    constexpr int kBoneCount   = 0x4c;
    constexpr int kFrameCount  = 0x50;
    constexpr int kDuration    = 0x58;
    constexpr int kKeys        = 0x60;
    constexpr int kStride      = 0x70;
}

// Graphics::AnimationGroup — the two fields PlayAnimation and GetInstanceId
// read. `group + 0x20` is the clip buffer and `group + 0x28` its count, indexed
// by `animIndex * 0x70`.
namespace anim_group {
    constexpr int kClips       = 0x20;
    constexpr int kClipCount   = 0x28;
    constexpr int kDefaultFade = 0x30;   // float, seconds
    constexpr int kFadeTable   = 0x48;   // float*, [fromClip * clipCount + toClip]
}

}  // namespace RtGraphics

// ############################################################################
// ENDGAME B.3.5 step 5, block H1 — the REAL `Graphics::AnimationManager`.
//
// `RtGraphics::AnimationManagerView` above (the head overlay) and
// `RtGraphics::AnimationManager` in Animation.h (the whole 0x9d8) both KEEP
// LIVING (B4b-1), and `tests/unit/graphics_animation_abi_test.cpp` cross-asserts
// the class against both.
//
// *** THE `anim_clip` / `anim_group` OFFSET-CONSTANT NAMESPACES ABOVE ARE NOW
// REDUNDANT WITH TWO REAL CLASSES, AND THEY STAY. *** `anim_clip::k*` describe
// `Graphics::Animation`'s fields and `anim_group::k*` describe
// `Graphics::AnimationGroup`'s; every one is CROSS-ASSERTED against the class in
// the unit test, so the two spellings cannot drift. They are kept because
// unconverted bodies outside block H1's file set still use them
// (`wave28_lane_gfxbody.cpp`, `wave29_lane_gfxbody.cpp`), and retiring a
// constant those files read would be editing another block's files.
// ############################################################################

namespace Graphics {

// ============================================================================
// Graphics::AnimationManager — 0x9d8 bytes, NO vtable, ONE INSTANCE.
//
// Both `??0AnimationManager@Graphics@@QEAA@XZ` (0x1800fc430) and
// `??1…@QEAA@XZ` (0x1800fc400) are `QEAA` — non-virtual — and the constructor
// never stores a vtable pointer.
//
// B.4 wave Y (RT-GFX): BOTH ARE REAL NOW (animation.cpp). The constructor names
// `_instances` (Array(): the +0x00 / +0x08 qwords) and `_rngIndex` and nothing else
// by initialiser, so +0x9d4 stays unwritten; the doors placement-new / call
// ~AnimationManager. The paragraph below is the HISTORY of why they were not.
// [was:] NO REAL CONSTRUCTOR OR DESTRUCTOR, and the reason is NOT B4b-2's vtable test
// (there is no vtable). It is retail's own store list. The constructor at
// 0x1800fc430 writes +0x00..+0x13 and then the 624 words at +0x14..+0x9d3, and
// STOPS: the four bytes at +0x9d4 are never written. A C++ constructor with a
// member-initialiser list covering every member would write them — the exact
// class of divergence RULE B5-13 names for `Renderer` +0x3c/+0x4c. The
// constructor also publishes `this` into the exported static `_singleton`, which
// is a global side effect, and `src/game/Game/gameupdateloop_load.cpp:333`
// calls it over raw `MemAlloc`'d memory. The doors in
// `src/engine/graphics/animation.cpp` stay free functions; both members are
// declared here for the record and deliberately left undefined.
//
// `?_singleton@AnimationManager@Graphics@@0PEAV12@EA` @ 0x1801fe7b0 is the
// CLASS STATIC `_singleton` below (ENDGAME B.3.9 block GFX, B39_RULES D7 row
// A44). Every reader and both writers were in this block's files and read it
// directly now; the mint's storage row is retired in the same commit, so no
// redirect is needed.
// ============================================================================
class AnimationManager {
public:
    // ------------------------------------------------------------------------
    // Graphics::AnimationManager::Instance — 0x30 bytes.
    //
    // One cached, shared skeleton animation: the group/clip it plays, a
    // reference count, the bind-pose node list it was built for, and one
    // `AnimationState` per frame of the clip. Retail's own nested name:
    // `Array<AnimationManager::Instance>::Serialize` is 0x18005c660.
    // Field for field the same record as `RtGraphics::AnimInstanceRec` above.
    // ------------------------------------------------------------------------
    //
    // B.4 Y RT: both Arrays carry retail's rows (`NodeArray`, kSpec_AnimState),
    // so the one C++ local (GetInstanceId's fill, 0x1800fb9d0) zeroes the two
    // headers through their ctors; its implicit member ~Arrays, after the two
    // explicit retail ~Array calls zeroed the headers, are no-ops.
    // B.4: the Arrays carry their element type. Nothing else builds or destroys
    // an Instance through a C++ ctor / dtor (the copy ctor 0x18005b700 and the
    // element destroy are the registered rows in array_graphics_anim.cpp), so no
    // Array member ctor / dtor is instantiated through this struct.
    struct Instance {
        AnimationGroup* _group;                    // +0x00  ExternalPtr<AnimationGroup>,
                                                   //        copied as a RAW qword
        int32_t _animIndex;                        // +0x08  -1 on a blanked slot
        int32_t _refCount;                         // +0x0c
        GraphicsMesh::NodeArray _nodes;                // +0x10  Array<Node>, stride 0x48
        AnimationStateArray _states;               // +0x20  Array<AnimationState>, stride 0x48
    };

    // ---- the members retail exports -----------------------------------------
    // ??0AnimationManager@Graphics@@QEAA@XZ        0x1800fc430 — door, see banner
    // ??1AnimationManager@Graphics@@QEAA@XZ        0x1800fc400 — door, see banner
    AnimationManager();
    ~AnimationManager();
    // ??0AnimationManager@Graphics@@QEAA@AEBV01@@Z 0x18005a780  NOT PORTED
    // ??4AnimationManager@Graphics@@QEAAAEAV01@AEBV01@@Z 0x18005a870 NOT PORTED
    AnimationManager(const AnimationManager&);
    AnimationManager& operator=(const AnimationManager&);
    // ??2AnimationManager@Graphics@@SAPEAX_K@Z      0x18005a5b0  NOT PORTED
    // ??2AnimationManager@Graphics@@SAPEAX_KPEAX@Z  0x18005a620  NOT PORTED
    // ??3AnimationManager@Graphics@@SAXPEAX@Z       0x18005a630  NOT PORTED
    static void* operator new(std::size_t bytes);
    static void* operator new(std::size_t bytes, void* place);
    static void  operator delete(void* p);

    // ?Get@AnimationManager@Graphics@@SAPEAV12@XZ   0x18005a690  NOT PORTED
    //   One load of `_singleton`. No caller in the tree.
    static AnimationManager* Get();

    // ?_singleton@AnimationManager@Graphics@@0PEAV12@EA  0x1801fe7b0 — retail's
    // PRIVATE static (`0`). Public here because its readers are the ported
    // free-function bodies across this block's files (animation.cpp's ctor /
    // dtor doors write it; animationstate*.cpp, mesh.cpp, meshinstance_draw.cpp,
    // wave27_lane_gfxbig.cpp and wave28_lane_gfxbody.cpp read it). Defined in
    // animation.cpp; zero-initialised, as the mint zeroed it.
    static AnimationManager* _singleton;

    // An AnimationState layer handle (AnimationState::Layer::_handle) packs
    // `instance << 8 | state`: bits 8-15 index _instances, bits 0-7 that
    // instance's states (GetInstanceId, animationstate_transforms.cpp). Retail
    // decodes with SAR (arithmetic) then MOVZX: the helpers keep `>>` on the
    // signed int.
    static constexpr int32_t kHandleIndexBits = 8;      // shift of the instance index
    static constexpr int32_t kHandleIndexMask = 0xff;   // width of each index
    static constexpr int32_t HandleInstance(int32_t handle) {
        return (handle >> kHandleIndexBits) & kHandleIndexMask;
    }
    static constexpr int32_t HandleState(int32_t handle) {
        return handle & kHandleIndexMask;
    }
    static constexpr int32_t MakeHandle(int32_t instance, int32_t state) {
        return (instance << kHandleIndexBits) | state;
    }

    // ?Update@AnimationManager@Graphics@@QEAAXAEBVTime@System@@_N@Z 0x1800fc240
    //   PORTED — the body is this member, in `wave23_lane_gfxbody.cpp`'s
    //   `namespace Graphics` block; the registered door delegates and keeps both
    //   counters (ENDGAME B.3.5 block GC).
    //
    //   TWO CORRECTIONS TO WHAT THIS NOTE USED TO SAY. (1) The lane's
    //   `RT_GFX23_CORRUPT(n)` effectiveness-probe scaffolding is DELETED, not
    //   worked around: it was guarded by `GFXBODY23_PROBE`, which nothing in the
    //   tree defines, and the `tools/gfxbody23_probe` it named does not exist.
    //   (2) `src/diag/scene_watch.cpp` does NOT read `Attend()` or
    //   `g_animUpdStates` — that was wrong. Their one reader is the lane's own
    //   `RtGraphicsWave23GfxbodySummary()` printf. (`g_animUpdCalls`, the
    //   unreferenced door's entry count, went with the door in B.4 R0.)
    void Update(const System::Time& time, bool rootOnly);

    // ?Serialize@AnimationManager@Graphics@@QEAAXAEAVSerializer@IO@@@Z 0x1800fc190
    //   DECLARED, DELIBERATELY UNDEFINED — the live body is
    //   `wave24_lane_gfxmat.cpp`'s registered door, which is block G's file, not
    //   block H1's. This class is non-polymorphic, so no vtable slot needs a
    //   member definition and one would only be a second resolution path.
    void Serialize(IO::Serializer& s);

    // ?GetPose@AnimationManager@Graphics@@QEBAXHPEAVTransform@Math@@H@Z 0x1800fb8e0
    //   PORTED HERE — the body moved out of animationstate.cpp's
    //   `RtGraphics_AnimationManager_GetPose`. Unguarded on every step, exactly
    //   as retail is: a bad handle indexes off the end of the instance table.
    void GetPose(int handle, Math::Transform* out, int count) const;

    // ?GetPose@AnimationManager@Graphics@@QEBAPEBVTransform@Math@@H@Z 0x18005a6d0
    //   NOT PORTED — the one-argument overload that returns the buffer instead
    //   of copying out of it. A real C++ overload, not a renamed one: B5-17's
    //   distinct-name rule is about overloaded VIRTUALS (MSVC groups and
    //   reverses those within a vtable) and neither of these is virtual.
    const Math::Transform* GetPose(int handle) const;

    // ?GetInstanceId@AnimationManager@Graphics@@QEAAHV?$ExternalPtr@VAnimationGroup
    // @Graphics@@@Object@@HAEBV?$Array@UNode@GraphicsMesh@Graphics@@@Collection@@@Z
    //   0x1800fb9d0. PORTED — the body IS this member, in
    //   animationstate_transforms.cpp's `namespace Graphics` block, and the door
    //   delegates (ENDGAME B.3.5 block GC). It used to be "DECLARED, DELIBERATELY
    //   UNDEFINED ... kept there for the scaffolding reason above"; the
    //   "NOT DIFFABLE: the body allocates instances, bakes poses into them and
    //   moves reference counts" note is DOCUMENTATION, it travelled with the body,
    //   and there was no oracle machinery left to move out from under it.
    //
    //   `nodes` is retail's `const Array<GraphicsMesh::Node>&`, stride 0x48.
    //   `GraphicsMesh` is block G's class and is deliberately not named here.
    int32_t GetInstanceId(Object::ExternalPtr<AnimationGroup> group, int animIndex,
                          const GraphicsMesh::NodeArray& nodes);

    // ?FreeInstanceId@AnimationManager@Graphics@@QEAAXH@Z 0x1800fb970 NOT PORTED
    void FreeInstanceId(int handle);
    // ?GetRootAnimationOffset@…QEBA?AVVector3@Math@@H@Z 0x18005a730 NOT PORTED
    //   hidden retslot (Math::Vector3)
    Math::Vector3 GetRootAnimationOffset(int handle) const;
    // ?GetRootPose@…QEBA?AVTransform@Math@@H@Z 0x1800fb870 NOT PORTED
    //   hidden retslot (Math::Transform)
    Math::Transform GetRootPose(int handle) const;
    // ?GetTransforms@…QEAAPEAV?$InterpolatedState@VTransform@Math@@@2@H@Z
    //   0x18005a700 NOT PORTED  /  the const form 0x18005a6a0 NOT PORTED
    void* GetTransforms(int handle);
    const void* GetTransforms(int handle) const;

    // ---- the 0x9d8 bytes ---------------------------------------------------
    // +0x00  Collection::Array<Instance>, stride 0x30. NOT a CRITICAL_SECTION —
    //        see the CORRECTION banner in Animation.h: 0x1800fc268 reads +0x08
    //        as a LOOP BOUND and 0x1800fc2a0 reads +0x00 as an ARRAY BASE, and a
    //        LockCount is not a loop bound.
    // B.4 wave Y: retail's row (0x18005bf60 is this member's ~Array).
    Collection::Array<Instance, Collection::DefaultAllocator,
                      Collection::ExplicitSpec<&rtcoll::gfxanim::kSpec_AnimInstance>>
        _instances;

    // +0x10  UNVERIFIED — no reader in the ctor, the dtor, `Update` or
    //        `Serialize`. The DO-NOT-DELETE note in Animation.h owns this field's
    //        history; do not re-open it on the same guess.
    uint32_t _rngIndex;

    // +0x14  Mersenne-Twister state, `_time64(NULL)`-seeded by the constructor
    //        with the textbook `init_genrand` recurrence.
    uint32_t _rngState[624];

    // +0x9d4 NEVER WRITTEN by the constructor — alignment to the 0x9d8
    //        allocation size, and a real hole (the B5-13 class).
    uint32_t _pad9d4;
};
static_assert(sizeof(AnimationManager) == 0x9d8,
              "Graphics::AnimationManager must match AllocSub(0x9d8, 8)");
static_assert(offsetof(AnimationManager, _pad9d4) == 0x9D4, "AnimationManager::_pad9d4");
static_assert(alignof(AnimationManager) == 8, "Graphics::AnimationManager aligns to 8");
static_assert(sizeof(AnimationManager::Instance) == 0x30,
              "AnimationManager::Instance is 0x30 bytes");
static_assert(offsetof(AnimationManager::Instance, _group) == 0x0, "Instance::_group");
static_assert(offsetof(AnimationManager, _instances) == 0x00, "_instances @ +0x00");
static_assert(offsetof(AnimationManager, _rngIndex)       == 0x10, "_f10 @ +0x10");
static_assert(offsetof(AnimationManager, _rngState)  == 0x14, "_rngState @ +0x14");
static_assert(offsetof(AnimationManager::Instance, _animIndex) == 0x08, "Instance::_animIndex");
static_assert(offsetof(AnimationManager::Instance, _refCount)  == 0x0c, "Instance::_refCount");
static_assert(offsetof(AnimationManager::Instance, _nodes)     == 0x10, "Instance::_nodes");
static_assert(offsetof(AnimationManager::Instance, _states)    == 0x20, "Instance::_states");
static_assert(sizeof(AnimationManager::Instance) == sizeof(RtGraphics::AnimInstanceRec),
              "the class's nested Instance and the POD describe the same bytes");
static_assert(offsetof(AnimationManager, _instances) ==
              offsetof(RtGraphics::AnimationManagerView, _instances), "alias _instances");
static_assert(!std::is_polymorphic<AnimationManager>::value,
              "Graphics::AnimationManager must never acquire a vtable: both its "
              "ctor and its dtor are QEAA and the ctor stores no vptr");
static_assert(std::is_standard_layout<AnimationManager>::value,
              "the ported doors reach these fields through a reinterpret_cast");

}  // namespace Graphics


// graphics/animationstate.cpp: `Array<AnimationManager::Instance>::Serialize`
// (0x18005c660), typed by its element (B.4).
void RtGraphics_SerializeAnimInstances(IO::Serializer& ser, int count,
                                       Graphics::AnimationManager::Instance* base);
