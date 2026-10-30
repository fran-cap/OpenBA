// ============================================================================
// src/engine/math/vector.cpp — Math::Scalar / Vector2 / Vector3 / Vector4
// ============================================================================
//
// Wave 1, Lane 1 of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2/§6).
// Every function below is a byte-faithful port of the decompilation at the
// retail address named in its comment (Runtime-x64.dll, image base
// 0x180000000, decompiled 2026-09-06).
//
// ENDGAME B.3.1 step 1: the bodies MOVED onto the real classes in
// src/engine/math/Math.h. They were not rewritten — every expression, every
// term order and every branch is the one Wave 1 transcribed. What changed is
// only the plumbing: the hidden return slot the DLL boundary needed is gone
// (there is no cross-compiler ABI any more, B31_RULES), so a function that
// wrote `*ret` and returned it now returns by value. The `RtMath_*` free
// functions at the bottom of this file are one-line shims onto the members,
// kept so that no caller has to move in the same commit; they and their
// registry rows die in step 2.
//
// FUN_180128110 (sqrt) and FUN_180126680/FUN_180127d90/FUN_180126d10
// (atan2/sinf/cosf) were decompiled and confirmed to be plain CRT math
// wrapped in matherr-style domain-error handling; for every finite,
// non-exceptional input actually reached by these callers they reduce to the
// hardware sqrtss/plain libm call. Transcendentals go through the RtLibm seam
// (src/engine/rt_libm.h) rather than calling std:: or the DLL directly:
// our own libm by default (so the port survives Runtime-x64.dll's eventual
// removal, PLAN.md §4.1 decision 1), with the DLL's own CRT available as a
// diagnostic to tell a real bug from ordinary last-bit libm rounding.
// ============================================================================

#include <cmath>
#include "math/Math.h"
#include "rt_libm.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 callers batch H

namespace {
// Deg->rad already lives in RtMathConst; rad->deg is only needed here.
// Pinned by Vector3::Heading (0x18007c900): _DAT_180161708 = 0x42652ee1.
constexpr float kRadToDeg = 57.29578f;  // 0x180161708
}  // namespace

// ?SinCos@Scalar@Math@@QEBAXAEAV12@0@Z  retail 0x180078790
// The angle is in DEGREES: Crt_sinf(v * DAT_18016167c) into sinOut, then Crt_cosf of the
// same product into cosOut (the product is formed twice, as retail does).
void Scalar::SinCos(Scalar& sinOut, Scalar& cosOut) const {
    sinOut.v = RtLibm::Sin(this->v * RtMathConst::kDegToRad);
    cosOut.v = RtLibm::Cos(this->v * RtMathConst::kDegToRad);
}

namespace Math {

// ============================================================
// Math::Scalar
// ============================================================

// ?Abs@Scalar@Math@@QEBA?AV12@XZ  retail 0x1800646b0

// ?Lerp@Scalar@Math@@QEAA?AV12@AEBV12@0@Z  retail 0x1800218c0
// Non-const (QEAA) but reads only `this`; t is the 3rd arg (R9).

// ?Max@Scalar@Math@@QEBA?AV12@AEBV12@@Z  retail 0x18005ff60

// ============================================================
// Math::Vector2
// ============================================================

// ??0Vector2@Math@@QEAA@XZ  retail 0x180060030 — the default ctor. It zeroes
// both fields; that body is now the constexpr member initialiser in Math.h
// (B31_RULES rule 5: read retail's, and it is not a no-op).

// ??0Vector2@Math@@QEAA@AEBVScalar@1@0@Z  retail 0x180070db0

// ??XVector2@Math@@QEAAXAEBV01@@Z  retail 0x18005ffd0  (operator*=, void return, mutates self
// in place — not diffed here since diffing would require running the original on live state
// twice; the formula is a two-multiply and trivially verified by inspection).

// ?Dot@Vector2@Math@@QEBA?AVScalar@2@AEBV12@@Z  retail 0x18006c840

// ?Max@Vector2@Math@@QEBA?AV12@AEBV12@@Z  retail 0x180060330

// ?Min@Vector2@Math@@QEBA?AV12@AEBV12@@Z  retail 0x1800602e0

// ?Length@Vector2@Math@@QEBA?AVScalar@2@XZ  retail 0x18006c880
Scalar Vector2::Length() const {
    Scalar ret;
    ret.v = RtLibm::Sqrt(this->x * this->x + this->y * this->y);
    return ret;
}

// ?LengthSquared@Vector2@Math@@QEBA?AVScalar@2@XZ  retail 0x18006c860

// ?Normalize@Vector2@Math@@QEBA?AV12@XZ  retail 0x18006c600  (by value; self unchanged)
Vector2 Vector2::Normalize() const {
    Vector2 ret;
    float len = RtLibm::Sqrt(this->x * this->x + this->y * this->y);
    if (len == 0.0f) {
        ret = *this;
    } else {
        float inv = RtMathConst::kOne / len;
        ret.x = this->x * inv;
        ret.y = this->y * inv;
    }
    return ret;
}

// ?Normalize@Vector2@Math@@QEBA?AV12@AEAVScalar@2@@Z  retail 0x18006c7b0
// Same as above but also writes the pre-normalize length out through `mag`.
Vector2 Vector2::Normalize(Scalar& mag) const {
    Vector2 ret;
    float len = RtLibm::Sqrt(this->x * this->x + this->y * this->y);
    mag.v = len;
    if (len == 0.0f) {
        ret = *this;
    } else {
        float inv = RtMathConst::kOne / len;
        ret.x = this->x * inv;
        ret.y = this->y * inv;
    }
    return ret;
}

// ?Serialize@Vector2@Math@@QEAAXAEAVSerializer@IO@@@Z  retail 0x18005ff40
// NOT diffable: mutates the live IO::Serializer stream (reads or writes bytes
// from/into it).
//
// ENDGAME B.3.4 callers batch H. This used to be a hand-inlined slot call, and
// its comment called `serializer + 8` "the Serializer's Stream*". It is not:
// the Stream is at Serializer+0x00 and +0x08 is the READERWRITER, whose vtable
// +0x28 is rw_vtbl::kArrayFloat — "serialize N floats", which is why the count
// (2/3/4 for Vector2/3/4) is arg 2. The old three-argument form left the
// ignored `const char* name` register unset; passing nullptr is wire-identical.
void Vector2::Serialize(::IO::Serializer& serializer) {
    serializer.Serialize(2, &x, nullptr);   // +0x28, the float-array door
}

// ============================================================
// Math::Vector3
// ============================================================

// ??0Vector3@Math@@QEAA@XZ  retail 0x180015070 — zeroes x/y/z; that body is the
// constexpr default ctor in Math.h.
// ??0Vector3@Math@@QEAA@MMM@Z  retail 0x1800152d0 — three plain float stores;
// that body is the constexpr (float,float,float) ctor in Math.h.

// ??0Vector3@Math@@QEAA@AEBVScalar@1@00@Z  retail 0x180015120

// ??0Vector3@Math@@QEAA@AEBVScalar@1@@Z  retail 0x180015360  (splat one Scalar to x/y/z)

// ?Cross@Vector3@Math@@QEBA?AV12@AEBV12@@Z  retail 0x180021b10

// ?Dot@Vector3@Math@@QEBA?AVScalar@2@AEBV12@@Z  retail 0x18001dff0

// ?FromYawPitch@Vector3@Math@@SA?AV12@AEBVScalar@2@0@Z  retail 0x1800220b0 (static)
// x = -cos(pitch_rad) * sin(yaw_rad); y = cos(pitch_rad) * cos(yaw_rad); z = sin(pitch_rad)
Vector3 Vector3::FromYawPitch(const Scalar& yaw, const Scalar& pitch) {
    Vector3 ret;
    float yawRad = yaw.v * RtMathConst::kDegToRad;
    float pitchRad = pitch.v * RtMathConst::kDegToRad;
    float sinYaw = RtLibm::Sin(yawRad);
    float sinPitch = RtLibm::Sin(pitchRad);
    float cosYaw = RtLibm::Cos(yawRad);
    float cosPitch = RtLibm::Cos(pitchRad);
    ret.z = sinPitch;
    ret.x = -cosPitch * sinYaw;
    ret.y = cosPitch * cosYaw;
    return ret;
}

// ?Heading@Vector3@Math@@QEBAMXZ  retail 0x18007c900  (returns a plain float in XMM0)
// FUN_180126680 was decompiled and is the CRT double atan2() (its own
// domain-error fallback even names it "atan2f"); RtLibm::Atan2 reproduces it.
float Vector3::Heading() const {
    float ours = RtLibm::Atan2(-this->x, this->y) * kRadToDeg;
    return ours;
}

// ?Length@Vector3@Math@@QEBA?AVScalar@2@XZ  retail 0x1800219a0
Scalar Vector3::Length() const {
    Scalar ret;
    ret.v = RtLibm::Sqrt(this->x * this->x + this->y * this->y + this->z * this->z);
    return ret;
}

// ?Lerp@Vector3@Math@@QEBA?AV12@AEBV12@AEBVScalar@2@@Z  retail 0x18001d450

// ?Min@Vector3@Math@@QEBA?AV12@AEBV12@@Z  retail 0x180015280

// ?Normalize@Vector3@Math@@QEBA?AV12@AEAVScalar@2@@Z  retail 0x180021a20
// Takes an explicit Scalar& out-param for the pre-normalize magnitude.
Vector3 Vector3::Normalize(Scalar& mag) const {
    Vector3 ret;
    float len = RtLibm::Sqrt(this->x * this->x + this->y * this->y + this->z * this->z);
    mag.v = len;
    if (len != 0.0f) {
        float inv = RtMathConst::kOne / len;
        ret.x = this->x * inv;
        ret.y = this->y * inv;
        ret.z = this->z * inv;
    } else {
        ret.x = this->x;
        ret.y = this->y;
        ret.z = this->z;
    }
    return ret;
}

// ?Normalize@Vector3@Math@@QEBA?AV12@XZ  retail 0x180021ae0
// Retail is a thin wrapper: allocates a throwaway Scalar on the stack and
// calls the Scalar&-out overload above, discarding the magnitude. Ported the
// same way (no independent diff site — it is exactly NormalizeWithMag's diff,
// twice, so a bare forwarding call keeps the two implementations in sync).
Vector3 Vector3::Normalize() const {
    Scalar scratchMag{};
    return this->Normalize(scratchMag);
}

// ??8Vector3@Math@@QEBA_NAEBV01@@Z  retail 0x1800150a0  (operator==, bool in AL, no hidden slot)

// ?Serialize@Vector3@Math@@QEAAXAEAVSerializer@IO@@@Z  retail 0x1800150e0 — see the Vector2
// Serialize comment above; NOT diffable, same vtable-slot pattern, count = 3.
void Vector3::Serialize(::IO::Serializer& serializer) {
    serializer.Serialize(3, &x, nullptr);   // +0x28, the float-array door
}

// ============================================================
// Math::Vector4
// ============================================================

// ??0Vector4@Math@@QEAA@AEBVScalar@1@@Z  retail 0x18001d750  (splat one Scalar to x/y/z/w)

// ??0Vector4@Math@@QEAA@AEBVVector3@1@AEBVScalar@1@@Z  retail 0x18001e070

// ?Serialize@Vector4@Math@@QEAAXAEAVSerializer@IO@@@Z  retail 0x18001d360 — see the Vector2
// Serialize comment above; NOT diffable, same vtable-slot pattern, count = 4.
void Vector4::Serialize(::IO::Serializer& serializer) {
    serializer.Serialize(4, &x, nullptr);   // +0x28, the float-array door
}

}  // namespace Math
