#pragma once
// ============================================================================
// src/game/Game/TerrainHeightfield.h — ENDGAME B.4 W1 block GAME-c
//
// The records the new-game terrain build (worldgen*.cpp, water_contour_mesh.cpp)
// works on. Retail has no names for any of them; the names are descriptive (H3),
// the layouts and widths are retail's.
//
// TerrainHeightfield is the 0xA30-byte scratch record WorldGeometry::BuildTerrain
// (0x1400b0230) keeps on its stack and hands to the core build 0x1400ac4b0 and
// the f390 height pipeline: the grid size, the float heightmap, the feature
// polylines, three lists of {x, y, radius} stamps, the seeded Random, the
// generator and its size row. Its constructor is b0230's zero stores (the tree
// MemAllocs and memsets it, then seeds the Random in place), its destructor the
// list frees at b0230's tail: no C++ constructor or destructor runs on it.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "collection/Collection.h"
#include "system/Value.h"          // System::Random
#include "Action/PathSearch.h"     // RawArray<T>

class TerrainGenerator;

// One row of TerrainGenerator's size table (+0x80, stride 0x44): 17 ints,
// serialized raw by 0x140020870 (row[9] last). The names are what the f390
// stages do with each value (worldgen_f390_*.cpp); +0x1C is read by none of
// them.
struct TerrainSizeRow {
    int32_t mapSize;             // +0x00 the map edge in tiles (gridSize = 2 * mapSize)
    int32_t riverCount2;         // +0x04 cd460 pass 2: point count
    int32_t riverMinEdge2;       // +0x08
    int32_t riverMaxEdge2;       // +0x0C
    int32_t riverCount1;         // +0x10 cd460 pass 1: point count
    int32_t riverMinEdge1;       // +0x14
    int32_t riverMaxEdge1;       // +0x18
    int32_t flatAreaConnections;          // +0x1C
    int32_t secondaryPathCount;  // +0x20 ccf60
    int32_t erosionIterations;   // +0x24 cc080
    int32_t minRangeCount;       // +0x28 cd150
    int32_t maxRangeCount;       // +0x2C
    int32_t minRangeBand;        // +0x30
    int32_t maxRangeBand;        // +0x34
    int32_t basinsPerRange;      // +0x38
    int32_t basinRadiusMax;      // +0x3C
    int32_t basinRadiusMin;      // +0x40
};
static_assert(sizeof(TerrainSizeRow) == 0x44, "TerrainSizeRow stride 0x44");
static_assert(offsetof(TerrainSizeRow, mapSize) == 0x0, "TerrainSizeRow::mapSize");
static_assert(offsetof(TerrainSizeRow, riverCount2) == 0x4, "TerrainSizeRow::riverCount2");
static_assert(offsetof(TerrainSizeRow, riverMinEdge2) == 0x8, "TerrainSizeRow::riverMinEdge2");
static_assert(offsetof(TerrainSizeRow, riverMaxEdge2) == 0xC, "TerrainSizeRow::riverMaxEdge2");
static_assert(offsetof(TerrainSizeRow, riverMinEdge1) == 0x14, "TerrainSizeRow::riverMinEdge1");
static_assert(offsetof(TerrainSizeRow, riverMaxEdge1) == 0x18, "TerrainSizeRow::riverMaxEdge1");
static_assert(offsetof(TerrainSizeRow, flatAreaConnections) == 0x1C, "TerrainSizeRow::flatAreaConnections");
static_assert(offsetof(TerrainSizeRow, maxRangeCount) == 0x2C, "TerrainSizeRow::maxRangeCount");
static_assert(offsetof(TerrainSizeRow, minRangeBand) == 0x30, "TerrainSizeRow::minRangeBand");
static_assert(offsetof(TerrainSizeRow, maxRangeBand) == 0x34, "TerrainSizeRow::maxRangeBand");
static_assert(offsetof(TerrainSizeRow, basinRadiusMax) == 0x3C, "TerrainSizeRow::basinRadiusMax");
static_assert(offsetof(TerrainSizeRow, riverCount1) == 0x10, "TerrainSizeRow +0x10");
static_assert(offsetof(TerrainSizeRow, secondaryPathCount) == 0x20, "TerrainSizeRow +0x20");
static_assert(offsetof(TerrainSizeRow, erosionIterations) == 0x24, "TerrainSizeRow +0x24");
static_assert(offsetof(TerrainSizeRow, minRangeCount) == 0x28, "TerrainSizeRow +0x28");
static_assert(offsetof(TerrainSizeRow, basinsPerRange) == 0x38, "TerrainSizeRow +0x38");
static_assert(offsetof(TerrainSizeRow, basinRadiusMin) == 0x40, "TerrainSizeRow +0x40");

// One point of a feature polyline (0x1C). e910 / ec50 stamp every point with
// two radial brushes (0x1400ce910 / 0x1400cec50); the path builder 0x1400cc850
// writes the same five values into every point of a path.
struct TerrainPathPoint {
    float x;                     // +0x00
    float y;                     // +0x04
    float strength;              // +0x08 outer brush strength
    float innerRadius;           // +0x0C
    float innerFalloff;          // +0x10
    float outerRadius;           // +0x14 (the channel half-width the water mesh reads)
    float outerFalloff;          // +0x18
};
static_assert(sizeof(TerrainPathPoint) == 0x1C, "TerrainPathPoint stride 0x1C");
static_assert(offsetof(TerrainPathPoint, x) == 0x0, "TerrainPathPoint::x");
static_assert(offsetof(TerrainPathPoint, y) == 0x4, "TerrainPathPoint::y");
static_assert(offsetof(TerrainPathPoint, innerFalloff) == 0x10, "TerrainPathPoint::innerFalloff");
static_assert(offsetof(TerrainPathPoint, strength) == 0x08, "TerrainPathPoint +0x08");
static_assert(offsetof(TerrainPathPoint, innerRadius) == 0x0C, "TerrainPathPoint +0x0C");
static_assert(offsetof(TerrainPathPoint, outerRadius) == 0x14, "TerrainPathPoint +0x14");
static_assert(offsetof(TerrainPathPoint, outerFalloff) == 0x18, "TerrainPathPoint +0x18");

// A radial stamp {x, y, radius} (0xC; grown by 0x1400245d0).
struct TerrainStamp {
    float x;                     // +0x00
    float y;                     // +0x04
    float radius;                // +0x08
};
static_assert(sizeof(TerrainStamp) == 0x0C && alignof(TerrainStamp) == 4, "TerrainStamp stride 0xC");
static_assert(offsetof(TerrainStamp, x) == 0x0, "TerrainStamp::x");
static_assert(offsetof(TerrainStamp, y) == 0x4, "TerrainStamp::y");
static_assert(offsetof(TerrainStamp, radius) == 0x8, "TerrainStamp::radius");

// The polyline list's row (worldgen_f390_coastline.cpp): append 0x140024460 deep-
// copies each path (0x140024ac0), the destructor 0x1400175d0 frees every path.
extern const rtcoll::ArraySpec kSpec_WgPathVec;
using TerrainPathList = Collection::Array<RawArray<TerrainPathPoint>, Collection::DefaultAllocator,
                                          Collection::ExplicitSpec<&kSpec_WgPathVec>>;
static_assert(sizeof(TerrainPathList) == 0x10, "Array header 0x10");

struct TerrainHeightfield {
    int32_t gridSize;                      // +0x00 2 * sizeRow->mapSize (f390)
    uint8_t _pad04[4];                     // +0x04
    RawArray<float> heightmap;             // +0x08 (gridSize + 1)^2 heights
    TerrainPathList paths;                 // +0x18 the feature polylines (coastline ring, paths)
    Collection::Array<TerrainStamp> riverPoints;     // +0x28 cd460's river points (e910: maskA)
    Collection::Array<TerrainStamp> basinStamps;     // +0x38 cd150's carve stamps (e910 / ec50: maskC)
    Collection::Array<TerrainStamp> waterFeatures;   // +0x48 cd150's tarn features (b260 phase 2)
    System::Random random;                 // +0x58 seeded by b0230
    uint8_t _pad[0xA20 - 0x58 - sizeof(System::Random)];
    TerrainGenerator* generator;           // +0xA20
    TerrainSizeRow*   sizeRow;             // +0xA28 generator->sizeRows + sizeIndex
};
static_assert(offsetof(TerrainHeightfield, heightmap) == 0x08, "TerrainHeightfield +0x08");
static_assert(offsetof(TerrainHeightfield, paths) == 0x18, "TerrainHeightfield +0x18");
static_assert(offsetof(TerrainHeightfield, riverPoints) == 0x28, "TerrainHeightfield +0x28");
static_assert(offsetof(TerrainHeightfield, basinStamps) == 0x38, "TerrainHeightfield +0x38");
static_assert(offsetof(TerrainHeightfield, waterFeatures) == 0x48, "TerrainHeightfield +0x48");
static_assert(offsetof(TerrainHeightfield, random) == 0x58, "TerrainHeightfield +0x58");
static_assert(offsetof(TerrainHeightfield, generator) == 0xA20, "TerrainHeightfield +0xA20");
static_assert(offsetof(TerrainHeightfield, sizeRow) == 0xA28, "TerrainHeightfield +0xA28");
static_assert(sizeof(TerrainHeightfield) == 0xA30, "b0230's stack record is 0xA30");
static_assert(offsetof(TerrainHeightfield, gridSize) == 0x0, "TerrainHeightfield::gridSize");
static_assert(offsetof(TerrainHeightfield, _pad04) == 0x4, "TerrainHeightfield::_pad04");

// The f390 height-pipeline stages (worldgen_f390_*.cpp), each on the heightfield.
void wg_f390_coastline(TerrainHeightfield* hf);   // 0x1400cd090 coastline ring -> paths
void wg_f390_ranges(TerrainHeightfield* hf);      // 0x1400cd150 mountain ranges -> stamps
void wg_f390_paths(TerrainHeightfield* hf);       // 0x1400ccf60 secondary paths -> paths
void wg_f390_rivers(TerrainHeightfield* hf);      // 0x1400cd460 river network -> riverPoints
void wg_f390_stamp(TerrainHeightfield* hf);       // 0x1400ce910 feature stamp (carves height)
void wg_f390_erosion(TerrainHeightfield* hf);     // 0x1400cc080 droplet erosion
void wg_f390_carve(TerrainHeightfield* hf);       // 0x1400cec50 feature stamp finalize
void wg_f390_smooth(TerrainHeightfield* hf);      // "c350" 3-tap box blur (called twice)
