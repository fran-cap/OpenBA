#pragma once
// ============================================================================
// src/render/common/better_trees/blossom.h -- natural tree growth's spring blossom and catkins
// (docs/extensions/BLOSSOM.md)
// ============================================================================
// NOT RETAIL. Pure: no engine or D3D types; the unit tests link it.
//
// WHAT IT IS. Part of ext.better_trees' natural tree growth (NATURAL_GROWTH.md): in spring each deciduous
// canopy flowers for about half a month on its own schedule. Fruit trees get blossom clusters (white, pink),
// oak / birch / pecan / chestnut get catkins (yellow-green streaks). Everything is procedural in the
// rewritten billboard shaders (natural_growth.cpp): no texture, no new binding on any backend.
//
// THE SPECIES. Every deciduous canopy shares one BillboardMaterial program, so the shader cannot tell an
// apple from an oak. The CPU writes a per-draw code into gc.time.z / gc.time.w (cb0[10].zw), which
// Context::SetTime always writes 0 and no retail program reads (NATURAL_GROWTH.md S3 made the same case for
// .y). Ext::BetterTrees' MaterialInstance::Setup hook picks the code from the draw's material instance:
//     z = 1 + peak + 16 flags + 64 q     peak in [0, 12) months, flags bit 0 catkin / bit 1 pink buds,
//                                        q = density * 16 - 1 in 0..15
//     w = R * 65536 + G * 256 + B         the colour, 8 bits a channel (exact in a float)
// and 0 / 0 on every other draw. z = 0 (no code) or time.y = 0 (no season stamp) = no bloom: retail.
//
// THE VERTEX TERMS (BillboardColour only; T, h, g, d as natural_growth.h):
//     j     = kJitter (2 frc(h.x + h.y) - 1) + kGroveShift g        the tree's offset, months
//     x     = wrap12(d - peak - j)                                   months from the tree's peak, [-6, 6)
//     b     = sat(kPlateau (1 - |x| / kHalfWidth))                   the bloom amount
//     open  = sat(x / kHalfWidth + 1)                                0 at the first bud, 1 at the peak
//     cov   = b * density, negative for catkins
//     rgb   = colour * (1 + pink (1 - open) kBudTint)                pink buds open to the colour
// out: a second new interpolant (rgb, cov).
//
// THE PIXEL TERMS. A cell grid over the card's UV (kCells cells a side); each cell holds a cluster with
// probability |cov| at a hashed centre, radius kRadius0 + kRadius1 h, a soft edge; catkins squash the cell
// into a vertical streak. Where a cell is under ~2.5 pixels the mask blends to its average (|cov| kArea),
// so the far canopy reads as a haze instead of shimmering. The flower mask F then:
//     kept  = sat(4 F)                 the leaf loss is cut by (1 - kept): a bare tree keeps its flowers
//     l'    = l (1 - kept)             replaces l in the season mad (alpha - l' move)
//     k     = (F + l (kept - F)) move  the colour lerp toward rgb * bright
// ============================================================================
#include "better_trees/natural_growth.h"

#include <cstdint>
#include <string>

namespace Blossom {

// ---- the vertex curve (months) --------------------------------------------------------------------
constexpr float kHalfWidth  = 0.30f;   // a tree flowers for about 2 * 0.3 = 0.6 month (one to two weeks real)
constexpr float kPlateau    = 1.6f;    // the bloom is full over the middle 3/8 of that
constexpr float kJitter     = 0.15f;   // the per-tree offset's half range
constexpr float kGroveShift = 0.05f;   // groves flower a little together (g in [-2, 2])
constexpr float kBudTint[3] = {0.0f, -0.28f, -0.20f};   // pink buds: G and B down before the flower opens

// ---- the pixel mask ----------------------------------------------------------------------------------
constexpr float kCells     = 32.0f;    // cells across the card's UV square
constexpr float kRadius0   = 0.16f;    // cluster radius in cells: kRadius0 + kRadius1 h
constexpr float kRadius1   = 0.14f;
constexpr float kEdge      = 30.0f;    // the soft edge: sat((r^2 - d^2) * kEdge)
constexpr float kCatkinX   = 2.4f;     // catkins: the cell is narrowed in u ...
constexpr float kCatkinY   = 0.55f;    // ... and stretched in v
constexpr float kArea      = 0.18f;    // the mean covered share of a present cell (the far-field average)
constexpr float kAaStart   = 0.40f;    // cells per pixel where the blend to the average begins
constexpr float kAaSlope   = 2.5f;     // ... and its rate (full at 0.8 cells per pixel)
constexpr float kKeep      = 4.0f;     // kept = sat(kKeep F)
constexpr float kBright0   = 0.85f;    // a cluster's brightness: kBright0 + kBright1 h
constexpr float kBright1   = 0.30f;

enum Flags : uint32_t {
    kCatkin   = 1u,
    kPinkBuds = 2u,
};

struct Species {
    const char* name;      // a log label
    uint8_t     rgb[3];
    float       density;   // 1/16 .. 1 (quantised)
    float       peak;      // months, 0 = the start of Early Spring
    uint32_t    flags;
};

// The species of a canopy from its draw's names (lower case): the material's path and the diffuse texture's
// path and the material instance's own path, joined. False when the draw is not a deciduous canopy (not a
// billboard material, or the evergreen one): its code is 0 / 0. A canopy no table row names (a mod's tree)
// gets a species from the FNV-1a hash of `key`: blossom from a pale palette, flowering in mid spring.
// `known` (may be null) is set when a table row matched.
bool SpeciesFor(const std::string& materialPath, const std::string& names, Species* out, bool* known);

// The two floats for gc.time.z / .w.
void Pack(const Species& s, float* z, float* w);

// FNV-1a, 32 bits (the same on every platform and compiler; std::hash is not).
uint32_t Fnv1a(const std::string& s);

// ---- the CPU mirror (unit tests) ---------------------------------------------------------------------
// The vertex shader's second interpolant (r, g, b, cov) of a tree at `months` under the code (z, w), with
// the stamp present (time.y >= 0.5) or not.
void VertexOut(const NgRewrite::TreeTerms& t, float months, bool stamped, float z, float w, float out[4]);

struct PixelTerms {
    float lossCut;   // l' (the season mad's loss)
    float amount;    // k (the colour lerp, move included)
    float bright;    // the cluster's brightness
    float mask;      // F
};
// The pixel terms at (u, v) for the interpolant `n2`, the leaf loss `l`, `move`, and the AA blend `aa`
// (the shader's sat((cells per pixel - kAaStart) * kAaSlope); 0 near).
PixelTerms Pixel(float u, float v, const float n2[4], float l, float move, float aa);

// hash33 (natural_growth.h's Dave Hoskins hash without sine) of (x, y), the CPU twin of the shader's.
void Hash33(float x, float y, float out[3]);

}  // namespace Blossom
