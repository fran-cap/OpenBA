#pragma once
// ============================================================================
// src/render/common/better_trees/receiver_rewrite.h -- ext.billboard_shadow's vertex-shader
// rewriter (docs/extensions/TREE_SHADOW.md, handins/TS-CORE.md "TS-FIX2")
// ============================================================================
// NOT RETAIL. Pure: no D3D, no retail bytes (the unit tests build synthetic vs_4_0 blobs). It reads
// the blob it is handed -- the user's installed vertex shaders, at run time -- and emits a copy in
// memory.
//
// THE PROBLEM IT SOLVES. The billboard canopy's colour vertex shader builds each leaf card facing the
// PLAYER camera (from `gc.worldToCamera`, cb0 rows 4..7) and looks the sun shadow up at that drawn
// position P: `shadowProjection = lc.shadowProjection[0] * P` (cb2 rows 10..13). The shadow pass drew
// the same card facing the SUN, so P's depth along the light is unrelated to where the card's own
// caster is: half of every card shadows itself and the canopy shows a straight terminator that turns
// with the camera.
//
// THE MATCH (`Analyse`): a vs_4_x program with no flow control (`ret` once and last), no indexable
// temps, whose SHADOW OUTPUT (the output written from cb2[10..13]) depends on cb0[4..7] through one
// position temp P that the shadow-matrix instructions read with replicated swizzles, where the first
// camera-dependent write of P's xyz is `add P, C, offset` with exactly one camera-independent temp
// source C: the card CENTRE (retail: the instance-transformed vertex position, shared by the card's
// four corners, before the sway and corner offsets are added).
//
// THE LOOKUP (TS-DAPPLE, `Transform`): keep P's light-space position (the texel retail samples) and move
// the depth along the light by a per-card amount:
//     d = C - P,  r = |d|,  h = frc(dot(V, K) + dot(T, Kt)) (see THE HASH),
//     e = margin + jitter (2 h - 1),
//     P' = P + (basis dot(d, L) - e r) L,      L = lc.nearPlane.xyz (the sun direction)
// With basis 0, margin 0, jitter 0 the lookup is retail's (P' = P). Retail's look inside a crown is
// the card's own sun-facing caster cutting the drawn card in half; what reads wrong is only that every
// card's cut lies on the same plane, so the cuts line up into one straight edge that turns with the
// camera. A per-card depth offset (jitter, in units of the card's half extent r) moves each cut to a
// different place across its card: the halves stay, the dapple stays, the straight edge dissolves.
// margin shifts every cut toward the light (less self-shadow); basis 1 measures from the card
// centre's depth instead of P's (basis 1, margin 1, jitter 0 was TS-FIX2: never self-shadowed, flat).
// The light-space projection is orthographic (cb2[10..13]'s w row is 0 0 0 1), so moving along L
// changes depth linearly and leaves the texel alone. Emitted as `mov rS.xyz, C` before the centre
// add and a 10-instruction block before the first shadow-matrix instruction; the three shadow-matrix
// reads of P are pointed at P'. `dcl_temps` + 3; every other token is the user's own.
//
// NOTE (TS-HASH, handins/TS-CORE.md): the card's four corners carry four different base positions
// (a small rectangle, not one shared centre), so the hash differs per VERTEX; the cut inside a
// card is the bilinear blend of four corner offsets. A per-texel term needs the pixel shader:
//
// THE HASH (TS-GROW). It must not move while a tree grows. C is the instance-transformed position, and
// retail scales it by the instance's growth (tc.transforms[i] rows 0..2 .w, eased every frame by
// GrowthComponent::PostUpdate, also while the game is paused at speed 0): a 0.1% size step moves C
// by ~0.005 units, which re-rolls frc(C . K) entirely, so every growing tree's cuts jumped frame to
// frame (a shimmer on the crowns, worst after a spell at high speed). The hash therefore reads two
// inputs the growth never touches:
//     V = the vertex input C is loaded from (`itof` / `mov rC.xyz, v#`, the model-space position;
//         after an itof scaled by kInputUnit, retail's own `mul ..., 0.00195312`), and
//     T = the instance translation tc.transforms[i] row 3 (cb3[rI + 3]), read just before retail's
//         first read of it while its index register is live; a program without that read hashes V
//         alone (T = 0).
// Emitted as `itof|mov rV.xyz, v#` + `dp3 rV.x, rV.xyz, K'` after the input load and
// `dp3 rV.w, cb3[rI + 3].xyz, Kt` before the translation read (or `mov rV.w, l(0)`); the lookup adds
// them. A program whose C does not come from a vertex input is refused (kept retail).
//
// THE EXTENT (BETTER-TREES, edge_dither.h): after the shadow output's last write, `o<shadow>.w =
// -(r * |lc.shadowProjection[0] row 2 .xyz|)`, the card's half extent in depth units, NEGATIVE so a
// retail vertex shader (w = +1, orthographic) gives the pixel-side dither an amplitude of 0. The
// pixel shader reads nothing from that w in retail (dcl_input_ps v3.xyz, every variant); the dither
// rewrite declares it. Four more instructions, in the same temps.
// ============================================================================
#include "better_trees/dxbc.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BbsRewrite {

constexpr uint32_t kGlobalCb      = 0;    // gc
constexpr uint32_t kWorldToCamera = 4;    // gc.worldToCamera rows 4..7
constexpr uint32_t kLightCb       = 2;    // lc
constexpr uint32_t kNearPlane     = 3;    // lc.nearPlane: xyz = the sun direction (SunLight::DrawLight)
constexpr uint32_t kShadowProj0   = 10;   // lc.shadowProjection[0] rows 10..13

// The lookup's three immediates (the debug overlay's live sliders, TS-TWEAK; the shipped values).
struct Params {
    float basis;    // 0 = from P's depth (retail's), 1 = from the card centre's depth
    float margin;   // the mean offset toward the light, in half extents r
    float jitter;   // the per-vertex spread, in half extents r (the cut lands anywhere within +-jitter r)
    // BETTER-TREES (edge_dither.h): the per-texel noise on the compare depth, in the billboard PIXEL
    // shader, in half extents r (0 = none; the VS passes r through the shadow output's unused w).
    float noise;
    float noiseScale;   // the noise's cell size: cells per atlas uv unit (64 = 8 texels of a 512 atlas)
    bool operator==(const Params& o) const {
        return basis == o.basis && margin == o.margin && jitter == o.jitter && noise == o.noise &&
               noiseScale == o.noiseScale;
    }
    bool operator!=(const Params& o) const { return !(*this == o); }
};
constexpr Params kDefaultParams{0.0f, -0.05f, 0.4f, 0.05f, 32.0f};   // the user's ruling 2026-10-17: noise 0.05, scale 32
constexpr Params kRetailParams{0.0f, 0.0f, 0.0f, 0.0f, 64.0f};   // the lookup is retail's exactly
constexpr float  kHashX = 12.9898f, kHashY = 78.233f, kHashZ = 37.719f;   // K above (on V)
constexpr float  kTreeX = 0.1031f, kTreeY = 0.1030f, kTreeZ = 0.0973f;    // Kt above (on T, world units)
constexpr float  kInputUnit = 1.0f / 512.0f;   // an integer position input's unit (retail: itof, then * 0.00195312)

struct Match {
    bool        container = false;   // parsed as DXBC with an SHDR / SHEX chunk
    bool        program   = false;   // its token stream parsed
    bool        vertex    = false;   // program type 1
    bool        candidate = false;   // reads cb2[10..13] AND cb0[4..7] somewhere (worth a log line when refused)
    bool        matched   = false;   // everything below is valid
    std::string why;                 // the refusal (matched == false)

    Dxbc::Container cont;
    Dxbc::Program   prog;
    uint32_t        temps         = 0;   // dcl_temps
    uint32_t        cb2Rows       = 0;   // dcl_constantbuffer cb2[n]
    uint32_t        shadowOut     = 0;   // the output register (o#)
    uint32_t        posReg        = 0;   // P: the temp the shadow-matrix instructions read
    int             insertAt      = -1;  // instruction index of the first shadow-matrix instruction
    int             centerIdx     = -1;  // instruction index of `add P, C, offset`
    int             centerOperand = 0;   // which of its operands is C (1 or 2)
    int             inputIdx      = -1;  // THE HASH: instruction index of `itof|mov rC.xyz, v#` (C's input load)
    int             xlateIdx      = -1;  // THE HASH: the first read of cb3[rI + 3] before centerIdx (-1: none)
    int             xlateOperand  = 0;   // which of its operands that is
    int             extentIdx     = -1;  // instruction index of the shadow output's last write (the extent goes after it)
};

// Parse and decide. Never throws, never touches the blob.
void Analyse(const uint8_t* blob, size_t size, Match* m);

// The transformed blob (fresh checksum). False when `m` did not match or the emitted copy fails its
// own re-parse (then nothing is used).
bool Transform(const Match& m, std::vector<uint8_t>* out);   // with kDefaultParams
bool TransformWithParams(const Match& m, std::vector<uint8_t>* out, const Params& p);


}  // namespace BbsRewrite
