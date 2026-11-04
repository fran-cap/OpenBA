// ============================================================================
// tests/unit/ext_restricted_foods_test.cpp -- ext.restricted_foods
// ============================================================================
// src/ext/restricted_foods/restricted_foods.{h,cpp}, save_state.cpp without the game: the registry row
// (class C, off by default), the list of blocked foods (template paths, case-insensitive), the save-tail
// codec (round trip with awkward names, empty state, every kind of damaged payload, a refused payload
// leaving the old list alone), the resolution of the list into the flat array the seams scan, `g_on`
// following the row, and the lock-owner rule for the reset. The seams themselves and the factory walk are
// the exe's (restricted_foods_town.cpp).
// ============================================================================
#include "unit.h"
#include "ext/restricted_foods/restricted_foods.h"
#include "ext/registry.h"
#include "ext/savetail.h"
#include "io/settings_file.h"

#include <string>
#include <vector>

namespace {

using IO::SettingsFile::Entries;
using namespace Ext::RestrictedFoods;

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
    return Ext::SaveTail::FindCodec("restricted_foods");
}

// A payload built by hand: `count`, then { kind, path } per entry.
std::vector<uint8_t> Payload(uint32_t count, const std::vector<std::pair<uint8_t, std::string>>& entries) {
    std::vector<uint8_t> bytes;
    Ext::SaveTail::Writer w(bytes);
    w.U32(count);
    for (const auto& e : entries) {
        w.Bytes(&e.first, 1);
        w.String(e.second);
    }
    return bytes;
}

const std::string kWheat = "Template\\RawMaterialWheat.rsc:rawmaterial";
const std::string kFish = "Template\\RawMaterialFish.rsc:rawmaterial";
// names the old settings grammar would have cut, split or stripped
const std::vector<std::string> kAwkward = {
    "Template\\Fish, Smoked.rsc:rawmaterial",
    "Template\\Dried.Fish.rsc:rawmaterial",
    "RawMaterialHoney",
    "A;B#C:D/E",
    "Mods\\Pa\xC3\xADs.rsc:rawmaterial",
};

}  // namespace

UNIT_TEST(ext_restricted_foods_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("restricted_foods");
    CHECK(e != nullptr);
    if (!e) {
        return;
    }
    CHECK(e->tier == Ext::Tier::Extension);
    // per-town state: the checked list is recorded in the save
    CHECK(e->cls == Ext::Class::C);
    CHECK(!e->defaultOn);
    CHECK(e->formatVersion == 1);
    CHECK(Ext::SettingsKey(*e) == "ext.restricted_foods");
    CHECK(std::string(kId) == e->id);
    CHECK(!ResolveText("").ext.restricted_foods);                      // absent key = the default (off)
    CHECK(ResolveText("ext.restricted_foods=1\n").ext.restricted_foods);
    CHECK(!ResolveText("profile=faithful\n").ext.restricted_foods);   // the faithful profile turns it off
    CHECK(TheCodec() != nullptr);                                      // class C: it has a state codec
}

UNIT_TEST(ext_restricted_foods_list_edits) {
    ResetState();
    CHECK(BlockedPaths().empty());
    CHECK(SetPathBlocked(kWheat, true));
    CHECK(!SetPathBlocked(kWheat, true));                              // already listed: nothing changes
    CHECK(IsPathBlocked(kWheat));
    CHECK(IsPathBlocked("template\\rawmaterialwheat.rsc:RAWMATERIAL")); // paths compare without case
    CHECK(!SetPathBlocked("TEMPLATE\\RAWMATERIALWHEAT.RSC:RAWMATERIAL", true));
    CHECK(BlockedPaths().size() == 1);
    CHECK(SetPathBlocked(kFish, true));
    CHECK(BlockedPaths().size() == 2 && BlockedPaths()[0] == kWheat && BlockedPaths()[1] == kFish);
    CHECK(SetPathBlocked("template\\rawmaterialwheat.rsc:rawmaterial", false));   // uncheck, any case
    CHECK(!IsPathBlocked(kWheat) && IsPathBlocked(kFish));
    CHECK(!SetPathBlocked(kWheat, false));                             // already free
    CHECK(!SetPathBlocked("", true));                                  // no path: nothing to key on
    ResetState();
    CHECK(BlockedPaths().empty());
}

UNIT_TEST(ext_restricted_foods_state_codec_round_trip) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    ResetState();
    CHECK(SetPathBlocked(kWheat, true));
    for (const std::string& name : kAwkward) {
        CHECK(SetPathBlocked(name, true));
    }
    const std::vector<std::string> before = BlockedPaths();
    std::vector<uint8_t> payload;
    c->encode(payload);
    CHECK(!payload.empty());
    c->reset();                                                        // a new game: nothing carried over
    CHECK(BlockedPaths().empty());
    CHECK(c->decode(payload.data(), payload.size(), 1));
    // exactly the names that went in, in order: no comma, hash, dot, colon or prefix is read as grammar
    CHECK(BlockedPaths() == before);
    std::vector<uint8_t> again;
    c->encode(again);
    CHECK(again == payload);                                           // stable: encode(decode(x)) == x
    c->reset();
}

UNIT_TEST(ext_restricted_foods_state_codec_empty_state_is_still_a_valid_record) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    if (!c) {
        CHECK(false);
        return;
    }
    c->reset();
    std::vector<uint8_t> payload;
    c->encode(payload);
    CHECK_EQ(payload.size(), 4u);                                      // count 0
    CHECK(SetPathBlocked(kWheat, true));
    CHECK(c->decode(payload.data(), payload.size(), 1));               // an empty record empties the list
    CHECK(BlockedPaths().empty());
}

UNIT_TEST(ext_restricted_foods_state_codec_refuses_every_damaged_payload) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    if (!c) {
        CHECK(false);
        return;
    }
    ResetState();
    CHECK(SetPathBlocked(kWheat, true));
    CHECK(SetPathBlocked(kFish, true));
    std::vector<uint8_t> good;
    c->encode(good);
    const std::vector<std::string> kept = BlockedPaths();
    auto refused = [&](const std::vector<uint8_t>& bytes, int32_t version) {
        const bool ok = c->decode(bytes.data(), bytes.size(), version);
        return !ok && BlockedPaths() == kept;                          // refused AND the old list stands
    };
    CHECK(refused(std::vector<uint8_t>(good.begin(), good.end() - 1), 1));          // cut short
    std::vector<uint8_t> extra = good;
    extra.push_back(0);
    CHECK(refused(extra, 1));                                                       // bytes left over
    CHECK(refused(good, 2));                                                        // a layout this build does not know
    CHECK(refused(good, 0));
    CHECK(refused(std::vector<uint8_t>(), 1));                                      // no count at all
    CHECK(refused(Payload(1, {{0, kWheat}}), 1));                                   // kind 0: not a kind
    CHECK(refused(Payload(1, {{2, kWheat}}), 1));                                   // a kind this version lacks
    CHECK(refused(Payload(1, {{1, ""}}), 1));                                       // an empty path
    CHECK(refused(Payload(1, {{1, std::string("a\0b", 3)}}), 1));                   // a NUL in the path
    CHECK(refused(Payload(1, {{1, "line\nbreak"}}), 1));                            // a control character
    CHECK(refused(Payload(1, {{1, std::string(2000, 'x')}}), 1));                   // far too long
    CHECK(refused(Payload(2, {{1, kWheat}, {1, "TEMPLATE\\RAWMATERIALWHEAT.RSC:RAWMATERIAL"}}), 1));   // twice
    CHECK(refused(Payload(2, {{1, kWheat}}), 1));                                   // count says more than there is
    CHECK(refused(Payload(0xFFFFFFFFu, {{1, kWheat}}), 1));                         // a count no payload could hold
    CHECK(refused(Payload(1, {{1, kWheat}, {1, kFish}}), 1));                       // count says fewer than there is
    CHECK(c->decode(good.data(), good.size(), 1));                                  // the good one still reads
    CHECK(BlockedPaths() == kept);
    c->reset();
}

UNIT_TEST(ext_restricted_foods_apply_state_with_a_damaged_payload_keeps_the_old_state) {
    ResetState();
    CHECK(SetPathBlocked(kWheat, true));
    std::vector<Ext::SaveTail::Record> records(1);
    records[0].id = "restricted_foods";
    records[0].formatVersion = 1;
    records[0].payload = Payload(1, {{1, kFish}});
    records[0].payload.pop_back();                                     // damaged: cut short
    std::string offending;
    CHECK(!Ext::SaveTail::ApplyState(records, &offending));
    CHECK(offending == "restricted_foods");                            // the load names the extension
    CHECK(BlockedPaths().size() == 1 && IsPathBlocked(kWheat));        // and the town's old list is untouched
    ResetState();
}

UNIT_TEST(ext_restricted_foods_state_travels_in_the_tail) {
    // The registry's ACTIVE class C row carries the codec's payload in its own record; the lock gives it back
    // with the save.
    ResetState();
    CHECK(SetPathBlocked(kWheat, true));
    Ext::State s = ResolveText("ext.restricted_foods=1\n");
    Ext::Install(s, Ext::Profile::PerEntry, false);
    Ext::LockForGame(&s);
    std::vector<Ext::SaveTail::Record> recs = Ext::SaveTail::ActiveRecords();
    bool found = false;
    for (const Ext::SaveTail::Record& r : recs) {
        if (r.id == "restricted_foods") {
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
    CHECK(IsPathBlocked(kWheat));                                      // the town's list came back with the save
    Ext::UnlockForGame(&s);                                            // the game ends: the state goes with it
    CHECK(BlockedPaths().empty());
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_restricted_foods_the_faithful_profile_writes_no_record) {
    ResetState();
    CHECK(SetPathBlocked(kWheat, true));
    // the row asked for, but the faithful profile turns every extension off: no record, zero bytes
    Ext::State s = ResolveText("profile=faithful\next.restricted_foods=1\n");
    CHECK(!s.ext.restricted_foods);
    Ext::Install(s, Ext::Profile::Faithful, false);
    Ext::LockForGame(&s);
    for (const Ext::SaveTail::Record& r : Ext::SaveTail::ActiveRecords()) {
        CHECK(r.id != "restricted_foods");
    }
    CHECK(Ext::SaveTail::EncodeActive().empty());
    Ext::UnlockForGame(&s);
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
    ResetState();
}

UNIT_TEST(ext_restricted_foods_the_reset_follows_the_lock_owner) {
    // A reload can lock the new loop before the old one is destroyed. The old loop's unlock must not wipe
    // the list the new town just decoded.
    ResetState();
    int oldGame = 0, newGame = 0;
    Ext::State s = ResolveText("ext.restricted_foods=1\n");
    Ext::Install(s, Ext::Profile::PerEntry, false);
    Ext::LockForGame(&oldGame);
    Ext::LockForGame(&newGame);
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    const std::vector<uint8_t> payload = Payload(1, {{1, kWheat}});
    CHECK(c->decode(payload.data(), payload.size(), 1));
    Ext::UnlockForGame(&oldGame);                                      // not the owner: ignored
    CHECK(IsPathBlocked(kWheat));
    Ext::UnlockForGame(&newGame);                                      // the owner: the town is over
    CHECK(BlockedPaths().empty());
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_restricted_foods_resolves_the_list_into_a_flat_array) {
    ResetState();
    int wheat = 0, fish = 0, apple = 0;
    const std::vector<Candidate> game = {
        { &wheat, kWheat },
        { &fish, "TEMPLATE\\RAWMATERIALFISH.RSC:RAWMATERIAL" },        // the game's own spelling differs
        { &apple, "Template\\RawMaterialApple.rsc:rawmaterial" },
    };
    CHECK(NeedsResolve(1));                                            // nothing resolved yet
    CHECK(SetPathBlocked(kWheat, true));
    CHECK(SetPathBlocked(kFish, true));
    Resolve(1, game);
    CHECK(!NeedsResolve(1));
    CHECK(g_blocked.size() == 2);
    CHECK(IsBlocked(&wheat) && IsBlocked(&fish));
    CHECK(!IsBlocked(&apple) && !IsBlocked(nullptr));
    CHECK(NeedsResolve(2));                                            // a new game, a load or a reload
    Resolve(2, game);
    CHECK(!NeedsResolve(2));
    CHECK(SetPathBlocked(kFish, false));                               // the list changed
    CHECK(NeedsResolve(2));
    Resolve(2, game);
    CHECK(IsBlocked(&wheat) && !IsBlocked(&fish));
    // a path the running game does not have matches nothing, and stays in the list
    CHECK(SetPathBlocked("Mods\\Gone.rsc:rawmaterial", true));
    Resolve(2, game);
    CHECK(g_blocked.size() == 1 && IsPathBlocked("Mods\\Gone.rsc:rawmaterial"));
    ResetState();
}

UNIT_TEST(ext_restricted_foods_g_on_follows_the_row_and_the_list) {
    ResetState();
    CHECK(!g_on);                                                      // inert by default
    int wheat = 0;
    const std::vector<Candidate> game = { { &wheat, kWheat } };
    CHECK(SetPathBlocked(kWheat, true));
    Resolve(1, game);
    SetRowActive(false);                                               // the row off: nothing is blocked
    CHECK(!g_on);
    SetRowActive(true);
    CHECK(g_on);
    ResetState();                                                      // an empty list, even with the row on
    SetRowActive(true);
    CHECK(!g_on);
    CHECK(SetPathBlocked(kWheat, true));
    Resolve(1, game);
    SetRowActive(true);
    CHECK(g_on);
    ResetState();                                                      // the game ends: g_on is cleared
    CHECK(!g_on && g_blocked.empty());
    // the list names a food the game does not have: nothing resolved, so the seams stay off
    CHECK(SetPathBlocked("Mods\\Gone.rsc:rawmaterial", true));
    Resolve(1, game);
    SetRowActive(true);
    CHECK(!g_on);
    ResetState();
}

// Review F-243 / F-248. The codec compares every path with every earlier one (the duplicate check), so a
// hostile save of N distinct paths (8 bytes each) makes the load do N^2/2 string compares: 60,000 entries
// took 4.3 s, a few MB of tail tens of minutes. The list is capped at 4096 entries, as item_priority's:
// a longer list than any town's materials is damage. The cap itself still decodes.
UNIT_TEST(ext_restricted_foods_state_codec_refuses_a_list_past_the_cap) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    const uint32_t kCap = 4096;
    std::vector<std::pair<uint8_t, std::string>> entries;
    for (uint32_t i = 0; i <= kCap; ++i) {
        std::string path(3, '\0');
        path[0] = static_cast<char>(0x80 + (i % 128));
        path[1] = static_cast<char>(0x80 + ((i / 128) % 128));
        path[2] = static_cast<char>(0x80 + ((i / (128 * 128)) % 128));
        entries.emplace_back(static_cast<uint8_t>(1), path);
    }
    ResetState();
    CHECK(SetPathBlocked(kWheat, true));
    const std::vector<uint8_t> over = Payload(kCap + 1, entries);
    CHECK(!c->decode(over.data(), over.size(), 1));
    // refused: the old list stays
    CHECK(BlockedPaths() == std::vector<std::string>{ kWheat });
    entries.pop_back();
    const std::vector<uint8_t> at = Payload(kCap, entries);
    CHECK(c->decode(at.data(), at.size(), 1));
    CHECK(BlockedPaths().size() == kCap);
    c->reset();
}
