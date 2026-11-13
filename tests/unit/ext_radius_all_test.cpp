// ============================================================================
// tests/unit/ext_radius_all_test.cpp -- ext.radius_all
// ============================================================================
// The registry row (class A, ON) and the rules in src/ext/radius_all/radius_all_math.h: which kinds are
// shown after a Shift+click, the centre cell of a footprint (RadiusUI::OnOpened's), when the circles are
// rebuilt, which are dropped and added, and what a long press is. The decals themselves need the running game.
// ============================================================================
#include "unit.h"
#include "ext/radius_all/radius_all_math.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <string>
#include <vector>

namespace {

using IO::SettingsFile::Entries;
using namespace Ext::RadiusAll;

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

// Stand-ins for templates and buildings: only their addresses are used.
int g_gatherer = 0;
int g_forester = 0;
int g_hutA = 0;
int g_hutB = 0;
int g_hutC = 0;

Wanted Want(const void* entity, const void* kind, int32_t cell) {
    Wanted w;
    w.entity = entity;
    w.kind = kind;
    w.cell = cell;
    return w;
}

Circle Have(const void* entity, const void* kind, int32_t cell, int32_t decal) {
    Circle c;
    c.entity = entity;
    c.kind = kind;
    c.cell = cell;
    c.decal = decal;
    return c;
}

}  // namespace

UNIT_TEST(ext_radius_all_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("radius_all");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);   // the game's own decals, none of them saved
    CHECK(e->defaultOn);
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.radius_all");
    CHECK(ResolveText("").ext.radius_all);
    CHECK(!ResolveText("ext.radius_all=0\n").ext.radius_all);
    CHECK(!ResolveText("profile=faithful\next.radius_all=1\n").ext.radius_all);
}

UNIT_TEST(ext_radius_all_a_second_click_on_a_kind_hides_it) {
    std::vector<const void*> kinds;
    CHECK(ToggleKind(kinds, &g_gatherer));
    CHECK(kinds.size() == 1);
    CHECK(ToggleKind(kinds, &g_forester));   // several kinds at once
    CHECK(kinds.size() == 2);
    CHECK(!ToggleKind(kinds, &g_gatherer));
    CHECK(kinds.size() == 1 && kinds[0] == &g_forester);
    CHECK(!ToggleKind(kinds, &g_forester));
    CHECK(kinds.empty());
}

UNIT_TEST(ext_radius_all_centre_cell_is_the_panels) {
    // RadiusUI::OnOpened: (y0 + (h >> 1)) * size + x0 + (w >> 1)
    CHECK(CentreCell(10, 20, 4, 6, 256) == 23 * 256 + 12);
    CHECK(CentreCell(10, 20, 5, 3, 256) == 21 * 256 + 12);   // odd extents round down
    CHECK(CentreCell(0, 0, 1, 1, 64) == 0);
    // a MapComponent not on the map keeps -1 in its rect
    CHECK(CentreCell(-1, -1, -1, -1, 256) == -1);
    CHECK(CentreCell(10, 20, 4, 6, 0) == -1);
    CHECK(CentreCell(255, 20, 4, 6, 256) == -1);   // the centre would be off the map
}

UNIT_TEST(ext_radius_all_digest_follows_the_buildings_and_their_order) {
    const uint64_t a = DigestAdd(DigestAdd(kDigestSeed, 1), 2);
    CHECK(a == DigestAdd(DigestAdd(kDigestSeed, 1), 2));
    CHECK(a != DigestAdd(DigestAdd(kDigestSeed, 2), 1));
    CHECK(a != DigestAdd(kDigestSeed, 1));
    CHECK(a != DigestAdd(a, 0));   // one more building, even a null one
}

UNIT_TEST(ext_radius_all_diff_keeps_the_circles_of_buildings_still_standing) {
    const std::vector<Circle> have = {Have(&g_hutA, &g_gatherer, 100, 3), Have(&g_hutB, &g_gatherer, 200, 4)};
    // hut A stays, hut B was removed, hut C is new
    const std::vector<Wanted> want = {Want(&g_hutA, &g_gatherer, 100), Want(&g_hutC, &g_gatherer, 300)};
    std::vector<size_t> drop;
    std::vector<size_t> add;
    Diff(have, want, drop, add);
    CHECK(drop.size() == 1 && drop[0] == 1);
    CHECK(add.size() == 1 && add[0] == 1);
    // nothing changed: nothing to do
    const std::vector<Wanted> same = {Want(&g_hutA, &g_gatherer, 100), Want(&g_hutB, &g_gatherer, 200)};
    Diff(have, same, drop, add);
    CHECK(drop.empty() && add.empty());
}

UNIT_TEST(ext_radius_all_diff_redraws_a_building_at_a_reused_address) {
    // a new building at the address of a removed one, on another spot: the old circle goes, a new one comes
    const std::vector<Circle> have = {Have(&g_hutA, &g_gatherer, 100, 3)};
    const std::vector<Wanted> want = {Want(&g_hutA, &g_gatherer, 500)};
    std::vector<size_t> drop;
    std::vector<size_t> add;
    Diff(have, want, drop, add);
    CHECK(drop.size() == 1 && drop[0] == 0);
    CHECK(add.size() == 1 && add[0] == 0);
    // a kind hidden: every circle of it goes
    Diff(have, std::vector<Wanted>(), drop, add);
    CHECK(drop.size() == 1 && add.empty());
    // a kind shown: every building of it gets one
    Diff(std::vector<Circle>(), want, drop, add);
    CHECK(drop.empty() && add.size() == 1);
}

UNIT_TEST(ext_radius_all_long_press_is_a_still_finger_held_half_a_second) {
    const float edge = 1080.0f;
    CHECK(!IsLongPress(0.2f, 0.0f, edge));    // a tap
    CHECK(!IsLongPress(0.49f, 0.0f, edge));
    CHECK(IsLongPress(0.5f, 0.0f, edge));
    CHECK(IsLongPress(2.0f, 10.0f, edge));    // a trembling finger
    CHECK(!IsLongPress(2.0f, 40.0f, edge));   // a drag: 3% of the short edge is 32.4
}
