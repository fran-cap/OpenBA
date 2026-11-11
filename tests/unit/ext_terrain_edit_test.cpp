// ============================================================================
// tests/unit/ext_terrain_edit_test.cpp -- ext.terrain_edit
// ============================================================================
// The registry row (class B, OFF, locked) and the pure part of the brush
// (src/ext/terrain_edit/terrain_edit_math.h):
// the disc, its falloff and the height-word arithmetic. The tool and the page need the running game.
// ============================================================================
#include "unit.h"
#include "ext/terrain_edit/plant_math.h"
#include "ext/terrain_edit/terrain_edit_math.h"
#include "ext/terrain_edit/terrain_undo.h"
#include "ext/terrain_edit/terrain_water.h"
#include "ext/terrain_edit/terrain_texture.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace {

using IO::SettingsFile::Entries;

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

}  // namespace

UNIT_TEST(ext_terrain_edit_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("terrain_edit");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::B);     // play diverges; the save already carries what the brush writes
    CHECK(!e->defaultOn);
    CHECK(e->formatVersion == 1);
    CHECK(Ext::SettingsKey(*e) == "ext.terrain_edit");
    CHECK(!ResolveText("").ext.terrain_edit);
    CHECK(ResolveText("ext.terrain_edit=1\n").ext.terrain_edit);
    CHECK(!ResolveText("profile=faithful\n").ext.terrain_edit);
}

UNIT_TEST(ext_terrain_edit_falloff_is_a_smooth_bump) {
    using namespace Ext::TerrainEdit;
    CHECK(Falloff(0.0f, 6.0f) == 1.0f);
    CHECK(Falloff(6.0f, 6.0f) == 0.0f);
    CHECK(Falloff(9.0f, 6.0f) == 0.0f);
    CHECK(Falloff(1.0f, 0.0f) == 0.0f);
    float prev = 1.0f;
    for (float d = 0.0f; d <= 6.0f; d += 0.25f) {
        const float f = Falloff(d, 6.0f);
        CHECK(f <= prev + 1e-6f);   // never rises with distance
        CHECK(f >= 0.0f && f <= 1.0f);
        prev = f;
    }
}

UNIT_TEST(ext_terrain_edit_disc_is_centred_clipped_and_clamped) {
    using namespace Ext::TerrainEdit;
    const std::vector<Cell> d = Disc(10, 10, 4.0f, 64);
    CHECK(!d.empty());
    int centre = 0;
    for (const Cell& c : d) {
        CHECK(c.col >= 6 && c.col <= 14 && c.row >= 6 && c.row <= 14);
        CHECK(c.weight >= 0.0f && c.weight <= 1.0f);
        if (c.col == 10 && c.row == 10) {
            ++centre;
            CHECK(c.weight == 1.0f);
        }
    }
    CHECK(centre == 1);
    // at a corner of the map only the cells inside it are returned
    for (const Cell& c : Disc(0, 0, 4.0f, 64)) CHECK(c.col >= 0 && c.row >= 0 && c.col < 64 && c.row < 64);
    CHECK(Disc(0, 0, 4.0f, 0).empty());
    // a radius beyond the limits is clamped
    CHECK(Disc(40, 40, 1000.0f, 128).size() == Disc(40, 40, kMaxRadius, 128).size());
}

UNIT_TEST(ext_terrain_edit_raised_word_is_clamped) {
    using namespace Ext::TerrainEdit;
    CHECK(RaisedWord(0, 1.0f) == 256);
    CHECK(RaisedWord(100, 0.5f) == 228);
    CHECK(RaisedWord(32700, 10.0f) == 32767);
    CHECK(RaisedWord(-32700, -10.0f) == -32768);
    CHECK(RaisedWord(5, 0.0f) == 5);
}

UNIT_TEST(ext_terrain_edit_raise_and_lower_move_the_ground) {
    using namespace Ext::TerrainEdit;
    CHECK(NextWord(0, Mode::Raise, 1.0f, 1.0f, 0.0f) > 0);
    CHECK(NextWord(0, Mode::Raise, 0.0f, 1.0f, 0.0f) == 0);            // no weight: no change
    // strength scales
    CHECK(NextWord(100, Mode::Raise, 1.0f, 2.0f, 0.0f) > NextWord(100, Mode::Raise, 1.0f, 1.0f, 0.0f));
    CHECK(NextWord(100, Mode::Lower, 1.0f, 1.0f, 0.0f) < 100);
    // lowering stops at the floor, which is deep enough to dig a lake, and never raises ground that is
    // already below it
    const int floorWord = static_cast<int>(std::llround(kLowerFloor * 256.0f));
    int w = 200;
    for (int i = 0; i < 400; ++i) w = NextWord(w, Mode::Lower, 1.0f, 4.0f, 0.0f);
    CHECK(w == floorWord);
    CHECK(static_cast<float>(floorWord) / 256.0f < -1.4f);    // under the classifier's deep-water line
    // below the floor already: left alone
    CHECK(NextWord(floorWord - 100, Mode::Lower, 1.0f, 1.0f, 0.0f) == floorWord - 100);
    // above it: still lowered
    CHECK(NextWord(-400, Mode::Lower, 1.0f, 1.0f, 0.0f) < -400);
}

UNIT_TEST(ext_terrain_edit_raise_stops_at_the_height_ceiling) {
    using namespace Ext::TerrainEdit;
    const int ceilingWord = static_cast<int>(std::llround(kHeightCeiling * 256.0f));
    CHECK(kHeightCeiling < 16.0f);   // the terrain chunks are drawn between -16 and +16
    int w = 0;
    for (int i = 0; i < 5000; ++i) w = NextWord(w, Mode::Raise, 1.0f, 4.0f, 0.0f);
    CHECK(w == ceilingWord);
    // ground already above the ceiling is left where it is, not pulled down
    CHECK(NextWord(ceilingWord + 100, Mode::Raise, 1.0f, 4.0f, 0.0f) == ceilingWord + 100);
    // a step that would pass the ceiling lands on it
    CHECK(NextWord(ceilingWord - 1, Mode::Raise, 1.0f, 4.0f, 0.0f) == ceilingWord);
}

UNIT_TEST(ext_terrain_edit_flatten_converges_inside_the_buildable_band) {
    using namespace Ext::TerrainEdit;
    // the target is the starting height held inside the band the game builds on (-0.25 to 0.75)
    CHECK(FlattenTarget(0) == 0.0f);
    CHECK(FlattenTarget(5000) == kFlattenMax);
    CHECK(FlattenTarget(-5000) == kFlattenMin);
    CHECK(kFlattenMin > -0.25f && kFlattenMax < 0.75f);
    const float target = 0.3f;
    const int targetWord = static_cast<int>(std::llround(target * 256.0f));
    int above = 900;
    int below = -300;
    for (int i = 0; i < 300; ++i) {
        above = NextWord(above, Mode::Flatten, 1.0f, 1.0f, target);
        below = NextWord(below, Mode::Flatten, 1.0f, 1.0f, target);
    }
    CHECK(std::abs(above - targetWord) <= 8);
    CHECK(std::abs(below - targetWord) <= 8);
    // a vertex at the edge of the disc (no weight) is untouched
    CHECK(NextWord(900, Mode::Flatten, 0.0f, 1.0f, target) == 900);
}

UNIT_TEST(ext_terrain_edit_mountain_moves_toward_its_aim_and_never_lowers) {
    using namespace Ext::TerrainEdit;
    const float aim = 3.0f;
    int w = 0;
    for (int i = 0; i < 300; ++i) w = NextWord(w, Mode::Mountain, 1.0f, 1.0f, aim);
    CHECK(static_cast<float>(w) / 256.0f > 2.9f);
    CHECK(static_cast<float>(w) / 256.0f <= aim + 0.05f);
    // ground already higher than the aim stays where it is
    const int high = static_cast<int>(std::llround((aim + 1.0f) * 256.0f));
    CHECK(NextWord(high, Mode::Mountain, 1.0f, 4.0f, aim) == high);
}

UNIT_TEST(ext_terrain_edit_mountain_height_is_a_peaked_cone) {
    using namespace Ext::TerrainEdit;
    // the weakest peak is still above the game's mountain line (2.0)
    CHECK(kMountainBase + kMountainPerStrength * kMinStrength > 2.0f);
    const float radius = 8.0f;
    const uint32_t seed = 12345u;
    // at the centre the height is the peak, within the noise; beyond the radius it is 0
    for (float strength : { 0.5f, 1.0f, 2.0f, 4.0f }) {
        const float peak = kMountainBase + kMountainPerStrength * strength;
        const float top = MountainHeight(0.0f, radius, strength, 40, 40, seed);
        const float high = peak * (1.0f + kMountainRugged);
        CHECK(top >= peak * (1.0f - kMountainRugged) - 1e-4f);
        CHECK(top <= (high < kHeightCeiling ? high : kHeightCeiling) + 1e-4f);
        CHECK(top > 2.0f);
        CHECK(top <= kHeightCeiling);   // never above the chunks' height range (16)
    }
    CHECK(MountainHeight(radius, radius, 1.0f, 40, 40, seed) == 0.0f);
    CHECK(MountainHeight(radius + 3.0f, radius, 1.0f, 40, 40, seed) == 0.0f);
    CHECK(MountainHeight(1.0f, 0.0f, 1.0f, 40, 40, seed) == 0.0f);
    // a cone, not a dome: halfway out it is well under half the peak's height (a dome's would be over half)
    const float peak1 = kMountainBase + kMountainPerStrength;
    float sum = 0.0f;
    int n = 0;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            sum += MountainHeight(radius * 0.5f, radius, 1.0f, x * 7, y * 7, seed);
            ++n;
        }
    }
    CHECK(sum / static_cast<float>(n) < 0.45f * peak1);
    // the same inputs give the same height; another seed gives another mountain
    CHECK(MountainHeight(3.0f, radius, 1.0f, 41, 39, seed) == MountainHeight(3.0f, radius, 1.0f, 41, 39,
        seed));
    int differ = 0;
    for (int i = 0; i < 20; ++i) {
        const float a = MountainHeight(3.0f, radius, 1.0f, 30 + i, 50, seed);
        const float b = MountainHeight(3.0f, radius, 1.0f, 30 + i, 50, seed + 7u);
        if (a != b) ++differ;
    }
    CHECK(differ > 10);
}

UNIT_TEST(ext_terrain_edit_smooth_moves_toward_the_neighbours_in_both_directions) {
    using namespace Ext::TerrainEdit;
    CHECK(kModeCount == 6);   // the five brushes and the resource tool
    // a spike above its neighbours' average comes down, a pit below it comes up, and neither overshoots
    const float neighbours = 1.0f;
    const int avgWord = static_cast<int>(std::llround(neighbours * 256.0f));
    int spike = 1500;
    int pit = -500;
    for (int i = 0; i < 40; ++i) {
        const int s = NextWord(spike, Mode::Smooth, 1.0f, 1.0f, neighbours);
        const int p = NextWord(pit, Mode::Smooth, 1.0f, 1.0f, neighbours);
        CHECK(s <= spike && s >= avgWord);
        CHECK(p >= pit && p <= avgWord);
        spike = s;
        pit = p;
    }
    CHECK(std::abs(spike - avgWord) <= 4);
    CHECK(std::abs(pit - avgWord) <= 4);
    // no weight, no change; more strength moves faster; ground already at the average stays
    CHECK(NextWord(900, Mode::Smooth, 0.0f, 1.0f, neighbours) == 900);
    CHECK(NextWord(900, Mode::Smooth, 1.0f, 4.0f, neighbours) < NextWord(900, Mode::Smooth, 1.0f, 1.0f,
        neighbours));
    CHECK(NextWord(avgWord, Mode::Smooth, 1.0f, 1.0f, neighbours) == avgWord);
}

UNIT_TEST(ext_terrain_edit_undo_keeps_the_first_value_of_a_stroke) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    CHECK(u.Depth() == 0);
    u.RecordHeight(5, 100);   // no stroke open: ignored
    u.BeginStroke();
    u.RecordHeight(5, 100);
    u.RecordHeight(5, 140);   // a later frame over the same vertex: the first value stays
    u.RecordHeight(6, -20);
    u.RecordType(9, 2);
    u.RecordType(9, 10);
    u.CommitStroke();
    CHECK(u.Depth() == 1);
    Stroke s;
    CHECK(u.Pop(s));
    CHECK(s.heights.size() == 2 && s.heights.at(5) == 100 && s.heights.at(6) == -20);
    CHECK(s.types.size() == 1 && s.types.at(9) == 2);
    CHECK(u.Depth() == 0);
    CHECK(!u.Pop(s));
}

// An undo gives a cell its old type back only while it still holds the type the stroke wrote there: the
// stroke keeps the first type before and the last type written, and records nothing outside a stroke.
UNIT_TEST(ext_terrain_edit_undo_records_the_type_the_stroke_wrote) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    u.RecordWrittenType(9, 6);   // no stroke open: ignored
    u.BeginStroke();
    u.RecordType(9, 2);
    u.RecordWrittenType(9, 10);
    u.RecordType(9, 10);          // a later frame retypes the cell again: the type before the stroke stays 2
    u.RecordWrittenType(9, 6);    // and the type written is the last one
    u.RecordType(11, 7);
    u.RecordWrittenType(11, 2);
    u.CommitStroke();
    Stroke s;
    CHECK(u.Pop(s));
    CHECK(s.types.size() == 2 && s.types.at(9) == 2 && s.types.at(11) == 7);
    CHECK(s.written.size() == 2 && s.written.at(9) == 6 && s.written.at(11) == 2);
}

// The Resource drop down shows a template by its short name: no folder, no ".rsc:resource", no
// "NaturalResource".
UNIT_TEST(ext_terrain_edit_resource_names_are_short) {
    using namespace Ext::TerrainEdit;
    CHECK(ResourceShortName("Template\\NaturalResourceWillowTree.rsc:resource") == "WillowTree");
    CHECK(ResourceShortName("Template\\Flax\\NaturalResourceFlax.rsc:resource") == "Flax");
    CHECK(ResourceShortName("Template/NaturalResourceRock.rsc:resource") == "Rock");
    CHECK(ResourceShortName("Template\\Mushroom.rsc") == "Mushroom");   // no prefix to take off
    CHECK(ResourceShortName("Odd") == "Odd");
    CHECK(ResourceShortName("").empty());
}

// Trees first, plants between, rocks, ore and salt last; each group by name.
UNIT_TEST(ext_terrain_edit_resources_are_listed_trees_then_plants_then_rocks) {
    using namespace Ext::TerrainEdit;
    const std::vector<std::string> names = { "Rock", "Reed", "Tree", "Ore",
                                             "WillowTree", "Bushes", "ShoreSalt", "Grass" };
    const std::vector<size_t> order = ResourceOrder(names);
    CHECK(order.size() == names.size());
    const std::vector<std::string> expected = { "Tree", "WillowTree", "Bushes", "Grass",
                                                "Reed", "Ore", "Rock", "ShoreSalt" };
    for (size_t i = 0; i < order.size() && i < expected.size(); ++i) {
        CHECK(names[order[i]] == expected[i]);
    }
    CHECK(ResourceOrder(std::vector<std::string>()).empty());
}

// The density is how full the disc ends up: the share of its eligible cells, rounded up, and what already
// stands there counts toward it.
UNIT_TEST(ext_terrain_edit_density_is_a_share_of_the_eligible_cells) {
    using namespace Ext::TerrainEdit;
    CHECK(PlantTarget(113, 0.1f) == 12);   // 11.3 rounds up
    CHECK(PlantTarget(113, 0.5f) == 57);
    CHECK(PlantTarget(113, 1.0f) == 113);
    CHECK(PlantTarget(10, 1.0f) == 10);
    CHECK(PlantTarget(3, 0.1f) == 1);      // a click on a small disc at a low density still plants one
    CHECK(PlantTarget(0, 0.5f) == 0);      // nothing eligible
    CHECK(PlantTarget(50, 0.0f) == 0);
    CHECK(kMinDensity > 0.0f && kMaxDensity <= 1.0f && kDefaultDensity >= kMinDensity &&
          kDefaultDensity <= kMaxDensity);
}

// A stroke picks exactly the number of cells it needs, the same ones for the same seed, and other cells for
// another seed.
UNIT_TEST(ext_terrain_edit_a_stroke_picks_the_cells_it_needs) {
    using namespace Ext::TerrainEdit;
    std::vector<int> cells;
    for (int i = 0; i < 100; ++i) cells.push_back(1000 + i * 3);
    const std::vector<int> a = PickCells(cells, 12, 7u);
    CHECK(a.size() == 12);
    CHECK(a == PickCells(cells, 12, 7u));   // the same seed picks the same cells
    for (const int c : a) CHECK(std::find(cells.begin(), cells.end(), c) != cells.end());
    std::vector<int> sorted = a;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());   // no cell twice
    CHECK(a != PickCells(cells, 12, 8u));   // another stroke picks other cells
    CHECK(PickCells(cells, 0, 7u).empty());
    CHECK(PickCells(cells, -3, 7u).empty());
    CHECK(PickCells(cells, 100, 7u).size() == 100);   // all of them
    CHECK(PickCells(cells, 500, 7u).size() == 100);   // more than there are: all of them
    CHECK(PickCells(std::vector<int>(), 5, 7u).empty());
}

// A stroke remembers what it planted, and a stroke that only planted is still a stroke.
UNIT_TEST(ext_terrain_edit_undo_remembers_what_a_stroke_planted) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    u.RecordPlanted(3, 0x1000, 1u);   // no stroke open: ignored
    u.BeginStroke();
    u.RecordPlanted(3, 0x1000, 1u);
    u.RecordPlanted(9, 0x2000, 2u);
    u.CommitStroke();
    CHECK(u.Depth() == 1);   // planted resources alone make a step
    Stroke s;
    CHECK(u.Pop(s));
    CHECK(s.planted.size() == 2);
    CHECK(s.planted[0].cell == 3 && s.planted[0].entity == 0x1000 && s.planted[0].generation == 1u);
    CHECK(s.planted[1].cell == 9 && s.planted[1].entity == 0x2000 && s.planted[1].generation == 2u);
    CHECK(s.heights.empty() && s.types.empty());
}

namespace {

// A stroke that changed one height word, painted over the given cells.
void PaintStroke(Ext::TerrainEdit::UndoStack& u, int firstCell, int frames, int vertex,
                 bool keepRedo = false) {
    u.BeginStroke(keepRedo);
    u.RecordTool(2, "Oak");
    for (int i = 0; i < frames; ++i) u.RecordFrame(firstCell + i, 3.0f, 1.5f, 0.5f);
    u.RecordHeight(vertex, 100);
    u.CommitStroke();
}

}  // namespace

// A stroke remembers how it was painted, and an Undo can hand that to the redo list.
UNIT_TEST(ext_terrain_edit_redo_gets_how_the_stroke_was_painted) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    PaintStroke(u, 40, 3, 7);
    Stroke s;
    CHECK(u.Pop(s));
    CHECK(s.replay.mode == 2 && s.replay.plant == "Oak" && s.replay.frames.size() == 3);
    CHECK(s.replay.frames[0].cell == 40 && s.replay.frames[2].cell == 42);
    CHECK(s.replay.frames[1].radius == 3.0f && s.replay.frames[1].strength == 1.5f &&
          s.replay.frames[1].density == 0.5f);
    u.PushRedo(std::move(s.replay));
    CHECK(u.RedoDepth() == 1);
    Replay r;
    CHECK(u.PopRedo(r));
    CHECK(r.frames.size() == 3 && r.mode == 2);
    CHECK(u.RedoDepth() == 0 && !u.PopRedo(r));
}

// Redo is a fork in the road: a new stroke that changes something ends the way forward, a Redo's own stroke
// and a stroke that changed nothing do not.
UNIT_TEST(ext_terrain_edit_redo_list_ends_with_a_new_stroke) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    PaintStroke(u, 10, 2, 1);
    PaintStroke(u, 20, 2, 2);
    for (int i = 0; i < 2; ++i) {
        Stroke s;
        CHECK(u.Pop(s));
        u.PushRedo(std::move(s.replay));
    }
    CHECK(u.RedoDepth() == 2);
    u.BeginStroke();            // a click that changed nothing
    u.RecordFrame(30, 3.0f, 1.0f, 0.5f);
    u.CommitStroke();
    CHECK(u.RedoDepth() == 2);
    Replay first;
    CHECK(u.PopRedo(first));    // the Redo: paints its stroke again and keeps the rest
    PaintStroke(u, first.frames[0].cell, 2, 3, true);
    CHECK(u.RedoDepth() == 1 && u.Depth() == 1);
    PaintStroke(u, 50, 1, 4);   // anything new
    CHECK(u.RedoDepth() == 0);
}

// The strokes can be read from the top, and the version moves when they change.
UNIT_TEST(ext_terrain_edit_undo_strokes_are_read_from_the_top_and_the_version_moves) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    const uint32_t v0 = u.Version();
    PaintStroke(u, 10, 1, 1);
    PaintStroke(u, 20, 1, 2);
    CHECK(u.Version() != v0);
    CHECK(u.FromTop(0) && u.FromTop(0)->replay.frames[0].cell == 20);
    CHECK(u.FromTop(1) && u.FromTop(1)->replay.frames[0].cell == 10);
    CHECK(u.FromTop(2) == nullptr);
    const uint32_t v1 = u.Version();
    Stroke s;
    CHECK(u.Pop(s));
    CHECK(u.Version() != v1);
    const uint32_t v2 = u.Version();
    u.Clear();
    CHECK(u.Version() != v2);
}

UNIT_TEST(ext_terrain_edit_redo_list_is_cleared_with_the_world_and_capped) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    Replay empty;
    u.PushRedo(std::move(empty));   // a stroke with no frames cannot be painted again
    CHECK(u.RedoDepth() == 0);
    for (size_t i = 0; i < kMaxUndoStrokes + 5; ++i) {
        Replay r;
        r.frames.push_back(ReplayFrame{ static_cast<int>(i), 1.0f, 1.0f, 0.5f });
        u.PushRedo(std::move(r));
    }
    CHECK(u.RedoDepth() == kMaxUndoStrokes);
    Replay top;
    CHECK(u.PopRedo(top) && top.frames[0].cell == static_cast<int>(kMaxUndoStrokes + 4));
    u.Clear();
    CHECK(u.RedoDepth() == 0);
}

UNIT_TEST(ext_terrain_edit_undo_is_last_in_first_out_and_skips_empty_strokes) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    u.BeginStroke();
    u.CommitStroke();         // changed nothing: not a step
    CHECK(u.Depth() == 0);
    for (int i = 1; i <= 3; ++i) {
        u.BeginStroke();
        u.RecordHeight(i, static_cast<int16_t>(i * 10));
        u.CommitStroke();
    }
    CHECK(u.Depth() == 3);
    Stroke s;
    CHECK(u.Pop(s) && s.heights.count(3) == 1);
    CHECK(u.Pop(s) && s.heights.count(2) == 1);
    CHECK(u.Pop(s) && s.heights.count(1) == 1);
    CHECK(!u.Pop(s));
    // a second commit with no stroke open does nothing
    u.CommitStroke();
    CHECK(u.Depth() == 0);
}

UNIT_TEST(ext_terrain_edit_undo_drops_the_oldest_stroke_past_the_limit_and_clears) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    const int total = static_cast<int>(kMaxUndoStrokes) + 5;
    for (int i = 0; i < total; ++i) {
        u.BeginStroke();
        u.RecordHeight(i, 1);
        u.CommitStroke();
    }
    CHECK(u.Depth() == kMaxUndoStrokes);
    Stroke s;
    CHECK(u.Pop(s) && s.heights.count(total - 1) == 1);   // the newest is the first out
    while (u.Pop(s)) {}
    // the oldest kept is stroke 5: strokes 0 to 4 were dropped
    CHECK(s.heights.count(5) == 1);
    u.BeginStroke();
    u.RecordHeight(1, 1);
    u.Clear();
    CHECK(u.Depth() == 0 && !u.Open());
}

UNIT_TEST(ext_terrain_edit_water_patch_covers_the_cells_taken_under_water) {
    using namespace Ext::TerrainEdit;
    // a 6 x 6 vertex grid of dry ground with a 3 x 3 pit of -2 in the middle (vertices 2 to 4)
    auto height = [](int vx, int vy) { return (vx >= 2 && vx <= 4 && vy >= 2 && vy <= 4) ? -2.0f : 0.5f; };
    auto never = [](int, int) { return false; };
    const int halfGrid = 3;
    const WaterPatch patch = BuildWaterPatch(height, 0, 0, 5, 5, halfGrid, -188, never);
    // cells with a corner in the pit: lower corners 1 to 4 on each axis = 16 cells
    CHECK(patch.cells.size() == 16);
    CHECK(patch.indices.size() == 16 * 6);
    CHECK(!patch.overflow);
    // corners are shared between neighbouring quads: the 5 x 5 vertices around the pit, not 16 x 4
    CHECK(patch.vertices.size() == 25);
    for (const uint16_t i : patch.indices) CHECK(i < patch.vertices.size());
    for (const WaterVertex& v : patch.vertices) {
        CHECK(v.position[2] == -188);
        CHECK(v.position[0] % 48 == 0 && v.position[1] % 48 == 0);
        CHECK(v.uv[0] == v.position[0] && v.uv[1] == v.position[1]);
        CHECK(v.normal[0] == 0x7f && v.normal[1] == 0x7f);
    }
    // a vertex in the pit has all of the shore weight, one on dry ground at 0.5 has none
    int full = 0;
    int none = 0;
    for (const WaterVertex& v : patch.vertices) {
        if (v.normal[2] == 255) ++full;
        if (v.normal[2] == 0) ++none;
    }
    CHECK(full == 9);
    CHECK(none > 0);
    // dry ground makes no water at all
    auto dry = [](int, int) { return 0.5f; };
    CHECK(BuildWaterPatch(dry, 0, 0, 5, 5, halfGrid, 0, never).indices.empty());
}

UNIT_TEST(ext_terrain_edit_water_patch_skips_cells_the_mesh_already_draws) {
    using namespace Ext::TerrainEdit;
    auto pit = [](int, int) { return -2.0f; };
    const int halfGrid = 4;
    // the cell whose lower corner is world (col, row) = (vx - 4, vy - 4): skip the cell at vertex (1, 1) and
    // (2, 2)
    auto drawn = [&](int col, int row) {
        return (col == 1 - halfGrid && row == 1 - halfGrid) || (col == 2 - halfGrid && row == 2 - halfGrid);
    };
    const WaterPatch patch = BuildWaterPatch(pit, 0, 0, 4, 4, halfGrid, 0, drawn);
    CHECK(patch.cells.size() == 16 - 2);
    for (const uint64_t cell : patch.cells) {
        CHECK(cell != WaterCellKey(1 - halfGrid, 1 - halfGrid));
        CHECK(cell != WaterCellKey(2 - halfGrid, 2 - halfGrid));
    }
    // cell keys are distinct for distinct cells, negative coordinates included
    CHECK(WaterCellKey(-5, 7) != WaterCellKey(7, -5));
    CHECK(WaterCellKey(-1, -1) != WaterCellKey(0, 0));
    CHECK(WaterCellKey(-300, 400) == WaterCellKey(-300, 400));
}

UNIT_TEST(ext_terrain_edit_shore_weight_fades_with_the_ground_height) {
    using namespace Ext::TerrainEdit;
    CHECK(ShoreWeight(kWaterTest) == 255);
    CHECK(ShoreWeight(-3.0f) == 255);
    CHECK(ShoreWeight(kShoreTop) == 0);
    CHECK(ShoreWeight(2.0f) == 0);
    int prev = 256;
    for (float h = -1.0f; h <= 0.5f; h += 0.05f) {
        const int w = ShoreWeight(h);
        CHECK(w <= prev);   // never grows as the ground rises
        prev = w;
    }
}

UNIT_TEST(ext_terrain_edit_undo_remembers_the_water_meshs_sizes_once) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    u.BeginStroke();
    u.RecordWater(1000, 6000, 3);
    u.RecordWater(2000, 9000, 4);   // a second note in the same stroke does not replace the first
    u.CommitStroke();
    Stroke s;
    CHECK(u.Pop(s));
    CHECK(s.hasWater && s.waterVertices == 1000 && s.waterIndices == 6000 && s.waterRanges == 3);
    CHECK(!s.Empty());   // a stroke that only added water is still a step
    // no stroke open: ignored
    u.RecordWater(1, 2, 3);
    u.BeginStroke();
    u.CommitStroke();
    CHECK(u.Depth() == 0);
}

UNIT_TEST(ext_terrain_edit_undo_keeps_the_quads_a_stroke_took_out) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    u.BeginStroke();
    const uint16_t a[6] = { 1, 2, 3, 1, 3, 4 };
    const uint16_t b[6] = { 5, 6, 7, 5, 7, 8 };
    u.RecordWater(100, 600, 2);
    u.RecordKilledQuad(12, a);
    u.RecordKilledQuad(30, b);
    u.CommitStroke();
    Stroke s;
    CHECK(u.Pop(s));
    CHECK(s.killedQuads.size() == 2);
    CHECK(s.killedQuads[0].position == 12 && s.killedQuads[0].indices[5] == 4);
    CHECK(s.killedQuads[1].position == 30 && s.killedQuads[1].indices[1] == 6);
    // no stroke open: ignored
    u.RecordKilledQuad(1, a);
    u.BeginStroke();
    u.CommitStroke();
    CHECK(u.Depth() == 0);
}

UNIT_TEST(ext_terrain_edit_a_patch_takes_the_height_of_the_water_it_is_given) {
    using namespace Ext::TerrainEdit;
    CHECK(kDefaultWaterZ == -191);   // the first lake's: 0 above the lowest of the game's lake heights
    CHECK(kEditMarker != 0);         // the game writes 0 in the normal's fourth byte
    auto pit = [](int, int) { return -2.0f; };
    auto never = [](int, int) { return false; };
    // two patches built for the same water match each other, and every vertex carries the edit marker and no
    // other value in z
    const WaterPatch p1 = BuildWaterPatch(pit, 0, 0, 3, 3, 2, -187, never);
    const WaterPatch p2 = BuildWaterPatch(pit, 3, 0, 6, 3, 2, -187, never);
    CHECK(!p1.vertices.empty() && !p2.vertices.empty());
    for (const WaterVertex& v : p1.vertices) CHECK(v.position[2] == -187 && v.normal[3] == kEditMarker);
    for (const WaterVertex& v : p2.vertices) CHECK(v.position[2] == -187 && v.normal[3] == kEditMarker);
    // and a patch for other water takes that water's height
    const WaterPatch p3 = BuildWaterPatch(pit, 0, 0, 3, 3, 2, kDefaultWaterZ, never);
    for (const WaterVertex& v : p3.vertices) CHECK(v.position[2] == kDefaultWaterZ);
}

UNIT_TEST(ext_terrain_edit_uv_map_of_a_river_triangle) {
    using namespace Ext::TerrainEdit;
    UvMap m;
    // v grows along +x at 1/32 per unit and u across, along +y, over a channel 8 wide
    const float x[3] = { 4.0f, 14.0f, 4.0f };
    const float y[3] = { 2.0f, 2.0f, 12.0f };
    const float u[3] = { 0.25f, 0.25f, 0.25f + 10.0f / 8.0f };
    const float v[3] = { 0.5f, 0.5f + 10.0f / 32.0f, 0.5f };
    CHECK(UvMapOfTriangle(x, y, u, v, m));
    CHECK(std::fabs(m.vX - 1.0f / 32.0f) < 1e-5f && std::fabs(m.vY) < 1e-5f);
    CHECK(std::fabs(m.uX) < 1e-5f && std::fabs(m.uY - 1.0f / 8.0f) < 1e-5f);
    // the map reproduces the three vertices, and continues the plane beyond them
    for (int i = 0; i < 3; ++i) {
        CHECK(std::fabs(m.uBase + m.uX * x[i] + m.uY * y[i] - u[i]) < 1e-4f);
        CHECK(std::fabs(m.vBase + m.vX * x[i] + m.vY * y[i] - v[i]) < 1e-4f);
    }
    CHECK(std::fabs((m.uBase + m.uX * 4.0f + m.uY * 22.0f) - (0.25f + 20.0f / 8.0f)) < 1e-4f);
    // a thin triangle has no map; neither has one where nothing changes
    const float xt[3] = { 0.0f, 5.0f, 10.0f };
    const float yt[3] = { 0.0f, 0.0f, 0.0f };
    CHECK(!UvMapOfTriangle(xt, yt, u, v, m));
    const float flat[3] = { 0.3f, 0.3f, 0.3f };
    CHECK(!UvMapOfTriangle(x, y, flat, flat, m));
}

UNIT_TEST(ext_terrain_edit_point_in_triangle) {
    using namespace Ext::TerrainEdit;
    const float x[3] = { 0.0f, 10.0f, 0.0f };
    const float y[3] = { 0.0f, 0.0f, 10.0f };
    CHECK(PointInTriangle(2.0f, 2.0f, x, y));
    CHECK(PointInTriangle(0.0f, 0.0f, x, y));     // a corner is inside
    CHECK(!PointInTriangle(8.0f, 8.0f, x, y));
    CHECK(!PointInTriangle(-1.0f, 3.0f, x, y));
    // the same triangle wound the other way
    const float x2[3] = { 0.0f, 0.0f, 10.0f };
    const float y2[3] = { 0.0f, 10.0f, 0.0f };
    CHECK(PointInTriangle(2.0f, 2.0f, x2, y2));
    CHECK(!PointInTriangle(8.0f, 8.0f, x2, y2));
}

UNIT_TEST(ext_terrain_edit_a_river_patch_continues_the_rivers_texture) {
    using namespace Ext::TerrainEdit;
    auto pit = [](int, int) { return -2.0f; };
    auto never = [](int, int) { return false; };
    WaterStyle style;
    style.z = -192;
    style.river = true;
    style.map.uBase = 0.25f;
    style.map.uX = 0.0f;
    style.map.uY = 1.0f / 8.0f;
    style.map.vBase = -0.5f;
    style.map.vX = 1.0f / 32.0f;
    style.map.vY = 0.0f;
    const WaterPatch patch = BuildWaterPatchStyled(pit, 0, 0, 4, 4, 2, style, never);
    CHECK(!patch.vertices.empty());
    bool uVaries = false;
    for (const WaterVertex& v : patch.vertices) {
        CHECK(v.position[2] == -192 && v.normal[3] == kEditMarker);
        const float x = static_cast<float>(v.position[0]) / 48.0f;
        const float y = static_cast<float>(v.position[1]) / 48.0f;
        CHECK(std::fabs(static_cast<float>(v.uv[0]) - (0.25f + y / 8.0f) * 1024.0f) <= 1.0f);
        CHECK(std::fabs(static_cast<float>(v.uv[1]) - (-0.5f + x / 32.0f) * 1024.0f) <= 1.0f);
        if (v.uv[0] != patch.vertices[0].uv[0]) uVaries = true;
    }
    CHECK(uVaries);   // across the channel the texture changes: not one stretched column
    // a lake-style patch keeps the planar uv
    WaterStyle lake;
    lake.z = -191;
    const WaterPatch p2 = BuildWaterPatchStyled(pit, 0, 0, 4, 4, 2, lake, never);
    for (const WaterVertex& v : p2.vertices) CHECK(v.uv[0] == v.position[0] && v.uv[1] == v.position[1]);
}

UNIT_TEST(ext_terrain_edit_a_river_patch_takes_each_vertexs_uv_from_the_nearest_anchor) {
    using namespace Ext::TerrainEdit;
    auto pit = [](int, int) { return -2.0f; };
    auto never = [](int, int) { return false; };
    WaterStyle style;
    style.z = -192;
    style.river = true;
    // two anchors that map the texture differently: one near the patch's west end, one near its east end
    UvAnchor west;
    west.x = -6.0f;
    west.y = 0.0f;
    west.map.uBase = 0.0f;
    west.map.uX = 0.0f;
    west.map.uY = 0.0f;
    west.map.vBase = 1.0f;
    west.map.vX = 0.0f;
    west.map.vY = 0.0f;
    UvAnchor east = west;
    east.x = 14.0f;
    east.map.vBase = 2.0f;
    style.anchors.push_back(west);
    style.anchors.push_back(east);
    // a patch from world x = -4 to 9
    const WaterPatch patch = BuildWaterPatchStyled(pit, 0, 0, 13, 3, 4, style, never);
    CHECK(!patch.vertices.empty());
    int fromWest = 0;
    int fromEast = 0;
    for (const WaterVertex& v : patch.vertices) {
        const float x = static_cast<float>(v.position[0]) / 48.0f;
        if (std::fabs(static_cast<float>(v.uv[1]) - 1024.0f) < 1.0f) {
            ++fromWest;
            // the west half of the span between the two anchors (a tie at 4 goes to the first)
            CHECK(x <= 4.0f);
        } else if (std::fabs(static_cast<float>(v.uv[1]) - 2048.0f) < 1.0f) {
            ++fromEast;
            CHECK(x > 4.0f);
        } else {
            CHECK(false);
        }
    }
    CHECK(fromWest > 0 && fromEast > 0);
    // with no anchors the style's own map is used
    WaterStyle plain;
    plain.river = true;
    plain.map.vBase = 0.5f;
    plain.map.vX = 0.0f;
    plain.map.vY = 0.0f;
    const WaterPatch p2 = BuildWaterPatchStyled(pit, 0, 0, 3, 3, 2, plain, never);
    for (const WaterVertex& v : p2.vertices) CHECK(std::fabs(static_cast<float>(v.uv[1]) - 512.0f) < 1.0f);
}

UNIT_TEST(ext_terrain_edit_the_rock_byte_follows_the_height) {
    using namespace Ext::TerrainEdit;
    // below the floor: all rock (the lake bed and its shore), whatever the scatter
    CHECK(RockByte(-0.6f, 0) == 255);
    CHECK(RockByte(-3.0f, 77) == 255);
    // ordinary ground: the scatter as it is
    CHECK(RockByte(1.0f, 0) == 0);
    CHECK(RockByte(1.0f, 255) == 255);
    CHECK(RockByte(1.0f, 63) == 63);
    // above 8 rock ramps in over 3 units, never taking away what the scatter gave
    CHECK(RockByte(8.0f, 0) == 0);
    CHECK(RockByte(9.5f, 0) == 127);
    CHECK(RockByte(11.0f, 0) == 255);
    CHECK(RockByte(20.0f, 0) == 255);
    CHECK(RockByte(9.5f, 200) == 200);
}

UNIT_TEST(ext_terrain_edit_the_scatter_matches_the_looks_it_is_given) {
    using namespace Ext::TerrainEdit;
    float nm = 0.0f;
    float ns = 0.0f;
    ScatterNoiseStats(5u, nm, ns);
    CHECK(nm > 0.3f && nm < 0.7f && ns > 0.01f && ns < 0.5f);
    ScatterLook look;
    look.mean = 40.0f;
    look.spread = 12.0f;
    // the mean noise gives the mean; one spread above gives mean + spread
    CHECK(ScatterByte(nm, nm, ns, look) == 40);
    CHECK(ScatterByte(nm + ns, nm, ns, look) >= 51 && ScatterByte(nm + ns, nm, ns, look) <= 52);
    // over a patch the scatter has about the asked mean and spread, and stays inside a byte
    double sum = 0.0;
    double sum2 = 0.0;
    int count = 0;
    for (int r = 0; r < 64; ++r) {
        for (int c = 0; c < 64; ++c) {
            const uint32_t v = ScatterByte(ScatterNoise01(c, r, 5u), nm, ns, look);
            CHECK(v <= 255);
            sum += static_cast<double>(v);
            sum2 += static_cast<double>(v) * static_cast<double>(v);
            ++count;
        }
    }
    const double mean = sum / count;
    const double spread = std::sqrt(sum2 / count - mean * mean);
    CHECK(std::fabs(mean - 40.0) < 2.0);
    CHECK(std::fabs(spread - 12.0) < 2.0);
    // a look that would go below 0 or above 255 is held to a byte
    ScatterLook wild;
    wild.mean = 250.0f;
    wild.spread = 400.0f;
    for (float n = 0.0f; n <= 1.0f; n += 0.1f) CHECK(ScatterByte(n, nm, ns, wild) <= 255);
}

UNIT_TEST(ext_terrain_edit_the_grass_byte_follows_the_slope_and_height) {
    using namespace Ext::TerrainEdit;
    CHECK(GrassByte(1.0f, 1.0f) == 0);                  // flat: none
    CHECK(GrassByte(1.0f, 0.80f) == 255);               // steep: all
    CHECK(GrassByte(1.0f, kGrassLower) == 255);
    CHECK(GrassByte(1.0f, kGrassUpper) == 0);
    int prev = 256;
    for (float nz = kGrassLower + 0.001f; nz < kGrassUpper; nz += 0.01f) {
        const int g = GrassByte(1.0f, nz);
        CHECK(g <= prev);   // the feathering only falls as the ground flattens
        prev = g;
    }
    CHECK(GrassByte(7.0f, 1.0f) == 0);                  // the height ramp starts above 7
    CHECK(GrassByte(8.5f, 1.0f) == 127);
    CHECK(GrassByte(12.0f, 1.0f) == 255);
}

UNIT_TEST(ext_terrain_edit_the_normal_and_the_occlusion_of_a_grid) {
    using namespace Ext::TerrainEdit;
    const int dim = 40;
    auto flat = [](int, int) { return 0.5f; };
    float n[3];
    CellNormal(flat, dim, 20, 20, n);
    CHECK(std::fabs(n[0]) < 1e-6f && std::fabs(n[1]) < 1e-6f && std::fabs(n[2] - 1.0f) < 1e-6f);
    // a slope rising along +x leans the normal away from +x
    auto slope = [](int c, int) { return 0.25f * static_cast<float>(c); };
    CellNormal(slope, dim, 20, 20, n);
    CHECK(n[0] < -0.2f && n[2] > 0.5f && n[2] < 1.0f);
    CHECK(std::fabs(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] - 1.0f) < 1e-5f);
    // open flat ground has the most light, a pit less
    CHECK(OcclusionByte(flat, dim, 20, 20) >= 250);
    auto pit = [](int c, int r) { return (c == 20 && r == 20) ? -2.0f : 0.0f; };
    // the pit's own cell sees every neighbour above it
    CHECK(OcclusionByte(pit, dim, 20, 20) < OcclusionByte(flat, dim, 20, 20));
    auto bowl = [](int c, int r) {
        const float dx = static_cast<float>(c - 20);
        const float dy = static_cast<float>(r - 20);
        return 0.15f * (dx * dx + dy * dy);
    };
    CHECK(OcclusionByte(bowl, dim, 20, 20) < OcclusionByte(flat, dim, 20, 20));
}

UNIT_TEST(ext_terrain_edit_the_scatter_noise_is_in_range_and_repeatable) {
    using namespace Ext::TerrainEdit;
    float lo = 1.0f;
    float hi = 0.0f;
    for (int r = 0; r < 60; ++r) {
        for (int c = 0; c < 60; ++c) {
            const float v = ScatterNoise01(c, r, 77u);
            CHECK(v >= 0.0f && v <= 1.0f);
            CHECK(v == ScatterNoise01(c, r, 77u));
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    }
    CHECK(hi - lo > 0.3f);   // it varies
    CHECK(ScatterNoise01(5, 5, 1u) != ScatterNoise01(5, 5, 2u) ||
          ScatterNoise01(9, 3, 1u) != ScatterNoise01(9, 3, 2u));
}

UNIT_TEST(ext_terrain_edit_undo_keeps_the_texels_a_stroke_repainted) {
    using namespace Ext::TerrainEdit;
    UndoStack u;
    u.RecordTexel(1, 0x11223344u);   // no stroke open: ignored
    u.BeginStroke();
    u.RecordTexel(7, 0xAABBCCDDu);
    u.RecordTexel(7, 0x01020304u);   // the first value stays
    u.RecordHeight(3, 5);
    CHECK(u.Current().heights.size() == 1 && u.Current().texels.size() == 1);
    u.CommitStroke();
    Stroke s;
    CHECK(u.Pop(s));
    CHECK(s.texels.size() == 1 && s.texels.at(7) == 0xAABBCCDDu);
    // a stroke that only repainted texels is still a step
    u.BeginStroke();
    u.RecordTexel(9, 1u);
    u.CommitStroke();
    CHECK(u.Depth() == 1);
}

UNIT_TEST(ext_terrain_edit_value_noise_is_smooth_and_bounded) {
    using namespace Ext::TerrainEdit;
    float prev = ValueNoise(0.0f, 0.0f, 9u);
    for (int i = 1; i <= 400; ++i) {
        const float v = ValueNoise(static_cast<float>(i) * 0.05f, 3.3f, 9u);
        CHECK(v >= -1.0f && v <= 1.0f);
        // no jumps between nearby points (the steepest the smoothstep blend allows is about 0.23)
        CHECK(std::fabs(v - prev) < 0.3f);
        prev = v;
    }
}

// review F-245 / F-246 / F-249: a contour draw range comes from the save unchecked. The brush reads one
// only when start + count stays inside the index array (with no int overflow) and base + any 16-bit index
// stays a valid, non-negative int.
UNIT_TEST(ext_terrain_edit_contour_range_fits_refuses_overflowing_ranges) {
    using namespace Ext::TerrainEdit;
    CHECK(ContourRangeFits(0, 0, 12, 12));
    CHECK(ContourRangeFits(6, 100, 6, 12));
    CHECK(ContourRangeFits(12, 0, 0, 12));
    CHECK(!ContourRangeFits(7, 0, 6, 12));    // runs one past the indices
    CHECK(!ContourRangeFits(-1, 0, 6, 12));
    CHECK(!ContourRangeFits(0, -1, 6, 12));
    CHECK(!ContourRangeFits(0, 0, -6, 12));
    // start + count wraps negative in int: the old `start + count > size` passed it
    CHECK(!ContourRangeFits(0x7FFFFFF0, 0, 0x20, 12));
    CHECK(!ContourRangeFits(0x7FFFFF00, 0, 0x200, 12));
    // base + a 16-bit index wraps negative in int: the old `v >= size` passed it
    CHECK(!ContourRangeFits(0, 0x7FFFFFFF, 6, 12));
    CHECK(!ContourRangeFits(0, 0x7FFFFFF5, 3, 12));
    CHECK(ContourRangeFits(0, 0x7FFFFFFF - 0xFFFF, 6, 12));
    CHECK(!ContourRangeFits(0, 0x7FFFFFFF - 0xFFFF + 1, 6, 12));
}
