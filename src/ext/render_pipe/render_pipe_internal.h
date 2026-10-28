// ============================================================================
// src/ext/render_pipe/render_pipe_internal.h -- RENDERPIPE: shared by the TUs
// of src/ext/render_pipe only
// ============================================================================
// NOT RETAIL. docs/extensions/handins/RENDERPIPE0.md.
#pragma once

#include <cstdint>

#include "graphics/Scene.h"   // rtgfxbig::RtGraphics_Renderer_DrawItemSort, Graphics::Scene

namespace Graphics { class Renderer; class Camera; class SunLight; struct ExtAoFrame; }
namespace Video { class Context; class RenderBuffer; }

namespace Ext {
namespace RenderPipe {

// passes.cpp: Renderer::Draw's two loops, copied and cut where pass 1 ends.
void Pass1(Graphics::Renderer& r, const Video::RenderBuffer* defaultRenderBuffer, float alpha,
           int32_t frameIndex, const Graphics::Scene& sceneRef, const Graphics::Camera& cameraRef);
// `ao`: ext.ambient_occlusion's frame record (the packet's), null = no AO pass.
void Pass2(Graphics::Renderer& r, Video::Context& context, const Graphics::ExtAoFrame* ao);

// shadow.cpp (COMPPROF=4): a hash of the renderer's lists (pads masked) and of
// the state their items reference that pass 2 reads -- each item's Geometry
// header (bounds, opacity pair, transform pair), a MeshInstance's node pairs,
// root offset, colour and instanced pose, a ParticleInstance's live records --
// and the SunLight's eleven Set dwords. `ownPointers` false leaves out the pointers the
// snapshot replaces (the Geometry, its particle pool, its pose array), so a frame and its
// frozen copies hash alike (P13_OVERLAP.md).
uint64_t HashFrame(const Graphics::Renderer& r, const Graphics::SunLight* sun, bool ownPointers = true);

}  // namespace RenderPipe
}  // namespace Ext
