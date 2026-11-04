// ============================================================================
// src/ext/sharp_ui/sharp_encode.cpp -- ext.sharp_ui's BC3 mip chain (NOT RETAIL)
// ============================================================================
// sharp_encode.h has the contract. docs/extensions/handins/SHARP_SPRITES.md.
// ============================================================================
#include "ext/sharp_ui/sharp_encode.h"

#include "bc4/bc4.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

namespace Ext {
namespace SharpUI {

namespace {

// ---- RGB565 ----------------------------------------------------------------

int Expand5(int q) {
    return (q << 3) | (q >> 2);
}

int Expand6(int q) {
    return (q << 2) | (q >> 4);
}

// The 5- or 6-bit value whose expansion is nearest v.
int Quantise(float v, int bits) {
    const int top = (1 << bits) - 1;
    const int guess = static_cast<int>(std::lround(v * static_cast<float>(top) / 255.0f));
    int best = 0;
    float bestErr = 1e30f;
    for (int q = guess - 1; q <= guess + 1; ++q) {
        if (q < 0 || q > top) continue;
        const float e = static_cast<float>(bits == 5 ? Expand5(q) : Expand6(q)) - v;
        if (e * e < bestErr) {
            bestErr = e * e;
            best = q;
        }
    }
    return best;
}

uint16_t Pack565(int r5, int g6, int b5) {
    return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

uint16_t Quantise565(const float rgb[3]) {
    return Pack565(Quantise(rgb[0], 5), Quantise(rgb[1], 6), Quantise(rgb[2], 5));
}

// The four-colour palette of (c0, c1), rounded as bc_decode.cpp rounds it.
void Palette(uint16_t c0, uint16_t c1, int pal[4][3]) {
    pal[0][0] = Expand5(c0 >> 11);
    pal[0][1] = Expand6((c0 >> 5) & 63);
    pal[0][2] = Expand5(c0 & 31);
    pal[1][0] = Expand5(c1 >> 11);
    pal[1][1] = Expand6((c1 >> 5) & 63);
    pal[1][2] = Expand5(c1 & 31);
    for (int k = 0; k < 3; ++k) {
        pal[2][k] = (2 * pal[0][k] + pal[1][k] + 1) / 3;
        pal[3][k] = (pal[0][k] + 2 * pal[1][k] + 1) / 3;
    }
}

// ---- The exact single-colour fit ------------------------------------------
// For each 8-bit value and each channel width, the endpoint pair (hi, lo) whose
// index-2 entry (2 hi + lo + 1) / 3 is nearest the value: a flat block of any
// colour is then within the palette's own rounding, not the 565 grid's.
struct SingleColourTable {
    uint8_t hi5[256];
    uint8_t lo5[256];
    uint8_t hi6[256];
    uint8_t lo6[256];

    SingleColourTable() {
        Fill(5, hi5, lo5);
        Fill(6, hi6, lo6);
    }

    static void Fill(int bits, uint8_t* hi, uint8_t* lo) {
        const int n = 1 << bits;
        for (int v = 0; v < 256; ++v) {
            int bestErr = 1 << 30;
            for (int a = 0; a < n; ++a) {
                const int ea = bits == 5 ? Expand5(a) : Expand6(a);
                for (int b = 0; b < n; ++b) {
                    const int eb = bits == 5 ? Expand5(b) : Expand6(b);
                    const int e = std::abs((2 * ea + eb + 1) / 3 - v);
                    // Prefer the closer pair: a wider one costs more on texels that differ.
                    const int score = e * 1024 + std::abs(ea - eb);
                    if (score < bestErr) {
                        bestErr = score;
                        hi[v] = static_cast<uint8_t>(a);
                        lo[v] = static_cast<uint8_t>(b);
                    }
                }
            }
        }
    }
};

const SingleColourTable& SingleColour() {
    static const SingleColourTable table;
    return table;
}

// ---- One colour block -----------------------------------------------------

struct ColourBlock {
    float rgb[16][3];
    float weight[16];  // alpha (0..255); 0 for texels outside the level
};

// Nearest palette entry per texel; returns the weighted squared error.
float FitIndices(const ColourBlock& b, uint16_t c0, uint16_t c1, uint8_t idx[16]) {
    int pal[4][3];
    Palette(c0, c1, pal);
    float total = 0.0f;
    for (int i = 0; i < 16; ++i) {
        float best = 1e30f;
        int bestK = 0;
        for (int k = 0; k < 4; ++k) {
            const float dr = b.rgb[i][0] - static_cast<float>(pal[k][0]);
            const float dg = b.rgb[i][1] - static_cast<float>(pal[k][1]);
            const float db = b.rgb[i][2] - static_cast<float>(pal[k][2]);
            const float d = dr * dr + dg * dg + db * db;
            if (d < best) {
                best = d;
                bestK = k;
            }
        }
        idx[i] = static_cast<uint8_t>(bestK);
        total += b.weight[i] * best;
    }
    return total;
}

// Least-squares endpoints for fixed indices: the palette entry of index k is
// (1 - t) e0 + t e1 with t = 0, 1, 1/3, 2/3. False when the system is singular.
bool SolveEndpoints(const ColourBlock& b, const uint8_t idx[16], float e0[3], float e1[3]) {
    static const float kT[4] = { 0.0f, 1.0f, 1.0f / 3.0f, 2.0f / 3.0f };
    float aa = 0.0f;
    float ab = 0.0f;
    float bb = 0.0f;
    float x0[3] = { 0.0f, 0.0f, 0.0f };
    float x1[3] = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 16; ++i) {
        const float w = b.weight[i];
        if (w <= 0.0f) continue;
        const float t = kT[idx[i]];
        const float s = 1.0f - t;
        aa += w * s * s;
        ab += w * s * t;
        bb += w * t * t;
        for (int k = 0; k < 3; ++k) {
            x0[k] += w * s * b.rgb[i][k];
            x1[k] += w * t * b.rgb[i][k];
        }
    }
    const float det = aa * bb - ab * ab;
    if (std::fabs(det) <= 1e-6f * (aa * bb + 1e-12f)) return false;
    const float inv = 1.0f / det;
    for (int k = 0; k < 3; ++k) {
        e0[k] = std::min(255.0f, std::max(0.0f, (bb * x0[k] - ab * x1[k]) * inv));
        e1[k] = std::min(255.0f, std::max(0.0f, (aa * x1[k] - ab * x0[k]) * inv));
    }
    return true;
}

struct Candidate {
    uint16_t c0 = 0;
    uint16_t c1 = 0;
    uint8_t  idx[16] = {};
    float    err = 1e30f;
};

void Consider(const ColourBlock& b, uint16_t c0, uint16_t c1, Candidate& best) {
    Candidate c;
    c.c0 = c0;
    c.c1 = c1;
    c.err = FitIndices(b, c0, c1, c.idx);
    if (c.err < best.err) best = c;
}

// The weighted principal axis of the block's colours (power iteration).
void PrincipalAxis(const ColourBlock& b, const float mean[3], float sumW, float axis[3]) {
    float cov[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };  // rr rg rb gg gb bb
    for (int i = 0; i < 16; ++i) {
        const float w = b.weight[i];
        if (w <= 0.0f) continue;
        const float r = b.rgb[i][0] - mean[0];
        const float g = b.rgb[i][1] - mean[1];
        const float bl = b.rgb[i][2] - mean[2];
        cov[0] += w * r * r;
        cov[1] += w * r * g;
        cov[2] += w * r * bl;
        cov[3] += w * g * g;
        cov[4] += w * g * bl;
        cov[5] += w * bl * bl;
    }
    for (float& c : cov)
        c /= sumW;
    // Start from the covariance row of the widest channel: never orthogonal to the axis.
    float v[3];
    if (cov[0] >= cov[3] && cov[0] >= cov[5]) {
        v[0] = cov[0];
        v[1] = cov[1];
        v[2] = cov[2];
    } else if (cov[3] >= cov[5]) {
        v[0] = cov[1];
        v[1] = cov[3];
        v[2] = cov[4];
    } else {
        v[0] = cov[2];
        v[1] = cov[4];
        v[2] = cov[5];
    }
    for (int iter = 0; iter < 8; ++iter) {
        const float x = cov[0] * v[0] + cov[1] * v[1] + cov[2] * v[2];
        const float y = cov[1] * v[0] + cov[3] * v[1] + cov[4] * v[2];
        const float z = cov[2] * v[0] + cov[4] * v[1] + cov[5] * v[2];
        const float m = std::max(std::fabs(x), std::max(std::fabs(y), std::fabs(z)));
        if (m <= 1e-12f) break;
        v[0] = x / m;
        v[1] = y / m;
        v[2] = z / m;
    }
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len <= 1e-12f) {
        axis[0] = 0.57735f;
        axis[1] = 0.57735f;
        axis[2] = 0.57735f;
        return;
    }
    axis[0] = v[0] / len;
    axis[1] = v[1] / len;
    axis[2] = v[2] / len;
}

// Writes the eight colour bytes in four-colour order (c0 > c1).
void WriteColourBlock(Candidate c, uint8_t out[8]) {
    if (c.c0 < c.c1) {
        std::swap(c.c0, c.c1);
        for (uint8_t& i : c.idx)
            i ^= 1;  // 0 <-> 1, 2 <-> 3: the same palette, mirrored
    } else if (c.c0 == c.c1) {
        // One colour: c0 exact, c1 one step below it, every texel on the exact endpoint.
        if (c.c0 > 0) {
            c.c1 = static_cast<uint16_t>(c.c0 - 1);
            std::memset(c.idx, 0, sizeof(c.idx));
        } else {
            c.c0 = 1;
            std::memset(c.idx, 1, sizeof(c.idx));
        }
    }
    uint32_t bits = 0;
    for (int i = 0; i < 16; ++i)
        bits |= static_cast<uint32_t>(c.idx[i]) << (2 * i);
    out[0] = static_cast<uint8_t>(c.c0 & 0xff);
    out[1] = static_cast<uint8_t>(c.c0 >> 8);
    out[2] = static_cast<uint8_t>(c.c1 & 0xff);
    out[3] = static_cast<uint8_t>(c.c1 >> 8);
    out[4] = static_cast<uint8_t>(bits & 0xff);
    out[5] = static_cast<uint8_t>((bits >> 8) & 0xff);
    out[6] = static_cast<uint8_t>((bits >> 16) & 0xff);
    out[7] = static_cast<uint8_t>(bits >> 24);
}

void EncodeColourBlock(ColourBlock& b, uint8_t out[8]) {
    float sumW = 0.0f;
    for (int i = 0; i < 16; ++i)
        sumW += std::max(0.0f, b.weight[i]);
    Candidate best;
    if (sumW <= 0.0f) {
        // No coverage: no colour shows. Fit the plain colours of the level's texels anyway
        // (a mip below blends this block's colour with its neighbours' only through alpha).
        // The texels outside the level are marked by a negative weight from the caller.
        for (float& w : b.weight)
            w = w < 0.0f ? 0.0f : 1.0f;
        sumW = 0.0f;
        for (float w : b.weight)
            sumW += w;
    } else {
        for (float& w : b.weight)
            w = std::max(0.0f, w);
    }
    float mean[3] = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 16; ++i) {
        for (int k = 0; k < 3; ++k)
            mean[k] += b.weight[i] * b.rgb[i][k];
    }
    for (float& m : mean)
        m /= sumW;

    // 1. The single-colour fit of the mean (exact for a flat block).
    const SingleColourTable& sc = SingleColour();
    const int mr = std::min(255, std::max(0, static_cast<int>(std::lround(mean[0]))));
    const int mg = std::min(255, std::max(0, static_cast<int>(std::lround(mean[1]))));
    const int mb = std::min(255, std::max(0, static_cast<int>(std::lround(mean[2]))));
    Consider(b, Pack565(sc.hi5[mr], sc.hi6[mg], sc.hi5[mb]), Pack565(sc.lo5[mr], sc.lo6[mg], sc.lo5[mb]), best);
    if (best.err > 0.0f) {
        // 2. The principal axis, endpoints at the extreme projections of the covered texels.
        float axis[3];
        PrincipalAxis(b, mean, sumW, axis);
        float tMin = 1e30f;
        float tMax = -1e30f;
        for (int i = 0; i < 16; ++i) {
            if (b.weight[i] <= 0.0f) continue;
            const float t = (b.rgb[i][0] - mean[0]) * axis[0] + (b.rgb[i][1] - mean[1]) * axis[1] +
                            (b.rgb[i][2] - mean[2]) * axis[2];
            tMin = std::min(tMin, t);
            tMax = std::max(tMax, t);
        }
        float e0[3];
        float e1[3];
        for (int k = 0; k < 3; ++k) {
            e0[k] = std::min(255.0f, std::max(0.0f, mean[k] + tMax * axis[k]));
            e1[k] = std::min(255.0f, std::max(0.0f, mean[k] + tMin * axis[k]));
        }
        Candidate fit;
        Consider(b, Quantise565(e0), Quantise565(e1), fit);
        // 3. Least-squares refinement to the chosen indices, while it lowers the error.
        for (int iter = 0; iter < 3; ++iter) {
            if (!SolveEndpoints(b, fit.idx, e0, e1)) break;
            Candidate next = fit;
            Consider(b, Quantise565(e0), Quantise565(e1), next);
            if (!(next.err < fit.err)) break;
            fit = next;
        }
        if (fit.err < best.err) best = fit;
    }
    WriteColourBlock(best, out);
}

}  // namespace

// ---- Mip chain --------------------------------------------------------------

int MipLevelCount(int w, int h) {
    int m = std::max(w, h);
    int n = 1;
    while (m > 1) {
        m >>= 1;
        ++n;
    }
    return n;
}

int MipSize(int size, int level) {
    return std::max(1, size >> level);
}

MipChain BuildMipChain(const uint8_t* bgra, int w, int h, int pitch) {
    MipChain chain;
    chain.width = w;
    chain.height = h;
    const int levels = MipLevelCount(w, h);
    size_t total = 0;
    for (int i = 0; i < levels; ++i) {
        chain.offsets.push_back(total);
        total += static_cast<size_t>(MipSize(w, i)) * static_cast<size_t>(MipSize(h, i)) * 4;
    }
    chain.pixels.resize(total);
    for (int y = 0; y < h; ++y)
        std::memcpy(&chain.pixels[static_cast<size_t>(y) * w * 4], bgra + static_cast<size_t>(y) * pitch,
                    static_cast<size_t>(w) * 4);
    for (int i = 1; i < levels; ++i) {
        const int sw = MipSize(w, i - 1);
        const int sh = MipSize(h, i - 1);
        const int dw = MipSize(w, i);
        const int dh = MipSize(h, i);
        const uint8_t* src = &chain.pixels[chain.offsets[i - 1]];
        uint8_t* dst = &chain.pixels[chain.offsets[i]];
        for (int y = 0; y < dh; ++y) {
            const int y0 = std::min(2 * y, sh - 1);
            const int y1 = std::min(2 * y + 1, sh - 1);
            for (int x = 0; x < dw; ++x) {
                const int x0 = std::min(2 * x, sw - 1);
                const int x1 = std::min(2 * x + 1, sw - 1);
                const uint8_t* p[4] = {
                    src + (static_cast<size_t>(y0) * sw + x0) * 4,
                    src + (static_cast<size_t>(y0) * sw + x1) * 4,
                    src + (static_cast<size_t>(y1) * sw + x0) * 4,
                    src + (static_cast<size_t>(y1) * sw + x1) * 4,
                };
                uint32_t sumA = 0;
                uint32_t sumC[3] = { 0, 0, 0 };
                uint32_t sumCa[3] = { 0, 0, 0 };
                for (int k = 0; k < 4; ++k) {
                    const uint32_t a = p[k][3];
                    sumA += a;
                    for (int c = 0; c < 3; ++c) {
                        sumC[c] += p[k][c];
                        sumCa[c] += p[k][c] * a;
                    }
                }
                uint8_t* o = dst + (static_cast<size_t>(y) * dw + x) * 4;
                for (int c = 0; c < 3; ++c) {
                    const uint32_t v = sumA > 0 ? (sumCa[c] + sumA / 2) / sumA : (sumC[c] + 2) / 4;
                    o[c] = static_cast<uint8_t>(v);
                }
                o[3] = static_cast<uint8_t>((sumA + 2) / 4);
            }
        }
    }
    return chain;
}

// ---- BC3 ----------------------------------------------------------------------

size_t Bc3LevelSize(int w, int h) {
    const size_t bw = static_cast<size_t>(std::max(1, (w + 3) / 4));
    const size_t bh = static_cast<size_t>(std::max(1, (h + 3) / 4));
    return bw * bh * 16;
}

int Bc3RowPitch(int w) {
    return std::max(1, (w + 3) / 4) * 16;
}

void EncodeBc3Block(const uint8_t bgra[64], unsigned valid, uint8_t out[16]) {
    uint8_t alpha[16];
    ColourBlock b;
    for (int i = 0; i < 16; ++i) {
        const uint8_t* p = bgra + i * 4;
        alpha[i] = p[3];
        b.rgb[i][0] = static_cast<float>(p[2]);
        b.rgb[i][1] = static_cast<float>(p[1]);
        b.rgb[i][2] = static_cast<float>(p[0]);
        // Outside the level: -1, so the no-coverage fallback can tell it from a transparent texel.
        b.weight[i] = (valid >> i) & 1u ? static_cast<float>(p[3]) : -1.0f;
    }
    Bc4::EncodeBlock(alpha, valid & 0xffffu, out);
    EncodeColourBlock(b, out + 8);
}

void EncodeBc3Rows(const uint8_t* bgra, int w, int h, int pitch, int rowBegin, int rowEnd, uint8_t* dst) {
    const int bw = std::max(1, (w + 3) / 4);
    uint8_t texels[64];
    for (int by = rowBegin; by < rowEnd; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            unsigned valid = 0;
            for (int y = 0; y < 4; ++y) {
                const int py = by * 4 + y;
                const int sy = std::min(py, h - 1);
                for (int x = 0; x < 4; ++x) {
                    const int px = bx * 4 + x;
                    const int sx = std::min(px, w - 1);
                    std::memcpy(texels + (y * 4 + x) * 4, bgra + static_cast<size_t>(sy) * pitch + sx * 4, 4);
                    if (px < w && py < h) valid |= 1u << (y * 4 + x);
                }
            }
            EncodeBc3Block(texels, valid, dst + (static_cast<size_t>(by) * bw + bx) * 16);
        }
    }
}

void EncodeBc3(const uint8_t* bgra, int w, int h, int pitch, uint8_t* dst) {
    EncodeBc3Rows(bgra, w, h, pitch, 0, std::max(1, (h + 3) / 4), dst);
}

int DefaultEncodeThreads() {
    const unsigned hw = std::thread::hardware_concurrency();
    int n = static_cast<int>(hw / 2);
    if (n > 8) n = 8;
    if (n < 1) n = 1;
    return n;
}

Bc3Chain EncodeBc3Chain(const MipChain& chain, int threads) {
    Bc3Chain out;
    out.width = chain.width;
    out.height = chain.height;
    const int levels = static_cast<int>(chain.offsets.size());
    size_t total = 0;
    for (int i = 0; i < levels; ++i) {
        out.offsets.push_back(total);
        total += Bc3LevelSize(MipSize(chain.width, i), MipSize(chain.height, i));
    }
    out.data.resize(total);

    // Bands of block rows; a 4096-wide level is 1024 blocks a row, so 8 rows is ~8k blocks.
    struct Band {
        int level;
        int rowBegin;
        int rowEnd;
    };
    const int kBandRows = 8;
    std::vector<Band> bands;
    for (int i = 0; i < levels; ++i) {
        const int rows = std::max(1, (MipSize(chain.height, i) + 3) / 4);
        for (int r = 0; r < rows; r += kBandRows)
            bands.push_back(Band{ i, r, std::min(rows, r + kBandRows) });
    }
    std::atomic<size_t> next(0);
    auto work = [&]() {
        for (;;) {
            const size_t k = next.fetch_add(1);
            if (k >= bands.size()) return;
            const Band& band = bands[k];
            const int w = MipSize(chain.width, band.level);
            const int h = MipSize(chain.height, band.level);
            EncodeBc3Rows(&chain.pixels[chain.offsets[band.level]], w, h, w * 4, band.rowBegin, band.rowEnd,
                          &out.data[out.offsets[band.level]]);
        }
    };
    int n = threads > 0 ? threads : DefaultEncodeThreads();
    n = std::max(1, std::min(n, static_cast<int>(bands.size())));
    std::vector<std::thread> pool;
    for (int t = 1; t < n; ++t)
        pool.emplace_back(work);
    work();
    for (std::thread& t : pool)
        t.join();
    return out;
}

}  // namespace SharpUI
}  // namespace Ext
