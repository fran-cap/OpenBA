// ============================================================================
// tests/unit/ext_registry_test.cpp -- the fixes and extensions registry
// ============================================================================
// src/ext/registry.{h,cpp} on their own: the settings parse (absent key =
// the entry's default, 0 / 1, a malformed value, an unknown id ignored, the
// profile key and the command line's profile over it), the read from a real
// settings.cfg text through IO::SettingsFile::Parse, and the class lock
// (A live, B / C locked for the game; the owner rule; the pseudo-mod list).
// The three `unit_*` extensions exist only under BANISHED_UNIT_TEST.
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <string>

namespace {

using IO::SettingsFile::Entries;

// Lookup over a parsed settings.cfg.
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

const Ext::Entry& E(const char* id) {
    const Ext::Entry* e = Ext::FindEntry(id);
    if (e == nullptr) { std::printf("  no entry %s\n", id); std::abort(); }
    return *e;
}

}  // namespace

UNIT_TEST(ext_registry_table_shape) {
    // The three fixes block FIXPILOT wires, default ON, no class.
    for (const char* id : {"icon_slide", "residence_goto", "tavern_counter", "start_barn_height",
                           "ui_scale_layout", "rock_seat", "terrain_resort",
                           "large_packages", "boot_device_lost", "slider_drag_clamp",
                           "dialog_position_drift", "alloc_tiebreak", "outline_with_panel",
                           "foundation_piles", "path_query_first", "crop_planting_exit",
                           "firefighter_wells", "unresolved_seeds",
                           "commute_cost", "nomad_arrival", "mods_persist",
                           "save_failure_notice", "boat_reach_notice", "cancel_refund",
                           "autotrade_free_goods", "input_fetch_room"}) {
        const Ext::Entry& e = E(id);
        CHECK(e.tier == Ext::Tier::Fix);
        CHECK(e.cls == Ext::Class::None);
        CHECK(e.defaultOn);
        CHECK(e.displayName != nullptr && e.displayName[0] != '\0');
        CHECK(e.description != nullptr && e.description[0] != '\0');
    }
    // Every id is [a-z0-9_]+ (never a `.`: `ext:<id>` must not name a file),
    // unique, and every B / C entry carries a formatVersion (SAVETAIL).
    for (int i = 0; i < Ext::EntryCount(); ++i) {
        const Ext::Entry& e = Ext::EntryAt(i);
        CHECK(Ext::ValidId(e.id));
        CHECK(Ext::FindEntry(e.id) == &e);
        if (Ext::IsLockedClass(e.cls)) CHECK(e.formatVersion >= 1);
        if (e.tier == Ext::Tier::Fix) CHECK(e.cls == Ext::Class::None);
        else CHECK(e.cls != Ext::Class::None);
        // Class A ships ON, class B / C OFF (DESIGN.md section 2, ruled 2026-10-18).
        // The unit_* test rows keep OFF: the lock tests below start from nothing on.
        // eye_camera is class A but changes how the game is played: the player's call, OFF.
        // no_fog is class A but changes how the map looks: the player's call, OFF. So is show_grid.
        // texture_quality is class A but lowers texture detail: the player's call, OFF (TEXQUALITY).
        // phone_menu is class A but moves the desktop menu: OFF; the Android first run turns it on (PHONEMENU).
        // fps_cap is class A but caps a desktop's game, retail's uncapped: OFF; the Android first run turns it on (P8).
        // touch_mode is class A but puts camera controls over a desktop's game: OFF; the Android first run turns it on.
        // touch_pointer is class A but takes a touch screen's fingers from the mouse: OFF; the Android first run turns it on.
        if (e.tier == Ext::Tier::Extension && std::strncmp(e.id, "unit_", 5) != 0)
            CHECK(e.defaultOn == (e.cls == Ext::Class::A && std::strcmp(e.id, "eye_camera") != 0 &&
                                  std::strcmp(e.id, "no_fog") != 0 && std::strcmp(e.id, "show_grid") != 0 &&
                                  std::strcmp(e.id, "texture_quality") != 0 &&
                                  std::strcmp(e.id, "phone_menu") != 0 && std::strcmp(e.id, "fps_cap") != 0 &&
                                  std::strcmp(e.id, "touch_mode") != 0 && std::strcmp(e.id, "touch_pointer") != 0));
    }
    CHECK(!Ext::ValidId("a.b"));
    CHECK(!Ext::ValidId("Abc"));
    CHECK(!Ext::ValidId(""));
    CHECK(!Ext::ValidId(nullptr));
    CHECK(Ext::ValidId("icon_slide2"));
    CHECK(Ext::FindEntry("no_such_id") == nullptr);
    CHECK(Ext::FindEntry(nullptr) == nullptr);
    CHECK_STREQ(Ext::SettingsKey(E("icon_slide")).c_str(), "fix.icon_slide");
    CHECK_STREQ(Ext::SettingsKey(E("unit_b")).c_str(), "ext.unit_b");
    CHECK_STREQ(Ext::ClassLetter(Ext::Class::None), "");
    CHECK_STREQ(Ext::ClassLetter(E("unit_c").cls), "C");
    // Fixes come first in the table (the menu order).
    CHECK(Ext::EntryAt(0).tier == Ext::Tier::Fix);
    CHECK(Ext::EntryAt(Ext::EntryCount() - 1).tier == Ext::Tier::Extension);
}

UNIT_TEST(ext_registry_absent_keys_take_defaults) {
    Ext::LoadReport r;
    const Ext::State s = ResolveText("", nullptr, &r);
    CHECK(s.fix.icon_slide && s.fix.residence_goto && s.fix.tavern_counter);
    CHECK(!s.ext.unit_a && !s.ext.unit_b && !s.ext.unit_c);
    CHECK(r.profile == Ext::Profile::PerEntry);
    CHECK(!r.profileFromCommandLine);
    CHECK_EQ(r.malformed, 0);
    // A null lookup is "no settings at all": the defaults too.
    const Ext::State n = Ext::Resolve(nullptr, nullptr, nullptr, nullptr);
    CHECK(n.fix.icon_slide && !n.ext.unit_b);
}

UNIT_TEST(ext_registry_zero_and_one) {
    const Ext::State s = ResolveText(
        "fix.icon_slide=0\nfix.tavern_counter=1\next.unit_b=1\next.unit_c=0\n");
    CHECK(!s.fix.icon_slide);
    CHECK(s.fix.residence_goto);        // absent: default
    CHECK(s.fix.tavern_counter);
    CHECK(s.ext.unit_b);
    CHECK(!s.ext.unit_c);
    CHECK(!s.ext.unit_a);               // absent: default
}

UNIT_TEST(ext_registry_malformed_value_keeps_default) {
    Ext::LoadReport r;
    const Ext::State s = ResolveText(
        "fix.icon_slide=true\nfix.residence_goto=\next.unit_a=2\nfix.tavern_counter= 0 \n",
        nullptr, &r);
    CHECK(s.fix.icon_slide);            // "true" is not a toggle value
    CHECK(s.fix.residence_goto);        // empty
    CHECK(!s.ext.unit_a);               // "2"
    CHECK(!s.fix.tavern_counter);       // the codec trims spaces: "0"
    CHECK_EQ(r.malformed, 3);
}

UNIT_TEST(ext_registry_unknown_ids_ignored) {
    Ext::LoadReport r;
    const Ext::State s = ResolveText(
        "fix.no_such_fix=0\next.no_such_ext=1\nfix.=0\nicon_slide=0\nFix.icon_slide=0\n",
        nullptr, &r);
    CHECK(s.fix.icon_slide && s.fix.residence_goto && s.fix.tavern_counter);
    CHECK(!s.ext.unit_a && !s.ext.unit_b && !s.ext.unit_c);
    CHECK_EQ(r.malformed, 0);
}

UNIT_TEST(ext_registry_profile_key_overrides_entries) {
    Ext::LoadReport r;
    Ext::State s = ResolveText("profile=faithful\nfix.icon_slide=1\next.unit_b=1\n", nullptr, &r);
    CHECK(r.profile == Ext::Profile::Faithful);
    CHECK(!r.profileFromCommandLine);
    CHECK(!s.fix.icon_slide && !s.fix.residence_goto && !s.fix.tavern_counter);
    CHECK(!s.ext.unit_a && !s.ext.unit_b && !s.ext.unit_c);

    s = ResolveText("profile=all\nfix.icon_slide=0\n", nullptr, &r);
    CHECK(r.profile == Ext::Profile::All);
    CHECK(s.fix.icon_slide && s.ext.unit_a && s.ext.unit_b && s.ext.unit_c);

    // An unknown profile name is the per-entry keys.
    s = ResolveText("profile=Faithful\nfix.icon_slide=0\n", nullptr, &r);
    CHECK(r.profile == Ext::Profile::PerEntry);
    CHECK(!s.fix.icon_slide && s.fix.residence_goto);
}

UNIT_TEST(ext_registry_command_line_profile_wins) {
    Ext::LoadReport r;
    // The gate's force: faithful from the command line over everything.
    Ext::State s = ResolveText("profile=all\nfix.icon_slide=1\n", "faithful", &r);
    CHECK(r.profile == Ext::Profile::Faithful);
    CHECK(r.profileFromCommandLine);
    CHECK(!s.fix.icon_slide && !s.ext.unit_a);

    // An empty command-line value is "not given": the settings key decides.
    s = ResolveText("profile=faithful\n", "", &r);
    CHECK(!r.profileFromCommandLine);
    CHECK(r.profile == Ext::Profile::Faithful);

    // A command-line name that is not a profile is the per-entry keys, and
    // it still counts as given (it overrode the settings key).
    s = ResolveText("profile=faithful\nfix.icon_slide=0\n", "bogus", &r);
    CHECK(r.profileFromCommandLine);
    CHECK(r.profile == Ext::Profile::PerEntry);
    CHECK(!s.fix.icon_slide && s.fix.residence_goto);
}

UNIT_TEST(ext_registry_gate_profile_forced) {
    Ext::State s = ResolveText("", "faithful");
    Ext::Install(s, Ext::Profile::Faithful, true);
    CHECK(Ext::GateProfileForced());
    CHECK(!Ext::Fix().icon_slide);
    Ext::Install(s, Ext::Profile::Faithful, false);      // from settings.cfg: not the gate
    CHECK(!Ext::GateProfileForced());
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
    CHECK(Ext::Fix().icon_slide);
}

UNIT_TEST(ext_registry_fix_and_class_a_toggle_live) {
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
    int owner = 0;
    Ext::LockForGame(&owner);
    CHECK(Ext::Locked());

    // A fix: live even under the lock.
    CHECK(!Ext::EntryLocked(E("icon_slide")));
    CHECK(Ext::SetRequested(E("icon_slide"), false));
    CHECK(!Ext::Fix().icon_slide);
    CHECK(!Ext::Requested(E("icon_slide")));

    // Class A: live.
    CHECK(!Ext::EntryLocked(E("unit_a")));
    CHECK(Ext::SetRequested(E("unit_a"), true));
    CHECK(Ext::Ext().unit_a);
    CHECK(!Ext::SetRequested(E("unit_a"), true));      // no change the second time

    Ext::UnlockForGame(&owner);
    CHECK(!Ext::Locked());
}

UNIT_TEST(ext_registry_class_b_c_locked_for_the_game) {
    Ext::Install(ResolveText("ext.unit_c=1\n"), Ext::Profile::PerEntry, false);
    CHECK(!Ext::Locked());

    // At the menu (no lock) B / C take effect at once.
    CHECK(Ext::SetRequested(E("unit_b"), true));
    CHECK(Ext::Ext().unit_b);

    int game1 = 0;
    Ext::LockForGame(&game1);
    CHECK(Ext::EntryLocked(E("unit_b")));
    CHECK(Ext::EntryLocked(E("unit_c")));

    // In game: the request changes, the active value does not.
    CHECK(!Ext::SetRequested(E("unit_b"), false));
    CHECK(Ext::Ext().unit_b);
    CHECK(!Ext::Requested(E("unit_b")));
    CHECK(!Ext::SetRequested(E("unit_c"), false));
    CHECK(Ext::Ext().unit_c);

    // A reload: the new game locks BEFORE the old one is destroyed. It starts
    // with the request; the old game's unlock does not release the new lock.
    int game2 = 0;
    Ext::LockForGame(&game2);
    CHECK(!Ext::Ext().unit_b && !Ext::Ext().unit_c);
    CHECK(!Ext::SetRequested(E("unit_b"), true));      // locked again
    Ext::UnlockForGame(&game1);
    CHECK(Ext::Locked());
    CHECK(!Ext::Ext().unit_b);                          // still the game's value

    // A null owner never unlocks.
    Ext::UnlockForGame(nullptr);
    CHECK(Ext::Locked());

    // Back to the menu: the active state is the request.
    Ext::UnlockForGame(&game2);
    CHECK(!Ext::Locked());
    CHECK(Ext::Ext().unit_b);
    CHECK(!Ext::Ext().unit_c);
}

UNIT_TEST(ext_registry_in_game_change_keeps_the_request) {
    Ext::Install(ResolveText("ext.unit_b=1\n"), Ext::Profile::PerEntry, false);
    int game1 = 0;
    Ext::LockForGame(&game1);
    CHECK(!Ext::LockKeepsRequest());                    // a plain load: the save's set decides

    // The player turns unit_b off and unit_c on in game and confirms the reload.
    CHECK(!Ext::SetRequested(E("unit_b"), false));
    CHECK(!Ext::SetRequested(E("unit_c"), true));
    Ext::KeepRequestOnNextLock();
    CHECK(!Ext::LockKeepsRequest());                    // not until the reload locks

    // The old game is destroyed, then the reloaded one locks with the request.
    Ext::UnlockForGame(&game1);
    int game2 = 0;
    Ext::LockForGame(&game2);
    CHECK(Ext::LockKeepsRequest());
    CHECK(!Ext::Ext().unit_b);
    CHECK(Ext::Ext().unit_c);

    // One shot: the next game's lock is a plain one again.
    Ext::UnlockForGame(&game2);
    CHECK(!Ext::LockKeepsRequest());
    int game3 = 0;
    Ext::LockForGame(&game3);
    CHECK(!Ext::LockKeepsRequest());
    Ext::UnlockForGame(&game3);

    // A lock from a save's set never keeps the request.
    Ext::KeepRequestOnNextLock();
    int game4 = 0;
    Ext::LockForGame(&game4);
    CHECK(Ext::LockForGameWithSet(&game4, { "unit_b" }));
    CHECK(!Ext::LockKeepsRequest());
    Ext::UnlockForGame(&game4);
}

UNIT_TEST(ext_registry_pseudo_mods_for_save) {
    Ext::Install(ResolveText("ext.unit_a=1\next.unit_b=1\next.unit_c=1\n"),
                 Ext::Profile::PerEntry, false);
    Ext::PseudoMod mods[4];
    // Class A never goes in the save; B and C do, as `ext:<id>`.
    CHECK_EQ(Ext::PseudoModsForSave(mods, 4), 2);
    CHECK_STREQ(mods[0].path.c_str(), "ext:unit_b");
    CHECK_EQ(mods[0].formatVersion, 1);
    CHECK_EQ(mods[0].field10, 0);
    CHECK_EQ(mods[0].field14, 0x010007ffu);
    CHECK_STREQ(mods[1].path.c_str(), "ext:unit_c");
    CHECK_STREQ(mods[1].name.c_str(), "Unit C");
    CHECK_EQ(mods[1].formatVersion, 3);
    // The count is the whole list even past the capacity.
    CHECK_EQ(Ext::PseudoModsForSave(nullptr, 0), 2);

    // The engine-facing provider: the same list as plain pointers.
    CHECK(Ext::SaveModsProvider() == &Ext::ProvideExtraModsForSave);
    Ext::PseudoModRecord rec[4];
    CHECK_EQ(Ext::ProvideExtraModsForSave(rec, 4), 2);
    CHECK_STREQ(rec[0].path, "ext:unit_b");
    CHECK_STREQ(rec[1].name, "Unit C");
    CHECK_EQ(rec[1].field14, 0x010007ffu);
    CHECK_EQ(rec[1].formatVersion, 3);

    // It reports the ACTIVE set: a B toggled off in game is still in this
    // game's save.
    int game = 0;
    Ext::LockForGame(&game);
    Ext::SetRequested(E("unit_b"), false);
    CHECK_EQ(Ext::PseudoModsForSave(nullptr, 0), 2);
    Ext::UnlockForGame(&game);
    CHECK_EQ(Ext::PseudoModsForSave(nullptr, 0), 1);

    // Faithful: nothing, and no provider installed at all.
    Ext::Install(ResolveText("", "faithful"), Ext::Profile::Faithful, true);
    CHECK_EQ(Ext::PseudoModsForSave(nullptr, 0), 0);
    CHECK(Ext::SaveModsProvider() == nullptr);
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}

// A settings.cfg with the same key twice: IO::SettingsFile::Parse keeps ONE entry, at the
// first occurrence's position, holding the LAST value (settings_file.cpp, "last wins"),
// and the game's lookup (registry_boot.cpp's PersistenceLookup over the settings store,
// settings_store.cpp) reads that same parse. So the LAST line wins, in either order, and
// a malformed last value does not fall back to the earlier good one: it is the entry's
// value, malformed, and the default applies.
UNIT_TEST(ext_registry_duplicate_key_last_line_wins) {
    const char* text = "fix.icon_slide=1\nfix.icon_slide=0\next.unit_b=0\next.unit_b=1\n";
    Entries e = IO::SettingsFile::Parse(std::string(text));
    CHECK_EQ(e.size(), static_cast<size_t>(2));
    CHECK(e.size() == 2 && e[0].key == "fix.icon_slide" && e[0].value == "0");
    CHECK(e.size() == 2 && e[1].key == "ext.unit_b" && e[1].value == "1");

    Ext::State s = ResolveText(text);
    CHECK(!s.fix.icon_slide);
    CHECK(s.ext.unit_b);
    s = ResolveText("fix.icon_slide=0\nfix.icon_slide=1\next.unit_b=1\next.unit_b=0\n");
    CHECK(s.fix.icon_slide);
    CHECK(!s.ext.unit_b);

    Ext::LoadReport r;
    s = ResolveText("fix.icon_slide=0\nfix.icon_slide=bogus\n", nullptr, &r);
    CHECK(s.fix.icon_slide);            // the default (ON): the surviving value is malformed
    CHECK_EQ(r.malformed, 1);
    // Three occurrences, the last one blank: a blank value is malformed too (not 0 / 1).
    r = Ext::LoadReport();
    s = ResolveText("ext.unit_b=1\next.unit_b=0\next.unit_b=\n", nullptr, &r);
    CHECK(!s.ext.unit_b);               // the default (OFF)
    CHECK_EQ(r.malformed, 1);
    // The profile key is read through the same lookup: the last one wins there as well.
    r = Ext::LoadReport();
    s = ResolveText("profile=faithful\nprofile=all\n", nullptr, &r);
    CHECK(r.profile == Ext::Profile::All);
    CHECK(s.fix.icon_slide && s.ext.unit_b && s.ext.unit_c);
}
