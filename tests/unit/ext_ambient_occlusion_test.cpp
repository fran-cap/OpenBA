// ============================================================================
// tests/unit/ext_ambient_occlusion_test.cpp -- ext.ambient_occlusion's pure parts
// ============================================================================
// src/ext/ambient_occlusion/ao_math.{h,cpp}: the tier table, the zoom curve,
// the projection handed to ASSAO and the linearisation constants it implies
// (docs/extensions/AO.md §3, §6.2), and the frame record. No device.
// ============================================================================
#include "unit.h"
#include "ext/ambient_occlusion/ao_math.h"
#include "ext/ambient_occlusion/params.h"
#include "graphics/renderer_ext_ao.h"

#include <cmath>

namespace {

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps * (1.0f + std::fabs(b)); }

// The game camera's projection (row-vector convention): retail's RH 0..1
// Frustum::Projection x kZUpToYUp, as AOSPIKE S4 logged it for a 1024x640 window
// (n = 2, f = 512, fovY 40), built here from n / f / the two scales.
void GameProjection(float n, float f, float sx, float sy, float m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0 * 4 + 0] = sx;
    m[1 * 4 + 2] = f / (f - n);         // y (forward) -> clip z
    m[1 * 4 + 3] = 1.0f;                // y (forward) -> clip w
    m[2 * 4 + 1] = sy;                  // z (up) -> clip y
    m[3 * 4 + 2] = -n * f / (f - n);
}

}  // namespace

UNIT_TEST(ext_ambient_occlusion_tier_table) {
    Ext::AmbientOcclusion::AoMath::Tier t{};
    CHECK(!Ext::AmbientOcclusion::AoMath::TierFor(0, false, t));
    CHECK(!Ext::AmbientOcclusion::AoMath::TierFor(5, false, t));
    CHECK(Ext::AmbientOcclusion::AoMath::TierFor(4, false, t) && t.qualityLevel == Ext::AmbientOcclusion::AoMath::kAssaoLowest && t.blurPassCount == 1);
    CHECK(!Ext::AmbientOcclusion::AoMath::TierFor(-1, false, t));
    CHECK(Ext::AmbientOcclusion::AoMath::TierFor(1, false, t) && t.qualityLevel == Ext::AmbientOcclusion::AoMath::kAssaoLow && t.blurPassCount == 1);
    CHECK(Ext::AmbientOcclusion::AoMath::TierFor(1, true, t) && t.qualityLevel == Ext::AmbientOcclusion::AoMath::kAssaoLowest);
    CHECK(Ext::AmbientOcclusion::AoMath::TierFor(2, false, t) && t.qualityLevel == Ext::AmbientOcclusion::AoMath::kAssaoMedium && t.blurPassCount == 2);
    CHECK(Ext::AmbientOcclusion::AoMath::TierFor(3, false, t) && t.qualityLevel == Ext::AmbientOcclusion::AoMath::kAssaoHigh && t.blurPassCount == 2);
    // ASSAO's QualityLevel numbering (ASSAO.h): -1 lowest, 0 low, 1 medium, 2 high
    CHECK(Ext::AmbientOcclusion::AoMath::kAssaoLowest == -1 && Ext::AmbientOcclusion::AoMath::kAssaoLow == 0 && Ext::AmbientOcclusion::AoMath::kAssaoMedium == 1 &&
          Ext::AmbientOcclusion::AoMath::kAssaoHigh == 2);
}

UNIT_TEST(ext_ambient_occlusion_zoom_curve) {
    const Ext::AmbientOcclusion::AoMath::Zoom street = Ext::AmbientOcclusion::AoMath::ZoomFor(Ext::AmbientOcclusion::AoMath::kZoomMin);
    const Ext::AmbientOcclusion::AoMath::Zoom over   = Ext::AmbientOcclusion::AoMath::ZoomFor(Ext::AmbientOcclusion::AoMath::kZoomMax);
    CHECK(Near(street.t, 0.0f) && Near(street.radius, Ext::AmbientOcclusion::AoMath::kRadiusStreet));
    CHECK(Near(over.t, 1.0f) && Near(over.radius, Ext::AmbientOcclusion::AoMath::kRadiusOverview));
    // clamped outside the camera's range, NaN -> street
    CHECK(Near(Ext::AmbientOcclusion::AoMath::ZoomFor(5.0f).radius, Ext::AmbientOcclusion::AoMath::kRadiusStreet));
    CHECK(Near(Ext::AmbientOcclusion::AoMath::ZoomFor(500.0f).radius, Ext::AmbientOcclusion::AoMath::kRadiusOverview));
    CHECK(Near(Ext::AmbientOcclusion::AoMath::ZoomFor(std::nanf("")).radius, Ext::AmbientOcclusion::AoMath::kRadiusStreet));
    // monotonic, and the fade always ends before the far plane (the sky, depth 1.0 = 512)
    float prev = -1.0f;
    for (float d = 0.0f; d <= 200.0f; d += 2.5f) {
        const Ext::AmbientOcclusion::AoMath::Zoom z = Ext::AmbientOcclusion::AoMath::ZoomFor(d);
        CHECK(z.radius >= prev);
        prev = z.radius;
        CHECK(z.fadeOutFrom < z.fadeOutTo);
        CHECK(z.fadeOutTo < Ext::AmbientOcclusion::AoMath::kFar);
    }
    const Ext::AmbientOcclusion::AoMath::Zoom mid = Ext::AmbientOcclusion::AoMath::ZoomFor(0.5f * (Ext::AmbientOcclusion::AoMath::kZoomMin + Ext::AmbientOcclusion::AoMath::kZoomMax));
    CHECK(Near(mid.radius, 0.5f * (Ext::AmbientOcclusion::AoMath::kRadiusStreet + Ext::AmbientOcclusion::AoMath::kRadiusOverview)));
}

UNIT_TEST(ext_ambient_occlusion_linearisation_constants) {
    // AOSPIKE S4's matrix: [1.60033 0 0 0 | 0 0 1.00392 1 | 0 2.74748 0 0 | 0 0 -2.00784 0]
    float game[16];
    GameProjection(2.0f, 512.0f, 1.60033f, 2.74748f, game);
    CHECK(Near(game[1 * 4 + 2], 1.00392f));
    CHECK(Near(game[3 * 4 + 2], -2.00784f));

    float mul = 0, add = 0;
    CHECK(Ext::AmbientOcclusion::AoMath::LinearizeConsts(game, mul, add));
    CHECK(Near(mul, 2.00784f));   // n f / (f - n)
    CHECK(Near(add, 1.00392f));   // f / (f - n)
    // depth 0 = near, depth 1 = far (the sky's clear value)
    CHECK(Near(mul / (add - 0.0f), 2.0f));
    CHECK(Near(mul / (add - 1.0f), 512.0f, 1e-3f));

    // ASSAO's matrix: rows 1 and 2 swapped = a standard LH perspective
    float lh[16];
    Ext::AmbientOcclusion::AoMath::AssaoProjection(game, lh);
    CHECK(Near(lh[1 * 4 + 1], 2.74748f) && Near(lh[2 * 4 + 2], 1.00392f) && lh[2 * 4 + 3] == 1.0f);
    CHECK(lh[1 * 4 + 2] == 0.0f && lh[2 * 4 + 1] == 0.0f);
    float amul = 0, aadd = 0, tx = 0, ty = 0;
    Ext::AmbientOcclusion::AoMath::AssaoUnpack(lh, amul, aadd, tx, ty);
    CHECK(Near(amul, mul) && Near(aadd, add));        // ASSAO linearises exactly as §6.2
    CHECK(Near(ty, std::tan(20.0f * 3.14159265f / 180.0f), 1e-3f));   // fovY 40
    CHECK(Near(tx, 1.0f / 1.60033f));
    // the swap is its own inverse
    float back[16];
    Ext::AmbientOcclusion::AoMath::AssaoProjection(lh, back);
    for (int i = 0; i < 16; ++i) CHECK(back[i] == game[i]);

    // eye_camera's near 0.1: the constants follow the matrix, whatever n is
    GameProjection(0.1f, 512.0f, 1.6f, 2.74748f, game);
    CHECK(Ext::AmbientOcclusion::AoMath::LinearizeConsts(game, mul, add));
    CHECK(Near(mul / add, 0.1f) && Near(mul / (add - 1.0f), 512.0f, 1e-3f));

    // orthographic (no clip-w row): refused
    float ortho[16] = {};
    ortho[0] = ortho[5] = ortho[10] = ortho[15] = 1.0f;
    CHECK(!Ext::AmbientOcclusion::AoMath::LinearizeConsts(ortho, mul, add));
}

UNIT_TEST(ext_ambient_occlusion_frame_record) {
    Ext::AmbientOcclusion::Params p;   // the contract's defaults: Medium, 20 percent
    CHECK(p.quality == 2 && p.opacity == 20);
    const float kDefault = 0.20f * Ext::AmbientOcclusion::AoMath::kShadowMultiplier;
    Graphics::ExtAoFrame f{};
    CHECK(Ext::AmbientOcclusion::AoMath::FrameFor(p, Ext::AmbientOcclusion::AoMath::kZoomMin, f));
    CHECK(f.qualityLevel == Ext::AmbientOcclusion::AoMath::kAssaoMedium);
    CHECK(Near(f.radius, Ext::AmbientOcclusion::AoMath::kRadiusStreet));
    CHECK(Near(f.shadowMultiplier, kDefault));
    CHECK(f.fadeOutTo < Ext::AmbientOcclusion::AoMath::kFar);

    p.opacity = 10;
    CHECK(Ext::AmbientOcclusion::AoMath::FrameFor(p, Ext::AmbientOcclusion::AoMath::kZoomMax, f));
    CHECK(Near(f.shadowMultiplier, 0.5f * kDefault));
    CHECK(Near(f.radius, Ext::AmbientOcclusion::AoMath::kRadiusOverview));
    p.opacity = 250;   // clamped to the 30 ceiling
    CHECK(Ext::AmbientOcclusion::AoMath::FrameFor(p, 50.0f, f) && Near(f.shadowMultiplier, 1.5f * kDefault));

    p.opacity = 0;     // nothing to draw
    CHECK(!Ext::AmbientOcclusion::AoMath::FrameFor(p, 50.0f, f));
    p.opacity = 20;
    p.quality = 0;     // off
    CHECK(!Ext::AmbientOcclusion::AoMath::FrameFor(p, 50.0f, f));
    p.quality = 3;
    CHECK(Ext::AmbientOcclusion::AoMath::FrameFor(p, 50.0f, f) && f.qualityLevel == Ext::AmbientOcclusion::AoMath::kAssaoHigh);
}

UNIT_TEST(ext_ambient_occlusion_fade_follows_fog) {
    // Context::SetFog(near, far, c) stores { -near, 1 / (far - near) }; the fade takes
    // exactly that range (the materials' fog is linear in view depth over it).
    Graphics::ExtAoFrame f{};
    f.fadeOutFrom = 120.0f; f.fadeOutTo = 220.0f;        // the zoom fade, to be replaced
    CHECK(Ext::AmbientOcclusion::AoMath::FadeFromFog(-80.0f, 1.0f / (300.0f - 80.0f), f));
    CHECK(Near(f.fadeOutFrom, 80.0f) && Near(f.fadeOutTo, 300.0f));
    // A far fog is capped below the far plane: the sky (view depth 512) stays unoccluded.
    CHECK(Ext::AmbientOcclusion::AoMath::FadeFromFog(-100.0f, 1.0f / (900.0f - 100.0f), f));
    CHECK(Near(f.fadeOutFrom, 100.0f) && f.fadeOutTo < Ext::AmbientOcclusion::AoMath::kFar);
    // A fog starting past the cap still fades in order (from < to).
    CHECK(Ext::AmbientOcclusion::AoMath::FadeFromFog(-600.0f, 1.0f / 100.0f, f));
    CHECK(f.fadeOutFrom < f.fadeOutTo && f.fadeOutTo < Ext::AmbientOcclusion::AoMath::kFar);
    // No fog (y = 0, the cleared constants) or a negative range: the zoom fade is kept.
    f.fadeOutFrom = 120.0f; f.fadeOutTo = 220.0f;
    CHECK(!Ext::AmbientOcclusion::AoMath::FadeFromFog(-0.0f, 0.0f, f));
    CHECK(!Ext::AmbientOcclusion::AoMath::FadeFromFog(-50.0f, -1.0f, f));
    CHECK(Near(f.fadeOutFrom, 120.0f) && Near(f.fadeOutTo, 220.0f));
}
