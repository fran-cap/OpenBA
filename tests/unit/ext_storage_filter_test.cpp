// ============================================================================
// tests/unit/ext_storage_filter_test.cpp -- ext.storage_filter
// ============================================================================
// src/ext/storage_filter/storage_filter.{h,cpp}, save_state.cpp without the game: the registry row (class C,
// off by default), the list of refused items per building (keyed by its footprint corner tile: set, accept
// again, case-folded paths, limits, the revision counter the game side watches), and the save-tail codec
// (round trip, empty state, every kind of damaged payload, a refused payload leaving the old list alone).
// The seam in AcceptsMaterial, the entity lookup and the tab are the exe's.
// ============================================================================
#include "unit.h"
#include "ext/storage_filter/storage_filter.h"
#include "ext/registry.h"
#include "ext/savetail.h"
#include "io/settings_file.h"

#include <string>
#include <vector>

namespace {

using SettingsEntries = IO::SettingsFile::Entries;
using namespace Ext::StorageFilter;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const SettingsEntries* entries = static_cast<const SettingsEntries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    SettingsEntries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

const Ext::SaveTail::StateCodec* TheCodec() {
    return Ext::SaveTail::FindCodec("storage_filter");
}

struct Wire {
    int32_t tx;
    int32_t ty;
    std::vector<std::string> items;
};

// A payload built by hand: `count`, then { tile x, tile y, item count, paths, no caps } per entry.
std::vector<uint8_t> Payload(uint32_t count, const std::vector<Wire>& entries) {
    std::vector<uint8_t> bytes;
    Ext::SaveTail::Writer w(bytes);
    w.U32(count);
    for (const Wire& e : entries) {
        w.I32(e.tx);
        w.I32(e.ty);
        w.U32(static_cast<uint32_t>(e.items.size()));
        for (const std::string& p : e.items) {
            w.String(p);
        }
        w.U32(0);   // no caps
    }
    return bytes;
}

const std::string kLogs = "Template/RawMaterialLogs.rsc:rawmaterial";
const std::string kStone = "Template/RawMaterialStone.rsc:rawmaterial";
const std::string kWheat = "Template/RawMaterialWheat.rsc:rawmaterial";

}  // namespace

UNIT_TEST(ext_storage_filter_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("storage_filter");
    CHECK(e != nullptr);
    if (!e) {
        return;
    }
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::C);   // per-town state: the refusals are recorded in the save
    CHECK(!e->defaultOn);
    CHECK(e->formatVersion == 1);
    CHECK(Ext::SettingsKey(*e) == "ext.storage_filter");
    CHECK(std::string(kId) == e->id);
    CHECK(!ResolveText("").ext.storage_filter);
    CHECK(ResolveText("ext.storage_filter=1\n").ext.storage_filter);
    CHECK(!ResolveText("profile=faithful\n").ext.storage_filter);
    CHECK(TheCodec() != nullptr);
}

UNIT_TEST(ext_storage_filter_refuse_and_accept_again) {
    ResetState();
    CHECK(!AnyRefusals());
    CHECK(!IsRefused(10, 20, kLogs));
    CHECK(SetRefused(10, 20, kLogs, true));
    CHECK(AnyRefusals());
    CHECK(IsRefused(10, 20, kLogs));
    CHECK(!IsRefused(10, 20, kStone));                               // one item, not the building
    CHECK(!IsRefused(11, 20, kLogs) && !IsRefused(10, 21, kLogs));   // one building, not the town
    CHECK(!SetRefused(10, 20, kLogs, true));                         // already refused: nothing changes
    CHECK(SetRefused(10, 20, kStone, true));
    CHECK(RefusedOf(10, 20).size() == 2 && RefusedOf(10, 20)[0] == kLogs && RefusedOf(10, 20)[1] == kStone);
    CHECK(SetRefused(30, 40, kWheat, true));                         // a second building has its own entry
    CHECK(Entries().size() == 2);
    CHECK(SetRefused(10, 20, kLogs, false));
    CHECK(!IsRefused(10, 20, kLogs) && IsRefused(10, 20, kStone));
    CHECK(!SetRefused(10, 20, kLogs, false));                        // not refused: nothing changes
    CHECK(!SetRefused(5, 5, kLogs, false));                          // no entry: nothing changes
    CHECK(SetRefused(10, 20, kStone, false));                        // the last one removes the entry
    CHECK(Entries().size() == 1 && Entries()[0].tileX == 30 && Entries()[0].tileY == 40);
    CHECK(RefusedOf(10, 20).empty());
    CHECK(ClearBuilding(30, 40));
    CHECK(!ClearBuilding(30, 40));
    CHECK(!AnyRefusals());
    ResetState();
}

UNIT_TEST(ext_storage_filter_paths_compare_without_case) {
    ResetState();
    CHECK(SetRefused(10, 20, "Template/RawMaterialLogs.rsc:rawmaterial", true));
    CHECK(IsRefused(10, 20, "template/rawmateriallogs.rsc:RAWMATERIAL"));
    CHECK(!SetRefused(10, 20, "TEMPLATE/RAWMATERIALLOGS.RSC:RAWMATERIAL", true));   // the same item twice
    // accepted again by any casing
    CHECK(SetRefused(10, 20, "template/RAWMATERIALLOGS.rsc:rawmaterial", false));
    CHECK(!AnyRefusals());
    ResetState();
}

UNIT_TEST(ext_storage_filter_refuses_what_cannot_be_stored) {
    ResetState();
    CHECK(!SetRefused(-1, 20, kLogs, true) && !SetRefused(10, -1, kLogs, true));   // no tile to key on
    CHECK(!SetRefused(10, 20, "", true));                                          // no item
    CHECK(!SetRefused(10, 20, std::string(kMaxPathBytes + 1, 'a'), true));
    CHECK(!SetRefused(10, 20, "Logs\nStone", true));
    CHECK(!AnyRefusals());
    CHECK(PathIsValid(std::string(kMaxPathBytes, 'a')));
    CHECK(PathIsValid("Trader, Odd;A#B:C/\xe9"));
    // one building cannot refuse more items than the limit
    for (size_t i = 0; i < kMaxItemsPerBuilding; ++i) {
        CHECK(SetRefused(10, 20, "item" + std::to_string(i), true));
    }
    CHECK(!SetRefused(10, 20, "one more", true));
    CHECK(RefusedOf(10, 20).size() == kMaxItemsPerBuilding);
    ResetState();
}

UNIT_TEST(ext_storage_filter_every_change_bumps_the_revision) {
    ResetState();
    uint32_t rev = Revision();
    CHECK(SetRefused(10, 20, kLogs, true));
    CHECK(Revision() != rev);
    rev = Revision();
    // no change: the game side keeps its resolved paths
    CHECK(!SetRefused(10, 20, kLogs, true));
    CHECK(Revision() == rev);
    CHECK(SetRefused(10, 20, kLogs, false));
    CHECK(Revision() != rev);
    rev = Revision();
    CHECK(SetRefused(10, 20, kLogs, true) && ClearBuilding(10, 20));
    CHECK(Revision() != rev);
    rev = Revision();
    ResetState();
    CHECK(Revision() != rev);
}

UNIT_TEST(ext_storage_filter_a_tile_is_one_building) {
    ResetState();
    // the key is the corner tile: both coordinates must match, and neither is a building by itself
    CHECK(SetRefused(10, 20, kLogs, true));
    CHECK(SetRefused(20, 10, kLogs, true));                    // swapped coordinates: another building
    CHECK(Entries().size() == 2);
    CHECK(SetRefused(0, 0, kWheat, true));                     // the origin is a tile like any other
    CHECK(IsRefused(0, 0, kWheat) && !IsRefused(0, 1, kWheat));
    CHECK(ClearBuilding(10, 20));
    CHECK(!IsRefused(10, 20, kLogs) && IsRefused(20, 10, kLogs));
    ResetState();
}

UNIT_TEST(ext_storage_filter_state_codec_round_trip) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    ResetState();
    CHECK(SetRefused(10, 20, kLogs, true));
    CHECK(SetRefused(10, 20, kStone, true));
    CHECK(SetRefused(0, 300, kWheat, true));
    CHECK(SetRefused(123456, 10, "Trader, Odd;A#B:C/\xe9", true));
    std::vector<uint8_t> payload;
    c->encode(payload);
    const std::vector<Entry> before = Entries();
    c->reset();
    CHECK(Entries().empty());
    CHECK(c->decode(payload.data(), payload.size(), 1));
    CHECK(Entries().size() == before.size());
    for (size_t i = 0; i < before.size() && i < Entries().size(); ++i) {
        CHECK(Entries()[i].tileX == before[i].tileX && Entries()[i].tileY == before[i].tileY);
        CHECK(Entries()[i].refused == before[i].refused);
    }
    // an empty list is still a valid record
    c->reset();
    payload.clear();
    c->encode(payload);
    CHECK(payload.size() == 4);
    CHECK(c->decode(payload.data(), payload.size(), 1) && Entries().empty());
    ResetState();
}

UNIT_TEST(ext_storage_filter_state_codec_refuses_every_damaged_payload) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    ResetState();
    // the old list a refused payload must leave alone
    CHECK(SetRefused(7, 8, kLogs, true));
    auto refused = [&](const std::vector<uint8_t>& bytes) {
        const bool ok = c->decode(bytes.data(), bytes.size(), 1);
        const bool kept = Entries().size() == 1 && Entries()[0].tileX == 7 && IsRefused(7, 8, kLogs);
        return !ok && kept;
    };
    CHECK(refused(std::vector<uint8_t>()));                                          // cut short: no count
    // a count the payload cannot hold
    CHECK(refused(Payload(1, {})));
    CHECK(refused(Payload(static_cast<uint32_t>(kMaxBuildings) + 1, {})));           // over the limit
    // a negative tile
    CHECK(refused(Payload(1, { { -1, 20, { kLogs } } })));
    CHECK(refused(Payload(1, { { 10, -1, { kLogs } } })));
    // a building with no items
    CHECK(refused(Payload(1, { { 10, 20, {} } })));
    CHECK(refused(Payload(1, { { 10, 20, { "" } } })));                                      // an empty path
    // a control character
    CHECK(refused(Payload(1, { { 10, 20, { "Logs\nStone" } } })));
    CHECK(refused(Payload(1, { { 10, 20, { std::string(kMaxPathBytes + 1, 'a') } } })));     // over-long
    // a path twice
    CHECK(refused(Payload(1, { { 10, 20, { kLogs, "TEMPLATE/RAWMATERIALLOGS.RSC:RAWMATERIAL" } } })));
    // a building (tile) twice
    CHECK(refused(Payload(2, { { 10, 20, { kLogs } }, { 10, 20, { kStone } } })));
    std::vector<uint8_t> extra = Payload(1, { { 10, 20, { kLogs } } });
    extra.push_back(0);
    CHECK(refused(extra));                                                           // bytes left over
    std::vector<uint8_t> cut = Payload(1, { { 10, 20, { kLogs } } });
    cut.pop_back();
    // cut short inside a path
    CHECK(refused(cut));
    // a newer or older format version is refused too
    const std::vector<uint8_t> good = Payload(1, { { 10, 20, { kLogs } } });
    CHECK(!c->decode(good.data(), good.size(), 2));
    CHECK(Entries().size() == 1 && Entries()[0].tileX == 7);
    // and a good payload replaces the old list
    CHECK(c->decode(good.data(), good.size(), 1));
    CHECK(Entries().size() == 1 && Entries()[0].tileX == 10 && IsRefused(10, 20, kLogs) &&
          !IsRefused(7, 8, kLogs));
    ResetState();
}

UNIT_TEST(ext_storage_filter_a_reset_forgets_every_refusal) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    ResetState();
    CHECK(SetRefused(10, 20, kLogs, true));
    Ext::SaveTail::ResetAllState();                    // a new game, or the game ends
    CHECK(!AnyRefusals() && Entries().empty());
}

UNIT_TEST(ext_storage_filter_caps_set_change_and_clear) {
    ResetState();
    CHECK(CapOf(10, 20, kLogs) == 0);
    CHECK(!SetCap(10, 20, kLogs, 0));                        // nothing to clear
    CHECK(!SetCap(-1, 20, kLogs, 5) && !SetCap(10, 20, "", 5));
    CHECK(SetCap(10, 20, kLogs, 50));
    CHECK(CapOf(10, 20, "TEMPLATE/RAWMATERIALLOGS.RSC:RAWMATERIAL") == 50);   // compared without case
    CHECK(AnyRefusals() && Entries().size() == 1 && Entries()[0].refused.empty());
    CHECK(!SetCap(10, 20, kLogs, 50));                       // the same value: no change
    const uint32_t before = Revision();
    CHECK(SetCap(10, 20, kLogs, 80) && CapOf(10, 20, kLogs) == 80 && Revision() != before);
    CHECK(CapOf(11, 20, kLogs) == 0);                        // the cap is this building's
    CHECK(SetCap(10, 20, kLogs, kMaxCap + 5) && CapOf(10, 20, kLogs) == kMaxCap);   // clamped
    CHECK(SetRefused(10, 20, kStone, true));                 // a refusal beside a cap
    CHECK(SetCap(10, 20, kLogs, 0) && CapOf(10, 20, kLogs) == 0);
    CHECK(Entries().size() == 1);                            // the refusal keeps the entry
    CHECK(SetRefused(10, 20, kStone, false));
    CHECK(Entries().empty());                                // nothing left: the entry goes
    CHECK(SetCap(1, 2, kLogs, 5) && SetRefused(1, 2, kLogs, true) && SetRefused(1, 2, kLogs, false));
    CHECK(Entries().size() == 1);                            // the cap keeps the entry
    ResetState();
}

UNIT_TEST(ext_storage_filter_state_codec_round_trip_with_caps) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    ResetState();
    CHECK(SetRefused(10, 20, kLogs, true));
    CHECK(SetCap(10, 20, kStone, 120));
    CHECK(SetCap(3, 4, kWheat, 7));                          // a building with caps only
    std::vector<uint8_t> payload;
    c->encode(payload);
    const std::vector<Entry> before = Entries();
    c->reset();
    CHECK(c->decode(payload.data(), payload.size(), 1));
    CHECK(Entries().size() == 2 && CapOf(10, 20, kStone) == 120 && CapOf(3, 4, kWheat) == 7 &&
          IsRefused(10, 20, kLogs));
    CHECK(Entries()[1].refused.empty() && Entries()[1].tileX == 3);
    // damaged caps are refused and leave the list alone
    auto cap = [&](int32_t limit, bool twice, bool empty) {
        std::vector<uint8_t> b;
        Ext::SaveTail::Writer w(b);
        w.U32(1);
        w.I32(10);
        w.I32(20);
        w.U32(0);                                            // no refusals
        w.U32(empty ? 0u : (twice ? 2u : 1u));
        for (int k = 0; k < (twice ? 2 : 1) && !empty; ++k) {
            w.String(k == 0 ? kStone : "TEMPLATE/RAWMATERIALSTONE.RSC:RAWMATERIAL");
            w.I32(limit);
        }
        return c->decode(b.data(), b.size(), 1);
    };
    CHECK(cap(5, false, false) && CapOf(10, 20, kStone) == 5);   // the good one
    CHECK(!cap(0, false, false) && !cap(-3, false, false) && !cap(kMaxCap + 1, false, false));
    CHECK(!cap(5, true, false));                             // a path twice
    CHECK(!cap(5, false, true));                             // an entry with nothing in it
    CHECK(CapOf(10, 20, kStone) == 5);                       // still the good one's list
    ResetState();
}
