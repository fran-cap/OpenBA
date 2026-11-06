#pragma once
// ============================================================================
// src/render/common/better_trees/edge_dither.h -- ext.better_trees' billboard PIXEL-shader rewrite:
// per-texel noise on the shadow compare depth (docs/extensions/handins/BETTER-TREES.md)
// ============================================================================
// NOT RETAIL. Pure: no D3D, no retail bytes (the unit tests build synthetic ps_4_0 blobs). It reads
// the blob it is handed -- the user's installed pixel shaders, at run time, AFTER ext.foliage_aa's
// cutout rewrite when that applied -- and emits a copy in memory.
//
// THE PROBLEM IT SOLVES. receiver_rewrite.h moves each card's self-shadow cut per vertex, but a
// vertex-side term is interpolated across the card and can only cut the caster's constant depth
// along a line (TS-HASH): on a large card a long straight edge always remains. A per-TEXEL term
// lives here: the compare depth the PCF taps use gets a world-stable noise, so the cut becomes a
// leaf-scale jagged edge. The noise is a hash of the diffuse texcoord (attached to the card, so the
// pattern does not swim with the camera) quantised to cells of 1 / noiseScale uv; its amplitude is
// the card's half extent in depth units, handed over by the vertex rewrite in the shadow
// interpolant's unused w (NEGATIVE; retail's w is +1, so a retail vertex shader gives amplitude 0).
//
// THE MATCH (`Analyse`): a ps_4_x program, no flow control (discards allowed: the cutout), `ret` once
// and last, whose `sample_c` / `sample_c_lz` instructions take their compare value straight from one
// input register S (the interpolated shadow projection; retail: v3.z, nine taps), and whose first
// plain `sample` reads its coordinates from another input register U (the diffuse texcoord; retail:
// v1.xy). S's `dcl_input_ps` must exist (its mask grows to xyzw) and the ISGN chunk must list S.
//
// THE EMIT (`Transform`), at THE PLACEMENT below, in two new temps T (= S's copy) and N:
//     mov T, vS
//     mul N.xy, vU.xy, noiseScale ; round_ni N.xy ; dp2 N.x, N.xy, (12.9898, 78.233)
//     sincos N.x, null, N.x ; mul N.x, N.x, 43758.5453 ; frc N.x ; add N.x, N.x, -0.5      h - 0.5
//     max N.y, -vS.w, 0 ; lt N.z, N.y, kExtentLimit ; and N.y, N.y, N.z                       the extent, or 0
//     mul N.x, N.x, N.y ; mad T.c, N.x, noise, T.c                                             c = the compare component
// then every read of vS in the program is pointed at T. 13 instructions, `dcl_temps` + 2, the
// dcl_input_ps of S and its ISGN read mask widened to xyzw; every other token is the user's own.
// THE PLACEMENT (issue #253). In a cutout program (the only ones the stages dither) the block goes right
// after the last discard -- retail's discard_nz, the fill floor's discard_z, or the coverage path's
// discard_z of the sharpened alpha -- so a fragment the alpha test drops (about 96% of a leaf card's
// fragments at town zoom on Windows) never runs it. The block reads only vS and vU and writes only T and N,
// which nothing before it reads, so moving it changes no value: the same pixels. Every read of S is
// pointed at T, so the block must come before the first of them: in the installed leaf programs all nine
// shadow taps follow the discard (the block lands after it); a program that read S before its last discard
// would get the block right before that read instead. A program with no discard keeps the block before its
// first instruction.
// The early-out is the GPU's: D3D's discard keeps a dropped lane's quad derivatives defined, and dxbc-spirv
// lowers it to OpDemoteToHelperInvocation (third_party/dxbc-spirv/dxbc/dxbc_converter.cpp handleDiscard; the
// Vulkan 1.1 path's DowngradeSpirv turns that into OpKill), so a dropped lane may still run on as a helper;
// what is skipped is a quad (or wave) whose every lane was dropped -- most of a leaf card's empty area. A
// terminate (OpTerminateInvocation) per program would be wrong here: after the shadow taps (sample_c_lz,
// explicit lod) every leaf program takes the AO map with a plain `sample` of v1.zw, an implicit lod whose
// derivatives need the dropped lanes as helpers.
//
// THREE GUARDS (the first look arm dithered the terrain and the buildings black): the stage applies this
// to the cutout programs only (foliage_aa_stage.cpp Build); the ISGN element of S must say the vertex
// stage WRITES w (else the read would be an unlinked component); and the amplitude is taken only when
// -w is in (0, kExtentLimit): the extent is r x the depth per world unit (0.004..0.02 at the retail fit),
// so a w that carries anything else (retail's +1, a packed fog or season term) gives 0.
// ============================================================================
#include "better_trees/receiver_rewrite.h"   // BbsRewrite::Params
#include "better_trees/dxbc.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BbsDither {

struct Match {
    bool        container = false;   // parsed as DXBC with an SHDR / SHEX chunk
    bool        program   = false;   // its token stream parsed
    bool        pixel     = false;   // program type 0
    bool        candidate = false;   // has a sample_c* instruction (worth a log line when refused)
    bool        matched   = false;
    std::string why;

    Dxbc::Container cont;
    Dxbc::Program   prog;
    uint32_t        temps      = 0;
    uint32_t        shadowIn   = 0;   // S
    uint32_t        shadowComp = 0;   // c: the compare component of S
    uint32_t        uvIn       = 0;   // U
    uint32_t        uvSwizzle  = 0;   // U's xy as the sample reads them (an 8-bit swizzle, xy used)
    int             dclIdx     = -1;  // the dcl_input_ps of S
    int             firstInstr = -1;       // the first instruction
    int             lastDiscard = -1;      // the last discard (the cutout's, the fill floor's, the coverage's)
    int             firstShadowRead = -1;  // the first instruction that reads S
    int             insertAt = -1;         // where the block goes (THE PLACEMENT)
};

void Analyse(const uint8_t* blob, size_t size, Match* m);

// The transformed blob (fresh checksum). False when `m` did not match, the ISGN element of S is
// missing, or the copy fails its own re-parse (then nothing is used). Uses p.noise / p.noiseScale.
bool Transform(const Match& m, std::vector<uint8_t>* out, const BbsRewrite::Params& p);

// THE PLACEMENT the match chose, for the stages' log lines: "after the discard", "before the first read of S"
// (a read of S precedes the last discard) or "first instruction" (no discard).
const char* PlacementName(const Match& m);

constexpr float kHashU = 12.9898f, kHashV = 78.233f, kHashMul = 43758.5453f;
constexpr float kExtentLimit = 0.1f;   // -w past this is not an extent

}  // namespace BbsDither
