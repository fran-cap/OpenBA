#pragma once
// ============================================================================
// src/render/common/better_trees/natural_growth.h -- ext.better_trees' "Natural tree growth": the
// per-tree autumn colour, leaf fall, spring regrowth and green variation (docs/extensions/NATURAL_GROWTH.md)
// ============================================================================
// NOT RETAIL. Pure: no D3D, no retail bytes (the unit tests build synthetic SM4 blobs). It reads the
// blob it is handed -- the user's installed shaders, at run time, BEFORE the cutout, the receiver
// lookup and the edge dither -- and emits a copy in memory.
//
// RETAIL. Every deciduous canopy turns at the same moment: WeatherSimulation's fadeA (the fall colour,
// gc/pc.userPosition.y) and fadeB (the leaf loss, .z) are map-wide. BillboardMaterial's colour pixel
// shader lerps the leaf toward one gold tint by fadeA * move and cuts the alpha by fadeB * move; its
// depth vertex shader hands fadeB * move to the shadow caster. FoliageMaterial (the pines) has no season
// term but the snow.
//
// THE DAY. The rewritten vertex shaders read `gc.time.y` (cb0[10].y), which retail's SetTime always
// writes 0 and no retail program reads. While the part is on, the game writes 1 + the day of the year in
// months (0..12) there after every SetTime (Ext::BetterTrees::SeasonStamp). 0 = no stamp (the menu
// scene, a loader): the copies then use retail's fadeA / fadeB and gold, so a missed stamp is retail.
//
// THE PER-TREE TERMS (all kinds, in the vertex shader, inserted before the first read of the instance's
// translation row tc.transforms[instance] row 3 = cb3[rI + 3], the same for every vertex of a tree):
//     h = hash33(T.xy)                    three hashes in [0, 1) (Dave Hoskins' hash without sine)
//     g = sin(T.xy . G1) + sin(T.xy . G2 + 2.1)   a grove term in [-2, 2], wavelength ~450 units
//     t = 2 h.z - 1                       the green variation
// BillboardColour / BillboardDepth only (Curves):
//     o = autumnSpread (1.2 h.x - 0.6 + 0.2 g)                 the tree's autumn offset, months
//     s = springStart + springSpread (1.2 frc(h.x + h.z) - 0.6 + 0.2 g)   its regrowth start
//     d = time.y - 1
//     c = saturate((d - o - colourStart) / colourLength)                          the fall colour
//     l = max(saturate((d - o - fallStart) / fallLength), 1 - saturate((d - s) / growLength))  the loss
//     time.y < 0.5: c = userPosition.y, l = userPosition.z, hue 0 (retail's terms and gold)
//
// THE KINDS.
//   BillboardColour (vs): a NEW interpolant, the next register and TEXCOORD index after the last output,
//     = (c, l, hue = h.y, t); OSGN gains the element. The pixel copy reads it.
//   BillboardColour (ps): the season mad reads l (the interpolant's y) for fadeB; the fall mul reads c
//     (x) for fadeA; the gold immediate becomes a palette colour of hue (z) browned by l; after the
//     discard the leaf is tinted by 1 + t * move * kTint. ISGN gains the element, `dcl_input_ps linear`
//     the register. With (c, l, hue, t) = (fadeA, fadeB, 0, 0) the colour is retail's but for the browning.
//   BillboardDepth (vs): the shadow caster's `fadeB * move` reads l instead of userPosition.z. No
//     signature change; the depth pixel shader is retail's.
//   Foliage (vs): the snow interpolant's unused w carries t (dcl_output and OSGN widened to xyzw).
//   Foliage (ps): after the discard the needle colour is tinted by 1 + t * kTint (ISGN and dcl widened).
// A rewritten billboard pixel shader reads an input only the rewritten vertex shader writes: the stages
// bind it only beside a natural-growth vertex copy (`Pairs`), else retail's.
//
// THE MATCHES (Analyse*). vs_4_x / ps_4_x, no flow control, `ret` once and last, no indexable temps,
// plus each kind's own shape (natural_growth.cpp). Anything else is not a match and stays byte-identical.
// ============================================================================
#include "better_trees/dxbc.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace NgRewrite {

constexpr float kMonthSeconds = 300.0f;   // weather_sim.cpp kKfSpan: Calendar::yearTime / 300 = months
constexpr float kStampBase    = 1.0f;     // time.y = kStampBase + months; 0 = no stamp

// The curves (NATURAL_GROWTH.md §3.2; months, 0 = the start of Early Spring). Immediates in the code.
struct Curves {
    float autumnSpread = 0.75f;   // the autumn offset's half range (with the grove term)
    float colourStart  = 6.0f;    // retail's fadeA starts at 6.0 for 1.5
    float colourLength = 1.25f;
    float fallStart    = 7.25f;   // retail's fadeB rises from 7.5 for 0.5
    float fallLength   = 0.75f;
    float springStart  = 0.75f;   // retail's fadeB falls from 0 for 0.5
    float springSpread = 0.75f;
    float growLength   = 0.75f;
};
constexpr Curves kCurves{};

// The autumn palette (hue 0 = retail's gold 233,185,71) and the late browning.
constexpr float kGold[3]   = {233.0f / 255.0f, 185.0f / 255.0f, 71.0f / 255.0f};
constexpr float kOrange[3] = {0.93f, 0.52f, 0.16f};
constexpr float kRed[3]    = {0.80f, 0.22f, 0.10f};
constexpr float kBrown[3]  = {0.55f, 0.36f, 0.18f};
constexpr float kBrowning  = 0.5f;    // the share of brown at full leaf loss
constexpr float kOrangeAt  = 1.6f;    // gold -> orange over hue [0, 1 / 1.6]
constexpr float kRedFrom   = 3.0f;    // orange -> red over hue [0.75, 1]: saturate(4 hue - 3)
// The green variation: colour *= 1 + t * kTint, t in [-1, 1] (yellower and lighter / bluer and darker).
constexpr float kTint[3]   = {0.08f, 0.05f, -0.05f};

constexpr float kHashK[3]  = {0.1031f, 0.1030f, 0.0973f};
constexpr float kHashAdd   = 33.33f;
constexpr float kGroveA[2] = {0.0123f, 0.0071f};
constexpr float kGroveB[2] = {-0.0057f, 0.0139f};
constexpr float kGroveBPhase = 2.1f;

enum class Kind { None, BillboardColour, BillboardDepth, Foliage };
const char* KindName(Kind k);

struct Match {
    bool        container = false;   // parsed as DXBC with an SHDR / SHEX chunk
    bool        program   = false;   // its token stream parsed
    bool        vertex    = false;   // program type 1 (else pixel)
    bool        candidate = false;   // looked like a tree program (worth a log line when refused)
    Kind        kind      = Kind::None;
    std::string why;                 // the refusal (kind == None)

    Dxbc::Container cont;
    Dxbc::Program   prog;
    uint32_t        temps = 0;       // dcl_temps

    // vertex
    int      xlateIdx   = -1;        // the first instruction reading cb3[rI + 3] (the block goes before it)
    int      xlateOp    = -1;        // which of its operands
    int      lastDclOut = -1;        // the last dcl_output* (the new dcl goes after it)
    int      depthMulIdx = -1;       // BillboardDepth: the instruction reading userPosition.z
    int      depthMulOp  = -1;
    // both
    uint32_t newReg   = 0;           // BillboardColour: the new register (o / v)
    uint32_t newSemIx = 0;           // its TEXCOORD index
    uint32_t snowReg  = 0;           // Foliage: the snow interpolant's register
    int      dclIdx   = -1;          // Foliage: its dcl_output / dcl_input_ps
    // pixel
    int      sampleIdx  = -1;        // the diffuse sample
    int      discardIdx = -1;        // the cutout's discard (the tint goes after it)
    int      seasonMadIdx = -1;      // BillboardColour: mad rA.w, -userPosition.z, move, rA.w
    int      goldIdx    = -1;        //   mad rB.xyz, |c|^2, l(gold), -c
    int      fallMulIdx = -1;        //   mul rA.w, move, userPosition.y
    int      fallMulOp  = -1;        //   which of its operands reads userPosition.y
    int      lastDclIn  = -1;        //   the last dcl_input_ps (the new dcl goes after it)
    uint32_t moveReg = 0, moveComp = 3;   // the move input (lightFogMove.w)
};

// Never fails loudly: a blob that is not ours is reported through the fields.
void Analyse(const uint8_t* blob, size_t size, Match* m);

// The transformed blob (fresh checksum, self-checked: it re-parses, its checksum verifies, the
// instruction count is the expected one). False when `m` did not match or the check failed.
bool Transform(const Match& m, std::vector<uint8_t>* out, const Curves& c = kCurves);

// ---- the CPU mirror (unit tests; phase 2's falling leaves use the same terms) ---------------------
struct TreeTerms {
    float h[3];   // hash33
    float grove;  // g in [-2, 2]
    float tint;   // t
};
TreeTerms TermsOf(float x, float y);
// c, l of a tree at `months` (0..12), as the vertex shader computes them.
void SeasonOf(const TreeTerms& t, float months, float* colour, float* loss, const Curves& c = kCurves);

}  // namespace NgRewrite
