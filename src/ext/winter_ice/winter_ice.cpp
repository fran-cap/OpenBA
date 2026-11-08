// ============================================================================
// src/ext/winter_ice/winter_ice.cpp -- ext.winter_ice: the lake ice pass (docs/extensions/handins/WINTERICE.md)
// ============================================================================
// NOT RETAIL. winter_ice.h has the design.
//
// THE PROGRAM. As ext.better_water does (better_water.cpp), the pass draws through a "twin" of the
// game's River / Lake VideoProgram: the same vertex layout, vertex program and render state, our pixel program
// in place of the water's, all made through the engine's own Video classes, so every backend creates
// and binds it as it does a program read from a package. A twin remembers what it was made from; a
// world load or a device restart makes new water programs, nothing matches, and a new twin is made.
//
// THE TEXTURES are ours and live for the process: the ice surface and detail (ice_texture_data.h,
// a full mip chain built here, wrapped, anisotropic) and the open-water mask (one texel per map cell,
// remade when the set of blocked water cells changes). Made on the main thread; a remade mask's old
// image is kept a few generations (a backend may still hold it in cached state) and then deleted.
// Each keeps its levels, so a device restart makes it again (OnVideoRestored).
// ============================================================================

#include "ext/winter_ice/winter_ice.h"
#include "ext/winter_ice/ice_model.h"
#include "ext/winter_ice/ice_town.h"
#include "ext/winter_ice/walk_cells.h"
#include "ext/winter_ice/walk_type.h"
#include "ext/winter_ice/open_mask.h"
#include "ext/boat_lane/boat_lane.h"
#include "ext/winter_ice/ice_texture_data.h"

#include "ext/registry.h"
#if BANISHED_TWEAK_OVERLAY
#include "ext/tweak/ext_tweaks.h"   // the debug overlay's live look and preview (Alt+F10, "Winter ice")
#endif

#include "winter_ice/ice_overlay_ps.h"   // src/render/common: g_wi_PSIceOverlay (tools/water_shaders.py)

#include "Game/GameUpdateLoop.h"
#include "Game/Calendar.h"
#include "Game/Map.h"
#include "Game/PathfinderThread.h"   // ext.walk_on_ice: SetCellTerrain
#include "Game/WeatherSimulation.h"
#include "Game/WorldGeometry.h"       // _contourOutlines: the trade boats' polyline
#include "Entity/ComponentEntity.h"   // MapCell::blocker.Get()
#include "Component/TradeComponent.h" // FindComponent<TradeComponent>: a trading post's cells
#include "Component/InteractComponent.h"   // the dock's "boat" attach point
#include "Component/BoatComponent.h"       // BoatRoute_FindAttachPoint
#include "system/String.h"

#include "video/Context.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"
#include "video/ShaderProgram.h"
#include "video/VideoProgram.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

namespace Ext {
namespace WinterIce {

namespace {

// ---- the textures (main thread) ----------------------------------------------------------------

constexpr int32_t kFormatBGRA8 = 5;   // ImageBuffer format 5 = DXGI B8G8R8A8_UNORM
constexpr int32_t kUsageDefault = 2;
constexpr int32_t kWrap = 0;
constexpr int32_t kClamp = 1;
constexpr int32_t kLinear = 1;
constexpr int32_t kAnisotropic = 2;

// One of our textures: the image and the levels it was made from (kept for a device restart).
struct Texture {
    Video::ImageBuffer* image = nullptr;
    int32_t size = 0;
    uint32_t flags = 0;
    int32_t filter = kLinear;
    int32_t address = kClamp;
    std::vector<std::vector<uint8_t>> levels;   // BGRA8, level 0 first
};

// Straight RGBA8 -> BGRA8 level 0, then each next level the 2x2 box mean of the one above.
std::vector<std::vector<uint8_t>> MipChain(const uint8_t* rgba, int32_t size) {
    std::vector<std::vector<uint8_t>> levels;
    std::vector<uint8_t> top(static_cast<size_t>(size) * static_cast<size_t>(size) * 4u);
    for (size_t i = 0; i < top.size(); i += 4) {
        top[i + 0] = rgba[i + 2];
        top[i + 1] = rgba[i + 1];
        top[i + 2] = rgba[i + 0];
        top[i + 3] = rgba[i + 3];
    }
    levels.push_back(std::move(top));
    int32_t s = size;
    while (s > 1) {
        const int32_t n = s / 2;
        const std::vector<uint8_t>& up = levels.back();
        std::vector<uint8_t> down(static_cast<size_t>(n) * static_cast<size_t>(n) * 4u);
        for (int32_t y = 0; y < n; ++y) {
            for (int32_t x = 0; x < n; ++x) {
                for (int32_t c = 0; c < 4; ++c) {
                    const size_t r0 = (static_cast<size_t>(2 * y) * s + 2 * x) * 4 + c;
                    const size_t r1 = (static_cast<size_t>(2 * y + 1) * s + 2 * x) * 4 + c;
                    const int sum = up[r0] + up[r0 + 4] + up[r1] + up[r1 + 4];
                    down[(static_cast<size_t>(y) * n + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
                }
            }
        }
        levels.push_back(std::move(down));
        s = n;
    }
    return levels;
}

// The GPU texture from the kept levels, through the engine's own Create (what a package load runs).
bool CreateOnDevice(Texture& t) {
    Video::ImageBuffer::LevelArray levels;
    levels.SetSize(static_cast<int>(t.levels.size()));
    for (size_t i = 0; i < t.levels.size(); ++i) {
        Collection::Array<uint8_t>& level = levels[static_cast<int>(i)];
        level.SetSize(static_cast<int>(t.levels[i].size()));
        std::memcpy(level.GetData(), t.levels[i].data(), t.levels[i].size());
    }
    return t.image->Create(0, kUsageDefault, kFormatBGRA8, t.flags, t.size, t.size, t.filter, t.address,
                           t.address, t.address, &levels);
}

bool MakeTexture(Texture& t, std::vector<std::vector<uint8_t>> levels, int32_t size, uint32_t flags,
                 int32_t filter, int32_t address, const char* what) {
    t.levels = std::move(levels);
    t.size = size;
    t.flags = flags;
    t.filter = filter;
    t.address = address;
    t.image = new Video::ImageBuffer();   // the class's operator new (MemAlloc)
    if (t.image == nullptr || !CreateOnDevice(t)) {
        std::fprintf(stderr, "[WinterIce] the device refused the %s texture (%d x %d): no ice\n", what, size, size);
        delete t.image;
        t.image = nullptr;
        return false;
    }
    return true;
}

Texture g_surface;
Texture g_detail;
bool g_texturesTried = false;

// The open-water mask, as the draw reads it: published whole through one pointer.
struct Mask {
    Texture texture;
    int32_t size = 0;
};
std::atomic<Mask*> g_mask{nullptr};
std::deque<Mask*> g_oldMasks;   // the last few replaced masks, oldest first (main thread)
constexpr size_t kKeepOldMasks = 4;
std::vector<int> g_blocked;     // the blocked water cells the current mask was built from
std::vector<const void*> g_posts;   // the trading posts the current mask's channel was built for
ChannelShape g_channelShape{-1.0f, -1.0f, -1.0f};   // ...and the channel's shape
int32_t g_blockedSize = -1;
int g_scanCountdown = 0;
constexpr int kScanFrames = 60;   // the cells are scanned once a second or so

void RetireMask(Mask* m) {
    if (m == nullptr) return;
    g_oldMasks.push_back(m);
    while (g_oldMasks.size() > kKeepOldMasks) {
        Mask* old = g_oldMasks.front();
        g_oldMasks.pop_front();
        delete old->texture.image;
        delete old;
    }
}

bool IsWater(PathType terrainType) {
    return terrainType == PathType::Water || terrainType == PathType::DeepWater;
}

// The blocked water cells of the map; a new mask when they changed (or the map's size did).
// The in-map stretch of the trade boats' polyline (WorldGeometry::_contourOutlines record 0, world
// coordinates; BoatComponent::SetDestination 0x1400456e0 sails it in, past the dock and out), as cell
// coordinates; empty when the world has none.
std::vector<float> BoatLine(const WorldGeometry* geo, int32_t size) {
    std::vector<float> xy;
    if (geo == nullptr || geo->_contourOutlines.data == nullptr || geo->_contourOutlines.size <= 0) return xy;
    const RawArray<Math::Vector2>& line = geo->_contourOutlines.data[0];
    const float half = static_cast<float>(size >> 1);
    const float edge = static_cast<float>(size);
    auto inMap = [&](int i) {
        const float x = line.data[i].x + half;
        const float y = line.data[i].y + half;
        return x >= 0.0f && y >= 0.0f && x < edge && y < edge;
    };
    int first = 0;
    while (first < line.size && !inMap(first)) ++first;
    if (first == line.size) return xy;
    // From the last point before the map (the channel reaches the edge) to the first one back outside.
    int i = first > 0 ? first - 1 : first;
    for (; i < line.size; ++i) {
        xy.push_back(line.data[i].x + half);
        xy.push_back(line.data[i].y + half);
        if (i > first && !inMap(i)) break;
    }
    return xy;
}

// The boat channel's shape: the defaults, or the debug overlay's (Alt+F10).
ChannelShape CurrentChannel() {
#if BANISHED_TWEAK_OVERLAY
    return Ext::Tweak::IceValues().channel;
#else
    return ChannelShape{};
#endif
}

// The open-water edge's noise: the defaults, or the debug overlay's.
IceEdge CurrentEdge() {
#if BANISHED_TWEAK_OVERLAY
    return Ext::Tweak::IceValues().edge;
#else
    return IceEdge{};
#endif
}

// The half width of the narrow cut along the boats' own legs, cells (about a hull).
constexpr float kBoatCut = 0.75f;

// A trading post on the map.
struct TradePost {
    ComponentEntity* post;
};

// The terrain height at a cell's centre, the mean of its four heightmap corners (the heightmap is the
// world's, centred: WorldGeometry::BuildWaterShores' sampleH). A bridge writes a land type into its
// cells; the height still says they lie in the water.
float CellHeight(const WorldGeometry& geo, int index, int32_t size) {
    const int16_t* heights = geo._heights.data;
    const int n = geo._heightRowStride;
    if (heights == nullptr || n <= 0) return 0.0f;
    const int half = geo._gridSize >> 1;
    const int col = index % size - (size >> 1) + half;
    const int row = index / size - (size >> 1) + half;
    auto at = [&](int c, int r) {
        if (c < 0) c = 0;
        if (c >= n) c = n - 1;
        if (r < 0) r = 0;
        if (r >= n) r = n - 1;
        return static_cast<float>(heights[static_cast<size_t>(r) * static_cast<size_t>(n) + static_cast<size_t>(c)]);
    };
    return (at(col, row) + at(col + 1, row) + at(col, row + 1) + at(col + 1, row + 1)) * (0.25f / 256.0f);
}

// Under the water by Map::ClassifyTerrain's own threshold (0x14009e2c0: below -0.25 is shore or deep).
constexpr float kUnderwater = -0.25f;

// The boats' way for one trading post, as retail builds it (BoatComponent::SetDestination 0x1400456e0):
// the polyline's first point in the map -> the dock's "boat" attach point -> the polyline's last point in
// the map, the two legs through the water (ext/boat_lane/boat_lane.h). When either leg finds no water,
// retail keeps the polyline alone, and so does the channel. True when the legs were found.
//
// With ext.boat_lane on (class B: the town's locked value) the boats sail the lane, so the channel is
// that lane exactly (BoatLane's fixed shape; the overlay's pull and smoothing do not apply). Off, the
// boats sail the game's legs: the lane is the look (the overlay's shape) and a narrow cut follows the
// game's legs, so a boat is never seen on the ice.
bool AddPostChannel(ComponentEntity* post, const Map& map, const std::vector<float>& line,
                    const ChannelShape& shape, std::vector<OpenStamp>& stamps) {
    const bool boatsOnLane = Ext::Ext().boat_lane;
    const float halfWidth = shape.halfWidth;
    const float pull = boatsOnLane ? BoatLane::kLanePull : shape.centrePull;
    const int smoothing = boatsOnLane ? BoatLane::kLaneSmoothing : static_cast<int>(shape.smoothing + 0.5f);
    // The polyline alone, as the boats sail it when the legs fail: as it is under ext.boat_lane (the
    // boats take its points), smoothed for the look otherwise.
    auto polylineOnly = [&]() {
        AddChannel(boatsOnLane ? line : BoatLane::SmoothLine(line, smoothing), stamps, halfWidth);
        return false;
    };
    const int32_t size = map.size;
    const float half = static_cast<float>(size >> 1);
    float dock[3] = {0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    const System::String boat("boat");
    InteractComponent* interact = post->FindComponent<InteractComponent>();
    const bool haveDock = interact != nullptr && BoatRoute_FindAttachPoint(interact, &boat, dock, &yaw);
    // The polyline's first and last points inside the map (BoatLine keeps one point either side).
    if (!haveDock || line.size() < 6) return polylineOnly();
    // (int)(coord) clamped: retail's ToTileClamped on cell coordinates (world + the half edge).
    auto tile = [&](float x, float y) {
        int cx = static_cast<int>(x);
        int cy = static_cast<int>(y);
        cx = cx < 0 ? 0 : (cx >= size ? size - 1 : cx);
        cy = cy < 0 ? 0 : (cy >= size ? size - 1 : cy);
        return cy * size + cx;
    };
    const size_t last = line.size() - 4;   // the last in-map point (the final one lies outside)
    const int entry = tile(line[2], line[3]);
    const int exit = tile(line[last], line[last + 1]);
    const int dockCell = tile(dock[0] + half, dock[1] + half);
    std::vector<uint8_t> types(static_cast<size_t>(size) * static_cast<size_t>(size));
    const MapCell* cells = map.cells.GetData();
    for (size_t i = 0; i < types.size(); ++i) types[i] = static_cast<uint8_t>(cells[i].terrainType);
    BoatLane::LaneLegs legs;
    if (!BoatLane::Legs(types.data(), size, entry, dockCell, exit, pull, smoothing, legs)) return polylineOnly();
    if (!boatsOnLane) {
        AddChannel(BoatLane::CellPathLine(BoatLane::BoatWaterPath(types.data(), size, entry, dockCell), size), stamps,
                   kBoatCut);
        AddChannel(BoatLane::CellPathLine(BoatLane::BoatWaterPath(types.data(), size, dockCell, exit), size), stamps,
                   kBoatCut);
    }
    // In from the polyline's last point before the map (the boats skip its first point inside), and out
    // to its first point beyond the map.
    std::vector<float> legIn = {line[0], line[1]};
    legIn.insert(legIn.end(), legs.in.begin(), legs.in.end());
    std::vector<float> legOut = legs.out;
    legOut.push_back(line[last + 2]);
    legOut.push_back(line[last + 3]);
    AddChannel(legIn, stamps, halfWidth);
    AddChannel(legOut, stamps, halfWidth);
    return true;
}

// The water cells under a building and the trading posts of the map. `map` covers size * size cells.
void CollectBuildings(const Map& map, const WorldGeometry& geo, std::vector<int>& blocked,
                      std::vector<TradePost>& posts) {
    const int32_t size = map.size;
    const MapCell* cells = map.cells.GetData();
    const ComponentEntity* lastEntity = nullptr;
    bool lastIsPost = false;
    for (int i = 0; i < size * size; ++i) {
        // Buildings write themselves into a cell's occupant (their footprint); the blocker is the other
        // half of the pair. Either one on a water cell is a building standing in the water: a dock's own
        // water cells keep their type, a bridge's are land typed but lie under the waterline.
        ComponentEntity* building = cells[i].occupant.Get();
        if (building == nullptr) building = cells[i].blocker.Get();
        if (building == nullptr) continue;
        if (IsWater(cells[i].terrainType) || CellHeight(geo, i, size) < kUnderwater) blocked.push_back(i);
        if (building != lastEntity) {
            lastEntity = building;
            lastIsPost = building->FindComponent<TradeComponent>() != nullptr;
            if (lastIsPost) {
                bool seen = false;
                for (const TradePost& post : posts) seen = seen || post.post == building;
                if (!seen) posts.push_back(TradePost{building});
            }
        }
    }
}

// The open-water mask's stamps for these buildings: a disc round every blocked water cell, and while there
// is a trading post the boats' channel of `shape`.
std::vector<OpenStamp> OpenStamps(const Map& map, const WorldGeometry* geo, const std::vector<int>& blocked,
                                  const std::vector<TradePost>& posts, const ChannelShape& shape,
                                  int* channelPoints, int* waterLegs) {
    std::vector<OpenStamp> stamps;
    for (const int index : blocked) stamps.push_back(BuildingStamp(index, map.size));
    if (!posts.empty()) {
        const std::vector<float> line = BoatLine(geo, map.size);
        if (channelPoints != nullptr) *channelPoints = static_cast<int>(line.size() / 2);
        for (const TradePost& post : posts) {
            if (AddPostChannel(post.post, map, line, shape, stamps) && waterLegs != nullptr) ++*waterLegs;
        }
    }
    return stamps;
}

// The drawing's open-water mask: a new one when the blocked cells or the trading posts changed (or the
// map's size, or the channel's shape).
void ScanMap(const Map& map, const WorldGeometry* geo) {
    const int32_t size = map.size;
    if (size <= 0 || map.cells.GetSize() < size * size || geo == nullptr) return;
    std::vector<int> blocked;
    std::vector<TradePost> posts;
    CollectBuildings(map, *geo, blocked, posts);
    std::vector<const void*> postKey;
    for (const TradePost& post : posts) postKey.push_back(post.post);
    const ChannelShape shape = CurrentChannel();
    if (size == g_blockedSize && blocked == g_blocked && postKey == g_posts && shape == g_channelShape &&
        g_mask.load(std::memory_order_relaxed) != nullptr)
        return;

    int channelPoints = 0;
    int waterLegs = 0;   // trading posts whose channel follows the two water legs (the rest: the polyline)
    const std::vector<OpenStamp> stamps = OpenStamps(map, geo, blocked, posts, shape, &channelPoints, &waterLegs);

    std::vector<uint8_t> open;
    BuildOpenMask(stamps, size, open);
    std::vector<std::vector<uint8_t>> levels(1);
    levels[0].resize(open.size() * 4u);
    for (size_t i = 0; i < open.size(); ++i) {
        levels[0][i * 4 + 0] = open[i];
        levels[0][i * 4 + 1] = open[i];
        levels[0][i * 4 + 2] = open[i];
        levels[0][i * 4 + 3] = 255;
    }
    Mask* m = new Mask();
    m->size = size;
    if (!MakeTexture(m->texture, std::move(levels), size, 0u, kLinear, kClamp, "open-water mask")) {
        delete m;
        return;
    }
    g_blocked = std::move(blocked);
    g_posts = std::move(postKey);
    g_channelShape = shape;
    g_blockedSize = size;
    RetireMask(g_mask.exchange(m, std::memory_order_acq_rel));
    std::fprintf(stderr,
                 "[WinterIce] open-water mask: %d x %d, %d blocked water cell(s), %d trading post(s), "
                 "boat channel %d polyline point(s), %d by the water legs\n",
                 size, size, static_cast<int>(g_blocked.size()), static_cast<int>(g_posts.size()), channelPoints,
                 waterLegs);
}

// ---- the ice (main thread) -------------------------------------------------------------------------

bool g_on = false;
// The fronts themselves are Town() (ice_town.h), shared with ext.walk_on_ice and its save record.
double g_lastSeconds = 0.0;             // the calendar, game seconds since year 0, at the last tick
const void* g_scanWeather = nullptr;    // the town the open-water mask was last scanned for

// The calendar as game seconds since year 0 (pause and game speed honoured).
double CalendarSeconds(const Calendar& cal) {
    return static_cast<double>(cal.year) * cal.yearLength.AsSeconds() + cal.yearTime.AsSeconds();
}

// ---- ext.walk_on_ice (main thread; WINTERICE.md 5.1 / 5.2) ------------------------------------------

// The triangles of a set of contour ranges (WorldGeometry::_contourRangesA / B) in cell coordinates: a
// vertex's position is (col - size/2, row - size/2) times 48 (WorldGeometryElements.h TerrainVertex).
template <typename Ranges>
std::vector<float> RangeTriangles(const WorldGeometry& geo, const Ranges& ranges, int32_t size) {
    std::vector<float> tris;
    const TerrainVertex* verts = geo._contourVertices.data;
    const uint16_t* idx = geo._contourIndices.data;
    if (verts == nullptr || idx == nullptr || ranges.data == nullptr) return tris;
    const float half = static_cast<float>(size >> 1);
    for (int r = 0; r < ranges.size; ++r) {
        const int start = ranges.data[r].values[0];
        const int base = ranges.data[r].values[1];
        const int count = ranges.data[r].values[3];
        if (start < 0 || base < 0 || count < 0 || start + count > geo._contourIndices.size) continue;
        for (int k = 0; k + 2 < count; k += 3) {
            bool ok = true;
            float xy[6];
            for (int c = 0; c < 3; ++c) {
                const int v = base + idx[start + k + c];
                if (v >= geo._contourVertices.size) {
                    ok = false;
                    break;
                }
                xy[c * 2] = static_cast<float>(verts[v].position[0]) / 48.0f + half;
                xy[c * 2 + 1] = static_cast<float>(verts[v].position[1]) / 48.0f + half;
            }
            if (ok) tris.insert(tris.end(), xy, xy + 6);
        }
    }
    return tris;
}

// The walk set of the map as it stands: the lakes of the water mesh, less the rivers and the open water
// (the drawing's mask with the default channel shape: the debug overlay's sliders never reach the sim).
std::vector<uint8_t> TakeWalkSet(const Map& map, const WorldGeometry& geo) {
    const int32_t size = map.size;
    std::vector<int> blocked;
    std::vector<TradePost> posts;
    CollectBuildings(map, geo, blocked, posts);
    std::vector<uint8_t> open;
    BuildOpenMask(OpenStamps(map, &geo, blocked, posts, ChannelShape{}, nullptr, nullptr), size, open);
    std::vector<uint8_t> types(static_cast<size_t>(size) * static_cast<size_t>(size));
    const MapCell* cells = map.cells.GetData();
    for (size_t i = 0; i < types.size(); ++i) types[i] = static_cast<uint8_t>(cells[i].terrainType);
    return WalkCells(size, types.data(), RangeTriangles(geo, geo._contourRangesB, size),
                     RangeTriangles(geo, geo._contourRangesA, size), open);
}

// Each set cell to the pathfinder as `frozen` ? type 9 : its own type, its region block dirty, the zones
// stale: retail's own lazy rebuilds (FillZones, the region graph) then see the cells as walk_type.h reads
// them.
void SendWalkSet(Map& map, const std::vector<uint8_t>& set, bool frozen) {
    const int32_t size = map.size;
    const MapCell* cells = map.cells.GetData();
    int count = 0;
    for (size_t i = 0; i < set.size(); ++i) {
        if (set[i] == 0) continue;
        const int cell = static_cast<int>(i);
        map.regionGraph->MarkCellDirty(cell % size, cell / size);   // 0x1400a8c90
        map.pathfinder->SetCellTerrain(   // 0x1400a6fe0
            cell, static_cast<int>(frozen ? WalkOnIce::kFrozenType : cells[i].terrainType));
        ++count;
    }
    map.zonesStale = 1;   // the zones go stale (as Map::SetTerrainType 0x14009f2f0 marks them)
    std::fprintf(stderr, "[WalkOnIce] %s: %d lake cell(s) %s\n", frozen ? "frozen solid" : "thaw",
                 count, frozen ? "open to walkers" : "back to water");
}

// The ice-surface record (walk_type.h THE SURFACE): one depth per walk cell, kept from the freeze until no ice
// is left. The depth is the ice model's (ice_model.h): 0 at the waterline, 1 about kDepthUnit below it; a
// deeper cell takes the middle of the deep water's patch noise (1 + kDeepSpread / 2), as the drawing does.
constexpr float kWaterline = -0.75f;   // GetUnderWaterAmount's 0 (ext/better_water/shore_strip.h)
constexpr float kDepthUnit = 1.5f;     // world units per depth unit (the frame copy's alpha)
std::vector<float> g_surfaceDepth;

void DropSurface() {
    WalkOnIce::g_surface = nullptr;
    WalkOnIce::g_surfaceBase = nullptr;
    WalkOnIce::g_surfaceCount = 0;
    WalkOnIce::g_surfaceThaw = 0.0f;
    g_surfaceDepth.clear();
}

void TakeSurface(const Map& map, const WorldGeometry& geo, const std::vector<uint8_t>& set) {
    DropSurface();
    g_surfaceDepth.assign(set.size(), -1.0f);
    for (size_t i = 0; i < set.size(); ++i) {
        if (set[i] == 0) continue;
        float depth = (kWaterline - CellHeight(geo, static_cast<int>(i), map.size)) / kDepthUnit;
        if (depth < 0.0f) depth = 0.0f;
        if (depth >= 1.0f) depth = 1.0f + kDeepSpread * 0.5f;
        g_surfaceDepth[i] = depth;
    }
    WalkOnIce::g_surfaceBase = map.cells.GetData();
    WalkOnIce::g_surfaceCount = static_cast<int>(g_surfaceDepth.size());
    WalkOnIce::g_surfaceThaw = 0.0f;
    WalkOnIce::g_surface = g_surfaceDepth.data();
}

// The town's walk set onto the map (the freeze, or the first frame after a load), and its ice surface. False
// when the set does not fit the map (a damaged save): the town is then not walkable.
bool ApplyWalk(Map& map, const WorldGeometry& geo, IceTown& town) {
    const size_t cells = static_cast<size_t>(map.size) * static_cast<size_t>(map.size);
    if (map.size != town.walkEdge || town.walkSet.size() != cells || map.cells.GetSize() < static_cast<int>(cells) ||
        map.pathfinder == nullptr || map.regionGraph == nullptr) {
        return false;
    }
    WalkOnIce::g_base = map.cells.GetData();
    WalkOnIce::g_count = static_cast<int>(cells);
    WalkOnIce::g_frozen = town.walkSet.data();
    SendWalkSet(map, town.walkSet, true);
    TakeSurface(map, geo, town.walkSet);
    return true;
}

// The walk set off the map (the thaw). The seam lets go first, so the rebuilds read water again. The ice
// surface stays (UpdateWalk lets it go with the last of the ice).
void RemoveWalk(Map& map, IceTown& town) {
    WalkOnIce::g_frozen = nullptr;
    WalkOnIce::g_base = nullptr;
    WalkOnIce::g_count = 0;
    if (town.walkSet.size() == static_cast<size_t>(map.size) * static_cast<size_t>(map.size) &&
        map.pathfinder != nullptr && map.regionGraph != nullptr) {
        SendWalkSet(map, town.walkSet, false);
    }
    town.walkable = false;
    town.walkEdge = 0;
    town.walkSet.clear();
}

// A seam set taken for another cell array (a new game, a load) is forgotten without a word to the
// pathfinder: that map's worker and zones went with it.
void ForgetStaleWalk(const Map* map) {
    if (WalkOnIce::g_surface != nullptr && (map == nullptr || WalkOnIce::g_surfaceBase != map->cells.GetData())) {
        DropSurface();
    }
    if (WalkOnIce::g_frozen == nullptr) return;
    if (map == nullptr || WalkOnIce::g_base != map->cells.GetData() || WalkOnIce::g_frozen != Town().walkSet.data()) {
        WalkOnIce::g_frozen = nullptr;
        WalkOnIce::g_base = nullptr;
        WalkOnIce::g_count = 0;
    }
}


// What a game tick (and the frame, for a load) does about walking: open the lake at the freeze, close it at
// the first thaw, put a loaded town's set back on its map.
void UpdateWalk(GameUpdateLoop& game, IceTown& town, bool walkOn, bool flip) {
    Map* map = game.m_game;
    ForgetStaleWalk(map);
    if (map == nullptr || game.m_worldGeometry == nullptr) return;
    const bool want = walkOn && town.known && Solid(town.ice);
    if (flip && want && !town.walkable) {
        town.walkSet = TakeWalkSet(*map, *game.m_worldGeometry);
        town.walkEdge = map->size;
        town.walkable = true;
        if (!ApplyWalk(*map, *game.m_worldGeometry, town)) {
            town.walkable = false;
            town.walkSet.clear();
        }
    } else if (flip && !want && town.walkable) {
        RemoveWalk(*map, town);
    } else if (walkOn && town.walkable && WalkOnIce::g_frozen == nullptr &&
               !ApplyWalk(*map, *game.m_worldGeometry, town)) {
        std::fprintf(stderr, "[WalkOnIce] the saved walk set does not fit the map: the lake stays water\n");
        town.walkable = false;
        town.walkSet.clear();
    }
    // The ice surface follows the thaw front out from the banks, and goes with the last of the ice.
    if (WalkOnIce::g_surface != nullptr) {
        if (!town.known || !HasIce(town.ice)) {
            DropSurface();
        } else {
            WalkOnIce::g_surfaceThaw = town.walkable ? 0.0f : town.ice.thaw;
        }
    }
}

// What the draw reads.
std::atomic<bool> g_drawing{false};
std::atomic<const WorldGeometry*> g_townGeometry{nullptr};   // the town's world, as BeginFrame last saw it
std::atomic<int64_t> g_townStamp{0};                          // steady-clock ms of that BeginFrame
constexpr int64_t kFreshMs = 1000;
std::atomic<float> g_freeze{0.0f};
std::atomic<float> g_thaw{0.0f};

void Publish(const IceState& shown) {
    g_freeze.store(shown.freeze, std::memory_order_relaxed);
    g_thaw.store(shown.thaw, std::memory_order_relaxed);
}

// The look: the defaults, or the debug overlay's live set (Alt+F10, "Winter ice").
IceParams CurrentLook() {
#if BANISHED_TWEAK_OVERLAY
    return Ext::Tweak::IceValues().p;
#else
    return IceParams{};
#endif
}

// ---- the twin (drawing thread) ----------------------------------------------------------------------

Video::PixelProgram* g_icePixel = nullptr;
bool g_icePixelTried = false;

struct Twin {
    const Video::VideoProgram* water = nullptr;
    const void* waterData = nullptr;
    const void* layout = nullptr;
    const void* vertex = nullptr;
    const void* state = nullptr;
    Video::VideoProgram* twin = nullptr;
};
constexpr int kMaxTwins = 8;   // the town's River and Lake, the menu backdrop's pair, a mod's own
Twin g_twins[kMaxTwins];
int g_nextSlot = 0;

Video::PixelProgram* IcePixel() {
    if (g_icePixelTried) return g_icePixel;
    g_icePixelTried = true;
    Video::PixelProgram* p = static_cast<Video::PixelProgram*>(Video::PixelProgram::CreateNewObject());
    if (p == nullptr || p->_data == nullptr) {
        std::fprintf(stderr, "[WinterIce] no device: the ice program was not made\n");
        return nullptr;
    }
    const int size = static_cast<int>(sizeof(g_wi_PSIceOverlay));
    p->_byteCode.SetSize(1);   // one permutation: a backend binds variant 0 for every detail
    Collection::Array<uint8_t>& blob = p->_byteCode[0];
    blob.SetSize(size);
    std::memcpy(blob._data, g_wi_PSIceOverlay, static_cast<size_t>(size));
    if (!p->PlatformCreate()) {
        std::fprintf(stderr, "[WinterIce] the device refused the ice program: no ice on this device\n");
        return nullptr;
    }
    std::fprintf(stderr, "[WinterIce] ice program made (%d bytes)\n", size);
    g_icePixel = p;
    return p;
}

bool Matches(const Twin& t, const Video::VideoProgram* water) {
    return t.water == water && t.waterData == static_cast<const void*>(water->_data) &&
           t.layout == static_cast<const void*>(water->_vertexLayout.Get()) &&
           t.vertex == static_cast<const void*>(water->_vertexProgram.Get()) &&
           t.state == static_cast<const void*>(water->_renderState.Get());
}

const Video::VideoProgram* TwinFor(const Video::VideoProgram* water) {
    Video::PixelProgram* ice = IcePixel();
    if (ice == nullptr) return nullptr;
    for (const Twin& t : g_twins) {
        if (Matches(t, water)) return t.twin;
    }
    // A new water program (a world load, the menu backdrop, a device restart). The twin it replaces is
    // left alive: a backend may still hold it in cached state, and it is a few hundred bytes.
    Twin& t = g_twins[g_nextSlot];
    g_nextSlot = (g_nextSlot + 1) % kMaxTwins;
    t.water = water;
    t.waterData = water->_data;
    t.layout = water->_vertexLayout.Get();
    t.vertex = water->_vertexProgram.Get();
    t.state = water->_renderState.Get();
    Video::VideoProgram* v = static_cast<Video::VideoProgram*>(Video::VideoProgram::CreateNewObject());
    if (v != nullptr && v->_data != nullptr) {
        v->_vertexLayout = water->_vertexLayout;
        v->_vertexProgram = water->_vertexProgram;
        v->_pixelProgram = ice;
        v->_renderState = water->_renderState;
        v->PlatformCreate();
    } else {
        v = nullptr;
    }
    t.twin = v;
    std::fprintf(stderr, "[WinterIce] twin made for water program %p -> %p\n", static_cast<const void*>(water),
                 static_cast<const void*>(v));
    return v;
}

}  // namespace

bool g_tick = false;

bool Enabled() { return Ext::Ext().winter_ice; }

bool WalkEnabled() { return Ext::Ext().walk_on_ice; }   // class C: the game's locked value

void Tick(double seconds) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    // A new game's weather has not run yet: its temperature is the ctor's 0 until the first
    // WeatherSimulation::Update sets the season step (ctor -1; a load restores both).
    if (game == nullptr || game->m_weather == nullptr || game->m_dayCounter == nullptr ||
        game->m_weather->seasonStep < 0) {
        return;
    }
    IceTown& town = Town();
    const float temperature = game->m_weather->temperature;
    const double now = CalendarSeconds(*game->m_dayCounter);
    if (town.adopt) {
        // Decoded from the save: these fronts are this town's, and this tick steps them as the unbroken game's
        // next tick would have.
        town.adopt = false;
        town.weather = game->m_weather;
    } else if (town.known && town.weather == game->m_weather) {
        // The calendar moved by more than this tick (the debug calendar, or the rows were off a while): the
        // fronts no longer fit the day.
        const double jump = now - g_lastSeconds - seconds;
        if (jump < -kMaxStepSeconds || jump > kMaxStepSeconds) town.known = false;
    } else {
        town.known = false;   // a new game or a load (ResetAllState emptied the state), another weather
    }
    if (!town.known) {
        DropSurface();   // ext.walk_on_ice: a surface taken for another town
        town.ice = Guess(temperature);
        town.known = true;
        town.weather = game->m_weather;
        std::fprintf(stderr, "[WinterIce] town: %.1f F, freeze %.2f\n", temperature, town.ice.freeze);
    } else {
        Step(town.ice, temperature, static_cast<float>(seconds));
    }
    g_lastSeconds = now;
    // ext.walk_on_ice: the lake opens to walkers on the tick it is solid and closes on the first thaw.
    UpdateWalk(*game, town, WalkEnabled(), true);
}

bool Drawing(const WorldGeometry& geo) {
    if (!g_drawing.load(std::memory_order_relaxed)) return false;
    if (&geo != g_townGeometry.load(std::memory_order_relaxed)) return false;
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    return now - g_townStamp.load(std::memory_order_relaxed) < kFreshMs;
}

void BeginFrame() {
    g_on = Enabled();
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    const bool town = game != nullptr && game->m_weather != nullptr && game->m_dayCounter != nullptr &&
                      game->m_game != nullptr;
    // The game tick keeps the ice while either row is on: the walk row's ice is the town's state and must
    // not stop when the drawing is turned off.
    g_tick = town && (g_on || WalkEnabled());
    // A loaded town's walk set goes back on its map here, before the frame's first game tick, so no walker
    // asks for a path without it (the unbroken game had it on).
    if (town) UpdateWalk(*game, Town(), WalkEnabled(), false);
    const IceTown& townIce = Town();
    if (!g_on || !town || !townIce.known || townIce.weather != game->m_weather) {
        g_drawing.store(false, std::memory_order_relaxed);
        return;
    }
    if (g_scanWeather != townIce.weather) {
        g_scanWeather = townIce.weather;   // a new town: scan its water at once
        g_scanCountdown = 0;
    }

    // What is drawn: the weather's ice, or the debug overlay's preview fronts (drawing only).
    IceState shown = townIce.ice;
#if BANISHED_TWEAK_OVERLAY
    const Ext::Tweak::Ice tweak = Ext::Tweak::IceValues();
    if (tweak.preview) {
        shown.freeze = tweak.freeze;
        shown.thaw = tweak.thaw;
    }
#endif
    Publish(shown);

    const bool ice = HasIce(shown);
    if (ice && !g_texturesTried && Video::Device::_singleton != nullptr) {
        g_texturesTried = true;
        if (!MakeTexture(g_surface, MipChain(kIceSurface, kIceTextureSize), kIceTextureSize,
                         Video::ImageBuffer::kMipmaps, kAnisotropic, kWrap, "ice surface") ||
            !MakeTexture(g_detail, MipChain(kIceDetail, kIceTextureSize), kIceTextureSize,
                         Video::ImageBuffer::kMipmaps, kAnisotropic, kWrap, "ice detail")) {
            delete g_surface.image;
            g_surface.image = nullptr;
        } else {
            std::fprintf(stderr, "[WinterIce] ice textures made (%d x %d, %d levels)\n", kIceTextureSize,
                         kIceTextureSize, static_cast<int>(g_surface.levels.size()));
        }
    }
#if BANISHED_TWEAK_OVERLAY
    if (tweak.channel != g_channelShape) g_scanCountdown = 0;   // the overlay's channel sliders: a new mask now
#endif
    if (ice && --g_scanCountdown <= 0) {
        g_scanCountdown = kScanFrames;
        ScanMap(*game->m_game, game->m_worldGeometry);
    }
    const bool ready = g_surface.image != nullptr && g_detail.image != nullptr &&
                       g_mask.load(std::memory_order_relaxed) != nullptr;
    g_townGeometry.store(game->m_worldGeometry, std::memory_order_relaxed);
    g_townStamp.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count(),
                      std::memory_order_relaxed);
    g_drawing.store(ice && ready, std::memory_order_relaxed);
}

void DrawIce(Video::Context& context, int32_t a, int32_t b, int32_t c, int32_t d) {
    // The lake material bound its VideoProgram before WorldGeometry's draw ran (ext.better_water's
    // pass, when it drew, bound it back); that is the program the twin stands in for.
    const Video::VideoProgram* water = context._pixelConstantStaging.videoProgram;
    const Mask* mask = g_mask.load(std::memory_order_acquire);
    if (water == nullptr || mask == nullptr) return;
    const Video::VideoProgram* twin = TwinFor(water);
    if (twin == nullptr) return;

    IceParams params = CurrentLook();
    params.freeze = g_freeze.load(std::memory_order_relaxed);
    params.thaw = g_thaw.load(std::memory_order_relaxed);
    // The world position is the view direction plus the camera (SetCamera's view row, which the water
    // vertex programs subtract as gc.cameraPosition); cells = world + the map's half edge.
    const float half = static_cast<float>(mask->size >> 1);
    params.offsetX = context._cameraPosition.x + half;
    params.offsetY = context._cameraPosition.y + half;

    // The look as the program's MaterialConstants (b1), for this draw only; retail's put back after
    // (dirty bit 0x8000 uploads the staging at the next draw), as ext.better_water does.
    float saved[4][4];
    std::memcpy(saved, context._pixelConstantStaging.material, sizeof(saved));
    static_assert(sizeof(params) == sizeof(saved), "IceParams fills the material staging");
    std::memcpy(context._pixelConstantStaging.material, &params, sizeof(saved));
    context._dirty |= RtVideo::kDirtyMaterialConstants;

    // Our textures in slots no water material uses, put back after.
    const Video::ImageBuffer* slot4 = context._imageBuffers[4];
    const Video::ImageBuffer* slot6 = context._imageBuffers[6];
    const Video::ImageBuffer* slot7 = context._imageBuffers[7];
    context.SetImageBuffer(g_surface.image, 4);
    context.SetImageBuffer(g_detail.image, 6);
    context.SetImageBuffer(mask->texture.image, 7);

    // The edge noise in b0 userPosition.yzw (the scene writes .x, the snow cover, with dirty bits 0x10800;
    // .yzw are unread by the water), and under the overlay its preview snow cover in .x; put back after.
    const Math::Vector4 savedUser = context._pixelConstantStaging.pixel[4];
    const IceEdge edge = CurrentEdge();
    context._pixelConstantStaging.pixel[4].y = edge.wobble;
    context._pixelConstantStaging.pixel[4].z = edge.wobbleSize > 0.1f ? 1.0f / edge.wobbleSize : 10.0f;
    context._pixelConstantStaging.pixel[4].w = edge.grain;
#if BANISHED_TWEAK_OVERLAY
    const Ext::Tweak::Ice tweak = Ext::Tweak::IceValues();
    if (tweak.preview && tweak.snow >= 0.0f) context._pixelConstantStaging.pixel[4].x = tweak.snow;
#endif
    context._dirty |= RtVideo::kDirtyGlobalConstants | RtVideo::kDirtyPixelConstants;

    twin->SetupShaders(context);
    context.DrawIndexed(0, a, b, c, d, 1);

    context._pixelConstantStaging.pixel[4] = savedUser;
    context._dirty |= RtVideo::kDirtyGlobalConstants | RtVideo::kDirtyPixelConstants;
    context.SetImageBuffer(slot4, 4);
    context.SetImageBuffer(slot6, 6);
    context.SetImageBuffer(slot7, 7);
    std::memcpy(context._pixelConstantStaging.material, saved, sizeof(saved));
    context._dirty |= RtVideo::kDirtyMaterialConstants;
    water->SetupShaders(context);
}

void OnVideoLost() {
    g_icePixel = nullptr;
    g_icePixelTried = false;
    for (Twin& t : g_twins) t = Twin{};
    g_nextSlot = 0;
    if (g_surface.image != nullptr) g_surface.image->Release();
    if (g_detail.image != nullptr) g_detail.image->Release();
    if (Mask* m = g_mask.load(std::memory_order_relaxed)) {
        if (m->texture.image != nullptr) m->texture.image->Release();
    }
    for (Mask* m : g_oldMasks) {
        delete m->texture.image;
        delete m;
    }
    g_oldMasks.clear();
}

namespace {
// A texture OnVideoLost released (ImageBuffer::Release: the companion deleted, _data null): a new
// companion on the new device, as ImageBuffer::Recreate makes one, then Create with the kept levels
// (Recreate's own Create(nullptr) would make an empty texture first).
void Restore(Texture& t) {
    if (t.image == nullptr || t.image->_data != nullptr) return;
    Video::Device* dev = Video::Device::_singleton;
    if (dev == nullptr || dev->deviceData == nullptr) return;
    t.image->_data = dev->deviceData->CreateImageBufferData(t.image);
    if (t.image->_data == nullptr || !CreateOnDevice(t)) {
        std::fprintf(stderr, "[WinterIce] a texture was not made again after the device restart\n");
    }
}
}  // namespace

void OnVideoRestored() {
    Restore(g_surface);
    Restore(g_detail);
    if (Mask* m = g_mask.load(std::memory_order_relaxed)) Restore(m->texture);
}

}  // namespace WinterIce
}  // namespace Ext
