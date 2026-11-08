#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)
// ============================================================================
// src/render/common/draw_batch/smoke_vs_backend.h -- ext.draw_batch's render side, as the extension
// sees it (docs/extensions/handins/DRAWBATCH.md, Idea B)
// ============================================================================
// NOT RETAIL. No D3D types: src/ext/draw_batch/draw_batch.cpp includes it (the billboard_shadow_backend.h
// precedent). Everything D3D is src/render/dx11/draw_batch/smoke_vs_stage.{h,cpp}.
// ============================================================================

namespace Video {
class VertexProgram;
}

namespace SmokeVsBackend {
BANISHED_RENDER_IMPL_BEGIN

// The extension's one call (Ext::DrawBatch::Sync, main thread). Idempotent.
//   on   points the seam (VideoDX11::g_extDrawBatchVs) at the stage now; the rewritten particle vertex
//        shaders (smoke_vs_rewrite.h) are made per program at load (PlatformCreate) or at the first bind.
//   off  the binding thread's next vertex-program bind releases every copy and nulls the seam: from
//        then on the faithful path runs again (one null test per bind).
void Request(bool on);

// True when every compiled variant of `vp` has its rewritten copy and every bind of `vp` binds that
// copy: a draw with this program may then write an emitter slot k at the vertex's +0x0C. False while
// the stage is off, for every program the rewriter does not match, and for a program whose copies
// are not made yet (they are made at its next bind). Any thread (the stage's mutex).
bool Rewritten(const Video::VertexProgram* vp);

BANISHED_RENDER_IMPL_END
}  // namespace SmokeVsBackend
