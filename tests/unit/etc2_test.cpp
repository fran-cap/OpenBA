// ============================================================================
// tests/unit/etc2_test.cpp -- src/render/common/etc2: the ETC2 / EAC codec and the BC1 / BC3 transcode
// ============================================================================
// docs/android/P13_MEM.md. Synthetic data only (AGENTS.md §1).
//   * the decoders against an independent decoder: 256 pseudo-random words per format (every mode:
//     individual, differential, T, H, planar, punch-through), the hash of the output recorded from the
//     `texture2ddecoder` Python package (transparent punch-through texels as (0, 0, 0, 0), as Vulkan);
//   * the RGB encoder: flat colours within 4 per channel, linear gradients within 3, two-colour blocks
//     with noise above 28 dB, and an encode never worse than the flat-mean block;
//   * punch-through: alpha exact (a transparent texel stays transparent, an opaque one opaque);
//   * EAC: constant alpha exact, binary alpha exact, ramps within 4;
//   * the transcode: BC1 / BC3 levels against BcDecode (PSNR bound), transparency detection, edge
//     blocks of a 5 x 3 level and a 1 x 1 level, a band transcode equal to the whole.
// ============================================================================
#include "unit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "bc_decode/bc_decode.h"
#include "etc2/bc_to_etc2.h"
#include "etc2/etc2.h"

namespace {

struct Lcg {
    uint64_t x;
    uint64_t Next() {
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        return x;
    }
    int Below(int n) { return static_cast<int>((Next() >> 33) % static_cast<uint64_t>(n)); }
};

// The generator's words as blocks: byte k of a word is (x >> 8k).
std::vector<uint8_t> Words(int count, uint64_t seed) {
    Lcg rng{ seed };
    std::vector<uint8_t> out;
    for (int i = 0; i < count; ++i) {
        const uint64_t x = rng.Next();
        for (int k = 0; k < 8; ++k)
            out.push_back(static_cast<uint8_t>(x >> (8 * k)));
    }
    return out;
}

uint64_t Fnv(const uint8_t* data, size_t n, uint64_t h) {
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

constexpr uint64_t kFnvSeed = 0xcbf29ce484222325ull;

int MaxChannelError(const uint8_t* a, const uint8_t* b, bool alpha) {
    int worst = 0;
    for (int i = 0; i < 16; ++i) {
        for (int ch = 0; ch < (alpha ? 4 : 3); ++ch) {
            const int d = std::abs(a[i * 4 + ch] - b[i * 4 + ch]);
            if (d > worst) worst = d;
        }
    }
    return worst;
}

double SquaredError(const uint8_t* a, const uint8_t* b) {
    double e = 0;
    for (int i = 0; i < 16; ++i) {
        for (int ch = 0; ch < 3; ++ch) {
            const double d = a[i * 4 + ch] - b[i * 4 + ch];
            e += d * d;
        }
    }
    return e;
}

double Psnr(double squaredError, double samples) {
    return 10.0 * std::log10(255.0 * 255.0 / (squaredError / samples + 1e-12));
}

void SetPixel(uint8_t* px, int i, int r, int g, int b, int a) {
    px[i * 4 + 0] = static_cast<uint8_t>(r);
    px[i * 4 + 1] = static_cast<uint8_t>(g);
    px[i * 4 + 2] = static_cast<uint8_t>(b);
    px[i * 4 + 3] = static_cast<uint8_t>(a);
}

int Clamp(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

}  // namespace

UNIT_TEST(etc2_decoders_match_reference) {
    // Recorded from texture2ddecoder (decode_etc2 / decode_etc2a1 / decode_etc2a8) over the same words.
    const std::vector<uint8_t> rgb = Words(256, 1);
    const std::vector<uint8_t> rgba = Words(512, 2);
    uint64_t hRgb = kFnvSeed;
    uint64_t hA1 = kFnvSeed;
    uint64_t hRgba = kFnvSeed;
    for (int i = 0; i < 256; ++i) {
        uint8_t px[64];
        Etc2::DecodeRgbBlock(&rgb[static_cast<size_t>(i) * 8], false, px);
        hRgb = Fnv(px, 64, hRgb);
        Etc2::DecodeRgbBlock(&rgb[static_cast<size_t>(i) * 8], true, px);
        hA1 = Fnv(px, 64, hA1);
        Etc2::DecodeRgbaBlock(&rgba[static_cast<size_t>(i) * 16], px);
        hRgba = Fnv(px, 64, hRgba);
    }
    CHECK_EQ(hRgb, 0x558ae55c833797c8ull);
    CHECK_EQ(hA1, 0x77beb65125c2f5bfull);
    CHECK_EQ(hRgba, 0x900f482ddd146bd2ull);
}

UNIT_TEST(etc2_rgb_flat_colours) {
    Lcg rng{ 7 };
    int worst = 0;
    for (int n = 0; n < 2000; ++n) {
        uint8_t px[64];
        const int r = rng.Below(256);
        const int g = rng.Below(256);
        const int b = rng.Below(256);
        for (int i = 0; i < 16; ++i)
            SetPixel(px, i, r, g, b, 255);
        uint8_t block[8];
        uint8_t out[64];
        Etc2::EncodeRgbBlock(px, block);
        Etc2::DecodeRgbBlock(block, false, out);
        const int e = MaxChannelError(px, out, true);
        if (e > worst) worst = e;
    }
    CHECK(worst <= 4);
}

UNIT_TEST(etc2_rgb_gradients) {
    Lcg rng{ 11 };
    int worst = 0;
    for (int n = 0; n < 2000; ++n) {
        uint8_t px[64];
        int o[3];
        int dx[3];
        int dy[3];
        for (int ch = 0; ch < 3; ++ch) {
            // Inside 0..255 everywhere (o +- 3 * 10 * 2): a plane, which the planar mode holds.
            o[ch] = 60 + rng.Below(136);
            dx[ch] = rng.Below(21) - 10;
            dy[ch] = rng.Below(21) - 10;
        }
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                SetPixel(px, 4 * y + x, Clamp(o[0] + dx[0] * x + dy[0] * y), Clamp(o[1] + dx[1] * x + dy[1] * y),
                         Clamp(o[2] + dx[2] * x + dy[2] * y), 255);
            }
        }
        uint8_t block[8];
        uint8_t out[64];
        Etc2::EncodeRgbBlock(px, block);
        Etc2::DecodeRgbBlock(block, false, out);
        const int e = MaxChannelError(px, out, false);
        if (e > worst) worst = e;
    }
    std::printf("  etc2 gradients: worst channel error %d\n", worst);
    CHECK(worst <= 3);
}

UNIT_TEST(etc2_rgb_two_colour_noise) {
    Lcg rng{ 13 };
    double err = 0;
    double samples = 0;
    bool neverWorseThanMean = true;
    for (int n = 0; n < 4000; ++n) {
        uint8_t px[64];
        int c[2][3];
        for (int k = 0; k < 2; ++k) {
            const int base = rng.Below(200);
            for (int ch = 0; ch < 3; ++ch)
                c[k][ch] = Clamp(base + rng.Below(56));
        }
        for (int i = 0; i < 16; ++i) {
            const int* col = c[rng.Below(4) == 0 ? 1 : 0];
            SetPixel(px, i, Clamp(col[0] + rng.Below(7) - 3), Clamp(col[1] + rng.Below(7) - 3),
                     Clamp(col[2] + rng.Below(7) - 3), 255);
        }
        uint8_t block[8];
        uint8_t out[64];
        Etc2::EncodeRgbBlock(px, block);
        Etc2::DecodeRgbBlock(block, false, out);
        const double e = SquaredError(px, out);
        err += e;
        samples += 48;
        // The block's mean colour everywhere: any encode should do at least as well (with slack for the
        // mean's own quantisation).
        int mean[3] = { 0, 0, 0 };
        for (int i = 0; i < 16; ++i) {
            for (int ch = 0; ch < 3; ++ch)
                mean[ch] += px[i * 4 + ch];
        }
        uint8_t flat[64];
        for (int i = 0; i < 16; ++i)
            SetPixel(flat, i, (mean[0] + 8) / 16, (mean[1] + 8) / 16, (mean[2] + 8) / 16, 255);
        if (e > SquaredError(px, flat) + 16 * 3 * 16) neverWorseThanMean = false;
    }
    std::printf("  etc2 two-colour noise: %.2f dB\n", Psnr(err, samples));
    // Two colour clusters in one half are ETC's weak spot (a half is one base colour plus grey offsets;
    // the T and H modes, which would hold them, are not encoded): 29.2 dB when this was written.
    CHECK(Psnr(err, samples) > 28.0);
    CHECK(neverWorseThanMean);
}

UNIT_TEST(etc2_punch_through_alpha_exact) {
    Lcg rng{ 17 };
    bool alphaExact = true;
    double err = 0;
    double samples = 0;
    for (int n = 0; n < 2000; ++n) {
        uint8_t px[64];
        const int base = rng.Below(200);
        for (int i = 0; i < 16; ++i) {
            const bool transparent = (n % 4 != 0) && rng.Below(3) == 0;
            SetPixel(px, i, Clamp(base + rng.Below(40)), Clamp(base + rng.Below(40)), Clamp(base + rng.Below(40)),
                     transparent ? 0 : 255);
        }
        uint8_t block[8];
        uint8_t out[64];
        Etc2::EncodeRgbA1Block(px, block);
        Etc2::DecodeRgbBlock(block, true, out);
        for (int i = 0; i < 16; ++i) {
            if (out[i * 4 + 3] != px[i * 4 + 3]) alphaExact = false;
            if (px[i * 4 + 3] == 0) continue;
            for (int ch = 0; ch < 3; ++ch) {
                const double d = px[i * 4 + ch] - out[i * 4 + ch];
                err += d * d;
                samples += 1;
            }
        }
    }
    std::printf("  etc2 punch-through: %.2f dB on the opaque texels\n", Psnr(err, samples));
    CHECK(alphaExact);
    // Differential only, three modifiers: 29.1 dB when this was written.
    CHECK(Psnr(err, samples) > 28.0);
}

UNIT_TEST(etc2_eac_alpha) {
    Lcg rng{ 19 };
    int worstConstant = 0;
    int worstBinary = 0;
    int worstRamp = 0;
    for (int n = 0; n < 2000; ++n) {
        uint8_t px[64] = {};
        uint8_t block[8];
        uint8_t out[64] = {};
        const int a = rng.Below(256);
        for (int i = 0; i < 16; ++i)
            px[i * 4 + 3] = static_cast<uint8_t>(a);
        Etc2::EncodeAlphaBlock(px, block);
        Etc2::DecodeAlphaBlock(block, out);
        worstConstant = std::max(worstConstant, MaxChannelError(px, out, true));

        for (int i = 0; i < 16; ++i)
            px[i * 4 + 3] = rng.Below(2) ? 255 : 0;
        Etc2::EncodeAlphaBlock(px, block);
        Etc2::DecodeAlphaBlock(block, out);
        worstBinary = std::max(worstBinary, MaxChannelError(px, out, true));

        const int lo = rng.Below(200);
        const int step = rng.Below(4);
        for (int i = 0; i < 16; ++i)
            px[i * 4 + 3] = static_cast<uint8_t>(Clamp(lo + step * i));
        Etc2::EncodeAlphaBlock(px, block);
        Etc2::DecodeAlphaBlock(block, out);
        worstRamp = std::max(worstRamp, MaxChannelError(px, out, true));
    }
    CHECK_EQ(worstConstant, 0);
    CHECK_EQ(worstBinary, 0);
    CHECK(worstRamp <= 4);
}

namespace {

// A w x h BC1 level of smooth, noisy content: per block two endpoints near a slowly varying colour,
// random indices; `transparentBlocks` makes every eighth block three-colour with index 3 in it.
std::vector<uint8_t> MakeBc1(int w, int h, bool transparentBlocks, uint64_t seed) {
    Lcg rng{ seed };
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    std::vector<uint8_t> out;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const int r = Clamp(60 + bx * 3 + rng.Below(20));
            const int g = Clamp(90 + by * 2 + rng.Below(20));
            const int b = Clamp(40 + rng.Below(30));
            uint16_t c0 = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            uint16_t c1 = static_cast<uint16_t>((((r / 2) >> 3) << 11) | (((g / 2) >> 2) << 5) | ((b / 2) >> 3));
            const bool three = transparentBlocks && (by * bw + bx) % 8 == 3;
            if (three ? c0 > c1 : c0 <= c1) {
                const uint16_t t = c0;
                c0 = c1;
                c1 = t;
            }
            if (!three && c0 == c1) c0 = static_cast<uint16_t>(c0 + 1);
            const uint32_t bits = static_cast<uint32_t>(rng.Next() >> 32);
            out.push_back(static_cast<uint8_t>(c0));
            out.push_back(static_cast<uint8_t>(c0 >> 8));
            out.push_back(static_cast<uint8_t>(c1));
            out.push_back(static_cast<uint8_t>(c1 >> 8));
            for (int k = 0; k < 4; ++k)
                out.push_back(static_cast<uint8_t>(bits >> (8 * k)));
        }
    }
    return out;
}

// The same with a BC4 alpha block (two endpoints, random indices) before each colour block.
std::vector<uint8_t> MakeBc3(int w, int h, uint64_t seed) {
    const std::vector<uint8_t> colour = MakeBc1(w, h, false, seed);
    Lcg rng{ seed + 1 };
    std::vector<uint8_t> out;
    for (size_t i = 0; i < colour.size(); i += 8) {
        const int a0 = 128 + rng.Below(128);
        const int a1 = rng.Below(128);
        out.push_back(static_cast<uint8_t>(a0));
        out.push_back(static_cast<uint8_t>(a1));
        const uint64_t bits = rng.Next();
        for (int k = 0; k < 6; ++k)
            out.push_back(static_cast<uint8_t>(bits >> (8 * k)));
        out.insert(out.end(), colour.begin() + static_cast<long>(i), colour.begin() + static_cast<long>(i) + 8);
    }
    return out;
}

}  // namespace

UNIT_TEST(etc2_transcode_bc1_level) {
    const int w = 64;
    const int h = 32;
    const std::vector<uint8_t> bc = MakeBc1(w, h, false, 23);
    CHECK_EQ(bc.size(), BcDecode::Bc1Size(w, h));
    CHECK(!BcToEtc2::Bc1HasTransparency(bc.data(), w, h));
    std::vector<uint8_t> etc(bc.size(), 0xCD);
    BcToEtc2::TranscodeBC1(bc.data(), w, h, false, etc.data(), 0, (h + 3) / 4);
    std::vector<uint8_t> want(static_cast<size_t>(w) * h * 4);
    BcDecode::DecodeBC1(bc.data(), w, h, want.data(), w * 4);
    double err = 0;
    for (int b = 0; b < (w / 4) * (h / 4); ++b) {
        uint8_t got[64];
        Etc2::DecodeRgbBlock(&etc[static_cast<size_t>(b) * 8], false, got);
        const int bx = b % (w / 4);
        const int by = b / (w / 4);
        for (int i = 0; i < 16; ++i) {
            const uint8_t* src = &want[(static_cast<size_t>(by * 4 + i / 4) * w + bx * 4 + i % 4) * 4];
            for (int ch = 0; ch < 3; ++ch) {
                const double d = src[ch] - got[i * 4 + ch];
                err += d * d;
            }
        }
    }
    // Random BC1 indices are the noisiest content BC1 holds; real textures come out far better.
    std::printf("  etc2 BC1 level: %.2f dB against the BC1 decode\n", Psnr(err, static_cast<double>(w) * h * 3));
    CHECK(Psnr(err, static_cast<double>(w) * h * 3) > 30.0);
}

UNIT_TEST(etc2_transcode_bc1_punch_through) {
    const int w = 32;
    const int h = 32;
    const std::vector<uint8_t> bc = MakeBc1(w, h, true, 29);
    CHECK(BcToEtc2::Bc1HasTransparency(bc.data(), w, h));
    std::vector<uint8_t> etc(bc.size(), 0);
    BcToEtc2::TranscodeBC1(bc.data(), w, h, true, etc.data(), 0, (h + 3) / 4);
    bool alphaExact = true;
    for (int b = 0; b < (w / 4) * (h / 4); ++b) {
        uint8_t want[64];
        uint8_t got[64];
        BcDecode::DecodeColourBlock(&bc[static_cast<size_t>(b) * 8], false, want);
        Etc2::DecodeRgbBlock(&etc[static_cast<size_t>(b) * 8], true, got);
        for (int i = 0; i < 16; ++i) {
            if (want[i * 4 + 3] != got[i * 4 + 3]) alphaExact = false;
        }
    }
    CHECK(alphaExact);
}

UNIT_TEST(etc2_transcode_bc3_level) {
    const int w = 32;
    const int h = 32;
    const std::vector<uint8_t> bc = MakeBc3(w, h, 31);
    CHECK_EQ(bc.size(), BcDecode::Bc3Size(w, h));
    std::vector<uint8_t> etc(bc.size(), 0);
    BcToEtc2::TranscodeBC3(bc.data(), w, h, etc.data(), 0, (h + 3) / 4);
    double err = 0;
    double errAlpha = 0;
    for (int b = 0; b < (w / 4) * (h / 4); ++b) {
        uint8_t want[64];
        uint8_t got[64];
        BcDecode::DecodeBC3Block(&bc[static_cast<size_t>(b) * 16], want);
        Etc2::DecodeRgbaBlock(&etc[static_cast<size_t>(b) * 16], got);
        err += SquaredError(want, got);
        for (int i = 0; i < 16; ++i) {
            const double d = want[i * 4 + 3] - got[i * 4 + 3];
            errAlpha += d * d;
        }
    }
    std::printf("  etc2 BC3 level: colour %.2f dB, alpha %.2f dB\n", Psnr(err, static_cast<double>(w) * h * 3),
                Psnr(errAlpha, static_cast<double>(w) * h));
    CHECK(Psnr(err, static_cast<double>(w) * h * 3) > 30.0);
    CHECK(Psnr(errAlpha, static_cast<double>(w) * h) > 30.0);
}

UNIT_TEST(etc2_transcode_edges_and_bands) {
    // A 5 x 3 level: two blocks; texels outside the level do not count for transparency.
    std::vector<uint8_t> bc = MakeBc1(5, 3, false, 37);
    CHECK_EQ(bc.size(), static_cast<size_t>(16));
    // Block 1 (level x 4..7, only x 4 inside) made three-colour, index 3 at its pixel (1, 0): level x 5,
    // outside the level.
    bc[8] = 0x00;
    bc[9] = 0x00;
    bc[10] = 0xFF;
    bc[11] = 0xFF;
    bc[12] = 0x0C;   // pixel (1, 0): index 3
    bc[13] = 0x00;
    bc[14] = 0x00;
    bc[15] = 0x00;
    CHECK(!BcToEtc2::Bc1HasTransparency(bc.data(), 5, 3));
    bc[12] = 0x03;   // pixel (0, 0): index 3, inside
    CHECK(BcToEtc2::Bc1HasTransparency(bc.data(), 5, 3));

    // A 1 x 1 level is one block; its encode follows the one texel.
    std::vector<uint8_t> one = MakeBc1(1, 1, false, 41);
    uint8_t etc[8];
    BcToEtc2::TranscodeBC1(one.data(), 1, 1, false, etc, 0, 1);
    uint8_t want[64];
    uint8_t got[64];
    BcDecode::DecodeColourBlock(one.data(), false, want);
    Etc2::DecodeRgbBlock(etc, false, got);
    for (int ch = 0; ch < 3; ++ch)
        CHECK(std::abs(want[ch] - got[ch]) <= 4);

    // Bands give the bytes of the whole.
    const int w = 16;
    const int h = 40;
    const std::vector<uint8_t> big = MakeBc3(w, h, 43);
    std::vector<uint8_t> whole(big.size(), 0);
    std::vector<uint8_t> banded(big.size(), 0);
    BcToEtc2::TranscodeBC3(big.data(), w, h, whole.data(), 0, 10);
    BcToEtc2::TranscodeBC3(big.data(), w, h, banded.data(), 0, 3);
    BcToEtc2::TranscodeBC3(big.data(), w, h, banded.data(), 3, 7);
    BcToEtc2::TranscodeBC3(big.data(), w, h, banded.data(), 7, 99);
    CHECK(whole == banded);
}
