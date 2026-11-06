#pragma once
// ============================================================================
// src/render/common/decal_fog/decal_fog_rewrite.h -- fix.decal_fog_order's pixel-shader rewriter
// (docs/retail_bugs.md section 32)
// ============================================================================
// NOT RETAIL. Pure: no graphics API, no retail bytes (the unit tests build their own ps_4_0 blobs,
// tests/unit/ext_decal_fog_test.cpp). It reads the blob it is handed -- the user's installed pixel
// programs, at load -- and emits a corrected copy in memory (docs/extensions/DESIGN.md section 3, rule 7).
//
// THE BUG. A terrain decal's pixel program (Decal.ps and the programs that inherit it) lights the decal
// colour L, fogs it, F = lerp(L, fog, f), and only then tints it toward the constant underwater colour,
// out = lerp(F, (0.025, 0.123, 0.19), u). Below the water line u is 1 and the fog is replaced by the dark
// tint: the decal shows as a sharp dark band through fog. The model programs tint first, then fog.
//
// THE MATCH (`Analyse`): a ps_4_x program without flow control whose ONE underwater tint
//     E  add U.xyz, -F.xyzx, l(0.025, 0.123, 0.19, *)
// is preceded by the fog lerp and followed by the rest of the tint, four straight instructions:
//     C  mad F.xyz, f.cccc, D, L        F = L + f D, the fog lerp (D = fog - L)
//     E  add U.xyz, -F.xyz, l(uw)
//     G  mov_sat u.c, v#.c              the underwater amount, an interpolated input
//     H  mad o#.xyz, u.cccc, U.xyz, F.xyz   the colour OUTPUT
// where f's last writer is `min_sat f, *, cbN[R].w` (the fog amount, capped by the fog colour's w) and D's
// is a `mad D, -X, Y, cbN[R].xyz` on the same constant row (the fog colour minus the lit colour). After H
// no instruction may read what C, E or G wrote before writing it again (the rewrite leaves those values
// out), and no operand there is relatively indexed. A program whose tint writes a temp (the tint comes
// before the fog: the model programs and the correct terrain programs) does not match.
//
// THE REWRITE (`Transform`): C, E, G and H become six instructions in two new temps N, M:
//     add  rN.xyz, -L, l(uw)              uw - L
//     mov_sat rN.w, v#.c                  u (G's source)
//     mad  rN.xyz, rN.wwww, rN.xyzw, L    T = lerp(L, uw, u)
//     add  rM.xyz, D, L                   fog = L + D (the colour retail's fog lerp ends at)
//     add  rM.xyz, -rN.xyzw, rM.xyzw      fog - T
//     mad  o#.xyz, f.cccc, rM.xyzw, rN.xyzw   lerp(T, fog, f)
// The two lerps change places; every other token is the user's own, `dcl_temps` + 2. With u = 0 the
// output is retail's fog lerp; with f = 0 it is retail's tint.
// ============================================================================
#include "better_trees/dxbc.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace DecalFog {

// The underwater tint's immediate, as retail's compiler wrote it (bits of 0.025f, 0.123f, 0.19f).
constexpr uint32_t kTintBits[3] = {0x3CCCCCCDu, 0x3DFBE76Du, 0x3E428F5Cu};
constexpr int      kAddedInstrs = 2;

struct Match {
    bool        container = false;   // parsed as DXBC with an SHDR / SHEX chunk
    bool        program   = false;   // its token stream parsed (and re-emits identically)
    bool        pixel     = false;   // program type 0, shader model 4 or 5
    bool        tint      = false;   // has the underwater tint immediate (worth a log line when refused)
    bool        tintFirst = false;   // the tint writes a temp: it already comes before the fog (correct)
    bool        matched   = false;   // everything below is valid
    std::string why;                 // the refusal (matched == false)

    Dxbc::Container cont;
    Dxbc::Program   prog;
    uint32_t        temps    = 0;    // dcl_temps
    int             tempsIdx = -1;   // instrs[] index of dcl_temps
    int             fogIdx   = -1;   // instrs[] index of C (E, G, H follow it)
};

// Parse and decide. Never throws, never touches the blob.
void Analyse(const uint8_t* blob, size_t size, Match* m);

// The rewritten copy (fresh checksum), self-checked: it parses, its checksum verifies and it has
// exactly kAddedInstrs instructions more. False (and `out` untouched) when `m` did not match or the
// check failed.
bool Transform(const Match& m, std::vector<uint8_t>* out);

}  // namespace DecalFog
