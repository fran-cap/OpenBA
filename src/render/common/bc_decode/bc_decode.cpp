// ============================================================================
// src/render/common/bc_decode/bc_decode.cpp -- BC1 / BC3 to RGBA8 (bc_decode.h has the formats)
// ============================================================================
#include "bc_decode/bc_decode.h"

#include "bc4/bc4.h"

namespace BcDecode {

namespace {

size_t Blocks(int w, int h) {
    const size_t bw = static_cast<size_t>(w > 0 ? (w + 3) / 4 : 1);
    const size_t bh = static_cast<size_t>(h > 0 ? (h + 3) / 4 : 1);
    return bw * bh;
}

void Expand565(uint32_t c, int rgb[3]) {
    const int r = static_cast<int>((c >> 11) & 31);
    const int g = static_cast<int>((c >> 5) & 63);
    const int b = static_cast<int>(c & 31);
    rgb[0] = (r << 3) | (r >> 2);
    rgb[1] = (g << 2) | (g >> 4);
    rgb[2] = (b << 3) | (b >> 2);
}

// The blocks of a w x h level, `blockBytes` each, through `decode` into dst.
template <typename Decode>
void DecodeSurface(const uint8_t* src, int w, int h, uint8_t* dst, int dstPitch, size_t blockBytes,
                   Decode decode) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            uint8_t block[64];
            decode(src + (static_cast<size_t>(by) * bw + bx) * blockBytes, block);
            for (int y = 0; y < 4; ++y) {
                const int py = by * 4 + y;
                if (py >= h) break;
                for (int x = 0; x < 4; ++x) {
                    const int px = bx * 4 + x;
                    if (px >= w) break;
                    uint8_t* out = dst + static_cast<size_t>(py) * dstPitch + static_cast<size_t>(px) * 4;
                    const uint8_t* in = block + (y * 4 + x) * 4;
                    out[0] = in[0];
                    out[1] = in[1];
                    out[2] = in[2];
                    out[3] = in[3];
                }
            }
        }
    }
}

}  // namespace

size_t Bc1Size(int w, int h) {
    return Blocks(w, h) * 8;
}

size_t Bc3Size(int w, int h) {
    return Blocks(w, h) * 16;
}

void DecodeColourBlock(const uint8_t in[8], bool fourColour, uint8_t out[64]) {
    const uint32_t c0 = static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8);
    const uint32_t c1 = static_cast<uint32_t>(in[2]) | (static_cast<uint32_t>(in[3]) << 8);
    int pal[4][4];
    Expand565(c0, pal[0]);
    Expand565(c1, pal[1]);
    pal[0][3] = 255;
    pal[1][3] = 255;
    if (fourColour || c0 > c1) {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = (2 * pal[0][k] + pal[1][k] + 1) / 3;
            pal[3][k] = (pal[0][k] + 2 * pal[1][k] + 1) / 3;
        }
        pal[2][3] = 255;
        pal[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = (pal[0][k] + pal[1][k] + 1) / 2;
            pal[3][k] = 0;
        }
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
    const uint32_t bits = static_cast<uint32_t>(in[4]) | (static_cast<uint32_t>(in[5]) << 8) |
                          (static_cast<uint32_t>(in[6]) << 16) | (static_cast<uint32_t>(in[7]) << 24);
    for (int i = 0; i < 16; ++i) {
        const int* c = pal[(bits >> (2 * i)) & 3];
        out[i * 4 + 0] = static_cast<uint8_t>(c[0]);
        out[i * 4 + 1] = static_cast<uint8_t>(c[1]);
        out[i * 4 + 2] = static_cast<uint8_t>(c[2]);
        out[i * 4 + 3] = static_cast<uint8_t>(c[3]);
    }
}

void DecodeBC3Block(const uint8_t in[16], uint8_t out[64]) {
    DecodeColourBlock(in + 8, true, out);
    uint8_t alpha[16];
    Bc4::DecodeBlock(in, alpha);
    for (int i = 0; i < 16; ++i)
        out[i * 4 + 3] = alpha[i];
}

void DecodeBC1(const uint8_t* src, int w, int h, uint8_t* dst, int dstPitch) {
    DecodeSurface(src, w, h, dst, dstPitch, 8, [](const uint8_t* in, uint8_t* out) {
        DecodeColourBlock(in, false, out);
    });
}

void DecodeBC3(const uint8_t* src, int w, int h, uint8_t* dst, int dstPitch) {
    DecodeSurface(src, w, h, dst, dstPitch, 16, [](const uint8_t* in, uint8_t* out) {
        DecodeBC3Block(in, out);
    });
}

}  // namespace BcDecode
