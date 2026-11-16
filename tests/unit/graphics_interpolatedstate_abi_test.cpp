// ============================================================================
// tests/unit/graphics_interpolatedstate_abi_test.cpp
//   ENDGAME B.3 step 5, closing pass — Graphics::InterpolatedState<T>
//
// The class three caller batches reported as "NO SUCH CLASS EXISTS IN
// src/engine/graphics at all". It exists in retail; it had never been
// authored. `src/engine/graphics/InterpolatedState.h` now declares it and
// defines, inline, the one member with a caller (`Lerp` for `Math::Transform`).
// This test is the ABI proof that licenses both.
//
// WHAT IS BEING PROVED, and why each part is a test and not a comment:
//
// PART A — LAYOUT. `InterpolatedState<T>` is two Ts back to back, `_current` at
//   +0x00 and `_previous` at +sizeof(T). Read from retail three ways (the
//   header's banner quotes all three):
//     * the seeding ctor 0x18001f800 copies the argument's seven dwords to
//       +0x00..+0x18 and AGAIN to +0x1c..+0x34;
//     * the default ctor 0x18005b780 writes 1.0f at +0x0c AND at +0x28, and its
//       last store is `MOV qword [RCX+0x30]` -> the object ends at +0x37;
//     * `Lerp` itself does `ADD RCX,0x1c`.
//   The header static_asserts these, so PART A would fail the BUILD rather than
//   the run. It is still measured here at RUNTIME, on a real object, because a
//   static_assert proves what the compiler computed and a runtime read proves
//   what the compiler EMITTED -- and because the 0x38 stride is what
//   `Array<Graphics::InterpolatedState<Math::Transform>>` (kSpec_InterpTransform in
//   src/engine/collection/array_graphics_anim.cpp, with exactly 0x38) walks.
//   If those two ever disagree the animation pool walks off its own elements.
//
// PART B — THE DIRECTION OF THE LERP. This is the part that cannot be a
//   static_assert and is the whole content of the ported body. Retail's eleven
//   instructions are:
//       MOV R9,R8        ; the Scalar&
//       MOV R8,RCX       ; this + 0x00  -> the callee's `other`
//       ADD RCX,0x1c     ; this + 0x1c  -> the callee's `this`
//       CALL 0x18001d690 ; Math::Transform::Lerp
//   i.e. `_previous.Lerp(_current, t)`. Getting it BACKWARDS is silent: both
//   directions typecheck, both return a Transform, and at t == 0.5 the two
//   answers are IDENTICAL. So the test pins the two ENDPOINTS, where the
//   directions disagree: at t == 0 the result must be `_previous`, and at
//   t == 1 it must be `_current`. A reversed implementation passes every
//   midpoint check and fails both of these.
//
// PART C — THE SEEDING CTOR'S "no history" property: a state built from one
//   value must lerp to that value at EVERY t, because both halves hold it.
//
// PART D — TRIVIALITY. Retail copies these with seven-dword field moves and
//   `Array<InterpolatedState<Transform>>`'s element spec has no element destroy
//   (its init stores two identity transforms). A non-trivial member would make GCC emit a second
//   element lifecycle beside the ported one.
//
// NOTE ON SCOPE: only the `Math::Transform` instantiation's `Lerp` is defined
// (the header says why), so only it is exercised. The template's LAYOUT is
// checked for a second T as well, to prove the +0x00 / +sizeof(T) rule is the
// template's and not a coincidence of Transform's 0x1c.
// ============================================================================

#include "unit.h"

#include "graphics/InterpolatedState.h"
#include "math/Math.h"

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <type_traits>

namespace {

bool NearF(float a, float b) { return std::fabs(a - b) <= 1e-5f; }

// The seven floats of a Transform, by name: rotation x/y/z/w (+0x00..+0x0c),
// then translation x/y/z (+0x10..+0x18).
bool SameTransform(const Math::Transform& a, const Math::Transform& b) {
    return NearF(a.rotation.x, b.rotation.x) && NearF(a.rotation.y, b.rotation.y) &&
           NearF(a.rotation.z, b.rotation.z) && NearF(a.rotation.w, b.rotation.w) &&
           NearF(a.translation.x, b.translation.x) &&
           NearF(a.translation.y, b.translation.y) &&
           NearF(a.translation.z, b.translation.z);
}

// A Transform with recognisable, distinct components in both halves, so a
// direction mistake cannot hide behind a symmetric value.
Math::Transform MakeTransform(float qx, float tx) {
    Math::Transform t;
    t.rotation.x = qx; t.rotation.y = 0.0f; t.rotation.z = 0.0f;
    t.rotation.w = std::sqrt(1.0f - qx * qx);                                 // unit quat
    t.translation.x = tx; t.translation.y = tx * 2.0f; t.translation.z = tx * 3.0f;
    return t;
}

}  // namespace

// ---------------------------------------------------------------- PART A ----

UNIT_TEST(graphics_interpolatedstate_transform_layout) {
    using IS = Graphics::InterpolatedState<Math::Transform>;

    // InterpolatedState adds no alignment of its own.
    CHECK_EQ(alignof(IS), alignof(Math::Transform));

    // 0x38 is also the element stride the
    // `Array<Graphics::InterpolatedState<Math::Transform>>` spec walks
    // (src/engine/collection/array_graphics_anim.cpp); InterpolatedState.h's static_assert
    // pins the size: if it ever changed, that array would read off its elements.

    // Not polymorphic -- no ctor in retail stamps a vtable, and a vptr would
    // move every field by 8.
    CHECK(!std::is_polymorphic<IS>::value);

    // And the two halves really are AT those addresses in an emitted object,
    // not merely where the compiler says they are.
    IS s;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(&s);
    CHECK_EQ(reinterpret_cast<const uint8_t*>(&s._current) - base, static_cast<ptrdiff_t>(0x00));
    CHECK_EQ(reinterpret_cast<const uint8_t*>(&s._previous) - base, static_cast<ptrdiff_t>(0x1c));
}

UNIT_TEST(graphics_interpolatedstate_layout_rule_is_the_templates) {
    // The +0x00 / +sizeof(T) rule belongs to the template, not to Transform's
    // 0x1c. Retail instantiates it for `float` too (?Lerp@?$InterpolatedState@M@
    // Graphics@@QEBAMM@Z at 0x1800153e0).
    using ISF = Graphics::InterpolatedState<float>;
    CHECK_EQ(sizeof(ISF), 2 * sizeof(float));
    CHECK_EQ(offsetof(ISF, _current), static_cast<size_t>(0));
    CHECK_EQ(offsetof(ISF, _previous), sizeof(float));

    using ISV = Graphics::InterpolatedState<Math::Vector3>;
    CHECK_EQ(sizeof(ISV), 2 * sizeof(Math::Vector3));
    CHECK_EQ(offsetof(ISV, _previous), sizeof(Math::Vector3));
}

// ---------------------------------------------------------------- PART B ----

UNIT_TEST(graphics_interpolatedstate_lerp_runs_previous_to_current) {
    Graphics::InterpolatedState<Math::Transform> s;
    s._current  = MakeTransform(0.6f, 10.0f);   // this + 0x00  -- Lerp's R8
    s._previous = MakeTransform(0.0f,  2.0f);   // this + 0x1c  -- Lerp's RCX

    // t == 0 -> the PREVIOUS value. A reversed implementation returns _current.
    const Math::Transform at0 = s.Lerp(Math::Scalar(0.0f));
    CHECK(SameTransform(at0, s._previous));
    CHECK(!SameTransform(at0, s._current));

    // t == 1 -> the CURRENT value. Same check from the other end.
    const Math::Transform at1 = s.Lerp(Math::Scalar(1.0f));
    CHECK(SameTransform(at1, s._current));
    CHECK(!SameTransform(at1, s._previous));

    // The translation is a plain component-wise lerp, so the midpoint is
    // checkable in closed form even though the rotation is not.
    const Math::Transform mid = s.Lerp(Math::Scalar(0.5f));
    const Math::Vector3& pm = mid.translation;
    const Math::Vector3& pc = s._current.translation;
    const Math::Vector3& pp = s._previous.translation;
    CHECK(NearF(pm.x, pp.x + (pc.x - pp.x) * 0.5f));
    CHECK(NearF(pm.y, pp.y + (pc.y - pp.y) * 0.5f));
    CHECK(NearF(pm.z, pp.z + (pc.z - pp.z) * 0.5f));
}

UNIT_TEST(graphics_interpolatedstate_lerp_does_not_mutate) {
    // `Lerp` is `QEBA` -- const. It must not write either half; the callers
    // (geometry_draw.cpp, statusicon_geometry.cpp, src/ext/draw_batch/draw_batch.cpp)
    // call it inside a per-frame Draw and would accumulate drift if it did.
    Graphics::InterpolatedState<Math::Transform> s;
    s._current  = MakeTransform(0.6f, 10.0f);
    s._previous = MakeTransform(0.0f,  2.0f);

    const Math::Transform beforeCur  = s._current;
    const Math::Transform beforePrev = s._previous;
    (void)s.Lerp(Math::Scalar(0.25f));
    (void)s.Lerp(Math::Scalar(0.75f));
    CHECK(SameTransform(s._current,  beforeCur));
    CHECK(SameTransform(s._previous, beforePrev));
}

// ---------------------------------------------------------------- PART C ----

UNIT_TEST(graphics_interpolatedstate_seeding_ctor_has_no_history) {
    // ??0?$InterpolatedState@VTransform@Math@@@Graphics@@QEAA@AEBVTransform@Math@@@Z
    // (0x18001f800) stores ONE value into BOTH halves, so a freshly seeded state
    // lerps to that value at every t.
    const Math::Transform v = MakeTransform(0.3f, -4.0f);
    const Graphics::InterpolatedState<Math::Transform> s(v);

    CHECK(SameTransform(s._current, v));
    CHECK(SameTransform(s._previous, v));
    CHECK(SameTransform(s.Lerp(Math::Scalar(0.0f)), v));
    CHECK(SameTransform(s.Lerp(Math::Scalar(0.5f)), v));
    CHECK(SameTransform(s.Lerp(Math::Scalar(1.0f)), v));
}

// ---------------------------------------------------------------- PART D ----

UNIT_TEST(graphics_interpolatedstate_stays_trivially_copyable) {
    using IS = Graphics::InterpolatedState<Math::Transform>;
    CHECK(std::is_trivially_copyable<IS>::value);
    CHECK(std::is_trivially_destructible<IS>::value);
    CHECK(std::is_trivially_copy_assignable<IS>::value);
    // The Array<> element spec for this type has no element destroy (its init
    // stores two identity transforms); a non-trivial member here would make GCC emit a second element
    // lifecycle beside the ported one.
    CHECK(std::is_trivially_copy_constructible<IS>::value);
}
