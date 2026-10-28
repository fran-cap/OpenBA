// ============================================================================
// src/render/common/etc2/etc2.cpp -- ETC2 / EAC blocks (etc2.h has the formats and the search)
// ============================================================================
#include "etc2/etc2.h"

#include <cstdint>

namespace Etc2 {

namespace {

// The ETC1 modifier tables: index 0 = +a, 1 = +b, 2 = -a, 3 = -b.
const int kModifiers[8][2] = { { 2, 8 },   { 5, 17 },  { 9, 29 },  { 13, 42 },
                               { 18, 60 }, { 24, 80 }, { 33, 106 }, { 47, 183 } };

// T / H mode distances.
const int kDistances[8] = { 3, 6, 11, 16, 23, 32, 41, 64 };

// The EAC alpha tables.
const int kAlphaTables[16][8] = {
    { -3, -6, -9, -15, 2, 5, 8, 14 }, { -3, -7, -10, -13, 2, 6, 9, 12 }, { -2, -5, -8, -13, 1, 4, 7, 12 },
    { -2, -4, -6, -13, 1, 3, 5, 12 }, { -3, -6, -8, -12, 2, 5, 7, 11 }, { -3, -7, -9, -11, 2, 6, 8, 10 },
    { -4, -7, -8, -11, 3, 6, 7, 10 }, { -3, -5, -8, -11, 2, 4, 7, 10 }, { -2, -6, -8, -10, 1, 5, 7, 9 },
    { -2, -5, -8, -10, 1, 4, 7, 9 },  { -2, -4, -8, -10, 1, 3, 7, 9 },  { -2, -5, -7, -10, 1, 4, 6, 9 },
    { -3, -4, -7, -10, 2, 3, 6, 9 },  { -1, -2, -3, -10, 0, 1, 2, 9 },  { -4, -6, -8, -9, 3, 5, 7, 8 },
    { -3, -5, -7, -9, 2, 4, 6, 8 },
};

// A block whose ETC1-style error is at most this (a mean squared error of 2 per channel, ~45 dB) is
// kept without trying the planar mode.
constexpr int64_t kPlanarGate = 16 * 3 * 2;

// The alpha table whose index 4 is 0: a constant block is exact with it.
constexpr int kFlatAlphaTable = 13;

inline int Clamp255(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

inline int Modifier(int table, int index) {
    const int m = kModifiers[table][index & 1];
    return (index & 2) ? -m : m;
}

inline int Expand4(int q) {
    return q * 17;
}

inline int Expand5(int q) {
    return (q << 3) | (q >> 2);
}

inline int Expand6(int q) {
    return (q << 2) | (q >> 4);
}

inline int Expand7(int q) {
    return (q << 1) | (q >> 6);
}

// v (0..255) to `bits` bits: the code whose expansion is nearest.
int Quantise(int v, int bits) {
    const int top = (1 << bits) - 1;
    int q = (v * top + 127) / 255;
    if (q > top) q = top;
    const auto expand = [bits](int c) {
        switch (bits) {
        case 4: return Expand4(c);
        case 5: return Expand5(c);
        case 6: return Expand6(c);
        default: return Expand7(c);
        }
    };
    int best = q;
    int bestErr = v - expand(q) < 0 ? expand(q) - v : v - expand(q);
    for (int c = q - 1; c <= q + 1; c += 2) {
        if (c < 0 || c > top) continue;
        const int e = v - expand(c) < 0 ? expand(c) - v : v - expand(c);
        if (e < bestErr) {
            best = c;
            bestErr = e;
        }
    }
    return best;
}

inline uint64_t ReadBe(const uint8_t in[8]) {
    uint64_t w = 0;
    for (int i = 0; i < 8; ++i)
        w = (w << 8) | in[i];
    return w;
}

inline void WriteBe(uint64_t w, uint8_t out[8]) {
    for (int i = 7; i >= 0; --i) {
        out[i] = static_cast<uint8_t>(w);
        w >>= 8;
    }
}

inline uint32_t Bits(uint64_t w, int hi, int count) {
    return static_cast<uint32_t>((w >> (hi - count + 1)) & ((1u << count) - 1));
}

// Pixel i (= 4x + y) of the block's 2-bit index fields.
inline int PixelIndex(uint64_t w, int i) {
    return static_cast<int>(((w >> (16 + i)) & 1) << 1 | ((w >> i) & 1));
}

// Whether a differential base (5 bits at `hi`) plus its delta (3 bits below) leaves 0..31.
bool Overflows(uint64_t w, int hi) {
    const int base = static_cast<int>(Bits(w, hi, 5));
    int delta = static_cast<int>(Bits(w, hi - 5, 3));
    if (delta >= 4) delta -= 8;
    const int sum = base + delta;
    return sum < 0 || sum > 31;
}

// ---- the encoder's pieces ------------------------------------------------------------------------

// The pixels (4y + x) of each half: flip 0 left / right, flip 1 top / bottom.
const int kHalfPixels[2][2][8] = { { { 0, 1, 4, 5, 8, 9, 12, 13 }, { 2, 3, 6, 7, 10, 11, 14, 15 } },
                                   { { 0, 1, 2, 3, 4, 5, 6, 7 }, { 8, 9, 10, 11, 12, 13, 14, 15 } } };

// One half's best table for an expanded base: its squared error and per-pixel indices.
struct HalfFit {
    int table = 0;
    int64_t err = 0;
    int index[8] = {};
};

// The table pick ignores the clamp: for a modifier m added to every channel, a pixel's squared error
// is |p - base|^2 + 3 m^2 - 2 m d3 with d3 the sum of its channel offsets, so per table each pixel
// takes the m of least 3 m^2 - 2 m d3 (the sign of d3 picks the side; +b over +a once
// 2 |d3| > 3 (a + b)). The best table's indices and error are then taken exactly, with the clamp.
// `punch`: the punch-through transparent mode, modifiers {0, +b, -b} on opaque pixels and index 2 on
// the transparent ones (`transparent` bit k for pixel k of the half).
HalfFit FitHalf(const uint8_t* px, const int pix[8], const int base[3], bool punch, unsigned transparent) {
    int d3[8];
    for (int k = 0; k < 8; ++k) {
        const uint8_t* p = px + pix[k] * 4;
        d3[k] = (p[0] - base[0]) + (p[1] - base[1]) + (p[2] - base[2]);
    }
    int bestTable = 0;
    int bestScore = INT32_MAX;
    for (int t = 0; t < 8; ++t) {
        const int a = kModifiers[t][0];
        const int b = kModifiers[t][1];
        int score = 0;
        for (int k = 0; k < 8; ++k) {
            if (punch && ((transparent >> k) & 1)) continue;
            const int ad = d3[k] < 0 ? -d3[k] : d3[k];
            if (punch) {
                const int sb = 3 * b * b - 2 * b * ad;
                score += sb < 0 ? sb : 0;
            } else {
                const int m = 2 * ad > 3 * (a + b) ? b : a;
                score += 3 * m * m - 2 * m * ad;
            }
        }
        if (score < bestScore) {
            bestScore = score;
            bestTable = t;
        }
    }
    HalfFit fit;
    fit.table = bestTable;
    for (int k = 0; k < 8; ++k) {
        if (punch && ((transparent >> k) & 1)) {
            fit.index[k] = 2;
            continue;
        }
        const uint8_t* p = px + pix[k] * 4;
        int cand[2];
        if (punch) {
            cand[0] = 0;
            cand[1] = d3[k] >= 0 ? 1 : 3;
        } else {
            cand[0] = d3[k] >= 0 ? 0 : 2;
            cand[1] = d3[k] >= 0 ? 1 : 3;
        }
        int bestIndex = cand[0];
        int bestErr = INT32_MAX;
        for (int c = 0; c < 2; ++c) {
            const int m = punch && cand[c] == 0 ? 0 : Modifier(bestTable, cand[c]);
            int e = 0;
            for (int ch = 0; ch < 3; ++ch) {
                const int diff = p[ch] - Clamp255(base[ch] + m);
                e += diff * diff;
            }
            if (e < bestErr) {
                bestErr = e;
                bestIndex = cand[c];
            }
        }
        fit.index[k] = bestIndex;
        fit.err += bestErr;
    }
    return fit;
}

// The mean colour of a half's pixels, opaque ones only when `punch` (all transparent: grey).
void HalfMean(const uint8_t* px, const int pix[8], bool punch, unsigned transparent, int mean[3]) {
    int sum[3] = { 0, 0, 0 };
    int n = 0;
    for (int k = 0; k < 8; ++k) {
        if (punch && ((transparent >> k) & 1)) continue;
        const uint8_t* p = px + pix[k] * 4;
        for (int ch = 0; ch < 3; ++ch)
            sum[ch] += p[ch];
        ++n;
    }
    for (int ch = 0; ch < 3; ++ch)
        mean[ch] = n > 0 ? (sum[ch] + n / 2) / n : 128;
}

// The base that best fits a half's chosen modifiers: mean(pixel - modifier).
void RefitMean(const uint8_t* px, const int pix[8], const HalfFit& fit, bool punch, unsigned transparent,
               int mean[3]) {
    int sum[3] = { 0, 0, 0 };
    int n = 0;
    for (int k = 0; k < 8; ++k) {
        if (punch && ((transparent >> k) & 1)) continue;
        const int idx = fit.index[k];
        const int m = punch && idx == 0 ? 0 : Modifier(fit.table, idx);
        const uint8_t* p = px + pix[k] * 4;
        for (int ch = 0; ch < 3; ++ch)
            sum[ch] += p[ch] - m;
        ++n;
    }
    for (int ch = 0; ch < 3; ++ch) {
        const int v = n > 0 ? (sum[ch] >= 0 ? (sum[ch] + n / 2) / n : -((-sum[ch] + n / 2) / n)) : 128;
        mean[ch] = Clamp255(v);
    }
}

// An ETC1-style candidate: flip, mode, quantised bases, the two halves' fits.
struct Candidate {
    int64_t err = INT64_MAX;
    int flip = 0;
    bool differential = false;
    int q[2][3] = {};   // the halves' bases, 4 bits (individual) or 5 bits (differential)
    HalfFit fit[2];
};

// Quantised bases of two means for a mode; true when the differential delta had to be clamped.
bool QuantiseBases(const int mean[2][3], bool differential, int q[2][3]) {
    bool clamped = false;
    for (int ch = 0; ch < 3; ++ch) {
        if (!differential) {
            q[0][ch] = Quantise(mean[0][ch], 4);
            q[1][ch] = Quantise(mean[1][ch], 4);
            continue;
        }
        q[0][ch] = Quantise(mean[0][ch], 5);
        int second = Quantise(mean[1][ch], 5);
        if (second - q[0][ch] > 3) {
            second = q[0][ch] + 3;
            clamped = true;
        }
        if (second - q[0][ch] < -4) {
            second = q[0][ch] - 4;
            clamped = true;
        }
        q[1][ch] = second;
    }
    return clamped;
}

void Evaluate(const uint8_t* px, int flip, bool differential, const int q[2][3], bool punch,
              const unsigned transparent[2], Candidate* c) {
    c->err = 0;
    c->flip = flip;
    c->differential = differential;
    for (int h = 0; h < 2; ++h) {
        const int* pix = kHalfPixels[flip][h];
        int base[3];
        for (int ch = 0; ch < 3; ++ch) {
            c->q[h][ch] = q[h][ch];
            base[ch] = differential ? Expand5(q[h][ch]) : Expand4(q[h][ch]);
        }
        c->fit[h] = FitHalf(px, pix, base, punch, transparent[h]);
        c->err += c->fit[h].err;
    }
}

uint64_t PackCandidate(const Candidate& c, bool opaqueBit) {
    uint64_t w = 0;
    if (c.differential) {
        for (int ch = 0; ch < 3; ++ch) {
            const int delta = (c.q[1][ch] - c.q[0][ch]) & 7;
            const int hi = 63 - ch * 8;
            w |= static_cast<uint64_t>(c.q[0][ch]) << (hi - 4);
            w |= static_cast<uint64_t>(delta) << (hi - 7);
        }
    } else {
        for (int ch = 0; ch < 3; ++ch) {
            const int hi = 63 - ch * 8;
            w |= static_cast<uint64_t>(c.q[0][ch]) << (hi - 3);
            w |= static_cast<uint64_t>(c.q[1][ch]) << (hi - 7);
        }
    }
    w |= static_cast<uint64_t>(c.fit[0].table) << 37;
    w |= static_cast<uint64_t>(c.fit[1].table) << 34;
    if (opaqueBit) w |= uint64_t{ 1 } << 33;
    if (c.flip) w |= uint64_t{ 1 } << 32;
    for (int h = 0; h < 2; ++h) {
        const int* pix = kHalfPixels[c.flip][h];
        for (int k = 0; k < 8; ++k) {
            const int x = pix[k] & 3;
            const int y = pix[k] >> 2;
            const int i = 4 * x + y;
            const int idx = c.fit[h].index[k];
            w |= static_cast<uint64_t>(idx >> 1) << (16 + i);
            w |= static_cast<uint64_t>(idx & 1) << i;
        }
    }
    return w;
}

// The best ETC1-style block: both flips, the modes allowed (`individual` false: differential only),
// then the winner's bases refitted once.
Candidate SearchEtc1(const uint8_t* px, bool individual, bool punch, unsigned transparentMask) {
    Candidate best;
    for (int flip = 0; flip < 2; ++flip) {
        int mean[2][3];
        unsigned transparent[2] = { 0, 0 };
        for (int h = 0; h < 2; ++h) {
            const int* pix = kHalfPixels[flip][h];
            for (int k = 0; k < 8; ++k) {
                if ((transparentMask >> pix[k]) & 1) transparent[h] |= 1u << k;
            }
            HalfMean(px, pix, punch, transparent[h], mean[h]);
        }
        // Differential first (5-bit bases); individual (4-bit) only where the delta did not fit.
        int q[2][3];
        const bool clamped = QuantiseBases(mean, true, q);
        Candidate c;
        Evaluate(px, flip, true, q, punch, transparent, &c);
        if (c.err < best.err) best = c;
        if (individual && clamped) {
            QuantiseBases(mean, false, q);
            Evaluate(px, flip, false, q, punch, transparent, &c);
            if (c.err < best.err) best = c;
        }
    }
    // One refit: each half's base to its chosen modifiers.
    unsigned transparent[2] = { 0, 0 };
    int mean[2][3];
    for (int h = 0; h < 2; ++h) {
        const int* pix = kHalfPixels[best.flip][h];
        for (int k = 0; k < 8; ++k) {
            if ((transparentMask >> pix[k]) & 1) transparent[h] |= 1u << k;
        }
        RefitMean(px, pix, best.fit[h], punch, transparent[h], mean[h]);
    }
    int q[2][3];
    QuantiseBases(mean, best.differential, q);
    Candidate refit;
    Evaluate(px, best.flip, best.differential, q, punch, transparent, &refit);
    if (refit.err < best.err) best = refit;
    return best;
}

inline int ExpandPlanar(int ch, int code) {
    return ch == 1 ? Expand7(code) : Expand6(code);
}

// One channel's squared error under planar codes o / h / v.
int PlanarChannelError(const uint8_t* px, int ch, int o, int h, int v) {
    const int eo = ExpandPlanar(ch, o);
    const int eh = ExpandPlanar(ch, h);
    const int ev = ExpandPlanar(ch, v);
    int e = 0;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int d = px[(4 * y + x) * 4 + ch] - Clamp255((x * (eh - eo) + y * (ev - eo) + 4 * eo + 2) >> 2);
            e += d * d;
        }
    }
    return e;
}

// The planar block: a least-squares plane per channel, its codes rounded; when that comes within a
// quarter of `beat` (the best other mode's error), each code walked by one step while that lowers
// its channel's error (twice round: the rounding of three codes is not the best rounding of the
// plane). Out its squared error.
uint64_t SearchPlanar(const uint8_t* px, int64_t beat, int64_t* errOut) {
    int code[3][3];   // [channel][O, H, V]
    int chErr[3];
    int64_t err = 0;
    for (int ch = 0; ch < 3; ++ch) {
        // c(x, y) ~ m + bx (x - 1.5) + by (y - 1.5); sum (x - 1.5)^2 over the block = 20.
        int sum = 0;
        int sx = 0;   // sum of (2x - 3) c
        int sy = 0;
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                const int c = px[(4 * y + x) * 4 + ch];
                sum += c;
                sx += (2 * x - 3) * c;
                sy += (2 * y - 3) * c;
            }
        }
        const float m = sum / 16.0f;
        const float bx = sx / 40.0f;
        const float by = sy / 40.0f;
        const float fo = m - 1.5f * bx - 1.5f * by;
        const int bits = ch == 1 ? 7 : 6;
        const auto q = [bits](float v) {
            const int iv = static_cast<int>(v + (v >= 0 ? 0.5f : -0.5f));
            return Quantise(Clamp255(iv), bits);
        };
        code[ch][0] = q(fo);
        code[ch][1] = q(fo + 4.0f * bx);
        code[ch][2] = q(fo + 4.0f * by);
        chErr[ch] = PlanarChannelError(px, ch, code[ch][0], code[ch][1], code[ch][2]);
        err += chErr[ch];
    }
    if (4 * err <= 5 * beat) {
        err = 0;
        for (int ch = 0; ch < 3; ++ch) {
            const int top = ch == 1 ? 127 : 63;
            for (int pass = 0; pass < 2 && chErr[ch] > 0; ++pass) {
                for (int k = 0; k < 3; ++k) {
                    for (int step = -1; step <= 1; step += 2) {
                        int trial[3] = { code[ch][0], code[ch][1], code[ch][2] };
                        trial[k] += step;
                        if (trial[k] < 0 || trial[k] > top) continue;
                        const int e = PlanarChannelError(px, ch, trial[0], trial[1], trial[2]);
                        if (e < chErr[ch]) {
                            chErr[ch] = e;
                            code[ch][k] = trial[k];
                        }
                    }
                }
            }
            err += chErr[ch];
        }
    }
    const int o[3] = { code[0][0], code[1][0], code[2][0] };
    const int hq[3] = { code[0][1], code[1][1], code[2][1] };
    const int vq[3] = { code[0][2], code[1][2], code[2][2] };
    *errOut = err;
    uint64_t w = 0;
    w |= static_cast<uint64_t>(o[0]) << 57;
    w |= static_cast<uint64_t>(o[1] >> 6) << 56;
    w |= static_cast<uint64_t>(o[1] & 63) << 49;
    w |= static_cast<uint64_t>(o[2] >> 5) << 48;
    w |= static_cast<uint64_t>((o[2] >> 3) & 3) << 43;
    w |= static_cast<uint64_t>(o[2] & 7) << 39;
    w |= static_cast<uint64_t>(hq[0] >> 1) << 34;
    w |= uint64_t{ 1 } << 33;
    w |= static_cast<uint64_t>(hq[0] & 1) << 32;
    w |= static_cast<uint64_t>(hq[1]) << 25;
    w |= static_cast<uint64_t>(hq[2]) << 19;
    w |= static_cast<uint64_t>(vq[0]) << 13;
    w |= static_cast<uint64_t>(vq[1]) << 6;
    w |= static_cast<uint64_t>(vq[2]);
    // The free bits: R and G in range, B out of range (the planar selector).
    if (Overflows(w, 63)) w |= uint64_t{ 1 } << 63;
    if (Overflows(w, 55)) w |= uint64_t{ 1 } << 55;
    const uint64_t freeB = (uint64_t{ 7 } << 45) | (uint64_t{ 1 } << 42);
    const uint64_t options[4] = { 0, uint64_t{ 1 } << 42, uint64_t{ 7 } << 45, freeB };
    for (const uint64_t option : options) {
        const uint64_t trial = (w & ~freeB) | option;
        if (Overflows(trial, 47)) {
            w = trial;
            break;
        }
    }
    return w;
}

// A block of one colour that planar holds exactly (O = H = V, the colour 6:7:6-representable) is
// written at once; false for any other block, which takes the full search.
bool FlatBlock(const uint8_t* px, uint8_t out[8]) {
    for (int i = 1; i < 16; ++i) {
        if (px[i * 4] != px[0] || px[i * 4 + 1] != px[1] || px[i * 4 + 2] != px[2]) return false;
    }
    int64_t err = 0;
    const uint64_t planar = SearchPlanar(px, 0, &err);
    if (err == 0) {
        WriteBe(planar, out);
        return true;
    }
    return false;
}

}  // namespace

// ---- decoders ----------------------------------------------------------------------------------

void DecodeRgbBlock(const uint8_t in[8], bool punchThrough, uint8_t pixels[64]) {
    const uint64_t w = ReadBe(in);
    const bool diff = ((w >> 33) & 1) != 0;
    const bool opaque = !punchThrough || diff;
    const auto put = [&](int i, int r, int g, int b, int a) {
        const int x = i >> 2;
        const int y = i & 3;
        uint8_t* p = pixels + (4 * y + x) * 4;
        p[0] = static_cast<uint8_t>(r);
        p[1] = static_cast<uint8_t>(g);
        p[2] = static_cast<uint8_t>(b);
        p[3] = static_cast<uint8_t>(a);
    };
    if (!punchThrough && !diff) {
        // Individual.
        int base[2][3];
        for (int ch = 0; ch < 3; ++ch) {
            base[0][ch] = Expand4(static_cast<int>(Bits(w, 63 - ch * 8, 4)));
            base[1][ch] = Expand4(static_cast<int>(Bits(w, 59 - ch * 8, 4)));
        }
        const int tables[2] = { static_cast<int>(Bits(w, 39, 3)), static_cast<int>(Bits(w, 36, 3)) };
        const bool flip = ((w >> 32) & 1) != 0;
        for (int i = 0; i < 16; ++i) {
            const int x = i >> 2;
            const int y = i & 3;
            const int h = flip ? (y >= 2) : (x >= 2);
            const int m = Modifier(tables[h], PixelIndex(w, i));
            put(i, Clamp255(base[h][0] + m), Clamp255(base[h][1] + m), Clamp255(base[h][2] + m), 255);
        }
        return;
    }
    if (Overflows(w, 63)) {
        // T mode.
        const int r1 = static_cast<int>((Bits(w, 60, 2) << 2) | Bits(w, 57, 2));
        int c1[3] = { Expand4(r1), Expand4(static_cast<int>(Bits(w, 55, 4))), Expand4(static_cast<int>(Bits(w, 51, 4))) };
        int c2[3] = { Expand4(static_cast<int>(Bits(w, 47, 4))), Expand4(static_cast<int>(Bits(w, 43, 4))),
                      Expand4(static_cast<int>(Bits(w, 39, 4))) };
        const int d = kDistances[(Bits(w, 35, 2) << 1) | Bits(w, 32, 1)];
        int paint[4][3];
        for (int ch = 0; ch < 3; ++ch) {
            paint[0][ch] = c1[ch];
            paint[1][ch] = Clamp255(c2[ch] + d);
            paint[2][ch] = c2[ch];
            paint[3][ch] = Clamp255(c2[ch] - d);
        }
        for (int i = 0; i < 16; ++i) {
            const int idx = PixelIndex(w, i);
            if (!opaque && idx == 2) {
                put(i, 0, 0, 0, 0);
            } else {
                put(i, paint[idx][0], paint[idx][1], paint[idx][2], 255);
            }
        }
        return;
    }
    if (Overflows(w, 55)) {
        // H mode.
        const int r1 = static_cast<int>(Bits(w, 62, 4));
        const int g1 = static_cast<int>((Bits(w, 58, 3) << 1) | Bits(w, 52, 1));
        const int b1 = static_cast<int>((Bits(w, 51, 1) << 3) | Bits(w, 49, 3));
        const int r2 = static_cast<int>(Bits(w, 46, 4));
        const int g2 = static_cast<int>(Bits(w, 42, 4));
        const int b2 = static_cast<int>(Bits(w, 38, 4));
        const int order = ((r1 << 8) | (g1 << 4) | b1) >= ((r2 << 8) | (g2 << 4) | b2) ? 1 : 0;
        const int d = kDistances[(Bits(w, 34, 1) << 2) | (Bits(w, 32, 1) << 1) | static_cast<uint32_t>(order)];
        const int c1[3] = { Expand4(r1), Expand4(g1), Expand4(b1) };
        const int c2[3] = { Expand4(r2), Expand4(g2), Expand4(b2) };
        int paint[4][3];
        for (int ch = 0; ch < 3; ++ch) {
            paint[0][ch] = Clamp255(c1[ch] + d);
            paint[1][ch] = Clamp255(c1[ch] - d);
            paint[2][ch] = Clamp255(c2[ch] + d);
            paint[3][ch] = Clamp255(c2[ch] - d);
        }
        for (int i = 0; i < 16; ++i) {
            const int idx = PixelIndex(w, i);
            if (!opaque && idx == 2) {
                put(i, 0, 0, 0, 0);
            } else {
                put(i, paint[idx][0], paint[idx][1], paint[idx][2], 255);
            }
        }
        return;
    }
    if (Overflows(w, 47)) {
        // Planar (always opaque).
        const int ro = static_cast<int>(Bits(w, 62, 6));
        const int go = static_cast<int>((Bits(w, 56, 1) << 6) | Bits(w, 54, 6));
        const int bo = static_cast<int>((Bits(w, 48, 1) << 5) | (Bits(w, 44, 2) << 3) | Bits(w, 41, 3));
        const int rh = static_cast<int>((Bits(w, 38, 5) << 1) | Bits(w, 32, 1));
        const int gh = static_cast<int>(Bits(w, 31, 7));
        const int bh = static_cast<int>(Bits(w, 24, 6));
        const int rv = static_cast<int>(Bits(w, 18, 6));
        const int gv = static_cast<int>(Bits(w, 12, 7));
        const int bv = static_cast<int>(Bits(w, 5, 6));
        const int o[3] = { Expand6(ro), Expand7(go), Expand6(bo) };
        const int hc[3] = { Expand6(rh), Expand7(gh), Expand6(bh) };
        const int vc[3] = { Expand6(rv), Expand7(gv), Expand6(bv) };
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                int c[3];
                for (int ch = 0; ch < 3; ++ch)
                    c[ch] = Clamp255((x * (hc[ch] - o[ch]) + y * (vc[ch] - o[ch]) + 4 * o[ch] + 2) >> 2);
                put(4 * x + y, c[0], c[1], c[2], 255);
            }
        }
        return;
    }
    // Differential.
    int base[2][3];
    for (int ch = 0; ch < 3; ++ch) {
        const int q = static_cast<int>(Bits(w, 63 - ch * 8, 5));
        int delta = static_cast<int>(Bits(w, 58 - ch * 8, 3));
        if (delta >= 4) delta -= 8;
        base[0][ch] = Expand5(q);
        base[1][ch] = Expand5(q + delta);
    }
    const int tables[2] = { static_cast<int>(Bits(w, 39, 3)), static_cast<int>(Bits(w, 36, 3)) };
    const bool flip = ((w >> 32) & 1) != 0;
    for (int i = 0; i < 16; ++i) {
        const int x = i >> 2;
        const int y = i & 3;
        const int h = flip ? (y >= 2) : (x >= 2);
        const int idx = PixelIndex(w, i);
        if (!opaque && idx == 2) {
            put(i, 0, 0, 0, 0);
            continue;
        }
        const int m = !opaque && idx == 0 ? 0 : Modifier(tables[h], idx);
        put(i, Clamp255(base[h][0] + m), Clamp255(base[h][1] + m), Clamp255(base[h][2] + m), 255);
    }
}

void DecodeAlphaBlock(const uint8_t in[8], uint8_t pixels[64]) {
    const uint64_t w = ReadBe(in);
    const int base = static_cast<int>(Bits(w, 63, 8));
    const int mult = static_cast<int>(Bits(w, 55, 4));
    const int table = static_cast<int>(Bits(w, 51, 4));
    for (int i = 0; i < 16; ++i) {
        const int idx = static_cast<int>(Bits(w, 47 - 3 * i, 3));
        const int x = i >> 2;
        const int y = i & 3;
        pixels[(4 * y + x) * 4 + 3] = static_cast<uint8_t>(Clamp255(base + kAlphaTables[table][idx] * mult));
    }
}

void DecodeRgbaBlock(const uint8_t in[16], uint8_t pixels[64]) {
    DecodeRgbBlock(in + 8, false, pixels);
    DecodeAlphaBlock(in, pixels);
}

// ---- encoders ----------------------------------------------------------------------------------

void EncodeRgbBlock(const uint8_t pixels[64], uint8_t out[8]) {
    if (FlatBlock(pixels, out)) return;
    const Candidate etc1 = SearchEtc1(pixels, true, false, 0);
    if (etc1.err > kPlanarGate) {
        int64_t planarErr = 0;
        const uint64_t planar = SearchPlanar(pixels, etc1.err, &planarErr);
        if (planarErr < etc1.err) {
            WriteBe(planar, out);
            return;
        }
    }
    WriteBe(PackCandidate(etc1, etc1.differential), out);
}

void EncodeRgbA1Block(const uint8_t pixels[64], uint8_t out[8]) {
    unsigned transparent = 0;
    for (int i = 0; i < 16; ++i) {
        if (pixels[i * 4 + 3] < 128) transparent |= 1u << i;
    }
    if (transparent == 0) {
        // Opaque: differential (opaque bit set) or planar.
        const Candidate etc1 = SearchEtc1(pixels, false, false, 0);
        int64_t planarErr = INT64_MAX;
        const uint64_t planar = etc1.err > kPlanarGate ? SearchPlanar(pixels, etc1.err, &planarErr) : 0;
        WriteBe(planarErr < etc1.err ? planar : PackCandidate(etc1, true), out);
        return;
    }
    const Candidate etc1 = SearchEtc1(pixels, false, true, transparent);
    WriteBe(PackCandidate(etc1, false), out);
}

void EncodeAlphaBlock(const uint8_t pixels[64], uint8_t out[8]) {
    int a[16];   // in the block's index order (pixel i = 4x + y)
    int lo = 255;
    int hi = 0;
    // The distinct values and their counts: the fit is measured over these (at most 8 from a BC4 block).
    int value[16];
    int weight[16];
    int distinct = 0;
    for (int i = 0; i < 16; ++i) {
        const int x = i >> 2;
        const int y = i & 3;
        a[i] = pixels[(4 * y + x) * 4 + 3];
        if (a[i] < lo) lo = a[i];
        if (a[i] > hi) hi = a[i];
        int k = 0;
        while (k < distinct && value[k] != a[i])
            ++k;
        if (k == distinct) {
            value[distinct] = a[i];
            weight[distinct] = 0;
            ++distinct;
        }
        ++weight[k];
    }
    int bestBase = lo;
    int bestMult = 1;
    int bestTable = kFlatAlphaTable;
    int64_t bestErr = INT64_MAX;
    const auto tryFit = [&](int table, int mult, int base) {
        int values[8];
        for (int k = 0; k < 8; ++k)
            values[k] = Clamp255(base + kAlphaTables[table][k] * mult);
        int64_t err = 0;
        for (int i = 0; i < distinct && err < bestErr; ++i) {
            int e = INT32_MAX;
            for (int k = 0; k < 8; ++k) {
                const int d = value[i] - values[k];
                if (d * d < e) e = d * d;
            }
            err += static_cast<int64_t>(e) * weight[i];
        }
        if (err < bestErr) {
            bestErr = err;
            bestBase = base;
            bestMult = mult;
            bestTable = table;
        }
    };
    if (lo == hi) {
        bestBase = lo;
        bestErr = 0;
    } else {
        for (int t = 0; t < 16; ++t) {
            const int tmin = kAlphaTables[t][3];
            const int tmax = kAlphaTables[t][7];
            int mult = ((hi - lo) + (tmax - tmin) / 2) / (tmax - tmin);
            if (mult < 1) mult = 1;
            if (mult > 15) mult = 15;
            const int base = Clamp255((hi + lo - (tmax + tmin) * mult + 1) / 2);
            tryFit(t, mult, base);
            if (bestErr == 0) break;
        }
        const int t = bestTable;
        const int m0 = bestMult;
        const int b0 = bestBase;
        for (int mult = m0 - 1; mult <= m0 + 1 && bestErr > 0; ++mult) {
            if (mult < 1 || mult > 15) continue;
            for (int base = b0 - 2; base <= b0 + 2; ++base) {
                if (base < 0 || base > 255 || (mult == m0 && base == b0)) continue;
                tryFit(t, mult, base);
            }
        }
    }
    uint64_t w = static_cast<uint64_t>(bestBase) << 56;
    w |= static_cast<uint64_t>(bestMult) << 52;
    w |= static_cast<uint64_t>(bestTable) << 48;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        int e = INT32_MAX;
        for (int k = 0; k < 8; ++k) {
            const int d = a[i] - Clamp255(bestBase + kAlphaTables[bestTable][k] * bestMult);
            if (d * d < e) {
                e = d * d;
                best = k;
            }
        }
        w |= static_cast<uint64_t>(best) << (45 - 3 * i);
    }
    WriteBe(w, out);
}

void EncodeRgbaBlock(const uint8_t pixels[64], uint8_t out[16]) {
    EncodeAlphaBlock(pixels, out);
    EncodeRgbBlock(pixels, out + 8);
}

}  // namespace Etc2
