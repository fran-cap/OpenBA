// ============================================================================
// src/game/Game/worldgen_cells.cpp -- the per-cell loops of e5d0 and cf440 (block WGPAR)
// ============================================================================
// worldgen_cells.h has the contract. The loop bodies are worldgen.cpp's, verbatim;
// only the row range comes from System::ParallelFor.
// ============================================================================
#include "Game/worldgen_cells.h"

#include "system/parallel_for.h"   // System::ParallelFor
#include "utility/PerlinNoise.h"   // RtUtility_PerlinNoise_Generate (const, stateless)
#include "crt/msvc_libm.h"         // MsvcLibm::Powf: the MSVC CRT powf the exe linked, bit-exact (worldgen is
                                   // seed-deterministic; MinGW's powf differs in the last bit and differs again
                                   // between mingw-w64 runtimes, 2026-10-26 GCC 13 -> 16 WorldHash f32 change)

#include <cmath>
#include <cstddef>
#include <cstdint>

// .rdata float constants of e5d0 (verified via read_memory; blueprint §2).
static constexpr float kQuarter    = 0.25f;  // 0x140172100  pn2 persistence / falloff bias / +0.25 / pn1 coord
static constexpr float kClampFloor = 2.0f;   // 0x140172174  min height
static constexpr float kOne        = 1.0f;   // 0x14017214c  g_fUVScale
static constexpr float kPn2Coord   = 0.1f;   // 0x1401720e4  pn2 Generate coord scale
static constexpr float kFalloffBase= 1.1f;   // 0x140172150  powf base (radial falloff)
static constexpr float kFalloffBias= 16.0f;  // 0x1401721c0  falloff dist bias
static constexpr float kBaseScale  = 8.0f;   // 0x1401721a8  (pn1+1)*8
static constexpr float kHScale     = 48.0f;  // 0x1401721e4  (pn2+0.25)*48

// .rdata constants of cf440 (verified bytes at the listed addresses).
static constexpr float kCoord     = 0.25f;        // 0x140172100  perlin coord scale / persistence
static constexpr float kRRemap    = 0.5f;         // 0x140172114  (pn+1)*0.5
static constexpr float k255       = 255.0f;       // 0x14017222c
static constexpr float kRockHMin  = -0.5f;        // 0x140172278  below => full rock
static constexpr float kHi8       = 8.0f;         // 0x1401721a8  rock high-band start
static constexpr float kHi7       = 7.0f;         // 0x1401721a0  grass high-band start
static constexpr float kBandDiv   = 3.0f;         // 0x140172180  height-band divisor
static constexpr float kGUpper    = 0.998969f;    // 0x140172148  grass slope upper clamp
static constexpr float kGLower    = 0.85f;        // 0x140172130  grass slope lower band (bits 0x3f59999a, re-read 2026-10-11)
static constexpr float kGBandDiv  = 0.149047f;    // 0x1401720f0  grass slope band divisor

void WgCells_BaseHeightmap(float* heightmap, int dim,
                           const RtUtility::PerlinNoise* pn1, const RtUtility::PerlinNoise* pn2) {
    int edge = dim + 1;                          // (dim+1) vertices per side
    int half = dim >> 1;
    float distBias = (float)dim * kQuarter + kFalloffBias;      // dim*0.25 + 16

    // [WGPAR] rows split across System::ParallelFor. EXACT: every cell is a pure
    // function of (row, col) and the two noise objects -- the four RandTol draws happen
    // in the caller (worldgen.cpp e5d0) before the loop, PerlinNoise_Generate reads its
    // const object only (no state, no RNG), and nothing accumulates across cells -- so
    // each cell gets the same float operations in the same order whichever thread
    // computes it.
    System::ParallelFor(0, edge, 8, [&](int rowBegin, int rowEnd) {
        for (int row = rowBegin; row < rowEnd; ++row) {
            int dz = row - half;
            for (int col = 0; col < edge; ++col) {
                int dx = col - half;
                float dist  = sqrtf((float)(dx * dx + dz * dz));
                float curve = MsvcLibm::Powf(kFalloffBase, distBias - dist);  // 1.1 ^ ((dim*0.25+16) - dist), the exe's CRT powf
                float w     = kOne / (curve + kOne);

                float pn1s = RtUtility_PerlinNoise_Generate(pn1, (float)col * kQuarter, (float)row * kQuarter);
                float base = (pn1s + kOne) * kBaseScale;             // (pn1+1)*8
                float pn2s = RtUtility_PerlinNoise_Generate(pn2, (float)col * kPn2Coord, (float)row * kPn2Coord);
                float h    = (pn2s + kQuarter) * kHScale + pn1s;     // (pn2+0.25)*48 + pn1
                if (h < kClampFloor) h = kClampFloor;

                heightmap[row * edge + col] = (kOne - w) * base + h * w;
            }
        }
    });
}

// ============================================================
// cf150 — PER-CELL NORMAL via heightmap central differences.
// Verified from FUN_1400cf150 @0x1400cf150.
//
//   normal = normalize( -(h[r][c+1] - h[r][c-1]),     // -dh/dx
//                        -(h[r+1][c] - h[r-1][c]),     // -dh/dy
//                         2.0 )                         // DAT_140172174
//   All neighbour indices are clamped to [0, dim] (heightmap stride = dim+1).
//   The XOR-with-0x80000000 in the decomp is a float negate.
// Writes {nx,ny,nz} into out[0..2].
// ============================================================
static inline void wg_CellNormal_cf150(const float* hm, int dim, int col, int row,
                                        float out[3]) {
    int edge = dim + 1;
    auto clamp = [dim](int v) { return v < 0 ? 0 : (v > dim ? dim : v); };
    int cR   = clamp(row);
    int cC   = clamp(col);
    int colP = clamp(col + 1);
    int colM = clamp(col - 1);
    int rowP = clamp(row + 1);
    int rowM = clamp(row - 1);

    float nx = -(hm[edge * cR + colP] - hm[edge * cR + colM]);   // -dh/dx
    float ny = -(hm[edge * rowP + cC] - hm[edge * rowM + cC]);   // -dh/dy
    float nz = 2.0f;                                             // DAT_140172174
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    if (len > 0.0f) { nx /= len; ny /= len; nz /= len; }
    out[0] = nx; out[1] = ny; out[2] = nz;
}

void WgCells_BlendScratch(const float* hm, int dim, float* scratch) {
    int edge = dim + 1;
    // [WGPAR] rows split across System::ParallelFor. EXACT: each scratch record is a
    // pure function of (row, col) and the read-only heightmap (cf150 reads only hm).
    System::ParallelFor(0, dim, 8, [&](int rowBegin, int rowEnd) {
        for (int row = rowBegin; row < rowEnd; ++row) {
            for (int col = 0; col < dim; ++col) {
                float* e = scratch + (size_t)(dim * row + col) * 6;
                e[0] = (float)col;
                e[1] = (float)row;
                e[2] = hm[edge * row + col];
                wg_CellNormal_cf150(hm, dim, col, row, &e[3]);
            }
        }
    });
}

void WgCells_BlendRockGrass(const float* scratch, int dim, const RtUtility::PerlinNoise* pn, uint8_t* out) {
    // [WGPAR] rows split. EXACT: each texel's R/G bytes are a pure function of its own
    // scratch record and the const Perlin object (Generate keeps no state; the two
    // RandTol seeds were drawn by the caller, before any split); no running sum crosses texels.
    System::ParallelFor(0, dim, 8, [&](int rowBegin, int rowEnd) {
        for (int row = rowBegin; row < rowEnd; ++row) {
            for (int col = 0; col < dim; ++col) {
                const float* e = scratch + (size_t)(dim * row + col) * 6;
                float height = e[2];
                float nz     = e[5];

                // R: rock scatter.
                uint32_t rock;
                if (height >= kRockHMin) {
                    float pnv = RtUtility_PerlinNoise_Generate(pn, (float)col * kCoord,
                                                        (float)row * kCoord);
                    pnv = pnv * kRRemap + kRRemap;                  // (pn+1)*0.5 -> [0,1]
                    rock = (uint32_t)(long long)(pnv * pnv * k255);
                } else {
                    rock = 0xff;
                }

                // G: grass slope band on normal.z.
                uint32_t grass;
                if (nz >= kGUpper || nz <= kGLower) {
                    grass = (nz <= kGLower) ? 0xff : 0;
                } else {
                    grass = (uint32_t)(long long)((1.0f - (nz - kGLower) / kGBandDiv) * k255);
                }

                // Height ramp: rock above 8.0 (max-blend).
                if (height > kHi8) {
                    float t = (height - kHi8) / kBandDiv;
                    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                    uint8_t v = (uint8_t)(long long)(t * k255);
                    uint8_t rb = (uint8_t)rock;
                    rock = (rb <= v) ? v : rb;
                }
                // Height ramp: grass above 7.0 (max-blend).
                if (height > kHi7) {
                    float t = (height - kHi7) / kBandDiv;
                    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                    uint8_t v = (uint8_t)(long long)(t * k255);
                    uint8_t gb = (uint8_t)grass;
                    if (gb < v) grass = v;
                }

                uint8_t* px = &out[(size_t)(dim * row + col) * 4];   // the cell's 4 bytes
                px[0] = (uint8_t)(rock & 0xff);    // R
                px[1] = (uint8_t)(grass & 0xff);   // G
                px[2] = 0;                         // B (the AO pass below)
                px[3] = 0;                         // A (never written by retail)
            }
        }
    });
}

void WgCells_BlendOcclusion(const float* scratch, int dim, uint8_t* out) {
    // [WGPAR] rows split. EXACT: each texel's AO reads only the scratch array (written
    // by the pass above, which ParallelFor joined before returning) and accumulates its
    // OWN 21x21 sums in the unchanged dz/dx order; it writes only its own byte 2.
    System::ParallelFor(0, dim, 8, [&](int rowBegin, int rowEnd) {
        for (int row = rowBegin; row < rowEnd; ++row) {
            for (int col = 0; col < dim; ++col) {
                const float* ce = scratch + (size_t)(dim * row + col) * 6;
                float cx = ce[0], cy = ce[1], ch = ce[2];
                float cnx = ce[3], cny = ce[4], cnz = ce[5];
                float accumDot = 0.0f, count = 0.0f;
                float sumNx = 0.0f, sumNy = 0.0f, sumNz = 0.0f;

                for (int dz = -10; dz < 10; ++dz) {
                    int nrow = row + dz;
                    if (nrow < 0 || nrow >= dim) continue;
                    for (int dx = -10; dx < 11; ++dx) {
                        int ncol = col + dx;
                        if (ncol < 0 || ncol >= dim) continue;
                        if (dx * dx + dz * dz >= 0x65) continue;     // r^2 < 101 circle clip

                        const float* ne = scratch + (size_t)(dim * nrow + ncol) * 6;
                        float dirx = ne[0] - cx, diry = ne[1] - cy, dirz = ne[2] - ch;
                        float len = sqrtf(dirx * dirx + diry * diry + dirz * dirz);
                        if (len > 0.0f) { dirx /= len; diry /= len; dirz /= len; }
                        float dot = cnx * dirx + cny * diry + cnz * dirz;
                        if (dot < 0.0f) dot = 0.0f;                  // Scalar::Max(dot, 0)
                        accumDot += dot;
                        count += 1.0f;

                        if (dx > -2 && dx < 2 && dz > -2 && dz < 2) { // 3x3 smoothed normal
                            sumNx += ne[3]; sumNy += ne[4]; sumNz += ne[5];
                        }
                    }
                }

                float sl  = sqrtf(sumNx * sumNx + sumNy * sumNy + sumNz * sumNz);
                float snz = (sl > 0.0f) ? sumNz / sl : 0.0f;
                float avg = (count > 0.0f) ? accumDot / count : 0.0f;
                float ao  = (1.0f - avg) * snz * k255;
                uint32_t aoByte = (uint32_t)(long long)ao & 0xff;
                out[(size_t)(dim * row + col) * 4 + 2] |= (uint8_t)aoByte;   // B
            }
        }
    });
}

void WgCells_PerlinTable(float* out, int dim, const RtUtility::PerlinNoise* pn, float scale) {
    // [PERLINPRE] rows split across System::ParallelFor. EXACT: each value is the very
    // call the serial loop made -- Generate on the const object (no state, no RNG; the
    // two RandTol seeds were drawn by the caller, before the ctor) with the same float
    // arguments (float)x * scale, (float)y * scale -- written to its own slot; nothing
    // accumulates across cells. The serial loop reads the table in its own order.
    System::ParallelFor(0, dim, 8, [&](int rowBegin, int rowEnd) {
        for (int y = rowBegin; y < rowEnd; ++y) {
            float* row = out + (size_t)dim * y;
            for (int x = 0; x < dim; ++x)
                row[x] = RtUtility_PerlinNoise_Generate(pn, (float)x * scale, (float)y * scale);
        }
    });
}
