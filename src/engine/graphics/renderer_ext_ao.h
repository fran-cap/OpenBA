// ============================================================================
// src/engine/graphics/renderer_ext_ao.h -- the engine end of the ambient-occlusion seam
// ============================================================================
// NOT RETAIL. ext.ambient_occlusion (class A, docs/extensions/AO.md,
// docs/extensions/handins/AOCORE.md).
//
// `Graphics::Renderer::_extAo` (Camera.h) is a null-by-default static DATA pointer
// (DESIGN.md §3 hook rule 5): null = retail. The game's draw points it at this
// frame's record for the span of its own Renderer::Draw call and nulls it again
// right after (GameUpdateLoop::Draw step 7); nothing else ever sets it, so the
// main-menu backdrop and the minimap paint never see it. Renderer::Draw pass 2
// (wave27_lane_gfxbig.cpp) reads it ONCE per frame and, when it is set, calls
// ExtAoAfterOpaque for the MAIN scene entry (the default render buffer, shader 0;
// the water reflection is shader 2) between the Opaque (k = 0) and Blended
// (k = 1) lists. The render-thread copy of that loop (src/ext/render_pipe/
// passes.cpp) carries the same record in its packet instead.
//
// The record is plain data (no engine type, no include) so the render-pipe
// packet (packet.h, engine-free) can hold it by value. src/engine does not
// include src/ext: the body of ExtAoAfterOpaque lives in the D3D backend,
// src/render/dx11/ao/ao_pass.cpp (the only TUs that see the D3D headers).
// ============================================================================
#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)

#include <cstdint>

namespace Video { class Context; }

namespace Graphics {

struct RendererSceneEntry;

// One frame's AO parameters, decided on the main thread by the extension
// (src/ext/ambient_occlusion/ambient_occlusion.cpp) from Ext::AmbientOcclusion::Current() and the camera.
struct ExtAoFrame {
    int32_t qualityLevel;       // ASSAO QualityLevel: -1 Lowest, 0 Low, 1 Medium, 2 High
    int32_t blurPassCount;      // ASSAO BlurPassCount
    float   radius;             // world units (the zoom curve)
    float   shadowMultiplier;   // ASSAO ShadowMultiplier: the strength x the opacity (0..1)
    float   shadowPower;
    float   horizonAngleThreshold;
    float   sharpness;
    float   detailShadowStrength;
    float   fadeOutFrom;        // view depth, world units; FadeOutTo < far (512) skips the sky
    float   fadeOutTo;
};

// The backend's pass (src/render/dx11/ao/ao_pass.cpp): resolve the main depth,
// run the AO stage, multiply its result into the bound (main, MSAA) colour.
// Leaves every D3D binding as it found it.
BANISHED_RENDER_IMPL_BEGIN
void ExtAoAfterOpaque(Video::Context& context, const ExtAoFrame& frame,
                      const RendererSceneEntry& entry);
BANISHED_RENDER_IMPL_END

}  // namespace Graphics
