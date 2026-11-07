#pragma once
// ============================================================================
// src/engine/volume/Volume.h — binary layouts for the Volume namespace
// ============================================================================
//
// Wave 1, Lane 3 of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2/§6).
// Every layout below was read out of Runtime-x64.dll in Ghidra, not guessed;
// the function whose decompilation pins each one is named next to it.
//
// Vector2 / Vector3 / Vector4 are NOT redefined here: they already exist in
// engine/types/common.h with the same layout and are shared by the exe port.
// Scalar/Transform3 come from src/engine/math/Math.h (Lane 1/2 of this wave).
//
// ABI NOTE — see src/engine/math/Math.h for the full explanation. Every one
// of these classes has a user-declared constructor, so MSVC returns it BY
// MEMORY: the caller passes a hidden return-slot pointer in RDX (after `this`
// in RCX) and the callee returns that same pointer in RAX. Ports here are
// plain free functions with the hidden slot as an explicit parameter.
//
// ----------------------------------------------------------------------------
// THE <windows.h> NAME CLASH, AND WHY THIS HEADER IS NOW ORDER-FREE
// ----------------------------------------------------------------------------
// `Box`, `Rectangle` and `Frustum` are declared at GLOBAL scope here, matching
// retail's `Volume::` namespace only in spirit (see the note at the bottom).
// `<wingdi.h>`, pulled in by `<windows.h>` and therefore by `standalone/resolve.h`,
// declares a global FUNCTION `Rectangle(HDC,int,int,int,int)`. A function name and
// a class name may coexist in one scope, but the function HIDES the class name for
// ordinary lookup — so a plain `sizeof(Rectangle)` after `<windows.h>` is a hard
// error ("expected primary-expression"), and for a long time roughly forty caller
// files carried an ordering workaround: "runtime headers FIRST, before resolve.h".
//
// ENDGAME B.3.5 (b35-close) fixed it AT THE SOURCE instead. Every place in this
// header that names one of the three classes uses the ELABORATED type specifier
// (`struct Rectangle`, `struct Box`, `struct Frustum`), which is exactly what C++
// provides for a class name hidden by a function: elaborated-type-specifier lookup
// ignores non-type names ([basic.lookup.elab]). This header therefore compiles
// with `<windows.h>` included BEFORE it or after it, and no caller needs an
// include order for THIS header's sake any more.
//
// WHAT IS NOT FIXED BY THIS, and what the ~40 existing comments still describe:
// a CALLER that itself writes `Rectangle` unelaborated after `<windows.h>` hits the
// same hiding, in its own translation unit, and no change here can help it. Such a
// caller must either spell `struct Rectangle` or keep including this header first.
// The existing ordering comments in those files were deliberately left in place:
// they are still true statements about their own TUs and they are harmless.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "../global_types.h"   // Vector2, Vector3, Vector4
#include "../math/Math.h"                   // Scalar, Transform3
#include "../system/EnumFlags.h"            // OPENBA_ENUM_FLAGS (FrustumPlanes)

// ============================================================
// Volume::Box — axis-aligned bounding box.
// Pinned by ??0Box@Volume@@QEAA@XZ (0x180011ea0): default ctor fills `min`
// with 0x4cbebc20 (= 1.0e8f, RtMathConst::kBigPos) and `max` with 0xccbebc20
// (= -1.0e8f, RtMathConst::kBigNeg), so the box is empty and the first
// Extend() call always wins on both ends.
// ============================================================
struct Box {
    Vector3 min;   // +0x00
    Vector3 max;   // +0x0c
};
static_assert(sizeof(struct Box) == 0x18, "Volume::Box must be 0x18 (24) bytes");
static_assert(offsetof(struct Box, max) == 0x0c, "Volume::Box::max @ +0x0c");

// ============================================================
// Volume::Rectangle — axis-aligned 2D rectangle.
// Pinned by ??0Rectangle@Volume@@QEAA@XZ (0x18005ddd0), same empty-box
// sentinel values as Box: min = +1.0e8f, max = -1.0e8f.
// ============================================================
struct Rectangle {
    Vector2 min;   // +0x00
    Vector2 max;   // +0x08
};
static_assert(sizeof(struct Rectangle) == 0x10, "Volume::Rectangle must be 0x10 (16) bytes");
static_assert(offsetof(struct Rectangle, max) == 0x08, "Volume::Rectangle::max @ +0x08");

// ============================================================
// Volume::Frustum — six clip planes plus a projection-kind flag.
//
// Pinned by ??0Frustum@Volume@@QEAA@AEBVScalar@Math@@000@Z (0x1800fa1e0, the
// 4-Scalar near/far/fovX/fovY perspective ctor) and
// ??0Frustum@Volume@@QEAA@_NAEBVScalar@Math@@11111@Z (0x1800fa3d0, the 6-arg
// ctor that takes the flag explicitly as its first parameter). Both zero the
// whole object (0x00..0x5f) before filling it in.
//
// Each plane is a Vector4 {x,y,z,w} where (x,y,z) is the outward plane normal
// and w is the plane's `d` term, tested as dot(normal, p) - d. Plane order as
// built by the perspective ctor (forward = +Y, right = +X, up = +Z; apex at
// the origin for the four side planes, whose w is always 0):
//   [0] near  {0,  1,        0,        near}
//   [1] far   {0, -1,        0,       -far}
//   [2] side  { cosHalfFovX,  sinHalfFovX, 0, 0}
//   [3] side  {-cosHalfFovX,  sinHalfFovX, 0, 0}
//   [4] side  {0,  sinHalfFovY, -cosHalfFovY, 0}
//   [5] side  {0,  sinHalfFovY,  cosHalfFovY, 0}
//
// +0x60 is a single byte, read/written as a bool by every non-ctor method
// (operator*, Contains via a straight copy, GetCorners, GetScreenDirection).
// It is FALSE for the perspective ctor (0x1800fa1e0) and comes directly from
// the caller's first argument in the 6-Scalar ctor (0x1800fa3d0): when false
// that ctor normalizes four side-plane directions computed from slope ratios
// (an asymmetric/off-axis PERSPECTIVE frustum); when true it copies the six
// scalar args straight into the plane fields with no normalization — i.e. an
// axis-aligned ORTHOGRAPHIC volume, where the plane "d" terms already sit at
// unit-normal offsets. GetCorners/GetScreenDirection branch on the same byte:
// the false (perspective) path divides the near-plane offset through the
// side planes' ratios to find the near-plane corners then normalizes toward
// the far plane; the true (orthographic) path reads the corner coordinates
// straight out of the plane "d" terms with no perspective divide. So this
// byte means "is this frustum orthographic" (true = orthographic, false =
// perspective); named accordingly below.
//
// sizeof: fields run through +0x60 inclusive (0x61 bytes used); the struct's
// natural alignment is 4 (from the Vector4/float members), so the compiler
// pads the tail to a multiple of 4 -> 0x64 (100) bytes. No retail call site
// was found that depends on any byte beyond +0x60, so the pad bytes carry no
// meaning; they exist only to satisfy alignment.
// ============================================================
//
// ENDGAME B7 (CTOR, D6): the two retail ctors are REAL ctors (frustum.cpp); the
// `RtVolume_Frustum_Ctor_Perspective` / `_General` doors are deleted with their
// last caller. The "zero the whole object first" above is the six `Vector4()`
// member ctors (0x18001e0a0, retail's zeroing ctor: the twelve qword zero stores
// 0x1800fa212..0x1800fa23d / 0x1800fa3e5..0x1800fa410) plus the flag's
// mem-initialiser (the byte store at +0x60: 0 at 0x1800fa241, the argument at
// 0x1800fa41c), so the planes are default-initialised in the mem-initialiser and
// written in the body, as retail. The perspective ctor never stores the flag
// again after that zero. The defaulted ctor is the implicit one the struct had
// (planes zeroed by Vector4(), flag untouched); the tree's holders of a Frustum
// member (Camera, PlayerController) use it.
struct Frustum {
    Frustum() = default;
    // ??0Frustum@Volume@@QEAA@AEBVScalar@Math@@000@Z  0x1800fa1e0
    Frustum(const Math::Scalar& nearZ, const Math::Scalar& farZ,
            const Math::Scalar& fovX, const Math::Scalar& fovY);
    // ??0Frustum@Volume@@QEAA@_NAEBVScalar@Math@@11111@Z  0x1800fa3d0
    Frustum(bool isOrthographic, const Math::Scalar& nearZ, const Math::Scalar& farZ,
            const Math::Scalar& edgeA, const Math::Scalar& edgeB,
            const Math::Scalar& edgeC, const Math::Scalar& edgeD);

    Vector4 planes[6];       // +0x00 .. +0x5f, one Vector4 {x,y,z,d} per plane
    bool    isOrthographic;  // +0x60
    // +0x61..+0x63: compiler alignment padding, not read by any known caller.
};
static_assert(offsetof(Frustum, planes) == 0x0, "Frustum::planes");
static_assert(sizeof(struct Frustum) == 0x64, "Volume::Frustum must be 0x64 (100) bytes");
static_assert(offsetof(struct Frustum, isOrthographic) == 0x60, "Volume::Frustum::isOrthographic @ +0x60");

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

// ---- volume/box.cpp
Box* RtVolume_Box_Ctor(Box* self);  // 0x180011ea0
Box* RtVolume_Box_Assign(Box* self, const Box* other);  // 0x180011e70
Box* RtVolume_Box_Extend(const Box* self, Box* ret, const Math::Vector3* p);  // 0x180011ff0
void RtVolume_Box_GetCorners(const Box* self, Math::Vector3* out);  // 0x1800121b0
bool RtVolume_Box_Intersect(const Box* self, const Box* other);  // 0x180012160
// ---- volume/rectangle.cpp
struct Rectangle* RtVolume_Rectangle_Ctor(struct Rectangle* self);  // 0x18005ddd0
// ---- volume/frustum.cpp
// The plane mask Frustum::Contains (0x1800f8e80) takes and returns: bit i = plane
// i is still straddled and must be tested (planes[] order from the frustum
// builders, frustum.cpp). The functions keep `unsigned int`: a call passes
// Bits(FrustumPlanes::All) and compares the result with Bits(...).
enum class FrustumPlanes : uint32_t {
    Near    = 0x01,   // planes[0]
    Far     = 0x02,   // planes[1]
    XLow    = 0x04,   // planes[2]
    XHigh   = 0x08,   // planes[3]
    ZLow    = 0x10,   // planes[4]
    ZHigh   = 0x20,   // planes[5]
    All     = 0x3F,   // test all six planes
    Outside = 0x40,   // the result when the box is entirely outside one plane (culled)
};
OPENBA_ENUM_FLAGS(FrustumPlanes)
// A scene cull-stack record packs two plane masks: the view cull's in the low 16
// bits, the shadow cull's in the high 16. Both full:
constexpr uint32_t kFrustumAllPlanesBoth = 0x003F003Fu;

Frustum* RtVolume_Frustum_MulTransform3(const Frustum* self, Frustum* ret, const Math::Transform3* t);  // 0x1800f9310
unsigned int RtVolume_Frustum_Contains(const Frustum* self, const Box* box, unsigned int mask);  // 0x1800f8e80
void RtVolume_Frustum_GetCorners(const Frustum* self, Math::Vector3* out);  // 0x1800f9850
Math::Vector3* RtVolume_Frustum_GetScreenDirection(const Frustum* self, Math::Vector3* ret, float u, float v);  // 0x1800f9c80
Math::Matrix* RtVolume_Frustum_Projection(const Frustum* self, Math::Matrix* ret, bool zeroToOneDepth);  // 0x1800f9dc0
// ==== end B3R W0-P0c ====
