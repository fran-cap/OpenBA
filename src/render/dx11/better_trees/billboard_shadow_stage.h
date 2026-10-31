#pragma once
// ============================================================================
// src/render/dx11/better_trees/billboard_shadow_stage.h -- ext.billboard_shadow's D3D side: the
// rewritten billboard vertex shaders (docs/extensions/TREE_SHADOW.md §4.2, handins/TS-CORE.md)
// ============================================================================
// NOT RETAIL.
//
// THE SEAM (DESIGN.md §3 rule 5): `VideoDX11::g_extBillboardShadow`, null by default. With the row
// off it is null for the whole run and the three retail bodies that test it pay one load + branch:
//   VertexProgramData::Bind            per vertex-program bind (the stage binds instead)
//   VertexProgramData::PlatformCreate  per program load (make the copies when on)
//   ~VertexProgramData                 (forget what the stage holds)
//
// The rewritten variant reads only constants retail already supplies to the retail variant
// (lc.nearPlane, lc.shadowProjection[0]; TS-FIX2), so the stage binds it whenever it exists.
//
// Set by BbsBackend::Request (the extension, main thread) when the row goes on; cleared by the stage
// itself on the binding thread at the first bind after an off request, once everything is released.
// All state is under one mutex: programs are created on loader threads, bound on the main or the
// render thread. A device change (device lost) releases every copy; they are re-made lazily.
// ============================================================================
#include <atomic>

struct ID3D11DeviceContext;

namespace VideoDX11 {

class VertexProgramData;

namespace BbsStage { class Stage; }

// THE SEAM. Null = faithful.
extern std::atomic<BbsStage::Stage*> g_extBillboardShadow;

// NATURAL_GROWTH.md: the bound vertex shader is a natural-growth copy (it writes the per-tree
// interpolant). Written by BindVertex, read by the foliage_aa stage's pixel binds; both run on the one
// immediate context's binding thread.
extern std::atomic<bool> g_ngVertexBound;

namespace BbsStage {

class Stage {
public:
    // VertexProgramData::Bind with the seam set. `variant` is retail's post-fallback index.
    void BindVertex(ID3D11DeviceContext* ctx, VertexProgramData* p, int variant);
    // VertexProgramData::PlatformCreate succeeded: (re)make this program's copies now.
    void VertexCreated(VertexProgramData* p);
    void VertexDestroyed(VertexProgramData* p);
};

}  // namespace BbsStage
}  // namespace VideoDX11
