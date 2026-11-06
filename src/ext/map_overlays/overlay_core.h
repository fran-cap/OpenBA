#pragma once
// ============================================================================
// src/ext/map_overlays/overlay_core.h -- ext.map_overlays (class C, default OFF): the data model
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md. Standard C++ only: tests/unit/ext_map_overlays_test.cpp
// links overlay_core.cpp, overlay_math.cpp and save_state.cpp on their own.
//
// THE HISTORY (the saved state). What was produced and consumed where, and where citizens walked, over the
// last game year:
//   * a SITE is a map tile (x, z), the tile of the building (or home) where the flow happened. A site is
//     not an entity: a demolished building's history stays on its tiles and rolls out with the year, and a
//     save needs no entity binding;
//   * a RESOURCE is named by the template path of its RawMaterialDescription (External::GetPath),
//     compared case-insensitively, as ext.restricted_foods names its foods. The history keeps a table of the
//     paths it has seen; a site's cells index into it. A path the running game no longer has stays in the
//     table and matches nothing, so a save survives a changed mod list;
//   * time is a MONTH STAMP (year * 12 + month, ext.family_tree's stamp). Flows go to one of kMonths
//     buckets (stamp % kMonths); each bucket remembers the stamp it holds, and AdvanceTo clears a bucket
//     before it is reused, so a query over N months reads only buckets whose stamp is in the window;
//   * TRAFFIC is a grid of kTrafficCell x kTrafficCell tile cells with kTrafficBuckets seasonal buckets
//     of saturating 16-bit counts (one step = a citizen entering a new tile).
//
// THE VIEW (not saved). A Selection picks what the map shows; BuildTiles fills a size*size float grid for
// it (history views here, the instant views -- stock, services, citizens, natural -- game side in
// overlay_views.cpp). overlay_math.h blurs it by the scale slider and maps it to colours.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ext {
namespace MapOverlays {

constexpr const char* kId = "map_overlays";
constexpr int kWireVersion = 1;          // the registry row's formatVersion

constexpr int kMonths = 12;              // the rolling window: one game year, one bucket per month
constexpr int kTrafficCell = 2;          // tiles per traffic cell edge
constexpr int kTrafficBuckets = 4;       // traffic buckets: seasons of 3 months
constexpr int32_t kNoStamp = -1;
constexpr int kMaxMapSize = 4096;        // tiles per edge accepted from the game or a save
constexpr size_t kMaxResources = 65535;  // entries in the resource table (16-bit index)
constexpr size_t kMaxPathBytes = 1023;

enum class Flow : uint8_t { Produced = 0, Consumed = 1, Count = 2 };

// The views, in tab order (MAPOVERLAYS.md §2). The first is retail's map, drawn by MapUI unchanged.
enum class View : uint8_t {
    Map = 0,          // retail (no overlay)
    Production,       // history: Flow::Produced
    Consumption,      // history: Flow::Consumed
    Stock,            // instant: items in storage, markets and homes
    Services,         // instant: coverage of a service building type
    Traffic,          // history: footsteps
    Citizens,         // instant: homes by a wellbeing measure
    Natural,          // instant: trees, stone, iron, wildlife, fish
    Count
};

// What one view shows. `filter` is per view:
//   Production / Consumption / Stock: a resource set (resourceMask over the history's table, built from a
//     category flag mask or a single path by the UI);
//   Services: a service kind (ServiceKind); Citizens: a measure (CitizenMeasure); Natural: a NaturalKind.
enum class ServiceKind : uint8_t { Market, Well, Herbalist, Chapel, School, Tavern, TownHall, Count };
enum class CitizenMeasure : uint8_t { Happiness, Health, Food, Warmth, Age, Count };
enum class NaturalKind : uint8_t { Trees, Stone, Iron, Wildlife, Fish, Count };

struct Selection {
    View view = View::Map;
    uint32_t categoryFlags = 0;        // RawMaterialDescription::_flags bits; 0 with resourcePath empty = all
    std::string resourcePath;          // one resource (template path); wins over categoryFlags when set
    uint8_t kind = 0;                  // ServiceKind / CitizenMeasure / NaturalKind for those views
    int months = kMonths;              // history views: how many recent months to sum (1..kMonths)
    float scale = 0.5f;                // the slider: 0 = exact per tile .. 1 = coarsest blur (overlay_math.h)
    bool onTerrain = false;            // "Show overlay on terrain"
};

struct SiteKey {
    uint16_t x = 0;
    uint16_t z = 0;
};
inline bool operator==(SiteKey a, SiteKey b) {
    return a.x == b.x && a.z == b.z;
}

class History {
public:
    // The map edge in tiles. Clears everything when it changes (a new town).
    void SetMapSize(int tiles);
    int MapSize() const { return mapSize_; }

    // The table index of `path` (case-insensitive), added when new; kNoResource when the table is full or the
    // path is empty / too long.
    static constexpr uint16_t kNoResource = 0xffff;
    uint16_t ResourceIndex(const std::string& path);
    // Lookup only; kNoResource when absent.
    uint16_t FindResource(const std::string& path) const;
    const std::vector<std::string>& Resources() const { return resources_; }

    // Moves the clock to `stamp`: every bucket whose month left the window is cleared. A stamp that goes back
    // (a load of an older save into the same history is not a case: Decode replaces the history) is ignored.
    void AdvanceTo(int32_t stamp);
    int32_t Now() const { return now_; }

    // One flow of `count` items at a tile, in the current month. Ignored outside the map, for kNoResource,
    // and before the first AdvanceTo.
    void Add(Flow flow, SiteKey site, uint16_t resource, uint32_t count);
    // One footstep at a tile.
    void AddStep(int tileX, int tileZ);

    // Sums the last `months` months of `flow` per tile into `out` (size MapSize()^2, row-major z * size + x;
    // resized and zeroed here). `mask` has one entry per table index; an empty mask means every resource.
    void SumFlow(Flow flow, const std::vector<uint8_t>& mask, int months, std::vector<float>& out) const;
    // Footsteps of the last `months` months (rounded up to whole seasons) per tile; a cell's count is
    // spread over its tiles.
    void SumTraffic(int months, std::vector<float>& out) const;

    void Clear();   // keeps the map size
    bool Empty() const;

    // The wire format (MAPOVERLAYS.md §3). Decode replaces the whole history, or leaves it untouched and
    // returns false when the payload is malformed (cut, trailing bytes, counts past the payload, a map size
    // out of range, an index outside the table).
    void Encode(std::vector<uint8_t>& out) const;
    bool Decode(const uint8_t* data, size_t size);

private:
    struct Cell {
        uint16_t resource = 0;
        uint8_t flow = 0;
        uint32_t counts[kMonths] = {};
    };
    struct Site {
        SiteKey key;
        std::vector<Cell> cells;
    };
    // Owned by the core implementation (overlay_core.cpp); the layout is private and may change.
    int TrafficCells() const { return (mapSize_ + kTrafficCell - 1) / kTrafficCell; }
    void ClearMonthBucket(int bucket);
    void ClearTrafficBucket(int bucket);
    void DropEmptyCells();
    int mapSize_ = 0;
    int32_t now_ = kNoStamp;
    int trafficBucket_ = -1;               // the season bucket of now_; -1 before the first AdvanceTo
    // the stamp each bucket holds; a bucket whose stamp is not the one the window expects is cleared
    int32_t bucketStamp_[kMonths] = {};
    int32_t trafficStamp_[kTrafficBuckets] = {};
    std::vector<std::string> resources_;
    std::unordered_map<std::string, uint16_t> folded_;   // lower-case path -> index (case-insensitive lookup)
    std::vector<Site> sites_;              // sorted by (z, x)
    std::vector<uint16_t> traffic_;        // cells^2 * kTrafficBuckets
};

}  // namespace MapOverlays
}  // namespace Ext
