#pragma once
// ============================================================================
// src/render/common/better_trees/billboard_shadow_backend.h -- ext.billboard_shadow's render side,
// as the extension sees it (docs/extensions/TREE_SHADOW.md, handins/TS-CORE.md)
// ============================================================================
// NOT RETAIL. No D3D types: src/ext/better_trees/better_trees.cpp includes it (the
// foliage_aa_backend.h precedent). Everything D3D is src/render/dx11/better_trees/billboard_shadow_stage.{h,cpp}.
// ============================================================================

#include "better_trees/receiver_rewrite.h"   // BbsRewrite::Params (pure, no D3D)

namespace BbsBackend {

// The extension's one call (Ext::BillboardShadow::Sync, main thread). Idempotent.
//   on   points the seam (VideoDX11::g_extBillboardShadow) at the stage now; the rewritten vertex
//        shaders are made per program at load (PlatformCreate) or lazily at the first bind.
//   off  the binding thread's next vertex-program bind releases every rewritten shader and nulls
//        the seam: from then on the faithful path runs again (one null test per bind).
// `naturalGrowth` (NATURAL_GROWTH.md): the billboard and foliage vertex shaders also carry the per-tree
// season terms (natural_growth.h); only with `on`.
void Request(bool on, bool naturalGrowth = false);

#if BANISHED_TWEAK_OVERLAY
// TS-TWEAK (debug flavour only): the rewrite's immediates (basis / margin / jitter) from the debug
// tweak overlay. A new set releases the copies at the next vertex bind (the device-change reset
// path); they are re-made with it, once per program, never per frame. AppliedParams: the set the
// current copies were made with.
void RequestParams(const BbsRewrite::Params& p);
BbsRewrite::Params AppliedParams();
#endif

}  // namespace BbsBackend
