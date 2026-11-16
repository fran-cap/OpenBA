#pragma once
// ============================================================================
// src/ext/water_origin.h -- which extension laid down a range of water (NOT RETAIL)
// ============================================================================
// The fourth byte of a water vertex's normal names where its range came from. Retail writes 0
// (water_contour_mesh.cpp) and each extension that adds water of its own writes its value below, so that
// an extension can tell its own ranges from the game's and from another extension's by origin, never by
// reading the vertex data. The values live in one header so that two extensions cannot take the same one.
// ============================================================================

#include <cstdint>

namespace Ext {
namespace WaterOrigin {

constexpr uint8_t kRetail = 0;        // the game's own water
constexpr uint8_t kTerrainEdit = 1;   // ext.terrain_edit's patches (terrain_water.h)
constexpr uint8_t kCustomMaps = 2;    // ext.custom_maps: the coverage ranges of a map file's water

}  // namespace WaterOrigin
}  // namespace Ext
