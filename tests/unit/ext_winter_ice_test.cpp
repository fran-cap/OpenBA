// ============================================================================
// tests/unit/ext_winter_ice_test.cpp -- ext.winter_ice: the ice model, the open-water mask, the program
// ============================================================================
// The freeze / thaw fronts (src/ext/winter_ice/ice_model.h) driven by made-up weather, the mask round
// buildings in a lake (open_mask.h), the generated ice program (src/render/common/winter_ice/
// ice_overlay_ps.h, tools/water_shaders.py: a valid ps_4_0 container reading the lake vertex program's
// interpolants) and the registry row (class A, ON by default). Every platform.
// ============================================================================
#include "unit.h"

#include "better_trees/dxbc.h"
#include "winter_ice/ice_overlay_ps.h"
#include "ext/winter_ice/ice_model.h"
#include "ext/winter_ice/open_mask.h"
#include "ext/boat_lane/boat_lane.h"
#include "ext/registry.h"

#include <cstring>
#include <string>
#include <vector>

using namespace Ext::WinterIce;

namespace {

uint32_t U32(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, d.data() + at, 4);
    return v;
}

// `seconds` of game time at `temperature`, in one-second steps (the frame steps are small).
void Hold(IceState& s, float temperature, int seconds) {
    for (int i = 0; i < seconds; ++i) Step(s, temperature, 1.0f);
}

}  // namespace

UNIT_TEST(ext_winter_ice_no_ice_above_freezing) {
    IceState s;
    Hold(s, 40.0f, 3600);
    CHECK(!HasIce(s));
    CHECK(s.freeze == 0.0f && s.thaw == 0.0f);
}

UNIT_TEST(ext_winter_ice_banks_first_then_solid) {
    IceState s;
    Hold(s, 26.0f, 75);              // a quarter month at 6 below: the shallows only
    CHECK(HasIce(s));
    CHECK(s.freeze > 0.1f && s.freeze < 0.5f);
    CHECK(!Solid(s));
    Hold(s, 26.0f, 250);             // most of a month more: the middle is closing, not closed
    CHECK(!Solid(s));
    CHECK(s.freeze > 1.0f);
    Hold(s, 26.0f, 300);             // a month more: the whole lake
    CHECK(Solid(s));
    CHECK(s.freeze <= kFreezeCap);
}

UNIT_TEST(ext_winter_ice_thaw_opens_the_sides_then_clears) {
    IceState s;
    Hold(s, 20.0f, 600);
    CHECK(Solid(s));
    Hold(s, 36.0f, 60);              // the first warm days: the sides open, the middle holds
    CHECK(HasIce(s));
    CHECK(!Solid(s));
    CHECK(s.thaw > 0.0f && s.thaw < 1.0f);
    Hold(s, 40.0f, 900);
    CHECK(!HasIce(s));
    CHECK(s.freeze == 0.0f && s.thaw == 0.0f);
}

UNIT_TEST(ext_winter_ice_cold_snap_refreezes_the_edges) {
    IceState s;
    Hold(s, 20.0f, 600);
    Hold(s, 36.0f, 60);
    const float opened = s.thaw;
    CHECK(opened > 0.0f);
    Hold(s, 25.0f, 60);
    CHECK(s.thaw < opened);
}

UNIT_TEST(ext_winter_ice_long_steps_are_not_integrated) {
    IceState s;
    Step(s, 0.0f, kMaxStepSeconds + 1.0f);   // a load or a stall
    CHECK(s.freeze == 0.0f);
    Step(s, 0.0f, 0.0f);
    Step(s, 0.0f, -5.0f);
    CHECK(s.freeze == 0.0f);
}

UNIT_TEST(ext_winter_ice_guess_fits_the_day) {
    CHECK(!HasIce(Guess(50.0f)));
    CHECK(HasIce(Guess(28.0f)));
    CHECK(Solid(Guess(10.0f)));
}

UNIT_TEST(ext_winter_ice_open_mask_round_a_blocked_cell) {
    const int size = 16;
    std::vector<uint8_t> mask;
    BuildOpenMask({BuildingStamp(8 * size + 8, size)}, size, mask);
    CHECK(mask.size() == static_cast<size_t>(size * size));
    CHECK(mask[8 * size + 8] == 255);
    CHECK(mask[8 * size + 9] == 255);                     // within kOpenFull
    CHECK(mask[8 * size + 10] > 0 && mask[8 * size + 10] < 255);
    CHECK(mask[8 * size + 12] == 0);                      // past kOpenReach
    CHECK(mask[0] == 0);
}

UNIT_TEST(ext_winter_ice_open_mask_clips_at_the_map_edge) {
    const int size = 8;
    std::vector<uint8_t> mask;
    BuildOpenMask({BuildingStamp(0, size), BuildingStamp(size * size - 1, size)}, size, mask);
    CHECK(mask[0] == 255);
    CHECK(mask[size * size - 1] == 255);
    std::vector<uint8_t> empty;
    BuildOpenMask({}, 0, empty);
    CHECK(empty.empty());
}

// The boat channel: a line across the map leaves an open strip along it, ice beside it.
UNIT_TEST(ext_winter_ice_boat_channel_is_open_along_the_line) {
    const int size = 32;
    std::vector<OpenStamp> stamps;
    AddChannel({-2.0f, 16.5f, 34.0f, 16.5f}, stamps);   // from beyond one edge to beyond the other, row 16
    CHECK(!stamps.empty());
    std::vector<uint8_t> mask;
    BuildOpenMask(stamps, size, mask);
    for (int x = 0; x < size; ++x) {
        CHECK(mask[16 * size + x] == 255);   // on the line, edge to edge
        CHECK(mask[24 * size + x] == 0);     // well off it
    }
    CHECK(mask[17 * size + 10] == 255);      // within kChannelFull
    CHECK(mask[19 * size + 10] > 0 && mask[19 * size + 10] < 255);
}

UNIT_TEST(ext_winter_ice_program_is_a_ps_4_0_container) {
    Dxbc::Container c;
    std::string err;
    CHECK(Dxbc::Parse(g_wi_PSIceOverlay, sizeof(g_wi_PSIceOverlay), &c, &err));
    uint32_t sum[4];
    Dxbc::Checksum(g_wi_PSIceOverlay, sizeof(g_wi_PSIceOverlay), sum);
    CHECK(std::memcmp(sum, c.checksum, sizeof(sum)) == 0);
    const Dxbc::Chunk* shdr = c.Find(Dxbc::FourCC('S', 'H', 'D', 'R'));
    CHECK(shdr != nullptr);
    if (shdr == nullptr) return;
    const uint32_t version = U32(shdr->data, 0);
    CHECK((version >> 16) == 0);          // pixel
    CHECK(((version >> 4) & 0xF) == 4);   // ps_4_0
    CHECK((version & 0xF) == 0);
}

// The lake vertex program's outputs, by name and register (the Vulkan backend links by semantic name).
UNIT_TEST(ext_winter_ice_program_reads_the_lake_interpolants) {
    struct Want { const char* name; uint32_t index; uint32_t reg; };
    const Want want[5] = {
        { "SV_Position", 0, 0 },
        { "TEXCOORD", 1, 1 },
        { "TEXCOORD", 2, 2 },
        { "TEXCOORD", 3, 3 },
        { "TEXCOORD", 4, 4 },
    };
    Dxbc::Container c;
    std::string err;
    CHECK(Dxbc::Parse(g_wi_PSIceOverlay, sizeof(g_wi_PSIceOverlay), &c, &err));
    const Dxbc::Chunk* isgn = c.Find(Dxbc::FourCC('I', 'S', 'G', 'N'));
    CHECK(isgn != nullptr);
    if (isgn == nullptr) return;
    const std::vector<uint8_t>& d = isgn->data;
    const uint32_t count = U32(d, 0);
    CHECK(count == 5);
    if (count != 5) return;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t e = 8 + i * 24;
        const char* name = reinterpret_cast<const char*>(d.data() + U32(d, e));
        CHECK(std::strcmp(name, want[i].name) == 0);
        CHECK(U32(d, e + 4) == want[i].index);
        CHECK(U32(d, e + 16) == want[i].reg);
    }
}

UNIT_TEST(ext_winter_ice_row_is_class_a_default_on) {
    const Ext::Entry* row = Ext::FindEntry("winter_ice");
    CHECK(row != nullptr);
    if (row == nullptr) return;
    CHECK(row->cls == Ext::Class::A);
    CHECK(row->defaultOn);   // class A ON, the standing rule (the user's ruling, 2026-11-06)
}

// The channel's width follows the overlay's half width: a wider channel opens cells a narrow one leaves.
UNIT_TEST(ext_winter_ice_boat_channel_width) {
    const int size = 32;
    std::vector<OpenStamp> narrow;
    std::vector<OpenStamp> wide;
    AddChannel({-2.0f, 16.5f, 34.0f, 16.5f}, narrow, 1.0f);
    AddChannel({-2.0f, 16.5f, 34.0f, 16.5f}, wide, 4.0f);
    std::vector<uint8_t> a;
    std::vector<uint8_t> b;
    BuildOpenMask(narrow, size, a);
    BuildOpenMask(wide, size, b);
    CHECK(a[20 * size + 10] == 0);     // 4 cells off the line: past 1 + kChannelEdge
    CHECK(b[20 * size + 10] == 255);   // within the wide half width
    CHECK(a[16 * size + 10] == 255 && b[16 * size + 10] == 255);
}

// The boats' way keeps to the deep water (cost 1) over the shallows (cost 5), as BoatRoute_FindWaterPath's
// costs do.
UNIT_TEST(ext_winter_ice_boat_path_prefers_deep_water) {
    const int size = 10;
    std::vector<uint8_t> types(size * size, 2);   // land
    for (int x = 0; x < size; ++x) {
        types[2 * size + x] = 7;   // a shallow row
        types[3 * size + x] = 8;   // a deep row beside it
    }
    types[1 * size + 9] = 7;       // the dock's water cell, above the shallow row's end
    const std::vector<int> path = Ext::BoatLane::BoatWaterPath(types.data(), size, 2 * size + 0, 1 * size + 9);
    CHECK(!path.empty());
    if (path.empty()) return;
    CHECK(path.front() == 2 * size + 0);
    CHECK(path.back() == 1 * size + 9);
    // A dock on land is no goal: the game's A* tests the goal's type like any neighbour's, the leg
    // fails, and retail's route (and the channel) keeps the polyline alone.
    types[1 * size + 9] = 2;
    CHECK(Ext::BoatLane::BoatWaterPath(types.data(), size, 2 * size + 0, 1 * size + 9).empty());
    int deep = 0;
    for (const int c : path) deep += types[c] == 8 ? 1 : 0;
    CHECK(deep >= 6);              // the middle of the way runs along the deep row
}

UNIT_TEST(ext_winter_ice_boat_path_needs_joined_water) {
    const int size = 8;
    std::vector<uint8_t> types(size * size, 2);
    types[0] = 8;
    types[size * size - 1] = 8;    // two pools with land between
    CHECK(Ext::BoatLane::BoatWaterPath(types.data(), size, 0, size * size - 1).empty());
    CHECK(Ext::BoatLane::BoatWaterPath(types.data(), size, -1, 3).empty());
    CHECK(Ext::BoatLane::CellPathLine({0, size + 1}, size).size() == 4);
}

// The channel's lane: the centre pull keeps a leg off the bank, and the spline keeps the ends.
UNIT_TEST(ext_winter_ice_channel_keeps_to_the_middle) {
    const int size = 12;
    std::vector<uint8_t> types(size * size, 2);
    for (int y = 2; y <= 8; ++y) {
        for (int x = 0; x < size; ++x) types[y * size + x] = 8;   // a river 7 cells wide, rows 2..8
    }
    const std::vector<float> bank = Ext::BoatLane::BankDistance(types.data(), size);
    CHECK(bank[5 * size + 5] > bank[2 * size + 5]);               // the middle row lies furthest from land
    // From the top bank's edge on the left to the top bank's edge on the right.
    const std::vector<int> hug = Ext::BoatLane::BoatWaterPath(types.data(), size, 2 * size + 0, 2 * size + 11);
    const std::vector<int> mid = Ext::BoatLane::BoatWaterPath(types.data(), size, 2 * size + 0, 2 * size + 11, bank.data(), 2.0f);
    CHECK(!hug.empty() && !mid.empty());
    int hugRow = 0;
    int midRow = 0;
    for (const int c : hug) hugRow = c / size > hugRow ? c / size : hugRow;
    for (const int c : mid) midRow = c / size > midRow ? c / size : midRow;
    CHECK(hugRow == 2);                                            // without the pull: along the bank
    CHECK(midRow >= 4);                                            // with it: out towards the middle
    const std::vector<float> line = Ext::BoatLane::CellPathLine(mid, size);
    const std::vector<float> smooth = Ext::BoatLane::SmoothLine(line, 3);
    CHECK(smooth.size() >= line.size());
    CHECK(smooth[0] == line[0] && smooth[1] == line[1]);
    CHECK(smooth[smooth.size() - 2] == line[line.size() - 2] && smooth.back() == line.back());
}
