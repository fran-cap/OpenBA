// ============================================================================
// tests/unit/ext_trader_request_test.cpp -- ext.trader_request
// ============================================================================
// src/ext/trader_request/trader_request.{h,cpp}, save_state.cpp without the game: the registry row (class C,
// on by default), the list of requests (one per trading post), the arrival (the requested trader, a "next
// visit" request used up, an unknown trader leaving retail's roll), the doubled price, the save-tail codec
// (round trip, empty state, every kind of damaged payload, a refused payload leaving the old list alone) and
// the lock-owner rule for the reset. The seams and the Orders tab row are the exe's
// (trader_request_town.cpp, panel_row.cpp); PanelsForget is a counting stand-in here.
// ============================================================================
#include "unit.h"
#include "ext/trader_request/trader_request.h"
#include "ext/registry.h"
#include "ext/savetail.h"
#include "io/settings_file.h"

#include <limits>
#include <string>
#include <vector>

// The exe's PanelsForget is panel_row.cpp's (the UI side, not in the unit build): this one counts its calls.
namespace Ext {
namespace TraderRequest {
int g_panelsForgotten = 0;
void PanelsForget() {
    ++g_panelsForgotten;
}
}  // namespace TraderRequest
}  // namespace Ext

namespace {

using IO::SettingsFile::Entries;
using namespace Ext::TraderRequest;

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
    return Ext::SaveTail::FindCodec("trader_request");
}

struct Wire {
    int32_t post;
    uint32_t mode;
    std::string trader;
};

// A payload built by hand: `count`, then { post, mode, trader } per entry.
std::vector<uint8_t> Payload(uint32_t count, const std::vector<Wire>& entries) {
    std::vector<uint8_t> bytes;
    Ext::SaveTail::Writer w(bytes);
    w.U32(count);
    for (const Wire& e : entries) {
        w.I32(e.post);
        w.U32(e.mode);
        w.String(e.trader);
    }
    return bytes;
}

const std::string kSeeds = "SeedMerchant";
const std::string kLivestock = "LivestockMerchant";
const std::vector<std::string> kTraders = { "GeneralMerchant", kSeeds, kLivestock };

}  // namespace

UNIT_TEST(ext_trader_request_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("trader_request");
    CHECK(e != nullptr);
    if (!e) {
        return;
    }
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::C);   // per-town state: the requests are recorded in the save
    CHECK(e->defaultOn);
    CHECK(e->formatVersion == 1);
    CHECK(Ext::SettingsKey(*e) == "ext.trader_request");
    CHECK(std::string(kId) == e->id);
    CHECK(ResolveText("").ext.trader_request);
    CHECK(!ResolveText("ext.trader_request=0\n").ext.trader_request);
    CHECK(!ResolveText("profile=faithful\n").ext.trader_request);
    CHECK(TheCodec() != nullptr);
}

UNIT_TEST(ext_trader_request_one_request_per_post) {
    ResetState();
    Request r;
    CHECK(!RequestOf(7, r));
    CHECK(SetRequest(7, kSeeds, kModeOnce));
    CHECK(!SetRequest(7, kSeeds, kModeOnce));                  // the same request: nothing changes
    CHECK(RequestOf(7, r) && r.trader == kSeeds && r.mode == kModeOnce);
    CHECK(SetRequest(7, kSeeds, kModeAlways));                 // the mode combo
    CHECK(SetRequest(7, kLivestock, kModeAlways));             // the trader combo
    CHECK(Requests().size() == 1 && Requests()[0].trader == kLivestock && Requests()[0].mode == kModeAlways);
    CHECK(SetRequest(9, kSeeds, kModeOnce));                   // a second post has its own
    CHECK(Requests().size() == 2);
    CHECK(!SetRequest(-1, kSeeds, kModeOnce));                 // no post to key on
    CHECK(!SetRequest(3, "", kModeOnce));                      // no trader
    CHECK(!SetRequest(3, std::string(kMaxTraderBytes + 1, 'a'), kModeOnce));
    CHECK(!SetRequest(3, "Seed\nMerchant", kModeOnce));
    CHECK(!SetRequest(3, kSeeds, 0));                          // unknown modes
    CHECK(!SetRequest(3, kSeeds, 3));
    CHECK(ClearRequest(7));
    CHECK(!ClearRequest(7));
    CHECK(!RequestOf(7, r) && RequestOf(9, r));
    ResetState();
    CHECK(Requests().empty());
}

UNIT_TEST(ext_trader_request_the_arrival_is_the_requested_trader) {
    ResetState();
    CHECK(TakeArrival(7, kTraders) == -1);                     // no request: retail's roll stands
    CHECK(SetRequest(7, kLivestock, kModeAlways));
    CHECK(TakeArrival(8, kTraders) == -1);                     // another post's request is not this one's
    CHECK(TakeArrival(7, kTraders) == 2);
    CHECK(TakeArrival(7, kTraders) == 2);                      // "every visit" stays
    CHECK(Requests().size() == 1);
    CHECK(SetRequest(7, kSeeds, kModeOnce));
    CHECK(TakeArrival(7, kTraders) == 1);
    CHECK(Requests().empty());                                 // "next visit" is used up by the arrival
    CHECK(TakeArrival(7, kTraders) == -1);
    // A trader the game no longer has (a mod was removed): retail's roll stands and the request waits.
    CHECK(SetRequest(7, "SpiceMerchant", kModeOnce));
    CHECK(TakeArrival(7, kTraders) == -1);
    CHECK(Requests().size() == 1);
    CHECK(TakeArrival(7, std::vector<std::string>()) == -1);
    ResetState();
}

UNIT_TEST(ext_trader_request_a_requested_visit_costs_double) {
    CHECK(kPriceFactor == 2);
    CHECK(RequestedPrice(1) == 2);
    CHECK(RequestedPrice(12) == 24);
    CHECK(RequestedPrice(0) == 1);                             // retail's floor of 1 holds
    CHECK(RequestedPrice(-4) == 1);
    const int32_t top = std::numeric_limits<int32_t>::max();
    CHECK(RequestedPrice(top / 2) == (top / 2) * 2);
    CHECK(RequestedPrice(top / 2 + 1) == top);                 // never wraps
    CHECK(RequestedPrice(top) == top);
}

UNIT_TEST(ext_trader_request_combo_labels_are_short_and_carry_the_multiple) {
    CHECK(ComboLabel("Food Merchant") == "Food (2x)");
    CHECK(ComboLabel("General Goods Merchant") == "General Goods (2x)");
    CHECK(ComboLabel("Seed merchant ") == "Seed (2x)");
    CHECK(ComboLabel("Spice TRADER") == "Spice (2x)");                // a mod's trader
    CHECK(ComboLabel("Olga") == "Olga (2x)");                         // no such word: the name as it is
    CHECK(ComboLabel("Merchant") == "Merchant (2x)");                 // only that word: kept
    CHECK(ComboLabel("") == " (2x)");
}

UNIT_TEST(ext_trader_request_state_codec_round_trip) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    ResetState();
    CHECK(SetRequest(7, kSeeds, kModeOnce));
    CHECK(SetRequest(0, kLivestock, kModeAlways));
    CHECK(SetRequest(123456, "Trader, Odd;A#B:C/\xe9", kModeAlways));
    std::vector<uint8_t> payload;
    c->encode(payload);
    const std::vector<Request> before = Requests();
    c->reset();
    CHECK(Requests().empty());
    CHECK(c->decode(payload.data(), payload.size(), 1));
    CHECK(Requests().size() == before.size());
    for (size_t i = 0; i < before.size() && i < Requests().size(); ++i) {
        CHECK(Requests()[i].post == before[i].post && Requests()[i].trader == before[i].trader &&
              Requests()[i].mode == before[i].mode);
    }
    // an empty list is still a valid record
    c->reset();
    payload.clear();
    c->encode(payload);
    CHECK(payload.size() == 4);
    CHECK(c->decode(payload.data(), payload.size(), 1) && Requests().empty());
    ResetState();
}

UNIT_TEST(ext_trader_request_state_codec_refuses_every_damaged_payload) {
    const Ext::SaveTail::StateCodec* c = TheCodec();
    CHECK(c != nullptr);
    if (!c) {
        return;
    }
    ResetState();
    CHECK(SetRequest(7, kSeeds, kModeAlways));
    const std::vector<std::vector<uint8_t>> damaged = {
        {},                                                          // no count
        { 1, 0, 0 },                                                 // a cut-short count
        Payload(2, { { 1, kModeOnce, kSeeds } }),                    // a count the payload cannot hold
        Payload(5000, {}),                                           // more than any town's posts
        Payload(1, { { -1, kModeOnce, kSeeds } }),                   // a negative post
        Payload(1, { { 1, 0, kSeeds } }),                            // unknown modes
        Payload(1, { { 1, 3, kSeeds } }),
        Payload(1, { { 1, kModeOnce, "" } }),                        // an empty trader
        Payload(1, { { 1, kModeOnce, std::string(kMaxTraderBytes + 1, 'a') } }),
        Payload(1, { { 1, kModeOnce, "Seed\tMerchant" } }),          // a control character
        Payload(2, { { 1, kModeOnce, kSeeds }, { 1, kModeAlways, kLivestock } }),   // a post twice
    };
    for (const std::vector<uint8_t>& bytes : damaged) {
        CHECK(!c->decode(bytes.data(), bytes.size(), 1));
    }
    std::vector<uint8_t> good = Payload(1, { { 1, kModeOnce, kSeeds } });
    CHECK(!c->decode(good.data(), good.size(), 2));                  // a version this build does not know
    good.push_back(0);
    CHECK(!c->decode(good.data(), good.size(), 1));                  // bytes left over
    good.pop_back();
    good.pop_back();
    CHECK(!c->decode(good.data(), good.size(), 1));                  // a cut-short trader
    // every refusal left the old list alone
    Request r;
    CHECK(Requests().size() == 1 && RequestOf(7, r) && r.trader == kSeeds && r.mode == kModeAlways);
    ResetState();
}

UNIT_TEST(ext_trader_request_state_travels_in_the_tail) {
    ResetState();
    CHECK(SetRequest(7, kSeeds, kModeAlways));
    Ext::State s = ResolveText("ext.trader_request=1\n");
    Ext::Install(s, Ext::Profile::PerEntry, false);
    Ext::LockForGame(&s);
    std::vector<Ext::SaveTail::Record> recs = Ext::SaveTail::ActiveRecords();
    bool found = false;
    for (const Ext::SaveTail::Record& r : recs) {
        if (r.id == "trader_request") {
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
    Request r;
    CHECK(RequestOf(7, r) && r.trader == kSeeds && r.mode == kModeAlways);   // the request came back with the save
    Ext::UnlockForGame(&s);                                                  // the game ends: the state goes with it
    CHECK(Requests().empty());
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_trader_request_the_faithful_profile_writes_no_record) {
    ResetState();
    CHECK(SetRequest(7, kSeeds, kModeAlways));
    Ext::State s = ResolveText("profile=faithful\next.trader_request=1\n");
    CHECK(!s.ext.trader_request);
    Ext::Install(s, Ext::Profile::Faithful, false);
    Ext::LockForGame(&s);
    for (const Ext::SaveTail::Record& r : Ext::SaveTail::ActiveRecords()) {
        CHECK(r.id != "trader_request");
    }
    CHECK(Ext::SaveTail::EncodeActive().empty());
    Ext::UnlockForGame(&s);
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
    ResetState();
}

UNIT_TEST(ext_trader_request_a_reset_forgets_the_panel_rows) {
    const int before = g_panelsForgotten;
    ResetState();
    CHECK(g_panelsForgotten == before + 1);
}
