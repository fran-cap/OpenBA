// ============================================================================
// tests/unit/clear_area_road_test.cpp -- the Remove Roads marker's done test
// ============================================================================
// The Remove Roads tool (Template/ClearRoad.rsc) drops a Template/Clear.rsc entity on a
// finished road cell (RemovalTool::CalcPath 0x1401035e0). Its ClearArea (_clearGround set)
// is done when ClearAreaComponent::IsDone (0x140118440) says so: the ground test
// Map::IsGroundClear (0x14009d860) and then the occupant test Map::IsAreaEmpty
// (0x14009dac0). A road cell has no occupant, so the occupant count (CountRemaining
// 0x140118330) is 0 there while the ground test still fails. State0Enter (0x140117960)
// tests IsDone only; finishing on a zero count deleted the marker before a laborer was
// posted, so roads could not be removed. These tests drive the real Map bodies
// (src/game/Game/maparea.cpp, linked) over a Map record built in raw bytes.
// ============================================================================
#include "unit.h"
#include "Game/Map.h"
#include "Action/PathAvl.h"   // AvlNode28 (Map +0xE8)

#include <cstdint>
#include <cstring>

namespace {

constexpr int kDim = 8;

struct TestMap {
    alignas(16) uint8_t mapBytes[sizeof(Map)];
    alignas(16) uint8_t cellBytes[sizeof(MapCell) * kDim * kDim];

    TestMap() {
        std::memset(mapBytes, 0, sizeof(mapBytes));
        std::memset(cellBytes, 0, sizeof(cellBytes));
        Map* map = Get();
        map->size = kDim;
        map->cells._data = reinterpret_cast<MapCell*>(cellBytes);
        map->cells._size = kDim * kDim;
        map->cells._capacity = kDim * kDim;
        for (int i = 0; i < kDim * kDim; ++i) {
            MapCell& c = Cell(i);
            c.terrainType = PathType::Normal;          // buildable
            c.field_0x2A = MapCell::kNoRoadBatch;   // no road pending
        }
    }
    Map* Get() { return reinterpret_cast<Map*>(mapBytes); }
    MapCell& Cell(int index) { return reinterpret_cast<MapCell*>(cellBytes)[index]; }
    MapCell& Cell(int x, int y) { return Cell(y * kDim + x); }

    // A stand-in occupant: IsAreaEmpty only compares the pointer with null.
    void SetOccupant(int x, int y, uintptr_t fake) {
        std::memcpy(reinterpret_cast<uint8_t*>(&Cell(x, y)) + offsetof(MapCell, occupant), &fake,
                    sizeof(fake));
    }
};

// What a finished road cell holds (a retail save, RetailA.sav): road terrain 0 or 1, the
// road kind left at +0x2A by PlaceRoadCell, no occupant, not in the road-cell tree.
void MakeFinishedRoad(MapCell& c, PathType terrain) {
    c.footprintCount = 1;
    c.terrainType = terrain;
    c.field_0x2A = 0;
    c.field_0x2B = 0xff;
}

}  // namespace

UNIT_TEST(clear_area_free_ground_is_clear_and_empty) {
    TestMap t;
    CHECK(t.Get()->IsGroundClear(0, 0, kDim, kDim));
    CHECK(t.Get()->IsAreaEmpty(0, 0, kDim, kDim));
}

UNIT_TEST(clear_area_finished_road_is_empty_but_not_clear) {
    for (const PathType terrain : {PathType::Faster, PathType::Fast}) {
        TestMap t;
        MakeFinishedRoad(t.Cell(3, 4), terrain);
        // The Remove Roads marker's one-cell footprint: nothing stands on it...
        CHECK(t.Get()->IsAreaEmpty(3, 4, 1, 1));
        // ...but the road is still there, so the clear is not done.
        CHECK(!t.Get()->IsGroundClear(3, 4, 1, 1));
        // The neighbours are untouched.
        CHECK(t.Get()->IsGroundClear(2, 4, 1, 1));
        CHECK(t.Get()->IsGroundClear(4, 4, 1, 1));
        CHECK(!t.Get()->IsGroundClear(0, 0, kDim, kDim));
    }
}

UNIT_TEST(clear_area_removed_road_is_clear) {
    TestMap t;
    MakeFinishedRoad(t.Cell(5, 1), PathType::Faster);
    CHECK(!t.Get()->IsGroundClear(5, 1, 1, 1));
    // What Map::RemovePendingRoad (0x14009c700) leaves once the laborer removes it:
    // terrain 2, +0x2A back to 0xff.
    MapCell& c = t.Cell(5, 1);
    c.terrainType = PathType::Normal;
    c.field_0x2A = MapCell::kNoRoadBatch;
    CHECK(t.Get()->IsGroundClear(5, 1, 1, 1));
}

UNIT_TEST(clear_area_pending_road_is_not_clear) {
    TestMap t;
    t.Cell(1, 1).field_0x2A = 0;   // a road queued on buildable ground (PlaceRoadCell)
    CHECK(!t.Get()->IsGroundClear(1, 1, 1, 1));
    CHECK(t.Get()->IsAreaEmpty(1, 1, 1, 1));
}

UNIT_TEST(clear_area_road_tree_cell_is_not_clear) {
    TestMap t;
    // A cell in the road-cell tree (+0xE8) with buildable terrain and no road kind.
    AvlNode28 left{};
    AvlNode28 root{};
    AvlNode28 right{};
    left.key = 1 * kDim + 2;
    root.key = 3 * kDim + 3;
    right.key = 6 * kDim + 6;
    root.left = &left;
    root.right = &right;
    t.Get()->field_0xE8.root = &root;
    CHECK(!t.Get()->IsGroundClear(2, 1, 1, 1));
    CHECK(!t.Get()->IsGroundClear(3, 3, 1, 1));
    CHECK(!t.Get()->IsGroundClear(6, 6, 1, 1));
    CHECK(t.Get()->IsGroundClear(4, 3, 1, 1));
    CHECK(t.Get()->IsGroundClear(0, 0, 2, 2));
    t.Get()->field_0xE8.root = nullptr;
}

UNIT_TEST(clear_area_occupant_is_not_empty) {
    TestMap t;
    t.SetOccupant(6, 2, 0x1000);
    CHECK(!t.Get()->IsAreaEmpty(6, 2, 1, 1));
    CHECK(!t.Get()->IsAreaEmpty(5, 1, 3, 3));
    CHECK(t.Get()->IsAreaEmpty(0, 0, 3, 3));
    // An occupant does not make the ground unclear: the two tests are separate.
    CHECK(t.Get()->IsGroundClear(6, 2, 1, 1));
    t.SetOccupant(6, 2, 0);
}
