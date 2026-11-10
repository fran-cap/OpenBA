// ============================================================================
// tests/unit/ext_camera_angle_test.cpp -- ext.camera_angle + ext.camera_tilt
// ============================================================================
// src/ext/camera_angle/camera_angle.h without the game: the registry row (class A, OFF, live), the valued key, and
// the far pitch pair the seam hands PlayerToolManager::UpdateCamera (retail's own bits with the row off, the
// curve's anchors with it on, every pitch inside retail's -85 ceiling).
// ============================================================================
#include "unit.h"
#include "ext/better_camera/better_camera.h"
#include "ext/camera_angle/camera_angle.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

using IO::SettingsFile::Entries;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

uint32_t Bits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

// retail's four slopes (camera_input.cpp: 0x140172298 / 0x140172200 / 0x140172290 / 0x1401721f0)
constexpr float kFarMan = -65.0f, kFarFlat = 85.0f, kNearMan = -25.0f, kNearFlat = 60.0f;

bool Near(float a, float b) { return a - b < 1e-4f && b - a < 1e-4f; }

}  // namespace

UNIT_TEST(ext_camera_angle_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("camera_angle");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);      // recomputed every frame, nothing saved
    CHECK(e->defaultOn);                 // ON on a desktop (ruled 2026-11-08); tilt 50 = retail's pitch
    CHECK(e->formatVersion == 0);
    CHECK(ResolveText("").ext.camera_angle);
    CHECK(ResolveText("").val.camera_tilt == 50);
    CHECK(ResolveText("ext.camera_angle=1\n").ext.camera_angle);
    CHECK(!ResolveText("profile=faithful\n").ext.camera_angle);

    const Ext::ValueKey* k = Ext::FindValueKey("camera_tilt");
    CHECK(k != nullptr);
    if (k == nullptr) return;
    CHECK(std::strcmp(k->owner, "camera_angle") == 0);
    CHECK(k->minValue == Ext::CameraAngle::kTiltMin && k->maxValue == Ext::CameraAngle::kTiltMax);
    CHECK(k->defaultValue == Ext::CameraAngle::kTiltRetail);
    CHECK(ResolveText("ext.camera_tilt=150\n").val.camera_tilt == 100);   // clamped
}

UNIT_TEST(ext_camera_angle_far_pair_off_is_retail) {
    for (int tilt : {0, 25, 50, 90, 100}) {
        float m = 0.0f, f = 0.0f;
        Ext::CameraAngle::FarPair(false, tilt, kFarMan, kFarFlat, kNearMan, kNearFlat, m, f);
        CHECK(Bits(m) == Bits(kFarMan) && Bits(f) == Bits(kFarFlat));
    }
}

UNIT_TEST(ext_camera_angle_far_pair_anchors) {
    float m = 0.0f, f = 0.0f;
    Ext::CameraAngle::FarPair(true, 50, kFarMan, kFarFlat, kNearMan, kNearFlat, m, f);   // the middle: retail
    CHECK(Bits(m) == Bits(kFarMan) && Bits(f) == Bits(kFarFlat));
    Ext::CameraAngle::FarPair(true, 0, kFarMan, kFarFlat, kNearMan, kNearFlat, m, f);    // top-down
    CHECK(Near(m, -85.0f) && Near(f, 85.0f));
    Ext::CameraAngle::FarPair(true, 90, kFarMan, kFarFlat, kNearMan, kNearFlat, m, f);   // steady: the near pair
    CHECK(Near(m, kNearMan) && Near(f, kNearFlat));
    Ext::CameraAngle::FarPair(true, 100, kFarMan, kFarFlat, kNearMan, kNearFlat, m, f);  // a touch flatter
    CHECK(Near(m, -22.5f) && Near(f, 54.0f));
}

UNIT_TEST(ext_camera_angle_pitch_stays_inside_retail_ceiling) {
    // every slider position, every manual pitch, every zoom: -85 <= pitch <= -22.5
    for (int tilt = 0; tilt <= 100; ++tilt) {
        float m = 0.0f, f = 0.0f;
        Ext::CameraAngle::FarPair(true, tilt, kFarMan, kFarFlat, kNearMan, kNearFlat, m, f);
        for (int mi = 0; mi <= 8; ++mi) {
            for (int zi = 0; zi <= 10; ++zi) {
                const float p = Ext::BetterCamera::PitchTarget(mi / 8.0f, zi / 10.0f, m, f, kNearMan, kNearFlat, false);
                CHECK(p >= -85.0f - 1e-3f);
                CHECK(p <= -22.5f + 1e-3f);
            }
        }
    }
    // 90 is the old steady tilt: the same pitch at every zoom
    float m = 0.0f, f = 0.0f;
    Ext::CameraAngle::FarPair(true, 90, kFarMan, kFarFlat, kNearMan, kNearFlat, m, f);
    for (int mi = 0; mi <= 8; ++mi) {
        const float a = Ext::BetterCamera::PitchTarget(mi / 8.0f, 0.0f, m, f, kNearMan, kNearFlat, false);
        const float b = Ext::BetterCamera::PitchTarget(mi / 8.0f, 1.0f, m, f, kNearMan, kNearFlat, false);
        CHECK(Near(a, b));
    }
}

// The Extensions dialog's Experimental tab (registry.h Experimental, ruled 2026-11-08): the phone rows and staggered
// passes, and the lighter far trees while their look is judged (LEAF_LOD.md), nothing else. Only where a row is
// listed changes.
UNIT_TEST(ext_registry_experimental_rows) {
    int n = 0;
    for (int i = 0; i < Ext::EntryCount(); ++i) {
        const Ext::Entry& e = Ext::EntryAt(i);
        if (!Ext::Experimental(e)) continue;
        ++n;
        CHECK(e.tier == Ext::Tier::Extension);
        CHECK(std::strcmp(e.id, "touch_mode") == 0 || std::strcmp(e.id, "touch_pointer") == 0 ||
              std::strcmp(e.id, "phone_menu") == 0 || std::strcmp(e.id, "staggered_passes") == 0 ||
              std::strcmp(e.id, "leaf_lod") == 0);
    }
    CHECK(n == 5);
    CHECK(!Ext::Experimental(*Ext::FindEntry("camera_angle")));
    CHECK(!Ext::Experimental(*Ext::FindEntry("fps_cap")));
}
