// ============================================================================
// tests/unit/ext_walk_batch_grid_test.cpp -- ext.walk_batch_grid (class B): the registry row
// ============================================================================
// Class B, formatVersion 1, default off, locked for the game, the pseudo-mod
// ext:walk_batch_grid, a manifest naming it accepted, refused on load when the player has it
// off. The batched walk itself (both rows, one body) is tests/unit/ext_walk_batch_test.cpp.
// docs/extensions/handins/WALKGRID.md
// ============================================================================
#include "unit.h"
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

UNIT_TEST(ext_walk_batch_grid_registry_row) {
    const Ext::Entry& e = E("walk_batch_grid");
    CHECK(e.tier == Ext::Tier::Extension);
    CHECK(e.cls == Ext::Class::B);          // the grid lags inside the frame (WALKGRID.md)
    CHECK(e.defaultOn);   // ruled 2026-11-08 for 1.0.3, DESIGN.md section 2
    CHECK(e.formatVersion == 1);            // pseudo-mod ModInfo +0x18, the save manifest
    CHECK(e.displayName != nullptr && e.description != nullptr);
    CHECK(std::strstr(e.description, "iverges from the original") != nullptr);
    CHECK(std::strstr(e.description, "a frame late") != nullptr);
    CHECK(Ext::SettingsKey(e) == "ext.walk_batch_grid");
    const Ext::Entry& a = E("walk_batch");

    Ext::State s{};
    CHECK(!s.ext.walk_batch_grid);
    // Requested on, the class A row off: the B row alone is the batch (walk_batch.h).
    Ext::State on{};
    e.slot(on) = true;
    Ext::Install(on, Ext::Profile::PerEntry, false);
    CHECK(Ext::Ext().walk_batch_grid && !Ext::Ext().walk_batch);
    int owner = 0;
    Ext::LockForGame(&owner);
    CHECK(Ext::EntryLocked(e) && !Ext::EntryLocked(a));
    CHECK(!Ext::SetRequested(e, false));     // locked: the game keeps it
    CHECK(Ext::Ext().walk_batch_grid);
    CHECK(!Ext::Requested(e));
    // The save carries it as the pseudo-mod ext:walk_batch_grid (formatVersion 1, +0x14),
    // never the class A row.
    std::vector<Ext::PseudoMod> mods((size_t)Ext::EntryCount());
    const int n = Ext::PseudoModsForSave(mods.data(), (int)mods.size());
    int sawGrid = 0, sawA = 0;
    for (int k = 0; k < n && k < (int)mods.size(); ++k) {
        if (mods[(size_t)k].path == "ext:walk_batch_grid") {
            ++sawGrid;
            CHECK(mods[(size_t)k].formatVersion == 1);
            CHECK(mods[(size_t)k].gameVersion == Ext::kPseudoModField14);
            CHECK(mods[(size_t)k].versionMajor == 0);
            CHECK(mods[(size_t)k].name == e.displayName);
        }
        if (mods[(size_t)k].path == "ext:walk_batch") ++sawA;
    }
    CHECK_EQ(sawGrid, 1);
    CHECK_EQ(sawA, 0);
    // The load notice: the player has it off (requested false) -> modsMissing (1).
    CHECK(Ext::PseudoModLoadStatus("ext:walk_batch_grid", 1) == 1);
    Ext::UnlockForGame(&owner);
    CHECK(!Ext::Ext().walk_batch_grid);      // back at the menu: the request (off)
    CHECK(Ext::SetRequested(e, true));       // unlocked: takes effect now
    CHECK(Ext::PseudoModLoadStatus("ext:walk_batch_grid", 1) == 0);
    CHECK(Ext::PseudoModLoadStatus("ext:walk_batch_grid", 2) == 1);   // a newer format
    // A manifest naming it locks exactly it on, whatever is requested.
    CHECK(Ext::SetRequested(e, false));
    std::vector<std::string> ids = { "walk_batch_grid" };
    CHECK(Ext::LockForGameWithSet(&owner, ids));
    CHECK(Ext::Ext().walk_batch_grid);
    Ext::UnlockForGame(&owner);
    CHECK(!Ext::Ext().walk_batch_grid);
    // The faithful profile turns it off, and its absence writes no pseudo-mod.
    Ext::Install(Ext::State{}, Ext::Profile::Faithful, false);
    CHECK(Ext::PseudoModsForSave(nullptr, 0) == 0);
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
}
