#pragma once
// ============================================================================
// src/render/dx11/better_trees/foliage_aa_stage.h -- ext.foliage_aa's D3D side: the transformed
// cutout programs and the alpha-to-coverage blend clones (docs/extensions/FOLIAGE_AA.md §3 / §6,
// handins/FAA-CORE.md)
// ============================================================================
// NOT RETAIL.
//
// THE SEAM (DESIGN.md §3 rule 5): `VideoDX11::g_extFoliageAA`, null by default. With the row off it
// is null for the whole run and the four retail bodies that test it pay one load + branch each:
//   PixelProgramData::Bind            per pixel-program bind   (the stage binds the shader instead)
//   RenderStateData::Bind             per render-state bind    (after retail's three calls)
//   PixelProgramData::PlatformCreate  per program load         (make the copies when on)
//   ~PixelProgramData, ~RenderStateData                        (forget what the stage holds)
// The render-state test is needed because Video::Context::BindResources binds the pixel program
// (dirty 0x80) BEFORE the render state (0x20): a clone bound with the program would be replaced by
// the retail blend state a moment later, and a program change under an unchanged render state (the
// alpha-test props share OpaqueMaterial's state with every solid mesh) must restore the retail one.
//
// Set by FaaBackend::Request (the extension, main thread) when the row goes on; cleared by the
// stage itself on the binding thread after an off request, once the retail blend state is back and
// everything is released (a clear from the main thread could leave a clone bound under a retail
// program: an A2C draw with o0.w = 0 is an invisible draw).
//
// THE STATE the stage keeps (all under one mutex: programs are created on loader threads, bound on
// the main or the render thread):
//   per PixelProgramData   the transformed copy of each matched variant (null: retail), made from
//                          the user's own `_byteCode` by FaaRewrite (never written out)
//   per retail blend state its clone: AlphaToCoverage + BlendEnable, colour ONE / ZERO / ADD,
//                          alpha ZERO / ZERO / ADD, full write mask (FOLIAGE_AA §6.3: the coverage
//                          comes from o0.w, the target's alpha gets retail's 0). A retail state that
//                          blends has no clone: the draw keeps the retail program.
//   the bound pair         which retail program / blend state the engine asked for and whether the
//                          stage bound the transformed program / the clone in their place.
// A mode / k change, a device change (device lost) and an MSAA change (Video::Configuration::
// multisample, read per bind) reset all of it; the copies are re-made lazily per program.
// ============================================================================
#include <atomic>

struct ID3D11BlendState;
struct ID3D11DeviceContext;

namespace VideoDX11 {

class PixelProgramData;
class RenderStateData;

namespace FaaStage { class Stage; }

// THE SEAM. Null = faithful.
extern std::atomic<FaaStage::Stage*> g_extFoliageAA;

namespace FaaStage {

class Stage {
public:
    // PixelProgramData::Bind with the seam set: binds the retail or the transformed variant (and
    // the clone / the retail blend state to match). `variant` is retail's post-fallback index.
    void BindPixel(ID3D11DeviceContext* ctx, PixelProgramData* p, int variant);
    // RenderStateData::Bind with the seam set, after retail bound `retailBlend`.
    void AfterRenderState(ID3D11DeviceContext* ctx, ID3D11BlendState* retailBlend);
    // PixelProgramData::PlatformCreate succeeded: (re)make this program's copies now.
    void PixelCreated(PixelProgramData* p);
    void PixelDestroyed(PixelProgramData* p);
    void RenderStateDestroyed(ID3D11BlendState* retailBlend);
    // NATURAL_GROWTH.md: the vertex stage bound (or left) a natural-growth copy. A pixel copy that reads
    // its interpolant runs only beside one (g_ngVertexBound); the bound pair is re-applied.
    void VertexPairChanged(ID3D11DeviceContext* ctx);
};

}  // namespace FaaStage
}  // namespace VideoDX11
