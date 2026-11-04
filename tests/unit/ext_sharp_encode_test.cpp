// ============================================================================
// tests/unit/ext_sharp_encode_test.cpp -- src/ext/sharp_ui/sharp_encode: the
// magnified sheet's mip chain and BC3 encoder (SHARP_SPRITES)
// ============================================================================
//   * mip sizes and the level count of a full chain (square, wide, odd);
//   * the box filter weights colour by alpha (no dark fringe), alpha is the mean;
//   * BC3 round trips through bc_decode: flat blocks within 1 a channel, a
//     two-colour block exact, a gradient above a stated PSNR, a transparent
//     block alpha 0, transparent texels not pulling the endpoints, c0 > c1;
//   * 2 x 2 and 1 x 1 levels are one block; the chain is the same on 1 and 4
//     threads, byte for byte;
//   * the 4096 x 4096 chain's wall time is printed (not checked).
// ============================================================================
#include "unit.h"

#include "ext/sharp_ui/sharp_encode.h"

#include "bc_decode/bc_decode.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Ext::SharpUI;

namespace {

struct Rng {
    uint64_t s;
    uint64_t Next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    int Below(int n) { return static_cast<int>((Next() >> 32) % static_cast<uint64_t>(n)); }
};

void Put(std::vector<uint8_t>& img, int w, int x, int y, int r, int g, int b, int a) {
    uint8_t* p = &img[(static_cast<size_t>(y) * w + x) * 4];
    p[0] = static_cast<uint8_t>(b);
    p[1] = static_cast<uint8_t>(g);
    p[2] = static_cast<uint8_t>(r);
    p[3] = static_cast<uint8_t>(a);
}

// Encodes a w x h BGRA level, decodes it (RGBA), returns the decoded pixels.
std::vector<uint8_t> RoundTrip(const std::vector<uint8_t>& bgra, int w, int h) {
    std::vector<uint8_t> enc(Bc3LevelSize(w, h));
    EncodeBc3(bgra.data(), w, h, w * 4, enc.data());
    std::vector<uint8_t> dec(static_cast<size_t>(w) * h * 4);
    BcDecode::DecodeBC3(enc.data(), w, h, dec.data(), w * 4);
    return dec;
}

// Largest channel difference over the colour (and, with `alpha`, alpha) channels.
int MaxError(const std::vector<uint8_t>& bgra, const std::vector<uint8_t>& rgba, bool alpha) {
    int worst = 0;
    for (size_t i = 0; i < bgra.size(); i += 4) {
        const int d[4] = { std::abs(bgra[i + 2] - rgba[i + 0]), std::abs(bgra[i + 1] - rgba[i + 1]),
                           std::abs(bgra[i + 0] - rgba[i + 2]), std::abs(bgra[i + 3] - rgba[i + 3]) };
        for (int k = 0; k < (alpha ? 4 : 3); ++k)
            worst = std::max(worst, d[k]);
    }
    return worst;
}

double ColourPsnr(const std::vector<uint8_t>& bgra, const std::vector<uint8_t>& rgba) {
    double sse = 0.0;
    for (size_t i = 0; i < bgra.size(); i += 4) {
        for (int k = 0; k < 3; ++k) {
            const double d = static_cast<double>(bgra[i + 2 - k]) - static_cast<double>(rgba[i + k]);
            sse += d * d;
        }
    }
    if (sse == 0.0) return 99.0;
    const double mse = sse / (static_cast<double>(bgra.size()) / 4.0 * 3.0);
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

}  // namespace

UNIT_TEST(sharp_encode_mip_sizes) {
    CHECK_EQ(MipLevelCount(1, 1), 1);
    CHECK_EQ(MipLevelCount(4096, 4096), 13);
    CHECK_EQ(MipLevelCount(2048, 1024), 12);
    CHECK_EQ(MipLevelCount(1024, 4096), 13);
    CHECK_EQ(MipLevelCount(12, 20), 5);
    CHECK_EQ(MipSize(2048, 11), 1);
    CHECK_EQ(MipSize(2048, 3), 256);
    CHECK_EQ(MipSize(5, 1), 2);

    std::vector<uint8_t> img(static_cast<size_t>(16) * 8 * 4, 255);
    const MipChain chain = BuildMipChain(img.data(), 16, 8, 16 * 4);
    CHECK_EQ(chain.offsets.size(), static_cast<size_t>(5));
    const size_t expect[5] = { 0, 512, 640, 672, 680 };  // 16x8, 8x4, 4x2, 2x1, 1x1
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(chain.offsets[i], expect[i]);
    CHECK_EQ(chain.pixels.size(), static_cast<size_t>(684));
    for (uint8_t v : chain.pixels)
        CHECK_EQ(v, 255);

    const Bc3Chain bc = EncodeBc3Chain(chain, 1);
    const size_t bcExpect[5] = { 0, 128, 160, 176, 192 };  // 4x2, 2x1, then one block each
    CHECK_EQ(bc.offsets.size(), static_cast<size_t>(5));
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(bc.offsets[i], bcExpect[i]);
    CHECK_EQ(bc.data.size(), static_cast<size_t>(208));
    CHECK_EQ(Bc3RowPitch(16), 64);
    CHECK_EQ(Bc3RowPitch(2), 16);
}

UNIT_TEST(sharp_encode_mip_alpha_weighted) {
    // One opaque white texel next to three transparent black ones: white at quarter alpha.
    std::vector<uint8_t> img(2 * 2 * 4, 0);
    Put(img, 2, 0, 0, 255, 255, 255, 255);
    MipChain chain = BuildMipChain(img.data(), 2, 2, 8);
    const uint8_t* p = &chain.pixels[chain.offsets[1]];
    CHECK_EQ(p[0], 255);
    CHECK_EQ(p[1], 255);
    CHECK_EQ(p[2], 255);
    CHECK_EQ(p[3], 64);
    // Two colours of unequal alpha: weighted, rounded to nearest.
    Put(img, 2, 0, 0, 200, 0, 0, 255);
    Put(img, 2, 1, 0, 0, 0, 100, 85);
    chain = BuildMipChain(img.data(), 2, 2, 8);
    p = &chain.pixels[chain.offsets[1]];
    CHECK_EQ(p[2], 150);  // (200 * 255 + 0 * 85) / 340
    CHECK_EQ(p[0], 25);   // (100 * 85) / 340
    CHECK_EQ(p[3], 85);   // 340 / 4
    // No coverage: the plain mean of the colours, alpha 0.
    std::memset(img.data(), 0, img.size());
    Put(img, 2, 0, 0, 40, 80, 120, 0);
    Put(img, 2, 1, 1, 40, 80, 120, 0);
    chain = BuildMipChain(img.data(), 2, 2, 8);
    p = &chain.pixels[chain.offsets[1]];
    CHECK_EQ(p[2], 20);
    CHECK_EQ(p[1], 40);
    CHECK_EQ(p[0], 60);
    CHECK_EQ(p[3], 0);
}

UNIT_TEST(sharp_encode_flat_blocks) {
    Rng rng{ 0x51a7e5ull };
    int worst = 0;
    for (int trial = 0; trial < 2000; ++trial) {
        const int r = rng.Below(256);
        const int g = rng.Below(256);
        const int b = rng.Below(256);
        const int a = rng.Below(256);
        std::vector<uint8_t> img(4 * 4 * 4);
        for (int i = 0; i < 16; ++i)
            Put(img, 4, i % 4, i / 4, r, g, b, a);
        std::vector<uint8_t> enc(16);
        EncodeBc3(img.data(), 4, 4, 16, enc.data());
        // Four-colour order, always.
        const int c0 = enc[8] | (enc[9] << 8);
        const int c1 = enc[10] | (enc[11] << 8);
        CHECK(c0 > c1);
        std::vector<uint8_t> dec = RoundTrip(img, 4, 4);
        worst = std::max(worst, MaxError(img, dec, true));
    }
    CHECK(worst <= 1);
    // Pure black and pure white are exact.
    for (int v : { 0, 255 }) {
        std::vector<uint8_t> img(4 * 4 * 4);
        for (int i = 0; i < 16; ++i)
            Put(img, 4, i % 4, i / 4, v, v, v, 255);
        CHECK_EQ(MaxError(img, RoundTrip(img, 4, 4), true), 0);
    }
}

UNIT_TEST(sharp_encode_two_colour_and_transparent) {
    // Two 565-exact colours: exact.
    std::vector<uint8_t> img(4 * 4 * 4);
    for (int i = 0; i < 16; ++i) {
        if ((i ^ (i >> 2)) & 1)
            Put(img, 4, i % 4, i / 4, 255, 0, 0, 255);
        else
            Put(img, 4, i % 4, i / 4, 0, 0, 255, 255);
    }
    CHECK_EQ(MaxError(img, RoundTrip(img, 4, 4), true), 0);

    // Fully transparent block: alpha decodes 0.
    for (int i = 0; i < 16; ++i)
        Put(img, 4, i % 4, i / 4, i * 16, 255 - i * 16, 77, 0);
    std::vector<uint8_t> dec = RoundTrip(img, 4, 4);
    for (int i = 0; i < 16; ++i)
        CHECK_EQ(dec[i * 4 + 3], 0);

    // Half the block opaque grey ramp, half transparent saturated junk: the opaque half keeps
    // its accuracy (the junk does not pull the endpoints).
    for (int i = 0; i < 16; ++i) {
        const int x = i % 4;
        const int y = i / 4;
        if (y < 2)
            Put(img, 4, x, y, 60 + x * 40, 60 + x * 40, 60 + x * 40, 255);
        else
            Put(img, 4, x, y, x * 80, 255, 0, 0);
    }
    dec = RoundTrip(img, 4, 4);
    int worst = 0;
    for (int i = 0; i < 8; ++i) {
        for (int k = 0; k < 3; ++k)
            worst = std::max(worst, std::abs(img[i * 4 + 2 - k] - dec[i * 4 + k]));
    }
    CHECK(worst <= 4);
}

UNIT_TEST(sharp_encode_gradient_psnr) {
    // A 64 x 64 chrome-like gradient: diagonal colour ramp, opaque.
    const int n = 64;
    std::vector<uint8_t> img(static_cast<size_t>(n) * n * 4);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x)
            Put(img, n, x, y, 40 + x * 2, 30 + (x + y), 200 - y * 2, 255);
    }
    std::vector<uint8_t> dec = RoundTrip(img, n, n);
    const double psnr = ColourPsnr(img, dec);
    const int worst = MaxError(img, dec, true);
    std::printf("  sharp_encode gradient 64x64: colour PSNR %.2f dB, max error %d\n", psnr, worst);
    CHECK(psnr > 42.0);
    CHECK(worst <= 6);

    // Random-ish UI content (noisy blocks): no block worse than plain endpoint rounding would give
    // a decoder in three-colour mode; only checks the PSNR floor here.
    Rng rng{ 99 };
    std::vector<uint8_t> noisy(static_cast<size_t>(n) * n * 4);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const int base = 100 + x + y;
            Put(noisy, n, x, y, base + rng.Below(20), base + rng.Below(20), base / 2 + rng.Below(20), 255);
        }
    }
    const double noisyPsnr = ColourPsnr(noisy, RoundTrip(noisy, n, n));
    std::printf("  sharp_encode noisy 64x64: colour PSNR %.2f dB\n", noisyPsnr);
    CHECK(noisyPsnr > 32.0);
}

UNIT_TEST(sharp_encode_small_levels) {
    for (int s : { 1, 2, 3 }) {
        std::vector<uint8_t> img(static_cast<size_t>(s) * s * 4);
        for (int i = 0; i < s * s; ++i)
            Put(img, s, i % s, i / s, 255, 128, 0, 200);
        CHECK_EQ(Bc3LevelSize(s, s), static_cast<size_t>(16));
        // Nothing written past the one block.
        std::vector<uint8_t> enc(32, 0xcd);
        EncodeBc3(img.data(), s, s, s * 4, enc.data());
        for (int i = 16; i < 32; ++i)
            CHECK_EQ(enc[i], 0xcd);
        CHECK(MaxError(img, RoundTrip(img, s, s), true) <= 1);
    }
    // 2 x 2 of two colours, outside texels replicated but not fitted.
    std::vector<uint8_t> img(2 * 2 * 4);
    Put(img, 2, 0, 0, 0, 0, 0, 255);
    Put(img, 2, 1, 0, 255, 255, 255, 255);
    Put(img, 2, 0, 1, 255, 255, 255, 0);
    Put(img, 2, 1, 1, 0, 0, 0, 0);
    CHECK_EQ(MaxError(img, RoundTrip(img, 2, 2), true), 0);
}

UNIT_TEST(sharp_encode_threads_match) {
    const int w = 256;
    const int h = 128;
    Rng rng{ 7 };
    std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x)
            Put(img, w, x, y, (x * 3) & 255, (y * 5) & 255, rng.Below(256), (x / 16 + y / 16) % 3 ? 255 : rng.Below(256));
    }
    const MipChain chain = BuildMipChain(img.data(), w, h, w * 4);
    const Bc3Chain one = EncodeBc3Chain(chain, 1);
    const Bc3Chain four = EncodeBc3Chain(chain, 4);
    const Bc3Chain dflt = EncodeBc3Chain(chain);
    CHECK_EQ(one.offsets.size(), static_cast<size_t>(9));
    CHECK(one.data == four.data);
    CHECK(one.data == dflt.data);
    CHECK(one.offsets == four.offsets);
    // Each level equals the single-level encode.
    for (size_t i = 0; i < chain.offsets.size(); ++i) {
        const int lw = MipSize(w, static_cast<int>(i));
        const int lh = MipSize(h, static_cast<int>(i));
        std::vector<uint8_t> enc(Bc3LevelSize(lw, lh));
        EncodeBc3(&chain.pixels[chain.offsets[i]], lw, lh, lw * 4, enc.data());
        CHECK(std::memcmp(enc.data(), &one.data[one.offsets[i]], enc.size()) == 0);
    }
}

UNIT_TEST(sharp_encode_4096_timing) {
    // UI-sheet-like content: opaque gradient panels, transparent gaps, hard-edged icons.
    const int n = 4096;
    std::vector<uint8_t> img(static_cast<size_t>(n) * n * 4);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const int cell = ((x / 96) + (y / 96)) % 4;
            const int a = cell == 0 ? 0 : 255;
            const int r = cell == 3 ? ((x / 8 + y / 8) % 2 ? 230 : 40) : (x * 255) / n;
            Put(img, n, x, y, r, (y * 255) / n, 128 + cell * 30, a);
        }
    }
    const auto t0 = std::chrono::steady_clock::now();
    const MipChain chain = BuildMipChain(img.data(), n, n, n * 4);
    const auto t1 = std::chrono::steady_clock::now();
    const Bc3Chain bc = EncodeBc3Chain(chain);
    const auto t2 = std::chrono::steady_clock::now();
    const double mipMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double encMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::printf("  sharp_encode 4096x4096: mips %.0f ms, BC3 chain %.0f ms on %d thread(s), %zu bytes\n", mipMs,
                encMs, DefaultEncodeThreads(), bc.data.size());
    CHECK_EQ(bc.offsets.size(), static_cast<size_t>(13));
}
