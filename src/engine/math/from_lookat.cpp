// ============================================================================
// src/engine/math/from_lookat.cpp — Math::Transform3::FromLookAt
// ============================================================================
//
//   ?FromLookAt@Transform3@Math@@SA?AV12@AEBVVector3@2@0@Z
//   retail 0x180064750, ordinal 2383, 695 bytes, no branchless tricks.
//
// BATCH 4 (the Graphics render/cull/sun-camera cluster). Its one caller in that
// cluster is Graphics::SunLight::GetSunCamera (0x180107010), which builds the
// shadow camera's basis from the sun direction with `FromLookAt(origin, -dir)`.
//
// ---------------------------------------------------------------------------
// THE HIDDEN RETURN SLOT — the trap this function is the poster child for
// ---------------------------------------------------------------------------
// `FromLookAt` is a STATIC member returning `Transform3` (0x30 bytes) BY VALUE,
// so MSVC's x64 ABI passes the return slot in RCX and the two arguments in RDX
// and R8. Ghidra drops the R8 argument on static struct-returning functions and
// prints a TWO-parameter signature with an `in_R8` local; a typedef copied from that C
// output would pass the target vector where the ABI expects nothing and write
// the 0x30-byte result over whatever the caller left in RCX. Verified against
// the DISASSEMBLY here: `MOV RBX,RCX` (slot), `MOV RDI,RDX` (position) and
// `MOVSS XMM7,[R8]` / `SUBSS XMM7,[RDX]` (target minus position) at 0x180064767.
// The tail returns the slot in RAX (`MOV RAX,RBX`), like every such function.
//
// NOTE the argument roles, which the names in the export do not give away:
// RDX is the eye/POSITION (it is what lands in the translation row) and R8 is
// the TARGET. `forward = target - position`.
//
// ---------------------------------------------------------------------------
// WHAT IT COMPUTES
// ---------------------------------------------------------------------------
//   forward = normalize(target - position)          (unnormalised if length 0)
//   up      = (0,0,1), except when `forward` is within 0.999 of the world Z
//             axis, where it flips to (0,-1,0) (looking up) or (0,+1,0)
//             (looking down) so the cross product does not degenerate.
//   right   = normalize(cross(forward, up))
//   up'     = normalize(cross(right, forward))
//   result  = { axisX = right, axisY = forward, axisZ = up', translation = position }
//
// So the SECOND axis is the view direction, not the third. That is what
// GetSunCamera's later `dot(axisY, ...)` reads, and getting it round the other
// way would silently rotate every shadow frustum by 90 degrees.
//
// ---------------------------------------------------------------------------
// TRANSLITERATION RULES (same as wave9_lane_gfxr.cpp — do not simplify)
// ---------------------------------------------------------------------------
//  * Every sum is parenthesised to match the ADDSS order in the retail
//    schedule. Float addition is not associative and the two cross products
//    below are near-cancelling sums, which is exactly the shape where a
//    re-grouped term shows up in one call in tens of thousands.
//  * The three "is it zero" tests are `UCOMISS` + `JP`/`JZ`, i.e. a NaN length
//    takes the SCALE path, not the skip path. `if (!(len == 0.0f))` reproduces
//    that; `if (len != 0.0f)` happens to as well, but the spelling below says
//    which way the unordered case goes.
//  * The two threshold tests are `COMISS` + `JBE`, so a NaN dot falls through
//    to the DEFAULT up vector. Plain C `>` gives the same answer.
//  * The up-axis selection keeps retail's literal `* 0.0f` terms in the dot
//    product. They are not dead code to a strict-FP reader: `x * 0.0f` is -0.0
//    for negative x and NaN for infinite x, and the sum order is observable.
//  * Constants are lifted as raw bits, never retyped as decimals:
//      0x180161694 = 0x3f000000   0.5f   (not used here; listed for the family)
//      0x180161698 = 0x3f7fbe77  +0.999f  upper pole threshold
//      0x18016169c = 0x3f800000  +1.0f    reciprocal numerator and +up
//      0x180161758 = 0xbf7fbe77  -0.999f  lower pole threshold
//      0x18016175c = 0xbf800000  -1.0f    -up
//
// The square root is retail's own CRT `sqrtf` (0x180128110), reached through
// the RtLibm seam like every other ported Math body — its default is our port
// of that exact address (src/engine/crt/msvc_libm.cpp).
// ============================================================================

#include "rt_libm.h"
#include "math/Math.h"

#include <cstdint>
#include <cstring>

namespace {

inline float F32(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }

constexpr uint32_t kPosOne   = 0x3f800000u;   // 0x18016169c
constexpr uint32_t kNegOne   = 0xbf800000u;   // 0x18016175c
constexpr uint32_t kPosPole  = 0x3f7fbe77u;   // 0x180161698   +0.999f
constexpr uint32_t kNegPole  = 0xbf7fbe77u;   // 0x180161758   -0.999f

}  // namespace

// ?FromLookAt@Transform3@Math@@SA?AV12@AEBVVector3@2@0@Z — retail 0x180064750.
//
// ENDGAME B.3.1 step 1: the body MOVED onto Math::Transform3::FromLookAt. It
// is unchanged line for line; only the hidden return slot is gone (there is no
// cross-compiler ABI left, B31_RULES), so it returns by value. The banner
// above still explains why the ARGUMENT order is what it is, which is the part
// Ghidra got wrong and which the static-member signature preserves exactly.
Math::Transform3 Math::Transform3::FromLookAt(const Vector3& position, const Vector3& target) {
    Transform3 out;
    Transform3* const ret = &out;

    const float one  = F32(kPosOne);
    const float zero = 0.0f;

    // forward = target - position                                 0x180064767
    float fx = target.x - position.x;
    float fy = target.y - position.y;
    float fz = target.z - position.z;

    // 0x1800647aa..0x1800647da: (fx*fx + fy*fy) + fz*fz, then sqrtf.
    float len = RtLibm::Sqrt((fx * fx + fy * fy) + fz * fz);
    if (!(len == zero)) {                                       // 0x1800647ee UCOMISS/JP/JZ
        const float s = one / len;                              // 0x1800647f9 DIVSS
        fx = fx * s;
        fy = fy * s;
        fz = fz * s;
    }

    // The up-axis pick. `dot` is dot(forward, (0,0,1)) written the way retail
    // writes it, zero multiplies and all.                       0x18006480b
    const float dot = ((fy * zero) + (fx * zero)) + fz;

    // (a, b, c) is retail's XMM2 / XMM3 / XMM10, which spell the up vector
    // (u0, u1, u2) = (a, c, b) — see the cross product below.
    float a = zero;                       // XMM2  = XMM6
    float b = one;                        // XMM3  = XMM11
    float c = zero;                       // XMM10 = XMM6
    if (dot > F32(kPosPole)) {            // 0x18006482e COMISS / JBE
        c = F32(kNegOne);                 // up = (0, -1, 0)
        b = zero;
        a = zero;
    } else if (F32(kNegPole) > dot) {     // 0x18006484a COMISS / JBE
        c = one;                          // up = (0, +1, 0)
        b = zero;
        a = zero;
    }

    // right = cross(forward, up)                                 0x180064865
    float rx = b * fy - c * fz;
    float ry = a * fz - b * fx;
    float rz = c * fx - a * fy;

    // 0x18006489d: (ry*ry + rx*rx) + rz*rz, then sqrtf.
    float rlen = RtLibm::Sqrt((ry * ry + rx * rx) + rz * rz);
    if (!(rlen == zero)) {                                      // 0x1800648bc
        const float s = one / rlen;
        rx = rx * s;
        ry = ry * s;
        rz = rz * s;
    }

    // up' = cross(right, forward)                                0x1800648e6
    float ux = ry * fz - rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy - ry * fx;

    // 0x180064933: (uy*uy + ux*ux) + uz*uz, then sqrtf. Retail divides its
    // live 1.0f register IN PLACE here (`DIVSS XMM11,XMM0`), which is why the
    // zero-length arm leaves all three components untouched.
    const float ulen = RtLibm::Sqrt((uy * uy + ux * ux) + uz * uz);
    if (!(ulen == zero)) {                                      // 0x18006494c
        const float s = one / ulen;
        ux = ux * s;
        uy = uy * s;
        uz = uz * s;
    }

    ret->axisX.x = rx;                    // 0x180064985
    ret->axisX.y = ry;
    ret->axisX.z = rz;
    ret->axisY.x = fx;
    ret->axisY.y = fy;
    ret->axisY.z = fz;
    ret->axisZ.x = ux;
    ret->axisZ.y = uy;
    ret->axisZ.z = uz;
    // Retail copies the position through GP registers (`MOV EAX,[RDI]` ...),
    // i.e. a bit copy, so a signalling NaN in the argument survives unquieted.
    // (A float assignment is a MOVSS pair on x64: a bit copy as well.)
    ret->translation.x = position.x;
    ret->translation.y = position.y;
    ret->translation.z = position.z;
    return out;
}
