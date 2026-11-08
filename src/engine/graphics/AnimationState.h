#pragma once
// ============================================================================
// src/engine/graphics/AnimationState.h — Wave 27 lane GFXBIG
// Graphics::AnimationState and the two records it walks.
// ============================================================================
//
// EVERY OFFSET HERE WAS READ OUT OF ONE BODY: `?Update@AnimationState@
// Graphics@@` (retail 0x1800fdb20, ordinal 4474), disassembled with
// `objdump -d -M intel` over the shipped C:\GOG Games\Banished\Runtime-x64.dll.
//
// *** IT WAS NOT READ IN GHIDRA, AND THAT WAS NOT A PREFERENCE. ***
// Ghidra holds TWO OVERLAPPING function bodies at this address — `FUN_1800fd9a0`
// claiming 0x1800fd9a0-0x1800fdb58 and `Update` claiming
// 0x1800fdb20-0x1800fdf45 — and the ghidra-mcp `disassemble_function` endpoint
// resolves an address to the first body containing it. Asked for 0x1800fdb20 it
// returned 126 instructions of the OTHER function, printed under the addresses
// that had been asked for, with no warning. It reported the instruction at
// 0x1800fdb20 as `LEA RAX,[RBX + -0x8]`; the bytes in the file are
// `4c 89 4c 24 20 ...` = `MOV [RSP+0x20],R9`, a prologue.
// See docs/history/runtime_port/WAVE27_LANE_GFXBIG.md "INSTRUMENT DEFECT OBSERVED,
// NOT FIXED".
//
// WHAT IS MEASURED AND WHAT IS INHERITED
// --------------------------------------
// MEASURED HERE: every field this file marks with an instruction address.
// INHERITED (measured by WAVE 23 lane GFXBODY from the CALLER side, in
// `?Update@AnimationManager@Graphics@@` 0x1800fc240, and recorded in
// src/engine/graphics/wave23_lane_gfxbody.cpp §5.1): the 0x48 STRIDE of
// AnimationState and the meaning of +0x18 / +0x28 / +0x38. `Update` itself
// never touches those three, so this header cannot and does not claim to have
// re-derived them; it names their source instead
// (reference_a_measured_number_sitting_beside_an_asserted_one.md is exactly the
// defect being avoided).
//
// NOT PINNED, and left as explicit holes rather than invented: AnimationState
// +0x24, +0x30..+0x37, +0x40..+0x47; AnimationLayer +0x14, +0x31..+0x37;
// AnimationClip everything not listed. No body this lane read touches them.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "../collection/Collection.h"
#include "../object/External.h"   // Object::ExternalPtr<T>, for PlayAnimation
#include "../math/Math.h"          // B.4: Math::Transform / Vector3, the typed members
#include "InterpolatedState.h"     // B.4: Array<InterpolatedState<Math::Transform>>
#include "GraphicsMesh.h"          // B.4: Array<GraphicsMesh::Node>, retail's parameter type
#include "../system/Value.h"       // B.4: System::Time, the three Layer times

// Forward declarations only — see the `namespace Graphics` block at the bottom.
// Tags match the real declarations (RULE B4b-5).
namespace System { class Time; }
namespace Math { class Transform; class Vector3; }
namespace IO { class Serializer; }
namespace Graphics { class AnimationGroup; }

namespace RtGraphics {

// ============================================================================
// One playing animation layer, stride 0x38.
//
// The stride is MEASURED three independent ways inside Update: the iteration
// step `ADD R14,0x38` (0x1800fdef9), the erase-shift `ADD RCX,0x38` with a
// seven-qword copy from `+0x38` (0x1800fdeda / 0x1800fde9c..0x1800fded6), and
// the last-element address `(count-1) * 0x38` via `IMUL RDX,RDX,0x38`
// (0x1800fdbbc).
// ============================================================================
struct AnimationLayer {
    // +0x00  Graphics::AnimationGroup* — the resource the clip lives in.
    //        `MOV RAX,QWORD PTR [R14+R15*1]` at 0x1800fdce9, then
    //        `ADD RBP,QWORD PTR [RAX+0x20]` — so +0x20 of THIS is the clip array.
    void*   group;

    // +0x08  index of the clip within group->clips.
    //        `MOVSXD RBP,DWORD PTR [R14+R15*1+0x8]` at 0x1800fdce4, scaled by
    //        0x70 (`IMUL RBP,RBP,0x70` at 0x1800fdcf4).
    int32_t clipIndex;

    // +0x0c  packed handle into AnimationManager's group array. NEGATIVE means
    //        "no handle" (`TEST ECX,ECX / JS` at 0x1800fde35). When present,
    //        bits 8..15 are the group index: `SAR ECX,0x8` then
    //        `MOVZX EAX,CL` at 0x1800fde39.
    int32_t handle;

    // +0x10  playback rate. Compared against 1.0f (`UCOMISS XMM0,XMM8` at
    //        0x1800fdcfc, XMM8 loaded from 0x18016169c == 1.0f) to take a
    //        fast path that uses the frame delta verbatim.
    float   speed;

    int32_t _pad14;   // +0x14  no reader in Update

    // +0x18  accumulated playback position in System::Time ticks.
    //        Read/written at 0x1800fdd49, 0x1800fdd80, 0x1800fdd98,
    //        0x1800fddec, 0x1800fddf7.
    int64_t time;

    // +0x20  total fade-out length in ticks. `> 0` is what ARMS the countdown
    //        (`CMP QWORD PTR [R14+R15*1+0x20],0x0 / JLE` at 0x1800fddfb).
    int64_t fadeLength;

    // +0x28  remaining fade ticks; decremented by the frame delta and the layer
    //        is REMOVED when it reaches <= 0 (0x1800fde1b / 0x1800fde20).
    int64_t fadeRemaining;

    // +0x30  set to 1 on layer[0] on every entry into Update
    //        (`MOV BYTE PTR [RAX+0x30],0x1` at 0x1800fdb84).
    uint8_t restarted;

    uint8_t _pad31[7];   // +0x31  no reader in Update
};
static_assert(sizeof(AnimationLayer) == 0x38, "Graphics AnimationLayer stride is 0x38");
static_assert(offsetof(AnimationLayer, group) == 0x0, "AnimationLayer::group");
static_assert(offsetof(AnimationLayer, _pad14) == 0x14, "AnimationLayer::_pad14");
static_assert(offsetof(AnimationLayer, _pad31) == 0x31, "AnimationLayer::_pad31");
static_assert(offsetof(AnimationLayer, clipIndex)     == 0x08, "AnimationLayer::clipIndex @ +0x08");
static_assert(offsetof(AnimationLayer, handle)        == 0x0c, "AnimationLayer::handle @ +0x0c");
static_assert(offsetof(AnimationLayer, speed)         == 0x10, "AnimationLayer::speed @ +0x10");
static_assert(offsetof(AnimationLayer, time)          == 0x18, "AnimationLayer::time @ +0x18");
static_assert(offsetof(AnimationLayer, fadeLength)    == 0x20, "AnimationLayer::fadeLength @ +0x20");
static_assert(offsetof(AnimationLayer, fadeRemaining) == 0x28, "AnimationLayer::fadeRemaining @ +0x28");
static_assert(offsetof(AnimationLayer, restarted)     == 0x30, "AnimationLayer::restarted @ +0x30");

// ============================================================================
// One animation clip, stride 0x70 (`IMUL RBP,RBP,0x70` at 0x1800fdcf4).
//
// ONLY the eight fields Update reads are named. The record is 0x70 bytes and
// this header claims nothing about the other 0x48 of it.
// ============================================================================
struct AnimationClipPrefix {
    uint8_t  events[0x08];  // +0x00  no reader in Update

    // +0x08  event count. `> 0` (together with the caller's 8th bool and
    //        layer index 0) is what gates the Animation::ApplyEvents call
    //        (`CMP DWORD PTR [RBP+0x8],R12D / JLE` at 0x1800fdd61).
    int32_t  eventCount;

    uint8_t  eventCapacityAndNames[0x24];  // +0x0c  no reader in Update

    int32_t  frameFirst;      // +0x30  subtrahend in the loop-restart fraction
    int32_t  frameLast;       // +0x34  frameLast - frameFirst is the denominator
    int32_t  rate;    // +0x38  no reader in Update

    // +0x3c  looping flag. Non-zero => wrap `time` modulo `duration` and add a
    //        restart offset; zero => clamp `time` to `duration`
    //        (`CMP BYTE PTR [RBP+0x3c],0x0 / JE` at 0x1800fdd84).
    uint8_t  looping;
    uint8_t  _pad3d[3]; // +0x3d

    int32_t  loopFrame;       // +0x40  ZERO short-circuits the whole fraction to 0
    int32_t  frameRate;    // +0x44
    float    loopScale;       // +0x48  final multiplier
    int32_t  boneCount;    // +0x4c
    // +0x50  read as an INT and cvtdq2ps'd to float.
    //
    // *** THIS IS THE CLIP'S FRAME COUNT, NOT ITS FRAME RATE. Corrected
    // 2026-09-12 (cutover closure batch 3). *** The name here was a guess from
    // the cvtdq2ps; `?GetPoseAtTime@Animation@Graphics@@` (0x1800faca0) settles
    // it by using BOTH fields in one expression — it multiplies elapsed seconds
    // by +0x44 to get a frame cursor, then compares that cursor against +0x50
    // to decide whether the clip has run out and must clamp or wrap. A rate
    // cannot be the bound of a frame index. So +0x44 is the frames-per-second
    // and +0x50 is the number of keyframes; `loopScale` at +0x48 above has no
    // reader in GetPoseAtTime and is left as this lane named it.
    int32_t  frameCount;      // +0x50
    int32_t  _pad54;    // +0x54

    // +0x58  clip duration in System::Time ticks (int64, compared against
    //        `layer.time` with a SIGNED 64-bit CMP at 0x1800fdd8a).
    int64_t  duration;
};
static_assert(offsetof(AnimationClipPrefix, eventCount) == 0x08, "clip::eventCount @ +0x08");
static_assert(offsetof(AnimationClipPrefix, frameFirst) == 0x30, "clip::frameFirst @ +0x30");
static_assert(offsetof(AnimationClipPrefix, frameLast)  == 0x34, "clip::frameLast @ +0x34");
static_assert(offsetof(AnimationClipPrefix, looping)    == 0x3c, "clip::looping @ +0x3c");
static_assert(offsetof(AnimationClipPrefix, loopFrame)  == 0x40, "clip::loopFrame @ +0x40");
static_assert(offsetof(AnimationClipPrefix, loopScale)  == 0x48, "clip::loopScale @ +0x48");
static_assert(offsetof(AnimationClipPrefix, frameCount) == 0x50, "clip::frameCount @ +0x50");
static_assert(offsetof(AnimationClipPrefix, duration)   == 0x58, "clip::duration @ +0x58");
static_assert(sizeof(AnimationClipPrefix) == 0x60,
              "the PREFIX of an animation clip Update reads ends at +0x60; the "
              "record itself is 0x70 and this struct deliberately covers only "
              "the part that has a named reader");
static_assert(offsetof(AnimationClipPrefix, events) == 0x0, "AnimationClipPrefix::events");
static_assert(offsetof(AnimationClipPrefix, eventCapacityAndNames) == 0xC, "AnimationClipPrefix::eventCapacityAndNames");
static_assert(offsetof(AnimationClipPrefix, rate) == 0x38, "AnimationClipPrefix::rate");
static_assert(offsetof(AnimationClipPrefix, _pad3d) == 0x3D, "AnimationClipPrefix::_pad3d");
static_assert(offsetof(AnimationClipPrefix, frameRate) == 0x44, "AnimationClipPrefix::frameRate");
static_assert(offsetof(AnimationClipPrefix, boneCount) == 0x4C, "AnimationClipPrefix::boneCount");
static_assert(offsetof(AnimationClipPrefix, _pad54) == 0x54, "AnimationClipPrefix::_pad54");

// ============================================================================
// Graphics::AnimationState — 0x48 bytes.
//
// THE THREE FIELDS UPDATE ACTUALLY READS ARE +0x00, +0x08 and +0x10, and those
// are the three this lane MEASURED:
//
//   0x1800fdb60  CMP DWORD PTR [RCX+0x10],0x0   ; layer count, early return
//   0x1800fdb6d  MOV RAX,QWORD PTR [RCX+0x8]    ; layer array base
//   0x1800fdb84  MOV BYTE PTR [RAX+0x30],0x1    ; layers[0].restarted
//   0x1800fdbcf  MOV BYTE PTR [RSI],CL          ; +0x00 = 1
//   0x1800fdbd3  CMP BYTE PTR [RSI],0x0         ; +0x00 tested
//   0x1800fdbf8  MOV BYTE PTR [RSI],0x0         ; +0x00 cleared
//   0x1800fdee4  DEC DWORD PTR [RSI+0x10]       ; layer count decremented
//
// +0x08/+0x10/+0x14 are a `Collection::Array<AnimationLayer>` triple. The
// evidence is the same shape lane GFXBODY used for AnimationManager: +0x10 is a
// LOOP BOUND that is also DECREMENTED on removal, and +0x08 is an ARRAY BASE
// indexed by a 0x38 stride. `_layerCapacity` has NO reader in this body and is
// named by position in the triple, not by a measurement — stated plainly.
//
// +0x18 / +0x28 / +0x38 and the 0x48 stride are INHERITED, not measured here:
// see this file's banner.
// ============================================================================

// B.4 Z MIRROR2 GFX: `RtGraphics::InterpolatedTransform`, the POD twin of
// `Graphics::InterpolatedState<Math::Transform>` (InterpolatedState.h: +0x00
// CURRENT, +0x1c PREVIOUS, per `?Lerp@` 0x18001aff0), is deleted: its one user,
// the `RtGraphics::MeshInstance` POD in Mesh.h, had no reader left.

}  // namespace RtGraphics

// ############################################################################
// ENDGAME B.3.5 step 5, block H1 — the REAL `Graphics::AnimationState`.
//
// The three `RtGraphics::` PODs that describe the same 0x48 bytes all KEEP
// LIVING (B4b-1): `RtGraphics::AnimationState` above (lane GFXBIG's, the view
// `Update` reads), `RtGraphics::AnimStateRec` in AnimationManager.h (the
// serializer's view) and `RtGraphics::AnimationLayer` above (the nested record).
// `tests/unit/graphics_animation_abi_test.cpp` cross-asserts the class against
// all three so none of them can drift.
//
// *** THE 0x18-BYTE PREFIX RELATIONSHIP, RECORDED HERE BECAUSE THE OTHER END OF
// IT IS NOT THIS BLOCK'S TO TOUCH. *** `src/engine/graphics/Mesh.h:134-139`
// embeds a 0x18-byte prefix of this record inside `Graphics::MeshInstance` as
// `MeshAnimationState` — the bool at +0x00, the `Array<Layer>` header at +0x08
// and nothing past +0x18. That header belongs to block b35-E (the Geometry
// tree) and block H1 does not edit it. The consequence a later block must
// honour: `MeshAnimationState` is a PREFIX VIEW, not the class. It is NOT a
// duplicate POD in B34_RULES section 1.5.2's sense that could be collapsed by
// including this header, because it genuinely describes fewer bytes — a
// `MeshInstance` carries the first 0x18 of an `AnimationState` inline and the
// rest of the record lives in the AnimationManager's instance pool. B35_RULES
// section 13's line about it ("a PREFIX view, not the class") stands, and the
// unit test pins `offsetof(_rootOffset) == 0x18` so the boundary cannot move
// without failing.
//
// NOTHING IS RETIRED. There is no `??_7AnimationState@Graphics@@6B@`, no
// `_typeInfo`, no deleting-dtor row — the class is NON-POLYMORPHIC (both its
// constructors and its destructor are `QEAA`). The `?_singleton@AnimationManager
// @Graphics@@` cell three of these bodies read stays an `RT_DATA_SLOT` read per
// B35_RULES section 9 / RULE B4b-4.
//
// GREP HAZARD: inside a `namespace Graphics {` block a bare `AnimationState` or
// `AnimationLayer` now binds the CLASS. Spell the PODs `RtGraphics::` there.
// ############################################################################

// The row of `Array<AnimationState::Layer>` (array_graphics_anim.cpp): SetSize
// 0x18005c8a0's init, operator= 0x18001b700, ~Array 0x18001c480.
namespace rtcoll { namespace gfxanim {
extern const ArraySpec kSpec_AnimLayer;
// The row of `Array<InterpolatedState<Math::Transform>>`: SetSize 0x18005ac50's
// identity init, operator= 0x18001b810, ~Array 0x18001c550 (J1).
extern const ArraySpec kSpec_InterpTransform;
} }

namespace Graphics {

// ============================================================================
// Graphics::AnimationState — 0x18 bytes, NO vtable.
//
// B.4 Z MIRROR2 GFX: RETAIL'S CLASS IS 0x18 BYTES, read from its own ctor and
// dtor in the disassembly:
//   ??0AnimationState@Graphics@@QEAA@XZ 0x1800fdaf0:
//       MOV byte [RCX],0 ; MOV qword [RCX+0x8],0 ; MOV qword [RCX+0x10],0 ; RET
//   ??1AnimationState@Graphics@@QEAA@XZ 0x1800fe380:
//       ADD RCX,0x8 ; JMP 0x18001c480                (~Array<Layer> on +0x08)
//   ?IsInstanced@ 0x1800168a0: MOVZX EAX,byte [RCX]  (the bool at +0x00)
// and every member body (Serialize, ResetAnimationState, PlayAnimation, Update,
// UpdateRootOnly, UpdateNodes) reads `this` at +0x00 / +0x08 / +0x10 only. The
// 0x48 record the AnimationManager keeps per frame of an instanced clip EMBEDS
// one at +0x00 (its copy ctor 0x18005b660 opens with this class's inlined copy
// ctor 0x1800168d0 -- the byte, then the zeroed header and CALL 0x18001b700 --
// and its element destroy 0x18005bce0 frees +0x08 last): that record is
// `InstancedAnimationState` below. `MeshInstance` embeds one at +0xd8 (its ctor
// 0x1800fffc0 inlines 0x1800fdaf0, its dtor 0x180017060 inlines 0x1800fe380).
//
// The ctor and dtor are retail's inlined bodies (defined here, inline, as retail
// inlines them at both sites). The copy ctor and operator= stay declared and
// undefined: nothing in the tree copies one as a C++ object (the record's copy
// is the registered 0x18005b660 row, MeshInstance's copy ctor 0x1800170c0 is
// hand-written in retail's store order).
// ============================================================================
class AnimationState {
public:
    // ------------------------------------------------------------------------
    // Graphics::AnimationState::Layer — one playing layer, stride 0x38.
    //
    // Retail's own nested name: `Array<AnimationState::Layer>::Serialize` is
    // 0x18005cb40 and `…::SetSize` is 0x18005c8a0. The stride is MEASURED three
    // independent ways inside `Update` — see `RtGraphics::AnimationLayer`'s
    // banner above, which this declaration mirrors field for field.
    // ------------------------------------------------------------------------
    struct Layer {
        AnimationGroup* _group;  // +0x00  Graphics::AnimationGroup* (a raw copy;
                                 //        the reference is the layer's, not the
                                 //        instance cache entry's)
        int32_t _clipIndex;      // +0x08  index into _group->_clips, scaled 0x70
        int32_t _handle;         // +0x0c  packed (instance << 8 | frame), or < 0
        float   _speed;          // +0x10
        int32_t _pad14;    // +0x14  no reader in any body read
        // B.4: System::Time, retail's type -- `GetPoseAtTime` takes `+0x18` as
        // its `const Time&` (0x1800fcc20) and the fade weight reads the other two
        // as tick counts.
        System::Time _time;          // +0x18
        System::Time _fadeLength;    // +0x20  > 0 is what ARMS the countdown
        System::Time _fadeRemaining; // +0x28  <= 0 removes the layer
        bool    _restarted;      // +0x30  bool: SerializeBool (0x18005cb40)
        uint8_t _pad31[7]; // +0x31
    };

    // ---- the members retail exports ----------------------------------------
    // ??0AnimationState@Graphics@@QEAA@XZ        0x1800fdaf0  the bool, then the
    //   Array header's two qwords; +0x01..+0x07 not written.
    AnimationState() : _instanced(false), _layers() {}
    // ??0AnimationState@Graphics@@QEAA@AEBV01@@Z 0x1800168d0  NOT PORTED
    //   (the UNEXPORTED array-element copy constructor of the 0x48 record at
    //    0x18005b660 IS ported, as `RtGraphics_AnimState_CopyCtor` in
    //    animationstate.cpp — a different address and a different function)
    AnimationState(const AnimationState&);
    // ??1AnimationState@Graphics@@QEAA@XZ        0x1800fe380  `JMP 0x18001c480`
    //   on +0x08: the body is empty, the member ~Array<Layer> is the release.
    ~AnimationState() {}
    // ??4AnimationState@Graphics@@QEAAAEAV01@AEBV01@@Z 0x180016910 NOT PORTED
    AnimationState& operator=(const AnimationState&);

    // ?Serialize@AnimationState@Graphics@@QEAAXAEAVSerializer@IO@@@Z 0x1800fe250
    //   PORTED HERE — the body moved out of animationstate.cpp's
    //   `RtGraphics_AnimationState_Serialize`.
    //
    // *** IT SERIALIZES ONLY THE LAYERS AND THE BOOL. *** `_pose`, `_nodes` and
    // `_rootOffset` are derived state that `Update` recomputes next tick.
    void Serialize(IO::Serializer& s);

    // ?ResetAnimationState@AnimationState@Graphics@@QEAAXXZ  0x1800fd930
    //   PORTED HERE — the body moved out of animationstate.cpp.
    void ResetAnimationState();

    // ?PlayAnimation@AnimationState@Graphics@@QEAAXV?$ExternalPtr@VAnimationGroup
    // @Graphics@@@Object@@HAEBV?$Array@UNode@GraphicsMesh@Graphics@@@Collection@@
    // AEBVTime@System@@2M_N@Z   0x1800fdf50
    //
    // PORTED — the body IS this member, in animationstate_transforms.cpp's
    // `namespace Graphics` block, and the registered door delegates to it
    // (ENDGAME B.3.5 block GC). This note used to say the body stayed put
    // because it carried its lane's diff-oracle scaffolding and "moving a body
    // out from under its own scaffolding is a second change riding on this one".
    // THAT SCAFFOLDING IS GONE: the five replay captures in that file had no
    // reader anywhere in the tree — they were its five -Wunused-variable
    // warnings — so the move is a move again. The narrative that explains WHY a
    // replay against the DLL would be destructive is kept at the body.
    //
    // THE 5th AND 6th PARAMETERS ARE NEVER READ (the fade comes from the group's
    // own table, the speed is the literal 1.0f). Kept for retail's arity.
    //
    // `nodes` is retail's `const Collection::Array<GraphicsMesh::Node>&` (`NodeArray`), stride
    // 0x48. `GraphicsMesh` is block G's class and is deliberately NOT named here
    // (the H1 brief: forward-declare, do not convert), so the parameter is the
    // raw 16-byte Array header every ported body in this cluster already uses.
    void PlayAnimation(Object::ExternalPtr<AnimationGroup> group, int animIndex,
                       const GraphicsMesh::NodeArray& nodes,
                       const System::Time& startTime, const System::Time& fadeTime,
                       float speed, bool instanced);

    // ?Update@AnimationState@Graphics@@QEAA_N…   0x1800fdb20
    //   PORTED — the body IS this member, in wave27_lane_gfxbig.cpp's
    //   `namespace Graphics` block (ENDGAME B.3.5 block GC). Its three calls onto
    //   this class's own `CopyInstancedToUnique` / `UpdateRootOnly` /
    //   `UpdateNodes` are QUALIFIED member calls now, not three resolver hits.
    // B.4: retail's parameter types (the mangled name's `PEAV?$InterpolatedState@
    // VTransform@Math@@@2@PEAV78@`): the node transforms, then the pose buffer.
    bool Update(const System::Time& dt, const GraphicsMesh::NodeArray& nodes,
                const Math::Transform& root, InterpolatedState<Math::Transform>* interp,
                Math::Transform* pose, Math::Vector3& rootOffsetOut, bool rootOnly,
                bool flag8);

    // ?GetAnimationElapsed@          0x1800fe220  NOT PORTED
    float GetAnimationElapsed() const;
    // ?GetCurrentAnimationIndex@     0x1800fda90  NOT PORTED
    int32_t GetCurrentAnimationIndex() const;
    // ?GetCurrentAnimationTimeScale@ 0x1800fda70  NOT PORTED
    float GetCurrentAnimationTimeScale() const;
    // ?SetCurrentAnimationTimeScale@ 0x1800fda50  NOT PORTED
    void SetCurrentAnimationTimeScale(float scale);
    // ?GetInstanceId@                0x1800168b0  NOT PORTED
    int32_t GetInstanceId() const;
    // ?GetInstanceTransforms@        0x1800fe2d0  NOT PORTED
    //   retail returns `const InterpolatedState<Transform>*`
    const void* GetInstanceTransforms() const;
    // ?InstancedRootAnimationOffset@ 0x1800fe320  NOT PORTED — hidden retslot
    Math::Vector3 InstancedRootAnimationOffset() const;
    // ?IsAnimating@                  0x1800fdab0  NOT PORTED
    bool IsAnimating() const;
    // ?IsCurrentAnimationLooping@    0x1800fda20  NOT PORTED
    bool IsCurrentAnimationLooping() const;
    // ?IsInstanced@                  0x1800168a0  NOT PORTED
    bool IsInstanced() const;

    // The three `AEAA`/`AEBA` members — retail PRIVATE, and the mangling says so
    // (`A` is the private access code). This DLL exports its private members
    // (reference_aeaa_is_private_not_unexported.md).
    //
    // THEY ARE PUBLIC HERE, not private, and block GC changed that when it moved
    // their bodies onto the members: each one's registered door is an
    // `extern "C"` FREE function that has to make the qualified call, and a free
    // function cannot be a friend of this class without a declaration this
    // header does not need. That is exactly the reason and the wording block G
    // recorded for `MaterialInstance::FindConstant` / `FindTexture`
    // (`wave28_lane_gfxbody.cpp`), and retail's own access code is kept in the
    // comment rather than in the C++.
    //
    // ALL THREE BODIES ARE NOW MEMBERS. `CopyInstancedToUnique` and
    // `UpdateNodes` are in `animationstate_transforms.cpp`, `UpdateRootOnly` in
    // `wave28_lane_gfxbody.cpp` — each in that file's `namespace Graphics` block,
    // with its lane's attendance counters left in the door.
    //
    // ?CopyInstancedToUnique@ 0x1800fd4c0 — `this` IS NEVER READ (RCX is
    //   overwritten four instructions in); the parameter stays for the ABI.
    void CopyInstancedToUnique(const GraphicsMesh::NodeArray& nodes,
                               const Math::Transform& root,
                               InterpolatedState<Math::Transform>* interp,
                               int handle) const;
    // ?UpdateNodes@    0x1800fcc20
    void UpdateNodes(float speed, const GraphicsMesh::NodeArray& nodes,
                     const Math::Transform& root,
                     InterpolatedState<Math::Transform>* interp, Math::Transform* pose,
                     Math::Vector3& rootOffsetOut);
    // ?UpdateRootOnly@ 0x1800fc870 — body in wave28_lane_gfxbody.cpp
    void UpdateRootOnly(float speed, Math::Vector3& rootOffsetOut);

    // ---- the 0x18 bytes ----------------------------------------------------
    // +0x00  the bool `?IsInstanced@` (0x1800168a0) returns and retail's
    //        Serialize writes with SerializeBool (0x1800fe250). `Update` sets
    //        it when exactly one layer's handle is >= 0, then calls
    //        `CopyInstancedToUnique` and clears it.
    bool     _instanced;
    uint8_t  _pad01[7];                  // +0x01  never written by the ctor
    // B.4 Z MIRROR2 GFX: on retail's row, kSpec_AnimLayer -- its SetSize is
    // 0x18005c8a0, operator= 0x18001b700, ~Array 0x18001c480 (J1: the row has
    // no destroy step, as the trait row of the trivially destructible Layer
    // did not).
    using LayerArray = Collection::Array<Layer, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&rtcoll::gfxanim::kSpec_AnimLayer>>;
    LayerArray _layers;                  // +0x08  stride 0x38
};
static_assert(sizeof(AnimationState) == 0x18,
              "Graphics::AnimationState is 0x18 bytes -- its ctor 0x1800fdaf0 "
              "writes +0x00 / +0x08 / +0x10 and its dtor 0x1800fe380 frees +0x08");
static_assert(offsetof(AnimationState, _pad01) == 0x1, "AnimationState::_pad01");
static_assert(alignof(AnimationState) == 8, "Graphics::AnimationState aligns to 8");
static_assert(sizeof(AnimationState::Layer) == 0x38,
              "Graphics::AnimationState::Layer stride is 0x38");
static_assert(offsetof(AnimationState::Layer, _group) == 0x0, "Layer::_group");
static_assert(offsetof(AnimationState::Layer, _pad14) == 0x14, "Layer::_pad14");
static_assert(offsetof(AnimationState::Layer, _pad31) == 0x31, "Layer::_pad31");
static_assert(offsetof(AnimationState, _instanced)  == 0x00, "AnimationState::_instanced @ +0x00");
static_assert(offsetof(AnimationState, _layers)     == 0x08, "AnimationState::_layers @ +0x08");

// B.4 Z MIRROR2 GFX: `Array<InterpolatedState<Math::Transform>>` on retail's
// row (kSpec_InterpTransform: stride 0x38, alignment 4, no destroy step) --
// `MeshInstance::_nodes` and `InstancedAnimationState::_nodes`, the two members
// retail's 0x18005ac50 / 0x18001b810 / 0x18001c550 serve.
using InterpTransformArray = Collection::Array<InterpolatedState<Math::Transform>,
                                               Collection::DefaultAllocator,
                                               Collection::ExplicitSpec<&rtcoll::gfxanim::kSpec_InterpTransform>>;

// ============================================================================
// Graphics::InstancedAnimationState — 0x48 bytes, the element of
// `AnimationManager::Instance::_states` (one per frame of the instanced clip).
//
// Retail's own name for this record is not in the export table (its Array
// instantiation 0x18005b130 / 0x18005b1e0 / 0x18005bbb0 is unexported); it is
// named by role -- the INSTANCED pool slot `MeshInstance` reads when its
// `_animation._instanced` is set. Its layout, from retail:
//   * copy ctor 0x18005b660: the AnimationState at +0x00 (byte, zeroed header,
//     CALL 0x18001b700), three dwords +0x18..+0x20, then the zeroed headers at
//     +0x28 (CALL 0x18001b360) and +0x38 (CALL 0x18001b810);
//   * element destroy 0x18005bce0: frees +0x38, then +0x28, then +0x08 -- the
//     reverse member order, the AnimationState last;
//   * `?GetPose@AnimationManager@` 0x1800fb8e0 reads +0x28 as the pose buffer.
//
// NO C++ CONSTRUCTOR OR DESTRUCTOR IS DEFINED: every live one is an element of
// `AnimationStateArray`, whose construction, copy and destruction run through
// the REGISTERED `kSpec_AnimState` row at retail's addresses (0x18005b1e0's
// zeroing init, the copy ctor 0x18005b660, the destroy 0x18005bce0). Declared
// and undefined so no second lifecycle can be generated beside the row's.
// ============================================================================
struct InstancedAnimationState {
    InstancedAnimationState();
    InstancedAnimationState(const InstancedAnimationState&);
    ~InstancedAnimationState();
    InstancedAnimationState& operator=(const InstancedAnimationState&);

    AnimationState _state;               // +0x00  0x18 bytes
    Math::Vector3 _rootOffset;           // +0x18  Update's rootOffsetOut
    int32_t  _pad24;                     // +0x24  never written by the copy ctor
    Collection::Array<Math::Transform> _pose;   // +0x28  stride 0x1c
    InterpTransformArray _nodes;         // +0x38  stride 0x38
};
static_assert(sizeof(InstancedAnimationState) == 0x48,
              "the AnimationManager's per-frame record is 0x48 bytes -- the stride "
              "AnimationManager::Update walks and the record kD_InnerArr3_48 destroys");
static_assert(offsetof(InstancedAnimationState, _pad24) == 0x24, "InstancedAnimationState::_pad24");
static_assert(alignof(InstancedAnimationState) == 8, "aligns to 8");
static_assert(offsetof(InstancedAnimationState, _state)      == 0x00, "_state @ +0x00");
static_assert(offsetof(InstancedAnimationState, _rootOffset) == 0x18, "_rootOffset @ +0x18");
static_assert(offsetof(InstancedAnimationState, _pose)       == 0x28, "_pose @ +0x28");
static_assert(offsetof(InstancedAnimationState, _nodes)      == 0x38, "_nodes @ +0x38");
static_assert(offsetof(AnimationState::Layer, _clipIndex)     == 0x08, "Layer::_clipIndex @ +0x08");
static_assert(offsetof(AnimationState::Layer, _handle)        == 0x0c, "Layer::_handle @ +0x0c");
static_assert(offsetof(AnimationState::Layer, _speed)         == 0x10, "Layer::_speed @ +0x10");
static_assert(offsetof(AnimationState::Layer, _time)          == 0x18, "Layer::_time @ +0x18");
static_assert(offsetof(AnimationState::Layer, _fadeLength)    == 0x20, "Layer::_fadeLength @ +0x20");
static_assert(offsetof(AnimationState::Layer, _fadeRemaining) == 0x28, "Layer::_fadeRemaining @ +0x28");
static_assert(offsetof(AnimationState::Layer, _restarted)     == 0x30, "Layer::_restarted @ +0x30");
// The record and the PODs describe the same bytes.
static_assert(sizeof(AnimationState::Layer) == sizeof(RtGraphics::AnimationLayer), "alias Layer");
static_assert(!std::is_polymorphic<AnimationState>::value,
              "Graphics::AnimationState must never acquire a vtable: retail exports "
              "no ??_7AnimationState@Graphics@@6B@ and its ctor/dtor are QEAA");
static_assert(std::is_standard_layout<InstancedAnimationState>::value,
              "the kSpec_AnimState row's callbacks reach the record through its element pointer");

}  // namespace Graphics

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.
namespace rt { namespace io { struct Serializer; } }   // io/Serializer.h

// ---- graphics/animationstate.cpp: three `Array<T>::Serialize` instantiations
// (Collection::Array has no Serialize member -- Collection.h's banner), typed
// by the element retail's instantiation names (B.4).
void RtGraphics_SerializeMeshNodes(IO::Serializer& ser, int count,
                                   Graphics::GraphicsMesh::Node* base);  // 0x18005c120
void RtGraphics_SerializeTransformArray(Collection::Array<Math::Transform>* a,
                                        IO::Serializer& ser);  // 0x180059820
// ==== end B3R W0-P0c ====

// ==== B3R RT-VG: the graphics-anim Collection::Array instantiations ====
// Defined in src/engine/collection/array_graphics_anim.cpp (given external
// linkage for their direct readers in src/engine/graphics/).
// B.4 Y RT: the hand-derived row of `Array<Graphics::AnimationState>`
// (src/engine/collection/array_graphics_anim.cpp): SetSize 0x18005b1e0's
// zeroing init, the copy ctor 0x18005b660, the element destroy 0x18005bce0 that
// ~Array 0x18005bbb0 runs. The tree's AnimationState ctor / dtor are declared
// and undefined (retail's are these rows), so `AnimationStateArray` names it.
namespace rtcoll { namespace gfxanim {
extern const ArraySpec kSpec_AnimState;
} }
namespace Graphics {
// `Collection::Array<AnimationState>` with retail's element lifecycle: SetSize
// 0x18005b1e0, operator= 0x18005b130, ~Array 0x18005bbb0 are the
// `kSpec_AnimState` row (AnimationManager::Instance::_states).
using AnimationStateArray = Collection::Array<InstancedAnimationState, Collection::DefaultAllocator,
                                              Collection::ExplicitSpec<&rtcoll::gfxanim::kSpec_AnimState>>;
}  // namespace Graphics
namespace rtcoll {
void* Rt_ArrayDtor_180014090(Graphics::GraphicsMesh::NodeArray* a);  // 0x180014090 ~Array<GraphicsMesh::Node>
void* Rt_ArrayDtor_18005bbb0(Graphics::AnimationStateArray* a);  // 0x18005bbb0 ~Array<AnimationState>
void Rt_SetSize_18005aa30(Graphics::GraphicsMesh::NodeArray* a, int n);  // 0x18005aa30 Array<GraphicsMesh::Node>
void Rt_SetSize_18005b1e0(Graphics::AnimationStateArray* a, int n);  // 0x18005b1e0 Array<AnimationState>
void Rt_SetSize_18005c8a0(Graphics::AnimationState::LayerArray* a, int n);  // 0x18005c8a0 Array<AnimationState::Layer>
void Rt_SetSize_18005ac50(Graphics::InterpTransformArray* a, int n);  // 0x18005ac50 Array<InterpolatedState<Transform>>
void Rt_SetSize_18005c3d0(void* a, int n);  // 0x18005c3d0 Array<Instance>
void Rt_SetSize_18005be30(void* a, int n, const void* fill);  // 0x18005be30 Array<Instance>, fill
Graphics::GraphicsMesh::NodeArray* Rt_ArrayAssign_180013960(Graphics::GraphicsMesh::NodeArray* d,
                                                            const Graphics::GraphicsMesh::NodeArray* s);  // 0x180013960 Array<GraphicsMesh::Node>
Graphics::AnimationStateArray* Rt_ArrayAssign_18005b130(Graphics::AnimationStateArray* d,
                                                        const Graphics::AnimationStateArray* s);  // 0x18005b130 Array<AnimationState>
Graphics::AnimationState::LayerArray* Rt_ArrayAssign_18001b700(
        Graphics::AnimationState::LayerArray* d,
        const Graphics::AnimationState::LayerArray* s);  // 0x18001b700 Array<AnimationState::Layer>
Graphics::InterpTransformArray* Rt_ArrayAssign_18001b810(
        Graphics::InterpTransformArray* d,
        const Graphics::InterpTransformArray* s);  // 0x18001b810 Array<InterpolatedState<Transform>>
}  // namespace rtcoll
// ---- graphics/wave27_lane_gfxbig.cpp: the Update door (it counts its calls
// for the [GFXBIG27] line, then calls Graphics::AnimationState::Update)
extern "C" bool RtGraphics_AnimationState_Update(
        Graphics::AnimationState* self, const System::Time* time,
        const Graphics::GraphicsMesh::NodeArray* nodes,
        const Math::Transform* baseTransform,
        Graphics::InterpolatedState<Math::Transform>* interp, Math::Transform* pose,
        Math::Vector3* outVec3, bool rootOnly, bool applyEvents);  // 0x1800fdb20
// ==== end B3R RT-VG ====
