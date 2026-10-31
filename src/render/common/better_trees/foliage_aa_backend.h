#pragma once
// ============================================================================
// src/render/common/better_trees/foliage_aa_backend.h -- ext.foliage_aa's render side, as the
// extension sees it (docs/extensions/FOLIAGE_AA.md, handins/FAA-CORE.md)
// ============================================================================
// NOT RETAIL. No D3D types: src/ext/better_trees/better_trees.cpp includes it (the AO precedent,
// ao/ao_backend.h). Everything D3D is src/render/dx11/better_trees/foliage_aa_stage.{h,cpp}.
// ============================================================================

#include "better_trees/receiver_rewrite.h"   // BbsRewrite::Params (pure, no D3D)
#include <cstdint>

namespace FaaBackend {

// The extension's one call (Ext::FoliageAA::Sync, main thread). `mode` 0 = off, 1 = smooth edges,
// 2 = smooth edges + distance fill with `fillK`, applied only where the sampled alpha >= `fillFloor`
// (0 = no floor). Idempotent: the same (mode, k, floor) twice is a no-op.
//   on   points the seam (VideoDX11::g_extFoliageAA) at the stage now; the transformed shaders and
//        the blend clones are (re)made for the new (mode, k) lazily, per program, on the binding
//        thread -- a mode / k change re-creates them (k is an immediate in the emitted code).
//   off  the binding thread's next pixel-program bind restores the retail blend state if a clone is
//        bound, releases every transformed shader and clone, and nulls the seam: from then on the
//        faithful path runs again (one null test per bind).
// BETTER-TREES: `dither` (null = none) is the billboard edge-noise set (BbsRewrite::Params noise /
// noiseScale, handins/BETTER-TREES.md); the stage applies edge_dither after the cutout on the
// billboard colour programs. A change re-creates the copies like a mode / k change.
// `naturalGrowth` (NATURAL_GROWTH.md): the billboard colour and foliage pixel shaders also take the per-tree
// season terms and green variation (natural_growth.h); only with mode > 0.
void Request(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither = nullptr,
             bool naturalGrowth = false);

#if BANISHED_TWEAK_OVERLAY
// The debug tweak overlay's stats line (debug flavour only): the applied generation's counters,
// read under the stage's mutex (any thread).
struct TweakStats {
    int   reqMode = 0, mode = 0;
    unsigned reqGen = 0, gen = 0;
    bool  msaa = false, coverage = false;
    float fillK = 0.0f, fillFloor = 0.0f;
    int   blobsSeen = 0, blobsTransformed = 0, programsTransformed = 0, createFailed = 0;
    int64_t xfBinds = 0, pixelBinds = 0;
};
TweakStats Stats();
#endif

}  // namespace FaaBackend
