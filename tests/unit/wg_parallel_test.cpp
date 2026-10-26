// ============================================================================
// tests/unit/wg_parallel_test.cpp -- WGPAR: System::ParallelFor and the exact worldgen
// speedups, each against a sequential reference, bit-compared
// ============================================================================
//   * ParallelFor: with 0 / 1 / 2 / 4 / 7 workers every index of every range is run
//     exactly once, chunks are disjoint, nested calls run inline;
//   * e5d0's cell loop (WgCells_BaseHeightmap) and cf440's three passes
//     (WgCells_Blend*) vs the OLD sequential bodies (copied below, test only) on
//     several seeds and sizes, memcmp'd, with 1 / 2 / 4 / 7 workers;
//   * the eee0 brush falloff memo (WgBrushCurveCache) vs the OLD per-cell expression:
//     span and every w(s) of the scan window bit-equal for the game's (base, cutoff)
//     pairs and random ones, with both brush files' pow (powf, std::pow), across cache
//     evictions; and a whole brush application old vs memo on a random mask;
//   * [PERLINPRE] the start town's scatter noise table (WgCells_PerlinTable) vs the old
//     per-cell Generate call, memcmp'd, non-power-of-two dims, 0 / 1 / 2 / 4 / 7 workers.
// docs/extensions/handins/WGPAR.md
// ============================================================================
#include "unit.h"

#include "Game/worldgen_brush_curve.h"
#include "Game/worldgen_cells.h"
#include "system/parallel_for.h"
#include "utility/PerlinNoise.h"
#include "crt/msvc_libm.h"   // MsvcLibm::Powf: worldgen_cells.cpp's powf since the GCC 16 move (the reference must match)

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

const int kWorkerSets[] = {1, 2, 4, 7};

// ---------------------------------------------------------------------------
// ParallelFor on its own.
// ---------------------------------------------------------------------------
UNIT_TEST(wg_parallel_for_runs_every_index_once) {
    const int sets[] = {0, 1, 2, 4, 7};
    for (int workers : sets) {
        System::Parallel::SetWorkers(workers);
        CHECK_EQ(System::Parallel::Workers(), workers);
        for (int n : {0, 1, 2, 7, 64, 1000, 4097}) {
            for (int grain : {0, 1, 3, 8, 5000}) {
                std::vector<std::atomic<int>> hits(n > 0 ? n : 1);
                for (auto& h : hits) h.store(0);
                std::atomic<int> calls{0};
                System::ParallelFor(10, 10 + n, grain, [&](int b, int e) {
                    calls.fetch_add(1);
                    for (int i = b; i < e; ++i) hits[i - 10].fetch_add(1);
                });
                bool once = true;
                for (int i = 0; i < n; ++i) once = once && hits[i].load() == 1;
                CHECK(once);
                if (n == 0) CHECK_EQ(calls.load(), 0);
                if (workers == 0 && n > 0) CHECK_EQ(calls.load(), 1);   // inline: one call
            }
        }
        // A nested call runs inline and still covers its range.
        std::atomic<int> inner{0};
        System::ParallelFor(0, 16, 1, [&](int b, int e) {
            for (int i = b; i < e; ++i)
                System::ParallelFor(0, 10, 1, [&](int b2, int e2) { inner.fetch_add(e2 - b2); });
        });
        CHECK_EQ(inner.load(), 160);
    }
    System::Parallel::Shutdown();
}

// [EROSIONPAR] A rebuilt pool's threads must wait for the next job, not wake for the finished one
// whose epoch the pool still holds: such a thread's check-in landed in the next call's count, the
// call returned before its chunks were done (and a later call waited forever). Rebuild the pool
// before every call, call at once, and check every chunk ran exactly once BY THE RETURN.
UNIT_TEST(wg_parallel_for_rebuilt_pool_waits_for_the_next_job) {
    bool allDone = true;
    for (int round = 0; round < 400; ++round) {
        System::Parallel::SetWorkers(1 + round % 7);
        // The race needs a new thread to wake while the call is being published: wait 0-300 us
        // (thread start-up is about that) before calling.
        const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds((round % 16) * 20);
        while (std::chrono::steady_clock::now() < until) {}
        std::vector<std::atomic<int>> hits(32);
        for (auto& h : hits) h.store(0);
        System::ParallelFor(0, 32, 1, [&](int b, int e) {
            for (int i = b; i < e; ++i) {
                volatile int spin = 0;
                for (int k = 0; k < 20000; ++k) spin = spin + k;   // keep chunks in flight a while
                hits[i].fetch_add(1);
            }
        });
        for (int i = 0; i < 32; ++i) allDone = allDone && hits[i].load() == 1;
    }
    CHECK(allDone);
    System::Parallel::Shutdown();
}

// ---------------------------------------------------------------------------
// The OLD sequential bodies (worldgen.cpp before WGPAR), verbatim. Test only.
// ---------------------------------------------------------------------------
namespace old {

constexpr float kQuarter = 0.25f, kClampFloor = 2.0f, kOne = 1.0f, kPn2Coord = 0.1f,
                kFalloffBase = 1.1f, kFalloffBias = 16.0f, kBaseScale = 8.0f, kHScale = 48.0f;

void BaseHeightmap(float* heightmap, int dim, const RtUtility::PerlinNoise& pn1,
                   const RtUtility::PerlinNoise& pn2) {
    int edge = dim + 1;
    int half = dim >> 1;
    float distBias = (float)dim * kQuarter + kFalloffBias;
    for (int row = 0; row < edge; ++row) {
        int dz = row - half;
        for (int col = 0; col < edge; ++col) {
            int dx = col - half;
            float dist  = sqrtf((float)(dx * dx + dz * dz));
            float curve = MsvcLibm::Powf(kFalloffBase, distBias - dist);   // the exe's CRT powf, as production
            float w     = kOne / (curve + kOne);
            float pn1s = RtUtility_PerlinNoise_Generate(&pn1, (float)col * kQuarter, (float)row * kQuarter);
            float base = (pn1s + kOne) * kBaseScale;
            float pn2s = RtUtility_PerlinNoise_Generate(&pn2, (float)col * kPn2Coord, (float)row * kPn2Coord);
            float h    = (pn2s + kQuarter) * kHScale + pn1s;
            if (h < kClampFloor) h = kClampFloor;
            heightmap[row * edge + col] = (kOne - w) * base + h * w;
        }
    }
}

inline void CellNormal(const float* hm, int dim, int col, int row, float out[3]) {
    int edge = dim + 1;
    auto clamp = [dim](int v) { return v < 0 ? 0 : (v > dim ? dim : v); };
    int cR = clamp(row), cC = clamp(col);
    int colP = clamp(col + 1), colM = clamp(col - 1), rowP = clamp(row + 1), rowM = clamp(row - 1);
    float nx = -(hm[edge * cR + colP] - hm[edge * cR + colM]);
    float ny = -(hm[edge * rowP + cC] - hm[edge * rowM + cC]);
    float nz = 2.0f;
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    if (len > 0.0f) { nx /= len; ny /= len; nz /= len; }
    out[0] = nx; out[1] = ny; out[2] = nz;
}

void Blend(const float* hm, int dim, const RtUtility::PerlinNoise& pn, float* scratch, uint8_t* out) {
    const float kCoord = 0.25f, kRRemap = 0.5f, k255 = 255.0f, kRockHMin = -0.5f, kHi8 = 8.0f,
                kHi7 = 7.0f, kBandDiv = 3.0f, kGUpper = 0.998969f, kGLower = 0.85f, kGBandDiv = 0.149047f;
    int edge = dim + 1;
    for (int row = 0; row < dim; ++row)
        for (int col = 0; col < dim; ++col) {
            float* e = scratch + (size_t)(dim * row + col) * 6;
            e[0] = (float)col; e[1] = (float)row; e[2] = hm[edge * row + col];
            CellNormal(hm, dim, col, row, &e[3]);
        }
    for (int row = 0; row < dim; ++row)
        for (int col = 0; col < dim; ++col) {
            const float* e = scratch + (size_t)(dim * row + col) * 6;
            float height = e[2], nz = e[5];
            uint32_t rock;
            if (height >= kRockHMin) {
                float pnv = RtUtility_PerlinNoise_Generate(&pn, (float)col * kCoord, (float)row * kCoord);
                pnv = pnv * kRRemap + kRRemap;
                rock = (uint32_t)(long long)(pnv * pnv * k255);
            } else {
                rock = 0xff;
            }
            uint32_t grass;
            if (nz >= kGUpper || nz <= kGLower) grass = (nz <= kGLower) ? 0xff : 0;
            else grass = (uint32_t)(long long)((1.0f - (nz - kGLower) / kGBandDiv) * k255);
            if (height > kHi8) {
                float t = (height - kHi8) / kBandDiv;
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                uint8_t v = (uint8_t)(long long)(t * k255);
                uint8_t rb = (uint8_t)rock;
                rock = (rb <= v) ? v : rb;
            }
            if (height > kHi7) {
                float t = (height - kHi7) / kBandDiv;
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                uint8_t v = (uint8_t)(long long)(t * k255);
                uint8_t gb = (uint8_t)grass;
                if (gb < v) grass = v;
            }
            uint8_t* px = &out[(size_t)(dim * row + col) * 4];
            px[0] = (uint8_t)(rock & 0xff); px[1] = (uint8_t)(grass & 0xff); px[2] = 0; px[3] = 0;
        }
    for (int row = 0; row < dim; ++row)
        for (int col = 0; col < dim; ++col) {
            const float* ce = scratch + (size_t)(dim * row + col) * 6;
            float cx = ce[0], cy = ce[1], ch = ce[2], cnx = ce[3], cny = ce[4], cnz = ce[5];
            float accumDot = 0.0f, count = 0.0f, sumNx = 0.0f, sumNy = 0.0f, sumNz = 0.0f;
            for (int dz = -10; dz < 10; ++dz) {
                int nrow = row + dz;
                if (nrow < 0 || nrow >= dim) continue;
                for (int dx = -10; dx < 11; ++dx) {
                    int ncol = col + dx;
                    if (ncol < 0 || ncol >= dim) continue;
                    if (dx * dx + dz * dz >= 0x65) continue;
                    const float* ne = scratch + (size_t)(dim * nrow + ncol) * 6;
                    float dirx = ne[0] - cx, diry = ne[1] - cy, dirz = ne[2] - ch;
                    float len = sqrtf(dirx * dirx + diry * diry + dirz * dirz);
                    if (len > 0.0f) { dirx /= len; diry /= len; dirz /= len; }
                    float dot = cnx * dirx + cny * diry + cnz * dirz;
                    if (dot < 0.0f) dot = 0.0f;
                    accumDot += dot;
                    count += 1.0f;
                    if (dx > -2 && dx < 2 && dz > -2 && dz < 2) { sumNx += ne[3]; sumNy += ne[4]; sumNz += ne[5]; }
                }
            }
            float sl  = sqrtf(sumNx * sumNx + sumNy * sumNy + sumNz * sumNz);
            float snz = (sl > 0.0f) ? sumNz / sl : 0.0f;
            float avg = (count > 0.0f) ? accumDot / count : 0.0f;
            float ao  = (1.0f - avg) * snz * k255;
            uint32_t aoByte = (uint32_t)(long long)ao & 0xff;
            out[(size_t)(dim * row + col) * 4 + 2] |= (uint8_t)aoByte;
        }
}

// The brush (worldgen_f390_rivers.cpp wg_StampBrush_eee0 before WGPAR), pow / sqrt passed in.
float NegateBits(float v) { uint32_t b; std::memcpy(&b, &v, 4); b ^= 0x80000000u; std::memcpy(&v, &b, 4); return v; }

void Brush(float (*pw)(float, float), float (*sq)(float), int dim, const float* center, float radiusParam,
           float falloffPow, float strength, char mode, float* mask) {
    const float kOne1 = 1.0f, kThr = 0.99f;
    int edge = dim + 1, half = dim >> 1;
    float r = kOne1;
    float curve = pw(falloffPow, NegateBits(kOne1 - radiusParam));
    while (kOne1 / (curve + kOne1) < kThr) { r += kOne1; curve = pw(falloffPow, NegateBits(r - radiusParam)); }
    int ri = (int)r + 1, cxC = (int)center[0] + half, cyC = (int)center[1] + half;
    for (int dy = -ri; dy <= ri; ++dy) {
        int row = dy + cyC;
        if (row < 0 || row >= edge) continue;
        for (int dx = -ri; dx <= ri; ++dx) {
            int col = dx + cxC;
            if (col < 0 || col >= edge) continue;
            float dist = sq((float)(dx * dx + dy * dy));
            float c    = pw(falloffPow, NegateBits(dist - radiusParam));
            float w    = kOne1 / (c + kOne1);
            float* cell = &mask[(size_t)row * edge + col];
            if (mode == 0) { float v = w * strength; if (v < *cell) *cell = v; }
            else { float v = (kOne1 - w) * strength; if (*cell <= v && v != *cell) *cell = v; }
        }
    }
}

}  // namespace old

// The memo brush: the loop of the new wg_StampBrush_eee0 / wg_eee0_radial_brush.
void MemoBrush(WgBrushCurveCache& cache, int dim, const float* center, float radiusParam, float falloffPow,
               float strength, char mode, float* mask) {
    const float kOne1 = 1.0f;
    int edge = dim + 1, half = dim >> 1;
    WgBrushCurveCache::Curve& curve = cache.Get(falloffPow, radiusParam);
    int ri = curve.span, cxC = (int)center[0] + half, cyC = (int)center[1] + half;
    for (int dy = -ri; dy <= ri; ++dy) {
        int row = dy + cyC;
        if (row < 0 || row >= edge) continue;
        for (int dx = -ri; dx <= ri; ++dx) {
            int col = dx + cxC;
            if (col < 0 || col >= edge) continue;
            float w = cache.Weight(curve, dx * dx + dy * dy);
            float* cell = &mask[(size_t)row * edge + col];
            if (mode == 0) { float v = w * strength; if (v < *cell) *cell = v; }
            else { float v = (kOne1 - w) * strength; if (*cell <= v && v != *cell) *cell = v; }
        }
    }
}

float PowfFn(float b, float e) { return powf(b, e); }
float StdPowFn(float b, float e) { return std::pow(b, e); }
float SqrtfFn(float v) { return sqrtf(v); }
float StdSqrtFn(float v) { return std::sqrt(v); }

void MakeNoise(std::mt19937& rng, float persist, float octaves, RtUtility::PerlinNoise* pn) {
    const int a = (int)(rng() & 0xffffff), b = (int)(rng() & 0xffffff);
    *pn = RtUtility::PerlinNoise{};
    RtUtility_PerlinNoise_Ctor(pn, persist, octaves, b, a);
}

// ---------------------------------------------------------------------------
// e5d0 + cf440.
// ---------------------------------------------------------------------------
UNIT_TEST(wg_parallel_base_and_blend_match_sequential) {
    const int dims[] = {16, 66, 130, 256};
    const uint32_t seeds[] = {1u, 0xC0FFEEu, 424242u};
    for (int dim : dims) {
        for (uint32_t seed : seeds) {
            std::mt19937 rng(seed ^ (uint32_t)dim);
            RtUtility::PerlinNoise pn1, pn2, pnb;
            MakeNoise(rng, 0.5f, 5.0f, &pn1);
            MakeNoise(rng, 0.25f, 5.0f, &pn2);
            MakeNoise(rng, 0.25f, 4.0f, &pnb);
            const size_t cells = (size_t)(dim + 1) * (dim + 1);
            std::vector<float> refH(cells), refScratch((size_t)dim * dim * 6);
            std::vector<uint8_t> refOut((size_t)dim * dim * 4);
            old::BaseHeightmap(refH.data(), dim, pn1, pn2);
            // A carved field so every blend branch (rock < -0.5, ramps > 7 / 8, slopes) is hit.
            std::vector<float> field(refH);
            for (size_t i = 0; i < cells; ++i) field[i] = field[i] * 0.6f - 3.0f + (float)((i * 2654435761u) % 7) * 0.4f;
            old::Blend(field.data(), dim, pnb, refScratch.data(), refOut.data());

            for (int workers : kWorkerSets) {
                System::Parallel::SetWorkers(workers);
                std::vector<float> h(cells, -1.0f), scratch(refScratch.size(), -1.0f);
                std::vector<uint8_t> out(refOut.size(), 0xcd);
                WgCells_BaseHeightmap(h.data(), dim, &pn1, &pn2);
                CHECK(std::memcmp(h.data(), refH.data(), cells * sizeof(float)) == 0);
                WgCells_BlendScratch(field.data(), dim, scratch.data());
                CHECK(std::memcmp(scratch.data(), refScratch.data(), scratch.size() * sizeof(float)) == 0);
                WgCells_BlendRockGrass(scratch.data(), dim, &pnb, out.data());
                WgCells_BlendOcclusion(scratch.data(), dim, out.data());
                CHECK(std::memcmp(out.data(), refOut.data(), out.size()) == 0);
            }
        }
    }
    System::Parallel::Shutdown();
}

// ---------------------------------------------------------------------------
// The eee0 falloff memo.
// ---------------------------------------------------------------------------
float RefWeight(float (*pw)(float, float), float (*sq)(float), float base, float cutoff, int s) {
    float d = sq((float)s);
    float f = pw(base, old::NegateBits(d - cutoff));
    return 1.0f / (f + 1.0f);
}

int RefSpan(float (*pw)(float, float), float base, float cutoff) {
    float r = 1.0f;
    float fall = pw(base, old::NegateBits(1.0f - cutoff));
    while (1.0f / (fall + 1.0f) < 0.99f) { r += 1.0f; fall = pw(base, old::NegateBits(r - cutoff)); }
    return (int)r + 1;
}

UNIT_TEST(wg_brush_curve_memo_matches_per_cell_expression) {
    struct Fns { float (*pw)(float, float); float (*sq)(float); } fns[] = {
        {&PowfFn, &SqrtfFn}, {&StdPowFn, &StdSqrtFn}};
    // The game's pairs: ring inner (1.25, 10) / outer (4, 4), path inner (1.25, 8) /
    // outer (16, 1.5); satellites 2.25 and rivers 1.75 with random radii.
    std::vector<std::pair<float, float>> keys = {{1.25f, 10.0f}, {4.0f, 4.0f}, {1.25f, 8.0f}, {16.0f, 1.5f}};
    std::mt19937 rng(77);
    std::uniform_real_distribution<float> rad(1.0f, 24.0f), bas(1.2f, 20.0f);
    for (int i = 0; i < 40; ++i) keys.push_back({i % 2 ? 2.25f : 1.75f, rad(rng)});
    for (int i = 0; i < 20; ++i) keys.push_back({bas(rng), rad(rng)});
    for (const Fns& fn : fns) {
        WgBrushCurveCache cache(fn.pw, fn.sq, 1.0f, 0.99f);
        // Interleave so entries are evicted and re-keyed.
        for (int pass = 0; pass < 3; ++pass) {
            for (size_t k = 0; k < keys.size(); ++k) {
                const float base = keys[(k * 7 + pass) % keys.size()].first;
                const float cutoff = keys[(k * 7 + pass) % keys.size()].second;
                WgBrushCurveCache::Curve& c = cache.Get(base, cutoff);
                CHECK_EQ(c.span, RefSpan(fn.pw, base, cutoff));
                bool same = true;
                for (int dy = -c.span; dy <= c.span; ++dy)
                    for (int dx = -c.span; dx <= c.span; ++dx) {
                        const float a = cache.Weight(c, dx * dx + dy * dy);
                        const float b = RefWeight(fn.pw, fn.sq, base, cutoff, dx * dx + dy * dy);
                        same = same && std::memcmp(&a, &b, 4) == 0;
                    }
                CHECK(same);
            }
        }
    }
}

UNIT_TEST(wg_brush_memo_application_matches_old_brush) {
    const int dim = 192;
    const size_t cells = (size_t)(dim + 1) * (dim + 1);
    std::mt19937 rng(9001);
    std::uniform_real_distribution<float> pos(-110.0f, 110.0f), val(0.0f, 1.5f), rad(1.0f, 14.0f);
    for (int f = 0; f < 2; ++f) {
        float (*pw)(float, float) = f ? &StdPowFn : &PowfFn;
        float (*sq)(float) = f ? &StdSqrtFn : &SqrtfFn;
        WgBrushCurveCache cache(pw, sq, 1.0f, 0.99f);
        std::vector<float> a(cells), b(cells);
        for (size_t i = 0; i < cells; ++i) a[i] = b[i] = val(rng);
        for (int i = 0; i < 400; ++i) {
            const float center[2] = {pos(rng), pos(rng)};   // some off the map edge
            float base, cutoff, strength;
            switch (i % 4) {
            case 0:  base = 1.25f; cutoff = 8.0f; strength = 1.0f; break;
            case 1:  base = 16.0f; cutoff = 1.5f; strength = 1.5f; break;
            case 2:  base = 2.25f; cutoff = rad(rng); strength = 2.0f; break;
            default: base = 1.75f; cutoff = rad(rng); strength = 1.0f; break;
            }
            const char mode = (char)(i % 3 == 0 ? 0 : 1);
            old::Brush(pw, sq, dim, center, cutoff, base, strength, mode, a.data());
            MemoBrush(cache, dim, center, cutoff, base, strength, mode, b.data());
        }
        CHECK(std::memcmp(a.data(), b.data(), cells * sizeof(float)) == 0);
    }
}

// ---------------------------------------------------------------------------
// [PERLINPRE] the start town's scatter noise table (starttown.cpp StartTown_SpawnResources)
// vs the old per-cell call of its serial loop. docs/extensions/handins/PERLINPRE.md
// ---------------------------------------------------------------------------
UNIT_TEST(wg_parallel_scatter_perlin_table_matches_per_cell_call) {
    const int dims[] = {16, 97, 130, 256};                  // 97 / 130: not powers of two
    const float divs[] = {1.0f, 8.0f, 16.5f, 40.0f, 0.75f};  // scale = uvScale / div, as the caller
    const uint32_t seeds[] = {7u, 0xBEEFu};
    for (int dim : dims) {
        for (uint32_t seed : seeds) {
            std::mt19937 rng(seed ^ (uint32_t)(dim * 31));
            RtUtility::PerlinNoise pn;
            MakeNoise(rng, 0.5f + 0.05f * (float)(seed & 3), 4.0f + (float)(dim & 1), &pn);
            for (float div : divs) {
                const float uvScale = 1.0f;                  // kRdata_14017214c
                const float scale = uvScale / div;
                const size_t cells = (size_t)dim * dim;
                std::vector<float> ref(cells);
                for (int y = 0; y < dim; ++y)
                    for (int x = 0; x < dim; ++x)
                        ref[(size_t)dim * y + x] = RtUtility_PerlinNoise_Generate(&pn, (float)x * scale, (float)y * scale);
                for (int workers : {0, 1, 2, 4, 7}) {
                    System::Parallel::SetWorkers(workers);
                    std::vector<float> table(cells, -7.0f);
                    WgCells_PerlinTable(table.data(), dim, &pn, scale);
                    CHECK(std::memcmp(table.data(), ref.data(), cells * sizeof(float)) == 0);
                }
            }
        }
    }
    System::Parallel::Shutdown();
}

}  // namespace
