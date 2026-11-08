// ============================================================================
// tests/unit/ext_map_overlays_test.cpp -- ext.map_overlays (class C, MAPOVERLAYS.md): the history and its codec
// ============================================================================
// src/ext/map_overlays/overlay_core.cpp and save_state.cpp without the game: the registry row and the codec
// table, the resource table (case-insensitive, full), flows by tile and month (sum, window, year rollover),
// footsteps (saturation, seasons, the spread over a cell's tiles), the wire format's round trip, and every
// damaged payload of §3 leaving the history untouched (the codec then empties it and the load goes on).
// ============================================================================
#include "unit.h"
#include "ext/map_overlays/map_overlays.h"
#include "ext/map_overlays/overlay_core.h"
#include "ext/registry.h"
#include "ext/savetail.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace Ext::MapOverlays;

namespace {

SiteKey At(int x, int z) {
    SiteKey k;
    k.x = static_cast<uint16_t>(x);
    k.z = static_cast<uint16_t>(z);
    return k;
}

float TileOf(const std::vector<float>& grid, int size, int x, int z) {
    return grid[static_cast<size_t>(z) * size + x];
}

float Total(const std::vector<float>& grid) {
    float t = 0.0f;
    for (float v : grid) {
        t += v;
    }
    return t;
}

std::vector<uint8_t> Bytes(const History& h) {
    std::vector<uint8_t> out;
    h.Encode(out);
    return out;
}

void PutU32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b[at + i] = static_cast<uint8_t>(v >> (8 * i));
    }
}

void PutU16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}

// A small town: a 5x5 map (3x3 traffic cells), two resources, two sites, a few steps, at month 100.
// Payload layout (save_state.cpp): 72 bytes of header, then u32 resourceCount at 72.
constexpr const char* kApple = "Template\\Materials\\Apple.rsc";
constexpr const char* kWood = "Template\\Materials\\Wood.rsc";
constexpr size_t kResourceCountAt = 72;
// 76: u16 len + Apple (28 bytes), then u16 len + Wood (27 bytes)
constexpr size_t kAppleLenAt = 76;
constexpr size_t kWoodLenAt = kAppleLenAt + 2 + 28;
constexpr size_t kSiteCountAt = kWoodLenAt + 2 + 27;
constexpr size_t kSite0At = kSiteCountAt + 4;          // u16 x, u16 z, u16 cellCount
constexpr size_t kSite0Cell0At = kSite0At + 6;         // u16 resource, u8 flow, u32 counts[12]
constexpr size_t kCellBytes = 2 + 1 + 48;

History SmallTown() {
    History h;
    h.SetMapSize(5);
    h.AdvanceTo(100);
    const uint16_t apple = h.ResourceIndex(kApple);
    const uint16_t wood = h.ResourceIndex(kWood);
    h.Add(Flow::Produced, At(1, 2), apple, 5);
    h.Add(Flow::Consumed, At(1, 2), wood, 3);
    h.Add(Flow::Produced, At(4, 4), wood, 7);
    h.AddStep(0, 0);
    h.AddStep(4, 3);
    return h;
}

}  // namespace

UNIT_TEST(ext_map_overlays_registry_row) {
    const Ext::Entry* found = Ext::FindEntry("map_overlays");
    CHECK(found != nullptr);
    if (!found) return;
    CHECK(found->tier == Ext::Tier::Extension);
    CHECK(found->cls == Ext::Class::C);
    CHECK(found->defaultOn);   // ruled 2026-11-08 for 1.0.3, DESIGN.md section 2
    CHECK(found->formatVersion == kWireVersion);
    CHECK(Ext::SettingsKey(*found) == "ext.map_overlays");
    const Ext::SaveTail::StateCodec* codec = Ext::SaveTail::FindCodec("map_overlays");
    CHECK(codec != nullptr);
    if (codec) {
        CHECK(codec == &Codec());
        CHECK(codec->encode != nullptr && codec->decode != nullptr && codec->reset != nullptr);
        CHECK(std::strcmp(codec->id, kId) == 0);
    }
}

UNIT_TEST(ext_map_overlays_resource_table_is_case_insensitive) {
    History h;
    h.SetMapSize(8);
    const uint16_t a = h.ResourceIndex("Template\\Apple.rsc");
    CHECK_EQ(a, 0);
    CHECK_EQ(h.ResourceIndex("template\\APPLE.RSC"), a);
    CHECK_EQ(h.FindResource("TEMPLATE\\apple.rsc"), a);
    CHECK_EQ(h.Resources().size(), 1u);
    CHECK_EQ(h.Resources()[0], std::string("Template\\Apple.rsc"));   // the first spelling is kept
    CHECK_EQ(h.FindResource("Template\\Pear.rsc"), History::kNoResource);
    CHECK_EQ(h.ResourceIndex(""), History::kNoResource);
    CHECK_EQ(h.ResourceIndex(std::string(kMaxPathBytes + 1, 'x')), History::kNoResource);
    CHECK(h.ResourceIndex(std::string(kMaxPathBytes, 'x')) != History::kNoResource);
}

UNIT_TEST(ext_map_overlays_resource_table_full) {
    History h;
    h.SetMapSize(4);
    for (size_t i = 0; i < kMaxResources; ++i) {
        const uint16_t idx = h.ResourceIndex("r" + std::to_string(i));
        if (idx != i) {
            CHECK_EQ(idx, static_cast<uint16_t>(i));
            return;
        }
    }
    CHECK_EQ(h.Resources().size(), kMaxResources);
    CHECK_EQ(h.ResourceIndex("one more"), History::kNoResource);
    CHECK_EQ(h.ResourceIndex("R0"), 0);   // still found
}

UNIT_TEST(ext_map_overlays_add_and_sum) {
    History h;
    h.SetMapSize(6);
    const uint16_t apple = h.ResourceIndex(kApple);
    const uint16_t wood = h.ResourceIndex(kWood);
    // before the first AdvanceTo nothing counts
    h.Add(Flow::Produced, At(1, 1), apple, 9);
    h.AddStep(1, 1);
    CHECK(h.Empty());
    h.AdvanceTo(40);
    CHECK_EQ(h.Now(), 40);
    h.Add(Flow::Produced, At(1, 2), apple, 5);
    h.Add(Flow::Produced, At(1, 2), apple, 2);
    h.Add(Flow::Produced, At(1, 2), wood, 4);
    h.Add(Flow::Consumed, At(1, 2), apple, 1);
    h.Add(Flow::Produced, At(5, 0), wood, 3);
    h.Add(Flow::Produced, At(0, 0), apple, 0);                 // nothing
    h.Add(Flow::Produced, At(0, 0), History::kNoResource, 1);  // nothing
    h.Add(Flow::Produced, At(0, 0), 7, 1);                     // outside the table: nothing
    CHECK(!h.Empty());
    std::vector<float> out;
    h.SumFlow(Flow::Produced, {}, kMonths, out);
    CHECK_EQ(out.size(), 36u);
    CHECK_EQ(TileOf(out, 6, 1, 2), 11.0f);
    CHECK_EQ(TileOf(out, 6, 5, 0), 3.0f);
    CHECK_EQ(Total(out), 14.0f);
    // the mask picks resources by table index; an index past the mask is not picked
    h.SumFlow(Flow::Produced, { 1, 0 }, kMonths, out);
    CHECK_EQ(TileOf(out, 6, 1, 2), 7.0f);
    CHECK_EQ(Total(out), 7.0f);
    h.SumFlow(Flow::Produced, { 0 }, kMonths, out);
    CHECK_EQ(Total(out), 0.0f);
    h.SumFlow(Flow::Consumed, {}, kMonths, out);
    CHECK_EQ(Total(out), 1.0f);
    // the count saturates
    h.Add(Flow::Consumed, At(3, 3), apple, 0xffffffffu);
    h.Add(Flow::Consumed, At(3, 3), apple, 5);
    std::vector<uint8_t> appleOnly = { 1, 0 };
    h.SumFlow(Flow::Consumed, appleOnly, 1, out);
    CHECK_EQ(TileOf(out, 6, 3, 3), static_cast<float>(0xffffffffu));
}

UNIT_TEST(ext_map_overlays_outside_the_map_is_ignored) {
    History h;
    h.SetMapSize(4);
    h.AdvanceTo(0);
    const uint16_t r = h.ResourceIndex(kApple);
    h.Add(Flow::Produced, At(4, 0), r, 1);
    h.Add(Flow::Produced, At(0, 4), r, 1);
    h.Add(Flow::Produced, At(0xffff, 0xffff), r, 1);
    h.AddStep(-1, 0);
    h.AddStep(0, -1);
    h.AddStep(4, 0);
    h.AddStep(0, 4);
    h.AddStep(0x7fffffff, 0);
    CHECK(h.Empty());
    // no map at all: nothing either
    History none;
    none.AdvanceTo(0);
    const uint16_t n = none.ResourceIndex(kApple);
    none.Add(Flow::Produced, At(0, 0), n, 1);
    none.AddStep(0, 0);
    CHECK(none.Empty());
    std::vector<float> out(3, 1.0f);
    none.SumFlow(Flow::Produced, {}, kMonths, out);
    CHECK(out.empty());
}

UNIT_TEST(ext_map_overlays_months_roll_over_a_year) {
    History h;
    h.SetMapSize(3);
    const uint16_t r = h.ResourceIndex(kApple);
    for (int m = 100; m < 112; ++m) {
        h.AdvanceTo(m);
        h.Add(Flow::Produced, At(1, 1), r, static_cast<uint32_t>(m - 99));   // 1..12
    }
    std::vector<float> out;
    h.SumFlow(Flow::Produced, {}, kMonths, out);
    CHECK_EQ(TileOf(out, 3, 1, 1), 78.0f);   // 1 + 2 + ... + 12
    h.SumFlow(Flow::Produced, {}, 1, out);
    CHECK_EQ(TileOf(out, 3, 1, 1), 12.0f);
    h.SumFlow(Flow::Produced, {}, 3, out);
    CHECK_EQ(TileOf(out, 3, 1, 1), 33.0f);   // 10 + 11 + 12
    h.SumFlow(Flow::Produced, {}, 0, out);   // clamped to 1
    CHECK_EQ(TileOf(out, 3, 1, 1), 12.0f);
    h.SumFlow(Flow::Produced, {}, 99, out);  // clamped to 12
    CHECK_EQ(TileOf(out, 3, 1, 1), 78.0f);
    // the clock never goes back
    h.AdvanceTo(50);
    CHECK_EQ(h.Now(), 111);
    // month 100 leaves the window
    h.AdvanceTo(112);
    h.SumFlow(Flow::Produced, {}, kMonths, out);
    CHECK_EQ(TileOf(out, 3, 1, 1), 77.0f);
    h.SumFlow(Flow::Produced, {}, 1, out);
    CHECK_EQ(TileOf(out, 3, 1, 1), 0.0f);
    // a jump of more than a year empties everything
    h.AdvanceTo(140);
    h.SumFlow(Flow::Produced, {}, kMonths, out);
    CHECK_EQ(Total(out), 0.0f);
    CHECK(h.Empty());
    // and the clock keeps working
    h.Add(Flow::Produced, At(0, 0), r, 2);
    h.SumFlow(Flow::Produced, {}, 1, out);
    CHECK_EQ(TileOf(out, 3, 0, 0), 2.0f);
}

UNIT_TEST(ext_map_overlays_traffic_saturates_and_spreads) {
    History h;
    h.SetMapSize(5);   // 3x3 cells; the last row and column are one tile wide
    h.AdvanceTo(0);
    for (int i = 0; i < 70000; ++i) {
        h.AddStep(1, 1);
    }
    h.AddStep(4, 4);
    h.AddStep(4, 3);
    h.AddStep(4, 2);   // cell (2, 1)
    std::vector<float> out;
    h.SumTraffic(kMonths, out);
    CHECK_EQ(out.size(), 25u);
    // 65535 over the four tiles of cell (0, 0)
    CHECK_EQ(TileOf(out, 5, 0, 0), 65535.0f / 4.0f);
    CHECK_EQ(TileOf(out, 5, 1, 1), 65535.0f / 4.0f);
    CHECK_EQ(TileOf(out, 5, 4, 4), 1.0f);        // corner cell: one tile
    CHECK_EQ(TileOf(out, 5, 4, 3), 1.0f);        // cell (2, 1): two tiles, two steps
    CHECK_EQ(TileOf(out, 5, 4, 2), 1.0f);
    CHECK_EQ(TileOf(out, 5, 2, 2), 0.0f);
}

UNIT_TEST(ext_map_overlays_traffic_rolls_over_by_season) {
    History h;
    h.SetMapSize(4);
    h.AdvanceTo(100);   // season 33
    h.AddStep(0, 0);
    h.AdvanceTo(103);   // season 34
    h.AddStep(0, 0);
    h.AddStep(0, 0);
    std::vector<float> out;
    h.SumTraffic(3, out);   // this season
    CHECK_EQ(TileOf(out, 4, 0, 0), 0.5f);
    h.SumTraffic(4, out);   // rounded up to two seasons
    CHECK_EQ(TileOf(out, 4, 0, 0), 0.75f);
    h.SumTraffic(kMonths, out);
    CHECK_EQ(TileOf(out, 4, 0, 0), 0.75f);
    h.AdvanceTo(112);   // season 37: 34..37 in the window, 33 out
    h.SumTraffic(kMonths, out);
    CHECK_EQ(TileOf(out, 4, 0, 0), 0.5f);
    h.AdvanceTo(115);   // season 38: 34 out too
    h.SumTraffic(kMonths, out);
    CHECK_EQ(TileOf(out, 4, 0, 0), 0.0f);
    CHECK(h.Empty());
}

UNIT_TEST(ext_map_overlays_map_size_change_clears) {
    History h = SmallTown();
    CHECK(!h.Empty());
    h.SetMapSize(5);   // the same: kept
    CHECK(!h.Empty());
    h.SetMapSize(9);
    CHECK(h.Empty());
    CHECK(h.Resources().empty());
    CHECK_EQ(h.Now(), kNoStamp);
    CHECK_EQ(h.MapSize(), 9);
    h.SetMapSize(kMaxMapSize + 1);   // refused: no map
    CHECK_EQ(h.MapSize(), 0);
}

UNIT_TEST(ext_map_overlays_wire_round_trip) {
    const History h = SmallTown();
    const std::vector<uint8_t> bytes = Bytes(h);
    History back;
    CHECK(back.Decode(bytes.data(), bytes.size()));
    CHECK_EQ(back.MapSize(), 5);
    CHECK_EQ(back.Now(), 100);
    CHECK(back.Resources() == h.Resources());
    CHECK(Bytes(back) == bytes);
    std::vector<float> a;
    std::vector<float> b;
    for (int f = 0; f < 2; ++f) {
        h.SumFlow(static_cast<Flow>(f), {}, kMonths, a);
        back.SumFlow(static_cast<Flow>(f), {}, kMonths, b);
        CHECK(a == b);
    }
    h.SumTraffic(kMonths, a);
    back.SumTraffic(kMonths, b);
    CHECK(a == b);
    // the decoded history goes on counting where the saved one stopped
    back.AddStep(0, 0);
    back.SumTraffic(3, b);
    CHECK_EQ(TileOf(b, 5, 0, 0), 0.5f);
    CHECK_EQ(back.FindResource("template\\materials\\wood.rsc"), 1);
    // an empty history, map or not, round-trips too
    History empty;
    std::vector<uint8_t> e = Bytes(empty);
    History e2 = SmallTown();
    CHECK(e2.Decode(e.data(), e.size()));
    CHECK(e2.Empty());
    CHECK_EQ(e2.MapSize(), 0);
    History sized;
    sized.SetMapSize(kMaxMapSize);
    e = Bytes(sized);
    CHECK(e2.Decode(e.data(), e.size()));
    CHECK_EQ(e2.MapSize(), kMaxMapSize);
}

UNIT_TEST(ext_map_overlays_wire_layout_offsets) {
    // the offsets the damage cases below patch
    const std::vector<uint8_t> b = Bytes(SmallTown());
    CHECK_EQ(b[kResourceCountAt], 2);
    CHECK_EQ(static_cast<size_t>(b[kAppleLenAt]), std::strlen(kApple));
    CHECK_EQ(static_cast<size_t>(b[kWoodLenAt]), std::strlen(kWood));
    CHECK_EQ(b[kSiteCountAt], 2);
    CHECK_EQ(b[kSite0At], 1);       // x
    CHECK_EQ(b[kSite0At + 2], 2);   // z
    CHECK_EQ(b[kSite0At + 4], 2);   // two cells
    CHECK_EQ(b[kSite0Cell0At + 2], 0);   // produced
    const size_t site1At = kSite0Cell0At + 2 * kCellBytes;
    CHECK_EQ(b[site1At], 4);
    const size_t trafficAt = site1At + 6 + kCellBytes;
    CHECK_EQ(b[trafficAt], 3);      // ceil(5 / 2)
    CHECK_EQ(b.size(), trafficAt + 4 + 9 * 8);
}

UNIT_TEST(ext_map_overlays_wire_refuses_every_damaged_payload) {
    const std::vector<uint8_t> good = Bytes(SmallTown());
    const size_t site1At = kSite0Cell0At + 2 * kCellBytes;
    const size_t trafficAt = site1At + 6 + kCellBytes;
    std::vector<std::function<void(std::vector<uint8_t>&)>> damage = {
        [](std::vector<uint8_t>& b) { b.push_back(0); },                              // trailing byte
        [](std::vector<uint8_t>& b) { PutU32(b, 0, kMaxMapSize + 1); },               // map size
        [](std::vector<uint8_t>& b) { PutU32(b, 0, 0xffffffffu); },
        [](std::vector<uint8_t>& b) { PutU32(b, 0, 7); },          // trafficCells 3 != ceil(7 / 2)
        [](std::vector<uint8_t>& b) { PutU32(b, 0, 2); },          // site (4, 4) outside a 2x2 map
        [](std::vector<uint8_t>& b) { PutU32(b, kResourceCountAt, 0xffffffffu); },   // count past payload
        [](std::vector<uint8_t>& b) { PutU32(b, kResourceCountAt, 70000); },
        [](std::vector<uint8_t>& b) { PutU32(b, kResourceCountAt, 3); },
        [](std::vector<uint8_t>& b) { PutU16(b, kAppleLenAt, 0); },                  // empty path
        [](std::vector<uint8_t>& b) { PutU16(b, kAppleLenAt, 0xffff); },             // path past payload
        [](std::vector<uint8_t>& b) { PutU16(b, kAppleLenAt, kMaxPathBytes + 1); },
        [](std::vector<uint8_t>& b) {                                                // a path twice (case)
            const std::string apple = kApple;
            std::vector<uint8_t> copy(b.begin(), b.begin() + kWoodLenAt);   // header + Apple
            copy[kResourceCountAt] = 3;
            copy.push_back(static_cast<uint8_t>(apple.size()));
            copy.push_back(0);
            for (char c : apple) {
                copy.push_back(static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(c))));
            }
            copy.insert(copy.end(), b.begin() + kWoodLenAt, b.end());
            b = copy;
        },
        [](std::vector<uint8_t>& b) { PutU32(b, kSiteCountAt, 0xffffffffu); },       // sites past payload
        [](std::vector<uint8_t>& b) { PutU32(b, kSiteCountAt, 3); },
        [](std::vector<uint8_t>& b) { PutU32(b, kSiteCountAt, 1); },                 // leftovers
        [](std::vector<uint8_t>& b) { PutU16(b, kSite0At, 5); },                     // x outside the map
        [](std::vector<uint8_t>& b) { PutU16(b, kSite0At + 2, 5); },                 // z outside the map
        [](std::vector<uint8_t>& b) { PutU16(b, kSite0At + 4, 0xffff); },            // cells past payload
        [](std::vector<uint8_t>& b) { PutU16(b, kSite0Cell0At, 2); },                // resource index
        [](std::vector<uint8_t>& b) { PutU16(b, kSite0Cell0At, History::kNoResource); },
        [](std::vector<uint8_t>& b) { b[kSite0Cell0At + 2] = 2; },                   // flow
        [](std::vector<uint8_t>& b) {                                                // (resource, flow) twice
            PutU16(b, kSite0Cell0At + kCellBytes, 0);
            b[kSite0Cell0At + kCellBytes + 2] = 0;
        },
        [site1At](std::vector<uint8_t>& b) {                                         // site out of order
            PutU16(b, site1At, 1);
            PutU16(b, site1At + 2, 1);
        },
        [site1At](std::vector<uint8_t>& b) {                                         // a tile twice
            PutU16(b, site1At, 1);
            PutU16(b, site1At + 2, 2);
        },
        [trafficAt](std::vector<uint8_t>& b) { PutU32(b, trafficAt, 4); },           // traffic cells
        [trafficAt](std::vector<uint8_t>& b) { PutU32(b, trafficAt, 0xffffffffu); },
        [trafficAt](std::vector<uint8_t>& b) { PutU32(b, trafficAt, 2); },
    };
    const History before = SmallTown();
    const std::vector<uint8_t> beforeBytes = Bytes(before);
    int index = 0;
    for (const auto& d : damage) {
        std::vector<uint8_t> b = good;
        d(b);
        History h = SmallTown();
        const bool ok = h.Decode(b.data(), b.size());
        if (ok) {
            fprintf(stderr, "  damage case %d was accepted\n", index);
        }
        CHECK(!ok);
        CHECK(Bytes(h) == beforeBytes);   // untouched
        ++index;
    }
    // every cut
    for (size_t n = 0; n < good.size(); ++n) {
        History h = SmallTown();
        if (h.Decode(good.data(), n)) {
            fprintf(stderr, "  a cut at %zu bytes was accepted\n", n);
            CHECK(false);
            break;
        }
    }
    History h = SmallTown();
    CHECK(!h.Decode(nullptr, 4));
    CHECK(Bytes(h) == beforeBytes);
    // a zero map with sites behind it
    std::vector<uint8_t> zero = good;
    PutU32(zero, 0, 0);
    CHECK(!h.Decode(zero.data(), zero.size()));
    // and the good one is accepted
    CHECK(h.Decode(good.data(), good.size()));
}

UNIT_TEST(ext_map_overlays_codec_round_trip_and_damage_is_not_fatal) {
    const Ext::SaveTail::StateCodec& codec = Codec();
    codec.reset();
    CHECK(Town().Empty());
    CHECK_EQ(Town().MapSize(), 0);
    Town() = SmallTown();
    std::vector<uint8_t> payload;
    codec.encode(payload);
    CHECK(payload == Bytes(SmallTown()));
    codec.reset();
    CHECK(Town().Empty());
    CHECK(codec.decode(payload.data(), payload.size(), kWireVersion));
    CHECK(Bytes(Town()) == payload);
    // a damaged payload: the load goes on with an empty history
    std::vector<uint8_t> cut(payload.begin(), payload.begin() + payload.size() / 2);
    CHECK(codec.decode(cut.data(), cut.size(), kWireVersion));
    CHECK(Town().Empty());
    CHECK(Town().Resources().empty());
    // an unknown (older) version reads as damage too
    Town() = SmallTown();
    CHECK(codec.decode(payload.data(), payload.size(), 0));
    CHECK(Town().Empty());
    codec.reset();
}
