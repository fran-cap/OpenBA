// ============================================================================
// src/ext/show_grid/show_grid.cpp -- ext.show_grid (see show_grid.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/SHOWGRID.md.
//
// Each frame: look along nine rays through the screen (the corners, the edge middles and the centre), march
// each across the terrain as the game's mouse pick does, and take the box of cells they hit. The triangles of
// the box's tile edges (show_grid_math.h) are built again only when the box or the player's style changes,
// and are drawn the way the game's tile overlay is (retail 0x1401008c0): its material, Terrain/Selection.rsc,
// an identity transform, one 0x18-byte-stride vertex stream. A ray that hits nothing (a view that reaches the
// sky) adds no cell.

#include "ext/show_grid/show_grid.h"
#include "ext/show_grid/show_grid_math.h"
#include "ext/registry.h"

#include "video/Context.h"
#include "video/RenderState.h"
#include "video/VideoProgram.h"
#include "graphics/Material.h"
#include "graphics/Camera.h"
#include "math/Math.h"
#include "volume/Volume.h"
#include "system/Filename.h"
#include "object/External.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/PlayerController.h"
#include "Game/WorldGeometry.h"

#include <cstdio>
#include <vector>

namespace Ext {
namespace ShowGrid {

namespace {

constexpr float kMaxRayDistance = 400.0f;   // the pick's own limit (tool_base_ray.cpp)

// The ray through the screen point (u, v), both in [0, 1): the mouse ray's construction
// (PlayerRay_GetMouseRay_1005a0) with the point given instead of the cursor.
void RayAt(const Graphics::Camera& camera, float u, float v, float* origin, float* dir) {
    Math::Transform3 xform = camera._cameraToWorld;
    ::Frustum frustum(camera.GetFrustum());
    Math::Vector3 zero((Math::Scalar(0.0f)));
    const Math::Vector3 o = xform.operator*(zero);
    origin[0] = o.x;
    origin[1] = o.y;
    origin[2] = o.z;
    Math::Vector3 screenDir;
    Math::Vector3 dirOut;
    screenDir = *RtVolume_Frustum_GetScreenDirection(&frustum, &dirOut, u, v);
    const Math::Vector3 d = xform.Rotate(screenDir);
    dir[0] = d.x;
    dir[1] = d.y;
    dir[2] = d.z;
}

// The cell the ray through (u, v) lands on, or -1 when it hits no terrain.
int CellAt(const Graphics::Camera& camera, WorldGeometry* terrain, int mapSize, float u, float v) {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    float dir[3] = {0.0f, 0.0f, 0.0f};
    RayAt(camera, u, v, origin, dir);
    float maxDist = kMaxRayDistance;
    float hit[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float scratch[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    if (terrain->TerrainMarch(origin, dir, &maxDist, hit, scratch) == 0) return -1;
    const int col = static_cast<int>(hit[0] + static_cast<float>(mapSize >> 1));
    const int row = static_cast<int>(hit[1] + static_cast<float>(mapSize >> 1));
    if (col < 0 || row < 0 || col >= mapSize || row >= mapSize) return -1;
    return mapSize * row + col;
}

// The triangles are rebuilt only when the box or the style changes. Leaked on purpose: the draw may still run
// on the render thread while statics are destroyed at exit.
std::vector<LineVertex>& Triangles() {
    static std::vector<LineVertex>* triangles = new std::vector<LineVertex>();
    return *triangles;
}

Box g_box;
int g_mapSize = 0;
LineStyle g_style;
bool g_built = false;
uint64_t g_groundHash = 0;
bool g_reportedFailure = false;

// The ground height at vertex (vc, vr) of the tile grid: the heightmap's int16 / 256, as the overlay renderer
// samples it (PlayerTileRender_TerrainCorners_b10e0).
struct Ground {
    int base;
    int dim;
    const int16_t* heights;

    int16_t Raw(int vc, int vr) const {
        int c = base + vc;
        int r = base + vr;
        if (c < 0) c = 0;
        if (c >= dim) c = dim - 1;
        if (r < 0) r = 0;
        if (r >= dim) r = dim - 1;
        return heights[static_cast<int64_t>(dim) * r + c];
    }

    float operator()(int vc, int vr) const {
        return static_cast<float>(Raw(vc, vr)) * (1.0f / 256.0f);
    }
};

// The tile overlay's render state lets its quads show through everything (the placement tiles are meant to be
// seen under a building ghost): with it the grid's lines would lie over roofs and trees. The grid's own state
// is a copy of it with the depth test on and depth writes off, so a building, a tree or a cliff in front of a
// line hides it. Made once for the loaded overlay material (and again if the material is loaded anew). Null
// when the material has no state to copy.
const Video::RenderState* GridState(const Graphics::MaterialInstance* instance) {
    static const Video::RenderState* source = nullptr;
    static Video::RenderState* ours = nullptr;
    const Graphics::Material* material = instance->_material.Get();
    if (material == nullptr || material->_passes._size <= 0) return nullptr;
    const Video::VideoProgram* pass0 = material->_passes.GetData()[0].Get();
    if (pass0 == nullptr) return nullptr;
    const Video::RenderState* overlay = pass0->_renderState.Get();
    if (overlay == nullptr) return nullptr;
    if (overlay == source && ours != nullptr) return ours;
    Video::RenderState* state = static_cast<Video::RenderState*>(Video::RenderState::CreateNewObject());
    if (state == nullptr) return nullptr;
    Video::RenderState::Depth depth = overlay->_depth;
    depth.test = true;
    depth.write = false;
    depth.func = 4;   // the engine's default: less or equal
    state->Initialize(overlay->_alphaBlend, depth, overlay->_stencil, overlay->_cullMode, overlay->_fillMode,
                      overlay->_flag70);
    ours = state;   // kept for the life of the process, like the other leaked statics
    source = overlay;
    return ours;
}

void DrawTriangles(Video::Context* context, const std::vector<LineVertex>& triangles) {
    const Graphics::MaterialInstance* material = nullptr;
    System::Filename name("Terrain/Selection.rsc");
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    const Object::External* object = factory != nullptr ? factory->FindOrLoad(name) : nullptr;
    if (object != nullptr && Object::IsDerivedFromNullSafe(object, Graphics::MaterialInstance::TypeId()))
        material = static_cast<const Graphics::MaterialInstance*>(object);
    if (material == nullptr) return;
    material->Setup(*context, 0);
    const Video::RenderState* gridState = GridState(material);
    if (gridState != nullptr && context->_renderState != gridState) {
        context->_dirty |= RtVideo::kDirtyRenderState;
        context->_renderState = gridState;
    }
    Math::Matrix identity = Math::Matrix::Identity();
    context->SetTransform(0, identity);
    if (!context->BeginVertices(static_cast<int32_t>(triangles.size()), sizeof(LineVertex))) {
        if (!g_reportedFailure) {
            g_reportedFailure = true;
            std::fprintf(stderr, "[ShowGrid] the vertex stream refused %zu vertices: no grid\n",
                         triangles.size());
        }
        return;
    }
    for (const LineVertex& v : triangles) context->AddVertex(&v);
    context->EndVertices(0);
}

}  // namespace

namespace {
bool g_shown = false;   // runtime only: no value key, nothing in settings.cfg or a save
}

bool Shown() { return g_shown; }

void SetShown(bool shown) { g_shown = shown; }

void OnGameStart() { g_shown = false; }

void Draw(Video::Context* context) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_game == nullptr || game->m_worldGeometry == nullptr) return;
    if (game->m_playerController == nullptr) return;
    const int mapSize = game->m_game->size;
    if (mapSize <= 0) return;
    const WorldGeometry* terrain = game->m_worldGeometry;
    if (terrain->_heights.data == nullptr || terrain->_fieldD0 <= 0) return;
    const Graphics::Camera& camera = game->m_playerController->camera;

    static const float kPoints[][2] = {
        {0.001f, 0.001f}, {0.999f, 0.001f}, {0.001f, 0.999f}, {0.999f, 0.999f}, {0.5f, 0.001f},
        {0.5f, 0.999f},   {0.001f, 0.5f},   {0.999f, 0.5f},   {0.5f, 0.5f},
    };
    constexpr int kCount = static_cast<int>(sizeof(kPoints) / sizeof(kPoints[0]));
    int cols[kCount];
    int rows[kCount];
    int n = 0;
    for (int i = 0; i < kCount; ++i) {
        const int cell = CellAt(camera, game->m_worldGeometry, mapSize, kPoints[i][0], kPoints[i][1]);
        if (cell < 0) continue;
        cols[n] = cell % mapSize;
        rows[n] = cell / mapSize;
        ++n;
    }
    const Box box = BoxOf(cols, rows, n, mapSize);
    if (!box.Valid()) return;

    LineStyle style;
    style.opacity = Ext::Val().show_grid_opacity;
    style.shade = Ext::Val().show_grid_shade;
    style.width = Ext::Val().show_grid_width;
    style.hue = Ext::Val().show_grid_hue;
    style.saturation = Ext::Val().show_grid_saturation;
    const int base = (terrain->_gridSize - terrain->_fieldC4) >> 1;
    Ground ground;
    ground.base = base;
    ground.dim = terrain->_fieldD0;
    ground.heights = terrain->_heights.data;
    // One read of each vertex under the box a frame (a few thousand at most): a loaded town or edited terrain
    // changes the hash, and the lines follow the new ground.
    const auto raw = [&ground](int vc, int vr) {
        return ground.Raw(vc, vr);
    };
    const uint64_t groundHash = HeightHash(box, raw) ^ static_cast<uint64_t>(base);
    const bool same = g_built && SameBox(box, g_box) && mapSize == g_mapSize && style == g_style &&
                      groundHash == g_groundHash;
    if (!same) {
        const float center = static_cast<float>(terrain->_gridSize >> 1);
        const float origin = static_cast<float>(base) - center;
        Triangles() = BuildLines(box, origin, origin, style, ground);
        g_box = box;
        g_mapSize = mapSize;
        g_style = style;
        g_groundHash = groundHash;
        g_built = true;
    }
    if (Triangles().empty()) return;
    DrawTriangles(context, Triangles());
}

}  // namespace ShowGrid
}  // namespace Ext
