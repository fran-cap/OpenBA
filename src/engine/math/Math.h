#pragma once
// ============================================================================
// src/engine/math/Math.h — the real Math value classes
// ============================================================================
//
// Wave 1 of the Runtime-x64.dll port introduced these as PODs plus a wall of
// `RtMath_*` free functions with the DLL's raw ABI. ENDGAME B.3.1 step 1
// (docs/history/endgame_b/B31_RULES.md) turns them into REAL C++ classes
// under retail's own names in `namespace Math`, with the ported bodies MOVED
// (not rewritten) onto them as members.
//
// Every layout below was read out of Runtime-x64.dll in Ghidra, not guessed;
// the function whose decompilation pins each one is named next to it. Every
// `static_assert` Wave 1 wrote is kept, plus a `sizeof` assert on every type.
//
// THE ABI NOTE THAT GOVERNED WAVE 1 NO LONGER APPLIES
// ---------------------------------------------------
// It used to read: "every one of these classes has a user-declared
// constructor, so MSVC returns it BY MEMORY: the caller passes a hidden return
// slot pointer in RDX (after `this` in RCX) and the callee returns that same
// pointer in RAX ... do NOT write them as C++ methods returning by value and
// hope the ABI lines up." That was true while the callee lived in
// Runtime-x64.dll and the caller was compiled by GCC. The DLL is gone: every
// caller and callee is now compiled by the same compiler, so the
// hidden-return-slot shape is just the C body's shape and it goes (B31_RULES,
// "the one fact that changes the rules since the port"). What still binds is
// the OBJECT LAYOUT, which is why every assert below stays.
//
// The `RtMath_*` free functions still exist, in src/engine/math/*.cpp, as
// one-line shims onto these members, and the `RegisterRuntimeExport` rows
// register the shims. They die in step 2, when the last caller moves.
//
// TRIVIAL COPYABILITY IS LOAD-BEARING AND IS PRESERVED
// ----------------------------------------------------
// These are value types with no ownership. Retail's copy ctor / operator= are
// plain field copies (`Transform`'s is literally seven dword moves at
// 0x180015020), so they are spelled `= default` here rather than as
// user-provided bodies: that reproduces retail exactly AND keeps every type
// trivially copyable, so the tree's memcpy / reinterpret_cast traffic over
// these bytes stays well defined and by-value passing keeps the register shape
// it has today. The retail address of each is a comment on the declaration;
// the shim the registry points at keeps the memcpy body.
//
// DEFAULT CONSTRUCTORS (B31_RULES rule 5)
// ---------------------------------------
// Read out of the disassembly, one by one — NONE of them is a no-op, so none
// is `= default` and none is a forbidden user-provided empty body:
//   Scalar()     0x180021930  stores 0 at +0x00
//   Vector2()    0x180060030  zeroes 8 bytes
//   Vector3()    0x180015070  zeroes 12 bytes
//   Vector4()    0x18001e0a0  zeroes 16 bytes
//   Quaternion() 0x180015050  zeroes x/y/z, stores 1.0f at +0x0c  -> IDENTITY
//   Matrix()     0x18001d770  zeroes all 64 bytes (NOT the identity)
//   Transform()  0x180015080  identity quaternion + zero translation
//   Transform2() 0x18006c8b0  zeroes 0x18 bytes (NOT the identity)
//   Transform3() 0x18005cc20  zeroes 0x30 bytes (NOT the identity)
// They are `constexpr` so a namespace-scope `const Math::Vector3 k{...}` is
// still constant-initialised, which a non-constexpr ctor would silently turn
// into a dynamic initialiser.
//
// A few (float, float, ...) constructors below are marked NOT RETAIL. They
// exist because Wave 1's PODs were aggregates and the tree brace-initialises
// them (`Vector3 v{x, y, z}`); B31_RULES step 1 says such a site is fixed by
// giving the class the matching constructor, not by editing the caller.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cmath>

namespace RtMathConst {
// Float constants the DLL keeps in .rdata and that the ports must reproduce
// bit-for-bit. Addresses are Runtime-x64.dll (image base 0x180000000).
constexpr float kHalf     = 0.5f;             // 0x180161694
constexpr float kOne      = 1.0f;             // 0x18016169c
constexpr float kTwo      = 2.0f;             // 0x1801616d8
constexpr float kMinusOne = -1.0f;            // 0x18016175c
constexpr float kDegToRad = 0.01745329238474369f;  // 0x18016167c (pi/180, float)
// 0x1801617c0 is -0.0f, used as an XOR sign-flip mask, never as an addend.
constexpr uint32_t kSignMask = 0x80000000u;
// Volume::Box / Volume::Rectangle build their empty state from these.
constexpr float kBigPos = 1.0e8f;             // 0x4cbebc20
constexpr float kBigNeg = -1.0e8f;            // 0xccbebc20
}  // namespace RtMathConst

namespace IO { class Serializer; }

namespace Math {

class Scalar;
class Vector2;
class Vector3;
class Vector4;
class Quaternion;
class Matrix;
class Transform;
class Transform2;
class Transform3;

// ============================================================
// Math::Scalar — a single float in a class wrapper.
// Pinned by ?Length@Vector3@Math@@QEBA?AVScalar@2@XZ (0x1800219a0), which
// writes one float through the hidden return slot in RDX.
// ============================================================
class Scalar {
public:
    float v;

    constexpr Scalar() : v(0.0f) {}                 // ??0Scalar@Math@@QEAA@XZ   0x180021930
    constexpr Scalar(float f) : v(f) {}             // ??0Scalar@Math@@QEAA@M@Z  0x180015340

    Scalar Abs() const;                             // 0x1800646b0
    Scalar Lerp(const Scalar& to, const Scalar& t); // 0x1800218c0 (QEAA: non-const in retail)
    Scalar Max(const Scalar& other) const;          // 0x18005ff60
    void SinCos(Scalar& sinOut, Scalar& cosOut) const;   // 0x180078790 (degrees in)
};
static_assert(sizeof(Scalar) == 4, "Math::Scalar must be 4 bytes");
static_assert(offsetof(Scalar, v) == 0x0, "Scalar::v");

// ============================================================
// Math::Vector2 — {x, y}. The global `Vector2` that the exe-side tree spells
// (engine/types/common.h) is an alias of this class.
// ============================================================
class Vector2 {
public:
    float x, y;

    constexpr Vector2() : x(0.0f), y(0.0f) {}                       // 0x180060030
    constexpr Vector2(float x_, float y_) : x(x_), y(y_) {}         // NOT RETAIL (brace-init sites)
    Vector2(const Scalar& x_, const Scalar& y_);                    // 0x180070db0

    void    operator*=(const Vector2& rhs);                         // 0x18005ffd0 (void return in retail)
    Scalar  Dot(const Vector2& rhs) const;                          // 0x18006c840
    Vector2 Max(const Vector2& rhs) const;                          // 0x180060330
    Vector2 Min(const Vector2& rhs) const;                          // 0x1800602e0
    Scalar  Length() const;                                         // 0x18006c880
    Scalar  LengthSquared() const;                                  // 0x18006c860
    Vector2 Normalize() const;                                      // 0x18006c600
    Vector2 Normalize(Scalar& mag) const;                           // 0x18006c7b0
    void    Serialize(IO::Serializer& serializer);                  // 0x18005ff40
};
static_assert(sizeof(Vector2) == 8, "Math::Vector2 must be 8 bytes");
static_assert(offsetof(Vector2, x) == 0x0, "Vector2::x");
static_assert(offsetof(Vector2, y) == 0x4, "Vector2::y");

// ============================================================
// Math::Vector3 — {x, y, z}.
// ============================================================
class Vector3 {
public:
    float x, y, z;

    constexpr Vector3() : x(0.0f), y(0.0f), z(0.0f) {}                        // 0x180015070
    constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}  // 0x1800152d0
    Vector3(const Scalar& x_, const Scalar& y_, const Scalar& z_);            // 0x180015120
    explicit Vector3(const Scalar& s);                                        // 0x180015360 (splat)

    Vector3 Cross(const Vector3& rhs) const;                        // 0x180021b10
    Scalar  Dot(const Vector3& rhs) const;                          // 0x18001dff0
    static Vector3 FromYawPitch(const Scalar& yaw, const Scalar& pitch);      // 0x1800220b0
    float   Heading() const;                                        // 0x18007c900 (plain float in XMM0)
    Scalar  Length() const;                                         // 0x1800219a0
    Vector3 Lerp(const Vector3& to, const Scalar& t) const;         // 0x18001d450
    Vector3 Min(const Vector3& rhs) const;                          // 0x180015280
    Vector3 Normalize(Scalar& mag) const;                           // 0x180021a20
    Vector3 Normalize() const;                                      // 0x180021ae0
    bool    operator==(const Vector3& rhs) const;                   // 0x1800150a0
    void    Serialize(IO::Serializer& serializer);                  // 0x1800150e0
};
static_assert(sizeof(Vector3) == 12, "Math::Vector3 must be 12 bytes");
static_assert(offsetof(Vector3, x) == 0x0, "Vector3::x");
static_assert(offsetof(Vector3, y) == 0x4, "Vector3::y");
static_assert(offsetof(Vector3, z) == 0x8, "Vector3::z");

// ============================================================
// Math::Vector4 — {x, y, z, w}.
// ============================================================
class Vector4 {
public:
    float x, y, z, w;

    constexpr Vector4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}     // 0x18001e0a0
    constexpr Vector4(float x_, float y_, float z_, float w_)       // NOT RETAIL (brace-init sites)
        : x(x_), y(y_), z(z_), w(w_) {}
    explicit Vector4(const Scalar& s);                              // 0x18001d750 (splat)
    Vector4(const Vector3& xyz, const Scalar& w_);                  // 0x18001e070

    void Serialize(IO::Serializer& serializer);                     // 0x18001d360
};
static_assert(sizeof(Vector4) == 16, "Math::Vector4 must be 16 bytes");
static_assert(offsetof(Vector4, x) == 0x0, "Vector4::x");
static_assert(offsetof(Vector4, y) == 0x4, "Vector4::y");
static_assert(offsetof(Vector4, z) == 0x8, "Vector4::z");
static_assert(offsetof(Vector4, w) == 0xC, "Vector4::w");

// ============================================================
// Math::Quaternion — {x, y, z, w}, w LAST.
// Pinned by ??0Quaternion@Math@@QEAA@MMMM@Z (0x18001e260): param 1->+0x00,
// 2->+0x04, 3->+0x08, 4->+0x0c.
// ============================================================
class Quaternion {
public:
    float x, y, z, w;

    // 0x180015050 — zeroes x/y/z and writes 1.0f at +0x0c, i.e. the IDENTITY.
    constexpr Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(RtMathConst::kOne) {}
    Quaternion(float x_, float y_, float z_, float w_);             // 0x18001e260
    // Mike Day / Shepperd construction from an orthonormal basis.  0x180021b70
    Quaternion(const Vector3& right, const Vector3& forward, const Vector3& up);

    Quaternion operator*(const Quaternion& other) const;            // 0x18001e280
    Vector3    Rotate(const Vector3& v) const;                      // 0x18001e140
    Quaternion Lerp(const Quaternion& other, const Scalar& t) const;// 0x18001d500
    static Quaternion FromYawPitch(const Scalar& yawDeg, const Scalar& pitchDeg);  // 0x18007d710
};
static_assert(sizeof(Quaternion) == 16, "Math::Quaternion must be 16 bytes");
static_assert(offsetof(Quaternion, x) == 0x0, "Quaternion::x");
static_assert(offsetof(Quaternion, y) == 0x4, "Quaternion::y");
static_assert(offsetof(Quaternion, z) == 0x8, "Quaternion::z");
static_assert(offsetof(Quaternion, w) == 0xC, "Quaternion::w");

// ============================================================
// Math::Matrix — 16 floats, column-major storage in the sense used by
// Transform::AsMatrix (0x18001e5f0): the rotation basis lands in
// m[0..2], m[4..6], m[8..10]; the translation in m[12..14]; m[3]/m[7]/m[11]
// are cleared and m[15] is 1.0f.
// ============================================================
class Matrix {
public:
    float m[16];

    // 0x18001d770 — zeroes all sixteen cells. NOT the identity; Identity() is.
    constexpr Matrix() : m{} {}
    // NOT RETAIL. Wave 1's Matrix was an aggregate and one site brace-initialises
    // the cell array directly — `const Matrix kZUpToYUp = {{ ...16 floats... }}`
    // in src/engine/graphics/renderer.cpp. B31_RULES step 1: a brace-init site
    // that breaks is fixed by giving the class the matching constructor, not by
    // editing the caller.
    constexpr Matrix(const float (&cells)[16])
        : m{cells[0],  cells[1],  cells[2],  cells[3],
            cells[4],  cells[5],  cells[6],  cells[7],
            cells[8],  cells[9],  cells[10], cells[11],
            cells[12], cells[13], cells[14], cells[15]} {}
    Matrix(const Vector4& c0, const Vector4& c1,                    // 0x18001e540
           const Vector4& c2, const Vector4& c3);

    Matrix  operator*(const Matrix& other) const;                   // 0x18001d980
    Vector4 operator*(const Vector4& v) const;                      // 0x18007d030
    // Sixteen `==` comparisons with retail's short-circuit structure. Retail
    // ships no `operator==` counterpart, so this is not spelled as `!(*this ==
    // other)`. (ENDGAME B.3.1 step 2: body moved out of
    // src/engine/video/context.cpp, where it was a C free function.)
    bool    operator!=(const Matrix& other) const;                  // 0x18001d850
    Matrix  Inverse() const;                                        // 0x1800bb310
    static Matrix Identity();                                       // 0x18002c030
};
static_assert(sizeof(Matrix) == 64, "Math::Matrix must be 64 bytes");
static_assert(offsetof(Matrix, m) == 0x0, "Matrix::m");

// ============================================================
// Math::Transform — a rigid transform stored as quaternion + translation.
// Pinned by ??0Transform@Math@@QEAA@XZ (0x180015080): zeroes +0x00..+0x08,
// writes 1.0f at +0x0c (quaternion w) and zeroes +0x10..+0x1b; and by the copy
// ctor (0x180015020) which copies exactly seven dwords.
// ============================================================
class Transform {
public:
    Quaternion rotation;      // +0x00
    Vector3    translation;   // +0x10

    constexpr Transform() : rotation(), translation() {}            // 0x180015080
    // Retail's copy ctor (0x180015020) and operator= (0x180014fd0) are plain
    // seven-dword field copies; `= default` is that, and keeps the type
    // trivially copyable. The registered shims keep the memcpy body.
    Transform(const Transform&) = default;                          // 0x180015020
    Transform& operator=(const Transform&) = default;               // 0x180014fd0

    Vector3    operator*(const Vector3& v) const;                   // 0x18001e200
    Transform  operator*(const Transform& other) const;             // 0x18001e3c0
    // ENDGAME B.3.1 step 2: body moved out of
    // src/engine/graphics/wave9_lane_gfx.cpp.
    Transform  Lerp(const Transform& other, const Scalar& t) const; // 0x18001d690
    Matrix     AsMatrix() const;                                    // 0x18001e5f0
    Transform3 AsTransform3() const;                                // 0x18001fcd0
    Transform  Inverse() const;                                     // 0x18005db50
    void       Serialize(IO::Serializer& serializer);               // 0x180059b90

    static Transform Identity();                                    // 0x18001fab0
    static Transform FromDirection(const Vector3& direction, const Vector3& offset);  // 0x180021d70
    static Transform FromLookAt(const Vector3& from, const Vector3& to);              // 0x18007dbb0
    static Transform FromYawPitchOffset(const Scalar& yawDeg, const Scalar& pitchDeg, // 0x18007da70
                                        const Vector3& offset);
};
static_assert(sizeof(Transform) == 0x1c, "Math::Transform must be 0x1c bytes");
static_assert(offsetof(Transform, rotation) == 0x0, "Transform::rotation");
static_assert(offsetof(Transform, translation) == 0x10, "Transform::translation @ +0x10");

// ============================================================
// Math::Transform2 — 2x2 rotation matrix + 2D translation.
// Pinned by ??0Transform2@Math@@QEAA@AEBVScalar@1@AEBVVector2@1@@Z
// (0x18007dca0): +0x00 = cos, +0x04 = sin, +0x08 = -sin, +0x0c = cos,
// +0x10 = the Vector2 offset (written first, as one qword).
// ============================================================
class Transform2 {
public:
    Vector2 rotX;             // +0x00  { cos,  sin }
    Vector2 rotY;             // +0x08  { -sin, cos }
    Vector2 translation;      // +0x10

    // 0x18006c8b0 — zeroes all 0x18 bytes. NOT the identity rotation.
    constexpr Transform2() : rotX(), rotY(), translation() {}
    Transform2(const Scalar& angleDeg, const Vector2& offset);      // 0x18007dca0

    Transform2 operator*(const Transform2& other) const;            // 0x180078650
    Vector2    operator*(const Vector2& v) const;                   // 0x18006c8e0

    static Transform2 FromYawOffset(const Scalar& yawDeg, const Vector2& offset);  // 0x1800787f0
};
static_assert(sizeof(Transform2) == 0x18, "Math::Transform2 must be 0x18 bytes");
static_assert(offsetof(Transform2, rotX) == 0x0, "Transform2::rotX");
static_assert(offsetof(Transform2, rotY) == 0x8, "Transform2::rotY");
static_assert(offsetof(Transform2, translation) == 0x10, "Transform2::translation @ +0x10");

// ============================================================
// Math::Transform3 — full 3x3 basis + translation.
// Pinned by ?AsMatrix@Transform3@Math@@QEBA?AVMatrix@2@XZ (0x1800346e0):
// matrix[0,1,2] <- this[+0x00,+0x04,+0x08]; matrix[4,5,6] <- +0x0c,+0x10,+0x14;
// matrix[8,9,10] <- +0x18,+0x1c,+0x20; matrix[12,13,14] <- +0x24,+0x28,+0x2c.
// So the three basis vectors are contiguous Vector3s and translation follows.
// ============================================================
class Transform3 {
public:
    Vector3 axisX;            // +0x00
    Vector3 axisY;            // +0x0c
    Vector3 axisZ;            // +0x18
    Vector3 translation;      // +0x24

    // 0x18005cc20 — zeroes all 0x30 bytes. NOT the identity basis.
    constexpr Transform3() : axisX(), axisY(), axisZ(), translation() {}
    // 0x18001dee0 is a straight 0x30-byte field copy; see Transform above for
    // why this is `= default` rather than a user-provided memcpy body.
    Transform3(const Transform3&) = default;                        // 0x18001dee0
    Transform3& operator=(const Transform3&) = default;

    Vector3    operator*(const Vector3& v) const;                   // 0x18001fbc0
    // ENDGAME B.3.1 step 2: body moved out of
    // src/engine/graphics/imagebuffer_derived.cpp, where it lived as a
    // non-slot helper for ReflectionImageBuffer::Render.
    Transform3 operator*(const Transform3& other) const;            // 0x180022680
    Matrix     AsMatrix() const;                                    // 0x1800346e0
    Transform3 Inverse() const;                                     // 0x180060040
    Vector3    Rotate(const Vector3& v) const;                      // 0x18001e470

    static Transform3 Identity();                                   // 0x1800347f0
    // NOTE the argument roles, which the export name does not give away:
    // `position` is the eye (it lands in the translation row) and `target` is
    // what is looked at. See src/engine/math/from_lookat.cpp.
    static Transform3 FromLookAt(const Vector3& position, const Vector3& target);  // 0x180064750
};
static_assert(sizeof(Transform3) == 0x30, "Math::Transform3 must be 0x30 bytes");
static_assert(offsetof(Transform3, axisX) == 0x0, "Transform3::axisX");
static_assert(offsetof(Transform3, axisY) == 0x0c, "Transform3::axisY @ +0x0c");
static_assert(offsetof(Transform3, axisZ) == 0x18, "Transform3::axisZ @ +0x18");
static_assert(offsetof(Transform3, translation) == 0x24, "Transform3::translation @ +0x24");


// ============================================================================
// Inline definitions (ENDGAME B.3.1 closing pass, performance)
// ============================================================================
//
// Two caller batches replaced hand-inlined copies of the Vector3 ctor / splat /
// Dot in the pathfinding and citizen-AI hot paths with out-of-line class calls.
// These are the members small enough that a call frame costs more than the
// arithmetic, so they live here instead of in src/engine/math/*.cpp. The
// BODIES ARE THE ONES THAT WERE THERE, moved verbatim — same statements, same
// operand order, same retail address on each — so this is a linkage change and
// nothing else. Anything that calls out (RtLibm::Sqrt/Sin/Cos/Atan2, the
// Serialize vtable hop, the Normalize/Inverse/AsMatrix/Lerp families) stays
// out of line.

inline Scalar Scalar::Abs() const {
    Scalar ret;
    ret.v = std::fabs(this->v);
    return ret;
}

inline Scalar Scalar::Lerp(const Scalar& to, const Scalar& t) {
    Scalar ret;
    ret.v = this->v + (to.v - this->v) * t.v;
    return ret;
}

inline Scalar Scalar::Max(const Scalar& other) const {
    Scalar ret;
    ret.v = (other.v < this->v) ? this->v : other.v;
    return ret;
}

inline Vector2::Vector2(const Scalar& x_, const Scalar& y_) {
    this->x = x_.v;
    this->y = y_.v;
}

inline void Vector2::operator*=(const Vector2& rhs) {
    this->x *= rhs.x;
    this->y *= rhs.y;
}

inline Scalar Vector2::Dot(const Vector2& rhs) const {
    Scalar ret;
    ret.v = this->x * rhs.x + this->y * rhs.y;
    return ret;
}

inline Vector2 Vector2::Max(const Vector2& rhs) const {
    Vector2 ret;
    ret.x = (this->x <= rhs.x) ? rhs.x : this->x;
    ret.y = (this->y <= rhs.y) ? rhs.y : this->y;
    return ret;
}

inline Vector2 Vector2::Min(const Vector2& rhs) const {
    Vector2 ret;
    ret.x = (rhs.x <= this->x) ? rhs.x : this->x;
    ret.y = (rhs.y <= this->y) ? rhs.y : this->y;
    return ret;
}

inline Scalar Vector2::LengthSquared() const {
    Scalar ret;
    ret.v = this->x * this->x + this->y * this->y;
    return ret;
}

inline Vector3::Vector3(const Scalar& x_, const Scalar& y_, const Scalar& z_) {
    this->x = x_.v;
    this->y = y_.v;
    this->z = z_.v;
}

inline Vector3::Vector3(const Scalar& s) {
    this->x = s.v;
    this->y = s.v;
    this->z = s.v;
}

inline Vector3 Vector3::Cross(const Vector3& rhs) const {
    Vector3 ret;
    float x_ = this->y * rhs.z - this->z * rhs.y;
    float y_ = rhs.x * this->z - this->x * rhs.z;
    float z_ = this->x * rhs.y - rhs.x * this->y;
    ret.x = x_;
    ret.y = y_;
    ret.z = z_;
    return ret;
}

inline Scalar Vector3::Dot(const Vector3& rhs) const {
    Scalar ret;
    ret.v = rhs.y * this->y + rhs.x * this->x + rhs.z * this->z;
    return ret;
}

inline Vector3 Vector3::Lerp(const Vector3& to, const Scalar& t) const {
    Vector3 ret;
    float tt = t.v;
    ret.x = this->x + tt * (to.x - this->x);
    ret.y = this->y + (to.y - this->y) * tt;
    ret.z = this->z + (to.z - this->z) * tt;
    return ret;
}

inline Vector3 Vector3::Min(const Vector3& rhs) const {
    Vector3 ret;
    ret.x = (rhs.x <= this->x) ? rhs.x : this->x;
    ret.y = (rhs.y <= this->y) ? rhs.y : this->y;
    ret.z = (rhs.z <= this->z) ? rhs.z : this->z;
    return ret;
}

inline bool Vector3::operator==(const Vector3& rhs) const {
    bool ours = this->x == rhs.x && this->y == rhs.y && this->z == rhs.z;
    return ours;
}

inline Vector4::Vector4(const Scalar& s) {
    this->x = s.v;
    this->y = s.v;
    this->z = s.v;
    this->w = s.v;
}

inline Vector4::Vector4(const Vector3& xyz, const Scalar& w_) {
    this->x = xyz.x;
    this->y = xyz.y;
    this->z = xyz.z;
    this->w = w_.v;
}

inline Quaternion::Quaternion(float x_, float y_, float z_, float w_) {
    this->x = x_;
    this->y = y_;
    this->w = w_;   // retail writes w (this+0xc) before z (this+8); independent
    this->z = z_;   // field stores, order is inconsequential here.
}

inline Transform Transform::Identity() {
    Transform ret;
    ret.rotation.x = 0.0f;
    ret.rotation.y = 0.0f;
    ret.rotation.z = 0.0f;
    ret.rotation.w = RtMathConst::kOne;
    ret.translation.x = 0.0f;
    ret.translation.y = 0.0f;
    ret.translation.z = 0.0f;
    return ret;
}

inline Transform3 Transform3::Identity() {
    Transform3 ret;
    ret.axisX = {RtMathConst::kOne, 0.0f, 0.0f};
    ret.axisY = {0.0f, RtMathConst::kOne, 0.0f};
    ret.axisZ = {0.0f, 0.0f, RtMathConst::kOne};
    ret.translation = {0.0f, 0.0f, 0.0f};
    return ret;
}

}  // namespace Math

// ============================================================================
// Transitional global aliases (ENDGAME B.3.1 step 1).
//
// Wave 1 declared these types at GLOBAL scope, and ~800 sites across code/,
// engine/, runtime/, standalone/ and src/render/dx11/ still spell them that way. The
// aliases keep every one of them compiling while the classes move to retail's
// namespace; step 2 converts the callers and deletes the aliases.
//
// Vector2/Vector3/Vector4 are aliased in engine/types/common.h instead — that
// is the header the exe-side tree includes for them, and it is where they were
// defined before this batch.
// ============================================================================
using Scalar     = Math::Scalar;
using Quaternion = Math::Quaternion;
using Matrix     = Math::Matrix;
using Transform  = Math::Transform;
using Transform2 = Math::Transform2;
using Transform3 = Math::Transform3;

#include "../global_types.h"   // the Vector2/Vector3/Vector4 aliases
