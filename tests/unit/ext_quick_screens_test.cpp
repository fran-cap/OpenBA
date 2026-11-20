// ============================================================================
// tests/unit/ext_quick_screens_test.cpp -- ext.quick_screens (block SPLASH)
// ============================================================================
// The registry row (class A, default ON, off under /profile:faithful, on
// under profile=all, never a pseudo-mod) and the splash hold decision of
// src/ext/quick_screens/quick_screens.h: OFF is retail's `elapsed >= delay`
// whatever the work state; ON also ends the hold once the menu-scene build is
// done and the reveal fade finished, and keeps it ended once the fade-out ran.
// docs/extensions/handins/SPLASH.md.
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "ext/quick_screens/quick_screens.h"
#include "io/settings_file.h"

#include <string>

namespace {

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const IO::SettingsFile::Entries* entries = static_cast<const IO::SettingsFile::Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) { value = e.value; return true; }
    }
    return false;
}

bool Resolved(const char* text, const char* cmd = nullptr) {
    IO::SettingsFile::Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, cmd, nullptr).ext.quick_screens;
}

}  // namespace

UNIT_TEST(ext_quick_screens_row) {
    const Ext::Entry* e = Ext::FindEntry("quick_screens");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);   // class A ships ON (DESIGN.md section 2, ruled 2026-10-18)
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.quick_screens");

    CHECK(Resolved(""));                                       // class A: default ON
    CHECK(Resolved("ext.quick_screens=1\n"));
    CHECK(!Resolved("ext.quick_screens=0\n"));
    CHECK(Resolved("profile=all\n"));                          // the everything-on profile
    CHECK(!Resolved("ext.quick_screens=1\nprofile=faithful\n"));
    CHECK(!Resolved("ext.quick_screens=1\n", "faithful"));     // the gate's /profile:faithful

    // Class A: live, never in the save's mod list.
    Ext::State on{};
    on.ext.quick_screens = true;
    Ext::Install(on, Ext::Profile::PerEntry, false);
    CHECK(Ext::Ext().quick_screens);
    CHECK(Ext::PseudoModsForSave(nullptr, 0) == 0);
    Ext::LockForGame(&on);
    CHECK(!Ext::EntryLocked(*e));
    CHECK(Ext::SetRequested(*e, false));                       // takes effect at once under the lock
    CHECK(!Ext::Ext().quick_screens);
    Ext::UnlockForGame(&on);
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_quick_screens_off_is_retail_hold) {
    using Ext::QuickScreens::SplashHoldOver;
    // OFF: exactly `elapsed >= delay`, whatever the work and fade state.
    for (bool fadeOut : {false, true})
        for (bool pending : {false, true})
            for (float fade : {0.0f, 0.5f, 1.0f}) {
                CHECK(!SplashHoldOver(false, 0, 100, fadeOut, pending, fade));
                CHECK(!SplashHoldOver(false, 99, 100, fadeOut, pending, fade));
                CHECK(SplashHoldOver(false, 100, 100, fadeOut, pending, fade));
                CHECK(SplashHoldOver(false, 500, 100, fadeOut, pending, fade));
            }
}

UNIT_TEST(ext_quick_screens_on_waits_for_the_work) {
    using Ext::QuickScreens::SplashHoldOver;
    // ON, retail's hold not elapsed: over only when the menu-scene build is
    // done AND the reveal fade has finished ...
    CHECK(SplashHoldOver(true, 0, 100, false, false, 0.0f));
    CHECK(!SplashHoldOver(true, 0, 100, false, true, 0.0f));   // scene build armed / busy
    CHECK(!SplashHoldOver(true, 0, 100, false, false, 0.3f));  // still revealing
    CHECK(!SplashHoldOver(true, 0, 100, false, false, 1.0f));  // not yet revealed
    CHECK(!SplashHoldOver(true, 0, 100, false, true, 1.0f));
    // ... and stays over once the fade-out to black was kicked (fade rising).
    CHECK(SplashHoldOver(true, 0, 100, true, false, 0.4f));
    CHECK(SplashHoldOver(true, 0, 100, true, false, 1.0f));
    // ON never holds longer than retail.
    for (bool fadeOut : {false, true})
        for (bool pending : {false, true})
            for (float fade : {0.0f, 0.5f, 1.0f})
                CHECK(SplashHoldOver(true, 100, 100, fadeOut, pending, fade));
}

// The save list's in-frame loop: a fake clock that each wait and step advances.
UNIT_TEST(ext_quick_screens_list_pump_steps_within_the_budget) {
    using Ext::QuickScreens::PumpListSteps;
    // Every wait takes 1 ms and succeeds, every step advances: the loop runs
    // until the budget is spent and never starts a wait after it.
    {
        double now = 100.0;
        int waits = 0;
        const int steps = PumpListSteps(
            8.0, [&] { return now; },
            [&](double left) { CHECK(left > 0.0); ++waits; now += 1.0; return true; },
            [&] { return true; });
        CHECK(steps == 8);
        CHECK(waits == 8);
    }
    // A zero budget takes no step (the faithful arm never calls it at all).
    {
        int waits = 0;
        const int steps = PumpListSteps(
            0.0, [] { return 5.0; }, [&](double) { ++waits; return true; }, [] { return true; });
        CHECK(steps == 0);
        CHECK(waits == 0);
    }
}

UNIT_TEST(ext_quick_screens_list_pump_stops_on_busy_worker_or_finished_list) {
    using Ext::QuickScreens::PumpListSteps;
    // The worker is still busy when the wait ends: no step is taken on it.
    {
        double now = 0.0;
        int stepped = 0;
        const int steps = PumpListSteps(
            8.0, [&] { return now; },
            [&](double left) { now += left; return false; },
            [&] { ++stepped; return true; });
        CHECK(steps == 0);
        CHECK(stepped == 0);
    }
    // The list finishes (or a step changes nothing) on the third step: the loop
    // ends there with budget left.
    {
        double now = 0.0;
        int stepped = 0;
        const int steps = PumpListSteps(
            8.0, [&] { return now; },
            [&](double) { now += 0.5; return true; },
            [&] { return ++stepped < 3; });
        CHECK(steps == 3);
        CHECK(now < 8.0);
    }
    // The wait is offered only the time that is left.
    {
        double now = 0.0;
        double lastLeft = 0.0;
        PumpListSteps(
            8.0, [&] { return now; },
            [&](double left) { lastLeft = left; now += 3.0; return true; },
            [] { return true; });
        CHECK(lastLeft == 2.0);
    }
}

// Save::SaveLoad's gates wait while `_elapsed <= _minOperationTime`; the first
// SaveLoad::Update adds a positive frame delta before it dispatches, so a zero
// minimum never holds. A constant, so a static_assert, not a test.
static_assert(Ext::QuickScreens::kNoMinOperationTime == 0, "quick_screens: no minimum operation time");
