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
// the cells the camera's frustum sees between the map's lowest and highest ground, on a grid coarse enough to
// stay under kMaxCells quads; they are built again only when those cells, the step, the image or the ground
// changes.
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
#include "Game/WorldGeometry.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <utility>
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
    CellRows rows;
    CoarseGrid grid;
    int step = 0;
    int mapSize = 0;
    uint32_t version = 0;
    uint64_t groundHash = 0;
    bool built = false;
    bool reportedFailure = false;
    // the heightmap's summary (GroundOf)
    const int16_t* heights = nullptr;
    int heightsDim = 0;
    uint64_t heightsHash = 0;
    float groundLow = 0.0f;
    float groundHigh = 0.0f;
};

TerrainCache& Cache() {
    static TerrainCache* cache = new TerrainCache();
    return *cache;
}

// The camera's frustum corners (Volume::Frustum::GetCorners' order) in tile coordinates: world minus the world
// position of tile vertex 0.
void FrustumCorners(const Graphics::Camera& camera, float originX, float originY, Point3 (&out)[8]) {
    const ::Frustum frustum(camera.GetFrustum());
    Math::Vector3 local[8];
    RtVolume_Frustum_GetCorners(&frustum, local);
    const Math::Transform3& xform = camera._cameraToWorld;
    for (int i = 0; i < 8; ++i) {
        const Math::Vector3 w = xform.operator*(local[i]);
        out[i].x = w.x - originX;
        out[i].y = w.y - originY;
        out[i].z = w.z;
    }
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

// A number that changes with any height of the heightmap (four FNV-style lanes over its 64-bit words, so the
// multiplies do not wait on one another: a few hundred microseconds a frame on the largest map at most).
uint64_t HeightsHash(const int16_t* heights, size_t count) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(heights);
    const size_t size = count * sizeof(int16_t);
    uint64_t lane[4] = {1469598103934665603ull, 1099511628211ull, 7809847782465536322ull, 14695981039346656037ull};
    size_t at = 0;
    for (; at + 32 <= size; at += 32) {
        for (int k = 0; k < 4; ++k) {
            uint64_t word = 0;
            std::memcpy(&word, bytes + at + 8 * k, sizeof(word));
            lane[k] = (lane[k] ^ word) * 1099511628211ull;
        }
    }
    for (; at < size; ++at) lane[0] = (lane[0] ^ bytes[at]) * 1099511628211ull;
    return lane[0] ^ (lane[1] * 31u) ^ (lane[2] * 961u) ^ (lane[3] * 29791u);
}

// The heightmap's hash and its lowest and highest ground, worked out again only when the hash changes.
void GroundOf(const WorldGeometry& terrain, TerrainCache& cache) {
    const int dim = terrain._fieldD0;
    const size_t count = static_cast<size_t>(dim) * static_cast<size_t>(dim);
    const uint64_t hash = HeightsHash(terrain._heights.data, count);
    if (cache.heights == terrain._heights.data && cache.heightsDim == dim && cache.heightsHash == hash) return;
    cache.heights = terrain._heights.data;
    cache.heightsDim = dim;
    cache.heightsHash = hash;
    int16_t low = terrain._heights.data[0];
    int16_t high = low;
    for (size_t i = 1; i < count; ++i) {
        low = std::min(low, terrain._heights.data[i]);
        high = std::max(high, terrain._heights.data[i]);
    }
    cache.groundLow = static_cast<float>(low) * (1.0f / 256.0f);
    cache.groundHigh = static_cast<float>(high) * (1.0f / 256.0f);
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
        // The texture is upside down against the tiles (MinimapTextureIndex): rows are flipped here.
        for (int row = 0; row < size; ++row) {
            const uint32_t* src = image.data() + static_cast<size_t>(row) * static_cast<size_t>(size);
            uint32_t* dst = p.pixels.data() + MinimapTextureIndex(0, row, size);
            for (int col = 0; col < size; ++col) {
                dst[col] = TexturePixel(src[col]);
            }
        }
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

    const int base = (terrain->_gridSize - terrain->_fieldC4) >> 1;
    const float center = static_cast<float>(terrain->_gridSize >> 1);
    const float origin = static_cast<float>(base) - center;
    Ground ground;
    ground.base = base;
    ground.dim = terrain->_fieldD0;
    ground.heights = terrain->_heights.data;

    TerrainCache& cache = Cache();
    GroundOf(*terrain, cache);

    // what the screen sees of the ground (overlay_render_math.h), and the cells it overlaps
    Point3 corners[8];
    FrustumCorners(camera, origin, origin, corners);
    const Footprint footprint = FootprintOf(corners, cache.groundLow, cache.groundHigh + kTerrainLift);
    if (!footprint.Valid()) return;
    const int step = StepFor(footprint, mapSize, cache.step);
    CellRows rows;
    CellRowsOf(footprint, mapSize, step, kTerrainMargin, rows);
    const uint64_t groundHash = cache.heightsHash ^ static_cast<uint64_t>(base);

    Snapshot& s = Snap();
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if (!s.valid) return;
        if (step > 1) {
            if (cache.grid.Step() != step || cache.grid.MapSize() != mapSize) {
                cache.grid.Reset(step, mapSize);
            } else {
                if (s.version != cache.version) cache.grid.InvalidateColors();
                if (groundHash != cache.groundHash) cache.grid.InvalidateHeights();
            }
        }
        const bool same = cache.built && step == cache.step && mapSize == cache.mapSize &&
                          s.version == cache.version && groundHash == cache.groundHash &&
                          SameCellRows(rows, cache.rows);
        if (!same) {
            BuildTerrainVertices(rows, mapSize, origin, origin, s.pixels, s.size, kTerrainOpacity, ground,
                                 cache.grid, cache.vertices);
            cache.rows = std::move(rows);
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
