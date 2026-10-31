#pragma once
#include "../VideoMetalData.h"
#include "better_trees/receiver_rewrite.h"   // BbsRewrite::Params (pure, no D3D)

namespace VideoMetal::TreePasses {

// The extension's requests (FaaBackend::Request / BbsBackend::Request, integration.mm). Main
// thread; the render thread picks them up in BeginFrame. `dither` null = the shipped set.
// `naturalGrowth` (docs/extensions/NATURAL_GROWTH.md): the pixel / vertex halves also run natural_growth.h
// first, on the billboard and foliage programs.
void SetFoliage(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither, bool naturalGrowth);
void SetBillboardShadow(bool on, bool naturalGrowth);

// Snapshot the last request once per command frame, outside draw loops.
void BeginFrame();

// Returns retail on a non-match or conversion failure. Variants are keyed on the retail
// shader's identity string (never its address) and live per device.
Shader* Select(Shader* retail, const std::vector<uint8_t>& bytes,
               id<MTLDevice> device, bool vertex);

// True for a cutout variant that writes coverage in o0.w: it needs alpha-to-coverage and a
// multisampled target, otherwise the caller must run the retail shader.
bool IsCoverage(const Shader* pixel);

// True for a variant that carries natural growth (NATURAL_GROWTH.md): a pixel one reads the interpolant
// only a natural-growth vertex variant writes.
bool IsNaturalGrowth(const Shader* shader);
// Match DX11: transformed cutout shaders never run with alpha blending, and a natural-growth pixel
// variant never runs beside a vertex shader that does not write its interpolant (retail's there).
class BlendGuard {
    Shader*& slot;
    Shader* saved;

public:
    BlendGuard(Shader*& pixel, bool blending, const Shader* vertex);
    ~BlendGuard() { slot = saved; }
};

}  // namespace VideoMetal::TreePasses
