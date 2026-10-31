// ============================================================================
// tests/unit/ext_item_priority_test.cpp -- ext.item_priority
// ============================================================================
// src/ext/item_priority/item_priority.{h,cpp}, save_state.cpp without the game: the registry row (class C,
// off by default), the lists (rank, unrank, move, by template path without case), the resolution of the lists
// into the flat arrays the seams scan, `g_on` and Active following the row and the lists, the tier search
// (one search with no list or the row off, else one per rank with the ceiling set, then an unconstrained
// one), the save-tail codec (round trip with awkward paths, empty state, every kind of damaged payload, a
// refused payload leaving the old lists alone) and the state travelling in the tail. The seams themselves and
// the factory walk are the exe's (item_priority_town.cpp).
// ============================================================================
#include "unit.h"
#include "ext/item_priority/item_priority.h"
#include "ext/registry.h"
#include "ext/savetail.h"
#include "io/settings_file.h"

#include <string>
#include <utility>
#include <vector>

namespace {

using IO::SettingsFile::Entries;
using namespace Ext::ItemPriority;

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

void InstallText(const char* text) {
    Ext::Install(ResolveText(text), Ext::Profile::PerEntry, false);
}

const Ext::SaveTail::StateCodec* TheCodec() {
    return Ext::SaveTail::FindCodec("item_priority");
}

const std::string kSteel = "Template\\RawMaterialSteelTool.rsc:rawmaterial";
const std::string kIron = "Template\\RawMaterialIronTool.rsc:rawmaterial";
const std::string kBronze = "Template\\RawMaterialBronzeTool.rsc:rawmaterial";
const std::string kCoal = "Template\\RawMaterialCoal.rsc:rawmaterial";
const std::string kWood = "Template\\RawMaterialWood.rsc:rawmaterial";
const std::string kCoat = "Mods\\Winter, Coat.rsc:rawmaterial";   // a comma, a dot and a space in the path

// A payload built by hand: `categories`, then per list a count and { kind, path } entries (the first list is
// Fuel, category 1).
typedef std::vector<std::pair<uint8_t, std::string>> PathList;

std::vector<uint8_t> Payload(uint32_t categories, const std::vector<PathList>& per) {
    std::vector<uint8_t> bytes;
    Ext::SaveTail::Writer w(bytes);
    w.U32(categories);
    for (const auto& list : per) {
        w.U32(static_cast<uint32_t>(list.size()));
        for (const auto& e : list) {
            w.Bytes(&e.first, 1);
            w.String(e.second);
        }
    }
    return bytes;
}

}  // namespace

UNIT_TEST(ext_item_priority_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("item_priority");
    CHECK(e != nullptr);
    if (!e) {
        return;
    }
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::C);                    // the lists are the town's state: recorded in the save
    CHECK(!e->defaultOn);
    CHECK(e->formatVersion == 2);                      // v2 added the Never list; v1 payloads still load
    CHECK(Ext::SettingsKey(*e) == "ext.item_priority");
    CHECK(std::string(kId) == e->id);
    CHECK(!ResolveText("").ext.item_priority);
    CHECK(ResolveText("ext.item_priority=1\n").ext.item_priority);
    CHECK(!ResolveText("profile=faithful\n").ext.item_priority);   // the faithful profile turns it off
    CHECK(TheCodec() != nullptr);
}

UNIT_TEST(ext_item_priority_categories_and_flag_bits) {
    CHECK_EQ(FlagBit(kFuel), 6);
    CHECK_EQ(FlagBit(kTool), 9);
    CHECK_EQ(FlagBit(kHealth), 13);
    CHECK_EQ(FlagBit(kClothing), 14);
    CHECK_EQ(FlagBit(0), -1);                          // 0 is eating: the foods extension's, not this one's
    CHECK_EQ(FlagBit(5), -1);
    CHECK(IsCategory(1) && IsCategory(4) && !IsCategory(0) && !IsCategory(5));
}

UNIT_TEST(ext_item_priority_rank_unrank_move) {
    Lists l;
    CHECK(l.Empty());
    CHECK(Rank(l, kTool, kSteel));
    CHECK(Rank(l, kTool, kIron));
    CHECK(Rank(l, kTool, kBronze));
    CHECK(!Rank(l, kTool, kIron));                                       // already ranked
    CHECK(!Rank(l, kTool, "TEMPLATE\\RAWMATERIALIRONTOOL.RSC:RAWMATERIAL"));   // paths compare without case
    CHECK(!Rank(l, kTool, ""));                                          // no path
    CHECK(!Rank(l, 0, "x") && !Rank(l, 5, "x"));
    CHECK_EQ(RankOf(l, kTool, kSteel), 0);
    CHECK_EQ(RankOf(l, kTool, kBronze), 2);
    CHECK_EQ(RankOf(l, kTool, "template\\rawmaterialbronzetool.rsc:rawmaterial"), 2);
    CHECK_EQ(RankOf(l, kTool, "nothing"), kUnranked);
    CHECK_EQ(RankOf(l, kFuel, kSteel), kUnranked);                       // lists are per category
    CHECK(!l.Empty());
    CHECK(Move(l, kTool, kBronze, -1));                                  // up: swaps with its neighbour
    CHECK_EQ(RankOf(l, kTool, kBronze), 1);
    CHECK_EQ(RankOf(l, kTool, kIron), 2);
    CHECK(!Move(l, kTool, kSteel, -1));                                  // already first
    CHECK(!Move(l, kTool, kIron, +1));                                   // already last
    CHECK(!Move(l, kTool, "nothing", +1));
    CHECK(!Move(l, kTool, kIron, 2));                                    // only one step at a time
    CHECK(Unrank(l, kTool, kBronze.c_str()));
    CHECK_EQ(RankOf(l, kTool, kIron), 1);                                // the list closes up
    CHECK(!Unrank(l, kTool, kBronze));
}

UNIT_TEST(ext_item_priority_resolves_the_lists_into_flat_arrays) {
    ResetState();
    int steel = 0, iron = 0, bronze = 0, coal = 0, wood = 0;
    const std::vector<Candidate> game = {
        { &steel, kSteel },
        { &iron, "TEMPLATE\\RAWMATERIALIRONTOOL.RSC:RAWMATERIAL" },       // the game's own spelling differs
        { &bronze, kBronze },
        { &coal, kCoal },
        { &wood, kWood },
    };
    CHECK(NeedsResolve(1));                                              // nothing resolved yet
    Lists l;
    Rank(l, kTool, kIron);
    Rank(l, kTool, kSteel);
    Rank(l, kFuel, kCoal);
    Apply(l);
    Resolve(1, game);
    CHECK(!NeedsResolve(1));
    CHECK(g_ranked[kTool - 1].size() == 2);
    CHECK_EQ(RankOfMat(kTool, &iron), 0);                                // the list's order is the rank
    CHECK_EQ(RankOfMat(kTool, &steel), 1);
    CHECK_EQ(RankOfMat(kTool, &bronze), kUnranked);                      // not named: ranked last
    CHECK_EQ(RankOfMat(kFuel, &coal), 0);
    CHECK_EQ(RankOfMat(kFuel, &wood), kUnranked);
    CHECK_EQ(RankOfMat(kTool, &coal), kUnranked);                        // lists are per category
    CHECK_EQ(RankOfMat(kTool, nullptr), kUnranked);
    CHECK(NeedsResolve(2));                                              // a new game, a load or a reload
    Resolve(2, game);
    CHECK(!NeedsResolve(2));
    CHECK(Move(l, kTool, kSteel, -1));
    Apply(l);                                                            // a list changed
    CHECK(NeedsResolve(2));
    Resolve(2, game);
    CHECK_EQ(RankOfMat(kTool, &steel), 0);
    CHECK_EQ(RankOfMat(kTool, &iron), 1);
    // a path the game does not have ranks nothing, stays in the list, and does not shift the others
    Rank(l, kTool, "Mods\\Gone.rsc:rawmaterial");
    Apply(l);
    Resolve(2, game);
    CHECK(g_ranked[kTool - 1].size() == 2);
    CHECK(Current().Of(kTool).size() == 3);
    ResetState();
}

UNIT_TEST(ext_item_priority_inert_unless_the_row_is_active_and_a_list_resolves) {
    ResetState();
    SetRowActive(true);
    // row on, nothing ranked: retail's choice
    CHECK(!g_on);
    CHECK(!Active(kTool));
    int iron = 0;
    const std::vector<Candidate> game = { { &iron, kIron } };
    Lists l;
    Rank(l, kTool, kIron);
    Rank(l, kFuel, kCoal);                                               // a name the game does not have
    Apply(l);
    Resolve(1, game);
    SetRowActive(false);                                                 // the row off: nothing is narrowed
    CHECK(!g_on && !Active(kTool));
    SetRowActive(true);
    CHECK(g_on);
    CHECK(Active(kTool));
    // a list that resolved nothing is inert
    CHECK(!Active(kFuel));
    CHECK(!Active(kHealth) && !Active(0) && !Active(5));
    ResetState();                                                        // the game ends: g_on is cleared
    CHECK(!g_on && !Active(kTool) && g_ranked[kTool - 1].empty());
}

namespace {
struct Searches {
    std::vector<int> ceilings;      // the ceiling each finder call saw
};
}  // namespace

UNIT_TEST(ext_item_priority_tier_search_runs_best_first_and_stops_at_the_first_hit) {
    ResetState();
    int steel = 0, iron = 0, bronze = 0;
    const std::vector<Candidate> game = { { &steel, kSteel }, { &iron, kIron }, { &bronze, kBronze } };
    Lists l;
    Rank(l, kTool, kSteel);
    Rank(l, kTool, kIron);
    Rank(l, kTool, kBronze);
    Apply(l);
    Resolve(1, game);
    SetRowActive(true);
    CHECK(g_on);

    Searches s;
    const bool found = FindTiered(kTool, [&]() -> bool {                 // the second tier finds a storage
        s.ceilings.push_back(Ceiling());
        return s.ceilings.size() == 2;
    });
    CHECK(found);
    CHECK(s.ceilings.size() == 2 && s.ceilings[0] == 0 && s.ceilings[1] == 1);   // ranks 0, then 0..1
    // the ceiling is back to none afterwards
    CHECK_EQ(Ceiling(), kUnranked);

    // nothing anywhere: every tier, then the last
    Searches none;
    const bool nothing = FindTiered(kTool, [&]() -> bool {
        none.ceilings.push_back(Ceiling());
        return false;
    });
    CHECK(!nothing);
    CHECK(none.ceilings.size() == 4);                                    // 3 ranks + the unconstrained search
    CHECK(none.ceilings[0] == 0 && none.ceilings[1] == 1 && none.ceilings[2] == 2 &&
          none.ceilings[3] == kUnranked);
    CHECK_EQ(Ceiling(), kUnranked);

    // a category with no list: retail's one search
    Searches other;
    const bool fuel = FindTiered(kFuel, [&]() -> bool {
        other.ceilings.push_back(Ceiling());
        return true;
    });
    CHECK(fuel);
    CHECK(other.ceilings.size() == 1 && other.ceilings[0] == kUnranked);

    // the row off: one search, no ceiling
    SetRowActive(false);
    Searches off;
    FindTiered(kTool, [&]() -> bool {
        off.ceilings.push_back(Ceiling());
        return false;
    });
    CHECK(off.ceilings.size() == 1 && off.ceilings[0] == kUnranked);
    ResetState();
}

UNIT_TEST(ext_item_priority_state_codec_round_trip_with_awkward_paths) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    ResetState();
    Lists l;
    Rank(l, kTool, kSteel);
    Rank(l, kTool, kIron);
    Rank(l, kFuel, kCoal);
    Rank(l, kClothing, kCoat);
    Rank(l, kClothing, "A;B#C:D/E");
    Apply(l);
    const Lists before = Current();
    std::vector<uint8_t> payload;
    c->encode(payload);
    CHECK(!payload.empty());
    c->reset();                                                          // a new game: nothing carried over
    CHECK(Current().Empty());
    CHECK(c->decode(payload.data(), payload.size(), 1));
    // exactly the paths that went in, in order: no comma, hash, dot, colon or prefix is read as grammar
    for (int category = 1; category <= kCategories; ++category) {
        CHECK(Current().Of(category) == before.Of(category));
    }
    std::vector<uint8_t> again;
    c->encode(again);
    CHECK(again == payload);                                             // stable: encode(decode(x)) == x
    c->reset();
}

UNIT_TEST(ext_item_priority_state_codec_empty_state_is_still_a_valid_record) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    if (!c) {
        CHECK(false);
        return;
    }
    c->reset();
    std::vector<uint8_t> payload;
    c->encode(payload);
    // 4 categories + 4 counts, 4 bytes each
    CHECK_EQ(payload.size(), 20u);
    Lists l;
    Rank(l, kTool, kIron);
    Apply(l);
    CHECK(c->decode(payload.data(), payload.size(), 1));                 // an empty record empties the lists
    CHECK(Current().Empty());
}

UNIT_TEST(ext_item_priority_state_codec_refuses_every_damaged_payload) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    if (!c) {
        CHECK(false);
        return;
    }
    ResetState();
    Lists l;
    Rank(l, kTool, kSteel);
    Rank(l, kTool, kIron);
    Rank(l, kFuel, kCoal);
    Apply(l);
    std::vector<uint8_t> good;
    c->encode(good);
    const Lists kept = Current();
    auto refused = [&](const std::vector<uint8_t>& bytes, int32_t version) {
        const bool ok = c->decode(bytes.data(), bytes.size(), version);
        if (ok) {
            return false;
        }
        for (int category = 1; category <= kCategories; ++category) {
            if (Current().Of(category) != kept.Of(category)) {
                // a refused payload leaves the old lists
                return false;
            }
        }
        return true;
    };
    CHECK(refused(std::vector<uint8_t>(good.begin(), good.end() - 1), 1));            // cut short
    std::vector<uint8_t> extra = good;
    extra.push_back(0);
    CHECK(refused(extra, 1));                                                         // bytes left over
    // a layout this build does not know
    CHECK(refused(good, 3));   // v2 is the newest this build knows
    CHECK(refused(good, 0));
    CHECK(refused(std::vector<uint8_t>(), 1));                                        // no count at all
    // 5 categories: not this layout
    CHECK(refused(Payload(5, { PathList(), PathList(), PathList(), PathList(), PathList() }), 1));
    // kind 0: not a kind
    CHECK(refused(Payload(4, { { { 0, kSteel } }, PathList(), PathList(), PathList() }), 1));
    // a kind this version lacks (kind 2, an item citizens never take, arrived in v2)
    CHECK(refused(Payload(4, { { { 2, kSteel } }, PathList(), PathList(), PathList() }), 1));
    // an empty path
    CHECK(refused(Payload(4, { { { 1, "" } }, PathList(), PathList(), PathList() }), 1));
    // a NUL in the path
    CHECK(refused(Payload(4, { { { 1, std::string("a\0b", 3) } }, PathList(), PathList(), PathList() }), 1));
    // a control character
    CHECK(refused(Payload(4, { { { 1, "line\nbreak" } }, PathList(), PathList(), PathList() }), 1));
    // far too long
    CHECK(refused(Payload(4, { { { 1, std::string(2000, 'x') } }, PathList(), PathList(), PathList() }), 1));
    CHECK(refused(Payload(4, { { { 1, kSteel }, { 1, "TEMPLATE\\RAWMATERIALSTEELTOOL.RSC:RAWMATERIAL" } },
                               // twice in one list
                               PathList(), PathList(), PathList() }), 1));
    // a count nobody's town has
    std::vector<uint8_t> huge;
    Ext::SaveTail::Writer wh(huge);
    wh.U32(4);
    wh.U32(100000);
    CHECK(refused(huge, 1));
    // a count the payload cannot hold
    std::vector<uint8_t> big;
    Ext::SaveTail::Writer wb(big);
    wb.U32(4);
    wb.U32(3);
    CHECK(refused(big, 1));
    // the good one still reads
    CHECK(c->decode(good.data(), good.size(), 1));
    CHECK(Current().Of(kTool) == kept.Of(kTool));
    c->reset();
}

UNIT_TEST(ext_item_priority_apply_state_with_a_damaged_payload_keeps_the_old_state) {
    ResetState();
    Lists l;
    Rank(l, kTool, kIron);
    Apply(l);
    std::vector<Ext::SaveTail::Record> records(1);
    records[0].id = "item_priority";
    records[0].formatVersion = 1;
    records[0].payload = Payload(4, { { { 1, kSteel } }, {}, {}, {} });
    records[0].payload.pop_back();                                       // damaged: cut short
    std::string offending;
    CHECK(!Ext::SaveTail::ApplyState(records, &offending));
    CHECK(offending == "item_priority");                                 // the load names the extension
    CHECK(RankOf(Current(), kTool, kIron) == 0 && Current().Of(kTool).size() == 1);   // the old lists stand
    ResetState();
}

UNIT_TEST(ext_item_priority_state_travels_in_the_tail) {
    ResetState();
    Lists l;
    Rank(l, kTool, kIron);
    Apply(l);
    Ext::State s = ResolveText("ext.item_priority=1\n");
    Ext::Install(s, Ext::Profile::PerEntry, false);
    Ext::LockForGame(&s);
    std::vector<Ext::SaveTail::Record> recs = Ext::SaveTail::ActiveRecords();
    bool found = false;
    for (const Ext::SaveTail::Record& r : recs) {
        if (r.id == "item_priority") {
            found = true;
            CHECK(!r.payload.empty() && r.formatVersion == 2);
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
    // the town's lists came back with the save
    CHECK_EQ(RankOf(Current(), kTool, kIron), 0);
    // the game ends: the state goes with it
    Ext::UnlockForGame(&s);
    CHECK(Current().Empty());
    InstallText("");
}

UNIT_TEST(ext_item_priority_the_faithful_profile_writes_no_record) {
    ResetState();
    Lists l;
    Rank(l, kTool, kIron);
    Apply(l);
    Ext::State s = ResolveText("profile=faithful\next.item_priority=1\n");
    CHECK(!s.ext.item_priority);
    Ext::Install(s, Ext::Profile::Faithful, false);
    Ext::LockForGame(&s);
    for (const Ext::SaveTail::Record& r : Ext::SaveTail::ActiveRecords()) {
        CHECK(r.id != "item_priority");
    }
    CHECK(Ext::SaveTail::EncodeActive().empty());
    Ext::UnlockForGame(&s);
    InstallText("");
    ResetState();
}

UNIT_TEST(ext_item_priority_the_reset_follows_the_lock_owner) {
    // A reload can lock the new loop before the old one is destroyed. The old loop's unlock must not wipe the
    // lists the new town just decoded.
    ResetState();
    int oldGame = 0, newGame = 0;
    InstallText("ext.item_priority=1\n");
    Ext::LockForGame(&oldGame);
    Ext::LockForGame(&newGame);
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    const std::vector<uint8_t> payload = Payload(4, { { { 1, kIron } }, {}, {}, {} });
    CHECK(c->decode(payload.data(), payload.size(), 1));
    Ext::UnlockForGame(&oldGame);                                        // not the owner: ignored
    CHECK_EQ(RankOf(Current(), kFuel, kIron), 0);                        // the new town's list stands
    Ext::UnlockForGame(&newGame);
    CHECK(Current().Empty());
    InstallText("");
}

// ---- the Never list -------------------------------------------------------------------------------------

UNIT_TEST(ext_item_priority_never_is_exclusive_with_a_rank) {
    Lists l;
    CHECK(Rank(l, kTool, kSteel));
    CHECK(Rank(l, kTool, kIron));
    CHECK(!IsNever(l, kTool, kSteel));
    CHECK(SetNever(l, kTool, kSteel));                                   // a ranked item leaves the ranking
    CHECK(IsNever(l, kTool, kSteel));
    CHECK(IsNever(l, kTool, "TEMPLATE\\RAWMATERIALSTEELTOOL.RSC:RAWMATERIAL"));   // without case
    CHECK_EQ(RankOf(l, kTool, kSteel), kUnranked);
    CHECK_EQ(RankOf(l, kTool, kIron), 0);                                // the list closed up
    CHECK(!SetNever(l, kTool, kSteel));                                  // already Never
    CHECK(!SetNever(l, kTool, ""));
    CHECK(!SetNever(l, 0, kSteel) && !SetNever(l, 5, kSteel));
    CHECK(!IsNever(l, kFuel, kSteel));                                   // per category
    CHECK(!l.Empty());
    CHECK(Rank(l, kTool, kSteel));   // ranking it again takes it off Never
    CHECK(!IsNever(l, kTool, kSteel));
    CHECK_EQ(RankOf(l, kTool, kSteel), 1);
    CHECK(SetNever(l, kTool, kSteel));
    CHECK(ClearNever(l, kTool, kSteel));                                 // allowed again, but not ranked
    CHECK(!ClearNever(l, kTool, kSteel));
    CHECK_EQ(RankOf(l, kTool, kSteel), kUnranked);
    CHECK(SetNever(l, kFuel, kCoal));
    CHECK(Unrank(l, kTool, kIron));
    CHECK(!l.Empty());                                                   // a Never list alone is state
    CHECK(ClearNever(l, kFuel, kCoal));
    CHECK(l.Empty());
}

UNIT_TEST(ext_item_priority_never_rows_are_never_eligible) {
    ResetState();
    int steel = 0, iron = 0, bronze = 0;
    const std::vector<Candidate> game = { { &steel, kSteel }, { &iron, kIron }, { &bronze, kBronze } };
    Lists l;
    Rank(l, kTool, kIron);
    SetNever(l, kTool, kSteel);
    Apply(l);
    Resolve(1, game);
    SetRowActive(true);
    CHECK(Active(kTool));
    CHECK_EQ(RankOfMat(kTool, &steel), kNever);
    CHECK_EQ(RankOfMat(kTool, &iron), 0);
    CHECK_EQ(RankOfMat(kTool, &bronze), kUnranked);
    CHECK(Eligible(kTool, &iron, 0));                                    // the best rank of the rows
    CHECK(!Eligible(kTool, &bronze, 0));
    CHECK(!Eligible(kTool, &steel, 0));
    CHECK(Eligible(kTool, &bronze, kUnranked));                          // only unranked rows left: they win
    CHECK(!Eligible(kTool, &steel, kUnranked));
    // a storage holding only the Never item: no best rank (0x7fffffff), and the item still is not eligible
    CHECK(!Eligible(kTool, &steel, 0x7fffffff));
    CHECK(!Eligible(kTool, &iron, 0x7fffffff));
    ResetState();
}

UNIT_TEST(ext_item_priority_a_never_list_alone_makes_the_category_active) {
    ResetState();
    int steel = 0, iron = 0;
    const std::vector<Candidate> game = { { &steel, kSteel }, { &iron, kIron } };
    Lists l;
    SetNever(l, kTool, kSteel);
    SetNever(l, kFuel, kCoal);                                           // a name the game does not have
    Apply(l);
    Resolve(1, game);
    SetRowActive(true);
    CHECK(g_on);
    CHECK(Active(kTool));
    CHECK(!Active(kFuel));                                               // resolved nothing: inert
    CHECK(g_ranked[kTool - 1].empty() && g_never[kTool - 1].size() == 1);
    CHECK_EQ(RankOfMat(kTool, &steel), kNever);
    CHECK_EQ(RankOfMat(kTool, &iron), kUnranked);
    // no ranks, so one search, and its ceiling lets the unranked items in but never the Never item
    Searches s;
    FindTiered(kTool, [&]() -> bool {
        s.ceilings.push_back(Ceiling());
        return false;
    });
    CHECK(s.ceilings.size() == 1 && s.ceilings[0] == kUnranked);
    CHECK(kNever > kUnranked);
    SetRowActive(false);
    CHECK(!Active(kTool));
    ResetState();
    CHECK(g_never[kTool - 1].empty());
}

UNIT_TEST(ext_item_priority_state_codec_v2_carries_the_never_list) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    if (!c) {
        CHECK(false);
        return;
    }
    ResetState();
    Lists l;
    Rank(l, kTool, kIron);
    SetNever(l, kTool, kSteel);
    SetNever(l, kClothing, kCoat);
    Apply(l);
    std::vector<uint8_t> payload;
    c->encode(payload);
    c->reset();
    CHECK(Current().Empty());
    CHECK(c->decode(payload.data(), payload.size(), 2));
    CHECK(Current().Of(kTool).size() == 1 && Current().Of(kTool)[0] == kIron);
    CHECK(Current().NeverOf(kTool).size() == 1 && Current().NeverOf(kTool)[0] == kSteel);
    CHECK(Current().NeverOf(kClothing).size() == 1 && Current().NeverOf(kClothing)[0] == kCoat);
    CHECK(Current().NeverOf(kFuel).empty());
    std::vector<uint8_t> again;
    c->encode(again);
    CHECK(again == payload);
    // a v1 record has no Never kind: the same bytes under version 1 are refused, a ranked-only record loads
    c->reset();
    CHECK(!c->decode(payload.data(), payload.size(), 1));
    CHECK(Current().Empty());
    const std::vector<uint8_t> v1 = Payload(4, { PathList(), { { 1, kIron } }, PathList(), PathList() });
    CHECK(c->decode(v1.data(), v1.size(), 1));
    CHECK(c->decode(v1.data(), v1.size(), 2));
    CHECK(Current().Of(kTool).size() == 1);
    CHECK(Current().NeverOf(kTool).empty());
    // damage specific to v2: the same path ranked and Never, a path Never twice, kind 3
    CHECK(c->decode(payload.data(), payload.size(), 2));   // lists with a ranked and a Never entry to keep
    const auto refused = [&](const std::vector<uint8_t>& bytes) {
        const Lists keep = Current();
        const bool ok = c->decode(bytes.data(), bytes.size(), 2);
        return !ok && Current().Of(kTool) == keep.Of(kTool) &&
               Current().NeverOf(kTool) == keep.NeverOf(kTool);
    };
    CHECK(refused(Payload(4, { PathList(),
                               { { 1, kIron }, { 2, "TEMPLATE\\RAWMATERIALIRONTOOL.RSC:RAWMATERIAL" } },
                               PathList(), PathList() })));
    CHECK(refused(Payload(4, { PathList(), { { 2, kIron }, { 2, kIron } }, PathList(), PathList() })));
    CHECK(refused(Payload(4, { PathList(), { { 3, kIron } }, PathList(), PathList() })));
    c->reset();
}
