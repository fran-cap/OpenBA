// ============================================================================
// tests/unit/ext_better_camera_test.cpp -- ext.better_camera
// ============================================================================
// src/ext/better_camera/better_camera.h without the game: the registry row (class A, off by default, the
// profile keys), the pitch blend (retail's expression to the last bit), the wider zoom limits and their
// normalisation, and the clamp back into retail's range when the row is turned off mid-game.
// ============================================================================
#include "unit.h"
#include "ext/better_camera/better_camera.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>

namespace {

using IO::SettingsFile::Entries;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) { value = e.value; return true; }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

// camera_input.cpp's retail constants (.rdata 0x140172298 / 0x140172200 / 0x140172290 / 0x1401721f0)
constexpr float kFarMan = -65.0f, kFarFlat = 85.0f, kNearMan = -25.0f, kNearFlat = 60.0f;

// Retail's expression, written out here exactly as camera_input.cpp had it before the seam.
float RetailPitch(float manual, float normZ) {
    return (manual * kFarMan  - (1.0f - manual) * kFarFlat) * normZ +
           (manual * kNearMan - (1.0f - manual) * kNearFlat) * (1.0f - normZ);
}

}  // namespace

UNIT_TEST(ext_better_camera_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("better_camera");
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::B);                                   // the save records it: the zoom target and distance can leave retail's range
    CHECK(e->defaultOn);                                              // ON on a desktop since 1.0.3 (ruled 2026-11-08; Android seeds it off)
    CHECK(e->displayName != nullptr && *e->displayName != 0 && e->description != nullptr && *e->description != 0);
    CHECK(e->formatVersion == 1);                                     // class B carries a format version (the pseudo-mod's)
    CHECK(Ext::SettingsKey(*e) == "ext.better_camera");

    CHECK(ResolveText("").ext.better_camera);                         // absent key = the default (on, a desktop)
    CHECK(ResolveText("ext.better_camera=1\n").ext.better_camera);
    CHECK(!ResolveText("ext.better_camera=0\n").ext.better_camera);
    CHECK(!ResolveText("profile=faithful\next.better_camera=1\n").ext.better_camera);   // the faithful profile turns it off
    CHECK(ResolveText("profile=all\n").ext.better_camera);
}

UNIT_TEST(ext_better_camera_off_is_retails_pitch_to_the_last_bit) {
    for (int m = 0; m <= 8; ++m) {
        const float manual = m * 0.125f;                              // the manual pitch steps in 0.125
        for (int i = -10; i <= 120; ++i) {
            const float normZ = i * 0.01f;                            // beyond 0..1 on purpose: a stale saved zoom
            const float got = Ext::BetterCamera::PitchTarget(manual, normZ, kFarMan, kFarFlat, kNearMan, kNearFlat, false);
            CHECK(got == RetailPitch(manual, normZ));                 // exact, not approximate
        }
    }
}

UNIT_TEST(ext_better_camera_zoom_limits) {
    CHECK(Ext::BetterCamera::kRetailZoomMin == 20.0f && Ext::BetterCamera::kRetailZoomMax == 130.0f && Ext::BetterCamera::kRetailZoomSpan == 110.0f);
    CHECK(Ext::BetterCamera::kRetailZoomSpan == Ext::BetterCamera::kRetailZoomMax - Ext::BetterCamera::kRetailZoomMin);
    CHECK(Ext::BetterCamera::kZoomMin == 6.0f && Ext::BetterCamera::kZoomMax == 200.0f);
    // the floor keeps the eye clear of the terrain at the shallow tilt (25 degrees): distance * sin(25 deg) above the focus point
    CHECK(Ext::BetterCamera::kZoomMin * std::sin(25.0f * 3.14159265f / 180.0f) > 2.0f);
    CHECK(Ext::BetterCamera::kZoomSpan == Ext::BetterCamera::kZoomMax - Ext::BetterCamera::kZoomMin);       // recomputed, as the exe fix does
    CHECK(Ext::BetterCamera::kZoomMin < Ext::BetterCamera::kRetailZoomMin && Ext::BetterCamera::kZoomMax > Ext::BetterCamera::kRetailZoomMax);
    // normalised zoom runs 0 .. 1 across the wider range, so the pan speed and (retail) pitch blend keep their shape
    CHECK((Ext::BetterCamera::kZoomMin - Ext::BetterCamera::kZoomMin) / Ext::BetterCamera::kZoomSpan == 0.0f);
    CHECK(std::fabs((Ext::BetterCamera::kZoomMax - Ext::BetterCamera::kZoomMin) / Ext::BetterCamera::kZoomSpan - 1.0f) < 1e-6f);
}

UNIT_TEST(ext_better_camera_clamp_to_retail_range_when_off) {
    using Ext::BetterCamera::ClampToRetailRange;
    // The identity over every value retail can hold, bit for bit: clamping every frame costs the faithful route nothing.
    for (int i = 0; i <= 1100; ++i) {
        const float t = 20.0f + i * 0.1f;
        if (t > 130.0f) break;
        const float c = ClampToRetailRange(t);
        CHECK(std::memcmp(&c, &t, sizeof c) == 0);
    }
    CHECK(ClampToRetailRange(20.0f) == 20.0f && ClampToRetailRange(130.0f) == 130.0f);
    CHECK(ClampToRetailRange(1.0f) == 20.0f && ClampToRetailRange(19.99f) == 20.0f);        // zoomed in past retail's limit
    CHECK(ClampToRetailRange(130.01f) == 130.0f && ClampToRetailRange(200.0f) == 130.0f);   // zoomed out past it
}

UNIT_TEST(ext_better_camera_a_wide_target_cannot_push_the_faithful_pitch_out_of_retails_range) {
    // The review's case: a save written with the row on, or a camera preset stored with it on, hands the faithful camera a
    // zoom target of 200 with no toggle edge. Unclamped, retail's normZ is (200 - 20) / 110 = 1.64 and the pitch blend
    // (linear in normZ) extrapolates past the range it has over 0 .. 1.
    for (int m = 0; m <= 8; ++m) {
        const float manual = m * 0.125f;
        const float p0 = RetailPitch(manual, 0.0f), p1 = RetailPitch(manual, 1.0f);
        const float lo = std::min(p0, p1), hi = std::max(p0, p1);
        const float wide = RetailPitch(manual, (200.0f - 20.0f) / 110.0f);
        CHECK(wide < lo - 1.0f || wide > hi + 1.0f);                                          // the hazard is real
        for (int target = 1; target <= 200; ++target) {
            const float normZ = (Ext::BetterCamera::ClampToRetailRange(static_cast<float>(target)) - 20.0f) / 110.0f;
            const float got = RetailPitch(manual, normZ);
            CHECK(got >= lo - 1e-3f && got <= hi + 1e-3f);                                    // clamped: never outside
        }
    }
}
