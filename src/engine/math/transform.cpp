// ============================================================================
// src/engine/math/transform.cpp
// ============================================================================
// Wave 1 / Lane 2 of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2,
// §6 recipe). Owns Math::Matrix, Math::Quaternion, Math::Transform,
// Math::Transform2, Math::Transform3.
//
// ENDGAME B.3.1 step 1: the bodies MOVED onto the real classes in
// src/engine/math/Math.h. They were not rewritten — every expression, every
// parenthesisation and every branch is the one Wave 1 transcribed from the
// disassembly. Two things changed, both plumbing:
//
//  1. The hidden return slot is gone. There is no cross-compiler ABI any more
//     (B31_RULES, "the one fact that changes the rules since the port"), so a
//     body that wrote `*ret` and returned it now returns by value.
//  2. With that, the ALIASING hazard the old signatures carried is gone too —
//     `ret` can no longer be `self`. The `*Snap` locals the Wave 0 diff oracle
//     needed for that hazard were already dead (the oracle is retired) and are
//     removed here; the comments that explain WHY the hazard existed are kept,
//     because they are the reason the term order below is what it is.
//
// The old ABI note, kept because it explains the register lists in the function banners below:
//   member function, struct return : RCX=this,   RDX=hidden ret, R8=arg1, ...
//   static function,  struct return : RCX=hidden ret, RDX=arg1, R8=arg2, ...
//   ctor (no separate "return", `this` IS the target)  : RCX=this, RDX=arg1, ...
//   function returning a reference (operator=)          : RCX=this, RDX=arg1, ... (no hidden slot)
//
// Every arithmetic port is a literal transliteration of the decompiled
// expression, term order preserved, NOT a "cleaned up" reformulation — float
// addition is not associative.
// Variable names mirror the meaning of each retail fVarN once traced back to
// a named field, but the operation ORDER is copied verbatim.
//
// Transcendentals (sqrtf/sinf/cosf) go through RtLibm (src/engine/rt_libm.h)
// rather than a direct call into Runtime-x64.dll's own CRT (at 0x180128110 /
// 0x180127d90 / 0x180126d10 respectively, confirmed by disassembly) —
// calling the DLL directly would leave the port permanently dependent on a DLL
// we are removing.
//
// ============================================================================

#include "math/Math.h"
#include "rt_libm.h"
#include "io/Serializer.h"   // B3X W0: IO::ReaderWriter, called by name

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace {

// FromDirection's two "nearly parallel with world Z" thresholds.
inline float BitsToFloat(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
constexpr uint32_t kNearParallelPosBits = 0x3f7fbe77u; //  0.998573... (0x180161698)
constexpr uint32_t kNearParallelNegBits = 0xbf7fbe77u; // -0.998573... (0x180161758)

} // namespace

namespace Math {

// ============================================================================
// Math::Quaternion::Quaternion(float,float,float,float)   -- ctor, this=RCX
// retail 0x18001e260  ??0Quaternion@Math@@QEAA@MMMM@Z
// ============================================================================

// ============================================================================
// Math::Transform::Transform()  -- default ctor, this=RCX
// retail 0x180015080  ??0Transform@Math@@QEAA@XZ
// The body (identity quaternion, zero translation) is the constexpr member
// initialiser in Math.h — B31_RULES rule 5: retail's default ctor is not a
// no-op, so it is reproduced, not `= default`ed away.
// ============================================================================

// ============================================================================
// Math::Transform::Transform(const Transform&)  -- copy ctor, RCX=this,RDX=other
// retail 0x180015020  ??0Transform@Math@@QEAA@AEBV01@@Z
// A plain 7-dword field copy in retail; `= default` in Math.h is exactly that
// and keeps the type trivially copyable.
// ============================================================================

// ============================================================================
// Math::Transform::operator=(const Transform&)  -- returns Transform&, RCX=this,RDX=other
// retail 0x180014fd0  ??4Transform@Math@@QEAAAEAV01@AEBV01@@Z
// A reference return has NO hidden retval slot (unlike a by-value struct
// return); the only "output" is the mutation of `self` itself. Also a plain
// field copy, so also `= default` in Math.h.
// ============================================================================

// ============================================================================
// Math::Transform3::Transform3(const Transform3&)  -- copy ctor
// retail 0x18001dee0  ??0Transform3@Math@@QEAA@AEBV01@@Z
// Plain 0x30-byte copy; `= default` in Math.h.
// ============================================================================

// ============================================================================
// Math::Transform2::Transform2(const Scalar& angleDeg, const Vector2& offset)
// ctor: RCX=this, RDX=angleDeg, R8=offset
// retail 0x18007dca0  ??0Transform2@Math@@QEAA@AEBVScalar@1@AEBVVector2@1@@Z
// this+0x00 = cos, +0x04 = sin, +0x08 = -sin, +0x0c = cos, +0x10 = offset.
// ============================================================================
Transform2::Transform2(const Scalar& angleDeg, const Vector2& offset) {
    this->translation = offset;              // retail writes the offset qword first
    float rad = angleDeg.v * RtMathConst::kDegToRad;
    float s = RtLibm::Sin(rad);
    float c = RtLibm::Cos(rad);
    this->rotX.x = c;
    this->rotX.y = s;
    this->rotY.x = -s;   // bit-exact XOR sign-flip in retail
    this->rotY.y = c;
}

// ============================================================================
// Math::Matrix::operator*(const Matrix&) const
// member, struct return: RCX=this, RDX=hidden ret, R8=other
// retail 0x18001d980  ??DMatrix@Math@@QEBA?AV01@AEBV01@@Z
// Standard row-major 4x4 multiply, result = this * other. Verified against
// the decompiled term order for every one of the 16 output cells: k ascending
// 0..3 throughout. Computed into a local scratch first — retail did that
// because `ret` could alias `self`/`other`; the by-value return makes that
// impossible now, but the scratch is kept so the schedule is unchanged.
// ============================================================================
Matrix Matrix::operator*(const Matrix& other) const {
    Matrix ret;
    float out[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r * 4 + c] =
                this->m[r * 4 + 0] * other.m[0 * 4 + c] +
                this->m[r * 4 + 1] * other.m[1 * 4 + c] +
                this->m[r * 4 + 2] * other.m[2 * 4 + c] +
                this->m[r * 4 + 3] * other.m[3 * 4 + c];
        }
    }
    std::memcpy(ret.m, out, sizeof(out));
    return ret;
}

// ============================================================================
// Math::Matrix::operator*(const Vector4&) const
// member, struct return: RCX=this, RDX=hidden ret, R8=v
// retail 0x18007d030  ??DMatrix@Math@@QEBA?AVVector4@1@AEBV21@@Z
// result[c] = v.y*m[4+c] + v.x*m[0+c] + v.z*m[8+c] + v.w*m[12+c]  (verified
// per-component; term order y,x,z,w is retail's, not a typo).
// ============================================================================
Vector4 Matrix::operator*(const Vector4& v) const {
    Vector4 ret;
    float out[4];
    for (int c = 0; c < 4; ++c) {
        out[c] = v.y * this->m[4 + c] + v.x * this->m[0 + c] +
                 v.z * this->m[8 + c] + v.w * this->m[12 + c];
    }
    ret.x = out[0]; ret.y = out[1]; ret.z = out[2]; ret.w = out[3];
    return ret;
}

// ============================================================================
// Math::Transform2::operator*(const Transform2&) const
// member, struct return: RCX=this, RDX=hidden ret, R8=other
// retail 0x180078650  ??DTransform2@Math@@QEBA?AV01@AEBV01@@Z
// ============================================================================
Transform2 Transform2::operator*(const Transform2& other) const {
    Transform2 ret;
    float rotXx = this->rotX.x * other.rotX.x + this->rotX.y * other.rotY.x;
    float rotXy = this->rotX.x * other.rotX.y + this->rotX.y * other.rotY.y;
    float rotYx = this->rotY.x * other.rotX.x + this->rotY.y * other.rotY.x;
    float rotYy = this->rotY.x * other.rotX.y + this->rotY.y * other.rotY.y;
    float tx = other.rotX.x * this->translation.x + this->translation.y * other.rotY.x + other.translation.x;
    float ty = other.rotX.y * this->translation.x + other.rotY.y * this->translation.y + other.translation.y;
    ret.rotX.x = rotXx; ret.rotX.y = rotXy;
    ret.rotY.x = rotYx; ret.rotY.y = rotYy;
    ret.translation.x = tx; ret.translation.y = ty;
    return ret;
}

// ============================================================================
// Math::Transform2::operator*(const Vector2&) const
// member, struct return: RCX=this, RDX=hidden ret, R8=v
// retail 0x18006c8e0  ??DTransform2@Math@@QEBA?AVVector2@1@AEBV21@@Z
// ============================================================================
Vector2 Transform2::operator*(const Vector2& v) const {
    Vector2 ret;
    float x = v.y * this->rotY.x + v.x * this->rotX.x + this->translation.x;
    float y = v.y * this->rotY.y + v.x * this->rotX.y + this->translation.y;
    ret.x = x; ret.y = y;
    return ret;
}

// ============================================================================
// Math::Transform3::operator*(const Vector3&) const
// member, struct return: RCX=this, RDX=hidden ret, R8=v
// retail 0x18001fbc0  ??DTransform3@Math@@QEBA?AVVector3@1@AEBV21@@Z
// ============================================================================
Vector3 Transform3::operator*(const Vector3& v) const {
    Vector3 ret;
    float x = v.y * this->axisY.x + v.x * this->axisX.x + v.z * this->axisZ.x + this->translation.x;
    float z = v.y * this->axisY.z + v.x * this->axisX.z + v.z * this->axisZ.z + this->translation.z;
    float y = v.y * this->axisY.y + v.x * this->axisX.y + v.z * this->axisZ.y + this->translation.y;
    ret.x = x; ret.y = y; ret.z = z;
    return ret;
}

// ============================================================================
// Math::Transform::AsMatrix() const
// member, struct return: RCX=this, RDX=hidden ret
// retail 0x18001e5f0  ?AsMatrix@Transform@Math@@QEBA?AVMatrix@2@XZ
// ============================================================================
Matrix Transform::AsMatrix() const {
    Matrix ret;
    float qx = this->rotation.x;
    float qy = this->rotation.y;
    float qz = this->rotation.z;
    float qw = this->rotation.w;

    float qwqx2 = qw * qx * RtMathConst::kTwo;
    float qwqz2 = qw * qz * RtMathConst::kTwo;
    float qyqy2 = qy * qy * RtMathConst::kTwo;
    float qxqx2 = qx * qx * RtMathConst::kTwo;
    float qyqx2 = qy * qx * RtMathConst::kTwo;
    float qzqx2 = qz * qx * RtMathConst::kTwo;
    float qzqz2 = qz * qz * RtMathConst::kTwo;
    float qzqy2 = qz * qy * RtMathConst::kTwo;
    float qwqy2 = qw * qy * RtMathConst::kTwo;

    float out[16] = {0};
    out[4] = qwqz2 + qyqx2;
    out[8] = qzqx2 - qwqy2;
    out[9] = qwqx2 + qzqy2;
    out[10] = RtMathConst::kOne - (qyqy2 + qxqx2);
    out[5] = RtMathConst::kOne - (qzqz2 + qxqx2);
    out[0] = RtMathConst::kOne - (qzqz2 + qyqy2);
    out[1] = qyqx2 - qwqz2;
    out[12] = this->translation.x;
    out[2] = qwqy2 + qzqx2;
    out[6] = qzqy2 - qwqx2;
    out[13] = this->translation.y;
    out[3] = 0.0f; out[7] = 0.0f; out[11] = 0.0f;
    out[14] = this->translation.z;
    out[15] = RtMathConst::kOne;
    std::memcpy(ret.m, out, sizeof(out));
    return ret;
}

// ============================================================================
// Math::Transform3::AsMatrix() const
// member, struct return: RCX=this, RDX=hidden ret
// retail 0x1800346e0  ?AsMatrix@Transform3@Math@@QEBA?AVMatrix@2@XZ
// Pure rearrangement, no arithmetic — order does not matter for fidelity.
// ============================================================================
Matrix Transform3::AsMatrix() const {
    Matrix ret;
    float out[16] = {0};
    out[0] = this->axisX.x; out[1] = this->axisX.y; out[2] = this->axisX.z; out[3] = 0.0f;
    out[4] = this->axisY.x; out[5] = this->axisY.y; out[6] = this->axisY.z; out[7] = 0.0f;
    out[8] = this->axisZ.x; out[9] = this->axisZ.y; out[10] = this->axisZ.z; out[11] = 0.0f;
    out[12] = this->translation.x; out[13] = this->translation.y; out[14] = this->translation.z;
    out[15] = RtMathConst::kOne;
    std::memcpy(ret.m, out, sizeof(out));
    return ret;
}

// ============================================================================
// Math::Transform::Inverse() const
// member, struct return: RCX=this, RDX=hidden ret
// retail 0x18005db50  ?Inverse@Transform@Math@@QEBA?AV12@XZ
// Rigid inverse: rotation' = conjugate(q); translation' is formed from a
// quaternion-vector-rotate expansion of -translation by the conjugate.
// Transliterated verbatim from the decompile — NOT the clean textbook
// formula, retail's exact term order.
// ============================================================================
Transform Transform::Inverse() const {
    Transform ret;
    float qx = this->rotation.x;
    float negQx = -qx;
    float negTx = -this->translation.x;
    float negTz = -this->translation.z;
    float negTy = -this->translation.y;
    float qw = this->rotation.w;
    float qz = this->rotation.z;
    float negQz = -qz;
    float qy = this->rotation.y;
    float negQy = -qy;

    float t10 = (negTx * qw + negQx * 0.0f + negTy * negQz) - negTz * negQy;
    float t11 = (negQy * 0.0f - negTx * negQz) + negTy * qw + negTz * negQx;
    float t7  = ((negTx * negQy + negQz * 0.0f) - negTy * negQx) + negTz * qw;
    float t6  = ((qw * 0.0f - negTx * negQx) - negTy * negQy) - negTz * negQz;

    ret.rotation.x = negQx;
    ret.rotation.y = negQy;
    ret.rotation.z = negQz;
    ret.rotation.w = qw;
    ret.translation.x = (t6 * qx + t10 * qw + t7 * qy) - t11 * qz;
    ret.translation.y = (t11 * qw - t7 * qx) + t6 * qy + t10 * qz;
    ret.translation.z = ((t11 * qx + t7 * qw) - t10 * qy) + t6 * qz;
    return ret;
}

// ============================================================================
// Math::Transform3::Inverse() const
// member, struct return: RCX=this, RDX=hidden ret
// retail 0x180060040  ?Inverse@Transform3@Math@@QEBA?AV12@XZ
// Rigid inverse: rotation' = transpose(R) (orthonormal basis), translation' =
// -transpose(R) * translation.
// ============================================================================
Transform3 Transform3::Inverse() const {
    Transform3 ret;
    float negTz = -this->translation.z;
    float negTx = -this->translation.x;
    float negTy = -this->translation.y;

    ret.axisX.x = this->axisX.x;
    ret.axisX.y = this->axisY.x;
    ret.axisX.z = this->axisZ.x;
    ret.axisY.x = this->axisX.y;
    ret.axisY.y = this->axisY.y;
    ret.axisY.z = this->axisZ.y;
    ret.axisZ.x = this->axisX.z;
    ret.axisZ.y = this->axisY.z;
    ret.axisZ.z = this->axisZ.z;

    ret.translation.x = negTy * this->axisX.y + negTx * this->axisX.x + negTz * this->axisX.z;
    ret.translation.y = negTy * this->axisY.y + negTx * this->axisY.x + negTz * this->axisY.z;
    ret.translation.z = negTy * this->axisZ.y + negTx * this->axisZ.x + negTz * this->axisZ.z;
    return ret;
}

// ============================================================================
// Math::Quaternion::operator*(const Quaternion&) const
// member, struct return: RCX=this, RDX=hidden ret, R8=other
// retail 0x18001e280  ??DQuaternion@Math@@QEBA?AV01@AEBV01@@Z
// NOT in Wave 1's assigned function list, but required by Quaternion::Rotate
// below (retail calls it internally) — ported since it is genuine
// Math::Quaternion surface.
// ============================================================================
Quaternion Quaternion::operator*(const Quaternion& other) const {
    Quaternion ret;
    float x_ = (this->x * other.w + other.x * this->w + this->z * other.y) - this->y * other.z;
    float y_ = (this->y * other.w - this->z * other.x) + other.y * this->w + other.z * this->x;
    float w_ = ((other.w * this->w - this->x * other.x) - this->y * other.y) - other.z * this->z;
    // Final term is other.z * self.W, not self.y. Retail (0x18001e280) loads
    // fVar2 = *(this + 0xc) — the w component — and the z row reuses it:
    //   ((a.z*b.w + a.y*b.x) - b.y*a.x) + b.z*a.w
    // Using self->y here made every quaternion product wrong in z. It stayed
    // invisible through the whole of Wave 1 because NOTHING called it: this
    // export and Quaternion::Rotate below it had zero calls in every diff run
    // until Transform::operator*(Vector3) was added, at which point they
    // diverged on 82% and 100% of calls respectively.
    float z_ = ((this->z * other.w + this->y * other.x) - other.y * this->x) + other.z * this->w;
    ret.x = x_; ret.y = y_; ret.w = w_; ret.z = z_;
    return ret;
}

// ============================================================================
// Math::Quaternion::Rotate(const Vector3&) const
// member, struct return: RCX=this, RDX=hidden ret, R8=v
// retail 0x18001e140  ?Rotate@Quaternion@Math@@QEBA?AVVector3@2@AEBV32@@Z
// v' = q * Quaternion(v, 0) * conjugate(q). Confirmed by disassembly: RCX at
// both inner operator* calls is `this` unmodified — NOT the conjugate — so
// the multiplication order is (this * vectorQuat) * conjugate(this), not the
// textbook conj(q)*v*q.
// ============================================================================
Vector3 Quaternion::Rotate(const Vector3& v) const {
    Vector3 ret;
    Quaternion vectorQuat{v.x, v.y, v.z, 0.0f};
    Quaternion conj{-this->x, -this->y, -this->z, this->w};
    Quaternion tmp = *this * vectorQuat;
    Quaternion rotated = tmp * conj;
    ret.x = rotated.x; ret.y = rotated.y; ret.z = rotated.z;
    return ret;
}

// ============================================================================
// Math::Transform3::Rotate(const Vector3&) const
// member, struct return: RCX=this, RDX=hidden ret, R8=v
// retail 0x18001e470  ?Rotate@Transform3@Math@@QEBA?AVVector3@2@AEBV32@@Z
// Same as operator*(const Vector3&) above but WITHOUT adding translation (a basis rotate only).
// ============================================================================
Vector3 Transform3::Rotate(const Vector3& v) const {
    Vector3 ret;
    float x = v.y * this->axisY.x + v.x * this->axisX.x + v.z * this->axisZ.x;
    float z = v.y * this->axisY.z + v.x * this->axisX.z + v.z * this->axisZ.z;
    float y = v.y * this->axisY.y + v.x * this->axisX.y + v.z * this->axisZ.y;
    ret.x = x; ret.y = y; ret.z = z;
    return ret;
}

// ============================================================================
// Math::Matrix::Matrix(const Vector4& c0, const Vector4& c1,
//                      const Vector4& c2, const Vector4& c3)
// ??0Matrix@Math@@QEAA@AEBVVector4@1@000@Z        retail 0x18001e540
// ----------------------------------------------------------------------------
// WAVE 9 LANE SAVE2. Wave 1 ported every Matrix operator but not this ctor,
// because nothing in runtime/ calls it — the one caller in the whole tree is
// `src/game/Game/weather_particle_draw.cpp`, which reached it BY ADDRESS
// (`ResolveAddr(Module::Runtime, 0x18001e540)`) under a comment saying there
// was "no verified mangled name to hand". There is: the name above is in the
// shipped export table at that exact RVA, checked with pefile before this line
// was written.
//
// ctor ABI (this file's banner): `this` IS the target, so there is no hidden
// return slot — RCX=this, RDX=c0, R8=c1, R9=c2, and the FOURTH column arrives
// ON THE STACK. Retail reads it as `MOV RAX,[RSP+0x28]`, i.e. the first slot
// past the 32-byte shadow space, which is the fifth argument counting `this`.
// Getting that wrong is the "a pointer param typed int is only wrong from the
// 5th arg on" defect this project has already paid for.
//
// 39 instructions, all MOV: four 16-byte column copies done as four DWORD
// loads and four DWORD stores each, columns written in order 0,1,2,3 into
// +0x00 / +0x10 / +0x20 / +0x30. RAX = `this` at the RET.
// ============================================================================
Matrix::Matrix(const Vector4& c0, const Vector4& c1, const Vector4& c2, const Vector4& c3) {
    std::memcpy(&this->m[0],  &c0, 16);
    std::memcpy(&this->m[4],  &c1, 16);
    std::memcpy(&this->m[8],  &c2, 16);
    std::memcpy(&this->m[12], &c3, 16);
}

// ============================================================================
// Math::Matrix::Identity()  -- static, struct return: RCX=hidden ret
// retail 0x18002c030  ?Identity@Matrix@Math@@SA?AV12@XZ
// ============================================================================
Matrix Matrix::Identity() {
    Matrix ret;
    float out[16] = {0};
    out[0] = out[5] = out[10] = out[15] = RtMathConst::kOne;
    std::memcpy(ret.m, out, sizeof(out));
    return ret;
}

// ============================================================================
// Math::Transform::Identity()  -- static, struct return: RCX=hidden ret
// retail 0x18001fab0  ?Identity@Transform@Math@@SA?AV12@XZ
// ============================================================================

// ============================================================================
// Math::Transform3::Identity()  -- static, struct return: RCX=hidden ret
// retail 0x1800347f0  ?Identity@Transform3@Math@@SA?AV12@XZ
// ============================================================================

// ============================================================================
// Math::Matrix::operator!=(const Matrix&) const
// retail 0x18001d850, ordinal 1286  ??9Matrix@Math@@QEBA_NAEBV01@@Z
// ============================================================================
// ENDGAME B.3.1 step 2: moved here from src/engine/video/context.cpp, where
// it had been ported as the C free function `RtMath_Matrix_NotEqual` (and used
// inline for Context::SetTransform's dirty check). Retail's body is sixteen
// `==` comparisons with a nested short-circuit shape; the flat loop below is
// the same result instruction for instruction, NaN included — every `==` is
// false on a NaN cell, so the operator reports "not equal", which is what
// retail does and is why this is not spelled `!(*this == other)` (there is no
// retail `operator==` for Matrix at all).
// ============================================================================
bool Matrix::operator!=(const Matrix& other) const {
    for (int i = 0; i < 16; ++i) {
        if (!(m[i] == other.m[i])) return true;
    }
    return false;
}

// ============================================================================
// Math::Transform Math::Transform::Lerp(const Transform&, const Scalar&) const
// retail 0x18001d690, ordinal 3193
// ?Lerp@Transform@Math@@QEBA?AV12@AEBV12@AEBVScalar@2@@Z
// ============================================================================
// ENDGAME B.3.1 step 2: moved here from src/engine/graphics/wave9_lane_gfx.cpp
// unchanged.
//
// TRANSLATION, from 0x18001d696..0x18001d6fa, term for term:
//   x:  XMM2 = to.x - this->x ; XMM1 = t ; XMM1 = XMM1 * XMM2 ;
//       XMM8 = this->x        ; XMM8 = XMM8 + XMM1
//   y:  XMM4 = to.y - this->y ; XMM4 = XMM4 * t ; XMM7 = this->y + XMM4
//   z:  XMM3 = to.z - this->z ; XMM3 = XMM3 * t ; XMM6 = this->z + XMM3
//
// i.e. `a + t*(b - a)`, NOT `a*(1-t) + b*t`. Those two are not the same float.
// The x lane multiplies `t * (b-a)` and the y/z lanes `(b-a) * t`; IEEE
// multiplication is commutative to the bit, so the mirrored operand order is
// kept only for readers diffing against the listing.
//
// ROTATION: `CALL 0x18001d500` at 0x18001d6fe with RCX unchanged — Transform::
// rotation is at +0x00, so `this` doubles as `&this->rotation`. That is
// Quaternion::Lerp.
//
// The old hidden-return-slot signature allowed `ret == this` or `ret == other`;
// returning by value removes the hazard entirely, but the three translation
// floats are still computed into locals before anything is stored, which is
// also retail's own order.
// ============================================================================
Transform Transform::Lerp(const Transform& other, const Scalar& t) const {
    const float tv = t.v;

    const float ax = translation.x;
    const float ay = translation.y;
    const float az = translation.z;

    const float rx = ax + tv * (other.translation.x - ax);   // 0x18001d6d8/e1/ed
    const float ry = ay + (other.translation.y - ay) * tv;   // 0x18001d6dd/e5/f2
    const float rz = az + (other.translation.z - az) * tv;   // 0x18001d6e9/f6/fa

    Transform ret;
    ret.rotation = rotation.Lerp(other.rotation, t);  // the copy at 0x18001d703..716
    ret.translation.x = rx;                           // 0x18001d71c/728/732
    ret.translation.y = ry;
    ret.translation.z = rz;
    return ret;
}

// ============================================================================
// Math::Transform3 Math::Transform3::operator*(const Transform3&) const
// retail 0x180022680  ??DTransform3@Math@@QEBA?AV01@AEBV01@@Z
// ============================================================================
// ENDGAME B.3.1 step 2: moved here from
// src/engine/graphics/imagebuffer_derived.cpp, where it had been ported as the
// C free function `RtMath_Transform3_Multiply` because ReflectionImageBuffer::
// Render could not run without it.
//
// Derived instruction by instruction from 0x1800226bd..0x18002295d. A
// Transform3 is four ROWS of three floats: rows 0..2 are the 3x3 basis, row 3
// is the translation.
//
//     out.row[i] = a.row[i][0]*b.row[0] + a.row[i][1]*b.row[1]
//                                       + a.row[i][2]*b.row[2]     (i = 0,1,2)
//     out.row[3] = same, PLUS b.row[3]
//
// *** THE ADD ORDER IS LOAD-BEARING AND IS RETAIL'S. *** Every one of the
// twelve outputs accumulates strictly left to right — `(x0*b0 + x1*b3) + x2*b6`
// and, for the translation row, `((t0*b0 + t1*b3) + t2*b6) + b9`. Float
// addition is not associative, so a different grouping is a different number.
// The build uses no `-march` beyond the x86-64 baseline, so GCC cannot emit an
// FMA here and cannot contract a product into an add; the C below is therefore
// bit-exact against the shipped MULSS/ADDSS sequence.
//
// Retail's register scheduling interleaves all four rows and is NOT reproduced:
// the schedule is what the compiler chose, the arithmetic is what the function
// means, and no observer can tell the two apart.
//
// The rows are addressed as twelve contiguous floats rather than as the four
// named Vector3 fields, exactly as the C body did; the class layout
// static_asserts in Math.h are what make that legal (axisY @ +0x0c, axisZ @
// +0x18, translation @ +0x24 — contiguous, no padding).
// ============================================================================
Transform3 Transform3::operator*(const Transform3& other) const {
    const float* const a = &axisX.x;
    const float* const b = &other.axisX.x;

    Transform3 out;
    float* const o = &out.axisX.x;

    for (int row = 0; row < 4; ++row) {
        const float x = a[row * 3 + 0];
        const float y = a[row * 3 + 1];
        const float z = a[row * 3 + 2];
        for (int col = 0; col < 3; ++col) {
            float v = x * b[0 + col];
            v = v + y * b[3 + col];
            v = v + z * b[6 + col];
            if (row == 3) v = v + b[9 + col];      // the translation row only
            o[row * 3 + col] = v;
        }
    }
    return out;
}

// ============================================================================
// Math::Transform2::FromYawOffset(const Scalar& yawDeg, const Vector2& offset)
// static, struct return: RCX=hidden ret, RDX=yawDeg, R8=offset
// retail 0x1800787f0  ?FromYawOffset@Transform2@Math@@SA?AV12@AEBVScalar@2@AEBVVector2@2@@Z
// Identical construction to the Transform2 ctor above.
// ============================================================================
Transform2 Transform2::FromYawOffset(const Scalar& yawDeg, const Vector2& offset) {
    Transform2 ret;
    float rad = yawDeg.v * RtMathConst::kDegToRad;
    float s = RtLibm::Sin(rad);
    float c = RtLibm::Cos(rad);
    ret.rotX.x = c;
    ret.rotX.y = s;
    ret.rotY.x = -s;
    ret.rotY.y = c;
    ret.translation = offset;
    return ret;
}

// ============================================================================
// Math::Quaternion::FromYawPitch(const Scalar& yawDeg, const Scalar& pitchDeg)
// static, struct return: RCX=hidden ret, RDX=yawDeg, R8=pitchDeg
// retail 0x18007d710  ?FromYawPitch@Quaternion@Math@@SA?AV12@AEBVScalar@2@0@Z
// Half-angle yaw/pitch quaternion (retail term order transliterated
// verbatim; multiplication is commutative under IEEE754 so an operand-order
// difference from FromYawPitchOffset below cannot diverge).
// ============================================================================
Quaternion Quaternion::FromYawPitch(const Scalar& yawDeg, const Scalar& pitchDeg) {
    Quaternion ret;
    float yawHalfRad = yawDeg.v * RtMathConst::kHalf * RtMathConst::kDegToRad;
    float pitchHalfRad = pitchDeg.v * RtMathConst::kHalf * RtMathConst::kDegToRad;
    float sinYaw = RtLibm::Sin(yawHalfRad);
    float sinPitch = RtLibm::Sin(pitchHalfRad);
    float cosYaw = RtLibm::Cos(yawHalfRad);
    float cosPitch = RtLibm::Cos(pitchHalfRad);

    ret.w = cosPitch * cosYaw;
    ret.x = -(cosYaw * sinPitch);
    ret.y = -(sinPitch * sinYaw);
    ret.z = -(cosPitch * sinYaw);
    return ret;
}

// ============================================================================
// Math::Transform::FromYawPitchOffset(const Scalar& yawDeg, const Scalar& pitchDeg, const Vector3& offset)
// static, struct return: RCX=hidden ret, RDX=yawDeg, R8=pitchDeg, R9=offset
// retail 0x18007da70  ?FromYawPitchOffset@Transform@Math@@SA?AV12@AEBVScalar@2@0AEBVVector3@2@@Z
// Same rotation math as FromYawPitch, plus a copied translation.
// ============================================================================
Transform Transform::FromYawPitchOffset(const Scalar& yawDeg, const Scalar& pitchDeg,
                                        const Vector3& offset) {
    Transform ret;
    float yawHalfRad = yawDeg.v * RtMathConst::kHalf * RtMathConst::kDegToRad;
    float pitchHalfRad = pitchDeg.v * RtMathConst::kHalf * RtMathConst::kDegToRad;
    float sinYaw = RtLibm::Sin(yawHalfRad);
    float sinPitch = RtLibm::Sin(pitchHalfRad);
    float cosYaw = RtLibm::Cos(yawHalfRad);
    float cosPitch = RtLibm::Cos(pitchHalfRad);

    ret.rotation.x = -(cosYaw * sinPitch);
    ret.rotation.y = -(sinYaw * sinPitch);
    ret.rotation.z = -(cosPitch * sinYaw);
    ret.rotation.w = cosPitch * cosYaw;
    ret.translation = offset;
    return ret;
}

// ============================================================================
// Math::Quaternion::Quaternion(const Vector3& right, const Vector3& forward, const Vector3& up)
// ctor: RCX=this, RDX=right, R8=forward, R9=up
// retail 0x180021b70  ??0Quaternion@Math@@QEAA@AEBVVector3@1@00@Z
// NOT in Wave 1's assigned function list; ported as a dependency of
// FromDirection (below), and genuinely Math::Quaternion surface. Mike Day /
// Shepperd's robust matrix->quaternion construction from an
// orthonormal basis {right, forward, up} (right = local X axis of the
// resulting rotation's matrix column 0, etc — see AsMatrix's column layout).
// ============================================================================
Quaternion::Quaternion(const Vector3& right, const Vector3& forward, const Vector3& up) {
    float sumX = (right.x + RtMathConst::kOne - forward.y) - up.z;
    if (sumX < 0.0f) sumX = 0.0f;
    float x_ = RtLibm::Sqrt(sumX) * RtMathConst::kHalf;

    float sumY = ((RtMathConst::kOne - right.x) + forward.y) - up.z;
    if (sumY < 0.0f) sumY = 0.0f;
    float y_ = RtLibm::Sqrt(sumY) * RtMathConst::kHalf;

    float sumZ = ((RtMathConst::kOne - right.x) - forward.y) + up.z;
    if (sumZ < 0.0f) sumZ = 0.0f;
    float z_ = RtLibm::Sqrt(sumZ) * RtMathConst::kHalf;

    float sumW = 0.0f;
    float trace = (right.x + RtMathConst::kOne) + forward.y + up.z;
    if (0.0f <= trace) sumW = trace;
    float w_ = RtLibm::Sqrt(sumW) * RtMathConst::kHalf;

    this->x = std::copysignf(x_, up.y - forward.z);
    this->y = std::copysignf(y_, right.z - up.x);
    this->z = std::copysignf(z_, forward.x - right.y);
    this->w = w_;
}

// ============================================================================
// Math::Transform::FromDirection(const Vector3& direction, const Vector3& offset)
// static, struct return: RCX=hidden ret, RDX=direction, R8=offset
// retail 0x180021d70  ?FromDirection@Transform@Math@@SA?AV12@AEBVVector3@2@0@Z
// NOT in Wave 1's assigned function list; ported as a dependency of FromLookAt
// (below), and genuinely Math::Transform surface.
// Builds a right-handed look basis (right = dir x refUp, up = right x dir),
// picking refUp = world -Y or +Y instead of +Z when `direction` is nearly
// parallel to world Z (else refUp = world +Z), to avoid a degenerate cross
// product. `direction` MUST already be a unit vector — retail does not
// re-normalize it here (FromLookAt normalizes before calling in).
// ============================================================================
Transform Transform::FromDirection(const Vector3& direction, const Vector3& offset) {
    Transform ret;
    float dz = direction.z; // dot(direction, world +Z)

    float refY, refZ;
    if (BitsToFloat(kNearParallelPosBits) < dz) {
        refY = RtMathConst::kMinusOne;
        refZ = 0.0f;
    } else if (dz < BitsToFloat(kNearParallelNegBits)) {
        refY = RtMathConst::kOne;
        refZ = 0.0f;
    } else {
        refY = 0.0f;
        refZ = RtMathConst::kOne;
    }
    // refVec = (0, refY, refZ)

    float rightX = direction.y * refZ - direction.z * refY;
    float rightY = direction.z * 0.0f - direction.x * refZ;
    float rightZ = direction.x * refY - direction.y * 0.0f;
    float rightLen = RtLibm::Sqrt(rightY * rightY + rightX * rightX + rightZ * rightZ);
    if (rightLen != 0.0f) {
        float inv = RtMathConst::kOne / rightLen;
        rightX *= inv; rightY *= inv; rightZ *= inv;
    }

    float upX = direction.z * rightY - direction.y * rightZ;
    float upY = direction.x * rightZ - direction.z * rightX;
    float upZ = direction.y * rightX - direction.x * rightY;
    // Term order is load-bearing: retail sums upY^2 + upX^2 + upZ^2 (the
    // decompilation at 0x180021d70 adds them in that order), and float addition
    // is not associative. Summing upZ^2 first instead diverged from the DLL on
    // 7 of 980 calls by ~20 ULP in the resulting quaternion — caught by the
    // Wave 1 diff oracle, and NOT attributable to libm rounding.
    float upLen = RtLibm::Sqrt(upY * upY + upX * upX + upZ * upZ);
    if (upLen != 0.0f) {
        float inv = RtMathConst::kOne / upLen;
        upX *= inv; upY *= inv; upZ *= inv;
    }

    Vector3 right{rightX, rightY, rightZ};
    Vector3 up{upX, upY, upZ};
    ret.rotation = Quaternion(right, direction, up);
    ret.translation = offset;
    return ret;
}

// ============================================================================
// Math::Transform::FromLookAt(const Vector3& from, const Vector3& to)
// static, struct return: RCX=hidden ret, RDX=from, R8=to
// retail 0x18007dbb0  ?FromLookAt@Transform@Math@@SA?AV12@AEBVVector3@2@0@Z
// direction = normalize(to - from); FromDirection(direction, from).
// ============================================================================
Transform Transform::FromLookAt(const Vector3& from, const Vector3& to) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float dz = to.z - from.z;
    float len = RtLibm::Sqrt(dy * dy + dx * dx + dz * dz);
    Vector3 dir{dx, dy, dz};
    if (len != 0.0f) {
        float inv = RtMathConst::kOne / len;
        dir.x = inv * dx; dir.y = inv * dy; dir.z = inv * dz;
    }
    return Transform::FromDirection(dir, from);
}

// ============================================================================
// Math::Transform::Serialize(IO::Serializer&)
// member, void return: RCX=this, RDX=serializer
// retail 0x180059b90  ?Serialize@Transform@Math@@QEAAXAEAVSerializer@IO@@@Z
// Two calls through the encoding's vtable slot +0x28, the bulk float-array
// slot (rw_vtbl::kArrayFloat in src/engine/io/Serializer.h). count=4 serializes
// the Quaternion (this+0x00), count=3 the translation Vector3 (this+0x10).
//
// MUTATES the IO stream; correctness is exercised by save/load parity in
// sa-run per PLAN.md §6 step 6.
// ============================================================================
//
// B3X W0: the "encoding" at Serializer+0x08 is the IO::ReaderWriter and +0x28
// is its `SerializeFloatArray(int n, float* arr, const char* name)` slot
// (src/engine/io/Serializer.h:509), called BY NAME. The old typedef passed
// no `name` (R9 was whatever the caller left); every body in the family
// ignores `name` (Serializer.h's banner), so passing nullptr changes nothing.
void Transform::Serialize(::IO::Serializer& serializer) {
    ::IO::ReaderWriter* const rw = serializer._readerWriter;   // +0x08
    rw->SerializeFloatArray(4, &this->rotation.x, nullptr);     // +0x28  {x, y, z, w}
    rw->SerializeFloatArray(3, &this->translation.x, nullptr);  // +0x28  {x, y, z}
}

// ============================================================================
// Graphics::InterpolatedState<Math::Transform>::Lerp(const Scalar&) const
// retail 0x18001aff0
// ?Lerp@?$InterpolatedState@VTransform@Math@@@Graphics@@QEBA?AVTransform@Math@@AEBVScalar@4@@Z
//
// Defined in src/engine/graphics/InterpolatedState.h, over its callee
// Math::Transform::Lerp (0x18001d690, exported as ordinal 3193) above.
//
// THIS NOTE WAS WRONG AND IS KEPT ONLY TO SAY SO. Wave 1 read the call site as
// "only ONE Transform operand (at +0x1c) plus a bare Scalar (at +0x00) … no
// second Transform argument visible at this call site", and deferred on that
// basis. The second Transform IS visible — it is the `MOV R8,RCX` at
// 0x18001aff9, which passes `this` itself (i.e. the +0x00 half) as
// Transform::Lerp's `to`:
//
//     MOV R9,R8  /  MOV R8,RCX  /  ADD RCX,0x1c  /  MOV RBX,RDX
//     CALL 0x18001d690
//
// Nor was Transform::Lerp's address unfindable: it is the CALL target, and
// `pefile` on the shipped DLL confirms the export
// `?Lerp@Transform@Math@@QEBA?AV12@AEBV12@AEBVScalar@2@@Z` at 0x18001d690.
// The layout worry was real at the time (Wave 6 lane E pinned
// InterpolatedState<Transform> at 0x38 afterwards) — the ARGUMENT reading was
// not, and it is the half that survived four waves unchallenged.
// ============================================================================

// ============================================================================
// Math::Transform::operator*(const Vector3&) const
// member, struct return: RCX=this, RDX=hidden ret, R8=v
// retail 0x18001e200  ??DTransform@Math@@QEBA?AVVector3@1@AEBV21@@Z
//
// Transcribed from the DISASSEMBLY, not the decompilation: Ghidra drops the
// third argument of the inner Quaternion::Rotate call (it cannot track R8
// across a hidden-return-slot call), which makes the decompiled body look as
// though Rotate takes no vector at all. The disassembly shows R8 arriving
// untouched from our own caller, so the rotated vector is `v`.
//
//     ret = this->rotation.Rotate(v) + this->translation
//
// The old signature took an out-pointer that could alias `v` or `self`
// (`vec = transform * vec;` is an ordinary thing for a caller to write, and it
// is what most of our call sites actually do). Retail was safe under that
// aliasing because it consumes v inside Rotate before storing anything. The
// by-value return removes the hazard entirely.
// ============================================================================
Vector3 Transform::operator*(const Vector3& v) const {
    Vector3 ret;
    Vector3 rotated = this->rotation.Rotate(v);
    // Retail's operand order per component (0x18001e21a..0x18001e248): y and z
    // add translation + rotated, x adds rotated + translation. Float addition
    // is commutative, so this is bit-identical either way; kept as retail wrote
    // it so a future reader diffing against the listing sees the same shape.
    ret.y = this->translation.y + rotated.y;
    ret.x = rotated.x + this->translation.x;
    ret.z = this->translation.z + rotated.z;
    return ret;
}

// ============================================================================
// Math::Transform::operator*(const Transform&) const
// member, struct return: RCX=this, RDX=hidden ret, R8=other
// retail 0x18001e3c0  ??DTransform@Math@@QEBA?AV01@AEBV01@@Z
//
// Also transcribed from the disassembly. Note which operand is which — the
// decompilation attributes both inner calls to `in_R8` and reads as though
// `this` is unused, which is wrong. The listing at 0x18001e3e9/0x18001e402
// shows OTHER supplying the rotation on both calls and THIS supplying the
// operands:
//
//     ret.translation = other.rotation.Rotate(this->translation) + other.translation
//     ret.rotation    = other.rotation * this->rotation
//
// Retail also stamps `ret` with the identity Transform on entry (0x18001e3d1)
// before overwriting every field of it. That store is dead in every ordinary
// call, but it is reproduced here: it is observable if either inner call ever
// fails to write, and this port is byte-faithful first.
// ============================================================================
Transform Transform::operator*(const Transform& other) const {
    Transform ret;
    ret.rotation.x = 0.0f;
    ret.rotation.y = 0.0f;
    ret.rotation.z = 0.0f;
    ret.rotation.w = RtMathConst::kOne;
    ret.translation.x = 0.0f;
    ret.translation.y = 0.0f;
    ret.translation.z = 0.0f;

    Vector3 rotated = other.rotation.Rotate(this->translation);
    ret.translation.x = rotated.x + other.translation.x;
    ret.translation.y = other.translation.y + rotated.y;
    ret.translation.z = other.translation.z + rotated.z;

    ret.rotation = other.rotation * this->rotation;

    return ret;
}

}  // namespace Math
