// ============================================================================
// tests/unit/ext_registry_values_test.cpp -- the registry's VALUED KEYS (block AOMENU)
// ============================================================================
// src/ext/registry.{h,cpp} on their own: the table (ext.ao_quality 1..3 default
// 2, ext.ao_opacity 0..30 default 20), ParseValue (well-formed numbers are
// clamped, anything else is malformed = the default), Resolve over a settings
// text (absent / clamped / malformed / the profile rule: faithful and all leave
// every valued key at its default), SetValue (clamp, requested == active, never
// locked), and the "%i" round trip through IO::SettingsFile.
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <cstdint>
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

Ext::State ResolveText(const char* text, const char* cmd = nullptr,
                       Ext::LoadReport* report = nullptr) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, cmd, report);
}

const Ext::ValueKey& K(const char* id) {
    const Ext::ValueKey* k = Ext::FindValueKey(id);
    if (k == nullptr) { std::printf("  no valued key %s\n", id); std::abort(); }
    return *k;
}

}  // namespace

UNIT_TEST(ext_values_table_shape) {
    // 21: main's 16, plus show_grid_hue, _saturation, _opacity, _shade and _width
    // (ext_show_grid_test.cpp)
    // better_water_mode (ext_better_water_test.cpp), ao_quality, ao_opacity, foliage_aa_mode and
    // natural_tree_growth (ext_better_trees_test.cpp), max_shadow_size (below), rock_seat_mode
    // (ext_rock_seat_test.cpp), derived_cache_mb (ext_compress_ao_test.cpp), create_on_demand_idle_min
    // (ext_create_on_demand_test.cpp), texture_quality_drop (ext_texture_quality_test.cpp), fps_cap_rate
    // (ext_fps_cap_test.cpp), touch_mode_toolbar_scale / _zoom_min (ext_touch_mode_test.cpp),
    // touch_pointer_lift / _pick_radius / _tap_twice (ext_touch_pointer_test.cpp)
    CHECK_EQ(Ext::ValueKeyCount(), 21);
    const Ext::ValueKey& q = K("ao_quality");
    const Ext::ValueKey& o = K("ao_opacity");
    CHECK_EQ(q.minValue, 1); CHECK_EQ(q.maxValue, 3); CHECK_EQ(q.defaultValue, 2);
    CHECK_EQ(o.minValue, 0); CHECK_EQ(o.maxValue, 30); CHECK_EQ(o.defaultValue, 20);
    CHECK_STREQ(Ext::SettingsKey(q).c_str(), "ext.ao_quality");
    CHECK_STREQ(Ext::SettingsKey(o).c_str(), "ext.ao_opacity");
    CHECK(Ext::FindValueKey("no_such_key") == nullptr);
    CHECK(Ext::FindValueKey(nullptr) == nullptr);
    const Ext::State boot;   // the struct initialisers
    for (int i = 0; i < Ext::ValueKeyCount(); ++i) {
        const Ext::ValueKey& k = Ext::ValueKeyAt(i);
        CHECK(Ext::ValidId(k.id));
        CHECK(Ext::FindValueKey(k.id) == &k);
        // The settings key space is shared with the bool rows: no id in both.
        CHECK(Ext::FindEntry(k.id) == nullptr);
        // Every key belongs to a registered row: an extension's, or a fix's (rock_seat_mode).
        const Ext::Entry* owner = Ext::FindEntry(k.owner);
        CHECK(owner != nullptr);
        CHECK(k.minValue <= k.defaultValue && k.defaultValue <= k.maxValue);
        Ext::State s = boot;
        CHECK_EQ(k.slot(s), k.defaultValue);   // initialiser == table default
        // A discrete key: every choice in range, the default among them.
        if (k.choices != nullptr) {
            CHECK(k.choiceCount >= 2);
            bool defaultIsChoice = false;
            for (int32_t c = 0; c < k.choiceCount; ++c) {
                CHECK(k.minValue <= k.choices[c] && k.choices[c] <= k.maxValue);
                defaultIsChoice |= k.choices[c] == k.defaultValue;
            }
            CHECK(defaultIsChoice);
        } else {
            CHECK_EQ(k.choiceCount, 0);
        }
    }
    // The owner row the menu mirrors: class A, default on.
    const Ext::Entry* ao = Ext::FindEntry("ambient_occlusion");
    CHECK(ao != nullptr && ao->cls == Ext::Class::A && ao->defaultOn);
}

UNIT_TEST(ext_values_parse_and_clamp) {
    const Ext::ValueKey& q = K("ao_quality");
    const Ext::ValueKey& o = K("ao_opacity");
    bool ok = false;
    CHECK_EQ(Ext::ParseValue(q, "1", ok), 1); CHECK(ok);
    CHECK_EQ(Ext::ParseValue(q, "3", ok), 3); CHECK(ok);
    CHECK_EQ(Ext::ParseValue(q, "0", ok), 1); CHECK(ok);      // below: clamped up
    CHECK_EQ(Ext::ParseValue(q, "7", ok), 3); CHECK(ok);      // above: clamped down
    CHECK_EQ(Ext::ParseValue(q, "-5", ok), 1); CHECK(ok);
    CHECK_EQ(Ext::ParseValue(o, "15", ok), 15); CHECK(ok);
    CHECK_EQ(Ext::ParseValue(o, "55", ok), 30); CHECK(ok);     // above the 30 ceiling: clamped
    CHECK_EQ(Ext::ParseValue(o, "007", ok), 7); CHECK(ok);
    CHECK_EQ(Ext::ParseValue(o, "999999999", ok), 30); CHECK(ok);
    CHECK_EQ(Ext::ParseValue(o, "-999999999", ok), 0); CHECK(ok);
    // Malformed: the default, ok false.
    for (const char* bad : {"", "-", "+5", "5x", "x5", "0x10", "1.5", " 5", "5 ", "1e2",
                            "1234567890", "--1", "fifty"}) {
        ok = true;
        CHECK_EQ(Ext::ParseValue(o, bad, ok), 20);
        CHECK(!ok);
        ok = true;
        CHECK_EQ(Ext::ParseValue(q, bad, ok), 2);
        CHECK(!ok);
    }
    CHECK_EQ(Ext::ClampValue(o, -(INT64_C(1) << 40)), 0);
    CHECK_EQ(Ext::ClampValue(o, INT64_C(1) << 40), 30);
    CHECK_EQ(Ext::ClampValue(o, 12), 12);
}

UNIT_TEST(ext_values_resolve) {
    Ext::LoadReport r;
    // Absent: the defaults, nothing counted.
    Ext::State s = ResolveText("", nullptr, &r);
    CHECK_EQ(s.val.ao_quality, 2);
    CHECK_EQ(s.val.ao_opacity, 20);
    CHECK_EQ(r.malformedValues, 0);
    CHECK_EQ(r.clampedValues, 0);
    // Present and in range.
    s = ResolveText("ext.ao_quality=3\next.ao_opacity=15\n", nullptr, &r);
    CHECK_EQ(s.val.ao_quality, 3);
    CHECK_EQ(s.val.ao_opacity, 15);
    CHECK_EQ(r.malformedValues, 0);
    CHECK_EQ(r.clampedValues, 0);
    // Out of range: clamped and counted; malformed: default and counted.
    s = ResolveText("ext.ao_quality=9\next.ao_opacity=lots\n", nullptr, &r);
    CHECK_EQ(s.val.ao_quality, 3);
    CHECK_EQ(s.val.ao_opacity, 20);
    CHECK_EQ(r.clampedValues, 1);
    CHECK_EQ(r.malformedValues, 1);
    CHECK_EQ(r.malformed, 0);   // the bool rows' counter is separate
    // A valued key is not a toggle and a toggle is not a valued key.
    s = ResolveText("ext.ambient_occlusion=1\next.ao_quality=1\n", nullptr, &r);
    CHECK(s.ext.ambient_occlusion);
    CHECK_EQ(s.val.ao_quality, 1);
    CHECK_EQ(r.malformed, 0);
}

UNIT_TEST(ext_values_profile_rule) {
    // faithful / all (settings key or command line) leave every valued key at its default,
    // whatever settings.cfg holds; the toggles follow the profile as before.
    const char* text = "ext.ao_quality=1\next.ao_opacity=25\next.ambient_occlusion=1\n";
    Ext::LoadReport r;
    Ext::State s = ResolveText((std::string("profile=faithful\n") + text).c_str(), nullptr, &r);
    CHECK(r.profile == Ext::Profile::Faithful);
    CHECK(!s.ext.ambient_occlusion);
    CHECK_EQ(s.val.ao_quality, 2);
    CHECK_EQ(s.val.ao_opacity, 20);
    s = ResolveText((std::string("profile=all\n") + text).c_str(), nullptr, &r);
    CHECK(s.ext.ambient_occlusion);
    CHECK_EQ(s.val.ao_quality, 2);
    CHECK_EQ(s.val.ao_opacity, 20);
    s = ResolveText(text, "faithful", &r);
    CHECK(r.profileFromCommandLine);
    CHECK_EQ(s.val.ao_quality, 2);
    CHECK_EQ(s.val.ao_opacity, 20);
    // Per-entry: the keys.
    s = ResolveText(text, nullptr, &r);
    CHECK(s.ext.ambient_occlusion);
    CHECK_EQ(s.val.ao_quality, 1);
    CHECK_EQ(s.val.ao_opacity, 25);
}

UNIT_TEST(ext_values_set_and_round_trip) {
    const Ext::ValueKey& q = K("ao_quality");
    const Ext::ValueKey& o = K("ao_opacity");
    Ext::Install(Ext::State(), Ext::Profile::PerEntry, false);
    CHECK_EQ(Ext::Value(q), 2);
    CHECK_EQ(Ext::SetValue(q, 3), 3);
    CHECK_EQ(Ext::Val().ao_quality, 3);
    CHECK_EQ(Ext::SetValue(q, 0), 1);       // clamped
    CHECK_EQ(Ext::SetValue(o, 250), 30);    // clamped to the ceiling
    CHECK_EQ(Ext::SetValue(o, 10), 10);
    // Class A: a game lock does not hold a value back.
    int owner = 0;
    Ext::LockForGame(&owner);
    CHECK_EQ(Ext::SetValue(o, 25), 25);
    CHECK_EQ(Ext::Value(o), 25);
    Ext::UnlockForGame(&owner);
    CHECK_EQ(Ext::Value(o), 25);            // the unlock copies requested -> active: same value

    // The round trip: what the write-back records ("%i", Persistence::Set) reads back equal.
    Entries out;
    for (int i = 0; i < Ext::ValueKeyCount(); ++i) {
        const Ext::ValueKey& k = Ext::ValueKeyAt(i);
        out.push_back({Ext::SettingsKey(k), std::to_string(Ext::Value(k))});
    }
    const std::string text = IO::SettingsFile::Serialize(out);
    Ext::LoadReport r;
    const Ext::State back = ResolveText(text.c_str(), nullptr, &r);
    CHECK_EQ(back.val.ao_quality, 1);
    CHECK_EQ(back.val.ao_opacity, 25);
    CHECK_EQ(r.malformedValues, 0);
    CHECK_EQ(r.clampedValues, 0);
    Ext::Install(Ext::State(), Ext::Profile::PerEntry, false);   // leave the live state clean
}

// ---------------------------------------------------------------------------
// ext.max_shadow_size (block FAA-SHADOW2): a DISCRETE key, 4096 | 8192, default 4096;
// any other number (in range or not) and anything malformed reads as 4096.
// ---------------------------------------------------------------------------
UNIT_TEST(ext_values_max_shadow_size_table) {
    const Ext::ValueKey& z = K("max_shadow_size");
    CHECK_STREQ(Ext::SettingsKey(z).c_str(), "ext.max_shadow_size");
    CHECK_STREQ(z.owner, "max_shadow_res");
    CHECK_EQ(z.minValue, 4096); CHECK_EQ(z.maxValue, 8192); CHECK_EQ(z.defaultValue, 4096);
    CHECK(z.choices != nullptr);
    CHECK_EQ(z.choiceCount, 2);
    CHECK_EQ(z.choices[0], 4096); CHECK_EQ(z.choices[1], 8192);
    CHECK_EQ(Ext::State().val.max_shadow_size, 4096);
    // The owner row: class A, default on (off = retail's 2048 at High).
    const Ext::Entry* row = Ext::FindEntry("max_shadow_res");
    CHECK(row != nullptr && row->cls == Ext::Class::A && row->defaultOn);
}

UNIT_TEST(ext_values_max_shadow_size_parse) {
    const Ext::ValueKey& z = K("max_shadow_size");
    bool ok = false;
    CHECK_EQ(Ext::ParseValue(z, "4096", ok), 4096); CHECK(ok);
    CHECK_EQ(Ext::ParseValue(z, "8192", ok), 8192); CHECK(ok);
    CHECK_EQ(Ext::ParseValue(z, "08192", ok), 8192); CHECK(ok);
    // Well-formed but not a choice: the default (not clamped to a neighbour).
    for (const char* other : {"6000", "8191", "4097", "2048", "16384", "0", "-8192", "999999999"}) {
        ok = false;
        CHECK_EQ(Ext::ParseValue(z, other, ok), 4096);
        CHECK(ok);
    }
    // Malformed: the default, ok false.
    for (const char* bad : {"", "8k", "8192 ", " 4096", "0x2000", "8192.0", "+8192", "max"}) {
        ok = true;
        CHECK_EQ(Ext::ParseValue(z, bad, ok), 4096);
        CHECK(!ok);
    }
    CHECK_EQ(Ext::ClampValue(z, 8192), 8192);
    CHECK_EQ(Ext::ClampValue(z, 7000), 4096);
    CHECK_EQ(Ext::ClampValue(z, INT64_C(1) << 40), 4096);
    CHECK_EQ(Ext::ClampValue(z, 4096 + (INT64_C(1) << 32)), 4096);   // no truncation to a choice
    // The range keys are unchanged by the discrete rule.
    CHECK_EQ(Ext::ClampValue(K("ao_opacity"), 55), 30);
}

UNIT_TEST(ext_values_max_shadow_size_resolve_and_set) {
    Ext::LoadReport r;
    Ext::State s = ResolveText("", nullptr, &r);
    CHECK_EQ(s.val.max_shadow_size, 4096);
    s = ResolveText("ext.max_shadow_res=1\next.max_shadow_size=8192\n", nullptr, &r);
    CHECK(s.ext.max_shadow_res);
    CHECK_EQ(s.val.max_shadow_size, 8192);
    CHECK_EQ(r.malformedValues, 0);
    CHECK_EQ(r.clampedValues, 0);
    // Another number: the default, counted as clamped; garbage: the default, counted as malformed.
    s = ResolveText("ext.max_shadow_size=6000\n", nullptr, &r);
    CHECK_EQ(s.val.max_shadow_size, 4096);
    CHECK_EQ(r.clampedValues, 1);
    CHECK_EQ(r.malformedValues, 0);
    s = ResolveText("ext.max_shadow_size=huge\n", nullptr, &r);
    CHECK_EQ(s.val.max_shadow_size, 4096);
    CHECK_EQ(r.malformedValues, 1);
    // The profile rule: faithful leaves it at the default.
    s = ResolveText("profile=faithful\next.max_shadow_res=1\next.max_shadow_size=8192\n", nullptr, &r);
    CHECK(!s.ext.max_shadow_res);
    CHECK_EQ(s.val.max_shadow_size, 4096);

    // SetValue: a choice sticks, anything else = the default; the "%i" round trip.
    const Ext::ValueKey& z = K("max_shadow_size");
    Ext::Install(Ext::State(), Ext::Profile::PerEntry, false);
    CHECK_EQ(Ext::SetValue(z, 8192), 8192);
    CHECK_EQ(Ext::Val().max_shadow_size, 8192);
    CHECK_EQ(Ext::SetValue(z, 5000), 4096);
    CHECK_EQ(Ext::SetValue(z, 8192), 8192);
    Entries out;
    out.push_back({Ext::SettingsKey(z), std::to_string(Ext::Value(z))});
    const Ext::State back = ResolveText(IO::SettingsFile::Serialize(out).c_str(), nullptr, &r);
    CHECK_EQ(back.val.max_shadow_size, 8192);
    CHECK_EQ(r.clampedValues, 0);
    Ext::Install(Ext::State(), Ext::Profile::PerEntry, false);   // leave the live state clean
}
