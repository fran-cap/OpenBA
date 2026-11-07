// ============================================================================
// src/ext/terrain_edit/brush_tool.cpp -- ext.terrain_edit: the brush tool (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TERRAINEDIT.md. A Tool of its own (the game's tools are Tool subclasses the
// player manager holds at +0x28 and drives every frame). The Terrain page's buttons install it with
// StartBrush; right click ends it and the manager reinstalls the EditTool, as for every tool.
//
// While the left mouse button is held over the map (the manager's `active` flag is false over a dialog),
// each frame paints the current mode (raise, lower, flatten, mountain: terrain_edit_math.h) on the int16
// height words (z * 256) of the vertices of a disc, weighted to a bump. Then the dirty chunks rebuild by
// the game's own path (WorldGeometry::MarkChunksDirty, as the quarry does) and the cells of the disc are
// classified again from their new height (Map::ClassifyTerrain / SetTerrainType), so a cell raised into a
// mountain stops being walkable and a cell flattened into the buildable band can be built on. Cells with a
// building or another blocker are left as they are.
//
// Precedent: QuarryComponent::ApplyDepth (Component/quarrycomponent_deform.cpp) for the writes, RemovalTool
// (Player/removaltool.cpp) for the Tool shape and the tile overlay.
// ============================================================================
#include "Player/Tool.h"
#include "Player/exe_bodies.h"          // PlayerRay_ScreenToCell_101270, PlayerTileRender_DrawOverlay_08c0
#include "Game/GameUpdateLoop.h"
#include "Entity/ComponentEntity.h"      // InternalPtr<ComponentEntity>::Get() needs the class complete
#include "Entity/EntityList.h"           // removalList
#include "Entity/CitizenBehavior.h"      // the town's water access (WaterCellPair, AddCellEntry)
#include "Component/ComponentDescription.h"
#include "Component/ModelComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/GrowthComponent.h"
#include "Component/GrowthDescription.h"
#include "Component/ModelDescription.h"
#include "Component/NaturalResourceDescription.h"
#include "system/Filename.h"
#include "object/External.h"
#include "Game/exe_bodies.h"
#include "ext/registry.h"
#include "ext/rock_seat/rock_seat.h"
#include "system/Value.h"                // RtSystem_Time_Ctor
#include "Game/PlayerToolManager.h"
#include "Game/Map.h"
#include "Game/inputbindings.h"     // GameKey (the KeyMap ids)
#include "Game/WorldGeometry.h"
#include "Game/WorldGeometryElements.h"   // GeometryDrawRange, TerrainVertex (the water mesh log)
#include "Component/still_terrain.h"    // StillTerrain::Bump (the terrain epoch)
#include "collection/Collection.h"
#include "input/Input.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Object/factory_registration.h"

#include "ext/terrain_edit/terrain_edit.h"
#include "ext/terrain_edit/terrain_edit_math.h"
#include "ext/terrain_edit/terrain_undo.h"
#include "ext/terrain_edit/terrain_water.h"
#include "ext/terrain_edit/terrain_texture.h"
#include "video/ImageBuffer.h"
#include "video/Device.h"
#include "Action/PathSearch.h"            // RawArray
#include "system/MemoryAllocator.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <new>
#include <cstring>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

Ext::TerrainEdit::BrushState g_state;

// The undo steps, and the world they were painted on: the map, its height words and their stride. A stroke
// recorded on one world is never applied to another.
Ext::TerrainEdit::UndoStack g_undo;
// The vertices an edit wrote in the current stroke, as a box of height-grid vertices: where the water mesh is
// looked at again.
struct StrokeBox {
    bool valid = false;
    int  minX = 0;
    int  minY = 0;
    int  maxX = 0;
    int  maxY = 0;
    void Add(int x, int y) {
        if (!valid) {
            valid = true;
            minX = maxX = x;
            minY = maxY = y;
            return;
        }
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
};
StrokeBox g_box;

// The quads the world's water mesh draws (its lake ranges), by WaterCellKey: where the quad's six indices
// are, and whether it is one of the edit's (its vertices carry kEditMarker in the normal) or the game's
// own. An edit never draws a cell twice (a cell drawn twice shows as a pale patch). Built from the mesh the
// first time it is asked for in a world, kept up to date as strokes change the mesh, and dropped when a
// stroke is undone.
constexpr int kRiverOverlapRaise = 2;   // water heights are in 1/48 units: 0.04

struct WaterQuad {
    int  position;   // index into _contourIndices of the quad's first index
    bool edit;
    int  z;          // the height of its water (the vertices' z)
    bool river;      // in a river-style range (only the edit's own quads are read from those)
};
std::unordered_map<uint64_t, WaterQuad> g_waterQuads;
bool g_waterQuadsValid = false;

void (*g_undoHook)() = nullptr;   // the Terrain page's status refresh
void (*g_frameHook)() = nullptr;  // the Terrain page's slider read
const void* g_undoMap = nullptr;
const void* g_undoHeights = nullptr;
int g_undoStride = 0;
uint32_t g_undoEpoch = 0;
bool g_undoRefused = false;   // the last Undo was refused (UndoRefused)
bool g_redoBuilt = false;     // the next Redo's ground has something built on it
const char* g_redoNote = "";  // why else the last Redo did nothing (RedoNote); a string literal, or empty
bool g_toolRunning = false;    // the brush is the player's tool now (BrushRunning)
bool g_replaying = false;      // a Redo is painting a stroke again (it keeps the rest of the redo list)
uint32_t g_paintSeed = 1u;     // the stroke's noise seed (mountain)
float g_flattenTarget = 0.0f;  // world height flatten aims for, taken under the cursor when the stroke starts

// The world the undo steps and the water-quad cache belong to is the map, its height words and their
// stride, and the lock epoch (a new game, a load or a reload takes a new one, so a world allocated at the
// addresses of the last one is still another world).
bool SameWorld(const void* map, const void* heights, int stride) {
    return map == g_undoMap && heights == g_undoHeights && stride == g_undoStride &&
           g_undoEpoch == Ext::LockEpoch();
}

// Forget everything that was recorded against the last world: the undo steps, the stroke's box and the
// cache of the water quads (its positions index that world's mesh).
void ForgetWorld() {
    g_undo.Clear();
    g_box = StrokeBox();
    g_waterQuads.clear();
    g_waterQuadsValid = false;
    g_undoRefused = false;   // a refusal belongs to the stroke it refused, which is gone
    g_redoBuilt = false;
    g_redoNote = "";
}

void AdoptWorld(const void* map, const void* heights, int stride) {
    ForgetWorld();
    g_undoMap = map;
    g_undoHeights = heights;
    g_undoStride = stride;
    g_undoEpoch = Ext::LockEpoch();
}

// A road is a cell the game marked in field_0x2A (the road batch id, 0xFF when there is none): the road mesh
// is built once from the ground's heights at that time, so ground raised or lowered under a road would bury
// it or leave it in the air, and the cell's type must stay what the road was built on.
bool IsRoadCell(const MapCell& cell) {
    return cell.field_0x2A != MapCell::kNoRoadBatch;
}

// A vertex is a corner of four cells, and the terrain classifier reads the cell centre from all four
// corners: a vertex that touches a building, another blocker or a road is left alone, or the building
// floats or sinks at that edge and the road is buried.
bool VertexTouchesBlocker(Map* map, int vx, int vy, int offset) {
    const int size = map->size;
    const MapCell* cells = map->cells.GetData();
    for (int dy = -1; dy <= 0; ++dy) {
        for (int dx = -1; dx <= 0; ++dx) {
            const int col = vx - offset + dx;
            const int row = vy - offset + dy;
            if (col < 0 || row < 0 || col >= size || row >= size) continue;
            const MapCell& corner = cells[static_cast<int64_t>(row) * size + col];
            if (corner.blocker.Get() != nullptr || IsRoadCell(corner)) return true;
        }
    }
    return false;
}

// An Undo gives back the heights and the cell types a stroke recorded. It is refused as a whole when any of
// them has something built on it since: a vertex that touches a blocker or a road, or a cell that is blocked,
// is a road or no longer holds the type the stroke gave it. A part of it would leave the water mesh and the
// ground texture, which an Undo cuts back and restores whole, out of step with the heights that stayed.
bool UndoWouldSkip(Map* map, const Ext::TerrainEdit::Stroke& stroke, int stride, int offset,
                   bool typesMustMatch = true) {
    const int size = map->size;
    for (const auto& entry : stroke.heights) {
        const int vx = entry.first % stride;
        const int vy = entry.first / stride;
        if (vy >= stride) continue;
        if (VertexTouchesBlocker(map, vx, vy, offset)) return true;
    }
    for (const auto& entry : stroke.types) {
        if (entry.first < 0 || entry.first >= size * size) continue;
        const MapCell& cell = map->cells.GetData()[entry.first];
        const auto wrote = stroke.written.find(entry.first);
        if (cell.blocker.Get() != nullptr || IsRoadCell(cell)) return true;
        // A stroke further down the list finds its cells retyped by the strokes above it, which are undone
        // first and put the types back, so only the top stroke is held to the type it wrote.
        if (typesMustMatch &&
            (wrote == stroke.written.end() || static_cast<uint8_t>(cell.terrainType) != wrote->second)) {
            return true;
        }
    }
    return false;
}

// Water ranges are drawn by a key of the range's number with 0x8000 (lake) or 0x6000 (river) added, decoded
// with & 0xE000 (geometry_draw.cpp): a list of 0x2000 ranges would make a lake key read as another kind.
constexpr int kMaxWaterRanges = 0x2000;

// The cell types the terrain classifier assigns (Map::ClassifyTerrain): the brush may retype a cell only
// while it holds one of these, so a road, a field or any other cell the game gave a meaning to keeps it.
bool IsNaturalType(PathType t) {
    return t == PathType::Normal || t == PathType::Unusable || t == PathType::Water ||
           t == PathType::DeepWater || t == PathType::Walkable;
}

// The tint of the brush's disc, ARGB, per mode.
uint32_t DiscColor(Ext::TerrainEdit::Mode mode) {
    switch (mode) {
        case Ext::TerrainEdit::Mode::Raise:    return 0xA0FFD000u;   // amber
        case Ext::TerrainEdit::Mode::Lower:    return 0xA040A0FFu;   // blue
        case Ext::TerrainEdit::Mode::Flatten:  return 0xA060E060u;   // green
        case Ext::TerrainEdit::Mode::Mountain: return 0xA0C07040u;   // rock brown
        case Ext::TerrainEdit::Mode::Smooth:   return 0xA0C070E0u;   // lilac
        case Ext::TerrainEdit::Mode::Plant:    return 0xA0409040u;   // green
    }
    return 0xA0FFD000u;
}

// The average height (world units) of the up to eight vertices around (vx, vy) of a `stride` x `stride` grid
// of height words.
float NeighbourAverage(const int16_t* heights, int stride, int vx, int vy) {
    double sum = 0.0;
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const int x = vx + dx;
            const int y = vy + dy;
            if (x < 0 || y < 0 || x >= stride || y >= stride) continue;
            sum += static_cast<double>(heights[static_cast<int64_t>(stride) * y + x]);
            ++count;
        }
    }
    if (count == 0) return 0.0f;
    return static_cast<float>(sum / static_cast<double>(count) / 256.0);
}

// DIAGNOSTIC (the water scoping, docs/extensions/handins/TERRAINEDIT.md section 4): what the world's water
// mesh holds, as a stderr line when the brush starts. The mesh's vertices are indexed with 16 bits, so the
// first number to read is how far the vertex count is from 65535; then the draw ranges (the game draws each
// range as one item).
void LogWaterMesh(const WorldGeometry* terrain) {
    const int verts = terrain->_contourVertices.size;
    const int indices = terrain->_contourIndices.size;
    std::fprintf(stderr,
                 "[TerrainEdit] water mesh: %d vertices (%d left under the 16-bit index limit of 65535), "
                 "%d indices = %d quads, %d body range(s), %d feature range(s)\n",
                 verts, 65535 - verts, indices, indices / 6, terrain->_contourRangesA.size,
                 terrain->_contourRangesB.size);
    for (int pass = 0; pass < 2; ++pass) {
        const int count = pass == 0 ? terrain->_contourRangesA.size : terrain->_contourRangesB.size;
        const GeometryDrawRange* ranges =
            pass == 0 ? terrain->_contourRangesA.data : terrain->_contourRangesB.data;
        for (int i = 0; i < count && i < 24 && ranges != nullptr; ++i) {
            std::fprintf(stderr, "[TerrainEdit]   %s range %d: values %d %d %d %d\n",
                pass == 0 ? "body" : "feature", i,
                         ranges[i].values[0], ranges[i].values[1], ranges[i].values[2], ranges[i].values[3]);
        }
    }
    // The height of the vertices' z field (it carries the feature number) over the whole mesh.
    int zMin = 32767;
    int zMax = -32768;
    const TerrainVertex* v = terrain->_contourVertices.data;
    for (int i = 0; i < verts && v != nullptr; ++i) {
        if (v[i].position[2] < zMin) zMin = v[i].position[2];
        if (v[i].position[2] > zMax) zMax = v[i].position[2];
    }
    std::fprintf(stderr, "[TerrainEdit]   vertex z field runs %d to %d\n", zMin, zMax);
}

// ---- the water an edit makes (docs/extensions/handins/TERRAINEDIT.md, terrain_water.h)
// --------------------------------

// The mesh's lake ranges hold one quad per cell as six indices (c0 c1 c2 c0 c2 c3); a quad's lower corner
// is the least x and y of its four vertices, and a position is the coordinate times 48. A quad whose six
// indices are one vertex is a quad this took out, and is not counted.
void ScanWaterQuads(const WorldGeometry* terrain) {
    g_waterQuads.clear();
    g_waterQuadsValid = true;
    const TerrainVertex* verts = terrain->_contourVertices.data;
    const uint16_t* idx = terrain->_contourIndices.data;
    const GeometryDrawRange* ranges = terrain->_contourRangesB.data;
    if (verts == nullptr || idx == nullptr || ranges == nullptr) return;
    for (int r = 0; r < terrain->_contourRangesB.size; ++r) {
        const int start = ranges[r].values[0];
        const int base = ranges[r].values[1];
        const int count = ranges[r].values[3];
        if (!Ext::TerrainEdit::ContourRangeFits(start, base, count, terrain->_contourIndices.size)) continue;
        for (int k = 0; k + 5 < count; k += 6) {
            // taken out
            if (idx[start + k] == idx[start + k + 1] && idx[start + k] == idx[start + k + 2]) continue;
            int minX = 32767;
            int minY = 32767;
            const int corners[4] = { k, k + 1, k + 2, k + 5 };
            bool ok = true;
            for (const int c : corners) {
                const int v = base + idx[start + c];
                if (v < 0 || v >= terrain->_contourVertices.size) {
                    ok = false;
                    break;
                }
                if (verts[v].position[0] < minX) minX = verts[v].position[0];
                if (verts[v].position[1] < minY) minY = verts[v].position[1];
            }
            if (!ok) continue;
            const int first = base + idx[start + k];
            WaterQuad q;
            q.position = start + k;
            q.edit = verts[first].normal[3] == Ext::TerrainEdit::kEditMarker;
            q.z = verts[first].position[2];
            q.river = false;
            g_waterQuads[Ext::TerrainEdit::WaterCellKey(minX / 48, minY / 48)] = q;
        }
    }
    // The river ranges are strips, not quads, except the ones the edit made (their vertices carry the edit
    // marker): those are read the same way.
    const GeometryDrawRange* rivers = terrain->_contourRangesA.data;
    if (rivers == nullptr) return;
    for (int r = 0; r < terrain->_contourRangesA.size; ++r) {
        const int start = rivers[r].values[0];
        const int base = rivers[r].values[1];
        const int count = rivers[r].values[3];
        if (!Ext::TerrainEdit::ContourRangeFits(start, base, count, terrain->_contourIndices.size)) continue;
        if (count < 6) continue;
        if (base + idx[start] >= terrain->_contourVertices.size) continue;
        if (verts[base + idx[start]].normal[3] != Ext::TerrainEdit::kEditMarker) continue;
        for (int k = 0; k + 5 < count; k += 6) {
            // taken out
            if (idx[start + k] == idx[start + k + 1] && idx[start + k] == idx[start + k + 2]) continue;
            int minX = 32767;
            int minY = 32767;
            const int corners[4] = { k, k + 1, k + 2, k + 5 };
            bool ok = true;
            for (const int c : corners) {
                const int v = base + idx[start + c];
                if (v < 0 || v >= terrain->_contourVertices.size) {
                    ok = false;
                    break;
                }
                if (verts[v].position[0] < minX) minX = verts[v].position[0];
                if (verts[v].position[1] < minY) minY = verts[v].position[1];
            }
            if (!ok) continue;
            WaterQuad q;
            q.position = start + k;
            q.edit = true;
            q.z = verts[base + idx[start + k]].position[2];
            q.river = true;
            g_waterQuads[Ext::TerrainEdit::WaterCellKey(minX / 48, minY / 48)] = q;
        }
    }
}

// The game's own river triangles (not the edit's) that touch the box of world coordinates [x0, x1] x [y0,
// y1]: where a river is already drawn, so that a patch leaves those cells to it (two layers of water at one
// height z-fight).
struct RiverCover {
    std::vector<float> tri;   // x0 y0 x1 y1 x2 y2 per triangle
    bool Contains(float px, float py) const {
        for (size_t i = 0; i + 5 < tri.size(); i += 6) {
            const float x[3] = { tri[i], tri[i + 2], tri[i + 4] };
            const float y[3] = { tri[i + 1], tri[i + 3], tri[i + 5] };
            if (Ext::TerrainEdit::PointInTriangle(px, py, x, y)) return true;
        }
        return false;
    }
    // The cell is covered when its four corners and its centre are all inside the river's triangles. A cell
    // the river's edge cuts through (its centre inside, a corner outside) is not: skipping it left a
    // triangle of the cell with no water.
    bool Covers(int col, int row) const {
        const float x = static_cast<float>(col);
        const float y = static_cast<float>(row);
        return Contains(x + 0.5f, y + 0.5f) && Contains(x, y) && Contains(x + 1.0f, y) &&
               Contains(x + 1.0f, y + 1.0f) && Contains(x, y + 1.0f);
    }
};

RiverCover GatherRiverCover(const WorldGeometry* terrain, float x0, float y0, float x1, float y1) {
    RiverCover cover;
    const TerrainVertex* verts = terrain->_contourVertices.data;
    const uint16_t* idx = terrain->_contourIndices.data;
    const GeometryDrawRange* rivers = terrain->_contourRangesA.data;
    if (verts == nullptr || idx == nullptr || rivers == nullptr) return cover;
    for (int r = 0; r < terrain->_contourRangesA.size; ++r) {
        const int start = rivers[r].values[0];
        const int base = rivers[r].values[1];
        const int count = rivers[r].values[3];
        if (!Ext::TerrainEdit::ContourRangeFits(start, base, count, terrain->_contourIndices.size)) continue;
        for (int k = 0; k + 2 < count; k += 3) {
            int v[3];
            for (int j = 0; j < 3; ++j) v[j] = base + idx[start + k + j];
            if (v[0] == v[1] || v[0] == v[2] || v[1] == v[2]) continue;   // taken out
            if (v[0] >= terrain->_contourVertices.size || v[1] >= terrain->_contourVertices.size ||
                v[2] >= terrain->_contourVertices.size) continue;
            // the edit's own: rebuilt, not kept
            if (verts[v[0]].normal[3] == Ext::TerrainEdit::kEditMarker) continue;
            float x[3];
            float y[3];
            float minX = 1e9f;
            float maxX = -1e9f;
            float minY = 1e9f;
            float maxY = -1e9f;
            for (int j = 0; j < 3; ++j) {
                x[j] = static_cast<float>(verts[v[j]].position[0]) / 48.0f;
                y[j] = static_cast<float>(verts[v[j]].position[1]) / 48.0f;
                if (x[j] < minX) minX = x[j];
                if (x[j] > maxX) maxX = x[j];
                if (y[j] < minY) minY = y[j];
                if (y[j] > maxY) maxY = y[j];
            }
            if (maxX < x0 || minX > x1 || maxY < y0 || minY > y1) continue;
            for (int j = 0; j < 3; ++j) {
                cover.tri.push_back(x[j]);
                cover.tri.push_back(y[j]);
            }
        }
    }
    return cover;
}

// The mapping of every river triangle (the game's strips, and the edit's own river-style quads except those
// inside the box [x0, x1] x [y0, y1], which are being made again) with its centre, for the triangles whose
// centre is inside the box [bx0, bx1] x [by0, by1]. A river-style patch takes each vertex's uv from the
// nearest of these (WaterStyle::anchors).
std::vector<Ext::TerrainEdit::UvAnchor> GatherUvAnchors(const WorldGeometry* terrain, float bx0,
    float by0, float bx1, float by1,
                                                       float x0, float y0, float x1, float y1) {
    std::vector<Ext::TerrainEdit::UvAnchor> out;
    const TerrainVertex* verts = terrain->_contourVertices.data;
    const uint16_t* idx = terrain->_contourIndices.data;
    const GeometryDrawRange* rivers = terrain->_contourRangesA.data;
    if (verts == nullptr || idx == nullptr || rivers == nullptr) return out;
    for (int r = 0; r < terrain->_contourRangesA.size; ++r) {
        const int start = rivers[r].values[0];
        const int base = rivers[r].values[1];
        const int count = rivers[r].values[3];
        if (!Ext::TerrainEdit::ContourRangeFits(start, base, count, terrain->_contourIndices.size)) continue;
        for (int k = 0; k + 2 < count; k += 3) {
            int v[3];
            for (int j = 0; j < 3; ++j) v[j] = base + idx[start + k + j];
            if (v[0] == v[1] || v[0] == v[2] || v[1] == v[2]) continue;   // taken out
            if (v[0] >= terrain->_contourVertices.size || v[1] >= terrain->_contourVertices.size ||
                v[2] >= terrain->_contourVertices.size) continue;
            float x[3];
            float y[3];
            float tu[3];
            float tv[3];
            for (int j = 0; j < 3; ++j) {
                x[j] = static_cast<float>(verts[v[j]].position[0]) / 48.0f;
                y[j] = static_cast<float>(verts[v[j]].position[1]) / 48.0f;
                tu[j] = static_cast<float>(verts[v[j]].uv[0]) / 1024.0f;
                tv[j] = static_cast<float>(verts[v[j]].uv[1]) / 1024.0f;
            }
            const float cx = (x[0] + x[1] + x[2]) / 3.0f;
            const float cy = (y[0] + y[1] + y[2]) / 3.0f;
            if (cx < bx0 || cx > bx1 || cy < by0 || cy > by1) continue;
            const bool edit = verts[v[0]].normal[3] == Ext::TerrainEdit::kEditMarker;
            if (edit && cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1) continue;   // about to be made again
            Ext::TerrainEdit::UvAnchor a;
            if (!Ext::TerrainEdit::UvMapOfTriangle(x, y, tu, tv, a.map)) continue;
            a.x = cx;
            a.y = cy;
            out.push_back(a);
        }
    }
    return out;
}

// The nearest river triangle to the cell (col, row), among those within `reach` cells: any triangle of a
// river range (the game's strips, and the edit's own river-style quads), and its flow. False when none is
// near.
bool NearestRiver(const WorldGeometry* terrain, int col, int row, float reach,
    Ext::TerrainEdit::WaterStyle& out,
                  bool& isEdit) {
    const TerrainVertex* verts = terrain->_contourVertices.data;
    const uint16_t* idx = terrain->_contourIndices.data;
    const GeometryDrawRange* rivers = terrain->_contourRangesA.data;
    if (verts == nullptr || idx == nullptr || rivers == nullptr) return false;
    float best = reach * reach;
    bool found = false;
    for (int r = 0; r < terrain->_contourRangesA.size; ++r) {
        const int start = rivers[r].values[0];
        const int base = rivers[r].values[1];
        const int count = rivers[r].values[3];
        if (!Ext::TerrainEdit::ContourRangeFits(start, base, count, terrain->_contourIndices.size)) continue;
        for (int k = 0; k + 2 < count; k += 3) {
            int v[3];
            for (int j = 0; j < 3; ++j) v[j] = base + idx[start + k + j];
            if (v[0] == v[1] || v[0] == v[2] || v[1] == v[2]) continue;   // taken out
            if (v[0] >= terrain->_contourVertices.size || v[1] >= terrain->_contourVertices.size ||
                v[2] >= terrain->_contourVertices.size) continue;
            float x[3];
            float y[3];
            float tu[3];
            float tv[3];
            for (int j = 0; j < 3; ++j) {
                x[j] = static_cast<float>(verts[v[j]].position[0]) / 48.0f;
                y[j] = static_cast<float>(verts[v[j]].position[1]) / 48.0f;
                tu[j] = static_cast<float>(verts[v[j]].uv[0]) / 1024.0f;
                tv[j] = static_cast<float>(verts[v[j]].uv[1]) / 1024.0f;
            }
            const float cx = (x[0] + x[1] + x[2]) / 3.0f - (static_cast<float>(col) + 0.5f);
            const float cy = (y[0] + y[1] + y[2]) / 3.0f - (static_cast<float>(row) + 0.5f);
            const float d2 = cx * cx + cy * cy;
            if (d2 >= best) continue;
            Ext::TerrainEdit::WaterStyle s;
            if (!Ext::TerrainEdit::UvMapOfTriangle(x, y, tu, tv, s.map)) continue;
            s.river = true;
            s.z = verts[v[0]].position[2];
            isEdit = verts[v[0]].normal[3] == Ext::TerrainEdit::kEditMarker;
            best = d2;
            out = s;
            found = true;
        }
    }
    return found;
}

// Appends `count` items to a mesh array, which the world's own code frees with MemFree.
template <class T>
bool AppendToArray(RawArray<T>& array, const T* items, int count, int align) {
    if (count <= 0) return true;
    const int need = array.size + count;
    if (array.capacity < need) {
        const int newCapacity = need + need / 2;
        const uint64_t bytes = static_cast<uint64_t>(newCapacity) * sizeof(T);
        T* grown = static_cast<T*>(System::MemoryAllocator::MemAlloc(bytes, align));
        if (grown == nullptr) return false;
        if (array.data != nullptr && array.size > 0) std::memcpy(grown, array.data,
            static_cast<size_t>(array.size) * sizeof(T));
        if (array.data != nullptr) System::MemoryAllocator::MemFree(array.data);
        array.data = grown;
        array.capacity = newCapacity;
    }
    std::memcpy(array.data + array.size, items, static_cast<size_t>(count) * sizeof(T));
    array.size = need;
    return true;
}

// The mesh's two GPU buffers, made again from its arrays (BuildWaterMesh's tail): the old buffer is let go
// first, the way the buffer's own destructor and constructor do, since Create alone would leave it behind.
void UploadWaterMesh(WorldGeometry* terrain) {
    terrain->_waterIndexBuffer.PlatformDestruct();
    terrain->_waterIndexBuffer.PlatformConstruct();
    terrain->_waterIndexBuffer.Create(terrain->_contourIndices.size * 2, false,
        terrain->_contourIndices.data);
    terrain->_waterVertexBuffer.PlatformDestruct();
    terrain->_waterVertexBuffer.PlatformConstruct();
    terrain->_waterVertexBuffer.Create(terrain->_contourVertices.size << 4, false,
        terrain->_contourVertices.data);
}

// After a stroke: the water of the region it changed is made again from the ground's heights now. The
// edit's own quads in the region are taken out (their shore weights were made from the heights of an
// earlier stroke), as are the game's own where the ground no longer reaches the water (all four corners
// above -0.75), and a new lake range, which every quad carries the one height (z) of the water it touches,
// holds a quad for each cell of the region that is under water and not drawn by a game quad. The mesh is
// saved with the world, so the water stays.
void AddWaterForStroke(WorldGeometry* terrain) {
    using namespace Ext::TerrainEdit;
    const StrokeBox box = g_box;
    g_box = StrokeBox();
    const int16_t* heights = terrain->_heights.data;
    const int stride = terrain->_fieldD0;
    if (!box.valid || heights == nullptr || stride < 2 || terrain->_contourVertices.data == nullptr) return;
    if (!g_waterQuadsValid) ScanWaterQuads(terrain);
    // The cells around the written vertices: a vertex is a corner of four cells.
    int vx0 = box.minX - 1;
    int vy0 = box.minY - 1;
    int vx1 = box.maxX + 1;
    int vy1 = box.maxY + 1;
    if (vx0 < 0) vx0 = 0;
    if (vy0 < 0) vy0 = 0;
    if (vx1 > stride - 1) vx1 = stride - 1;
    if (vy1 > stride - 1) vy1 = stride - 1;
    const int halfGrid = terrain->_gridSize >> 1;
    auto height = [&](int vx,
        int vy) { return static_cast<float>(heights[static_cast<int64_t>(stride) * vy + vx]) / 256.0f; };

    const int vertsBefore = terrain->_contourVertices.size;
    const int indicesBefore = terrain->_contourIndices.size;
    const int rangesBefore = terrain->_contourRangesB.size;
    const int riverRangesBefore = terrain->_contourRangesA.size;
    uint16_t* indices = terrain->_contourIndices.data;

    // 0. The height of the water the patch joins: that of the edit's own quads in or beside the region if
    // there are any, else of
    // the game's nearest, else the first lake's. A step between two heights shows as a thin line where they
    // meet.
    int zPick = kDefaultWaterZ;
    {
        int bestEdit = 0;
        int bestOther = 0;
        bool haveEdit = false;
        bool haveOther = false;
        for (int vy = vy0 - 2; vy < vy1 + 2; ++vy) {
            for (int vx = vx0 - 2; vx < vx1 + 2; ++vx) {
                auto found = g_waterQuads.find(WaterCellKey(vx - halfGrid, vy - halfGrid));
                if (found == g_waterQuads.end()) continue;
                if (found->second.edit && !haveEdit) {
                    haveEdit = true;
                    bestEdit = found->second.z;
                } else if (!found->second.edit && !haveOther) {
                    haveOther = true;
                    bestOther = found->second.z;
                }
            }
        }
        zPick = haveEdit ? bestEdit : (haveOther ? bestOther : kDefaultWaterZ);
    }
    // A patch that touches a river is made river-style, its water flowing the way the river's does there;
    // any other patch is a lake, at the height picked above.
    WaterStyle style;
    style.z = zPick;
    {
        const int midCol = ((vx0 + vx1) >> 1) - halfGrid;
        const int midRow = ((vy0 + vy1) >> 1) - halfGrid;
        const float reach = static_cast<float>((vx1 - vx0 > vy1 - vy0 ? vx1 - vx0 : vy1 - vy0) / 2 + 3);
        WaterStyle river;
        bool fromEdit = false;
        if (NearestRiver(terrain, midCol, midRow, reach, river, fromEdit)) {
            style = river;
            // Over the game's own strip the patch lies a hair higher, so that where the two overlap (cells
            // the strip's edge cuts through) the patch is always in front; at one height the two z-fight
            // and flicker. An earlier patch of the edit already has the raise: its height is taken as it
            // is.
            if (!fromEdit) style.z += kRiverOverlapRaise;
            const float wx0 = static_cast<float>(vx0 - halfGrid);
            const float wy0 = static_cast<float>(vy0 - halfGrid);
            const float wx1 = static_cast<float>(vx1 - halfGrid);
            const float wy1 = static_cast<float>(vy1 - halfGrid);
            style.anchors = GatherUvAnchors(terrain, wx0 - 12.0f, wy0 - 12.0f, wx1 + 12.0f, wy1 + 12.0f,
                                            wx0 - 1.0f, wy0 - 1.0f, wx1 + 1.0f, wy1 + 1.0f);
        }
    }

    // 1. Take out the quads that no longer hold: the edit's own, and the game's where the ground is now dry.
    std::vector<KilledQuad> killed;
    for (int vy = vy0; vy < vy1; ++vy) {
        for (int vx = vx0; vx < vx1; ++vx) {
            auto found = g_waterQuads.find(WaterCellKey(vx - halfGrid, vy - halfGrid));
            if (found == g_waterQuads.end()) continue;
            bool under = false;
            for (int k = 0; k < 4; ++k) {
                const int cx = vx + (k == 1 || k == 2 ? 1 : 0);
                const int cy = vy + (k >= 2 ? 1 : 0);
                if (height(cx, cy) <= kWaterTest) under = true;
            }
            if (found->second.edit || !under) {
                KilledQuad q;
                q.position = found->second.position;
                for (int j = 0; j < 6; ++j) q.indices[j] = indices[q.position + j];
                killed.push_back(q);
                g_waterQuads.erase(found);
            }
        }
    }

    // 2. The quads the region needs now, for the cells no game quad covers.
    const RiverCover riverCover = GatherRiverCover(terrain, static_cast<float>(vx0 - halfGrid) - 1.0f,
                                                   static_cast<float>(vy0 - halfGrid) - 1.0f,
                                                   static_cast<float>(vx1 - halfGrid) + 1.0f,
                                                   static_cast<float>(vy1 - halfGrid) + 1.0f);
    auto drawn = [&](int col, int row) {
        return g_waterQuads.count(WaterCellKey(col, row)) != 0 || riverCover.Covers(col, row);
    };
    const WaterPatch patch = BuildWaterPatchStyled(height, vx0, vy0, vx1, vy1, halfGrid, style, drawn);
    if (killed.empty() && patch.indices.empty()) return;

    // 3. Apply: the killed quads become one repeated vertex (nothing is drawn); the patch is appended as a
    // new range.
    static_assert(sizeof(WaterVertex) == sizeof(TerrainVertex), "the patch's vertices are the mesh's");
    const int rangesOfKind = style.river ? riverRangesBefore : rangesBefore;
    if (!patch.indices.empty() && rangesOfKind >= kMaxWaterRanges) {
        std::fprintf(stderr, "[TerrainEdit] water: the mesh already holds %d %s ranges, the most that can be "
                     "drawn: this stroke's water was not changed\n", rangesOfKind,
                     style.river ? "river" : "lake");
        g_waterQuadsValid = false;   // step 1 took quads out of the cache that the mesh still draws
        return;
    }
    g_undo.RecordWater(vertsBefore, indicesBefore, rangesBefore, riverRangesBefore);
    for (const KilledQuad& q : killed) {
        g_undo.RecordKilledQuad(q.position, q.indices);
        for (int j = 1; j < 6; ++j) indices[q.position + j] = indices[q.position];
    }
    if (!patch.indices.empty()) {
        GeometryDrawRange range;
        range.values[0] = indicesBefore;                          // the first index
        range.values[1] = vertsBefore;                            // the base vertex
        range.values[2] = static_cast<int>(patch.vertices.size());
        range.values[3] = static_cast<int>(patch.indices.size());
        const bool ok = AppendToArray(terrain->_contourVertices,
            reinterpret_cast<const TerrainVertex*>(patch.vertices.data()),
                                      static_cast<int>(patch.vertices.size()), 2) &&
                        AppendToArray(terrain->_contourIndices, patch.indices.data(),
                            static_cast<int>(patch.indices.size()), 2) &&
                        AppendToArray(style.river ? terrain->_contourRangesA : terrain->_contourRangesB,
                            &range, 1, 4);
        if (!ok) {
            terrain->_contourVertices.size = vertsBefore;
            terrain->_contourIndices.size = indicesBefore;
            terrain->_contourRangesB.size = rangesBefore;
            terrain->_contourRangesA.size = riverRangesBefore;
            g_waterQuadsValid = false;
            std::fprintf(stderr, "[TerrainEdit] water: out of memory, the new water was not added\n");
        } else {
            for (size_t i = 0; i < patch.cells.size(); ++i) {
                WaterQuad q;
                q.position = indicesBefore + static_cast<int>(i) * 6;
                q.edit = true;
                q.z = style.z;
                q.river = style.river;
                g_waterQuads[patch.cells[i]] = q;
            }
        }
    }
    UploadWaterMesh(terrain);
    std::fprintf(stderr,
        "[TerrainEdit] water: %zu quad(s) taken out, %zu added as %s range %d (%zu vertices)%s\n",
                 killed.size(), patch.cells.size(), style.river ? "river" : "lake",
                     style.river ? riverRangesBefore : rangesBefore,
                 patch.vertices.size(),
                 patch.overflow ? ", stopped short: a range holds 65535" : "");
}

// ---- the ground texture (docs/extensions/handins/TERRAINEDIT.md, terrain_texture.h)
// ----------------------------------------- The game's ground texture (WorldGeometry::_heightBytes: four
// bytes per cell of the height grid, R rock, G grass, B shading, uploaded as _heightImage) is made once
// when the world is. This makes the bytes of the cells an edit changed again from the heights as they are
// now, and uploads the texture: lowered ground stops being mountain rock, a new mountain gets its rock, a
// dug lake gets its brown bed and shore. The rock byte is made again only for cells whose height changed
// (its scatter noise is not the game's); the grass and shading bytes, which read the neighbours, for the 8
// x 8 blocks around them and two blocks out.
void UploadGroundTexture(WorldGeometry* terrain) {
    const int dim = terrain->_gridSize;
    if (terrain->_heightImage == nullptr || terrain->_heightBytes == nullptr || dim <= 0) return;
    if (Video::Device::_singleton == nullptr) return;
    Video::Device::_singleton->Lock();
    terrain->_heightImage->CopyToTexture(terrain->_heightBytes, dim, dim);
    Video::Device::_singleton->Unlock();
}

void UpdateGroundTexture(WorldGeometry* terrain) {
    using namespace Ext::TerrainEdit;
    const int dim = terrain->_gridSize;
    const int stride = terrain->_fieldD0;
    const int16_t* heights = terrain->_heights.data;
    uint8_t* bytes = terrain->_heightBytes;
    if (heights == nullptr || bytes == nullptr || dim <= 0 || stride != dim + 1) return;
    const auto& changedVertices = g_undo.Current().heights;
    if (changedVertices.empty()) return;
    auto height = [&](int c, int r) {
        const int cc = c < 0 ? 0 : (c > dim ? dim : c);
        const int rr = r < 0 ? 0 : (r > dim ? dim : r);
        return static_cast<float>(heights[static_cast<int64_t>(stride) * rr + cc]) / 256.0f;
    };
    // the cells whose height changed, and the 8 x 8 blocks around them
    std::unordered_set<int> changed;
    std::unordered_set<int> blocks;
    const int blocksPerRow = (dim + 7) / 8;
    for (const auto& entry : changedVertices) {
        const int vx = entry.first % stride;
        const int vy = entry.first / stride;
        if (vx >= dim || vy >= dim) continue;
        changed.insert(vy * dim + vx);
        for (int by = (vy >> 3) - 2; by <= (vy >> 3) + 2; ++by) {
            for (int bx = (vx >> 3) - 2; bx <= (vx >> 3) + 2; ++bx) {
                if (bx >= 0 && by >= 0 && bx < blocksPerRow && by < blocksPerRow) {
                    blocks.insert(by * blocksPerRow + bx);
                }
            }
        }
    }
    const uint32_t seed = 0x5eed1234u;
    // The game's own scatter, measured from this world's texture: the rock bytes of ordinary ground (height
    // between -0.4 and 7.5) that this stroke has not changed, every 5th cell each way. The edit's noise is
    // scaled to that mean and spread.
    ScatterLook look;
    {
        double sum = 0.0;
        double sum2 = 0.0;
        long long samples = 0;
        for (int row = 0; row < dim; row += 5) {
            for (int col = 0; col < dim; col += 5) {
                const float h = height(col, row);
                if (h < -0.4f || h > 7.5f) continue;
                const int cell = row * dim + col;
                if (changed.count(cell) != 0) continue;
                const double v = static_cast<double>(bytes[static_cast<size_t>(cell) * 4]);
                sum += v;
                sum2 += v * v;
                ++samples;
            }
        }
        if (samples >= 200) {
            const double mean = sum / static_cast<double>(samples);
            const double var = sum2 / static_cast<double>(samples) - mean * mean;
            look.mean = static_cast<float>(mean);
            look.spread = static_cast<float>(var > 1.0 ? std::sqrt(var) : 1.0);
        }
    }
    float noiseMean = 0.5f;
    float noiseSpread = 0.2f;
    ScatterNoiseStats(seed, noiseMean, noiseSpread);
    int repainted = 0;
    for (const int block : blocks) {
        const int row0 = (block / blocksPerRow) * 8;
        const int col0 = (block % blocksPerRow) * 8;
        for (int row = row0; row < row0 + 8 && row < dim; ++row) {
            for (int col = col0; col < col0 + 8 && col < dim; ++col) {
                const int cell = row * dim + col;
                uint8_t* px = bytes + static_cast<size_t>(cell) * 4;
                const float h = height(col, row);
                float normal[3];
                CellNormal(height, dim, col, row, normal);
                uint8_t rock = px[0];
                if (changed.count(cell) != 0) {
                    const uint32_t scatter =
                        ScatterByte(ScatterNoise01(col, row, seed), noiseMean, noiseSpread, look);
                    rock = RockByte(h, scatter);
                }
                const uint8_t grass = GrassByte(h, normal[2]);
                const uint8_t occlusion = OcclusionByte(height, dim, col, row);
                if (px[0] == rock && px[1] == grass && px[2] == occlusion) continue;
                uint32_t before = 0;
                std::memcpy(&before, px, 4);
                g_undo.RecordTexel(cell, before);
                px[0] = rock;
                px[1] = grass;
                px[2] = occlusion;
                ++repainted;
            }
        }
    }
    if (repainted > 0) UploadGroundTexture(terrain);
    std::fprintf(stderr,
                 "[TerrainEdit] ground texture: %d cell(s) repainted (the game's own rock scatter here: "
                 "mean %.1f, spread %.1f)\n",
                 repainted, static_cast<double>(look.mean), static_cast<double>(look.spread));
}

// ---- the town's water access (docs/extensions/handins/TERRAINEDIT.md)
// -------------------------------------------------
// The town keeps a list of pairs {a buildable cell that touches water, the water cell it touches}
// (CitizenBehavior +0x130),
// built once when a world is made (MapTileGrid_Build's shore loop) and saved with the town, and a coarse
// index of it (+0x140,
// blocks of 8 x 8 cells holding the pairs' indices, and below zero the wells). The fire-fighting search reads
// both to find the
// nearest water (FindWater). A lake the brush digs is in neither, and a lake it drains is still in both. This
// makes the pairs
// of an area again from the cell types as they are now, by the shore loop's own rule (a buildable cell, type
// 2, takes the first
// water neighbour, type 7 or 8, of its 3 x 3, rows then columns), and builds the index again.
void RefreshWaterAccess(Map* map, int boxMinX, int boxMinY, int boxMaxX, int boxMaxY) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_citizenList == nullptr || map == nullptr) return;
    CitizenBehavior* town = game->m_citizenList;
    if (town->field_0x140._size < 1) return;   // the index is not built yet (a load builds it after this)
    const int size = map->size;
    const int offset = size >> 1;
    int col0 = boxMinX - offset - 3;
    int row0 = boxMinY - offset - 3;
    int col1 = boxMaxX - offset + 3;
    int row1 = boxMaxY - offset + 3;
    if (col0 < 0) col0 = 0;
    if (row0 < 0) row0 = 0;
    if (col1 > size - 1) col1 = size - 1;
    if (row1 > size - 1) row1 = size - 1;
    auto inArea = [&](int cell) {
        const int col = cell % size;
        const int row = cell / size;
        return col >= col0 && col <= col1 && row >= row0 && row <= row1;
    };

    // The pairs that do not touch the area stay; of those that do, the cell that is outside the area is
    // looked at again.
    std::vector<WaterCellPair> keep;
    std::set<int> look;   // the cells whose pair is made again, in cell order
    const int oldCount = town->field_0x130._size;
    const WaterCellPair* old = town->field_0x130._data;
    for (int i = 0; i < oldCount; ++i) {
        const bool a = inArea(old[i].cellA);
        const bool b = inArea(old[i].cellB);
        if (!a && !b) {
            keep.push_back(old[i]);
        } else if (!a) {
            look.insert(old[i].cellA);
        }
    }
    for (int row = row0; row <= row1; ++row) {
        for (int col = col0; col <= col1; ++col) look.insert(row * size + col);
    }
    const MapCell* cells = map->cells.GetData();
    for (const int cell : look) {
        if (cells[cell].terrainType != PathType::Normal) continue;
        const int x = cell % size;
        const int y = cell / size;
        bool found = false;
        for (int ny = y - 1; ny <= y + 1 && !found; ++ny) {
            for (int nx = x - 1; nx <= x + 1; ++nx) {
                if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
                const int neighbour = size * ny + nx;
                const PathType t = cells[neighbour].terrainType;
                if (t == PathType::Water || t == PathType::DeepWater) {
                    WaterCellPair pair;
                    pair.cellA = cell;
                    pair.cellB = neighbour;
                    keep.push_back(pair);
                    found = true;
                    break;
                }
            }
        }
    }

    // Write the pairs back, and build the index again: the entries of each block that are pairs (not below
    // zero, the wells) are dropped and every pair is filed in its block.
    WaterCellPair zero;
    zero.cellA = 0;
    zero.cellB = 0;
    town->field_0x130.SetSize(static_cast<int>(keep.size()), zero);
    for (size_t i = 0; i < keep.size(); ++i) town->field_0x130._data[i] = keep[i];
    for (int b = 0; b < town->field_0x140._size; ++b) {
        Collection::Array<int32_t>& block = town->field_0x140._data[b];
        int out = 0;
        for (int i = 0; i < block._size; ++i) {
            if (block._data[i] < 0) block._data[out++] = block._data[i];
        }
        block._size = out;
    }
    for (size_t i = 0; i < keep.size(); ++i) {
        town->AddCellEntry(keep[i].cellA % size, keep[i].cellA / size, static_cast<int>(i));
    }
    std::fprintf(stderr, "[TerrainEdit] water access: %d pair(s) before, %zu after\n", oldCount, keep.size());
}

// After a stroke: every cell of the area it changed (and two cells around) that the game now calls water
// has its tree, rock or ore deposit taken away. Painting does this as cells are re-typed; this is the check
// that does not depend on the type a cell was stored with, and it logs what it saw (the stroke's cells that
// hold a resource, and what the game says each is).
void RemoveNaturalResource(ComponentEntity* entity);   // below, with the tool

void RemoveResourcesInLakes(Map* map, const StrokeBox& box) {
    if (!box.valid) return;
    const int size = map->size;
    const int offset = size >> 1;
    int resources = 0;
    int inWater = 0;
    int removed = 0;
    int byClass[16] = {};
    for (int row = box.minY - offset - 2; row <= box.maxY - offset + 2; ++row) {
        for (int col = box.minX - offset - 2; col <= box.maxX - offset + 2; ++col) {
            if (col < 0 || row < 0 || col >= size || row >= size) continue;
            const int idx = row * size + col;
            ComponentEntity* entity = map->cells.GetData()[idx].occupant.Get();
            if (entity == nullptr || entity->FindComponent<NaturalResourceComponent>() == nullptr) continue;
            ++resources;
            const int cls = map->ClassifyTerrain(idx);
            if (cls >= 0 && cls < 16) ++byClass[cls];
            if (cls != static_cast<int>(PathType::Water) && cls != static_cast<int>(PathType::DeepWater)) {
                continue;
            }
            ++inWater;
            if ((entity->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) == 0) ++removed;
            RemoveNaturalResource(entity);
        }
    }
    std::fprintf(stderr,
        "[TerrainEdit] lakes: %d resource(s) in the stroke's area, %d on water cells, %d removed now "
                 "(classes: 2=%d 6=%d 7=%d 8=%d 10=%d other=%d)\n", resources, inWater, removed,
                     byClass[2], byClass[6], byClass[7],
                 byClass[8], byClass[10],
                     resources - byClass[2] - byClass[6] - byClass[7] - byClass[8] - byClass[10]);
}

// A stroke ends: resources in new lakes are taken away, water for what it took under, then it is one undo
// step.
void FinishStroke() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (g_undo.Open() && game != nullptr && game->m_worldGeometry != nullptr && game->m_game != nullptr) {
        RemoveResourcesInLakes(game->m_game, g_box);
        if (g_box.valid) {
            g_undo.RecordBox(g_box.minX, g_box.minY, g_box.maxX, g_box.maxY);
            RefreshWaterAccess(game->m_game, g_box.minX, g_box.minY, g_box.maxX, g_box.maxY);
        }
        AddWaterForStroke(game->m_worldGeometry);
        UpdateGroundTexture(game->m_worldGeometry);
    }
    g_undo.CommitStroke();
}

void NotifyUndoChanged() {
    if (g_undoHook != nullptr) g_undoHook();
}

// A tree, rock or other natural resource stands where the ground was when it was placed: its model's
// transform carries that height (NaturalResourceComponent::PlaceAtCell), so ground raised or lowered under
// it buries it or leaves it in the air. This puts it back on the ground: the transform's height is the
// terrain's at the model's own position, the mesh is given the transform the way PlaceAtCell gives it (rock
// seating included) and its pose is rebuilt, as RockSeat::SeatAll does for a model that is already in the
// scene. The saved transform changes with it, so a save keeps the new height.
void ReseatOccupant(Map* map, ComponentEntity* entity) {
    if (entity == nullptr) return;
    if (entity->FindComponent<NaturalResourceComponent>() == nullptr) return;
    ModelComponent* model = entity->FindComponent<ModelComponent>();
    if (model == nullptr || model->modelId < 0 || model->meshIndex < 0) return;
    const float in[2] = { model->transform.translation.x, model->transform.translation.y };
    float out[3] = { 0.0f, 0.0f, 0.0f };
    map->GetTerrainPos(out, in, '\0', nullptr);   // PlaceAtCell's own query
    if (out[2] == model->transform.translation.z) return;
    model->transform.translation.z = out[2];
    if (Ext::Fix().rock_seat) {
        Ext::RockSeat::SetMeshTransform(model);
    } else {
        model->meshInstance.SetTransform(model->transform);
    }
    System::Time t{0};
    RtSystem_Time_Ctor(&t, 0.0f);
    model->meshInstance.Update(t);
}

// A tree, rock or ore deposit on a cell that has become water is taken away: it cannot stand in a lake, and
// left in place it would sit on the lake bed showing through the water. It goes the way the game takes a
// resource away when it is not harvested (NaturalResourceComponent::RemoveResource, its first step): the
// entity is marked for removal and queued, and the game's own teardown frees its cell. An undo does not
// bring it back.
void RemoveNaturalResource(ComponentEntity* entity) {
    if (entity == nullptr || entity->FindComponent<NaturalResourceComponent>() == nullptr) return;
    if ((entity->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) != 0) return;   // already marked
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_entityList == nullptr) return;
    entity->MarkForRemoval();
    game->m_entityList->removalList.SetSize(game->m_entityList->removalList._size + 1, entity);
}

// ---- the resource tool: plant natural resources ---------------------------------------------------------
// The Resource drop down lists the templates of the natural resources standing on the map (trees, plants,
// rocks, ore): the distinct descriptions of the occupants that carry a NaturalResourceComponent. A new world
// starts the list over; within one world each scan only adds, so a resource that came onto the map later (a
// forester planting a tree the start did not scatter) joins the list and nothing on it ever goes away. The
// scan runs when the page opens and when a stroke starts. A stroke fills the brush's disc up to the density
// (plant_math.h, PlantTarget and PickCells) with the picked template, the way worldgen and the game's own
// CreateNaturalResource do (starttown.cpp, Action/createnaturalresource.cpp), without the orchard parent that
// action wants.
std::vector<ComponentDescription*> g_palette;
std::vector<std::string> g_paletteNames;
std::string g_plantName;               // the template the drop down shows, kept across worlds by name
int g_plantIndex = 0;
const void* g_paletteMap = nullptr;
uint32_t g_paletteEpoch = 0;
uint32_t g_plantSeed = 1u;

void BuildPalette(Map* map, bool keepKnown) {
    std::vector<ComponentDescription*> found;
    std::vector<std::string> names;
    std::set<ComponentDescription*> seen;
    if (keepKnown) {
        found = g_palette;
        names = g_paletteNames;
        seen.insert(found.begin(), found.end());
    }
    g_palette.clear();
    g_paletteNames.clear();
    g_paletteMap = map;
    g_paletteEpoch = Ext::LockEpoch();
    const int n = map->size * map->size;
    for (int i = 0; i < n; ++i) {
        ComponentEntity* e = map->cells.GetData()[i].occupant.Get();
        if (e == nullptr || e->FindComponent<NaturalResourceComponent>() == nullptr) continue;
        ComponentDescription* d = e->_description;
        // a resource is made of its natural-resource and model components: a template without them
        // cannot be placed
        if (d == nullptr || d->FindDescription<ModelDescription>() == nullptr || !seen.insert(d).second) {
            continue;
        }
        const System::Filename path = static_cast<const ::Object::External*>(d)->GetPath();
        found.push_back(d);
        names.push_back(Ext::TerrainEdit::ResourceShortName(path._data != nullptr ? path._data : ""));
    }
    for (const size_t i : Ext::TerrainEdit::ResourceOrder(names)) {
        g_palette.push_back(found[i]);
        g_paletteNames.push_back(names[i]);
    }
    g_plantIndex = 0;
    for (size_t i = 0; i < g_paletteNames.size(); ++i) {
        if (g_paletteNames[i] == g_plantName) g_plantIndex = static_cast<int>(i);
    }
    if (!g_paletteNames.empty()) g_plantName = g_paletteNames[static_cast<size_t>(g_plantIndex)];
    std::fprintf(stderr, "[TerrainEdit] resources: %d template(s) on the map\n",
                 static_cast<int>(g_palette.size()));
}

void EnsurePalette() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_game == nullptr) return;
    if (game->m_game != g_paletteMap || g_paletteEpoch != Ext::LockEpoch()) BuildPalette(game->m_game, false);
}

// Looks for resources that have come onto the map since the last scan and adds them to the list.
void RescanPalette() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_game == nullptr) return;
    BuildPalette(game->m_game, game->m_game == g_paletteMap && g_paletteEpoch == Ext::LockEpoch());
}

// A cell takes a resource when nothing stands on it, no road is on it and it is ordinary land (not a
// mountain or water).
bool CanPlantOn(const MapCell& cell) {
    if (cell.occupant.Get() != nullptr || cell.blocker.Get() != nullptr || IsRoadCell(cell)) return false;
    const PathType t = cell.terrainType;
    return IsNaturalType(t) && t != PathType::Unusable && t != PathType::Water && t != PathType::DeepWater;
}

// Plants the picked template on `cell` and returns the entity, or null when the cell does not take it.
// Plants are made full size with their life ahead of them: SetProgress takes a fraction of the plant's
// whole life (growth = fraction * growthScale; the plant dies when its growth passes growthScale,
// GrowthComponent::Update), and a plant is full size at growth 1.0, so it starts at 1 / growthScale. A
// growthScale of zero or less (a plant that is never harvested or never dies; GrowthComponent::Startup sets
// it to -1) gives growth 1.0 too, and the sign keeps it from ever passing the end. Rocks and ore have no
// GrowthComponent and skip that step. The mesh variant is picked under worldgen's own condition (the
// description's _b48 and a model with mesh groups, starttown.cpp), whether or not there is a GrowthComponent.
ComponentEntity* PlantAt(Map* map, int cell) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (g_palette.empty() || cell < 0 || cell >= map->size * map->size) return nullptr;
    if (!CanPlantOn(map->cells.GetData()[cell])) return nullptr;
    ComponentDescription* desc = g_palette[static_cast<size_t>(g_plantIndex)];
    const NaturalResourceDescription* nrDesc = desc->FindDescription<NaturalResourceDescription>();
    if (nrDesc == nullptr || desc->FindDescription<ModelDescription>() == nullptr) return nullptr;
    void* mem = System::MemoryAllocator::MemAlloc(0x48, 8);
    if (mem == nullptr) return nullptr;
    ComponentEntity* entity = ::new (mem) ComponentEntity(desc);
    NaturalResourceComponent* nr = entity->FindComponent<NaturalResourceComponent>();
    GrowthComponent* growth = entity->FindComponent<GrowthComponent>();
    ModelComponent* model = entity->FindComponent<ModelComponent>();
    if (nr == nullptr || model == nullptr) return nullptr;   // unreachable: checked on the description above
    nr->PlaceAtCell(cell);
    if (growth != nullptr && growth->growthScale != 0.0f) growth->SetProgress(1.0f / growth->growthScale);
    if (nrDesc->_b48 && model->MeshGroupCount() > 0) {
        System::Random* random = game->m_random;
        const uint32_t variant = random->Rand(model->MeshGroupCount());
        const int meshes = model->MeshCount(static_cast<int>(variant));
        const uint32_t sub = meshes > 0 ? random->Rand(static_cast<uint32_t>(meshes)) : 0u;
        model->SelectMesh(static_cast<uint16_t>(variant), static_cast<int16_t>(sub));
    }
    Object::InternalPtr<ComponentEntity> ref = entity;
    game->m_entityList->RegisterEntity(&ref, static_cast<int>(desc->_category));
    return entity;
}

// One frame of a resource stroke at `cell`: each free cell of the disc is decided once per stroke, and the
// ones chosen get the picked resource. The stroke is one undo step (the entities it planted are
// remembered).
void PlantStroke(Map* map, WorldGeometry* terrain, int cell, bool strokeStart) {
    using namespace Ext::TerrainEdit;
    int16_t* heights = terrain->_heights.data;
    const int stride = terrain->_fieldD0;
    if (heights == nullptr || stride <= 0) return;
    if (strokeStart) RescanPalette(); else EnsurePalette();
    if (g_palette.empty()) return;
    if (strokeStart || !g_undo.Open()) {
        if (!SameWorld(map, heights, stride)) AdoptWorld(map, heights, stride);
        g_undoRefused = false;
        g_redoBuilt = false;
        g_redoNote = "";
        g_undo.BeginStroke(g_replaying);
        g_undo.RecordTool(static_cast<int>(g_state.mode), g_plantName);
        g_plantSeed = static_cast<uint32_t>(cell) * 2654435761u + 0x1234567u;
    }
    g_undo.RecordFrame(cell, g_state.radius, g_state.strength, g_state.density);
    // The density is how full the disc ends up, and what already stands in it counts: the disc's eligible
    // cells are the free ones and those a natural resource stands on, and the stroke plants on enough free
    // ones to bring the disc up to the density. A click on ground that is full enough plants nothing, and a
    // held button stops adding once the disc is full, so repeated clicks over one place never go past the
    // density.
    const int size = map->size;
    const MapCell* cells = map->cells.GetData();
    std::vector<int> freeCells;
    int eligible = 0;
    int existing = 0;
    for (const Cell& c : Disc(cell % size, cell / size, g_state.radius, size)) {
        if (c.weight <= 0.0f) continue;
        const int idx = c.row * size + c.col;
        ComponentEntity* standing = cells[idx].occupant.Get();
        if (standing != nullptr && standing->FindComponent<NaturalResourceComponent>() != nullptr) {
            ++eligible;
            ++existing;
        } else if (CanPlantOn(cells[idx])) {
            ++eligible;
            freeCells.push_back(idx);
        }
    }
    const int missing = PlantTarget(eligible, g_state.density) - existing;
    for (const int idx : PickCells(freeCells, missing, g_plantSeed)) {
        ComponentEntity* entity = PlantAt(map, idx);
        if (entity == nullptr) continue;
        g_undo.RecordPlanted(idx, reinterpret_cast<uintptr_t>(entity), entity->GetGeneration());
    }
}

}  // namespace

class TerrainBrushTool final : public Tool {
    DECLARE_EXE_TYPE(TerrainBrushTool)

public:
    TerrainBrushTool() : Tool() {}
    ~TerrainBrushTool() override = default;

    void Startup() override {
        g_toolRunning = true;
    }
    void Shutdown() override {
        g_toolRunning = false;
        FinishStroke();
        NotifyUndoChanged();
    }
    // nonzero: the manager keeps the camera hotkeys and the right-click cancel
    char OnKey(char /*active*/) override { return 1; }
    char OnCancel() override { return 0; }               // zero: the manager drops back to the EditTool
    int  GetCursor() const override { return 5; }        // the "place" cursor

    void Update(const System::Time* /*time*/, char active) override {
        if (g_frameHook != nullptr) g_frameHook();
        const Input::Configuration* cfg = Input::Configuration::_singleton;
        const bool held = active && cfg != nullptr && cfg->_keyMap != nullptr && cfg->_keyMap->GetValue(0,
            ToUnderlying(GameKey::MouseLeft)) != 0;   // left button
        const bool pressed = held && !_wasHeld;
        if (!held && _wasHeld) {
            FinishStroke();   // the button went up: the stroke is one undo step
            NotifyUndoChanged();
        }
        _wasHeld = held;
        if (!held) return;
        GameUpdateLoop* game = GameUpdateLoop::_singleton;
        if (game == nullptr || game->m_game == nullptr || game->m_worldGeometry == nullptr) return;
        const int cell = PlayerRay_ScreenToCell_101270(nullptr);
        if (cell < 0) return;
        Paint(game->m_game, game->m_worldGeometry, cell, pressed);
    }

    void Draw(Video::Context* context, char /*active*/) override {
        GameUpdateLoop* game = GameUpdateLoop::_singleton;
        if (game == nullptr || game->m_game == nullptr) return;
        const int cell = PlayerRay_ScreenToCell_101270(nullptr);
        if (cell < 0) return;
        const int size = game->m_game->size;
        Collection::Array<ToolTileRecord> tiles;
        for (const Ext::TerrainEdit::Cell& c : Ext::TerrainEdit::Disc(cell % size, cell / size,
            g_state.radius, size)) {
            if (c.weight <= 0.0f) continue;
            ToolTileRecord rec;
            rec.col = c.col;
            rec.row = c.row;
            rec.variant = 0;
            rec.color = DiscColor(g_state.mode);
            rec.angle = 0;
            tiles.SetSize(tiles._size + 1, rec);
        }
        Collection::Array<ToolSegmentRecord> none;
        PlayerTileRender_DrawOverlay_08c0(nullptr, context, &tiles, &none);
    }

private:
    bool  _wasHeld = false;

public:
    // One frame of the brush at `cell`: change the heights, mark the chunks, classify the cells again. Static
    // so a Redo can paint a stroke again without the tool running.
    static void Paint(Map* map, WorldGeometry* terrain, int cell, bool strokeStart) {
        using namespace Ext::TerrainEdit;
        if (g_state.mode == Ext::TerrainEdit::Mode::Plant) {
            PlantStroke(map, terrain, cell, strokeStart);
            return;
        }
        int16_t* heights = terrain->_heights.data;
        if (heights == nullptr) return;
        const int size   = map->size;
        const int stride = terrain->_fieldD0;    // vertices per edge
        const int offset = size >> 1;            // the map's cells are the middle of the (wider) height grid
        if (strokeStart || !g_undo.Open()) {
            if (!SameWorld(map, heights, stride)) {
                AdoptWorld(map, heights, stride);   // another world: the old strokes do not belong to it
            }
            g_undoRefused = false;
            g_redoBuilt = false;
            g_redoNote = "";
            g_undo.BeginStroke(g_replaying);
            g_undo.RecordTool(static_cast<int>(g_state.mode), g_plantName);
            g_box = StrokeBox();
        }
        g_undo.RecordFrame(cell, g_state.radius, g_state.strength, g_state.density);
        if (strokeStart) {
            // the mountain noise differs from stroke to stroke
            g_paintSeed = static_cast<uint32_t>(cell) * 2654435761u + 0x1234567u;
            const int cx = cell % size + offset;
            const int cy = cell / size + offset;
            if (cx >= 0 && cy >= 0 && cx < stride && cy < stride) {
                g_flattenTarget = FlattenTarget(heights[static_cast<int64_t>(stride) * cy + cx]);
            }
        }
        const int centreCol = cell % size;
        const int centreRow = cell / size;
        const std::vector<Cell> disc = Disc(centreCol, centreRow, g_state.radius, size);
        // Smooth aims each vertex at the average of its neighbours, read before any vertex of this frame is
        // written, so the result does not depend on the order the vertices are visited in.
        std::vector<float> averages;
        if (g_state.mode == Mode::Smooth) {
            averages.reserve(disc.size());
            for (const Cell& c : disc) {
                averages.push_back(NeighbourAverage(heights, stride, c.col + offset, c.row + offset));
            }
        }
        size_t discIndex = 0;
        for (const Cell& c : disc) {
            const size_t thisIndex = discIndex++;
            if (c.weight <= 0.0f) continue;
            const int vx = c.col + offset, vy = c.row + offset;
            if (vx < 0 || vy < 0 || vx >= stride || vy >= stride) continue;
            // a building stands at one of its corners
            if (VertexTouchesBlocker(map, vx, vy, offset)) continue;
            int16_t& word = heights[static_cast<int64_t>(stride) * vy + vx];
            float target = g_flattenTarget;
            if (g_state.mode == Mode::Mountain) {
                const float dx = static_cast<float>(c.col - centreCol);
                const float dy = static_cast<float>(c.row - centreRow);
                target = MountainHeight(std::sqrt(dx * dx + dy * dy), g_state.radius, g_state.strength,
                    vx, vy, g_paintSeed);
            }
            if (g_state.mode == Mode::Smooth) target = averages[thisIndex];
            const int next = NextWord(word, g_state.mode, c.weight, g_state.strength, target);
            if (next == word) continue;
            g_undo.RecordHeight(stride * vy + vx, word);
            g_box.Add(vx, vy);
            word = static_cast<int16_t>(next);
            terrain->MarkChunksDirty(vx, vy);
        }
        StillTerrain::Bump();   // heightmap words were written
        // Classify again a little beyond the disc: a cell's slope reads its neighbours' heights.
        for (const Cell& c : Disc(cell % size, cell / size, g_state.radius + 2.0f, size)) {
            const int idx = c.row * size + c.col;
            MapCell& mc = map->cells.GetData()[idx];
            ReseatOccupant(map, mc.occupant.Get());   // a tree or rock on the cell goes with the ground
            if (mc.blocker.Get() != nullptr || IsRoadCell(mc) || !IsNaturalType(mc.terrainType)) continue;
            const int cls = map->ClassifyTerrain(idx);
            if (cls != static_cast<uint8_t>(mc.terrainType) && IsNaturalType(static_cast<PathType>(cls))) {
                g_undo.RecordType(idx, static_cast<uint8_t>(mc.terrainType));
                g_undo.RecordWrittenType(idx, static_cast<uint8_t>(cls));
                map->SetTerrainType(idx, static_cast<uint32_t>(cls));
                if (cls == static_cast<int>(PathType::Water) ||
                    cls == static_cast<int>(PathType::DeepWater)) {
                    RemoveNaturalResource(mc.occupant.Get());   // a lake covers it
                }
            }
        }
    }
};

::Object::Base* TerrainBrushTool::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TerrainBrushTool), 8);
    if (!p) return nullptr;
    return new (p) TerrainBrushTool();
}

::Object::Base* TerrainBrushTool::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return new (storage) TerrainBrushTool();
}

DEFINE_EXE_TYPE(TerrainBrushTool, "Tool")

namespace Ext {
namespace TerrainEdit {

BrushState& State() {
    return g_state;
}

void StartBrush() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_toolManager == nullptr) return;
    PlayerToolManager* manager = game->m_toolManager;
    // What SelectTool does to install a tool (Dialog/toolbardialog.cpp), without the registry lookup: shut
    // down and delete the current tool, then set the new one's three fields and start it.
    manager->field_0x20 = nullptr;
    if (manager->currentTool != nullptr) {
        manager->currentTool->Shutdown();
        delete manager->currentTool.Get();
        manager->currentTool = nullptr;
    }
    Tool* tool = static_cast<Tool*>(TerrainBrushTool::CreateNewObject());
    if (tool == nullptr) return;
    tool->dialogPtr      = manager;
    tool->descriptionPtr = nullptr;
    tool->toolSetting    = nullptr;
    manager->currentTool = tool;
    tool->Startup();
    std::fprintf(stderr, "[TerrainEdit] brush started\n");
    if (game->m_worldGeometry != nullptr) LogWaterMesh(game->m_worldGeometry);
}

// How many strokes of a list an Undo or a Redo can reach: from the next one down to the first with something
// built on its ground (they go in order, so the ones under it cannot be reached). One state per list, and the
// strokes are read again when the list changes and then at most twice a second: `skips(i, ...)` is the
// list's own test of stroke i, `blocked` is the list's "the next one cannot go" flag.
struct Recount {
    uint32_t version = 0;
    size_t   reachable = 0;
    bool     valid = false;
    std::chrono::steady_clock::time_point at;
};

template <class Skips>
size_t CountReachable(Recount& r, size_t depth, uint32_t version, Skips skips, bool& blocked) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (depth == 0) {
        r.reachable = 0;
        r.version = version;
        r.valid = true;
        blocked = false;
        return 0;
    }
    if (g_undo.Open()) return r.reachable;
    // The strokes were painted on one world; after a new game or a load there are none.
    const bool here = game != nullptr && game->m_game != nullptr && game->m_worldGeometry != nullptr &&
                      SameWorld(game->m_game, game->m_worldGeometry->_heights.data,
                                game->m_worldGeometry->_fieldD0);
    if (!here) {
        ForgetWorld();
        r.reachable = 0;
        return 0;
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const bool due = !r.valid || r.version != version || now - r.at >= std::chrono::milliseconds(500);
    if (due) {
        r.at = now;
        r.version = version;
        r.valid = true;
        const int stride = game->m_worldGeometry->_fieldD0;
        const int offset = game->m_game->size >> 1;
        r.reachable = 0;
        while (r.reachable < depth && !skips(r.reachable, game->m_game, stride, offset)) ++r.reachable;
        blocked = r.reachable == 0;   // the next stroke has something built on its ground
    }
    return r.reachable;
}

size_t UndoDepth() {
    static Recount recount;
    return CountReachable(
        recount, g_undo.Depth(), g_undo.Version(),
        [](size_t i, Map* map, int stride, int offset) {
            return UndoWouldSkip(map, *g_undo.FromTop(i), stride, offset, i == 0);
        },
        g_undoRefused);
}

void SetUndoChangedHook(void (*hook)()) {
    g_undoHook = hook;
}

void SetFrameHook(void (*hook)()) {
    g_frameHook = hook;
}

// A Redo paints a stroke again through the brush, which leaves alone every vertex that touches a building or
// a road, so a stroke painted over ground that has been built on since would come out different. Like an
// Undo, it is refused as a whole then: when a vertex the stroke changed touches a blocker or a road, or a
// cell it retyped is blocked or a road. Building anywhere else does not matter.
static bool RedoWouldSkip(Map* map, const Ext::TerrainEdit::Replay& replay, int stride, int offset) {
    const int size = map->size;
    for (const int index : replay.vertices) {
        const int vx = index % stride;
        const int vy = index / stride;
        if (vy >= stride) continue;
        if (VertexTouchesBlocker(map, vx, vy, offset)) return true;
    }
    for (const int index : replay.cells) {
        if (index < 0 || index >= size * size) continue;
        const MapCell& cell = map->cells.GetData()[index];
        if (cell.blocker.Get() != nullptr || IsRoadCell(cell)) return true;
    }
    return false;
}

bool UndoLast() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_game == nullptr || game->m_worldGeometry == nullptr) return false;
    Map* map = game->m_game;
    WorldGeometry* terrain = game->m_worldGeometry;
    int16_t* heights = terrain->_heights.data;
    const int stride = terrain->_fieldD0;
    if (heights == nullptr || stride <= 0) return false;
    FinishStroke();   // a stroke still being painted counts first
    if (!SameWorld(map, heights, stride)) {
        ForgetWorld();
        return false;
    }
    const int size = map->size;
    const int offset = size >> 1;
    const Stroke* top = g_undo.Top();
    if (top == nullptr) return false;
    if (UndoWouldSkip(map, *top, stride, offset)) {
        g_undoRefused = true;
        std::fprintf(stderr, "[TerrainEdit] undo refused: something was built on this stroke\n");
        NotifyUndoChanged();
        return false;
    }
    g_undoRefused = false;
    g_redoBuilt = false;
    g_redoNote = "";
    Stroke stroke;
    if (!g_undo.Pop(stroke)) return false;
    std::unordered_set<int> cells;   // the cells whose trees and rocks follow the ground
    for (const auto& entry : stroke.heights) {
        const int vx = entry.first % stride;
        const int vy = entry.first / stride;
        if (vy >= stride) continue;
        // something was built here since the stroke
        if (VertexTouchesBlocker(map, vx, vy, offset)) continue;
        heights[entry.first] = entry.second;
        terrain->MarkChunksDirty(vx, vy);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int col = vx - offset + dx;
                const int row = vy - offset + dy;
                if (col >= 0 && row >= 0 && col < size && row < size) cells.insert(row * size + col);
            }
        }
    }
    StillTerrain::Bump();   // heightmap words were written
    if (stroke.hasWater) {
        // The stroke changed the water mesh: the quads it took out come back, what it added is cut off, and
        // the buffers are made again.
        for (const KilledQuad& q : stroke.killedQuads) {
            if (q.position < 0 || q.position + 6 > terrain->_contourIndices.size) continue;
            for (int j = 0; j < 6; ++j) terrain->_contourIndices.data[q.position + j] = q.indices[j];
        }
        terrain->_contourVertices.size = stroke.waterVertices;
        terrain->_contourIndices.size = stroke.waterIndices;
        terrain->_contourRangesB.size = stroke.waterRanges;
        terrain->_contourRangesA.size = stroke.waterRangesA;
        UploadWaterMesh(terrain);
        g_waterQuadsValid = false;
    }
    if (!stroke.texels.empty() && terrain->_heightBytes != nullptr && terrain->_gridSize > 0) {
        const int cellsTotal = terrain->_gridSize * terrain->_gridSize;
        for (const auto& entry : stroke.texels) {
            if (entry.first < 0 || entry.first >= cellsTotal) continue;
            std::memcpy(terrain->_heightBytes + static_cast<size_t>(entry.first) * 4, &entry.second, 4);
        }
        UploadGroundTexture(terrain);
    }
    for (const auto& entry : stroke.types) {
        if (entry.first < 0 || entry.first >= size * size) continue;
        MapCell& undone = map->cells.GetData()[entry.first];
        const auto wrote = stroke.written.find(entry.first);
        // only a cell that is free and still holds the type the stroke gave it: not a road, a field or a
        // house built since
        if (undone.blocker.Get() != nullptr || IsRoadCell(undone) || wrote == stroke.written.end() ||
            static_cast<uint8_t>(undone.terrainType) != wrote->second) {
            continue;
        }
        map->SetTerrainType(entry.first, static_cast<uint32_t>(entry.second));
        cells.insert(entry.first);
    }
    for (const int idx : cells) ReseatOccupant(map, map->cells.GetData()[idx].occupant.Get());
    if (stroke.hasBox) RefreshWaterAccess(map, stroke.boxMinX, stroke.boxMinY, stroke.boxMaxX,
        stroke.boxMaxY);
    // The resources the stroke planted, while they still stand where it put them: one that was chopped,
    // mined or replaced since is left alone.
    size_t removedPlants = 0;
    for (const PlantedResource& p : stroke.planted) {
        if (p.cell < 0 || p.cell >= size * size) continue;
        ComponentEntity* standing = map->cells.GetData()[p.cell].occupant.Get();
        if (standing == nullptr) continue;
        if (p.IsStanding(reinterpret_cast<uintptr_t>(standing), standing->GetGeneration())) {
            RemoveNaturalResource(standing);
            ++removedPlants;
        }
    }
    std::fprintf(stderr, "[TerrainEdit] undo: %zu height(s), %zu terrain type(s), %zu of %zu resource(s)\n",
        stroke.heights.size(), stroke.types.size(), removedPlants, stroke.planted.size());
    // the ground the stroke changed, for the Redo to look at
    for (const auto& entry : stroke.heights) {
        stroke.replay.vertices.push_back(entry.first);
    }
    for (const auto& entry : stroke.types) {
        stroke.replay.cells.push_back(entry.first);
    }
    g_undo.PushRedo(std::move(stroke.replay));
    NotifyUndoChanged();
    return true;
}

size_t RedoDepth() {
    static Recount recount;
    return CountReachable(
        recount, g_undo.RedoDepth(), g_undo.RedoVersion(),
        [](size_t i, Map* map, int stride, int offset) {
            return RedoWouldSkip(map, *g_undo.RedoFromTop(i), stride, offset);
        },
        g_redoBuilt);
}

const char* RedoNote() {
    // The next Redo's ground has something built on it, or why the last Redo did nothing.
    if (g_redoBuilt) return "Cannot redo: something was built on this stroke";
    return g_redoNote;
}

// Selects the resource of the name, as BuildPalette keeps the choice across worlds; false when not listed.
static bool SelectPlantByName(const std::string& name) {
    for (size_t i = 0; i < g_paletteNames.size(); ++i) {
        if (g_paletteNames[i] == name) {
            g_plantIndex = static_cast<int>(i);
            g_plantName = name;
            return true;
        }
    }
    return false;
}

bool RedoLast() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_game == nullptr || game->m_worldGeometry == nullptr) return false;
    Map* map = game->m_game;
    WorldGeometry* terrain = game->m_worldGeometry;
    int16_t* heights = terrain->_heights.data;
    const int stride = terrain->_fieldD0;
    if (heights == nullptr || stride <= 0) return false;
    FinishStroke();   // a stroke still being painted counts first (and ends the redo list)
    if (!SameWorld(map, heights, stride)) {
        ForgetWorld();
        return false;
    }
    if (g_undo.RedoDepth() == 0) return false;
    g_redoNote = "";
    const Replay* next = g_undo.RedoFromTop(0);
    if (next != nullptr && RedoWouldSkip(map, *next, stride, map->size >> 1)) {   // built on since the undo
        g_redoBuilt = true;
        std::fprintf(stderr, "[TerrainEdit] redo refused: something was built on this stroke\n");
        NotifyUndoChanged();
        return false;
    }
    // A planting needs its resource in the list. Looked at before the stroke is taken off the list, as the
    // built-on test is, so a refusal leaves the list as it was.
    EnsurePalette();
    const bool planting = next != nullptr && static_cast<Mode>(next->mode) == Mode::Plant;
    bool listed = false;
    if (planting) {
        for (const std::string& name : g_paletteNames) {
            if (name == next->plant) listed = true;
        }
    }
    if (planting && !listed) {
        g_redoNote = "Cannot redo: that resource is not on the map any more";
        std::fprintf(stderr, "[TerrainEdit] redo refused: %s is not in the resource list\n",
                     next->plant.c_str());
        NotifyUndoChanged();
        return false;
    }
    Replay replay;
    if (!g_undo.PopRedo(replay)) return false;
    const BrushState savedState = g_state;
    const std::string savedPlant = g_plantName;
    if (planting) SelectPlantByName(replay.plant);
    const uint32_t versionBefore = g_undo.Version();
    size_t painted = 0;
    g_replaying = true;
    for (const ReplayFrame& frame : replay.frames) {
        g_state.mode = static_cast<Mode>(replay.mode);
        g_state.radius = frame.radius;
        g_state.strength = frame.strength;
        g_state.density = frame.density;
        TerrainBrushTool::Paint(map, terrain, frame.cell, painted == 0);
        ++painted;
    }
    FinishStroke();
    g_replaying = false;
    g_state = savedState;
    // the palette may have been scanned again (a stroke's start does), so the choice comes back by name
    SelectPlantByName(savedPlant);
    g_undoRefused = false;
    if (g_undo.Version() == versionBefore) {
        // the frames changed nothing (a planting whose disc has grown back to its density): the stroke is gone
        // from the redo list and was not committed, so the player is told
        g_redoNote = "Redo changed nothing: the ground already has what that stroke made";
    }
    std::fprintf(stderr, "[TerrainEdit] redo: %zu frame(s) painted again\n", painted);
    NotifyUndoChanged();
    return true;
}

bool BrushRunning() {
    return g_toolRunning;
}

bool UndoRefused() {
    // A refusal belongs to a stroke of the world it was painted on: after a new game or a load it is gone,
    // and nothing else would clear it, because the page asks this before it asks for the undo depth.
    if (g_undoRefused) {
        GameUpdateLoop* game = GameUpdateLoop::_singleton;
        const bool here = game != nullptr && game->m_game != nullptr && game->m_worldGeometry != nullptr &&
                          SameWorld(game->m_game, game->m_worldGeometry->_heights.data,
                                    game->m_worldGeometry->_fieldD0);
        if (!here) g_undoRefused = false;
    }
    return g_undoRefused;
}

void RescanPlantList() {
    RescanPalette();
}

int PlantCount() {
    EnsurePalette();
    return static_cast<int>(g_palette.size());
}

int PlantIndex() {
    EnsurePalette();
    return g_plantIndex;
}

const char* PlantNameAt(int index) {
    EnsurePalette();
    return index >= 0 && index < static_cast<int>(g_paletteNames.size())
               ? g_paletteNames[static_cast<size_t>(index)].c_str()
               : "";
}

const char* PlantName() {
    return PlantNameAt(PlantIndex());
}

void PlantSet(int index) {
    EnsurePalette();
    if (index < 0 || index >= static_cast<int>(g_palette.size())) return;
    g_plantIndex = index;
    g_plantName = g_paletteNames[static_cast<size_t>(index)];
}

bool BrushActive() {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_toolManager == nullptr) return false;
    Tool* tool = game->m_toolManager->currentTool.Get();
    return tool != nullptr && tool->IsDerivedFrom(TerrainBrushTool::TypeId());
}

}  // namespace TerrainEdit
}  // namespace Ext
