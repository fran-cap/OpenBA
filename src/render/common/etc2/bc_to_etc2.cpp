// ============================================================================
// src/render/common/etc2/bc_to_etc2.cpp -- BC1 / BC3 to ETC2 (bc_to_etc2.h has the contract)
// ============================================================================
#include "etc2/bc_to_etc2.h"

#include "bc_decode/bc_decode.h"
#include "etc2/etc2.h"

#include <cstring>

namespace BcToEtc2 {

namespace {

// An edge block's texels outside the level (x >= cols or y >= rows) copied from the nearest inside.
void FillEdge(uint8_t px[64], int cols, int rows) {
    if (cols >= 4 && rows >= 4) return;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int sx = x < cols ? x : cols - 1;
            const int sy = y < rows ? y : rows - 1;
            if (sx != x || sy != y) std::memcpy(px + (4 * y + x) * 4, px + (4 * sy + sx) * 4, 4);
        }
    }
}

template <typename Block>
void Transcode(const uint8_t* src, int w, int h, uint8_t* dst, int blockBytes, int rowBegin, int rowEnd,
               Block block) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    if (rowEnd > bh) rowEnd = bh;
    for (int by = rowBegin; by < rowEnd; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const size_t at = (static_cast<size_t>(by) * bw + bx) * static_cast<size_t>(blockBytes);
            const int cols = w - bx * 4 < 4 ? w - bx * 4 : 4;
            const int rows = h - by * 4 < 4 ? h - by * 4 : 4;
            block(src + at, dst + at, cols, rows);
        }
    }
}

}  // namespace

bool Bc1HasTransparency(const uint8_t* src, int w, int h) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* in = src + (static_cast<size_t>(by) * bw + bx) * 8;
            const unsigned c0 = in[0] | (in[1] << 8);
            const unsigned c1 = in[2] | (in[3] << 8);
            if (c0 > c1) continue;
            const uint32_t bits = static_cast<uint32_t>(in[4]) | (static_cast<uint32_t>(in[5]) << 8) |
                                  (static_cast<uint32_t>(in[6]) << 16) | (static_cast<uint32_t>(in[7]) << 24);
            for (int y = 0; y < 4 && by * 4 + y < h; ++y) {
                for (int x = 0; x < 4 && bx * 4 + x < w; ++x) {
                    if (((bits >> (2 * (4 * y + x))) & 3) == 3) return true;
                }
            }
        }
    }
    return false;
}

void TranscodeBC1(const uint8_t* src, int w, int h, bool punchThrough, uint8_t* dst, int rowBegin, int rowEnd) {
    Transcode(src, w, h, dst, 8, rowBegin, rowEnd, [punchThrough](const uint8_t* in, uint8_t* out, int cols, int rows) {
        uint8_t px[64];
        BcDecode::DecodeColourBlock(in, false, px);
        FillEdge(px, cols, rows);
        if (punchThrough) {
            Etc2::EncodeRgbA1Block(px, out);
        } else {
            Etc2::EncodeRgbBlock(px, out);
        }
    });
}

void TranscodeBC3(const uint8_t* src, int w, int h, uint8_t* dst, int rowBegin, int rowEnd) {
    Transcode(src, w, h, dst, 16, rowBegin, rowEnd, [](const uint8_t* in, uint8_t* out, int cols, int rows) {
        uint8_t px[64];
        BcDecode::DecodeBC3Block(in, px);
        FillEdge(px, cols, rows);
        Etc2::EncodeRgbaBlock(px, out);
    });
}

}  // namespace BcToEtc2
