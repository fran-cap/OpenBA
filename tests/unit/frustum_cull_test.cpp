// ============================================================================
// tests/unit/frustum_cull_test.cpp -- FRUSTUMCULL: the prepared frustum cull
// ============================================================================
// src/engine/volume/frustum_cull.h promises FrustumCull_Contains(Prepare(f), box, mask)
// == RtVolume_Frustum_Contains(f, box, mask) for every input, bit for bit. These tests
// run both bodies over millions of (frustum, box, mask) triples: frusta from the two
// retail ctors (perspective and orthographic) moved by random transforms, fully random
// planes, planes and boxes carrying -0.0 / denormals / +-inf / NaN, zero-size and
// inverted boxes, boxes with a face or a corner exactly on a plane, and masks with the
// high bits set. The fast body is compared through FrustumCull_Contains (the fast body
// while kFastCull is true), and the shadow entry point is checked to return the reference
// and to report the pair.
// ============================================================================
#include "unit.h"
#include "volume/Volume.h"
#include "volume/frustum_cull.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

struct Rng {
    uint64_t s;
    uint64_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    uint32_t U32() { return static_cast<uint32_t>(Next() >> 32); }
    float Uniform(float lo, float hi) {   // [lo, hi)
        return lo + (hi - lo) * static_cast<float>(U32() >> 8) * (1.0f / 16777216.0f);
    }
    int Below(int n) { return static_cast<int>(U32() % static_cast<uint32_t>(n)); }
};

float FromBits(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }

// A float that is usually ordinary and sometimes special.
float AnyFloat(Rng& r, float span) {
    switch (r.Below(24)) {
    case 0:  return 0.0f;
    case 1:  return -0.0f;
    case 2:  return std::numeric_limits<float>::infinity();
    case 3:  return -std::numeric_limits<float>::infinity();
    case 4:  return FromBits(0x7FC00000u | (r.U32() & 0x003FFFFFu));   // quiet NaN, any payload
    case 5:  return FromBits(0xFFC00001u);                             // negative NaN
    case 6:  return FromBits(r.U32() & 0x807FFFFFu);                   // denormal (or +-0)
    case 7:  return FromBits(r.U32());                                 // any bit pattern
    case 8:  return 1.0e8f;                                            // Box's empty sentinel
    case 9:  return -1.0e8f;
    default: return r.Uniform(-span, span);
    }
}

bool g_specials = false;   // set while a test wants special values in the planes / boxes
float Pick(Rng& r, float span) { return g_specials ? AnyFloat(r, span) : r.Uniform(-span, span); }

Math::Transform3 RandomTransform(Rng& r) {
    Math::Transform3 t;
    // A rotation-ish basis (not orthonormal on purpose: any basis is a valid input).
    t.axisX = { r.Uniform(-1, 1), r.Uniform(-1, 1), r.Uniform(-1, 1) };
    t.axisY = { r.Uniform(-1, 1), r.Uniform(-1, 1), r.Uniform(-1, 1) };
    t.axisZ = { r.Uniform(-1, 1), r.Uniform(-1, 1), r.Uniform(-1, 1) };
    t.translation = { r.Uniform(-500, 500), r.Uniform(-500, 500), r.Uniform(-100, 100) };
    if (r.Below(8) == 0) {   // the identity basis: axis-aligned planes, exact zeros in the normals
        t.axisX = { 1, 0, 0 }; t.axisY = { 0, 1, 0 }; t.axisZ = { 0, 0, 1 };
    }
    return t;
}

Frustum CameraFrustum(Rng& r, bool ortho) {
    if (ortho) {
        const Math::Scalar n(r.Uniform(0.0f, 2.0f)), f(r.Uniform(50.0f, 2000.0f));
        const Math::Scalar a(r.Uniform(-300, 0)), b(r.Uniform(0, 300)), c(r.Uniform(-300, 0)), d(r.Uniform(0, 300));
        return Frustum(true, n, f, a, b, c, d);
    }
    if (r.Below(2)) {
        const Math::Scalar n(r.Uniform(0.05f, 2.0f)), f(r.Uniform(50.0f, 2000.0f));
        const Math::Scalar fx(r.Uniform(20, 120)), fy(r.Uniform(20, 120));
        return Frustum(n, f, fx, fy);
    }
    const Math::Scalar n(r.Uniform(0.05f, 2.0f)), f(r.Uniform(50.0f, 2000.0f));
    const Math::Scalar a(r.Uniform(-2, 0)), b(r.Uniform(0, 2)), c(r.Uniform(-2, 0)), d(r.Uniform(0, 2));
    return Frustum(false, n, f, a, b, c, d);
}

Frustum WorldFrustum(Rng& r, bool ortho) {
    const Frustum cam = CameraFrustum(r, ortho);
    if (r.Below(6) == 0) return cam;   // camera space: the ctors' exact planes
    const Math::Transform3 t = RandomTransform(r);
    Frustum out;
    RtVolume_Frustum_MulTransform3(&cam, &out, &t);
    return out;
}

Frustum RandomPlanes(Rng& r) {
    Frustum f;
    f.isOrthographic = r.Below(2) != 0;
    for (auto& p : f.planes) p = { Pick(r, 2.0f), Pick(r, 2.0f), Pick(r, 2.0f), Pick(r, 500.0f) };
    return f;
}

Box RandomBox(Rng& r, const Frustum& f) {
    Box b;
    switch (r.Below(8)) {
    case 0: {   // zero-size box (a point)
        const float x = Pick(r, 600), y = Pick(r, 600), z = Pick(r, 100);
        b.min = { x, y, z }; b.max = { x, y, z };
        return b;
    }
    case 1: {   // inverted box (min > max): retail does not check, neither may we
        b.max = { Pick(r, 600), Pick(r, 600), Pick(r, 100) };
        b.min = { b.max.x + r.Uniform(0, 50), b.max.y + r.Uniform(0, 50), b.max.z + r.Uniform(0, 50) };
        return b;
    }
    case 2: {   // a box at the origin: every side plane through the apex reads exactly 0
        b.min = { 0.0f, 0.0f, 0.0f };
        b.max = r.Below(2) ? Math::Vector3{ 0.0f, 0.0f, 0.0f } : Math::Vector3{ r.Uniform(0, 10), r.Uniform(0, 10), r.Uniform(0, 10) };
        return b;
    }
    case 3: {   // a face exactly on an axis-aligned plane: coordinate = +-w of a planes[i]
        const Vector4& p = f.planes[r.Below(6)];
        const float lo = Pick(r, 600), hi = lo + r.Uniform(0, 80);
        b.min = { lo, lo, lo }; b.max = { hi, hi, hi };
        const float on = (p.x < 0.0f || p.y < 0.0f || p.z < 0.0f) ? -p.w : p.w;
        switch (r.Below(6)) {
        case 0: b.min.x = on; break;  case 1: b.max.x = on; break;
        case 2: b.min.y = on; break;  case 3: b.max.y = on; break;
        case 4: b.min.z = on; break;  default: b.max.z = on; break;
        }
        return b;
    }
    default: {   // an ordinary box around the frustum's neighbourhood
        const float cx = Pick(r, 800), cy = Pick(r, 800), cz = Pick(r, 150);
        const float hx = r.Uniform(0, 60), hy = r.Uniform(0, 60), hz = r.Uniform(0, 30);
        b.min = { cx - hx, cy - hy, cz - hz };
        b.max = { cx + hx, cy + hy, cz + hz };
        return b;
    }
    }
}

unsigned int RandomMask(Rng& r) {
    switch (r.Below(6)) {
    case 0:  return 0x3Fu;
    case 1:  return r.U32();                     // high bits, bit 6 included
    case 2:  return 0u;
    default: return r.U32() & 0x3Fu;
    }
}

struct Tally { long long cases = 0, mismatches = 0, culled = 0, inside = 0, partial = 0; };

void Compare(const Frustum& f, const Box& b, unsigned int mask, Tally& t) {
    FrustumCullPlanes p;
    FrustumCull_Prepare(&f, &p);
    const unsigned int ref  = RtVolume_Frustum_Contains(&f, &b, mask);
    const unsigned int fast = FrustumCull_Contains(&p, &b, mask);
    ++t.cases;
    if (ref == 0x40u) ++t.culled; else if ((ref & 0x3Fu) == 0) ++t.inside; else ++t.partial;
    if (ref != fast) {
        if (++t.mismatches <= 5)
            std::printf("  mismatch: mask=%08x ref=%08x fast=%08x\n", mask, ref, fast);
    }
}

// Many boxes per frustum, as the cull does; the frustum is prepared once per box here
// on purpose (Prepare is part of what is under test).
void Run(uint64_t seed, int frusta, int boxesPer, int kind, Tally& t) {
    Rng r{ seed };
    for (int i = 0; i < frusta; ++i) {
        const Frustum f = kind == 2 ? RandomPlanes(r) : WorldFrustum(r, kind == 1);
        for (int j = 0; j < boxesPer; ++j) Compare(f, RandomBox(r, f), RandomMask(r), t);
    }
}

}  // namespace

UNIT_TEST(frustum_cull_perspective_bit_identical) {
    Tally t;
    g_specials = false;
    Run(0x9E3779B97F4A7C15ull, 4000, 150, 0, t);
    g_specials = true;
    Run(0x1234567887654321ull, 1000, 100, 0, t);
    std::printf("  perspective: %lld cases, culled %lld, inside %lld, partial %lld\n",
                t.cases, t.culled, t.inside, t.partial);
    CHECK(t.cases >= 600000);
    CHECK(t.culled > 0 && t.inside > 0 && t.partial > 0);
    CHECK_EQ(t.mismatches, 0LL);
}

UNIT_TEST(frustum_cull_orthographic_bit_identical) {
    Tally t;
    g_specials = false;
    Run(0xC0FFEE0DDF00Dull, 4000, 150, 1, t);
    g_specials = true;
    Run(0xABCDEF0123456789ull, 1000, 100, 1, t);
    std::printf("  orthographic: %lld cases, culled %lld, inside %lld, partial %lld\n",
                t.cases, t.culled, t.inside, t.partial);
    CHECK(t.cases >= 600000);
    CHECK(t.culled > 0 && t.inside > 0 && t.partial > 0);
    CHECK_EQ(t.mismatches, 0LL);
}

UNIT_TEST(frustum_cull_random_planes_bit_identical) {
    Tally t;
    g_specials = false;
    Run(0x5555AAAA5555AAAAull, 2000, 100, 2, t);
    g_specials = true;   // NaN / inf / -0.0 / denormal planes and boxes
    Run(0x0F0F0F0FF0F0F0F0ull, 5000, 100, 2, t);
    CHECK(t.cases >= 700000);
    CHECK_EQ(t.mismatches, 0LL);
}

// Hand-picked edges: the plane's own sign tests (-0.0, NaN), a NaN dN (0 * inf), a box
// exactly on the plane (dN == 0 is "<= 0", then dP decides), and bit 6 in the mask.
UNIT_TEST(frustum_cull_edges) {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float vals[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -2.0f, inf, -inf, nan, FromBits(1), FromBits(0x80000001u) };
    const int n = static_cast<int>(sizeof(vals) / sizeof(vals[0]));
    Tally t;
    Rng r{ 77 };
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b)
            for (int c = 0; c < n; ++c) {
                Frustum f;
                f.isOrthographic = false;
                for (int i = 0; i < 6; ++i) f.planes[i] = { vals[(a + i) % n], vals[(b + 2 * i) % n], vals[(c + 3 * i) % n], vals[(a + b + c + i) % n] };
                for (int k = 0; k < 40; ++k) {
                    Box box;
                    box.min = { vals[r.Below(n)], vals[r.Below(n)], vals[r.Below(n)] };
                    box.max = { vals[r.Below(n)], vals[r.Below(n)], vals[r.Below(n)] };
                    Compare(f, box, RandomMask(r), t);
                    Compare(f, box, 0x3Fu, t);
                    Compare(f, box, 0x7Fu, t);
                }
            }
    // The exact-on-plane cases, by hand: plane x = 3 (normal +x, w = 3).
    Frustum f;
    for (auto& p : f.planes) p = { 0.0f, 0.0f, 0.0f, -1.0f };   // always inside (0 - -1 > 0)
    f.planes[2] = { 1.0f, 0.0f, 0.0f, 3.0f };
    FrustumCullPlanes p;
    FrustumCull_Prepare(&f, &p);
    Box on;  on.min  = { 3.0f, 0, 0 }; on.max  = { 3.0f, 1, 1 };   // dN = 0, dP = 0: straddles
    Box out; out.min = { 1.0f, 0, 0 }; out.max = { 2.0f, 1, 1 };   // dN < 0, dP < 0: culled
    Box in;  in.min  = { 4.0f, 0, 0 }; in.max  = { 5.0f, 1, 1 };   // dN > 0: inside
    CHECK_EQ(RtVolume_Frustum_Contains(&f, &on, 0x3Fu), 0x04u);
    CHECK_EQ(FrustumCull_Contains(&p, &on, 0x3Fu), 0x04u);
    CHECK_EQ(RtVolume_Frustum_Contains(&f, &out, 0x3Fu), 0x40u);
    CHECK_EQ(FrustumCull_Contains(&p, &out, 0x3Fu), 0x40u);
    CHECK_EQ(FrustumCull_Contains(&p, &out, 0x3Bu), 0x00u);       // plane 2 not tested
    CHECK_EQ(RtVolume_Frustum_Contains(&f, &in, 0xFFu), 0xC0u);   // high bits pass through
    CHECK_EQ(FrustumCull_Contains(&p, &in, 0xFFu), 0xC0u);
    std::printf("  edges: %lld cases\n", t.cases);
    CHECK_EQ(t.mismatches, 0LL);
}

// The shadow entry point: returns the reference, reports (ok, (ref << 8) | fast).
namespace { long long s_reports = 0, s_bad = 0, s_lastDetail = -1; }
UNIT_TEST(frustum_cull_shadow_reports) {
    void (*saved)(bool, long long) = g_frustumCullReport;
    g_frustumCullReport = [](bool ok, long long detail) { ++s_reports; if (!ok) ++s_bad; s_lastDetail = detail; };
    Rng r{ 4242 };
    Frustum f = WorldFrustum(r, false);
    FrustumCullPlanes p;
    FrustumCull_Prepare(&f, &p);
    for (int i = 0; i < 1000; ++i) {
        const Box b = RandomBox(r, f);
        const unsigned int m = RandomMask(r);
        const unsigned int got = FrustumCull_ContainsVerified(&p, &b, m);
        const unsigned int ref = RtVolume_Frustum_Contains(&f, &b, m);
        CHECK_EQ(got, ref);
        CHECK_EQ(s_lastDetail, (static_cast<long long>(ref) << 8) | ref);
    }
    CHECK_EQ(s_reports, 1000LL);
    CHECK_EQ(s_bad, 0LL);
    // A table prepared from stale planes is what the shadow exists to catch: the reference
    // reads the LIVE frustum through `src`.
    f.planes[0].w += 1000.0f;   // moves the near plane past every box near the camera
    long long badBefore = s_bad;
    for (int i = 0; i < 2000; ++i) {
        const Box b = RandomBox(r, f);
        (void)FrustumCull_ContainsVerified(&p, &b, 0x3Fu);
    }
    CHECK(s_bad > badBefore);
    g_frustumCullReport = saved;
}

// Prepare's own layout: lanes 6 and 7 are inert, the masks are all-ones / all-zeros bits.
UNIT_TEST(frustum_cull_prepare_layout) {
    Frustum f;
    for (int i = 0; i < 6; ++i) f.planes[i] = { i % 2 ? -1.0f : 1.0f, -0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f };
    FrustumCullPlanes p;
    FrustumCull_Prepare(&f, &p);
    CHECK(p.src == &f);
    for (int i = 0; i < 8; ++i) {
        uint32_t nx, ny, nz;
        std::memcpy(&nx, &p.negX[i], 4); std::memcpy(&ny, &p.negY[i], 4); std::memcpy(&nz, &p.negZ[i], 4);
        CHECK_EQ(nx, (i < 6 && i % 2) ? 0xFFFFFFFFu : 0u);
        CHECK_EQ(ny, 0u);   // -0.0 < 0 is false
        CHECK_EQ(nz, 0u);   // NaN < 0 is false
    }
    CHECK_EQ(p.x[6], 0.0f);
    CHECK_EQ(p.w[7], 0.0f);
}

// CULLROUTE: the routed sites' helper `FrustumCull_Test` over the routed sites' own box
// shapes (one table per frustum, many boxes, mask 0x3F, as the sites do):
//   * the terrain chunk grid of a large map (32 x 32 chunks of 32, z -16..16) in a
//     geometry-local frustum (world frustum times a second transform);
//   * flat decal / road boxes (z = 0, integer tile corners);
//   * the weather's 5x5x5 tile cubes around a snapped camera point, in a world frustum.
// verify = false is the fast body (kFastCull), verify = true the shadow, which must
// return the reference and report ok. And FrustumCull_CountSite's two counters.
namespace { long long s_routeReports = 0, s_routeBad = 0; }
UNIT_TEST(frustum_cull_routed_sites) {
    void (*saved)(bool, long long) = g_frustumCullReport;
    g_frustumCullReport = [](bool ok, long long) { ++s_routeReports; if (!ok) ++s_routeBad; };
    Rng r{ 0xC011u };
    Tally t;
    long long tests = 0;
    auto check = [&](const Frustum& f, const FrustumCullPlanes& p, const Box& b) {
        const unsigned int ref = RtVolume_Frustum_Contains(&f, &b, 0x3Fu);
        const unsigned int fast = FrustumCull_Test(&p, &b, 0x3Fu, false);
        const unsigned int shadow = FrustumCull_Test(&p, &b, 0x3Fu, true);
        ++tests;
        if (ref == 0x40u) ++t.culled; else if ((ref & 0x3Fu) == 0) ++t.inside; else ++t.partial;
        if (fast != ref || shadow != ref) ++t.mismatches;
    };
    for (int k = 0; k < 400; ++k) {
        const Frustum world = WorldFrustum(r, k % 5 == 0);
        const Math::Transform3 inv = RandomTransform(r);
        Frustum local;
        RtVolume_Frustum_MulTransform3(&world, &local, &inv);
        FrustumCullPlanes lp, wp;
        FrustumCull_Prepare(&local, &lp);
        FrustumCull_Prepare(&world, &wp);
        for (int i = 0; i < 32; ++i)            // the chunk grid: gridSize 1024, chunkSize 32
            for (int j = 0; j < 32; ++j) {
                const float xmin = static_cast<float>(j) * 32.0f - 512.0f;
                const float ymin = static_cast<float>(i) * 32.0f - 512.0f;
                Box b;
                b.min = { xmin, ymin, -16.0f };
                b.max = { xmin + 32.0f, ymin + 32.0f, 16.0f };
                check(local, lp, b);
            }
        for (int d = 0; d < 64; ++d) {          // decals / roads: flat, integer corners
            const int x0 = r.Below(1024), y0 = r.Below(1024);
            Box b;
            b.min = { static_cast<float>(x0) - 512.0f, static_cast<float>(y0) - 512.0f, 0.0f };
            b.max = { static_cast<float>(x0 + 1 + r.Below(32)) - 512.0f,
                      static_cast<float>(y0 + 1 + r.Below(32)) - 512.0f, 0.0f };
            check(local, lp, b);
        }
        const float box = r.Uniform(2.0f, 40.0f);   // the weather tiles
        const float sx = std::floor(r.Uniform(-500, 500) / box) * box;
        const float sy = std::floor(r.Uniform(-500, 500) / box) * box;
        const float sz = std::floor(r.Uniform(-100, 100) / box) * box;
        for (int i = -2; i < 3; ++i)
            for (int j = -2; j < 3; ++j)
                for (int m = -2; m < 3; ++m) {
                    Box b;
                    b.min = { box * static_cast<float>(m) + sx, box * static_cast<float>(j) + sy,
                              box * static_cast<float>(i) + sz };
                    b.max = { b.min.x + box, b.min.y + box, b.min.z + box };
                    check(world, wp, b);
                }
    }
    std::printf("  routed sites: %lld tests, culled %lld inside %lld partial %lld\n",
                tests, t.culled, t.inside, t.partial);
    CHECK_EQ(t.mismatches, 0LL);
    CHECK_EQ(s_routeReports, tests);
    CHECK_EQ(s_routeBad, 0LL);
    CHECK(t.culled > 0 && t.inside > 0 && t.partial > 0);
    g_frustumCullReport = saved;

    // The per-site counters: one call adds its boxes once.
    const uint64_t boxes0 = g_frustumCullSiteBoxes[kFrustumCullSiteTerrainChunks];
    const uint64_t calls0 = g_frustumCullSiteCalls[kFrustumCullSiteTerrainChunks];
    FrustumCull_CountSite(kFrustumCullSiteTerrainChunks, 1024u);
    FrustumCull_CountSite(kFrustumCullSiteTerrainChunks, 0u);
    CHECK_EQ(g_frustumCullSiteBoxes[kFrustumCullSiteTerrainChunks] - boxes0, 1024ull);
    CHECK_EQ(g_frustumCullSiteCalls[kFrustumCullSiteTerrainChunks] - calls0, 2ull);
    g_frustumCullSiteBoxes[kFrustumCullSiteTerrainChunks] = boxes0;
    g_frustumCullSiteCalls[kFrustumCullSiteTerrainChunks] = calls0;
}
