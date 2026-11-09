// ============================================================================
// src/engine/graphics/sun_camera.cpp — Graphics::SunLight::GetSunCamera
// ============================================================================
//
//   ?GetSunCamera@SunLight@Graphics@@QEBA?AVCamera@2@AEAVRenderer@2@@Z
//   retail 0x180107010, ordinal 2951, 3775 bytes — the largest body in BATCH 4.
//
// Builds the ORTHOGRAPHIC camera the shadow map is rendered from: fit a box
// around the view frustum's near corners and a 1000-unit extrusion of them,
// centre it, pull the eye 128 units back along the sun direction, and SNAP that
// eye onto a shadow-map texel grid so the shadow edges do not crawl when the
// view camera moves.
//
// ---------------------------------------------------------------------------
// THE HIDDEN RETURN SLOT — and it is NOT in RCX
// ---------------------------------------------------------------------------
// This is a non-static member returning `Graphics::Camera` (0xd8 bytes) by
// value. MSVC's x64 ABI puts `this` FIRST and the return slot SECOND:
//
//     RCX = this (SunLight*)   RDX = Camera* return slot   R8 = Renderer&
//
// Read off the disassembly, not the decompiled C: `MOV R12,RDX` / `MOV R13,RCX`
// at 0x180107079, then `MOV RCX,R12` immediately before `CALL 0x1800fe960`
// (Camera::Camera) and `MOV RAX,R12` as the return value. Ghidra's C prints
// `__thiscall GetSunCamera(SunLight *this, Renderer *param_1)` with an unnamed
// `in_R8`, which gets BOTH the slot position and the argument count wrong; a
// typedef copied from it writes 0xd8 bytes over the Renderer.
// (reference_hidden_retslot_wild_write.md, reference_ghidra_drops_r8_static_lookat.md)
//
// ---------------------------------------------------------------------------
// WHICH DRAW GROUP, AND WHICH CAMERA INSIDE IT
// ---------------------------------------------------------------------------
// `MOV R9,[R8+0x10]` then every read at [R9 + 0x08 .. 0x98] — Renderer+0x10 is
// the draw-group Array and there is NO `* 0x1f0`, so this is ALWAYS draw group
// ZERO, and the camera it reads is the one at group+0x08 (frustum +0x08..+0x6b,
// cameraToWorld +0x6c..+0x9b), not the cullCamera at group+0xe0. A draw group
// therefore holds two Cameras exactly 0xd8 apart, which is the independent
// confirmation of sizeof(Camera) that src/engine/graphics/Camera.h derives
// from the other two embeddings.
//
// ---------------------------------------------------------------------------
// A SHIPPED BUG THAT MUST BE PRESERVED
// ---------------------------------------------------------------------------
// The far-corner pass writes point `pts[2i+1]`, then clamps its Z into
// [-2, 15] — but it reads the value to clamp from `pts[i].z`, NOT
// `pts[2i+1].z`. In the disassembly the store cursor is RBX (stride 0x18) and
// the clamp source is `[RBP + RDI - 0x58]` with RDI = i*12, i.e. a stride-12
// walk over the same array. For i == 0 the two coincide; for i >= 1 the clamp
// takes the PREVIOUS iteration's already-clamped Z. It is a `p[i]` where the
// source clearly meant `p[i*2+1]`.
//
// It is deterministic, it is what every shipped frame of the game has done,
// and "fixing" it here would move shadow frusta relative to retail. Ported as
// written, marked at the site.
//
// ---------------------------------------------------------------------------
// DEAD CODE IN RETAIL, NOT PORTED
// ---------------------------------------------------------------------------
// The point loop also maintains six running min/max accumulators (seeded from
// 0x180161740 = +1e8 and 0x18016177c = -1e8). NOTHING after the loop reads any
// of them — the box they describe is recomputed from the centroid and a radius
// instead. They are leftovers of an earlier fit. Omitted, because a discarded
// float computation has no observable effect: nothing here reads errno or the
// FP status word, and none of the inputs can trap.
//
// ---------------------------------------------------------------------------
// CONSTANTS, lifted as raw bits (reference_crt_coefficients_must_be_lifted...)
// ---------------------------------------------------------------------------
//   0x180161688 = 0x3e000000     0.125f   (1/8: eight points averaged)
//   0x18016169c = 0x3f800000     1.0f
//   0x1801616d8 = 0x40000000     2.0f
//   0x1801616f8 = 0x41700000    15.0f     upper Z clamp
//   0x18016172c = 0x447a0000  1000.0f     corner extrusion length
//   0x180161768 = 0xc0000000    -2.0f     lower Z clamp AND the ground plane
//   0x180161770 = 0xc3000000  -128.0f     eye pull-back along the sun direction
//   0x180161774 = 0xc3800000  -256.0f     the far plane's w
//   0x1801617b0 = 7fffffffffffffff x2     the double ABS mask
//   0x1801617c0 = 80000000 x4             the float sign-flip mask
// ============================================================================

#include "rt_libm.h"
#include "math/Math.h"
#include "volume/Volume.h"
#include "graphics/Camera.h"
#include "graphics/SunLight.h"
#include "video/ImageBuffer.h"   // B.4: Video::ImageBuffer::Width
#include "../crt/msvc_libm.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace {

constexpr float kZero      =     0.0f;
constexpr float kOne       =     1.0f;
constexpr float kTwo       =     2.0f;
constexpr float kEighth    =     0.125f;
constexpr float kClampHi   =    15.0f;
constexpr float kClampLo   =    -2.0f;
constexpr float kExtrude   =  1000.0f;
constexpr float kEyePull   =  -128.0f;
constexpr float kFarW      =  -256.0f;

inline float NegF(float v) {                      // XORPS against 0x1801617c0
    uint32_t b; std::memcpy(&b, &v, 4); b ^= 0x80000000u;
    float r; std::memcpy(&r, &b, 4); return r;
}
// Retail widens to double, masks the sign with 0x1801617b0, narrows back.
// Exact for every float, so this is the same function written once.
inline float AbsF(float v) {
    uint32_t b; std::memcpy(&b, &v, 4); b &= 0x7fffffffu;
    float r; std::memcpy(&r, &b, 4); return r;
}

}  // namespace

// ############################################################################
// ENDGAME B.3 step 5 (block F) — THE BODY MOVED ONTO THE MEMBER
// ############################################################################
//
// `src/engine/graphics/SunLight.h` now DECLARES
// `Camera SunLight::GetSunCamera(Renderer&) const` — the one line block E
// recorded as a comment and left for this block, because a by-value return needs
// the COMPLETE type and `Graphics::Camera` was still a POD. The body below is
// that member; the registered door keeps its name, both registrations and
// retail's `(this, ret, renderer)` ABI, and delegates.
//
// TWO THINGS THE CONVERSION MADE NAMEABLE that this file had to spell as raw
// offsets before, and both are now `static_assert`ed in Camera.h:
//   * `renderer->drawGroups._data` + 0x08 is `_drawGroups[0]._camera`. The
//     banner above derived "a draw group holds two Cameras exactly 0xd8 apart"
//     from this function's own reads; block F confirmed it from the writer side
//     (`BeginDrawGroup` 0x180104fd0 runs `Camera::operator=` on group+0x08 and
//     group+0xe0) and from `?GetCamera@Renderer@Graphics@@QEAAAEBVCamera@2@H@Z`
//     (0x180017fb0), which RETURNS group+0x08 as a `const Camera&`.
//   * group + 0x6c is that Camera's `_cameraToWorld` (+0x08 + 0x64).
// So the twelve floats this body walks as `m[0..11]` are
// `_drawGroups[0]._camera._cameraToWorld`, and the `Frustum` it copies out is
// `_drawGroups[0]._camera._frustum`. They are still read through the float
// cursor `m`, because the arithmetic below is transcribed in retail's operand
// order and re-spelling it as `axisX.x` per term would invite a reader to
// re-group a float sum (reference_moved_term_hides_where_terms_cancel.md).
//
// The result is constructed DIRECTLY IN THE HIDDEN RETURN SLOT, as retail does
// (`MOV RCX,R12` immediately before `CALL 0x1800fe960`): the Camera
// constructor is a member since B.4 wave X round 4, so `return Camera(f, xf,
// true);` is the retail shape. `Camera::_pad_d5` (+0xd5..+0xd7) keeps the
// caller's own content, as in retail; before, the body built the camera in
// zeroed local storage and returned a copy.
// ############################################################################

namespace Graphics {

Camera SunLight::GetSunCamera(Renderer& renderer) const {
    const SunLight* const self = this;

    // Draw group ZERO's camera at group+0x08 — `_drawGroups[0]._camera`. See
    // the banner: there is no `* 0x1f0`, so it is always group zero.
    const Camera& viewCamera = renderer._drawGroups._data[0]._camera;

    Frustum viewFrustum = viewCamera._frustum;
    const float* const m = &viewCamera._cameraToWorld.axisX.x;   // group + 0x6c
    //   m[0..2] axisX   m[3..5] axisY   m[6..8] axisZ   m[9..11] translation

    // corners[0..3] are the frustum's near corners; retail OVERWRITES
    // corners[4..7] with the extruded far points, so one 8-slot array does both.
    Vector3 corner[8];
    RtVolume_Frustum_GetCorners(&viewFrustum, corner);   // 0x1800f9850 (volume/frustum.cpp)

    // The shadow basis: look from the origin towards the sun direction vector.
    const Vector3    origin = { kZero, kZero, kZero };
    // `_dirX/_dirY/_dirZ` are three consecutive floats (+0xc0); FromLookAt
    // takes them as the Vector3 they are.
    const Vector3    sunDir(self->_direction.x, self->_direction.y, self->_direction.z);
    const Transform3 look = Transform3::FromLookAt(origin, sunDir);
    const float* const p = &look.axisX.x;

    // ---- pass 1: extrude each near corner, and rotate its direction into world
    Vector3 dir[4];
    for (int i = 0; i < 4; ++i) {
        const float x = corner[i].x, y = corner[i].y, z = corner[i].z;

        const float len = RtLibm::Sqrt((x * x + y * y) + z * z);
        float nx = x, ny = y, nz = z;
        if (!(len == kZero)) {                       // UCOMISS/JP/JZ: NaN scales
            const float s = kOne / len;
            nx = x * s; ny = y * s; nz = z * s;
        }
        corner[4 + i].x = nx * kExtrude + x;
        corner[4 + i].y = ny * kExtrude + y;
        corner[4 + i].z = nz * kExtrude + z;

        // Retail normalises a SECOND time from a freshly reloaded x (and the
        // still-live y/z). Same value, kept because the schedule is the spec.
        const float x2 = corner[i].x;
        const float len2 = RtLibm::Sqrt((x2 * x2 + y * y) + z * z);
        float ax = x2, ay = y, az = z;
        if (!(len2 == kZero)) {
            const float s = kOne / len2;
            ax = x2 * s; ay = y * s; az = z * s;
        }
        dir[i].x = m[3] * ay + m[0] * ax + m[6] * az;
        dir[i].y = m[4] * ay + m[1] * ax + m[7] * az;
        dir[i].z = m[5] * ay + m[2] * ax + m[8] * az;
    }

    // ---- pass 2: eight world points, two per near corner
    float pts[24];
    std::memset(pts, 0, sizeof(pts));               // retail zeroes all 96 bytes

    const float camX = m[9], camY = m[10], camZ = m[11];
    // Distance from the camera down to the z = -2 ground plane, negated.
    const float planeZ = NegF((camY * kZero + camX * kZero) + camZ) - kTwo;

    float sumX = kZero, sumY = kZero, sumZ = kZero;

    for (int i = 0; i < 4; ++i) {
        float* const a = pts + i * 6;               // pts[2i]
        float* const b = pts + i * 6 + 3;           // pts[2i+1]

        const float dx = dir[i].x, dy = dir[i].y, dz = dir[i].z;
        const float t  = (kOne / ((dx * kZero + dy * kZero) + dz)) * planeZ;

        const float ex = corner[i].x - corner[4 + i].x;
        const float ey = corner[i].y - corner[4 + i].y;
        const float ez = corner[i].z - corner[4 + i].z;
        const float dist = RtLibm::Sqrt((ey * ey + ex * ex) + ez * ez);

        bool clampFar = true;
        if (t > kZero) {
            a[0] = t * dx + camX;
            a[1] = t * dy + camY;
            a[2] = t * dz + camZ;
            // `COMISS dist,t / JBE` — dist <= t OR UNORDERED falls through to
            // the far-corner arm, so the test is `dist > t`, not `!(dist <= t)`.
            if (dist > t) {                          // the ray stops short: both
                b[0] = t * dx + camX;                // points sit on the plane
                b[1] = t * dy + camY;
                b[2] = t * dz + camZ;
                clampFar = false;
            }
        } else {
            const float cx = corner[i].x, cy = corner[i].y, cz = corner[i].z;
            a[0] = m[0] * cx + m[3] * cy + m[6] * cz + m[9];
            a[1] = m[1] * cx + m[4] * cy + m[7] * cz + m[10];
            a[2] = m[2] * cx + m[5] * cy + m[8] * cz + m[11];
        }

        if (clampFar) {
            const float fx = corner[4 + i].x, fy = corner[4 + i].y, fz = corner[4 + i].z;
            const float bx = m[0] * fx + m[3] * fy + m[6] * fz + m[9];
            b[0] = bx;
            b[1] = m[1] * fx + m[4] * fy + m[7] * fz + m[10];
            b[2] = m[2] * fx + m[5] * fy + m[8] * fz + m[11];

            // *** RETAIL READS pts[i].z HERE, NOT pts[2i+1].z. ***
            // `MOVSS XMM3,[RBP + RDI - 0x58]` at 0x18010776f, RDI = i*12, while
            // the stores go through RBX at stride 0x18. See the banner.
            float zz = pts[i * 3 + 2];
            if (!(kClampHi > zz))      zz = kClampHi;
            else if (kClampLo > zz)    zz = kClampLo;
            // Retail re-stores x (and reloads/re-stores y, a no-op) before the
            // clamped z; only the z store changes anything.
            b[0] = bx;
            b[2] = zz;
        }

        sumX = sumX + a[0] + b[0];
        sumY = sumY + a[1] + b[1];
        sumZ = sumZ + a[2] + b[2];
    }

    // ---- the bounding sphere of the eight points
    float cx = sumX * kEighth;
    float cy = sumY * kEighth;
    float cz = sumZ * kEighth;

    float r2 = kZero;
    for (int k = 0; k < 8; ++k) {
        const float dxk = cx - pts[k * 3 + 0];
        const float dyk = cy - pts[k * 3 + 1];
        const float dzk = cz - pts[k * 3 + 2];
        const float d2  = (dyk * dyk + dxk * dxk) + dzk * dzk;
        if (r2 < d2) r2 = d2;
    }

    // sqrt(|-axisY.z|) -- how much of the view forward axis points at the sky.
    const float skew   = RtLibm::Sqrt(AbsF((m[4] * kZero + m[3] * kZero) - m[5] * kOne));
    const float radius = RtLibm::Sqrt(r2);

    // EXT SEAM (NOT RETAIL, ext.camera_angle, SunLight::_extDeepSunBox): retail's box is a fixed 256 deep
    // along the sun (the eye 128 back from the centre, the far plane at 256) while its width follows the
    // view. A shallow tilt or a wide zoom puts ground more than 128 past the centre, on the side away
    // from the sun; it lies past the far plane, its depth clamps to 1.0 and the LESS compare against the
    // cleared map reads it as fully shadowed (a dark wedge over the distant terrain). With the seam on the
    // depth follows the view's sphere; a view whose sphere fits retail's box keeps retail's numbers.
    float eyePull = kEyePull;
    float farW = kFarW;
    if (SunLight::_extDeepSunBox != nullptr && *SunLight::_extDeepSunBox) {
        constexpr float kCasterMargin = 64.0f;   // casters above the sphere toward the sun (peaks, trees)
        const float pull = radius + kCasterMargin;
        if (pull > NegF(kEyePull)) {
            eyePull = NegF(pull);
            farW = NegF(pull + radius + kOne);
        }
    }
    cz = self->_direction.z * eyePull + cz;
    cx = self->_direction.x * eyePull + cx;
    cy = self->_direction.y * eyePull + cy;

    // `Video::ImageBuffer::Width()` (0x1800f2530) INLINED by retail: the flags
    // word at +0x34, the device's back-buffer width for bits 8 / 9 (signed /2),
    // else the image's own width shifted by `Configuration::shadowScalar` for
    // bit 6. The ported member is that ladder (buffers.cpp).
    // EXT SEAM (NOT RETAIL, ext.max_shadow_zoom, graphics/shadow_zoom.h): the snap follows the share
    // of the map the casters are drawn into, which is retail's snap at that size. Shift 0 = retail.
    const int32_t dim = self->_depthTarget->Width() >> SunLight::ExtShadowZoomShift();

    // One shadow-map texel, in world units. The snap below is what stops the
    // shadow edges crawling as the view camera pans.
    const float texel = (radius * skew * kTwo) / static_cast<float>(dim);

    float a = p[1] * cy + p[0] * cx + p[2] * cz;
    float b = p[4] * cy + p[3] * cx + p[5] * cz;
    float c = p[7] * cy + p[6] * cx + p[8] * cz;
    a = a - MsvcLibm::Fmodf(a, texel);
    b = b - MsvcLibm::Fmodf(b, texel);
    c = c - MsvcLibm::Fmodf(c, texel);

    Transform3 xf;
    xf.axisX.x = p[0]; xf.axisX.y = p[1]; xf.axisX.z = p[2];
    xf.axisY.x = p[3]; xf.axisY.y = p[4]; xf.axisY.z = p[5];
    xf.axisZ.x = p[6]; xf.axisZ.y = p[7]; xf.axisZ.z = p[8];
    xf.translation.x = p[0] * a + p[3] * b + p[6] * c;
    xf.translation.y = p[1] * a + p[4] * b + p[7] * c;
    xf.translation.z = p[2] * a + p[5] * b + p[8] * c;

    const float w = NegF(radius * skew);

    Frustum f;
    f.planes[0] = { kZero,  kOne,  kZero,  kOne  };
    f.planes[1] = { kZero, -kOne,  kZero,  farW  };
    f.planes[2] = { kOne,   kZero, kZero,  w     };
    f.planes[3] = { -kOne,  kZero, kZero,  w     };
    f.planes[4] = { kZero,  kZero, -kOne,  w     };
    f.planes[5] = { kZero,  kZero, kOne,   w     };
    f.isOrthographic = true;

    // Constructed in the return slot, as retail (`MOV RCX,R12` then
    // `CALL 0x1800fe960`).
    return Camera(f, xf, true);
}

}  // namespace Graphics
