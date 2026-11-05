#pragma once
// ============================================================================
// src/render/common/better_trees/cutout_rewrite.h -- the cutout pattern match and the in-memory rewrite
// of an alpha-tested ps_4_x blob (ext.foliage_aa, docs/extensions/FOLIAGE_AA.md §3 as amended by §6).
// ============================================================================
// NOT RETAIL. Pure: no D3D, no retail bytes. It transforms the blob it is handed (the user's
// installed shaders, at run time) and never writes one out.
//
// THE GRAMMAR (FOLIAGE_AA §6.1; FAA-SPIKE S2 found all 24 matched blobs in this shape):
//   dcl_temps   n
//   sample      rA.xyzw, uv, tK, sK          opcode 69 (plain `sample`), four operands
//   [mad        rA.c, ..., ...]              optional: the season leaf-loss term (one mad at most)
//   add         rT.c, rA.c, l(-cut)          either operand order; rT.c = rA.c or a separate temp
//   lt          rT.c, rT.c, l(0)             immediately after the add
//   discard_nz  rT.c                         immediately after the lt; the only discard
//   ...                                      no flow control anywhere
//   mov         o0.w, l(0)                   exactly once ("colour" programs; the depth ones lack it)
//   ret                                      once, last
// Anything else is NOT a match and stays byte-identical (retail look).
//
// THE EMIT (Transform). Our temps are numbered from n up, never retail's:
//   P = n (always), and with the fill N = n+1, M = n+2 (scratch for the lod).
//   fill (Options::fillK > 0, FOLIAGE_AA §6.5): before the add,
//       P.x = lod (aniso: 0.5 * log2(max(Pmax^2 / 256, Pmin^2)) of the uv derivatives scaled by
//             resinfo's mip-0 size), P.x = rA.c * (1 + max(0, lod) * k); the retail add reads P.x.
//       with the floor (Options::fillFloor > 0, FAA-TUNE): the factor is 1 where rA.c < floor
//             (`ge M.x, rA.c, l(floor)` + `movc P.x, M.x, P.x, l(1)` after the mad), and the
//             floor gates the result too: coverage `and P.x, P.x, M.x` after the sharpen (the
//             ramp alone covers an unfilled low-alpha texel where fwidth is wide), MSAA off
//             `discard_z M.x` after the retail discard. A low-alpha texel is never covered.
//   no fill + coverage: `mov P.x, rA.c` before the add (in four programs the add overwrites rA.c).
//   coverage (Options::coverage, the MSAA-on path): the discard is dropped and
//       P.x = saturate((P.x - cut) / max(fwidth(P.x), 1e-4) + 0.5)    (Golus' sharpened alpha)
//       replaces the `mov o0.w, l(0)` by `mov o0.w, P.x` (the coverage for alpha-to-coverage);
//       `discard_z P.x` right after (issue #253): a fragment of coverage 0 writes no sample anyway,
//       so it is dropped instead of shaded (the same image).
//   no coverage (MSAA off, §6.6): the discard stays, o0.w stays 0; only the fill applies.
// Then the container fix-ups (instruction lengths, dcl_temps, chunk sizes, the checksum) and a
// self-check: the result must re-parse and round-trip byte-identical with a valid checksum.
// ============================================================================
#include "better_trees/dxbc.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace FaaRewrite {

struct Match {
    // the container / program read, and the identity check (parse -> re-emit == input)
    bool        container = false, program = false, identity = false, checksum = false;
    std::string err;
    // the scan
    int         discards = 0, flowControl = 0, rets = 0, o0wCount = 0;
    // the verdict: `colour` = the whole grammar above; `why` says why not (or "colour")
    bool        colour = false;
    std::string why;
    // instruction indices into prog.instrs (-1 = absent)
    int         iSample = -1, iMad = -1, iAdd = -1, iLt = -1, iDiscard = -1, iO0w = -1, iTemps = -1,
                iRet = -1;
    float         cut   = 0.0f;   // the add's immediate, negated (0.5 in every retail colour program)
    uint32_t      temps = 0;      // dcl_temps
    Dxbc::Operand alpha;          // the add's alpha source (rA.c)
    uint32_t      alphaComp = 0;  // the component it reads
    Dxbc::Program   prog;
    Dxbc::Container cont;
};

// Never fails loudly: a blob that is not a DXBC container, not ps_4_x or not the grammar is
// reported through the fields (colour false, `why` / `err` set).
void Analyse(const uint8_t* blob, size_t size, Match* m);

struct Options {
    bool  coverage  = true;   // drop the discard, write the sharpened alpha to o0.w (A2C)
    float fillK     = 0.0f;   // part 2's k; 0 = no distance fill
    float fillFloor = 0.0f;   // the fill applies only where the sampled alpha >= this; 0 = no floor
};

// The row's mode (1 smooth edges, 2 + distance fill) and the device's MSAA -> what to emit
// (FOLIAGE_AA §6.6): with MSAA off A2C does nothing, so the discard stays and mode 2 keeps only the
// fill (a thicker cutoff); mode 1 without MSAA emits nothing (Transform returns false: retail).
inline Options OptionsFor(int mode, float fillK, bool msaa, float fillFloor = 0.0f) {
    Options o;
    o.coverage  = msaa && mode >= 1;
    o.fillK     = mode == 2 && fillK > 0.0f ? fillK : 0.0f;
    o.fillFloor = o.fillK > 0.0f && fillFloor > 0.0f ? fillFloor : 0.0f;
    return o;
}

// True when there is something to emit (a colour match, and coverage or a fill) and the emit passed
// its own self-check; `out` is then the transformed container.
bool Transform(const Match& m, const Options& o, std::vector<uint8_t>* out);

}  // namespace FaaRewrite
