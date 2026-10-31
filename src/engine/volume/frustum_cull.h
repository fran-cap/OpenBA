#pragma once
// ============================================================================
// src/engine/volume/frustum_cull.h -- FRUSTUMCULL: the prepared frustum cull
// ============================================================================
//
// NOT RETAIL. Faithful perf work (docs/extensions/handins/FRUSTUMCULL.md): the
// same answer as `RtVolume_Frustum_Contains` (retail 0x1800f8e80, frustum.cpp)
// for every (frustum, box, mask), bit for bit, with the per-plane sign selects
// hoisted out of the per-box test and the six planes tested four lanes at a
// time (SSE2: same operands, same operations, same IEEE single rounding per
// lane as the scalar body).
//
// The side table: `FrustumCull_Prepare` copies a Frustum's six planes into a
// structure-of-arrays block and records, per plane and axis, the sign test
// `SelectVertices` makes (`plane.<axis> < 0.0f`). It is a SIDE object owned by
// the caller, never stored inside `Volume::Frustum` (whose 0x64-byte layout is
// pinned to retail; its three tail bytes are alignment padding and stay
// unused). The fast test reads only the table's own copy of the planes, so its
// answer is a pure function of the plane bits at Prepare time: the caller must
// Prepare after the last write to the frustum and before the first test (the
// one hot caller, Scene::AddCulledDrawItems, Prepares its two locals right
// after `operator*` fills them; nothing writes them afterwards).
//
// The fast body is INLINE here (not in frustum.cpp) so the scene cull loop
// keeps it in registers: out of line, the Win64 ABI made it save and restore
// xmm6..xmm15 (20 stack moves) on every box. The kill switch lives with it.
// ============================================================================

#include <cstdint>

#include "volume/Volume.h"

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace FrustumCull {
// The kill switch. false = FrustumCull_Contains is the reference body
// (RtVolume_Frustum_Contains on the frustum the table was prepared from).
inline constexpr bool kFastCull = true;
}  // namespace FrustumCull

struct alignas(16) FrustumCullPlanes {
    // Lanes 0..5 = planes[0..5]; lanes 6 and 7 are zero and never reach the
    // result (it is masked with 0x3F).
    float    x[8];
    float    y[8];
    float    z[8];
    float    w[8];
    // Lane masks: the bits 0xFFFFFFFF where `planes[i].<axis> < 0.0f`
    // (SelectVertices' test), else 0. Held as float so the SSE body loads them
    // with no cast; only their bit patterns are used (AND / XOR, never arithmetic).
    float    negX[8];
    float    negY[8];
    float    negZ[8];
    // The frustum the table was built from: the reference body's input when
    // kFastCull is false, and the shadow's reference input (it reads the LIVE
    // planes there, so a table built too early would show as a mismatch).
    const Frustum* src;
};

void FrustumCull_Prepare(const Frustum* f, FrustumCullPlanes* out);

// The fast body. Why it returns the reference's bits (hand-in section 1):
//   * the selects: SelectVertices picks max where `plane.<axis> < 0.0f` and min
//     otherwise (NaN and -0.0 pick min); Prepare evaluates that same compare
//     once per plane, and the per-box pick is min ^ ((min ^ max) & neg), which
//     is min's or max's exact bit pattern (NaN payloads included);
//   * the dot products are `((y*ny + x*nx) + z*nz) - w`: the same products and
//     the same association as the scalar body (the compiler may swap the two
//     operands of an add, which is exact: IEEE addition commutes; only the
//     payload of a NaN result could differ, and a NaN only ever reaches a
//     compare); MULPS / ADDPS / SUBPS round each lane exactly as MULSS / ADDSS
//     / SUBSS under the same MXCSR; no FMA (the tree is built without -mfma, so
//     neither body is contracted; the unit test would catch a contracted one);
//   * the decisions: CMPNGTPS is `!(dN > 0.0f)` (true on NaN, retail's JBE) and
//     CMPLTPS the ordered `dP < 0.0f` (false on NaN);
//   * the early-out: the reference returns the constant 0x40 at the first
//     tested plane that is outside, so WHICH plane is first cannot be seen;
//     otherwise it clears exactly the tested bits whose dN is > 0 and
//     leaves every other bit of `mask` (bit 6 and up included) alone. So
//     result = (outside & mask & 0x3F) ? 0x40 : mask & ~(notLe & 0x3F).
//   The dot products the reference skips have no observable effect but the
//   sticky MXCSR status flags (exceptions are masked; nothing reads them).
// Off x86, evaluate the same prepared snapshot with scalar operations.
inline unsigned int FrustumCull_ContainsFast(const FrustumCullPlanes* p, const Box* box, unsigned int mask) {
#if defined(__SSE2__)
    const __m128 mnx = _mm_set1_ps(box->min.x), mxx = _mm_set1_ps(box->max.x);
    const __m128 mny = _mm_set1_ps(box->min.y), mxy = _mm_set1_ps(box->max.y);
    const __m128 mnz = _mm_set1_ps(box->min.z), mxz = _mm_set1_ps(box->max.z);
    const __m128 dx = _mm_xor_ps(mnx, mxx);
    const __m128 dy = _mm_xor_ps(mny, mxy);
    const __m128 dz = _mm_xor_ps(mnz, mxz);
    const __m128 zero = _mm_setzero_ps();

    unsigned int le = 0, out = 0;
    for (int o = 0; o < 8; o += 4) {
        // n-vertex = max where the normal is negative, min otherwise; p-vertex the other.
        const __m128 sx = _mm_and_ps(dx, _mm_load_ps(p->negX + o));
        const __m128 sy = _mm_and_ps(dy, _mm_load_ps(p->negY + o));
        const __m128 sz = _mm_and_ps(dz, _mm_load_ps(p->negZ + o));
        const __m128 nx = _mm_xor_ps(mnx, sx), px = _mm_xor_ps(mxx, sx);
        const __m128 ny = _mm_xor_ps(mny, sy), py = _mm_xor_ps(mxy, sy);
        const __m128 nz = _mm_xor_ps(mnz, sz), pz = _mm_xor_ps(mxz, sz);

        const __m128 X = _mm_load_ps(p->x + o), Y = _mm_load_ps(p->y + o);
        const __m128 Z = _mm_load_ps(p->z + o), W = _mm_load_ps(p->w + o);

        // dN = plane.y * n.y + plane.x * n.x + plane.z * n.z - plane.w (left to right)
        const __m128 dN = _mm_sub_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(Y, ny), _mm_mul_ps(X, nx)),
                                                _mm_mul_ps(Z, nz)), W);
        const __m128 dP = _mm_sub_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(Y, py), _mm_mul_ps(X, px)),
                                                _mm_mul_ps(Z, pz)), W);
        const __m128 isLe = _mm_cmpngt_ps(dN, zero);  // !(dN > 0.0f) (true on NaN)
        const __m128 isLt = _mm_cmplt_ps(dP, zero);   // dP <  0.0f (false on NaN)
        le  |= static_cast<unsigned int>(_mm_movemask_ps(isLe)) << o;
        out |= static_cast<unsigned int>(_mm_movemask_ps(_mm_and_ps(isLe, isLt))) << o;
    }
    if (out & mask & 0x3Fu) return 0x40u;
    return mask & ~(~le & 0x3Fu);
#else
    unsigned int le = 0, out = 0;
    for (int i = 0; i < 6; ++i) {
        // Prepared masks are zero or all-one bits (a NaN float, unequal to zero).
        const bool sx = p->negX[i] != 0.0f, sy = p->negY[i] != 0.0f, sz = p->negZ[i] != 0.0f;
        const float nx = sx ? box->max.x : box->min.x, px = sx ? box->min.x : box->max.x;
        const float ny = sy ? box->max.y : box->min.y, py = sy ? box->min.y : box->max.y;
        const float nz = sz ? box->max.z : box->min.z, pz = sz ? box->min.z : box->max.z;
        const float dn = ((p->y[i] * ny + p->x[i] * nx) + p->z[i] * nz) - p->w[i];
        const float dp = ((p->y[i] * py + p->x[i] * px) + p->z[i] * pz) - p->w[i];
        if (!(dn > 0.0f)) {   // retail JBE: taken on unordered (row 1327)
            le |= 1u << i;
            if (dp < 0.0f) out |= 1u << i;
        }
    }
    if (out & mask & 0x3Fu) return 0x40u;
    return mask & ~(~le & 0x3Fu);
#endif
}

// == RtVolume_Frustum_Contains(p->src, box, mask) for the frustum bits the
// table was prepared from.
inline unsigned int FrustumCull_Contains(const FrustumCullPlanes* p, const Box* box, unsigned int mask) {
    if constexpr (FrustumCull::kFastCull) return FrustumCull_ContainsFast(p, box, mask);
    else return RtVolume_Frustum_Contains(p->src, box, mask);
}

// The COMPPROF=4 shadow (frustum.cpp): runs the fast body AND the reference
// body on the same inputs, reports the pair through g_frustumCullReport,
// returns the reference answer. Call it only when g_frustumCullVerify is set
// (hoisted by the caller), whatever kFastCull says.
unsigned int FrustumCull_ContainsVerified(const FrustumCullPlanes* p, const Box* box, unsigned int mask);

// Set by the stress harness (src/diag/stress_harness.cpp) at
// BANISHED_STRESS_COMPPROF=4 and never otherwise; the engine does not include
// the harness. detail = (reference << 8) | fast.
extern int  g_frustumCullVerify;
extern void (*g_frustumCullReport)(bool ok, long long detail);

// Counters for the stress report (main thread only; the scene cull adds its
// per-call totals once per AddCulledDrawItems, never per box).
extern uint64_t g_frustumCullBoxes;   // boxes tested by the scene cull
extern uint64_t g_frustumCullPasses;  // AddCulledDrawItems calls

// CULLROUTE (docs/extensions/handins/CULLROUTE.md): every caller of the frustum
// test goes through a prepared table. The sites, for the per-site breakdown of
// the stress report's `frustum cull` row. Each site adds its box count once per
// call (never per box), on the main thread.
enum FrustumCullSite : int {
    kFrustumCullSiteScene = 0,       // Graphics::Scene::AddCulledDrawItems (cull.cpp)
    kFrustumCullSiteTerrainChunks,   // WorldGeometry::AddDrawItems, the chunk grid (geometry_draw.cpp)
    kFrustumCullSiteTerrainDecals,   // WorldGeometry::AddDrawItems, the decals
    kFrustumCullSiteTerrainRoads,    // WorldGeometry::AddDrawItems, the road batches
    kFrustumCullSiteWeatherTiles,    // WeatherParticleGeometry::Draw, the 5x5x5 tiles
    kFrustumCullSiteExtDrawGather,   // ext.draw_gather's walk (class A, off in the faithful profile)
    kFrustumCullSiteCount
};
extern uint64_t g_frustumCullSiteBoxes[kFrustumCullSiteCount];   // box tests per site
extern uint64_t g_frustumCullSiteCalls[kFrustumCullSiteCount];   // calls (one per prepared loop)

inline void FrustumCull_CountSite(FrustumCullSite site, uint64_t boxes) {
    g_frustumCullSiteBoxes[site] += boxes;
    ++g_frustumCullSiteCalls[site];
}

// One box test at a routed site: the shadow when `verify` (the caller reads
// g_frustumCullVerify once, before its loop), else the fast body (or the
// reference when kFastCull is false). The same answer as
// RtVolume_Frustum_Contains(p->src, box, mask) in every case.
inline unsigned int FrustumCull_Test(const FrustumCullPlanes* p, const Box* box, unsigned int mask, bool verify) {
    return verify ? FrustumCull_ContainsVerified(p, box, mask) : FrustumCull_Contains(p, box, mask);
}
