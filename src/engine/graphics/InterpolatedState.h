#pragma once
// ============================================================================
// src/engine/graphics/InterpolatedState.h — Graphics::InterpolatedState<T>
// ============================================================================
//
// Authored by ENDGAME B.3.5's closing pass. Three caller batches reported
// `?Lerp@?$InterpolatedState@VTransform@Math@@@Graphics@@…` as "NO SUCH CLASS
// EXISTS IN src/engine/graphics AT ALL — nothing to convert to", and left two
// call sites on the door. The class does exist in retail; it had simply never been
// authored. This header is the minimum that makes those two call sites member
// calls, and it says plainly what it does NOT settle.
//
// ----------------------------------------------------------------------------
// THE LAYOUT, READ FROM RETAIL BYTES THREE WAYS (Runtime-x64.dll, 15290 fns)
// ----------------------------------------------------------------------------
// `InterpolatedState<T>` is TWO Ts back to back: the CURRENT value at +0x00 and
// the PREVIOUS value at +sizeof(T). Non-polymorphic — no ctor stamps a vtable.
//
//  1. `??0?$InterpolatedState@VTransform@Math@@@Graphics@@QEAA@AEBVTransform@
//     Math@@@Z` (0x18001f800), the seeding ctor: thirty instructions that copy the
//     argument's seven dwords to +0x00..+0x18 and then THE SAME seven dwords again
//     to +0x1c..+0x34. One value, stored twice — a freshly seeded state has no
//     history, so current == previous.
//  2. `??0?$InterpolatedState@VTransform@Math@@@Graphics@@QEAA@XZ` (0x18005b780),
//     the default ctor: zeroes both halves and writes 1.0f at +0x0c AND at +0x28
//     (== 0x0c + 0x1c) — the quaternion `w` of each half, i.e. two identity
//     Transforms. Its last store is `MOV qword [RCX+0x30]`, so the object ends at
//     +0x37 and sizeof is 0x38.
//  3. `?Lerp@…` (0x18001aff0) itself, eleven instructions, below.
//
// ----------------------------------------------------------------------------
// `Lerp`, INSTRUCTION FOR INSTRUCTION
// ----------------------------------------------------------------------------
//   18001aff0: PUSH RBX ; SUB RSP,0x20
//   18001aff6: MOV R9,R8        ; R9  <- the Scalar&   (4th arg of the callee)
//   18001aff9: MOV R8,RCX       ; R8  <- this + 0x00   (3rd arg: the OTHER)
//   18001affc: ADD RCX,0x1c     ; RCX <- this + 0x1c   (the RECEIVER)
//   18001b000: MOV RBX,RDX      ; keep the hidden return slot
//   18001b003: CALL 0x18001d690 ; ?Lerp@Transform@Math@@QEBA?AV12@AEBV12@AEBVScalar@2@@Z
//   18001b008: MOV RAX,RBX      ; return the retslot, as every retslot body does
// So it is exactly `_previous.Lerp(_current, t)` — the receiver is the +0x1c half
// and the argument is the +0x00 half. That fixes WHICH half is which beyond doubt:
// a lerp runs FROM the previous value TOWARD the current one, so +0x00 is
// `_current` and +0x1c is `_previous`. `src/engine/graphics/mesh.cpp`'s
// GetInstanceTransform note independently read the same pair as "cur at +0x00 and
// prev at +0x1c, interpolated from prev towards cur", which is a second witness
// written by a different lane from a different body.
//
// It is a HIDDEN-RETSLOT export (`?AVTransform@Math@@` after `QEBA`); it is not a
// vtable slot, so the member returns the prvalue in C++.
//
// ----------------------------------------------------------------------------
// NOT SETTLED BY THIS HEADER — do not read more into it than it says
// ----------------------------------------------------------------------------
//  * Retail instantiates this template FIVE times: `float` (`@M@`), `double`
//    (`@N@`), `Volume::Box`, `Math::Transform` and `Math::Vector3`. Only the
//    `Math::Transform` one has a caller in this tree, so only its `Lerp` is
//    DEFINED. `Lerp` also exists for `Volume::Box` (0x18005dad0), `Vector3`
//    (0x180062400), `float` (0x1800153e0) and `double` (0x180015440); each would
//    be the same two-liner over that T's own `Lerp`, and each is left to whichever
//    block acquires a caller for it.
//  * THERE IS A SECOND NAME, `Graphics::InterpolatedStateT<T>`, with the SAME
//    0x38 layout and a byte-identical seeding ctor (0x18001f8c0 vs 0x18001f800),
//    and the exported member set SPLITS between the two names: `Lerp` and
//    `operator=` belong to `InterpolatedState`, while `Next` (0x18001b0b0),
//    `Current` (0x18001b110), `Snap` (0x18005a9c0), `Serialize` (0x18005c7a0),
//    `operator T()` (0x18001b020) and `operator==` (0x18001b050) belong to
//    `InterpolatedStateT`. That split strongly suggests one derives from the
//    other with no added state — but TWO IDENTICAL CONSTRUCTORS ARE NOT A PROOF OF
//    INHERITANCE, and B35_RULES 2.1 requires a PROVED parent before a class
//    declares one. So `InterpolatedStateT` is deliberately NOT declared here and
//    this template deliberately has NO base. Settling the relationship, and
//    declaring the other six members on the right side of it, is a class block's
//    job, not a closing pass's.
//  * `AnimationManager::GetTransforms` (0x18005a6a0 / 0x18005a700) returns
//    `InterpolatedState<Transform>*`, and `AnimationState::{Update, UpdateNodes,
//    CopyInstancedToUnique, GetInstanceTransforms}` take or return the same
//    pointer. Those signatures still spell it `void*` / `uint8_t*` where they are
//    ported; retyping them belongs with the block that settles the hierarchy.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "../math/Math.h"   // Math::Transform, Math::Scalar

namespace Graphics {

// ----------------------------------------------------------------------------
// Graphics::InterpolatedState<T> — a value and its previous value.
//
// NO BASE, on purpose (see the banner). Trivially copyable, non-polymorphic, and
// laid out exactly as retail's two ctors write it.
// ----------------------------------------------------------------------------
template <typename T>
class InterpolatedState {
public:
    T _current;     // +0x00           the value as of this frame
    T _previous;    // +sizeof(T)      the value as of the previous frame

    // `??0?$InterpolatedState@…@Graphics@@QEAA@XZ`  — both halves default-built.
    InterpolatedState() = default;
    // `??0?$InterpolatedState@…@Graphics@@QEAA@AEBV01@@Z` / `@AEBVTransform@Math@@@Z`
    // — ONE value stored TWICE (0x18001f800). A freshly seeded state has no
    // history, so `_previous == _current`.
    explicit InterpolatedState(const T& v) : _current(v), _previous(v) {}
    InterpolatedState(const InterpolatedState&) = default;
    InterpolatedState& operator=(const InterpolatedState&) = default;

    // `?Lerp@?$InterpolatedState@VTransform@Math@@@Graphics@@QEBA?AVTransform@
    //  Math@@AEBVScalar@4@@Z`  0x18001aff0 — hidden retslot.
    // DEFINED for `Math::Transform` only (see the banner); any other instantiation
    // link-errors on purpose, which is the proof a caller batch is owed.
    T Lerp(const Math::Scalar& t) const;
};

// ---------------------------------------------------------------------------
// Math::Transform Graphics::InterpolatedState<Math::Transform>::Lerp(const Scalar&)
//   retail 0x18001aff0, ELEVEN instructions, transcribed:
//
//     MOV R9,R8        ; the Scalar& becomes the callee's 4th argument
//     MOV R8,RCX       ; this + 0x00  (_current)  becomes the callee's `other`
//     ADD RCX,0x1c     ; this + 0x1c  (_previous) becomes the callee's `this`
//     MOV RBX,RDX      ; keep the hidden return slot
//     CALL 0x18001d690 ; ?Lerp@Transform@Math@@QEBA?AV12@AEBV12@AEBVScalar@2@@Z
//     MOV RAX,RBX      ; return the retslot
//
// So: `_previous.Lerp(_current, t)`. Nothing else — no clamp on `t`, no early-out
// when the two halves are equal, no normalisation beyond whatever
// `Math::Transform::Lerp` itself does. The DIRECTION is the whole content of the
// function, and it is what lets the two fields be named at all: a lerp runs from
// the PREVIOUS value toward the CURRENT one.
//
// It is defined HERE, `inline`, rather than in a .cpp, for two reasons: a
// template's members belong beside the template, and the explicit specialisation
// has to be visible before anything triggers the implicit instantiation (a definition
// that came after a call would be rejected as "specialization after instantiation").
// Defined for `Math::Transform` ONLY, deliberately — see the banner for retail's
// other four instantiations.
template <>
inline Math::Transform
InterpolatedState<Math::Transform>::Lerp(const Math::Scalar& t) const {
    return _previous.Lerp(_current, t);
}

// The `Math::Transform` instantiation is the one retail's animation path uses and
// the only one with a caller here. Assert its layout against the three byte reads
// in the banner; a change in `Math::Transform` that moved these would otherwise
// walk `AnimationManager::GetTransforms`' array at the wrong stride.
static_assert(sizeof(InterpolatedState<Math::Transform>) == 0x38,
              "Graphics::InterpolatedState<Math::Transform> is 0x38 bytes -- the "
              "default ctor at 0x18005b780 ends with MOV qword [RCX+0x30]");
static_assert(alignof(InterpolatedState<Math::Transform>) == alignof(Math::Transform),
              "InterpolatedState adds no alignment of its own");
static_assert(offsetof(InterpolatedState<Math::Transform>, _current) == 0x00,
              "InterpolatedState<Transform>::_current @ +0x00 -- Lerp's R8");
static_assert(offsetof(InterpolatedState<Math::Transform>, _previous) == 0x1c,
              "InterpolatedState<Transform>::_previous @ +0x1c -- Lerp's ADD RCX,0x1c");
static_assert(std::is_trivially_copyable<InterpolatedState<Math::Transform>>::value,
              "retail copies it with seven-dword field moves; it must stay trivial");

}  // namespace Graphics
