// ============================================================================
// src/ext/sharp_ui/sharp_encode.h -- ext.sharp_ui's magnified sheet as a BC3
// texture with a full mip chain, the pure half (NOT RETAIL;
// docs/extensions/handins/SHARP_SPRITES.md)
// ============================================================================
// The 4x copy of the sprite sheet (sharp_sheet.h) is 4096 x 4096 BGRA8, 64 MB
// uncompressed; as BC3 with its mips it is ~21 MB. This file builds that from
// the one magnified level, on the CPU, at load:
//   1. BuildMipChain: the full chain down to 1 x 1, each level a 2 x 2 box of
//      the one above with ALPHA-WEIGHTED colour (colour = sum(c * a) / sum(a),
//      alpha = mean; a box with no coverage takes the plain colour mean). The
//      UI blends straight alpha, so a transparent texel's colour (black in the
//      sheet) must not darken the edge of a sprite in a smaller mip.
//   2. EncodeBc3: one level to BC3 blocks in the D3D layout. The alpha block
//      is a BC4 block (src/render/common/bc4/bc4.h, bit-identical). The colour
//      block is a BC1 block in its four-colour mode, c0 > c1 always (c0 == c1
//      is written as c0, c0 - 1 so a decoder that tests the order still reads
//      four colours): principal-axis fit, least-squares refinement of the
//      endpoints to the chosen indices, and an exact single-colour fit of the
//      block's mean, keeping the lowest error. Colour error is weighted by
//      alpha, so transparent texels do not pull the endpoints.
//   3. EncodeBc3Chain: every level, the block rows cut into bands that up to
//      min(8, hardware_concurrency / 2) worker threads take in turn; the call
//      joins them before it returns. The output does not depend on the thread
//      count.
// Pixels are BGRA8 (B at byte 0, the sheet's DXGI_FORMAT_B8G8R8A8 order);
// the BC3 block stores R in the top five bits of a 565 colour, as D3D reads it.
//
// Standard C++ only: tests/unit/ext_sharp_encode_test.cpp includes it. The
// source is built -O2 in every flavour (CMakeLists.txt, like bc4.cpp).
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Ext {
namespace SharpUI {

// Levels of a full chain from w x h down to 1 x 1: 1 + floor(log2(max(w, h))).
int MipLevelCount(int w, int h);
// The size of level `level` of a w x h chain: max(1, w >> level).
int MipSize(int size, int level);

// A chain of BGRA8 levels, back to back in `pixels`, rows packed tight
// (pitch = width * 4). Level i is MipSize(width, i) x MipSize(height, i) and
// starts at offsets[i]; offsets.size() == MipLevelCount(width, height).
struct MipChain {
    int                  width  = 0;
    int                  height = 0;
    std::vector<size_t>  offsets;
    std::vector<uint8_t> pixels;
};

// The full chain of one w x h BGRA8 level (row pitch `pitch` bytes); level 0
// is a copy. Odd sizes clamp the 2 x 2 box at the right / bottom edge.
MipChain BuildMipChain(const uint8_t* bgra, int w, int h, int pitch);

// Bytes of one BC3 level of w x h texels: 16 per 4 x 4 block,
// max(1, ceil(w / 4)) x max(1, ceil(h / 4)) blocks.
size_t Bc3LevelSize(int w, int h);
// Bytes of one row of blocks.
int Bc3RowPitch(int w);

// One block: 16 BGRA8 texels in row order (64 bytes); `valid` is a 16-bit
// mask of the texels that belong to the level (bit 4y + x), the others play
// no part in the fit. Out: 16 bytes (BC4 alpha block, then BC1 colour block).
void EncodeBc3Block(const uint8_t bgra[64], unsigned valid, uint8_t out[16]);

// Block rows [rowBegin, rowEnd) of one w x h level (row pitch `pitch` bytes)
// into dst, which points at the level's first block (Bc3LevelSize(w, h)).
void EncodeBc3Rows(const uint8_t* bgra, int w, int h, int pitch, int rowBegin, int rowEnd, uint8_t* dst);
// One whole level, on the calling thread.
void EncodeBc3(const uint8_t* bgra, int w, int h, int pitch, uint8_t* dst);

// A BC3 chain: levels back to back in `data`, level i at offsets[i],
// Bc3LevelSize(MipSize(width, i), MipSize(height, i)) bytes, row pitch
// Bc3RowPitch(MipSize(width, i)).
struct Bc3Chain {
    int                  width  = 0;
    int                  height = 0;
    std::vector<size_t>  offsets;
    std::vector<uint8_t> data;
};

// The worker count EncodeBc3Chain uses for threads == 0:
// min(8, hardware_concurrency / 2), at least 1.
int DefaultEncodeThreads();

// Every level of `chain` encoded. threads <= 0: DefaultEncodeThreads();
// 1 encodes on the calling thread. The result is the same for any count.
Bc3Chain EncodeBc3Chain(const MipChain& chain, int threads = 0);

}  // namespace SharpUI
}  // namespace Ext
