// tests/unit/ext_drain_split_test.cpp -- ext.drain_split (class B, HITCH_DRAINSPLIT.md):
//   the registry row (class B, format 1, default off, locked for the game, the pseudo-mod
//   ext:drain_split) and the owed AssignCitizens' bookkeeping: off owes nothing; on, a drain owes
//   one AssignCitizens that waits out the drain's frame and runs in the next frame's first tick,
//   or at once when the path-graph branch is due; a second drain while one is owed is refused
//   (an overlap); a new game, a load, the menu (UnlockForGame) or an Install between the halves
//   drops the debt; a different PathGraph never collects another's debt.
#include "unit.h"
#include "ext/drain_split/drain_split.h"
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

Ext::State On() {
    Ext::State s{};
    s.ext.drain_split = true;
    return s;
}
}  // namespace

UNIT_TEST(ext_drain_split_registry_row) {
    const Ext::Entry& e = E("drain_split");
    CHECK(e.tier == Ext::Tier::Extension);
    CHECK(e.cls == Ext::Class::B);           // the seats move a frame: the sim diverges
    CHECK(e.defaultOn);   // ruled 2026-11-08 for 1.0.3, DESIGN.md section 2
    CHECK(e.formatVersion == 1);
    CHECK(std::strstr(e.description, "iverges") != nullptr);
    CHECK(Ext::SettingsKey(e) == "ext.drain_split");

    Ext::Install(On(), Ext::Profile::PerEntry, false);
    int owner = 0;
    Ext::LockForGame(&owner);
    CHECK(Ext::EntryLocked(e));
    CHECK(!Ext::SetRequested(e, false));     // locked: the game keeps it
    CHECK(Ext::Ext().drain_split);
    std::vector<Ext::PseudoMod> mods((size_t)Ext::EntryCount());
    const int n = Ext::PseudoModsForSave(mods.data(), (int)mods.size());
    int saw = 0;
    for (int k = 0; k < n && k < (int)mods.size(); ++k)
        if (mods[(size_t)k].path == "ext:drain_split") { ++saw; CHECK(mods[(size_t)k].formatVersion == 1); }
    CHECK_EQ(saw, 1);
    Ext::UnlockForGame(&owner);
    Ext::Install(Ext::State{}, Ext::Profile::Faithful, false);
    CHECK(!Ext::Ext().drain_split);
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_drain_split_lock_epoch) {
    int a = 0, b = 0;
    const uint32_t e0 = Ext::LockEpoch();
    Ext::LockForGame(&a);
    const uint32_t e1 = Ext::LockEpoch();
    CHECK(e1 != e0);
    Ext::UnlockForGame(&b);                  // not the owner: ignored, epoch unchanged
    CHECK(Ext::LockEpoch() == e1);
    Ext::UnlockForGame(&a);
    const uint32_t e2 = Ext::LockEpoch();
    CHECK(e2 != e1);
    CHECK(Ext::LockForGameWithSet(&a, { "drain_split" }));
    CHECK(Ext::LockEpoch() != e2);
    CHECK(Ext::Ext().drain_split);
    const uint32_t e3 = Ext::LockEpoch();
    CHECK(!Ext::LockForGameWithSet(&a, { "no_such_row" }));   // refused: nothing changes
    CHECK(Ext::LockEpoch() == e3);
    Ext::UnlockForGame(&a);
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_drain_split_owed_half) {
    using namespace Ext::DrainSplit;
    int pgA = 0, pgB = 0, game = 0;

    // Off: BeginFrame reads the row; the seam never owes (g_on false), nothing is pending.
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
    BeginFrame();
    ResetStats();
    CHECK(!g_on);
    CHECK(!g_pending);
    CHECK(Take(&pgA, true) == Due::Wait);    // nothing owed: nothing runs
    CHECK_EQ(GetStats().runs, 0);

    // On, in a game.
    Ext::Install(On(), Ext::Profile::PerEntry, false);
    Ext::LockForGame(&game);
    BeginFrame();
    CHECK(g_on);

    // 1. The owed half waits out the drain's frame (speed N: N - 1 more ticks), runs in the next.
    CHECK(Owe(&pgA));
    CHECK(g_pending);
    CHECK(Take(&pgA, false) == Due::Wait);
    CHECK(Take(&pgA, false) == Due::Wait);
    CHECK(g_pending);
    BeginFrame();
    CHECK(Take(&pgA, false) == Due::Run);
    CHECK(!g_pending);
    CHECK(Take(&pgA, false) == Due::Wait);   // paid: nothing more
    CHECK_EQ(GetStats().runs, 1);
    CHECK_EQ(GetStats().maxGapTicks, 3);     // the run is the third Update after the drain
    CHECK_EQ(GetStats().gapTicks, 3);

    // 2. Speed 1 (the drain was the frame's only tick): the next Update is in the next frame.
    CHECK(Owe(&pgA));
    BeginFrame();
    CHECK(Take(&pgA, false) == Due::Run);
    CHECK_EQ(GetStats().gapTicks, 4);

    // 3. The path-graph branch due in the drain's frame: the owed half runs first, at once.
    CHECK(Owe(&pgA));
    CHECK(Take(&pgA, true) == Due::Run);
    CHECK_EQ(GetStats().runsForced, 1);

    // 4. A second drain while one is owed is refused (the caller runs AssignCitizens itself).
    CHECK(Owe(&pgA));
    CHECK(!Owe(&pgA));
    CHECK_EQ(GetStats().overlaps, 1);
    CHECK(g_pending);
    BeginFrame();
    CHECK(Take(&pgA, false) == Due::Run);

    // 5. Another PathGraph never collects the debt: it is dropped.
    CHECK(Owe(&pgA));
    BeginFrame();
    CHECK(Take(&pgB, false) == Due::Drop);
    CHECK(!g_pending);

    // 6. A load between the halves (a new lock, the save's B / C set): dropped, even for a
    //    PathGraph at the same address.
    CHECK(Owe(&pgA));
    CHECK(Ext::LockForGameWithSet(&game, { "drain_split" }));
    BeginFrame();
    CHECK(Take(&pgA, false) == Due::Drop);

    // 7. Back to the menu, then a new game, between the halves: dropped.
    CHECK(Owe(&pgA));
    Ext::UnlockForGame(&game);
    Ext::LockForGame(&game);
    BeginFrame();
    CHECK(Take(&pgA, true) == Due::Drop);

    // 8. The toggle while a debt is pending. In a game the class B row is locked, so the
    //    request waits; only a new lock or an Install changes the active value, and either drops
    //    the debt. Off afterwards, nothing is owed again.
    CHECK(Owe(&pgA));
    CHECK(!Ext::SetRequested(E("drain_split"), false));   // locked: still on for this game
    BeginFrame();
    CHECK(g_on);
    CHECK(Take(&pgA, false) == Due::Run);
    CHECK(Owe(&pgA));
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);   // the row off
    BeginFrame();
    CHECK(!g_on);
    CHECK(Take(&pgA, false) == Due::Drop);
    CHECK(!g_pending);

    const Stats& st = GetStats();
    CHECK_EQ(st.splits, 9);                  // every Owe that returned true
    CHECK_EQ(st.runs, 5);
    CHECK_EQ(st.dropped, 4);
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
    BeginFrame();
    ResetStats();
}
