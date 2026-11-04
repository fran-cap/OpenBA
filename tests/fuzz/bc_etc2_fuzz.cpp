// ============================================================================
// tests/fuzz/bc_etc2_fuzz.cpp -- BC1 / BC3 / BC4 / ETC2 block codecs over arbitrary blocks and levels
// ============================================================================
// P0_MAP surface rank 14 (src/render/common/bc_decode, bc4, etc2, bc_to_etc2). Input layout:
//   byte 0: mode, bytes 1-2: w, h (1..64 each: the callers' contract is w, h >= 1), byte 3: band
//   split / flags, then the level bytes. A level is taken only when the input holds at least
//   Bc1Size / Bc3Size / Bc4 EncodedSize(w, h) bytes (the callers check level sizes); it is copied
//   into a heap buffer of exactly that size so ASan sees any read past it. Destinations are exact.
// Modes also run the single-block decoders on raw blocks and the encoders on raw pixels, each encode
// decoded back.
// ============================================================================
#include "bc_decode/bc_decode.h"
#include "bc4/bc4.h"
#include "etc2/bc_to_etc2.h"
#include "etc2/etc2.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

void Bc1Level(const uint8_t* body, size_t n, int w, int h, int split, bool forcePt) {
    const size_t need = BcDecode::Bc1Size(w, h);
    if (n < need)
        return;
    std::vector<uint8_t> src(body, body + need);
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    BcDecode::DecodeBC1(src.data(), w, h, rgba.data(), w * 4);
    const bool pt = BcToEtc2::Bc1HasTransparency(src.data(), w, h) || forcePt;
    std::vector<uint8_t> out(need);
    const int bh = (h + 3) / 4;
    const int mid = split % (bh + 1);
    BcToEtc2::TranscodeBC1(src.data(), w, h, pt, out.data(), 0, mid);
    BcToEtc2::TranscodeBC1(src.data(), w, h, pt, out.data(), mid, bh);
}

void Bc3Level(const uint8_t* body, size_t n, int w, int h, int split) {
    const size_t need = BcDecode::Bc3Size(w, h);
    if (n < need)
        return;
    std::vector<uint8_t> src(body, body + need);
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    BcDecode::DecodeBC3(src.data(), w, h, rgba.data(), w * 4);
    std::vector<uint8_t> out(need);
    const int bh = (h + 3) / 4;
    const int mid = split % (bh + 1);
    BcToEtc2::TranscodeBC3(src.data(), w, h, out.data(), 0, mid);
    BcToEtc2::TranscodeBC3(src.data(), w, h, out.data(), mid, bh);
}

void Bc4Level(const uint8_t* body, size_t n, int w, int h) {
    const size_t need = Bc4::EncodedSize(w, h);
    if (n < need)
        return;
    std::vector<uint8_t> src(body, body + need);
    std::vector<uint8_t> plane(static_cast<size_t>(w) * h);
    Bc4::DecodeBC4(src.data(), w, h, plane.data(), w);
    std::vector<uint8_t> enc(need);
    Bc4::EncodeBC4(plane.data(), w, h, w, enc.data());
}

void RawBlocks(const uint8_t* body, size_t n, int split) {
    for (size_t at = 0; at + 16 <= n; at += 16) {
        uint8_t blk[16];
        std::memcpy(blk, body + at, 16);
        uint8_t a[64];
        uint8_t b[64];
        uint8_t p16[16];
        Etc2::DecodeRgbBlock(blk, false, a);
        Etc2::DecodeRgbBlock(blk, true, b);
        Etc2::DecodeAlphaBlock(blk, a);
        Etc2::DecodeRgbaBlock(blk, b);
        BcDecode::DecodeColourBlock(blk, (split & 1) != 0, a);
        BcDecode::DecodeBC3Block(blk, b);
        Bc4::DecodeBlock(blk, p16);
    }
}

void RawPixels(const uint8_t* body, size_t n, int split) {
    for (size_t at = 0; at + 64 <= n; at += 64) {
        uint8_t px[64];
        std::memcpy(px, body + at, 64);
        uint8_t out8[8];
        uint8_t out16[16];
        uint8_t dec[64];
        Etc2::EncodeRgbBlock(px, out8);
        Etc2::DecodeRgbBlock(out8, false, dec);
        Etc2::EncodeRgbA1Block(px, out8);
        Etc2::DecodeRgbBlock(out8, true, dec);
        Etc2::EncodeAlphaBlock(px, out8);
        Etc2::DecodeAlphaBlock(out8, dec);
        Etc2::EncodeRgbaBlock(px, out16);
        Etc2::DecodeRgbaBlock(out16, dec);
        uint8_t a16[16];
        for (int i = 0; i < 16; ++i)
            a16[i] = px[i * 4 + 3];
        const unsigned valid = (static_cast<unsigned>(split) << 8 | static_cast<unsigned>(split)) & 0xFFFF;
        Bc4::EncodeBlock(a16, valid, out8);
        Bc4::DecodeBlock(out8, a16);
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 4 || size > 0x20000)
        return 0;
    const int mode = data[0] % 7;
    const int w = 1 + data[1] % 64;
    const int h = 1 + data[2] % 64;
    const int split = data[3];
    const uint8_t* body = data + 4;
    const size_t n = size - 4;
    switch (mode) {
    case 0: Bc1Level(body, n, w, h, split, false); break;
    case 1: Bc1Level(body, n, w, h, split, true); break;
    case 2: Bc3Level(body, n, w, h, split); break;
    case 3: Bc4Level(body, n, w, h); break;
    case 4: RawBlocks(body, n, split); break;
    default: RawPixels(body, n, split); break;
    }
    return 0;
}
