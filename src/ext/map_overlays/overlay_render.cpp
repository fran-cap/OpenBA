// ============================================================================
// src/ext/map_overlays/overlay_render.cpp -- ext.map_overlays: the two pictures (see map_overlays.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §5, §6.
//
// THE MAP WINDOW. MapUI (retail) shows two images of one pixel per tile: the painted terrain on "imageMap"
// and the building footprints on "imageOverlay" (MapUI::SetupImage, 0x14006cb00). While an overlay view
// shows, "imageOverlay" carries OUR sprite: our own ImageBuffer at the image's size, its MaterialInstance and
// SpriteSheet made by retail's own SetupImage (the blend material, the element's click flag and id kept, so
// a click still moves the camera). Retail's Minimap::overlayPixels is never written: it is saved state (the
// resave check byte-compares it). The texture is uploaded only when Image()'s version changes. A Map
// selection, or the window closing, puts retail's sprite back (MapUI::overlaySpriteSheet) before ~MapUI
// deletes its sheets.
//
// THE TERRAIN. Drawn from the ground-draw seam (src/ext/ground_draw.h): inside Renderer::Draw, after the opaque
// list of the main scene and before the blended one, with the scene's camera, so the water, the particles and
// the status icons (pass 1, which write no depth) draw over the picture as over the ground. Drawn the way
// ext.show_grid draws its grid (and the game its tile overlay, retail 0x1401008c0): the Terrain/Selection.rsc
// material, the solid column of its atlas, an identity transform,
// one 0x18-byte vertex stream, with a copy of the overlay's render state that tests depth (so a roof, a
// tree or a hill in front hides the picture) and writes none. The triangles (overlay_render_math.h) cover
// the tiles the screen's nine sample rays hit, on a grid coarse enough to stay under kMaxCells quads; they
// are built again only when the box, the step, the image or the ground under the box changes.
//
// THREADS. With ext.render_pipe the ground draw (DrawOnTerrain) runs on the render thread's pass 2. It never calls
// Image(): RenderFrame (main thread, the end of BeginFrame) copies the image into a snapshot under a mutex
// when its version changes, and the draw reads only the snapshot.
//
// THE DEVICE. A restart (a display change, a lost device) destroys every video External and reloads it by
// path; our ImageBuffer is no External with a file, as the Minimap's are not. Like the Minimap
// (GameUpdateLoop::OnVideoError / Minimap::OnVideoRestart) it is released on the loss (OnVideoLost, from
// MainLoop::_extVideoLost) and recreated and refilled from the retained pixels on the restore
// (OnVideoRestored, from MainLoop::_extVideoRestored). The material and the sheet hold no device object.
// ============================================================================

#include "ext/map_overlays/map_overlays.h"
#include "ext/map_overlays/overlay_render.h"
#include "ext/map_overlays/overlay_render_math.h"

#include "video/ImageBuffer.h"
#include "video/Device.h"
#include "video/Context.h"
#include "video/RenderState.h"
#include "video/VideoProgram.h"
#include "graphics/Material.h"
#include "graphics/Camera.h"
#include "math/Math.h"
#include "volume/Volume.h"
#include "system/Filename.h"
#include "system/String.h"
#include "object/External.h"
#include "ui/Element.h"
#include "ui/ImageElement.h"
#include "ui/Sprite.h"
#include "ui/SpriteSheet.h"
#include "Dialog/MapUI.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/PlayerController.h"
#include "Game/WorldGeometry.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <new>
#include <vector>

namespace Ext {
namespace MapOverlays {

namespace {

// ---- the map window --------------------------------------------------------------------------------------

constexpr const char* kOverlayElement = "imageOverlay";

struct MapPicture {
    Video::ImageBuffer* image = nullptr;
    Graphics::MaterialInstance* material = nullptr;
    UI::SpriteSheet* sheet = nullptr;
    int size = 0;
    uint32_t version = 0;
    bool uploaded = false;
    std::vector<uint32_t> pixels;   // what the texture holds (TexturePixel order): refilled from here after a restart
};

MapPicture& Picture() {
    static MapPicture* picture = new MapPicture();   // leaked on purpose, as show_grid's triangles
    return *picture;
}

UI::ImageElement* OverlayElement(MapUI& map) {
    if (map._container == nullptr) return nullptr;
    System::String name(kOverlayElement);
    UI::Element* element = map._container->FindElement(name).Get();
    if (element == nullptr || !Object::IsDerivedFromNullSafe(element, UI::ImageElement::TypeId())) return nullptr;
    return static_cast<UI::ImageElement*>(element);
}

// `sheet` on `map`'s imageOverlay unless it is already there (the element's sprite is read, not remembered:
// a window closed with the game, or reopened, carries what it carries).
void ShowSheet(MapUI& map, UI::SpriteSheet* sheet) {
    UI::ImageElement* element = OverlayElement(map);
    if (element == nullptr || sheet == nullptr || element->_sprite.spriteSheet == sheet) return;
    UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(sheet), 0);
    element->SetSprite(&sprite);
}

// Retail's sprite back on `map`'s imageOverlay.
void ShowRetail(MapUI& map) {
    ShowSheet(map, map.overlaySpriteSheet);
}

void DestroyPicture() {
    MapPicture& p = Picture();
    delete p.sheet;      // each sheet before the material it draws with, as ~MapUI
    delete p.material;
    delete p.image;
    p.sheet = nullptr;
    p.material = nullptr;
    p.image = nullptr;
    p.size = 0;
    p.uploaded = false;
    p.pixels.clear();
}

// Our ImageBuffer at `size`, the minimap's own parameters (Minimap::CreateAndUpload: 2D, usage 2, format 5,
// no mips, filter 1, clamp). Null when the buffer cannot be made.
Video::ImageBuffer* CreateImage(int size) {
    Video::ImageBuffer* image = new Video::ImageBuffer();   // the class's operator new (MemAlloc)
    if (image == nullptr) return nullptr;
    image->Create(0, 2, 5, 0u, size, size, 1, 1, 1, 1, nullptr);
    return image;
}

void Upload(MapPicture& p) {
    if (p.image == nullptr || p.pixels.empty() || ::Video::Device::_singleton == nullptr) return;
    ::Video::Device::_singleton->Lock();
    p.image->CopyToTexture(p.pixels.data(), p.size, p.size);
    ::Video::Device::_singleton->Unlock();
    p.uploaded = true;
}

// ---- the terrain -------------------------------------------------------------------------------------------

constexpr int kTerrainOpacity = 80;            // percent of the ramp's alpha: the ground stays readable
constexpr float kMaxRayDistance = 400.0f;      // the pick's own limit (tool_base_ray.cpp)
constexpr int kVerticesPerDraw = 6 * 8192;     // one BeginVertices: 1.2 MB of the context's 8 MB stream

// The snapshot RenderFrame takes and the draw reads (THREADS above).
struct Snapshot {
    std::mutex lock;
    std::vector<uint32_t> pixels;
    int size = 0;
    uint32_t version = 0;
    bool valid = false;
};

Snapshot& Snap() {
    static Snapshot* snapshot = new Snapshot();
    return *snapshot;
}

std::atomic<bool> g_terrainShown{false};

// The draw's own state: only one thread draws at a time (the main thread, or the render thread while the
// main thread does not draw).
struct TerrainCache {
    std::vector<TerrainVertex> vertices;
    TileBox box;
    int step = 0;
    int mapSize = 0;
    uint32_t version = 0;
    uint64_t groundHash = 0;
    bool built = false;
    bool reportedFailure = false;
};

TerrainCache& Cache() {
    static TerrainCache* cache = new TerrainCache();
    return *cache;
}

// The ray through the screen point (u, v): ext.show_grid's RayAt (the mouse ray's construction,
// PlayerRay_GetMouseRay_1005a0, with the point given instead of the cursor).
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

// The tile the ray through (u, v) lands on, or -1 when it hits no terrain.
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

// The ground at vertex (vc, vr): the heightmap's int16 / 256, clamped to the heightmap (show_grid's Ground).
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

// A number that changes with the ground under the box, read on the cell grid's vertices only (at most
// kMaxCells-ish reads a frame).
uint64_t GroundHash(const TileBox& box, int step, const Ground& ground) {
    uint64_t hash = 1469598103934665603ull;   // FNV-1a
    for (int r = box.row0; r <= box.row1 + 1; r += step) {
        for (int c = box.col0; c <= box.col1 + 1; c += step) {
            hash ^= static_cast<uint64_t>(static_cast<uint16_t>(ground.Raw(c, r)));
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

// The overlay material's render state with the depth test on and depth writes off: show_grid's GridState
// (the tile overlay's own state shows through everything, which over a whole town would lie on the roofs).
// Made once for the loaded material, again if the material is loaded anew.
const Video::RenderState* TerrainState(const Graphics::MaterialInstance* instance) {
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
    ours = state;   // kept for the life of the process
    source = overlay;
    return ours;
}

void DrawVertices(Video::Context& ctx, const Graphics::Camera& camera, const std::vector<TerrainVertex>& vertices) {
    Video::Context* context = &ctx;
    const Graphics::MaterialInstance* material = nullptr;
    System::Filename name("Terrain/Selection.rsc");
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    const Object::External* object = factory != nullptr ? factory->FindOrLoad(name) : nullptr;
    if (object != nullptr && Object::IsDerivedFromNullSafe(object, Graphics::MaterialInstance::TypeId()))
        material = static_cast<const Graphics::MaterialInstance*>(object);
    if (material == nullptr) return;
    material->Setup(*context, 0);
    const Video::RenderState* state = TerrainState(material);
    if (state != nullptr && context->_renderState != state) {
        context->_dirty |= RtVideo::kDirtyRenderState;
        context->_renderState = state;
    }
    // The scene's camera, as Renderer::DrawItemList sets it for each list (the lists after us set their own).
    const Math::Matrix view = camera._cameraToWorld.AsMatrix();
    context->SetCamera(view, camera._projection);
    Math::Matrix identity = Math::Matrix::Identity();
    context->SetTransform(0, identity);
    const size_t total = vertices.size();
    for (size_t first = 0; first < total; first += kVerticesPerDraw) {
        const size_t count = std::min(total - first, static_cast<size_t>(kVerticesPerDraw));
        if (!context->BeginVertices(static_cast<int32_t>(count), sizeof(TerrainVertex))) {
            TerrainCache& cache = Cache();
            if (!cache.reportedFailure) {
                cache.reportedFailure = true;
                std::fprintf(stderr, "[MapOverlays] the vertex stream refused %zu vertices: no terrain picture\n",
                             count);
            }
            return;
        }
        for (size_t i = first; i < first + count; ++i) context->AddVertex(&vertices[i]);
        context->EndVertices(0);
    }
}

}  // namespace

// ---- the map window --------------------------------------------------------------------------------------

void MapWindowUpdate(MapUI& map) {
    MapPicture& p = Picture();
    int size = 0;
    uint32_t version = 0;
    const std::vector<uint32_t>& image = Image(size, version);
    if (size <= 0 || image.size() < static_cast<size_t>(size) * static_cast<size_t>(size)) {
        ShowRetail(map);
        return;
    }
    if (p.image != nullptr && p.size != size) {
        ShowRetail(map);
        DestroyPicture();
    }
    const bool fresh = p.image == nullptr;
    if (fresh) {
        p.image = CreateImage(size);
        if (p.image == nullptr) return;
        p.size = size;
    }
    if (!p.uploaded || p.version != version) {
        const size_t count = static_cast<size_t>(size) * static_cast<size_t>(size);
        p.pixels.resize(count);
        for (size_t i = 0; i < count; ++i) p.pixels[i] = TexturePixel(image[i]);
        p.version = version;
        Upload(p);
    }
    if (fresh) {
        // Retail's SetupImage builds the material and the sheet over our image and sets the sprite: the
        // blend material, flag 0x10000 and id 1000 on the element as for retail's overlay.
        map.SetupImage(p.image, &p.sheet, &p.material, '', kOverlayElement);
        if (p.sheet == nullptr) DestroyPicture();
        return;
    }
    ShowSheet(map, p.sheet);
}

void MapWindowClosed(MapUI& map) {
    ShowRetail(map);
}

// ---- the terrain -------------------------------------------------------------------------------------------

void RenderFrame() {
    if (!g_on) {
        g_terrainShown.store(false, std::memory_order_relaxed);
        return;
    }
    if (!CurrentSelection().onTerrain) {
        g_terrainShown.store(false, std::memory_order_relaxed);
        return;
    }
    int size = 0;
    uint32_t version = 0;
    const std::vector<uint32_t>& image = Image(size, version);
    if (size <= 0 || image.size() < static_cast<size_t>(size) * static_cast<size_t>(size)) {
        g_terrainShown.store(false, std::memory_order_relaxed);
        return;
    }
    Snapshot& s = Snap();
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if (!s.valid || s.version != version || s.size != size) {
            s.pixels.assign(image.begin(), image.begin() + static_cast<std::ptrdiff_t>(size) * size);
            s.size = size;
            s.version = version;
            s.valid = true;
        }
    }
    g_terrainShown.store(true, std::memory_order_relaxed);
}

void DrawOnTerrain(Video::Context& context, const Graphics::Camera& camera) {
    if (!g_terrainShown.load(std::memory_order_relaxed)) return;
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_game == nullptr || game->m_worldGeometry == nullptr) return;
    const int mapSize = game->m_game->size;
    if (mapSize <= 0) return;
    const WorldGeometry* terrain = game->m_worldGeometry;
    if (terrain->_heights.data == nullptr || terrain->_fieldD0 <= 0) return;

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
    const TileBox box = TileBoxOf(cols, rows, n, mapSize);
    if (!box.Valid()) return;
    const int step = StepFor(box);

    const int base = (terrain->_gridSize - terrain->_fieldC4) >> 1;
    Ground ground;
    ground.base = base;
    ground.dim = terrain->_fieldD0;
    ground.heights = terrain->_heights.data;
    const uint64_t groundHash = GroundHash(box, step, ground) ^ static_cast<uint64_t>(base);

    TerrainCache& cache = Cache();
    Snapshot& s = Snap();
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if (!s.valid) return;
        const bool same = cache.built && SameTileBox(box, cache.box) && step == cache.step &&
                          mapSize == cache.mapSize && s.version == cache.version &&
                          groundHash == cache.groundHash;
        if (!same) {
            const float center = static_cast<float>(terrain->_gridSize >> 1);
            const float origin = static_cast<float>(base) - center;
            BuildTerrainVertices(box, step, mapSize, origin, origin, s.pixels, s.size, kTerrainOpacity, ground,
                                 cache.vertices);
            cache.box = box;
            cache.step = step;
            cache.mapSize = mapSize;
            cache.version = s.version;
            cache.groundHash = groundHash;
            cache.built = true;
        }
    }
    if (cache.vertices.empty()) return;
    DrawVertices(context, camera, cache.vertices);
}

// ---- the device ------------------------------------------------------------------------------------------

void OnVideoLost() {
    MapPicture& p = Picture();
    if (p.image != nullptr) p.image->Release();
}

void OnVideoRestored() {
    MapPicture& p = Picture();
    if (p.image == nullptr) return;
    p.image->Recreate();
    Upload(p);
}

}  // namespace MapOverlays
}  // namespace Ext
