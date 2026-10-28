// ============================================================================
// src/engine/graphics/animationstate_transforms.cpp
// The five Graphics animation bodies that carry hand-scheduled floating point
// (cutover closure batch 3b, 2026-09-12).
// ============================================================================
//
// WHAT IS IN HERE, with retail addresses and the export name each registers:
//
//   0x1800fdf50  ?PlayAnimation@AnimationState@Graphics@@QEAAXV?$ExternalPtr@
//                VAnimationGroup@Graphics@@@Object@@HAEBV?$Array@UNode@
//                GraphicsMesh@Graphics@@@Collection@@AEBVTime@System@@2M_N@Z
//   0x1800fb9d0  ?GetInstanceId@AnimationManager@Graphics@@QEAAHV?$ExternalPtr@
//                VAnimationGroup@Graphics@@@Object@@HAEBV?$Array@UNode@
//                GraphicsMesh@Graphics@@@Collection@@@Z
//   0x1800fd4c0  ?CopyInstancedToUnique@AnimationState@Graphics@@AEBAXAEBV
//                ?$Array@UNode@GraphicsMesh@Graphics@@@Collection@@AEBV
//                Transform@Math@@PEAV?$InterpolatedState@VTransform@Math@@@2@H@Z
//   0x1800fcc20  ?UpdateNodes@AnimationState@Graphics@@AEAAXMAEBV?$Array@UNode@
//                GraphicsMesh@Graphics@@@Collection@@AEBVTransform@Math@@PEAV
//                ?$InterpolatedState@VTransform@Math@@@2@PEAV56@AEAVVector3@6@@Z
//   0x180100070  ?GetBones@MeshInstance@Graphics@@QEBAHMAEBVTransform@Math@@
//                PEAVMatrix@4@@Z
//
// The serializable half of the same cluster is next door in animationstate.cpp;
// its Array<T> instantiations are in src/engine/collection/array_graphics_anim.cpp
// and the record layouts are pinned in src/engine/graphics/AnimationManager.h.
// Nothing here re-derives a layout those three already assert.
//
// ---------------------------------------------------------------------------
// WHY THIS IS A SEPARATE FILE, AND WHY THE ARITHMETIC IS WRITTEN OUT LONGHAND
// ---------------------------------------------------------------------------
// Four of the five inline a quaternion multiply of a PURE quaternion (the
// vector to rotate, with w == 0) by a parent rotation, then multiply the result
// by the parent's conjugate — the classic `q * v * q^-1` rotate — and MSVC
// hand-scheduled each of those four DIFFERENTLY. Single-precision addition is
// not associative, so `a + b + c` and `a + (b + c)` are different floats, and a
// pose that is one ULP off on a bone propagates down the skeleton.
//
// So every `mulss` / `addss` / `subss` in the instruction stream is one C
// statement on a `float` here, in the instruction stream's order, with no fused
// expressions and no reassociation. The `xorps reg,[0x1801617c0]` sign flips
// (that constant is four -0.0f, read out of the DLL) are written as unary
// minus on a float, which is the same bit operation. The compiler is not asked
// to contract anything: the project builds with no `-ffp-contract` override and
// no `-mfma`, so the x86-64 baseline has no FMA to contract into, and this file
// adds no flags of its own (Makefile SA_CXXFLAGS, unchanged).
//
// A shared helper was considered and rejected on purpose: the four schedules
// are NOT the same order, so one helper would silently impose one of them on
// the other three. src/engine/math/transform.cpp already holds the project's
// ported `Math::Transform::operator*` with its OWN order, and it is likewise
// not reused here for the inlined copies — only for the real `CALL`s retail
// makes, which go through the port registry so our own body answers.
//
// ---------------------------------------------------------------------------
// THE INSTANCE HANDLE
// ---------------------------------------------------------------------------
// Same packed pair animationstate.cpp documents: `instanceIndex << 8 |
// frameIndex`, negative meaning "not instanced". GetInstanceId's last two
// instructions build it (`SHL ESI,0x8 / OR ESI,R14D`); PlayAnimation,
// CopyInstancedToUnique, UpdateNodes and GetBones all take it apart with
// `(h >> 8) & 0xff` and `h & 0xff`.
//
// ---------------------------------------------------------------------------
// HIDDEN RETURN SLOTS
// ---------------------------------------------------------------------------
// Four Math entry points here return a value by reference in RDX, which shifts
// the real arguments right (reference_hidden_retslot_wild_write.md):
//   ??DQuaternion@Math@@QEBA?AV01@AEBV01@@Z   (this, ret, rhs)
//   ?Rotate@Quaternion@Math@@QEBA?AVVector3@2@AEBV32@@Z  (this, ret, v)
//   ?Lerp@Quaternion@Math@@QEBA?AV12@AEBV12@AEBVScalar@2@@Z (from, ret, to, &t)
//   ?Inverse@Transform@Math@@QEBA?AV12@XZ     (this, ret)
//   ?AsMatrix@Transform@Math@@QEBA?AVMatrix@2@XZ (this, ret)
// Each returns the slot pointer in RAX, and retail reads the result back
// through RAX rather than through the buffer it passed; that is reproduced.
//
// PORTABILITY: no <windows.h>, per PLAN §4.1 decision 1.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "math/Math.h"
#include "collection/Collection.h"
#include "graphics/AnimationManager.h"
#include "graphics/AnimationState.h"   // ENDGAME B.3.5 block GC
#include "graphics/Mesh.h"             // Graphics::MeshInstance, ditto
#include "graphics/InterpolatedState.h"  // ENDGAME B.3.5 closing pass:
                                                     // Graphics::InterpolatedState<T>
#include <new>                                       // placement new, for the
                                                     // hidden-retslot door below
#include "system/Value.h"                // System::Time, ditto
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "graphics/AnimationGroup.h"   // B.4: AnimationGroup::GetAnimation
#include "graphics/Animation.h"   // Graphics::Animation::GetPoseAtTime

using Graphics::AnimationManager;
using Graphics::InterpolatedState;

namespace {

// ---------------------------------------------------------------------------
// The neighbours these bodies call, by name (B3R): the Collection::Array<T>
// instantiations are rtcoll::Rt_SetSize_* / Rt_ArrayAssign_* / Rt_ArrayDtor_*
// (array_graphics_anim.cpp, declared in AnimationState.h) and
// rtcoll::RtColl_ArraySetSize_180059140 (Array<Math::Transform>::SetSize).
// 0x18005be30 is Array<AnimationManager::Instance>::SetSize, the THREE-argument
// form whose new tail elements are copy-assigned from `fill`.
//
// Math. The real CALLs retail makes (Quaternion::operator* 0x18001e280,
// Quaternion::Rotate 0x18001e140, Quaternion::Lerp 0x18001d500,
// Transform::Inverse 0x18005db50, Transform::AsMatrix 0x18001e5f0) are the
// members of the real Math classes; each returns by value and the assignment
// at the call site is the store retail makes through the hidden return slot.
// (B.4: the five `(self, ret, …) -> const float*` wrappers that viewed raw
// float storage as those classes are gone with the raw storage.)
// ---------------------------------------------------------------------------

// ?_secondsPerTick@Timer@System@@0NA — a DOUBLE at 0x1801fe728. Seconds->ticks
// is a DIVIDE by it here (`DIVSD`), where GetPoseAtTime next door multiplies;
// both spellings are retail's, in the direction each needs.
double SecondsPerTick() {
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    return ::System::Timer::_secondsPerTick;
}

// `CVTTSD2SI` after a `DIVSD` — seconds to System::Time ticks, truncating.
int64_t SecondsToTicks(double seconds) {
    return static_cast<int64_t>(seconds / SecondsPerTick());
}

}  // namespace

// ############################################################################
// ENDGAME B.3.5 step 5, block GC — the `Graphics::AnimationState` member bodies.
//
// Their own `namespace Graphics { }` block: a member definition has to appear in
// a namespace ENCLOSING the class. Every helper this file defines sits in the
// GLOBAL unnamed namespace, so unqualified lookup from here reaches all of them.
//
// WHY THE ARITHMETIC IS WRITTEN OUT LONGHAND is this file's banner: every
// `mulss` / `addss` / `subss` is one C statement on a `float`, in the
// instruction stream's order. B.4 changed only how each operand is REACHED
// (a named field of a typed record instead of a byte offset); no statement
// moved and no expression was fused.
// ############################################################################

namespace Graphics {

// ---------------------------------------------------------------------------
// ?PlayAnimation@AnimationState@Graphics@@  0x1800fdf50 — ENDGAME B.3.5 block GC.
//
// NOT DIFFABLE against the DLL. The body mutates the layer array AND the
// AnimationManager's shared instance pool (reference counts, SetSize); running
// the original a second time on the same inputs would double every one of
// those.
//
// THE 5th AND 6th PARAMETERS ARE NEVER READ (the fade comes from the group's
// own table, the speed is the literal 1.0f). Arm A stores fadeRemaining before
// fadeLength; arm B the reverse. Both orders are retail's.
// ---------------------------------------------------------------------------
void AnimationState::PlayAnimation(Object::ExternalPtr<AnimationGroup> groupHandle,
                                   int animIndex,
                                   const GraphicsMesh::NodeArray& nodes,
                                   const System::Time& startTime,
                                   const System::Time& /*fadeTime, unread*/,
                                   float /*speed, unread*/, bool instanced) {
    AnimationGroup* const group = groupHandle.Get();

    if (animIndex < 0) {
        for (int32_t i = 0; i < _layers._size; ++i) {
            const int32_t handle = _layers._data[i]._handle;
            if (handle < 0) continue;                       // TEST ECX,ECX / JS

            AnimationManager::Instance* inst =
                &AnimationManager::_singleton->_instances._data[(handle >> 8) & 0xff];
            if (--inst->_refCount != 0) continue;

            inst->_group     = nullptr;
            inst->_animIndex = -1;
            rtcoll::Rt_SetSize_18005aa30(&inst->_nodes, 0);               // CALL 0x18005aa30
            rtcoll::Rt_SetSize_18005b1e0(&inst->_states, 0);              // CALL 0x18005b1e0
        }
        rtcoll::Rt_SetSize_18005c8a0(&_layers, 0);                        // CALL 0x18005c8a0
        return;
    }

    const int32_t count = _layers._size;

    if (count > 0) {
        Layer& l0 = _layers._data[0];
        if (l0._group == group) {
            const int32_t clip0 = l0._clipIndex;
            if (clip0 == animIndex) {
                if (group->GetAnimation(clip0)._looping) {
                    l0._speed = 1.0f;
                    return;
                }
            }
        }
    }

    if (count == 0) {
        rtcoll::Rt_SetSize_18005c8a0(&_layers, 1);
    } else {
        Layer* l = _layers._data;
        const bool armLayer0 = l[0]._restarted || (count == 1);

        if (!armLayer0) {
            // Arm A: re-arm the fade on layer 1. Note the STORE ORDER —
            // fadeRemaining (+0x28) before fadeLength (+0x20) — which is the
            // reverse of arm B below. Both are retail's.
            float fade;
            const int32_t clip1 = l[1]._clipIndex;
            if (l[1]._group == group && clip1 >= 0) {
                const int64_t clipCount = group->_clips._size;
                fade = group->_blendTimes._data[clipCount * animIndex + clip1];
            } else {
                fade = group->_defaultBlend;
            }
            const int64_t ticks = SecondsToTicks(static_cast<double>(fade));
            l[1]._fadeRemaining._ticks = ticks;
            _layers._data[1]._fadeLength._ticks = ticks;
        } else {
            // Arm B: arm the fade on layer 0, then insert a new layer 0.
            float fade;
            const int32_t clip0 = l[0]._clipIndex;
            if (l[0]._group == group && clip0 >= 0) {
                const int64_t clipCount = group->_clips._size;
                fade = group->_blendTimes._data[clipCount * animIndex + clip0];
            } else {
                fade = group->_defaultBlend;
            }
            const int64_t ticks = SecondsToTicks(static_cast<double>(fade));
            l[0]._fadeLength._ticks = ticks;
            _layers._data[0]._fadeRemaining._ticks = ticks;

            rtcoll::Rt_SetSize_18005c8a0(&_layers, _layers._size + 1);

            // Shift layers[k] -> layers[k+1] for k = oldCount-1 .. 0, one whole
            // record per step (retail: seven qword moves), re-reading the buffer
            // each iteration exactly as retail does.
            int64_t n = _layers._size - 1;
            if (n >= 1) {
                int64_t k = n;
                do {
                    Layer* base = _layers._data;
                    --k;
                    base[k + 1] = base[k];
                    --n;
                } while (n != 0);
            }
        }
    }

    // The common tail: write the new clip into layer 0.
    Layer& l0 = _layers._data[0];
    l0._group     = group;
    l0._clipIndex = animIndex;
    // Retail divides a literal 0.0 by _secondsPerTick TWICE rather than storing
    // a zero twice. Reproduced; it is two `DIVSD`/`CVTTSD2SI` pairs.
    l0._fadeLength._ticks    = SecondsToTicks(0.0);
    l0._fadeRemaining._ticks = SecondsToTicks(0.0);
    l0._speed     = 1.0f;
    l0._restarted = false;
    l0._time      = startTime;

    if (group->GetAnimation(animIndex)._instanced && instanced) {
        // CALL 0x1800fb9d0 — GetInstanceId, a non-virtual member call.
        l0._handle = AnimationManager::_singleton->GetInstanceId(
            Object::ExternalPtr<AnimationGroup>(group), animIndex, nodes);
    } else {
        l0._handle = -1;
    }
}

// ---------------------------------------------------------------------------
// ?CopyInstancedToUnique@AnimationState@Graphics@@  0x1800fd4c0 — block GC.
//
// *** `this` IS STILL NEVER READ. *** RCX is overwritten with the root transform
// four instructions in. The member is `const` and takes everything it needs as
// arguments; retail's receiver stays in the signature for the ABI.
//
// Node 0 goes through the two real calls (Quaternion::Rotate, then
// Quaternion::operator*); nodes 1..n-1 inline the rotate with the schedule
// below and call only operator*.
// ---------------------------------------------------------------------------
void AnimationState::CopyInstancedToUnique(const GraphicsMesh::NodeArray& nodes,
                                           const Math::Transform& root,
                                           InterpolatedState<Math::Transform>* interp,
                                           int handle) const {
    const AnimationManager::Instance& inst =
        AnimationManager::_singleton->_instances._data[(handle >> 8) & 0xff];
    const Math::Transform* pose = inst._states._data[handle & 0xff]._pose._data;

    // ---- node 0, through the two real calls ----
    const Math::Vector3 rot = root.rotation.Rotate(pose[0].translation);

    float ry = root.translation.y;
    float rz = root.translation.z;
    float rx = rot.x;
    ry = ry + rot.y;
    rz = rz + rot.z;
    rx = rx + root.translation.x;

    const Math::Quaternion q0 = root.rotation * pose[0].rotation;

    interp[0]._previous = interp[0]._current;
    interp[0]._current.rotation      = q0;
    interp[0]._current.translation.x = rx;
    interp[0]._current.translation.y = ry;
    interp[0]._current.translation.z = rz;

    // ---- nodes 1..n-1, rotate inlined ----
    for (int32_t i = 1; i < nodes._size; ++i) {
        const Math::Transform& posei = pose[i];

        const float vy = posei.translation.y;
        const float vx = posei.translation.x;
        const float vz = posei.translation.z;

        const int32_t parent = nodes._data[i]._parent;
        const Math::Transform& P = interp[parent]._current;

        const float px = P.rotation.x;
        const float pw = P.rotation.w;
        const float pz = P.rotation.z;
        const float py = P.rotation.y;

        const float mpx = -px;
        const float mpz = -pz;
        const float mpy = -py;

        const float tx = P.translation.x;
        const float ty = P.translation.y;
        const float tz = P.translation.z;

        // The parent rotation, rebuilt on the stack: the `this` retail hands
        // to operator*.
        const Math::Quaternion parentRot(px, py, pz, pw);

        float A = pw * vx;
        float C = py * vx;
        float B = py * 0.0f;
        float k1 = py * vz;
        float k2 = py * vy;
        float k3 = px * 0.0f;
        A = A + k3;
        float k4 = pz * vy;
        A = A + k4;
        A = A - k1;
        float k5 = pz * vx;
        float k6 = pw * vy;
        B = B - k5;
        float k7 = px * vz;
        B = B + k6;
        float k8 = pw * vz;
        B = B + k7;
        float k9 = pz * 0.0f;
        float k10 = pz * vz;
        C = C + k9;
        float k11 = px * vy;
        float k12 = px * vx;
        C = C - k11;
        C = C + k8;

        float rxi = pw * A;
        float D = pw * 0.0f;
        float u1 = C * mpy;
        D = D - k12;
        D = D - k2;
        D = D - k10;
        float u0 = D * mpx;
        rxi = rxi + u0;
        rxi = rxi + u1;
        float ryi = pw * B;
        float u2 = B * mpz;
        float rzi = pw * C;
        float u6 = B * mpx;
        rxi = rxi - u2;
        rzi = rzi + u6;
        float u3 = C * mpx;
        float u4 = D * mpy;
        float u5 = D * mpz;
        float wx = tx;
        wx = wx + rxi;
        ryi = ryi - u3;
        float u7 = A * mpz;
        ryi = ryi + u4;
        float u8 = A * mpy;
        ryi = ryi + u7;
        rzi = rzi - u8;
        rzi = rzi + u5;
        float wy = ty;
        wy = wy + ryi;
        float wz = tz;
        wz = wz + rzi;

        const Math::Quaternion q = parentRot * posei.rotation;

        InterpolatedState<Math::Transform>& slot = interp[i];
        slot._previous = slot._current;
        slot._current.rotation      = q;
        slot._current.translation.x = wx;
        slot._current.translation.y = wy;
        slot._current.translation.z = wz;
    }
}

// ---------------------------------------------------------------------------
// ?UpdateNodes@AnimationState@Graphics@@  0x1800fcc20 — ENDGAME B.3.5 block GC.
//
// Phase 1 poses the LAST layer straight into `pose`; phase 2 blends every
// earlier layer in BACKWARDS through a 48-entry scratch (retail's own size,
// which it also overruns for a mesh of more nodes); phase 3 hands out the root
// offset and rebuilds the hierarchy. `_layers._data` / `_layers._size` are
// RE-READ at every use exactly as retail reloads them.
// ---------------------------------------------------------------------------
void AnimationState::UpdateNodes(float speed,
                                 const GraphicsMesh::NodeArray& nodes,
                                 const Math::Transform& root,
                                 InterpolatedState<Math::Transform>* interp,
                                 Math::Transform* pose,
                                 Math::Vector3& rootOffset) {
    const int64_t nodeCount = nodes._size;

    // ---- phase 1: the last layer ----
    {
        const int32_t last = _layers._size - 1;
        const Layer& layer = _layers._data[last];
        const Animation& clip = layer._group->GetAnimation(layer._clipIndex);
        const int32_t handle = layer._handle;
        if (handle >= 0) {
            AnimationManager::_singleton->GetPose(handle, pose, static_cast<int>(nodeCount));
        } else {
            float scaled = speed;
            scaled = scaled * layer._speed;
            const System::Time delta = { SecondsToTicks(static_cast<double>(scaled)) };
            clip.GetPoseAtTime(layer._time, delta, pose, static_cast<int>(nodeCount));
        }
    }

    // ---- phase 2: every earlier layer, blended in backwards ----
    Math::Transform scratch[48];
    for (int32_t i = _layers._size - 2; i >= 0; --i) {
        for (int s = 0; s < 48; ++s) {
            scratch[s].rotation.x = 0.0f; scratch[s].rotation.y = 0.0f;
            scratch[s].rotation.z = 0.0f;
            scratch[s].rotation.w = 1.0f;
            scratch[s].translation.x = 0.0f; scratch[s].translation.y = 0.0f;
            scratch[s].translation.z = 0.0f;
        }

        const Layer& layer = _layers._data[i];
        const Animation& clip = layer._group->GetAnimation(layer._clipIndex);
        const int32_t handle = layer._handle;

        if (handle >= 0) {
            // Inlined GetPose, straight out of the instance pool into the
            // scratch: retail does NOT call 0x1800fb8e0 on this arm.
            const AnimationManager::Instance& inst =
                AnimationManager::_singleton->_instances._data[(handle >> 8) & 0xff];
            if (nodeCount > 0) {
                const Math::Transform* src = inst._states._data[handle & 0xff]._pose._data;
                for (int64_t k = 0; k < nodeCount; ++k)
                    scratch[k] = src[k];
            }
        } else {
            float scaled = speed;
            scaled = scaled * layer._speed;
            const System::Time delta = { SecondsToTicks(static_cast<double>(scaled)) };
            clip.GetPoseAtTime(layer._time, delta, &scratch[0],
                               static_cast<int>(nodeCount));
        }

        // t = layer[i+1].fadeRemaining / layer[i+1].fadeLength, both converted
        // to seconds as doubles first and narrowed before the DIVSS.
        const double spt = SecondsPerTick();
        const Layer* lbase = _layers._data;
        const double remSec = static_cast<double>(lbase[i + 1]._fadeRemaining._ticks) * spt;
        const double lenSec = static_cast<double>(lbase[i + 1]._fadeLength._ticks) * spt;
        float t = static_cast<float>(remSec);
        const float len = static_cast<float>(lenSec);
        t = t / len;

        if (nodeCount > 0) {
            for (int64_t k = 0; k < nodeCount; ++k) {
                Math::Transform& dst = pose[k];
                const Math::Transform& from = scratch[k];

                float bx = dst.translation.x;
                float by = dst.translation.y;
                float bz = dst.translation.z;
                bx = bx - from.translation.x;
                by = by - from.translation.y;
                bz = bz - from.translation.z;
                bx = bx * t;
                by = by * t;
                bz = bz * t;
                bx = bx + from.translation.x;
                by = by + from.translation.y;
                bz = bz + from.translation.z;

                dst.rotation = from.rotation.Lerp(dst.rotation, Math::Scalar(t));
                dst.translation.x = bx;
                dst.translation.y = by;
                dst.translation.z = bz;
            }
        }
    }

    // ---- phase 3: hand out the root offset, then build the hierarchy ----
    rootOffset.x = pose[0].translation.x;
    rootOffset.y = pose[0].translation.y;
    rootOffset.z = pose[0].translation.z;

    pose[0].rotation.x = 0.0f;
    pose[0].rotation.y = 0.0f;
    pose[0].rotation.z = 0.0f;
    pose[0].rotation.w = 1.0f;
    pose[0].translation.x = 0.0f;
    pose[0].translation.y = 0.0f;
    pose[0].translation.z = 0.0f;

    {
        // Node 0. Its translation was JUST zeroed, so MSVC folded the vector
        // into its zero register — every `* 0.0f` below is a real MULSS in the
        // instruction stream and is kept rather than folded away, because the
        // sign of a zero product still reaches the sums.
        const float qx = root.rotation.x;
        const float qy = root.rotation.y;
        const float qz = root.rotation.z;
        const float qw = root.rotation.w;
        const float mqx = -qx;
        const float mqy = -qy;
        const float mqz = -qz;

        float ka = 0.0f * qy;
        float A = 0.0f * qw;
        float C = 0.0f * qy;
        float B = qy * 0.0f;
        float kb = qx * 0.0f;
        float D = qw * 0.0f;
        A = A + kb;
        float kc = 0.0f * qz;
        A = A + kc;
        A = A - ka;
        float kd = 0.0f * qz;
        float ke = 0.0f * qw;
        B = B - kd;
        float kf = 0.0f * qx;
        float rx = A;
        B = B + ke;
        rx = rx * qw;
        B = B + kf;
        float kg = 0.0f * qw;
        float kh = qz * 0.0f;
        C = C + kh;
        float ki = 0.0f * qx;
        C = C - ki;
        C = C + kg;
        float kj = 0.0f * qx;
        float kk = 0.0f * qy;
        D = D - kj;
        float kl = 0.0f * qz;
        D = D - kk;
        float u1 = C * mqy;
        D = D - kl;
        float u0 = D * mqx;
        rx = rx + u0;
        rx = rx + u1;
        float u2 = B * mqz;
        float ry = B;
        ry = ry * qw;
        float u3 = D * mqy;
        float u4 = D * mqz;
        rx = rx - u2;
        rx = rx + root.translation.x;
        float u5 = C * mqx;
        float rz = C;
        rz = rz * qw;
        float u6 = B * mqx;
        ry = ry - u5;
        rz = rz + u6;
        ry = ry + u3;
        float u7 = A * mqz;
        float u8 = A * mqy;
        rz = rz - u8;
        ry = ry + u7;
        ry = ry + root.translation.y;
        rz = rz + u4;
        rz = rz + root.translation.z;

        const Math::Quaternion q = root.rotation * pose[0].rotation;

        interp[0]._previous = interp[0]._current;
        interp[0]._current.rotation      = q;
        interp[0]._current.translation.x = rx;
        interp[0]._current.translation.y = ry;
        interp[0]._current.translation.z = rz;
    }

    for (int64_t k = 1; k < nodeCount; ++k) {
        const Math::Transform& posek = pose[k];

        const float vx = posek.translation.x;
        const float vy = posek.translation.y;
        const float vz = posek.translation.z;

        const int32_t parent = nodes._data[k]._parent;
        const Math::Transform& P = interp[parent]._current;

        const float py = P.rotation.y;
        const float pz = P.rotation.z;
        const float px = P.rotation.x;
        const float pw = P.rotation.w;
        const float tx = P.translation.x;
        const float ty = P.translation.y;
        const float tz = P.translation.z;

        const float mpy = -py;
        const float mpx = -px;
        const float mpz = -pz;

        const Math::Quaternion parentRot(px, py, pz, pw);

        float k1 = vz * py;
        float B = py * 0.0f;
        float A = vx * pw;
        float k2 = px * 0.0f;
        float C = vx * py;
        A = A + k2;
        float k3 = vy * pz;
        A = A + k3;
        float k4 = vx * px;
        float k5 = vx * pz;
        A = A - k1;
        B = B - k5;
        float k6 = vy * pw;
        float k7 = vz * px;
        B = B + k6;
        B = B + k7;
        float k8 = vz * pw;
        float k9 = vz * pz;
        float k10 = pz * 0.0f;
        C = C + k10;
        float k11 = vy * py;
        float k12 = vy * px;
        float rx = A;
        rx = rx * pw;
        C = C - k12;
        C = C + k8;
        float D = pw * 0.0f;
        float u1 = C * mpy;
        D = D - k4;
        D = D - k11;
        D = D - k9;
        float u0 = D * mpx;
        rx = rx + u0;
        rx = rx + u1;
        float u2 = B * mpz;
        float ry = B;
        ry = ry * pw;
        float u3 = D * mpy;
        float u4 = D * mpz;
        rx = rx - u2;
        float u5 = C * mpx;
        rx = rx + tx;
        float rz = C;
        rz = rz * pw;
        float u6 = B * mpx;
        ry = ry - u5;
        rz = rz + u6;
        ry = ry + u3;
        float u7 = A * mpz;
        float u8 = A * mpy;
        rz = rz - u8;
        ry = ry + u7;
        ry = ry + ty;
        rz = rz + u4;
        rz = rz + tz;

        const Math::Quaternion q = parentRot * posek.rotation;

        InterpolatedState<Math::Transform>& slot = interp[k];
        slot._previous = slot._current;
        slot._current.rotation      = q;
        slot._current.translation.x = rx;
        slot._current.translation.y = ry;
        slot._current.translation.z = rz;
    }
}

// ---------------------------------------------------------------------------
// ?GetBones@MeshInstance@Graphics@@  0x180100070 -- ENDGAME B.3.5 block GC.
// The +0xd8 byte (`_animation._instanced`) is tested three separate times; the
// identity-versus-inverse root choice and MSVC's hoisted `q * 0.0f` seeds are
// retail's.
// ---------------------------------------------------------------------------
int MeshInstance::GetBones(float t, const Math::Transform& rootRef,
                           Math::Matrix* out) const {
    const GraphicsMesh* mesh = _mesh.Get();
    const GraphicsMesh::NodeArray& meshNodes = mesh->GetNodes();
    const int32_t boneCount = meshNodes._size;

    // The body writes only the caller's matrix buffer and returns the bone count;
    // nothing it reads is modified.

    Math::Transform rootT;
    if (_animation._instanced) {
        rootT.rotation.x = 0.0f; rootT.rotation.y = 0.0f; rootT.rotation.z = 0.0f;
        rootT.rotation.w = 1.0f;
        rootT.translation.x = 0.0f; rootT.translation.y = 0.0f; rootT.translation.z = 0.0f;
    } else {
        rootT = rootRef.Inverse();
    }

    const float rqx = rootT.rotation.x;
    const float rqy = rootT.rotation.y;
    const float rqz = rootT.rotation.z;
    const float rqw = rootT.rotation.w;
    const float rtx = rootT.translation.x;
    const float rty = rootT.translation.y;
    const float rtz = rootT.translation.z;

    // The root rotation, rebuilt as the `this` retail hands to operator*.
    const Math::Quaternion rootRot(rqx, rqy, rqz, rqw);

    const InterpolatedState<Math::Transform>* interp;
    // EXT SEAM (NOT RETAIL, docs/android/P13_OVERLAP.md §2.3): a frozen copy's own record.
    const InstancedAnimationState* const frozen =
        (_animation._instanced && _extInstancedState != nullptr) ? _extInstancedState(*this) : nullptr;
    if (frozen != nullptr) {
        interp = frozen->_nodes._data;
    } else if (_animation._instanced) {
        const int32_t handle =
            _animation._layers._data[_animation._layers._size - 1]._handle;
        const AnimationManager::Instance& inst =
            AnimationManager::_singleton->_instances._data[(handle >> 8) & 0xff];
        interp = inst._states._data[handle & 0xff]._nodes._data;
    } else {
        interp = _nodes._data;
    }

    if (boneCount <= 0) return boneCount;

    const float rqx0 = rqx * 0.0f;
    const float rqy0 = rqy * 0.0f;
    const float rqz0 = rqz * 0.0f;
    const float rqw0 = rqw * 0.0f;
    const float mrqx = -rqx;
    const float mrqy = -rqy;
    const float mrqz = -rqz;

    for (int64_t k = 0; k < boneCount; ++k) {
        const InterpolatedState<Math::Transform>& ip = interp[k];

        // Lerp the node's two stored transforms by `t`.
        float sx = ip._previous.translation.x;
        float fx = ip._current.translation.x;
        float sy = ip._previous.translation.y;
        float fy = ip._current.translation.y;
        float sz = ip._previous.translation.z;
        float fz = ip._current.translation.z;
        fx = fx - sx;
        fy = fy - sy;
        fz = fz - sz;
        fx = fx * t;
        fy = fy * t;
        fz = fz * t;
        sx = sx + fx;
        sy = sy + fy;
        sz = sz + fz;

        const Math::Quaternion lq = ip._previous.rotation.Lerp(ip._current.rotation,
                                                               Math::Scalar(t));

        const Math::Transform& bind = meshNodes._data[k]._bind;

        // ---- stage 1: rotate the bind translation by the lerped rotation ----
        const float lqx = lq.x;
        const float lqw = lq.w;
        const float lqz = lq.z;
        const float lqy = lq.y;
        const float bvx = bind.translation.x;
        const float bvy = bind.translation.y;
        const float bvz = bind.translation.z;
        const float mlqx = -lqx;
        const float mlqy = -lqy;
        const float mlqz = -lqz;

        const float ka = lqx * 0.0f;
        float B = lqy * 0.0f;
        float A = bvx * lqw;
        const float kb = bvz * lqy;
        float C = bvx * lqy;
        A = A + ka;
        const float kc = bvy * lqz;
        A = A + kc;
        const float kd = bvx * lqz;
        A = A - kb;
        const float ke = bvy * lqw;
        B = B - kd;
        const float kf = bvz * lqx;
        B = B + ke;
        const float kg = bvz * lqw;
        B = B + kf;
        const float kh = lqz * 0.0f;
        float D = lqw * 0.0f;
        C = C + kh;
        const float ki = bvy * lqx;
        C = C - ki;
        C = C + kg;
        const float kj = bvy * lqy;
        const float kk = bvz * lqz;
        float rx = A * lqw;
        const float u1 = C * mlqy;
        float ry = B * lqw;
        const float kl = bvx * lqx;
        D = D - kl;
        D = D - kj;
        D = D - kk;
        const float u0 = D * mlqx;
        rx = u0 + rx;
        const float u6 = B * mlqx;
        const float u2 = B * mlqz;
        rx = rx + u1;
        rx = rx - u2;
        const float u3 = D * mlqy;
        const float u4 = D * mlqz;
        const float u5 = C * mlqx;
        rx = rx + sx;
        float rz = C * lqw;
        ry = ry - u5;
        rz = u6 + rz;
        const float u7 = A * mlqz;
        const float u8 = A * mlqy;
        ry = ry + u3;
        rz = rz - u8;
        rz = rz + u4;
        ry = ry + u7;
        rz = rz + sz;
        ry = ry + sy;

        const Math::Quaternion lerpedRot(lqx, lqy, lqz, lqw);
        const Math::Quaternion composedQ = lerpedRot * bind.rotation;

        // ---- stage 2: the same shape again against the root rotation. The
        // four accumulators are SEEDED with the hoisted `rq * 0.0f` products,
        // which is why those are computed once outside the loop. ----
        float A2 = rx * rqw;
        A2 = A2 + rqx0;
        float C2 = rx * rqy;
        float B2 = rqy0;
        C2 = C2 + rqz0;
        const float m0 = ry * rqz;
        A2 = A2 + m0;
        const float m1 = rz * rqy;
        const float m2 = rx * rqz;
        A2 = A2 - m1;
        const float m3 = ry * rqw;
        B2 = B2 - m2;
        const float m4 = rz * rqx;
        B2 = B2 + m3;
        B2 = B2 + m4;
        const float m5 = ry * rqx;
        float D2 = rqw0;
        C2 = C2 - m5;
        const float m6 = rx * rqx;
        const float m7 = ry * rqy;
        D2 = D2 - m6;
        float Rx = A2 * rqw;
        const float m8 = rz * rqw;
        float Ry = B2 * rqw;
        C2 = C2 + m8;
        D2 = D2 - m7;
        const float m9 = rz * rqz;
        const float v1 = C2 * mrqy;
        D2 = D2 - m9;
        const float v0 = D2 * mrqx;
        Rx = v0 + Rx;
        const float v2 = B2 * mrqz;
        const float v6 = B2 * mrqx;
        Rx = Rx + v1;
        const float v3 = D2 * mrqy;
        Rx = Rx - v2;
        const float v4 = D2 * mrqz;
        const float v5 = C2 * mrqx;
        Rx = Rx + rtx;
        float Rz = C2 * rqw;
        Ry = Ry - v5;
        Rz = v6 + Rz;
        const float v7 = A2 * mrqz;
        const float v8 = A2 * mrqy;
        Ry = Ry + v3;
        Rz = Rz - v8;
        Rz = Rz + v4;
        Ry = Ry + v7;
        Ry = Ry + rty;
        Rz = Rz + rtz;

        Math::Transform finalXform;
        finalXform.rotation      = rootRot * composedQ;
        finalXform.translation.x = Rx;
        finalXform.translation.y = Ry;
        finalXform.translation.z = Rz;
        out[k] = finalXform.AsMatrix();
    }

    return boneCount;
}

// ---------------------------------------------------------------------------
// ?GetInstanceId@AnimationManager@Graphics@@  0x1800fb9d0 -- block GC.
//
// NOT DIFFABLE: the body allocates instances, bakes poses into them and moves
// reference counts. A second run against the DLL would create a second
// instance and double every count.
//
// One search pass does two jobs at once (remember the first blank slot, stop
// at an exact match); a miss either reuses the blank slot or grows the table
// by one through the three-argument SetSize; the new instance's per-frame
// states are baked by PlayAnimation + two zero-length Updates each; and the
// low byte of the packed handle is the frame with the smallest playback time
// (a sweep seeded with 10000.0f). `group` is retail's `ExternalPtr` BY VALUE:
// 8 bytes, trivially copyable, so it arrives in a register.
// ---------------------------------------------------------------------------
int32_t AnimationManager::GetInstanceId(Object::ExternalPtr<AnimationGroup> groupHandle,
                                        int animIndex,
                                        const GraphicsMesh::NodeArray& nodes) {
    AnimationGroup* const group = groupHandle.Get();

    int32_t freeSlot = -1;
    int32_t idx = 0;
    bool found = false;

    const int32_t count = _instances._size;
    if (count > 0) {
        const Instance* p = _instances._data;
        for (;;) {
            AnimationGroup* g = p->_group;
            if (g == nullptr && freeSlot < 0) freeSlot = idx;   // CMOVS EAX,ESI
            if (g == group && p->_animIndex == animIndex) {
                found = true;
                break;
            }
            ++idx;
            ++p;
            if (idx >= count) break;
        }
    }

    if (!found) {
        // A zeroed Instance on the stack, built the same way in both arms: the
        // group and clip index filled in, the reference count 0 and both Array
        // headers null. It is the FILL for the grow arm and the SOURCE of the
        // two empty arrays for the reuse arm.
        //
        // B.4 Y RT: a real local. Its implicit ctor is the two Array ctors'
        // zero triples (retail's header zero stores); its two Arrays are freed
        // below by retail's own two explicit `~Array` calls (0x18005bbb0 /
        // 0x180014090), which zero each header, so the implicit member
        // ~Arrays at scope exit are no-ops (no element destroy, MemFree(null)).
        Instance tmpInstance;
        Instance* const tmp = &tmpInstance;
        tmp->_group     = group;
        tmp->_animIndex = animIndex;
        tmp->_refCount  = 0;

        if (count > 0 && freeSlot >= 0) {
            idx = freeSlot;
            Instance* inst = &_instances._data[freeSlot];
            inst->_group     = group;
            inst->_animIndex = animIndex;
            inst->_refCount  = 0;
            rtcoll::Rt_ArrayAssign_180013960(&inst->_nodes, &tmp->_nodes);      // CALL 0x180013960
            rtcoll::Rt_ArrayAssign_18005b130(&inst->_states, &tmp->_states);    // CALL 0x18005b130
        } else {
            idx = count;
            rtcoll::Rt_SetSize_18005be30(&_instances, _instances._size + 1, tmp);   // CALL 0x18005be30
        }
        rtcoll::Rt_ArrayDtor_18005bbb0(&tmp->_states);                           // CALL 0x18005bbb0
        rtcoll::Rt_ArrayDtor_180014090(&tmp->_nodes);                           // CALL 0x180014090
    }

    if (!found) {
        Instance* inst = &_instances._data[idx];

        rtcoll::Rt_ArrayAssign_180013960(&inst->_nodes, &nodes);                // CALL 0x180013960

        const Animation& clip = group->GetAnimation(animIndex);

        // *** THE STATE COUNT IS clip+0x28, NOT clip+0x50. *** +0x50 is the
        // clip's keyframe count; +0x28 (`_instanceCount`, `?GetInstanceCount@`)
        // is how many baked samples an instanced clip carries, and it is the
        // bound of every loop below and of the final smallest-time sweep.
        rtcoll::Rt_SetSize_18005b1e0(&inst->_states, clip._instanceCount);

        const int32_t frames = clip._instanceCount;

        // step = clipDurationSeconds * (1.0f / frames). Written as the three
        // SSE ops retail emits — a CVTPD2PS of the double product, a reciprocal
        // by DIVSS, then one MULSS — not as `dur / frames`.
        const double durSec = static_cast<double>(clip._duration) * SecondsPerTick();
        float dur = static_cast<float>(durSec);
        const float fFrames = static_cast<float>(frames);
        float inv = 1.0f;
        inv = inv / fFrames;
        float step = dur;
        step = step * inv;

        float t = 0.0f;
        int32_t frame = 0;

        if (frames > 0) {
            int64_t stateIdx = 0;
            for (;;) {
                InstancedAnimationState* st = &inst->_states._data[stateIdx];

                rtcoll::Rt_SetSize_18005ac50(&st->_nodes, nodes._size);      // CALL 0x18005ac50
                InterpolatedState<Math::Transform>* ip = st->_nodes._data;
                // Node 0 is the identity transform, written field by field, and
                // then copied over its own second half.
                ip[0]._current.rotation.x = 0.0f;
                ip[0]._current.rotation.y = 0.0f;
                ip[0]._current.rotation.z = 0.0f;
                ip[0]._current.rotation.w = 1.0f;
                ip[0]._current.translation.x = 0.0f;
                ip[0]._current.translation.y = 0.0f;
                ip[0]._current.translation.z = 0.0f;
                ip[0]._previous = ip[0]._current;

                rtcoll::RtColl_ArraySetSize_180059140(&st->_pose, nodes._size);        // CALL 0x180059140

                if (st->_nodes._size > 1) {
                    int32_t j = 1;
                    for (;;) {
                        const GraphicsMesh::Node& nd = nodes._data[j];
                        InterpolatedState<Math::Transform>* ipArr = st->_nodes._data;

                        const int32_t parent = nd._parent;
                        const Math::Transform& P = ipArr[parent]._current;

                        const float px = P.rotation.x;
                        const float py = P.rotation.y;
                        const float pz = P.rotation.z;
                        const float pw = P.rotation.w;
                        const float tx = P.translation.x;
                        const float ty = P.translation.y;
                        const float tz = P.translation.z;

                        // The parent rotation, rebuilt on the stack because
                        // that is the `this` retail hands to operator*.
                        const Math::Quaternion parentRot(px, py, pz, pw);

                        const float vx = nd._local.translation.x;
                        const float vy = nd._local.translation.y;
                        const float vz = nd._local.translation.z;

                        const float mpx = -px;   // XORPS with four -0.0f
                        const float mpy = -py;
                        const float mpz = -pz;

                        // q * v, with v.w == 0 supplied as a literal 0.0f
                        // wherever retail multiplies by its zero register.
                        float A = pw * vx;
                        float s0 = px * 0.0f;
                        A = A + s0;
                        float s1 = pz * vy;
                        A = A + s1;
                        float s2 = py * vz;
                        A = A - s2;

                        float B = py * 0.0f;
                        float s3 = pz * vx;
                        B = B - s3;
                        float s4 = pw * vy;
                        B = B + s4;
                        float s5 = px * vz;
                        B = B + s5;

                        float C = py * vx;
                        float s6 = pz * 0.0f;
                        C = C + s6;
                        float s7 = px * vy;
                        C = C - s7;
                        float s8 = pw * vz;
                        C = C + s8;

                        float D = pw * 0.0f;
                        float s9 = px * vx;
                        D = D - s9;
                        float s10 = py * vy;
                        D = D - s10;
                        float s11 = pz * vz;
                        D = D - s11;

                        // (A,B,C,D) * conj(q)
                        float rx = pw * A;
                        float u0 = D * mpx;
                        rx = rx + u0;
                        float u1 = C * mpy;
                        rx = rx + u1;
                        float u2 = B * mpz;
                        rx = rx - u2;

                        float ry = pw * B;
                        float u3 = C * mpx;
                        ry = ry - u3;
                        float u4 = D * mpy;
                        ry = ry + u4;
                        float u5 = A * mpz;
                        ry = ry + u5;

                        float rz = pw * C;
                        float u6 = B * mpx;
                        rz = rz + u6;
                        float u7 = A * mpy;
                        rz = rz - u7;
                        float u8 = D * mpz;
                        rz = rz + u8;

                        float wx = tx;
                        wx = wx + rx;
                        float wy = ty;
                        wy = wy + ry;
                        float wz = tz;
                        wz = wz + rz;

                        const Math::Quaternion q = parentRot * nd._local.rotation;

                        InterpolatedState<Math::Transform>& slot = ipArr[j];
                        slot._current.rotation      = q;
                        slot._current.translation.x = wx;
                        slot._current.translation.y = wy;
                        slot._current.translation.z = wz;
                        slot._previous = slot._current;

                        ++j;
                        if (j >= st->_nodes._size) break;
                    }
                }

                // Seed the state: play the clip at `t` seconds with no fade,
                // then run two zero-length Updates so both halves of every
                // InterpolatedState hold the settled pose.
                const System::Time zero = { SecondsToTicks(0.0) };
                const System::Time at   = { SecondsToTicks(static_cast<double>(t)) };
                // Graphics::AnimationState::PlayAnimation (0x1800fdf50).
                st->_state.PlayAnimation(Object::ExternalPtr<AnimationGroup>(group), animIndex,
                                  nodes, at, zero, 1.0f, false);

                for (int rep = 0; rep < 2; ++rep) {
                    const System::Time z = { SecondsToTicks(0.0) };
                    const Math::Transform ident;   // the identity: Transform()
                    // Through the door: it counts the call for the [GFXBIG27]
                    // line before it calls the member.
                    RtGraphics_AnimationState_Update(&st->_state, &z, &inst->_nodes, &ident,
                                                     st->_nodes._data, st->_pose._data,
                                                     &st->_rootOffset, false, false);
                }

                ++frame;
                ++stateIdx;
                t = t + step;
                if (frame >= clip._instanceCount) break;
            }
        }
    }

    // ---- reference count and the smallest-playback-time sweep ----
    ++_instances._data[idx]._refCount;

    float best = 10000.0f;          // DAT_180161734
    int32_t bestIdx = 0;
    int32_t j = 0;
    const Instance& inst = _instances._data[idx];
    if (inst._states._size > 0) {
        const int32_t n = inst._states._size;
        const InstancedAnimationState* sp = inst._states._data;
        const double spt = SecondsPerTick();
        do {
            float v;
            if (sp->_state._layers._size == 0) {
                v = 0.0f;
            } else {
                v = static_cast<float>(static_cast<double>(sp->_state._layers._data[0]._time._ticks) * spt);
            }
            // COMISS/JBE: an unordered compare skips, so a NaN never wins.
            if (best > v) { bestIdx = j; best = v; }
            ++j;
            ++sp;
        } while (j < n);
    }

    return (idx << 8) | bestIdx;
}

}  // namespace Graphics
