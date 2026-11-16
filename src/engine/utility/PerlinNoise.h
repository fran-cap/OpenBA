#pragma once
// ============================================================================
// src/engine/utility/PerlinNoise.h — Utility::PerlinNoise layout
//                                       (Wave 8, lane E)
// ============================================================================
//
// sizeof(Utility::PerlinNoise) == 0x10, proven directly: the ctor
// (`??0PerlinNoise@Utility@@QEAA@MMHH@Z`, 0x180108c70) is four stores at
// +0x00 (float), +0x04 (float), +0xc (int), +0x08 (int) and nothing else, and
// `Generate`/`SmoothNoise` read no offset past +0xc.
//
//   MOV EAX,[RSP+0x28]           ; 4th ctor arg (int), spilled to the stack
//   MOVSS dword ptr [RCX],XMM1   ; +0x00 = 1st ctor arg (float)   -> _persistence
//   MOVSS dword ptr [RCX+0x4],XMM2  ; +0x04 = 2nd ctor arg (float) -> _octaves
//   MOV dword ptr [RCX+0xc],EAX  ; +0x0c = 4th ctor arg (int)     -> _offsetY
//   MOV RAX,RCX                  ; returns `this`
//   MOV dword ptr [RCX+0x8],R9D  ; +0x08 = 3rd ctor arg (int, R9D) -> _offsetX
//
// NOT A POLYMORPHIC CLASS: the first (and only) store is a float value, never
// a vtable `LEA`, and the DLL exports no `??_7PerlinNoise@Utility@@6B@`
// (WAVE8.md §2's pre-wave measurement).
//
// FIELD NAMES are inferred from usage, not from any exported accessor — none
// exists for this class:
//   * `_persistence` (+0x00) is the base `Generate`'s per-octave amplitude
//     multiplies, `pow(_persistence, i)`.
//   * `_octaves` (+0x04) is `Generate`'s loop bound, compared as a float
//     against the float loop counter — so it is genuinely a float field, not
//     an int truncated on read, matching the ctor's `MOVSS` store.
//   * `_offsetX` (+0x08, 3rd ctor arg) and `_offsetY` (+0x0c, 4th ctor arg)
//     are `SmoothNoise`'s two lattice-hash offsets: see the derivation next
//     to `SmoothNoise` in perlin.cpp for why X is +0x08 and Y is +0x0c.
// ============================================================================

#include <cstdint>
#include <cstddef>

namespace RtUtility {

struct PerlinNoise {
    float   _persistence;  // +0x00  amplitude base: pow(_persistence, octave)
    float   _octaves;      // +0x04  Generate's loop bound (compared as float)
    int32_t _offsetX;      // +0x08  SmoothNoise lattice X offset (ctor's 3rd arg, R9D)
    int32_t _offsetY;      // +0x0c  SmoothNoise lattice Y offset (ctor's 4th arg, stack)
};
static_assert(offsetof(PerlinNoise, _octaves)  == 0x04, "PerlinNoise._octaves @ 0x04");
static_assert(offsetof(PerlinNoise, _offsetX)  == 0x08, "PerlinNoise._offsetX @ 0x08");
static_assert(offsetof(PerlinNoise, _offsetY)  == 0x0c, "PerlinNoise._offsetY @ 0x0c");
static_assert(sizeof(PerlinNoise) == 0x10, "sizeof(Utility::PerlinNoise) == 0x10");
static_assert(offsetof(PerlinNoise, _persistence) == 0x0, "PerlinNoise::_persistence");

}  // namespace RtUtility

// ==== B3R W0-P0c: Runtime bodies, called by name ====
// Each body below is declared here, copied from its definition (linkage,
// namespace, types), so its callers can call it by name. The retail address
// is in its trailing comment.

// ---- utility/perlin.cpp
RtUtility::PerlinNoise* RtUtility_PerlinNoise_Ctor(RtUtility::PerlinNoise* self, float persistence, float octaves, int32_t offsetX, int32_t offsetY);  // 0x180108c70
float RtUtility_PerlinNoise_Generate(const RtUtility::PerlinNoise* self, float x, float y);  // 0x1801089b0
// ==== end B3R W0-P0c ====
