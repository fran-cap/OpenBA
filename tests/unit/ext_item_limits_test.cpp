// ============================================================================
// tests/unit/ext_item_limits_test.cpp -- ext.item_limits
// ============================================================================
// src/ext/item_limits/item_limits.{h,cpp}, save_state.cpp without the game: the registry row (class C, off
// by default), the list of item limits (template paths, case-insensitive, one limit per item), the save-tail
// codec (round trip, empty state, every kind of damaged payload, a refused payload leaving the old list
// alone), the resolution into the flat array the seams scan, the count and the limit test, `g_on` following
// the row and the list, and the lock-owner rule for the reset. The seams, the factory walk and the panel row
// are the exe's (item_limits_town.cpp, panel_row.cpp); PanelsForget is a counting stand-in here.
// ============================================================================
#include "unit.h"
#include "ext/item_limits/item_limits.h"
#include "ext/registry.h"
#include "ext/savetail.h"
#include "io/settings_file.h"

#include <string>
#include <utility>
#include <vector>

// The exe's PanelsForget is panel_row.cpp's (the UI side, not in the unit build): this one counts its calls.
namespace Ext {
namespace ItemLimits {
int g_panelsForgotten = 0;
void PanelsForget() {
    ++g_panelsForgotten;
}
}  // namespace ItemLimits
}  // namespace Ext

namespace {

using IO::SettingsFile::Entries;
using namespace Ext::ItemLimits;

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

const Ext::SaveTail::StateCodec* TheCodec() {
    return Ext::SaveTail::FindCodec("item_limits");
}

// A payload built by hand: `count`, then { path, limit } per entry.
std::vector<uint8_t> Payload(uint32_t count, const std::vector<std::pair<std::string, int32_t>>& entries) {
    std::vector<uint8_t> bytes;
    Ext::SaveTail::Writer w(bytes);
    w.U32(count);
    for (const auto& e : entries) {
        w.String(e.first);
        w.I32(e.second);
    }
    return bytes;
}

const std::string kSteel = "Mods\\Steel.rsc:rawmaterial";
const std::string kIron = "Template\\RawMaterialIronTools.rsc:rawmaterial";

}  // namespace

UNIT_TEST(ext_item_limits_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("item_limits");
    CHECK(e != nullptr);
    if (!e) {
        return;
    }
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::C);   // per-town state: the limits are recorded in the save
    CHECK(!e->defaultOn);
    CHECK(e->formatVersion == 1);
    CHECK(Ext::SettingsKey(*e) == "ext.item_limits");
    CHECK(std::string(kId) == e->id);
    CHECK(!ResolveText("").ext.item_limits);
    CHECK(ResolveText("ext.item_limits=1\n").ext.item_limits);
    CHECK(!ResolveText("profile=faithful\n").ext.item_limits);
    CHECK(TheCodec() != nullptr);
}

UNIT_TEST(ext_item_limits_one_limit_per_item) {
    ResetState();
    int32_t v = -1;
    CHECK(!LimitOf(kSteel, v));
    CHECK(SetLimit(kSteel, 50));
    CHECK(!SetLimit(kSteel, 50));                                      // the same value: nothing changes
    CHECK(LimitOf("mods\\STEEL.rsc:RawMaterial", v) && v == 50);       // paths compare without case
    CHECK(SetLimit("MODS\\STEEL.RSC:RAWMATERIAL", 80));                // a second panel edits the same limit
    CHECK(Limits().size() == 1 && Limits()[0].path == kSteel && Limits()[0].limit == 80);
    CHECK(SetLimit(kIron, -5));                                        // clamped into range
    CHECK(LimitOf(kIron, v) && v == 0);
    CHECK(SetLimit(kIron, 2000000000));
    CHECK(LimitOf(kIron, v) && v == kMaxLimit);
    CHECK(ClearLimit("mods\\steel.rsc:rawmaterial"));
    CHECK(!ClearLimit(kSteel));
    CHECK(!LimitOf(kSteel, v) && Limits().size() == 1);
    CHECK(!SetLimit("", 10));                                          // no path: nothing to key on
    ResetState();
    CHECK(Limits().empty());
}

UNIT_TEST(ext_item_limits_state_codec_round_trip) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    ResetState();
    CHECK(SetLimit(kSteel, 50));
    CHECK(SetLimit(kIron, 0));
    CHECK(SetLimit("Fish, Smoked;A#B:C/\xe9", kMaxLimit));
    std::vector<uint8_t> payload;
    c->encode(payload);
    const std::vector<Limit> before = Limits();
    c->reset();
    CHECK(Limits().empty());
    CHECK(c->decode(payload.data(), payload.size(), 1));
    CHECK(Limits().size() == before.size());
    for (size_t i = 0; i < before.size() && i < Limits().size(); ++i) {
        CHECK(Limits()[i].path == before[i].path && Limits()[i].limit == before[i].limit);
    }
    // an empty list is still a valid record
    c->reset();
    payload.clear();
    c->encode(payload);
    CHECK(payload.size() == 4);
    CHECK(c->decode(payload.data(), payload.size(), 1) && Limits().empty());
    ResetState();
}

UNIT_TEST(ext_item_limits_state_codec_refuses_every_damaged_payload) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    ResetState();
    CHECK(SetLimit(kIron, 7));
    const std::vector<std::vector<uint8_t>> bad = {
        Payload(2, {{kSteel, 1}}),                                     // a count the payload cannot hold
        Payload(1, {{"", 1}}),                                         // an empty path
        Payload(1, {{std::string(1025, 'a'), 1}}),                     // an over-long path
        Payload(1, {{"bad\npath", 1}}),                                // a control character
        Payload(1, {{kSteel, -1}}),                                    // a negative limit
        Payload(1, {{kSteel, kMaxLimit + 1}}),                         // a limit over the ceiling
        Payload(2, {{kSteel, 1}, {"MODS\\STEEL.RSC:RAWMATERIAL", 2}}), // the same item twice
    };
    for (const std::vector<uint8_t>& p : bad) {
        CHECK(!c->decode(p.data(), p.size(), 1));
    }
    std::vector<uint8_t> extra = Payload(1, {{kSteel, 1}});
    extra.push_back(0);                                                // bytes left over
    CHECK(!c->decode(extra.data(), extra.size(), 1));
    std::vector<uint8_t> cut = Payload(1, {{kSteel, 1}});
    cut.pop_back();                                                    // cut short
    CHECK(!c->decode(cut.data(), cut.size(), 1));
    const std::vector<uint8_t> good = Payload(1, {{kSteel, 1}});
    CHECK(!c->decode(good.data(), good.size(), 2));                    // a version this build does not know
    int32_t v = 0;
    CHECK(Limits().size() == 1 && LimitOf(kIron, v) && v == 7);       // every refusal left the old list alone
    ResetState();
}

UNIT_TEST(ext_item_limits_state_travels_in_the_tail) {
    ResetState();
    CHECK(SetLimit(kSteel, 50));
    Ext::State s = ResolveText("ext.item_limits=1\n");
    Ext::Install(s, Ext::Profile::PerEntry, false);
    Ext::LockForGame(&s);
    std::vector<Ext::SaveTail::Record> recs = Ext::SaveTail::ActiveRecords();
    bool found = false;
    for (const Ext::SaveTail::Record& r : recs) {
        if (r.id == "item_limits") {
            found = true;
            CHECK(!r.payload.empty() && r.formatVersion == 1);
        }
    }
    CHECK(found);
    const std::vector<uint8_t> tail = Ext::SaveTail::Encode(recs);
    ResetState();
    std::vector<Ext::SaveTail::Record> back;
    CHECK(Ext::SaveTail::Decode(tail.data(), tail.size(), back) == Ext::SaveTail::Status::Ok);
    std::string offending;
    CHECK(Ext::SaveTail::Validate(back, &offending) == Ext::SaveTail::Status::Ok);
    CHECK(Ext::SaveTail::ApplyState(back, &offending));
    int32_t v = 0;
    CHECK(LimitOf(kSteel, v) && v == 50);                              // the town's limit came back with the save
    Ext::UnlockForGame(&s);                                            // the game ends: the state goes with it
    CHECK(Limits().empty());
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_item_limits_the_faithful_profile_writes_no_record) {
    ResetState();
    CHECK(SetLimit(kSteel, 50));
    Ext::State s = ResolveText("profile=faithful\next.item_limits=1\n");
    CHECK(!s.ext.item_limits);
    Ext::Install(s, Ext::Profile::Faithful, false);
    Ext::LockForGame(&s);
    for (const Ext::SaveTail::Record& r : Ext::SaveTail::ActiveRecords()) {
        CHECK(r.id != "item_limits");
    }
    CHECK(Ext::SaveTail::EncodeActive().empty());
    Ext::UnlockForGame(&s);
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
    ResetState();
}

UNIT_TEST(ext_item_limits_the_reset_follows_the_lock_owner) {
    ResetState();
    int oldGame = 0, newGame = 0;
    Ext::State s = ResolveText("ext.item_limits=1\n");
    Ext::Install(s, Ext::Profile::PerEntry, false);
    Ext::LockForGame(&oldGame);
    Ext::LockForGame(&newGame);
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    const std::vector<uint8_t> payload = Payload(1, {{kSteel, 50}});
    CHECK(c->decode(payload.data(), payload.size(), 1));
    Ext::UnlockForGame(&oldGame);                                      // not the owner: ignored
    CHECK(Limits().size() == 1);
    Ext::UnlockForGame(&newGame);                                      // the owner: the town is over
    CHECK(Limits().empty());
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_item_limits_resolves_counts_and_tests_the_limit) {
    ResetState();
    int steel = 0, steel2 = 0, iron = 0, stone = 0;
    const std::vector<Candidate> game = {
        { &steel, kSteel },
        { &steel2, "MODS\\STEEL.RSC:RAWMATERIAL" },                    // a second template of the same material
        { &iron, kIron },
        { &stone, "Template\\RawMaterialStone.rsc:rawmaterial" },
    };
    CHECK(SetLimit(kSteel, 3));
    CHECK(NeedsResolve(1));
    Resolve(1, game);
    CHECK(!NeedsResolve(1));
    CHECK(g_items.size() == 2);                                        // both steel templates, not iron or stone
    BeginCount();
    Tally(&steel, 2);
    Tally(&iron, 100);                                                 // not limited: ignored
    CHECK(!AtLimit(&steel));
    Tally(&steel, 1);
    CHECK(AtLimit(&steel));                                            // retail's `limit <= stock`
    CHECK(AtLimit(&steel2));                                           // the same path: the same count
    CHECK(!AtLimit(&iron) && !AtLimit(nullptr));
    // a limit edited between the count and a workshop's test keeps the count
    CHECK(SetLimit(kSteel, 4));
    CHECK(NeedsResolve(1));
    Resolve(1, game);
    CHECK(!AtLimit(&steel));
    CHECK(SetLimit(kSteel, 3));
    Resolve(1, game);
    CHECK(AtLimit(&steel));
    BeginCount();                                                      // the next tick counts from zero
    CHECK(!AtLimit(&steel));
    // a limit of zero stops the item at once
    CHECK(SetLimit(kIron, 0));
    Resolve(1, game);
    CHECK(AtLimit(&iron));
    CHECK(NeedsResolve(2));                                            // a new game, a load or a reload
    // a path the running game does not have limits nothing, and stays in the list
    CHECK(ClearLimit(kIron));
    CHECK(ClearLimit(kSteel));
    CHECK(SetLimit("Mods\\Gone.rsc:rawmaterial", 1));
    Resolve(2, game);
    CHECK(g_items.empty() && Limits().size() == 1);
    ResetState();
}

UNIT_TEST(ext_item_limits_counts_every_template_of_a_path_together) {
    ResetState();
    int steel = 0, steelVariant = 0, iron = 0;
    const std::vector<Candidate> game = {
        { &steel, kSteel },
        { &steelVariant, "MODS\\STEEL.RSC:RAWMATERIAL" },            // a mod's variant on the same material
        { &iron, kIron },
    };
    CHECK(SetLimit(kSteel, 5));
    CHECK(SetLimit(kIron, 100));
    Resolve(1, game);
    CHECK(g_items.size() == 3);
    BeginCount();
    Tally(&steel, 3);
    Tally(&steelVariant, 1);
    CHECK(!AtLimit(&steel) && !AtLimit(&steelVariant));               // 4 of 5, both templates together
    Tally(&steelVariant, 1);
    CHECK(AtLimit(&steel) && AtLimit(&steelVariant));                 // 5: every workshop making either stops
    CHECK(!AtLimit(&iron));
    // a path joining the list moves the entries; the counts stay with their paths
    CHECK(ClearLimit(kIron));
    CHECK(SetLimit(kIron, 1));
    Resolve(1, game);
    CHECK(AtLimit(&steel) && AtLimit(&steelVariant) && !AtLimit(&iron));
    Tally(&iron, 1);
    CHECK(AtLimit(&iron) && AtLimit(&steel));
    ResetState();
}

UNIT_TEST(ext_item_limits_a_reset_forgets_the_panel_rows) {
    const int before = g_panelsForgotten;
    ResetState();                                                      // the game ends or another starts
    CHECK(g_panelsForgotten == before + 1);
}

UNIT_TEST(ext_item_limits_a_value_change_needs_no_walk) {
    ResetState();
    int steel = 0, iron = 0;
    const std::vector<Candidate> game = { { &steel, kSteel }, { &iron, kIron } };
    CHECK(SetLimit(kSteel, 3));
    CHECK(SetLimit(kIron, 7));
    CHECK(NeedsWalk(1));
    Resolve(1, game);
    CHECK(!NeedsWalk(1) && !NeedsResolve(1));
    BeginCount();
    Tally(&steel, 3);
    Tally(&iron, 5);
    CHECK(AtLimit(&steel) && !AtLimit(&iron));
    // a spinner edit: the value changes, no path joins or leaves the list
    CHECK(SetLimit(kSteel, 4));
    CHECK(SetLimit(kIron, 5));
    CHECK(NeedsResolve(1) && !NeedsWalk(1));
    RefreshLimits();
    CHECK(!NeedsResolve(1));
    CHECK(!AtLimit(&steel) && AtLimit(&iron));                         // the new values, the counts kept
    // the same path in another case is the same entry: still no walk
    CHECK(SetLimit("MODS\\STEEL.RSC:RAWMATERIAL", 2));
    CHECK(!NeedsWalk(1) && Limits().size() == 2);
    RefreshLimits();
    BeginCount();
    Tally(&steel, 2);
    CHECK(AtLimit(&steel));
    // a path that joins or leaves the list needs the walk; so does a new epoch
    CHECK(ClearLimit(kIron));
    CHECK(NeedsWalk(1));
    Resolve(1, game);
    CHECK(!AtLimit(&iron) && g_items.size() == 1);
    CHECK(NeedsWalk(2));
    ResetState();
}

UNIT_TEST(ext_item_limits_g_on_follows_the_row_and_the_list) {
    ResetState();
    CHECK(!g_on && !PanelActive());
    int steel = 0;
    const std::vector<Candidate> game = { { &steel, kSteel } };
    SetRowActive(true);                                                // the row on with no limit: the panel
    CHECK(!g_on && PanelActive());                                     // row is offered, the seams stay off
    CHECK(SetLimit(kSteel, 5));
    Resolve(1, game);
    SetRowActive(false);
    CHECK(!g_on && !PanelActive());
    SetRowActive(true);
    CHECK(g_on);
    ResetState();                                                      // the game ends: everything is cleared
    CHECK(!g_on && !PanelActive() && g_items.empty());
}
