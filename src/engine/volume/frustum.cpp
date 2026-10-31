// ============================================================================
// src/engine/volume/frustum.cpp — Volume::Frustum
// ============================================================================
//
// Wave 1, Lane 3 of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2/§6).
// Every function below is a byte-faithful port of the decompilation at the
// retail address named in its comment (Runtime-x64.dll, image base
// 0x180000000, decompiled 2026-09-06). See src/engine/volume/Volume.h for
// the layout, the plane order and what the `isOrthographic` byte means.
//
// Mirrors the original `code\Runtime\Volume\Frustum.cpp` (recovered from the
// profile build's debug strings — src/engine/volume/README.md).
//
// FUN_180128110/FUN_180127d90/FUN_180126d10 (sqrtf/sinf/cosf) go through the
// RtLibm seam (src/engine/rt_libm.h), not <cmath> directly: our own libm
// by default, or Runtime-x64.dll's own CRT as a
// diagnostic for attributing a diff-oracle divergence to libm rounding.
// ============================================================================

#include "rt_libm.h"
#include "volume/Volume.h"
#include "volume/frustum_cull.h"   // FRUSTUMCULL: the prepared cull (NOT RETAIL, below Contains)

#include <cstring>

namespace {

// How many bytes of a Frustum are actually DEFINED, for the diff oracle.
//
// sizeof(Frustum) is 0x64: the six planes plus `isOrthographic` occupy
// 0x00..0x60, and the compiler pads the tail to 4-byte alignment. Retail never
// writes those three pad bytes -- the ctors zero 0x00..0x5f in qwords and then
// store one byte at +0x60 -- and neither do we. Diffing sizeof(Frustum) was
// therefore comparing three bytes of whatever was previously on the caller's
// stack against a value-initialised scratch struct's zeros, and it reported a
// divergence every time the stack happened to be dirty. That is a harness
// defect, not a port defect: it manufactures false positives that would train
// us to ignore the oracle. Compare only the defined prefix.
constexpr size_t kFrustumDefinedBytes = offsetof(Frustum, isOrthographic) + sizeof(bool);
static_assert(kFrustumDefinedBytes == 0x61, "Frustum defined prefix is 0x61 bytes");


// Normalizes the 2D vector (a, b), matching the exact expression shape used
// by every side-plane computation in the 6-Scalar perspective ctor below:
// len = sqrt(a*a + b*b + 0.0); when len == 0, a and b are already 0 (their
// squares summed to zero), so the "unchanged" branch and the "normalized"
// branch agree in that case — the +0.0 term is exact and does not perturb
// the sum.
void NormalizeSide2(float a, float b, float& outA, float& outB) {
    float len = RtLibm::Sqrt(a * a + b * b + 0.0f);
    if (len == 0.0f) {
        outA = a;
        outB = b;
    } else {
        float inv = RtMathConst::kOne / len;
        outA = a * inv;
        outB = b * inv;
    }
}

// Volume::Frustum::operator* — transforms one plane by a Transform3's basis
// (rotate the normal, then fold the translation into `d`). Same formula
// shape at all 6 planes in ??DFrustum@Volume@@QEBA?AV01@AEBVTransform3@Math@@@Z
// (retail 0x1800f9310); only the compiler's register scheduling differs
// between planes, not the arithmetic order.
Vector4 TransformPlane(const Vector4& p, const Transform3& t) {
    Vector4 r;
    r.x = p.x * t.axisX.x + p.y * t.axisY.x + p.z * t.axisZ.x;
    r.y = p.y * t.axisY.y + p.x * t.axisX.y + p.z * t.axisZ.y;
    r.z = p.y * t.axisY.z + p.x * t.axisX.z + p.z * t.axisZ.z;
    r.w = t.translation.y * r.y + t.translation.x * r.x + t.translation.z * r.z + p.w;
    return r;
}

// Shared by GetCorners and GetScreenDirection (both retail functions branch
// on `isOrthographic` to compute the same four near-plane span values from
// the same four side planes: ?GetCorners@Frustum@Volume@@ 0x1800f9850,
// ?GetScreenDirection@Frustum@Volume@@ 0x1800f9c80).
//
//   perspective (isOrthographic == false): span = -planeSide.<axis> * near /
//     planeSide.<perp axis>, i.e. where the side plane crosses the near plane.
//   orthographic (isOrthographic == true): span = the side plane's own `d`
//     term (sign-flipped for planes[3]/[4]), already an axis-aligned offset.
void NearSpan(const Frustum& f, float nearDist, float& xLo, float& xHi, float& zLo, float& zHi) {
    if (!f.isOrthographic) {
        xLo = -f.planes[2].y * nearDist / f.planes[2].x;
        xHi = -f.planes[3].y * nearDist / f.planes[3].x;
        zLo = -f.planes[4].y * nearDist / f.planes[4].z;
        zHi = -f.planes[5].y * nearDist / f.planes[5].z;
    } else {
        xLo = f.planes[2].w;
        xHi = -f.planes[3].w;
        zLo = -f.planes[4].w;
        zHi = f.planes[5].w;
    }
}

// n-vertex / p-vertex selection for the AABB-vs-plane test used by Contains
// (the "Akenine-Möller p/n vertex" trick): FUN_1800f8fb0, retail
// 0x1800f8fb0. n-vertex is the box corner most against the plane normal
// (min on axes where normal >= 0, max where normal < 0); p-vertex is the
// opposite corner.
void SelectVertices(const Vector4& plane, const Box& box, Vector3& nVertex, Vector3& pVertex) {
    nVertex.x = plane.x < 0.0f ? box.max.x : box.min.x;
    nVertex.y = plane.y < 0.0f ? box.max.y : box.min.y;
    nVertex.z = plane.z < 0.0f ? box.max.z : box.min.z;
    pVertex.x = plane.x < 0.0f ? box.min.x : box.max.x;
    pVertex.y = plane.y < 0.0f ? box.min.y : box.max.y;
    pVertex.z = plane.z < 0.0f ? box.min.z : box.max.z;
}

}  // namespace

// ============================================================
// ??0Frustum@Volume@@QEAA@AEBVScalar@Math@@000@Z   retail 0x1800fa1e0
// Symmetric perspective ctor: near, far, fovX (degrees), fovY (degrees).
// Always builds a PERSPECTIVE frustum (isOrthographic = false); the four
// side planes pass through the origin (w = 0), apex-at-eye style.
//
// ENDGAME B7 (CTOR, D6): the REAL ctor (the `RtVolume_Frustum_Ctor_Perspective`
// door is deleted). Retail's order: the six planes' Vector4() zeroing and the
// flag's 0 (0x1800fa212..0x1800fa241) are the mem-initialiser; then sin / cos
// (0x1800fa272..0x1800fa2a0) and the 24 plane stores 0x1800fa2cd..0x1800fa363.
// The flag is not stored again.
// ============================================================
Frustum::Frustum(const Scalar& nearZ, const Scalar& farZ, const Scalar& fovX, const Scalar& fovY)
    : planes(), isOrthographic(false) {
    float halfFovXRad = fovX.v * RtMathConst::kHalf * RtMathConst::kDegToRad;
    float halfFovYRad = fovY.v * RtMathConst::kHalf * RtMathConst::kDegToRad;
    float sinX = RtLibm::Sin(halfFovXRad), cosX = RtLibm::Cos(halfFovXRad);
    float sinY = RtLibm::Sin(halfFovYRad), cosY = RtLibm::Cos(halfFovYRad);

    planes[0] = { 0.0f, RtMathConst::kOne, 0.0f, nearZ.v };
    planes[1] = { 0.0f, RtMathConst::kMinusOne, 0.0f, -farZ.v };
    planes[2] = { cosX, sinX, 0.0f, 0.0f };
    planes[3] = { -cosX, sinX, 0.0f, 0.0f };
    planes[4] = { 0.0f, sinY, -cosY, 0.0f };
    planes[5] = { 0.0f, sinY, cosY, 0.0f };
}

// ============================================================
// ??0Frustum@Volume@@QEAA@_NAEBVScalar@Math@@11111@Z   retail 0x1800fa3d0
// General ctor: isOrthographic flag, then 6 scalars (near, far, and 4 edge
// values). near/far planes are identical between the two modes; only the 4
// side planes differ (see Volume.h's Frustum comment for what each mode
// means).
//
// ENDGAME B7 (CTOR, D6): the REAL ctor (the `RtVolume_Frustum_Ctor_General` door
// is deleted). Retail's order: the planes' Vector4() zeroing
// (0x1800fa3e5..0x1800fa410) and the flag from the argument (0x1800fa41c) are the
// mem-initialiser; the branch on the flag (0x1800fa424) and the plane stores follow.
// ============================================================
Frustum::Frustum(bool isOrthographic_,
                 const Scalar& nearZ, const Scalar& farZ,
                 const Scalar& edgeA, const Scalar& edgeB,
                 const Scalar& edgeC, const Scalar& edgeD)
    : planes(), isOrthographic(isOrthographic_) {
    planes[0] = { 0.0f, RtMathConst::kOne, 0.0f, nearZ.v };
    planes[1] = { 0.0f, RtMathConst::kMinusOne, 0.0f, -farZ.v };

    if (!isOrthographic_) {
        float a, b;
        NormalizeSide2(nearZ.v, -edgeA.v, a, b);
        planes[2] = { a, b, 0.0f, 0.0f };
        NormalizeSide2(edgeB.v, -nearZ.v, a, b);
        planes[3] = { b, a, 0.0f, 0.0f };
        NormalizeSide2(edgeC.v, -nearZ.v, a, b);
        planes[4] = { 0.0f, a, b, 0.0f };
        NormalizeSide2(-edgeD.v, nearZ.v, a, b);
        planes[5] = { 0.0f, a, b, 0.0f };
    } else {
        planes[2] = { RtMathConst::kOne, 0.0f, 0.0f, edgeA.v };
        planes[3] = { RtMathConst::kMinusOne, 0.0f, 0.0f, -edgeB.v };
        planes[4] = { 0.0f, 0.0f, RtMathConst::kMinusOne, -edgeC.v };
        planes[5] = { 0.0f, 0.0f, RtMathConst::kOne, edgeD.v };
    }
}

// ??DFrustum@Volume@@QEBA?AV01@AEBVTransform3@Math@@@Z   retail 0x1800f9310
// operator*(Transform3) — returns (hidden slot) a new Frustum with every
// plane transformed by `t` and the same isOrthographic flag.
Frustum* RtVolume_Frustum_MulTransform3(const Frustum* self, Frustum* ret, const Transform3* t) {
    for (int i = 0; i < 6; i++) ret->planes[i] = TransformPlane(self->planes[i], *t);
    ret->isOrthographic = self->isOrthographic;
    return ret;
}

// ?Contains@Frustum@Volume@@QEBAIAEBVBox@2@I@Z   retail 0x1800f8e80
//
// NOT a bool despite the name: a hierarchical-culling plane bitmask.
// `mask` has one bit per plane (bit i = "still test plane i"); a caller
// culling a tree of boxes passes the parent's returned mask down to its
// children so already-satisfied planes are never re-tested. For each set
// bit: if the box is entirely on the outside of that plane, return the
// sentinel 0x40 (fully culled) immediately; if entirely on the inside,
// clear that bit (never needs testing again below this box); otherwise
// (straddling) leave the bit set. A normal return of 0 means the box is
// fully inside every plane that was tested.
//
// NaN (found by FRUSTUMCULL from the retail disassembly, 2026-10-12; row 1327):
// retail branches to the dP test on `dN <= 0 OR unordered` (COMISS + JBE), so
// this body tests `!(dN > 0)`: a NaN dN goes to the dP test, as in retail.
// LEFT: retail's SelectVertices tests `plane.<axis> >= 0` (COMISS + SETAE, false
// on NaN -> max for the n-vertex), this port `plane.<axis> < 0` (false on NaN ->
// min). The select difference cannot change a result (a
// NaN normal component makes both dN and dP NaN whatever vertex is picked; a
// unit-test mutation confirmed it); the dN one can: a NaN dN (a NaN anywhere,
// or 0 * inf, inf - inf) clears the bit here and culls / keeps it on dP in
// retail. Both agree on every input whose dN is not NaN, -0.0 included.
unsigned int RtVolume_Frustum_Contains(const Frustum* self, const Box* box, unsigned int mask) {
    unsigned int ours = mask;
    for (int i = 0; i < 6; i++) {
        unsigned int bit = 1u << i;
        if (!(ours & bit)) continue;
        const Vector4& plane = self->planes[i];
        Vector3 nVertex, pVertex;
        SelectVertices(plane, *box, nVertex, pVertex);
        float dN = plane.y * nVertex.y + plane.x * nVertex.x + plane.z * nVertex.z - plane.w;
        if (!(dN > 0.0f)) {   // COMISS + JBE: taken on unordered
            float dP = plane.y * pVertex.y + plane.x * pVertex.x + plane.z * pVertex.z - plane.w;
            if (dP < 0.0f) { ours = 0x40u; goto diffed; }
            // else: straddles this plane — leave the bit set, test the next plane.
        } else {
            ours &= ~bit;
        }
    }
diffed:
    return ours;
}

// ============================================================================
// FRUSTUMCULL (NOT RETAIL; faithful perf work, docs/extensions/handins/FRUSTUMCULL.md).
// The prepared table, the shadow entry and the counters. The fast body itself and
// the kill switch `FrustumCull::kFastCull` are in frustum_cull.h (inline, so the
// scene cull loop holds it in registers); the body above stays the reference.
// ============================================================================

int      g_frustumCullVerify = 0;
void   (*g_frustumCullReport)(bool ok, long long detail) = nullptr;
uint64_t g_frustumCullBoxes  = 0;
uint64_t g_frustumCullPasses = 0;
uint64_t g_frustumCullSiteBoxes[kFrustumCullSiteCount] = {};   // CULLROUTE: per-site box tests
uint64_t g_frustumCullSiteCalls[kFrustumCullSiteCount] = {};

void FrustumCull_Prepare(const Frustum* f, FrustumCullPlanes* out) {
    for (int i = 0; i < 8; i++) {
        if (i < 6) {
            const Vector4& p = f->planes[i];
            out->x[i] = p.x;
            out->y[i] = p.y;
            out->z[i] = p.z;
            out->w[i] = p.w;
            const uint32_t nx = p.x < 0.0f ? 0xFFFFFFFFu : 0u;   // SelectVertices' compares
            const uint32_t ny = p.y < 0.0f ? 0xFFFFFFFFu : 0u;
            const uint32_t nz = p.z < 0.0f ? 0xFFFFFFFFu : 0u;
            std::memcpy(&out->negX[i], &nx, 4);
            std::memcpy(&out->negY[i], &ny, 4);
            std::memcpy(&out->negZ[i], &nz, 4);
        } else {
            out->x[i] = out->y[i] = out->z[i] = out->w[i] = 0.0f;
            out->negX[i] = out->negY[i] = out->negZ[i] = 0.0f;
        }
    }
    out->src = f;
}

unsigned int FrustumCull_ContainsVerified(const FrustumCullPlanes* p, const Box* box, unsigned int mask) {
    const unsigned int ref = RtVolume_Frustum_Contains(p->src, box, mask);
    const unsigned int fast = FrustumCull_ContainsFast(p, box, mask);
    if (g_frustumCullReport)
        g_frustumCullReport(fast == ref, (static_cast<long long>(ref) << 8) | fast);
    return ref;
}

// ?GetCorners@Frustum@Volume@@QEBAXQEAVVector3@Math@@@Z   retail 0x1800f9850
// Writes 8 corners through `out`: [0..3] near-plane corners
// (x in {xLo,xHi}, y = near, z in {zLo,zHi}), [4..7] far-plane corners.
// Orthographic: far corners share the near corners' x/z, y = far (axis-
// aligned box). Perspective: far corners are found by normalizing the ray
// from the origin through each near corner, then rescaling that ray so its
// y-component lands exactly on `far`.
void RtVolume_Frustum_GetCorners(const Frustum* self, Vector3* out) {
    float nearDist = self->planes[0].w;
    float farDist = -self->planes[1].w;
    float xLo, xHi, zLo, zHi;
    NearSpan(*self, nearDist, xLo, xHi, zLo, zHi);

    out[0] = { xLo, nearDist, zLo };
    out[1] = { xHi, nearDist, zLo };
    out[2] = { xLo, nearDist, zHi };
    out[3] = { xHi, nearDist, zHi };

    if (self->isOrthographic) {
        out[4] = { xLo, farDist, zLo };
        out[5] = { xHi, farDist, zLo };
        out[6] = { xLo, farDist, zHi };
        out[7] = { xHi, farDist, zHi };
    } else {
        for (int i = 0; i < 4; i++) {
            const Vector3& c = out[i];
            float len = RtLibm::Sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
            Vector3 dir;
            if (len == 0.0f) {
                dir = c;
            } else {
                float inv = RtMathConst::kOne / len;
                dir = { c.x * inv, c.y * inv, c.z * inv };
            }
            float scale = (RtMathConst::kOne / dir.y) * farDist;
            out[4 + i] = { scale * dir.x, scale * dir.y, scale * dir.z };
        }
    }

}

// ?GetScreenDirection@Frustum@Volume@@QEBA?AVVector3@Math@@MM@Z   retail 0x1800f9c80
// Returns (hidden slot) the world-space ray direction through screen point
// (u,v) in [0,1]^2 across the near-plane span: x = lerp(xLo,xHi,u),
// z = lerp(zLo,zHi,v), y = near; normalized unless the length is 0.
Vector3* RtVolume_Frustum_GetScreenDirection(const Frustum* self, Vector3* ret, float u, float v) {
    float nearDist = self->planes[0].w;
    float xLo, xHi, zLo, zHi;
    NearSpan(*self, nearDist, xLo, xHi, zLo, zHi);

    float x = (xHi - xLo) * u + xLo;
    float z = (zHi - zLo) * v + zLo;
    float len = RtLibm::Sqrt(x * x + nearDist * nearDist + z * z);
    if (len == 0.0f) {
        *ret = { x, nearDist, z };
    } else {
        float inv = RtMathConst::kOne / len;
        *ret = { x * inv, nearDist * inv, z * inv };
    }

    return ret;
}

// ============================================================================
// Math::Matrix Volume::Frustum::Projection(bool) const
// ?Projection@Frustum@Volume@@QEBA?AVMatrix@Math@@_N@Z   retail 0x1800f9dc0
//
// Wave 9, lane P (tier 0). Member, struct return: RCX = this, RDX = hidden
// return slot, R8B = the bool. Adds the sixth and last Frustum member our tree
// asks for, so `Volume::Frustum` is now COMPLETE (§4.1 decision 5, class
// atomicity).
//
// NO NEW LANE-D ROWS: retail's body calls NOTHING — no helper, no libm, no
// global. It is the cheapest dependency profile in the whole of lane B's
// inventory and that is why it was picked first.
//
// WHAT IT COMPUTES. The four near-plane spans are the SAME four values
// `GetCorners` and `GetScreenDirection` derive, so this shares NearSpan() with
// them rather than re-deriving the division; the disassembly at 0x1800f9e0b
// (orthographic) and 0x1800f9e2b (perspective) is instruction-for-instruction
// the same shape as 0x1800f9850's, down to the sign of each term.
//
// Then it fills a 4x4. The two modes and the bool give four fills, and the bool
// only ever changes the DEPTH column (m[10]/m[14]) — never x or z:
//
//   orthographic, flag : m10 = -1/(f-n)          m14 = -n/(f-n)
//   orthographic,      : m10 = -2/(f-n)          m14 = -(f+n)/(f-n)
//   perspective,  flag : m10 = -f/(f-n)          m14 = -n*f/(f-n)
//   perspective,       : m10 = -(f+n)/(f-n)      m14 = -2*n*f/(f-n)
//
// i.e. `flag` selects a [0,-1] depth range and its absence the [-1,+1] one —
// which is what src/engine/graphics/renderer.cpp already calls the parameter
// (`zeroToOneDepth`), and the arithmetic agrees, so that name is kept.
//
// The .rdata constants are the ones Math.h already tabulates (1.0 @ 0x18016169c,
// 2.0 @ 0x1801616d8, -1.0 @ 0x18016175c, the -0.0f sign mask @ 0x1801617c0),
// plus -2.0 @ 0x180161768, which no earlier wave needed; it is declared beside
// the body rather than added to the shared table for one user.
//
// ORACLE: full 64-byte compare against the shipped body run into a private
// scratch. Every one of the 16 floats is written on every one of the four
// paths — checked store by store — so there is no unwritten-byte trap
// (reference_oracle_must_not_compare_unwritten_bytes.md).
// ============================================================================

// 0x180161768. Only Projection uses it, so it stays local rather than joining
// RtMathConst.
static constexpr float kMinusTwo = -2.0f;

Matrix* RtVolume_Frustum_Projection(const Frustum* self, Matrix* ret, bool zeroToOneDepth) {

    // Input snapshot for the oracle, taken before `ret` is touched. The hidden
    // return slot is a caller-chosen buffer and MAY overlap `self`:
    // src/engine/graphics/renderer.cpp:290 passes `&self->frustum` and
    // `&self->projection`, two members of the same Camera. They do not overlap
    // today, but "two members of one object" is one layout change away from
    // doing so, and the cost of being wrong is a phantom divergence that reads
    // exactly like a port defect — which is what Math::Transform::AsTransform3
    // paid 901 of before the aliasing at gameupdateloop.cpp:1496 was found.
    // Snapshot, and the question stops mattering.
    Frustum selfSnapshot{};

    const float nearDist = self->planes[0].w;
    const float farDist  = -self->planes[1].w;
    float xLo, xHi, zLo, zHi;
    NearSpan(*self, nearDist, xLo, xHi, zLo, zHi);

    // Retail computes both denominators as (hi - lo) in x and (lo - hi) in z.
    // The z one really is that way round — 0x1800f9ea2 `SUBSS XMM3,XMM10` with
    // XMM3 = zLo and XMM10 = zHi — and it is what flips the z axis.
    const float dx    = xHi - xLo;
    const float dz    = zLo - zHi;
    const float depth = farDist - nearDist;

    float* m = ret->m;
    m[4] = 0.0f;                                        // 0x1800f9e8b, both modes

    if (self->isOrthographic) {
        m[8]  = 0.0f;
        m[9]  = 0.0f;
        m[12] = -(xHi + xLo) / dx;
        m[13] = -(zHi + zLo) / dz;
        if (zeroToOneDepth) {
            m[10] = RtMathConst::kMinusOne / depth;
            m[14] = -nearDist / depth;
        } else {
            m[10] = kMinusTwo / depth;
            m[14] = -(farDist + nearDist) / depth;
        }
        m[11] = 0.0f;
        m[15] = RtMathConst::kOne;
        m[0]  = RtMathConst::kTwo / dx;
        m[5]  = RtMathConst::kTwo / dz;
    } else {
        m[12] = 0.0f;
        m[13] = 0.0f;
        // `near * 2` is formed ONCE and divided twice (0x1800f9fb3 then two
        // DIVSS), so both m[0] and m[5] share the same rounded numerator.
        const float near2 = nearDist * RtMathConst::kTwo;
        if (zeroToOneDepth) {
            m[10] = -farDist / depth;
            m[14] = nearDist * -farDist / depth;
        } else {
            m[10] = -(farDist + nearDist) / depth;
            m[14] = nearDist * (farDist * kMinusTwo) / depth;
        }
        m[8]  = (xHi + xLo) / dx;
        m[9]  = (zHi + zLo) / dz;
        m[11] = RtMathConst::kMinusOne;
        m[15] = 0.0f;
        m[0]  = near2 / dx;
        m[5]  = near2 / dz;
    }

    // The common tail, 0x1800fa0c9..0x1800fa0dd — five zeros shared by all four
    // paths. m[4] was already stored above, before the branch, exactly as retail
    // does it.
    m[1] = 0.0f;
    m[6] = 0.0f;
    m[2] = 0.0f;
    m[3] = 0.0f;
    m[7] = 0.0f;

    return ret;
}
