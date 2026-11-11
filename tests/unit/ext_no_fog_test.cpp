// ============================================================================
// tests/unit/ext_no_fog_test.cpp -- ext.no_fog
// ============================================================================
// src/ext/no_fog/no_fog.h without the game: the registry row (class A, OFF by default, no lock, the profile
// keys), a live toggle that is not held by the game's lock, and the fog maximum the seam writes (retail's own
// value to the last bit with the row off, zero with it on). The seam itself is
// WeatherSimulation::BlendLighting (weather_sun.cpp), which needs the running game.
// ============================================================================
#include "unit.h"
#include "ext/no_fog/no_fog.h"
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

}  // namespace

UNIT_TEST(ext_no_fog_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("no_fog");
    CHECK(e != nullptr);
    if (e == nullptr) {
        return;
    }
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);              // a picture-only change: no sim effect, nothing saved
    CHECK(e->defaultOn);                         // ON on a desktop since 1.0.3, at fog_distance 0 = retail's fog
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.no_fog");
    CHECK(std::strstr(e->description, "fog") != nullptr);
    CHECK(ResolveText("").ext.no_fog);                          // absent key = the default (on, at retail's fog)
    CHECK(!ResolveText("ext.no_fog=0\n").ext.no_fog);
    CHECK(ResolveText("ext.no_fog=1\n").ext.no_fog);
    CHECK(!ResolveText("profile=faithful\n").ext.no_fog);       // the faithful profile turns it off
}

UNIT_TEST(ext_no_fog_toggle_is_live_under_the_game_lock) {
    const Ext::Entry* e = Ext::FindEntry("no_fog");
    if (e == nullptr) {
        CHECK(false);
        return;
    }
    int game = 0;
    Ext::Install(ResolveText("ext.no_fog=1\n"), Ext::Profile::PerEntry, false);
    Ext::LockForGame(&game);
    CHECK(Ext::Active(*e));
    CHECK(!Ext::EntryLocked(*e));                // only class B / C rows are held by the lock
    CHECK(Ext::SetRequested(*e, false));         // takes effect at once, in the running game
    CHECK(!Ext::Active(*e));
    CHECK(Ext::SetRequested(*e, true));
    CHECK(Ext::Active(*e));
    Ext::UnlockForGame(&game);
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_no_fog_fog_distance_key) {
    const Ext::ValueKey* k = Ext::FindValueKey("fog_distance");
    CHECK(k != nullptr);
    if (k == nullptr) return;
    CHECK(std::strcmp(k->owner, "no_fog") == 0);
    CHECK(k->minValue == Ext::NoFog::kDistanceRetail);
    CHECK(k->maxValue == Ext::NoFog::kDistanceOff);
    CHECK(k->defaultValue == Ext::NoFog::kDistanceRetail);   // the row ships ON: the default must be retail's fog
    CHECK(ResolveText("").val.fog_distance == 0);
    CHECK(ResolveText("ext.fog_distance=50\n").val.fog_distance == 50);
    CHECK(ResolveText("ext.fog_distance=-5\n").val.fog_distance == 0);      // clamped into 0..100
    CHECK(ResolveText("ext.fog_distance=400\n").val.fog_distance == 100);
}

UNIT_TEST(ext_no_fog_distance_scale) {
    CHECK(Ext::NoFog::DistanceScale(1) > 1.0f && Ext::NoFog::DistanceScale(1) < 1.02f);
    CHECK(Ext::NoFog::DistanceScale(50) == 2.0f);
    CHECK(Ext::NoFog::DistanceScale(75) == 4.0f);
    CHECK(Ext::NoFog::DistanceScale(99) == 100.0f);
    CHECK(Ext::NoFog::DistanceScale(0) == 1.0f);          // below the range: no push
    CHECK(Ext::NoFog::DistanceScale(100) == 100.0f);      // above 99: 99's push (Apply never asks for 100)
    for (int d = 2; d <= 99; ++d) CHECK(Ext::NoFog::DistanceScale(d) > Ext::NoFog::DistanceScale(d - 1));
}

UNIT_TEST(ext_no_fog_apply) {
    // retail's default record (EnvironmentDescription.h: near 90, far 225, max 0.95)
    const float kNear = 90.0f, kFar = 225.0f, kMax = 0.95f;
    // row off: the very same bits, whatever the key says
    for (int d : {0, 1, 50, 99, 100}) {
        float n = kNear, f = kFar, m = kMax;
        Ext::NoFog::Apply(n, f, m, false, d);
        CHECK(Bits(n) == Bits(kNear) && Bits(f) == Bits(kFar) && Bits(m) == Bits(kMax));
    }
    // row on at 0 (the default since the row ships ON): retail's very bits too
    {
        float n = kNear, f = kFar, m = kMax;
        Ext::NoFog::Apply(n, f, m, true, 0);
        CHECK(Bits(n) == Bits(kNear) && Bits(f) == Bits(kFar) && Bits(m) == Bits(kMax));
    }
    // row on at 100: no fog, near / far left alone (the row's original behaviour)
    {
        float n = kNear, f = kFar, m = kMax;
        Ext::NoFog::Apply(n, f, m, true, 100);
        CHECK(Bits(m) == Bits(0.0f));
        CHECK(Bits(n) == Bits(kNear) && Bits(f) == Bits(kFar));
    }
    // row on at 50: twice as far, same maximum
    {
        float n = kNear, f = kFar, m = kMax;
        Ext::NoFog::Apply(n, f, m, true, 50);
        CHECK(n == 180.0f && f == 450.0f);
        CHECK(Bits(m) == Bits(kMax));
    }
}
