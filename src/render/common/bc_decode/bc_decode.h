#pragma once
// ============================================================================
// src/render/common/bc_decode/bc_decode.h -- BC1 / BC3 decoded to RGBA8, for a GPU without BC
// ============================================================================
// NOT RETAIL. Pure: no graphics API, no allocation. Retail's textures are BC1 (DXT1) and BC3 (DXT5),
// which every desktop GPU samples; a GPU without textureCompressionBC (the Galaxy S10's Adreno 640)
// cannot. A backend on such a GPU decodes each level at load, from the user's own data, into
// R8G8B8A8 (4 or 8 times the bytes) and samples that.
//
// THE FORMATS (the D3D10+ decode). A BC1 block is 8 bytes: two RGB565 endpoints c0, c1 (little
// endian), then sixteen 2-bit indices, pixel (x, y) at bit 2 * (4y + x). Each endpoint expands to
// 8 bits a channel by bit replication. c0 > c1 selects {c0, c1, (2 c0 + c1) / 3, (c0 + 2 c1) / 3}
// opaque; c0 <= c1 selects {c0, c1, (c0 + c1) / 2, transparent black}. A BC3 block is 16 bytes: a
// BC4 block for alpha (bc4.h), then a BC1 colour block always read in its four-colour mode. The
// interpolants are rounded to the nearest byte, within one step of what a GPU returns.
//
// THE SURFACE. As bc4.h: a level of w x h pixels is max(1, ceil(w / 4)) x max(1, ceil(h / 4))
// blocks, rows packed tight; texels of an edge block outside the level are not written.
// ============================================================================
#include <cstddef>
#include <cstdint>

namespace BcDecode {

// Bytes of one encoded level of w x h pixels (w, h >= 1): 8 per block for BC1, 16 for BC3.
size_t Bc1Size(int w, int h);
size_t Bc3Size(int w, int h);

// Decodes Bc1Size / Bc3Size(w, h) bytes into w x h RGBA8 pixels at dst (row pitch `dstPitch` bytes).
void DecodeBC1(const uint8_t* src, int w, int h, uint8_t* dst, int dstPitch);
void DecodeBC3(const uint8_t* src, int w, int h, uint8_t* dst, int dstPitch);

// One block: out holds 16 RGBA8 pixels in row order. `fourColour` forces the four-colour mode (BC3).
void DecodeColourBlock(const uint8_t in[8], bool fourColour, uint8_t out[64]);
void DecodeBC3Block(const uint8_t in[16], uint8_t out[64]);

}  // namespace BcDecode
