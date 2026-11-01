// ============================================================================
// src/ext/better_water/shore_strip.h -- ext.better_water Fancy: the shore strip's mesh (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERWATER.md section 7. No engine include: unit-tested.
//
// The strip is the terrain cells that cross the waterline, next to the game's water, laid on the
// terrain: the terrain's own int16 heights (WorldGeometry::_heights, 1/256 world units, one cell per
// world unit, world = index - grid / 2) and the terrain mesh's own triangle diagonal per cell
// (serialize_heightmap_mesh.cpp: even col + row splits bottom-left to top-right, odd top-left to
// bottom-right), so the strip's surface is the terrain's. Its vertices are in the water's format
// (water_contour_mesh.cpp's phase 2 packing: position * 48, height * 256, planar uv = position * 48,
// the edge-alpha byte), wound as the water's quads are, because the strip is drawn with the river
// water's vertex program and render state.
//
// WHICH CELLS. A cell is kept when (a) the game's water mesh is within kNearWater cells of it (terrain
// below the waterline with no water on it, a dry basin, gets no strip) and (b) its corners span the
// band from kBandBelow below the waterline to kBandAbove above it.
//
// THE VERTEX. Its edge-alpha byte holds the corner's height above the waterline, (h + 0.5) / 2 in world
// units (shore_strip.hlsl decodes it). Its drawn height is the terrain's plus kLift, and never below
// kWaterTop, so the strip lies over the water surface (retail's lakes sit up to a few 1/256 above
// -0.75) where it reaches past the line.
//
// CHUNKS. The water's indices are 16 bit; the strip is cut into chunks of at most kMaxChunkVertices
// vertices, each drawn with its own base vertex (Context::DrawIndexed's second argument).
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace Ext {
namespace BetterWater {
namespace Strip {

constexpr float kWaterline  = -0.75f;                    // GetUnderWaterAmount's 0 (functions.srsl)
constexpr float kBandBelow  = 0.25f;                     // cells reaching this far below the line...
constexpr float kBandAbove  = 0.60f;                     // ...and this far above it
constexpr float kLift       = 0.02f;                     // over the terrain
constexpr float kWaterTop   = -0.75f + 12.0f / 256.0f;   // over every water surface
constexpr int   kNearWater  = 2;                         // cells from the water mesh
constexpr int   kMaxChunkVertices = 65000;

// The water's 16-byte vertex (WorldGeometryElements.h TerrainVertex, as phase 2 fills it).
struct Vertex {
    int16_t position[4];   // x * 48, y * 48, height * 256, 0
    int16_t uv[2];         // x * 48, y * 48 (planar)
    uint8_t normal[4];     // 0x7f, 0x7f, the height byte, 0
};
static_assert(sizeof(Vertex) == 16, "the water's vertex stride");

struct Chunk {
    int32_t vertexStart;
    int32_t vertexCount;
    int32_t indexStart;
    int32_t indexCount;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;   // relative to their chunk's vertexStart
    std::vector<Chunk> chunks;
    int32_t cells = 0;
};

// The height byte for a height above the waterline (world units), and back.
uint8_t EncodeHeight(float aboveWaterline);
float DecodeHeight(uint8_t byte);

// Builds the strip from an n x n int16 height grid (world = index - halfGrid) and the game's water
// vertices (their position * 48 marks the cells the water covers). Clears `out` first.
void Build(const int16_t* heights, int32_t n, int32_t halfGrid, const Vertex* water, int32_t waterCount,
           Mesh& out);

}  // namespace Strip
}  // namespace BetterWater
}  // namespace Ext
