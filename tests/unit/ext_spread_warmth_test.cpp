// tests/unit/ext_spread_warmth_test.cpp -- ext.spread_warmth (class B, HITCH_PFBURST.md):
//   the registry row (class B, format 1, default off, locked for the game, the pseudo-mod
//   ext:spread_warmth), and the per-frame budget: off never defers and the meter is inert; on,
//   the first scan of a frame always runs, later scans run until the frame has charged
//   kMissBudget searches (the pair-cache ring advance, wrap included) or kCandidateBudget
//   candidates, the rest are deferred until the next BeginFrame, and the streak counter follows
//   frames with a deferral.
#include "unit.h"
#include "ext/spread_warmth/spread_warmth.h"
#include "ext/registry.h"

#include <cstring>
#include <string>
#include <vector>

namespace {
const Ext::Entry& E(const char* id) {
    const Ext::Entry* e = Ext::FindEntry(id);
    CHECK(e != nullptr);
    static Ext::Entry none{};
    return e ? *e : none;
}
}  // namespace

UNIT_TEST(ext_spread_warmth_registry_row) {
    const Ext::Entry& e = E("spread_warmth");
    CHECK(e.tier == Ext::Tier::Extension);
    CHECK(e.cls == Ext::Class::B);           // a deferred citizen diverges from retail
    CHECK(e.defaultOn);   // ruled 2026-11-08 for 1.0.3, DESIGN.md section 2
    CHECK(e.formatVersion == 1);
    CHECK(std::strstr(e.description, "iverges") != nullptr);
    CHECK(Ext::SettingsKey(e) == "ext.spread_warmth");

    Ext::State on{};
    e.slot(on) = true;
    Ext::Install(on, Ext::Profile::PerEntry, false);
    int owner = 0;
    Ext::LockForGame(&owner);
    CHECK(Ext::EntryLocked(e));
    CHECK(!Ext::SetRequested(e, false));     // locked: the game keeps it
    CHECK(Ext::Ext().spread_warmth);
    std::vector<Ext::PseudoMod> mods((size_t)Ext::EntryCount());
    const int n = Ext::PseudoModsForSave(mods.data(), (int)mods.size());
    int saw = 0;
    for (int k = 0; k < n && k < (int)mods.size(); ++k)
        if (mods[(size_t)k].path == "ext:spread_warmth") { ++saw; CHECK(mods[(size_t)k].formatVersion == 1); }
    CHECK_EQ(saw, 1);
    Ext::UnlockForGame(&owner);
    Ext::Install(Ext::State{}, Ext::Profile::Faithful, false);
    CHECK(!Ext::Ext().spread_warmth);
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
}

namespace {
struct FakeCache {   // the two fields ScanMeter reads (RegionPairCache::newest / ring.size)
    int newest = 0;
    struct { int size = 0x20000; } ring;
};
}  // namespace

UNIT_TEST(ext_spread_warmth_budget) {
    using namespace Ext::SpreadWarmth;
    // Off: never defers, charges nothing, the meter is inert.
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
    BeginFrame();
    ResetStats();
    FakeCache c;
    for (int i = 0; i < 100; ++i) {
        CHECK(!Defer(kCandidateBudget));
        ScanMeter<FakeCache> m(&c);
        c.newest += kMissBudget;
    }
    CHECK_EQ(GetStats().scans, 0);
    CHECK_EQ(GetStats().searches, 0);

    Ext::State on{};
    on.ext.spread_warmth = true;
    Ext::Install(on, Ext::Profile::PerEntry, false);
    BeginFrame();
    ResetStats();
    // The first scan always runs, even over both budgets.
    CHECK(!Defer(kCandidateBudget * 3));
    { ScanMeter<FakeCache> m(&c); c.newest += kMissBudget * 2; }
    CHECK(Defer(1));
    // Frame 2: searches decide. 300 candidates, 200 searches per scan: the third scan brings
    // the frame to 600 >= 512, the fourth is deferred.
    BeginFrame();
    int served = 0;
    while (!Defer(300)) { ScanMeter<FakeCache> m(&c); c.newest += 200; ++served; }
    CHECK_EQ(served, 3);
    // Frame 3: candidates decide (all hits: no search). 4096 / 1000 -> 5 scans pass.
    BeginFrame();
    served = 0;
    while (!Defer(1000)) { ScanMeter<FakeCache> m(&c); ++served; }
    CHECK_EQ(served, 5);
    // Frame 4: the ring wraps inside a scan; the advance is still the search count.
    BeginFrame();
    c.newest = c.ring.size - 10;
    CHECK(!Defer(10));
    { ScanMeter<FakeCache> m(&c); c.newest = 30; }
    BeginFrame();                             // frame 5: frame 4 had no deferral
    const Stats& st = GetStats();
    CHECK_EQ(st.searches, (long long)(kMissBudget * 2 + 600 + 40));
    CHECK_EQ(st.maxFrameSearches, kMissBudget * 2);
    CHECK_EQ(st.deferrals, 3);
    CHECK_EQ(st.framesDeferring, 3);
    CHECK_EQ(st.maxStreak, 3);
    CHECK_EQ(st.scans, 1 + 3 + 5 + 1);
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
    BeginFrame();
}
