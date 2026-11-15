#pragma once
// ============================================================================
// src/ext/render_culling/render_culling_rules.h -- NOT RETAIL. The pure decisions of ext.render_culling and
// ext.anim_batch (render_culling.h), header-only and standard C++ so the host unit tests
// (tests/unit/ext_render_culling_test.cpp) check them on their own.
// docs/extensions/handins/PERFXEXT.md
// ============================================================================
#include <cstdint>

namespace Ext {
namespace RenderCulling {

// ext.render_culling's knobs (all on: the set the perf build shipped default-on).
struct Knobs {
    bool animCull, geomXfLastOnly, geomCull, iconLastOnly, iconCull, pickCull, particleCull;
};
constexpr Knobs kRenderCulling = { true, true, true, true, true, true, true };
// World-unit margin around the frustum (~12 frames of pan); particles get +10 for rising smoke.
constexpr float kCullMargin = 6.0f;
constexpr float kParticleMargin = kCullMargin + 10.0f;

// What the seams read. Written only by BeginFrame (start of GameUpdateLoop::Update, main
// thread, outside the sim iterations): all false while both extensions are off.
struct Flags {
    bool any;              // render_culling || anim_batch
    bool animCull, geomXfLastOnly, geomCull, iconLastOnly, iconCull, pickCull, particleCull;
    bool animBatch;        // ext.anim_batch
};

// The flags for an extension state: every render_culling knob follows the one toggle.
inline Flags FlagsFor(bool renderCulling, bool animBatch) {
    const Knobs& k = kRenderCulling;
    Flags f{};
    f.animCull       = renderCulling && k.animCull;
    f.geomXfLastOnly = renderCulling && k.geomXfLastOnly;
    f.geomCull       = renderCulling && k.geomCull && k.animCull;   // flushed with the culled models
    f.iconLastOnly   = renderCulling && k.iconLastOnly;
    f.iconCull       = renderCulling && k.iconCull && k.animCull;   // ModelCulled needs the frustum
    f.pickCull       = renderCulling && k.pickCull;
    f.particleCull   = renderCulling && k.particleCull;
    f.animBatch      = animBatch;
    f.any            = renderCulling || animBatch;
    return f;
}

// The draw group whose cull camera the sim's off-screen tests use: the main view's, shader 0
// (Graphics::MaterialPass::Normal; render_culling.cpp holds the static_assert). The terrain's collect
// pass, which hands the frustum over, also runs for the shadow map's group (1), the reflection's (2)
// and a spot light's (3), each with its own cull camera.
constexpr int32_t kMainViewShader = 0;
inline bool IsMainViewEntry(int32_t groupShader) { return groupShader == kMainViewShader; }

// True when the PERFXTBL side table has a user (batch counts or particle verdicts).
inline bool SideTableNeeded(const Flags& f) { return f.animBatch || f.particleCull; }

// ---- particles (PERFXEXT §3.1) ---------------------------------------------------------
// ParticleComponent::Tick (0x140052b30) per live emitter (sceneId >= 0 and an instance):
// with fadeRate != 0 it integrates opacity += dt * fadeRate and calls StopEmitter when the
// result is <= 0 (both of its `< 0` / `<= 0` arms; above the UV scale it clamps instead);
// with fadeRate == 0 it calls StopEmitter when opacity <= 0. StopEmitter writes what the
// sim and the save read (sceneId = -1, active[i] = false, flag bit 2). So an emitter is
// QUIET -- its tick can only touch presentation (node-follow transform, opacity unchanged,
// the ParticleInstance simulation) -- exactly when it is not fading and visible.
inline bool EmitterQuiet(float fadeRate, float opacity) {
    return fadeRate == 0.0f && opacity > 0.0f;
}
// Retail's StopEmitter decision for one live emitter this tick (the COMPPROF=4 shadow
// recomputes it on every frozen tick: it must be false).
inline bool RetailStopsEmitter(float fadeRate, float opacity, float dtSeconds, float uvScale) {
    if (fadeRate != 0.0f) {
        const float op = opacity + dtSeconds * fadeRate;
        if (op > uvScale) return false;
        return op <= 0.0f;
    }
    return opacity <= 0.0f;
}

// ---- animation clock (ext.anim_batch's shadow) ------------------------------------------
// MeshInstance::IsAnimating (0x1800ff7d0) on layer 0: a looping clip always animates, a
// non-looping one while its time is below the duration. `extraTicks` = ticks the batched
// model has not applied yet: the answer at the clock retail's per-tick model would have.
inline bool IsAnimatingAt(bool hasLayer, bool looping, int64_t time, int64_t duration,
                          int64_t extraTicks) {
    if (!hasLayer) return false;
    if (looping) return true;
    return time + extraTicks < duration;
}

}  // namespace RenderCulling
}  // namespace Ext
