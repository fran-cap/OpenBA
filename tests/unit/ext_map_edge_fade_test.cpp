// ============================================================================
// tests/unit/ext_map_edge_fade_test.cpp -- ext.map_edge_fade
// ============================================================================
// src/ext/map_edge_fade/edge_math.h without the game: the registry row (class A, on by default) and the
// fade's arithmetic (the CPU twin of src/render/common/map_edge/map_edge.hlsl).
// ============================================================================
#include "unit.h"
#include "ext/map_edge_fade/edge_math.h"
#include "ext/registry.h"

#include <cmath>
#include <cstring>

using Ext::MapEdgeFade::Band;
using Ext::MapEdgeFade::BandFor;
using Ext::MapEdgeFade::Darkness;
using Ext::MapEdgeFade::Look;
using Ext::MapEdgeFade::TerrainHalf;

UNIT_TEST(ext_map_edge_fade_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("map_edge_fade");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);   // maintainer ruling 2026-11-16
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.map_edge_fade");
}

UNIT_TEST(ext_map_edge_fade_band) {
    const Look look;
    // 513 samples a row span 512 tiles, centred: the ground ends at +-256
    CHECK(TerrainHalf(513) == 256.0f);
    CHECK(TerrainHalf(1) == 0.0f && TerrainHalf(0) == 0.0f);
    const Band b = BandFor(TerrainHalf(513), look);
    CHECK(b.start == 256.0f - look.inset);
    CHECK(std::fabs(b.invWidth - 1.0f / (look.inset + look.outset)) < 1e-7f);
    // a degenerate look or map never divides by zero or starts the fade outside the map
    Look none;
    none.inset = 0.0f;
    none.outset = 0.0f;
    CHECK(std::isfinite(BandFor(256.0f, none).invWidth));
    CHECK(BandFor(2.0f, look).start == 0.0f);
}

UNIT_TEST(ext_map_edge_fade_darkness) {
    const Look look;
    const float half = TerrainHalf(641);
    const Band b = BandFor(half, look);
    // the inside of the terrain is untouched, to the last bit
    const float zero = 0.0f;
    for (float x = -(half - look.inset); x <= half - look.inset; x += 7.5f) {
        const float d = Darkness(x, x * 0.5f, b);
        CHECK(std::memcmp(&d, &zero, sizeof d) == 0);
    }
    // black from `outset` past the edge on, on every side and in the corners
    CHECK(Darkness(half + look.outset, 0.0f, b) == 1.0f);
    CHECK(Darkness(-(half + look.outset), 0.0f, b) == 1.0f);
    CHECK(Darkness(0.0f, half + look.outset, b) == 1.0f);
    CHECK(Darkness(0.0f, -(half + look.outset), b) == 1.0f);
    CHECK(Darkness(half + 500.0f, -half - 500.0f, b) == 1.0f);
    // at the edge itself most of the light is gone, but not all of it
    const float atEdge = Darkness(half, 0.0f, b);
    CHECK(atEdge > 0.8f && atEdge < 1.0f);
    // it only gets darker going out
    float last = 0.0f;
    for (float x = half - look.inset - 1.0f; x <= half + look.outset + 1.0f; x += 0.25f) {
        const float d = Darkness(x, 12.0f, b);
        CHECK(d >= last);
        last = d;
    }
    // the corner is round: on the diagonal the fade is reached later than the two sides' sum would give
    const float side = Darkness(half - look.inset + 4.0f, 0.0f, b);
    const float corner = Darkness(half - look.inset + 4.0f, half - look.inset + 4.0f, b);
    CHECK(corner > side);
    CHECK(corner < Darkness(half - look.inset + 8.0f, 0.0f, b));
    // the four sides are the same
    CHECK(Darkness(half - 3.0f, 5.0f, b) == Darkness(-(half - 3.0f), 5.0f, b));
    CHECK(Darkness(half - 3.0f, 5.0f, b) == Darkness(5.0f, half - 3.0f, b));
}
