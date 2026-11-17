#pragma once
// ============================================================================
// src/ext/custom_maps/custom_maps.h -- ext.custom_maps (class A)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/CUSTOMMAPS.md. A new game can start on a map file (bmap.h) instead
// of a generated map. The terrain stage of world creation takes the file's height map and water paths in
// place of the generator's, and everything after it (the blend texture, the meshes, the water mesh, the
// cell classification, the start search) runs unchanged.
//
// WATER. The water mesh draws a cell only inside the bounding box of a water feature {x, y, radius}
// (hf->waterFeatures), and only where a corner is at or below -0.75. The generator makes a few features
// by hand; a map file carries none, so CoverageFeatures lays square tiles of 200 cells over the grid and
// gives every tile that holds an underwater vertex one feature of radius 98 (the builder scans radius + 2
// on each side, so a feature's box is exactly its tile and no cell is meshed twice). Neighbouring tiles
// must meet without a seam: the builder writes the feature's index into every vertex as z and fades the
// ambient occlusion weight with the distance from the feature's centre, so the coverage features all use
// the first one's z and a full weight (IsCoverageFeature, CoverageZ, read at the two water mesh seams).
//
// CLASS A. The generator runs only for a NEW game and the save carries its own terrain, so nothing is
// stored and the row has no lock. The picked map is used by the next new game only.
//
// Standard C++ only: tests/unit/ext_custom_maps_test.cpp links this header and custom_maps.cpp.
// ============================================================================

#include "ext/custom_maps/bmap.h"
#include "ext/water_origin.h"

#include <cstdint>
#include <vector>

namespace Ext {
namespace CustomMaps {

constexpr float   kUnderwater    = -0.75f;   // a corner at or below this is water (the mesh builder's test)
constexpr int16_t kUnderwaterRaw = -192;     // the same in 1/256 units
constexpr int     kFeatureRadius = 98;       // cells
constexpr int     kRadMargin     = 2;        // the builder scans radius + 2 on each side of a feature
constexpr int     kTileSide      = 2 * (kFeatureRadius + kRadMargin);   // 200: a feature's box
constexpr int     kBaseZ         = 191;      // the builder's z for feature f is f - kBaseZ

// The height a stored value means.
inline float HeightOf(int16_t raw) {
    return static_cast<float>(raw) / 256.0f;
}

// A water feature as the mesh builder reads it: world cells, the map centre at 0, 0.
struct Feature {
    float x      = 0.0f;
    float y      = 0.0f;
    float radius = 0.0f;
};

// The tiles of the grid that hold an underwater vertex, one feature each, in row-major tile order.
std::vector<Feature> CoverageFeatures(const Map& map);

// The index of the size row whose map size is `mapSize`, or -1.
int SizeRowFor(uint32_t mapSize, const uint32_t* rowSizes, int rowCount);

// The map the next new game starts on. Process-wide, set by the New Game dialog. The terrain stage of the
// next new game moves it to the active slot (ActivatePending), where the later stages of that game's
// creation (the resources, the start) read it; the next new game's terrain stage empties the active slot
// first, so a game never inherits the last one's map. A load from a save runs neither.
void       SetPending(Map&& map);
const Map* Pending();
void       ClearPending();
void       ActivatePending();
void       ClearActive();
const Map* Active();

// Which water features of the world being built are coverage features: those from `first` on. -1 = none
// (a generated map). Set by the world seam, read by the water mesh builder.
void SetCoverageStart(int first);
bool IsCoverageFeature(int index);
int  CoverageZ();   // the z every coverage feature uses: that of the first one

// The origin byte (fourth component of a water vertex's normal) the mesh builder writes. A coverage feature's
// water is marked as this extension's, so ext.better_water's river-mouth fade leaves it alone: it runs at
// full alpha to the side of its tile, and fading it drew a dark seam where two tiles join. Other water
// stays retail's 0.
inline uint8_t CoverageWaterOrigin(bool coverage) {
    return coverage ? Ext::WaterOrigin::kCustomMaps : Ext::WaterOrigin::kRetail;
}

}  // namespace CustomMaps
}  // namespace Ext
