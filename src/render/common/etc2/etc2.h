#pragma once
// ============================================================================
// src/render/common/etc2/etc2.h -- an ETC2 RGB / RGB8A1 / EAC alpha block encoder and decoder
// ============================================================================
// NOT RETAIL. Pure: no graphics API, no allocation. docs/android/P13_MEM.md: a GPU without
// textureCompressionBC (the Galaxy S10's Adreno 640) samples ETC2, so the retail BC1 / BC3 textures
// are re-encoded to ETC2 at load (bc_to_etc2.h) instead of being decoded to RGBA8 at 4-8 times the
// bytes. The encode is lossy; the decoders here are the reference the unit tests measure it with.
//
// THE FORMATS (Khronos Data Format Specification, "ETC2"). A block is a big-endian 64-bit word.
// Pixel (x, y) is pixel i = 4x + y (column order) of the index fields.
//  * ETC2 RGB. Bit 33 is the diff bit, bit 32 the flip bit (0: two 2x4 halves left / right, 1: two
//    4x2 halves top / bottom). Diff 0, INDIVIDUAL: bits 63..40 are R1 R2 G1 G2 B1 B2, 4 bits each
//    (expanded x * 17). Diff 1, DIFFERENTIAL: R 63..59, dR 58..56, G 55..51, dG 50..48, B 47..43,
//    dB 42..40 (5-bit bases, 3-bit two's-complement deltas; half 2's base is base + delta, expanded
//    (x << 3) | (x >> 2)). Bits 39..37 / 36..34 pick each half's modifier table; pixel i's 2-bit
//    index is bit 16 + i (high) and bit i (low): 0 = +a, 1 = +b, 2 = -a, 3 = -b of the table, the
//    modifier added to every channel and clamped to 0..255. In differential mode a base + delta
//    outside 0..31 selects another mode: R the T mode, else G the H mode, else B the PLANAR mode:
//    RO 62..57, GO 56 + 54..49, BO 48 + 44..43 + 41..39, RH 38..34 + 32, GH 31..25, BH 24..19,
//    RV 18..13, GV 12..6, BV 5..0 (6:7:6 bits, expanded by replication), the pixel
//    (x (H - O) + y (V - O) + 4 O + 2) >> 2 per channel, clamped. The free bits 63, 55, 47..45 and 42
//    are set so that only B overflows.
//  * ETC2 RGB8A1 (punch-through). Bit 33 is the OPAQUE bit; there is no individual mode. Opaque 1 is
//    the RGB block above. Opaque 0 in differential mode: index 0 is the base itself, 1 = +b, 2 a
//    transparent black texel (0, 0, 0, 0), 3 = -b. Planar blocks are opaque either way.
//  * EAC alpha (the first half of ETC2 RGBA8): base 63..56, multiplier 55..52, table 51..48, then
//    sixteen 3-bit indices, pixel i at bits 47 - 3i .. 45 - 3i; alpha = base + table[index] *
//    multiplier, clamped.
//
// THE ENCODER (P13_MEM.md §3). RGB: per flip, each half's base is its mean (individual and
// differential quantisations), each pixel takes the modifier nearest its mean channel offset from the
// base, the error is measured exactly with the clamp; the best table per half, then one refit of the
// base to the chosen modifiers. Planar: a least-squares plane per channel. The least squared RGB error
// wins. T and H modes are decoded, never encoded. Alpha: the 16 tables, each with the multiplier
// spanning the block's range and the base centred, the best refined by +-1 multiplier and +-2 base.
// ============================================================================
#include <cstdint>

namespace Etc2 {

// 16 RGBA8 pixels in row order (pixel (x, y) at byte 4 * (4y + x)) into one 8-byte ETC2 RGB block.
// Alpha is ignored.
void EncodeRgbBlock(const uint8_t pixels[64], uint8_t out[8]);
// The same with punch-through alpha: a pixel whose alpha is below 128 is transparent (its colour is
// not encoded), every other pixel opaque.
void EncodeRgbA1Block(const uint8_t pixels[64], uint8_t out[8]);
// The 16 pixels' alpha (byte 4i + 3) into one 8-byte EAC block.
void EncodeAlphaBlock(const uint8_t pixels[64], uint8_t out[8]);
// EAC alpha + ETC2 RGB: one 16-byte ETC2 RGBA8 block.
void EncodeRgbaBlock(const uint8_t pixels[64], uint8_t out[16]);

// The decoders, out in the encoders' pixel order. DecodeRgbBlock writes alpha 255, or 0 for a
// punch-through transparent texel when `punchThrough`. DecodeAlphaBlock writes byte 4i + 3 only.
void DecodeRgbBlock(const uint8_t in[8], bool punchThrough, uint8_t pixels[64]);
void DecodeAlphaBlock(const uint8_t in[8], uint8_t pixels[64]);
void DecodeRgbaBlock(const uint8_t in[16], uint8_t pixels[64]);

}  // namespace Etc2
