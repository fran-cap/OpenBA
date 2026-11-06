// ============================================================================
// src/ext/ambient_occlusion/ao_math.h -- the pure parts of ext.ambient_occlusion
// ============================================================================
// NOT RETAIL. docs/extensions/AO.md §3 ("Zoom", "Tiers"), §6.2; handins/AOCORE.md.
// Standard C++ only: tests/unit/ext_ambient_occlusion_test.cpp links ao_math.cpp on its own, and
// the D3D backend (src/render/dx11/ao/ao_pass.cpp) uses AssaoProjection.
// ============================================================================
#pragma once

#include <cstdint>

namespace Graphics { struct ExtAoFrame; }
namespace Ext { namespace AmbientOcclusion { struct Params; } }

namespace Ext {
namespace AmbientOcclusion {
namespace AoMath {

// ---------------------------------------------------------------------------
// Tiers (AO.md §3): the menu's quality 1 / 2 / 3 -> ASSAO Low / Medium / High,
// and 4 -> ASSAO Lowest (-1: half density, checkerboard; the phone tier, issue
// #253). kLowIsLowest would also send quality 1 to Lowest (the AOCORE fallback).
// ---------------------------------------------------------------------------
constexpr int kAssaoLowest = -1;
constexpr int kAssaoLow    = 0;
constexpr int kAssaoMedium = 1;
constexpr int kAssaoHigh   = 2;
constexpr bool kLowIsLowest = false;   // AOCORE stress pair: handins/AOCORE.md §5

struct Tier {
    int32_t qualityLevel;    // ASSAO QualityLevel
    int32_t blurPassCount;   // ASSAO BlurPassCount (Low / Lowest: ASSAO runs one non-smart blur)
};
// False for quality 0 (off) and anything outside 1..4 (`out` untouched).
bool TierFor(int quality, bool lowIsLowest, Tier& out);

// ---------------------------------------------------------------------------
// The zoom curve. The player camera's distance from its look-at target runs
// kZoomMin..kZoomMax (camera_input.cpp, retail 0x1401721c4 / 0x140172218).
// The AO radius is in world units, lerped street -> overview by
// t = (distance - kZoomMin) / (kZoomMax - kZoomMin), clamped; the fade-out
// distances (view depth) move out with the camera and stay below the far plane
// (kFar), so the sky (depth exactly 1.0 -> view depth 512) never gets AO.
// ---------------------------------------------------------------------------
constexpr float kZoomMin = 20.0f;
constexpr float kZoomMax = 130.0f;
constexpr float kFar     = 512.0f;

constexpr float kRadiusStreet    = 1.5f;    // ~half a house's height at the closest zoom
constexpr float kRadiusOverview  = 5.0f;    // 4.0 until 2026-10-16: the user's look ruling (AO.md 7.1)
constexpr float kFadeFromStreet  = 120.0f;
constexpr float kFadeFromOverview = 300.0f;
constexpr float kFadeToStreet    = 220.0f;
constexpr float kFadeToOverview  = 460.0f;

struct Zoom {
    float t;            // 0 street .. 1 overview
    float radius;
    float fadeOutFrom;
    float fadeOutTo;
};
Zoom ZoomFor(float cameraDistance);

// ---------------------------------------------------------------------------
// The projection (AO.md §6.2). The game's camera projection (row-vector
// convention, v * M) is retail's RH 0..1 projection x kZUpToYUp: in the Z-up
// camera space it maps y (forward) to clip w and z (up) to clip y. ASSAO wants a
// standard LH perspective (x right, y up, z forward): rows 1 and 2 swapped.
// ---------------------------------------------------------------------------
void AssaoProjection(const float in[16], float out[16]);

// viewDepth = mul / (add - z) for a 0..1 depth `z`, read from the projection
// itself: the clip-w row r is the one whose column 3 is non-zero; add =
// M[r][2] / M[r][3], mul = -M[3][2]. For the game camera mul = n f / (f - n),
// add = f / (f - n). False when no row writes clip w (orthographic).
bool LinearizeConsts(const float m[16], float& mul, float& add);
// ASSAO's own reading of an LH row-major matrix (ASSAODX11.cpp UpdateConstants):
// mul = -M[3][2], add = M[2][2] (sign-fixed when mul * add < 0), tanHalfFov =
// (1 / M[0][0], 1 / M[1][1]). The unit tests prove it equals LinearizeConsts on
// the swapped matrix.
void AssaoUnpack(const float lh[16], float& mul, float& add, float& tanHalfFovX,
                 float& tanHalfFovY);

// ---------------------------------------------------------------------------
// The frame record: tier + zoom + opacity. Opacity is folded into ASSAO's
// ShadowMultiplier (strength x opacity / 100), not a lerp in the apply shader:
// ASSAO's multiply blend has no spare factor for it and its Apply shaders stay
// Intel's. False = nothing to draw (quality off, opacity 0): the caller leaves
// the seam null.
// ---------------------------------------------------------------------------
constexpr float kShadowMultiplier = 1.0f;   // ASSAO default; AOTUNE owns it
// The rest of the ASSAO_Settings FrameFor fills (ASSAO defaults; AOTUNE owns them). Named so the
// debug tweak overlay (src/ext/tweak/ext_tweaks.h) starts from exactly these.
constexpr float kShadowPower           = 1.5f;
constexpr float kHorizonAngleThreshold = 0.06f;
constexpr float kSharpness             = 0.98f;
constexpr float kDetailShadowStrength  = 0.5f;
bool FrameFor(const Ext::AmbientOcclusion::Params& params, float cameraDistance, Graphics::ExtAoFrame& out);

// ---------------------------------------------------------------------------
// The fog alignment (the user's look 2026-10-14: AO drawn over the fog). The
// materials fog in their pixel shaders, lerp(lit, fogColor, saturate(min(
// (fog.x + viewDepth) * fog.y, fogMax))) with fog = { -fogNear, 1 / (fogFar -
// fogNear) } (Context::SetFog, lighting.srsl GetLightingFog) -- linear in view
// depth, the same shape as ASSAO's FadeOutFrom / FadeOutTo. Our multiply runs
// after the fog, so the fade is set TO the fog's range: AO is gone where the
// fog has taken over. Keeps the zoom fade when the fog is degenerate (fog.y <=
// 0: no fog set) and never lets FadeOutTo reach the far plane (the sky).
// Returns true when the fog was applied.
// ---------------------------------------------------------------------------
constexpr float kFogFadeCeiling = 500.0f;   // FadeOutTo stays below the far plane: the sky stays unoccluded
bool FadeFromFog(float fogParamX, float fogParamY, Graphics::ExtAoFrame& frame);

}  // namespace AoMath
}  // namespace AmbientOcclusion
}  // namespace Ext
