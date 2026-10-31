#pragma once
// ============================================================================
// src/render/common/etc2/bc_to_etc2.h -- BC1 / BC3 levels re-encoded to ETC2, for a GPU without BC
// ============================================================================
// NOT RETAIL. Pure: no graphics API, no allocation. docs/android/P13_MEM.md: on a device without
// textureCompressionBC that samples ETC2, a backend transcodes each retail BC level at load, from the
// user's own data, instead of decoding it to RGBA8 (bc_decode.h): the same block grid and the same
// bytes per block, so a level keeps its BC size and layout.
//   BC1 -> ETC2 RGB8 (8-byte blocks), or ETC2 RGB8A1 when any texel of the texture is transparent
//          (a three-colour block's index 3; Bc1HasTransparency over every level decides);
//   BC3 -> ETC2 RGBA8 (16-byte blocks: EAC alpha, then ETC2 RGB).
// Each BC block is decoded (bc_decode.h) and encoded at once (etc2.h); no RGBA8 level is held. The
// texels of an edge block outside the level take the nearest texel inside it, so they do not pull the
// fit. The level sizes are bc_decode.h's Bc1Size / Bc3Size.
// ============================================================================
#include <cstdint>

namespace BcToEtc2 {

// Whether a w x h BC1 level has a transparent texel inside the level.
bool Bc1HasTransparency(const uint8_t* src, int w, int h);

// A w x h BC1 level (Bc1Size(w, h) bytes) into as many bytes of ETC2 RGB8, or RGB8A1 when
// `punchThrough`. Rows of blocks `rowBegin` .. `rowEnd` - 1 only (a band; 0 .. ceil(h / 4) for all).
void TranscodeBC1(const uint8_t* src, int w, int h, bool punchThrough, uint8_t* dst, int rowBegin, int rowEnd);
// A w x h BC3 level (Bc3Size(w, h) bytes) into as many bytes of ETC2 RGBA8; the same band.
void TranscodeBC3(const uint8_t* src, int w, int h, uint8_t* dst, int rowBegin, int rowEnd);

}  // namespace BcToEtc2
