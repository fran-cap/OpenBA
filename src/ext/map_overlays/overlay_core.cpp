// ============================================================================
// src/ext/map_overlays/overlay_core.cpp -- ext.map_overlays (class C): the history (overlay_core.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §2 / §3. Standard C++ only (the unit build links it).
//
// THE BUCKETS. Month m lives in bucket m % kMonths, season s = floor(m / 3) in bucket s % kTrafficBuckets.
// Each bucket remembers the stamp it holds. AdvanceTo works out, for every bucket, the one stamp of the
// window that maps to it; a bucket holding any other stamp is stale and is zeroed before it is reused. A
// sum reads only buckets whose stamp is inside the window, so a bucket nobody cleared yet reads as empty.
//
// THE SITES. sites_ is sorted by (z, x); a site keeps one cell per (resource, flow) it has seen. Cells whose
// twelve counts are all zero are dropped when a month bucket is cleared, so the year rolls them out.
//
// THE WIRE FORMAT is at the top of save_state.cpp (Encode / Decode live here, beside the layout).
// ============================================================================
#include "ext/map_overlays/overlay_core.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "ext/savetail.h"   // SaveTail::Writer / Reader (the byte helpers)

namespace Ext {
namespace MapOverlays {

namespace {

constexpr int kMonthsPerSeason = kMonths / kTrafficBuckets;   // 3
static_assert(kMonthsPerSeason * kTrafficBuckets == kMonths, "a season is a whole number of months");

// floor division and the non-negative remainder, for any stamp (a garbage year can make one negative)
int64_t FloorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

int Mod(int64_t a, int64_t b) {
    return static_cast<int>(a - FloorDiv(a, b) * b);
}

int64_t SeasonOf(int32_t stamp) {
    return FloorDiv(stamp, kMonthsPerSeason);
}

std::string Fold(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool SiteLess(SiteKey a, SiteKey b) {
    if (a.z != b.z) {
        return a.z < b.z;
    }
    return a.x < b.x;
}

// The wire's fixed parts (MAPOVERLAYS.md §3), for the "count past the payload" checks.
constexpr size_t kCellBytes = 2 + 1 + 4 * kMonths;
constexpr size_t kSiteBytes = 2 + 2 + 2;
constexpr size_t kTrafficCellBytes = 2 * kTrafficBuckets;

bool U16(SaveTail::Reader& r, uint16_t& v) {
    const uint8_t* at = nullptr;
    if (!r.Bytes(2, at)) {
        return false;
    }
    v = static_cast<uint16_t>(at[0] | (at[1] << 8));
    return true;
}

void PutU16(SaveTail::Writer& w, uint16_t v) {
    const uint8_t b[2] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8) };
    w.Bytes(b, 2);
}

}  // namespace

void History::SetMapSize(int tiles) {
    if (tiles < 0 || tiles > kMaxMapSize) {
        tiles = 0;
    }
    if (tiles == mapSize_) {
        return;
    }
    mapSize_ = tiles;
    Clear();
}

uint16_t History::FindResource(const std::string& path) const {
    if (path.empty() || path.size() > kMaxPathBytes) {
        return kNoResource;
    }
    const auto it = folded_.find(Fold(path));
    return it == folded_.end() ? kNoResource : it->second;
}

uint16_t History::ResourceIndex(const std::string& path) {
    const uint16_t found = FindResource(path);
    if (found != kNoResource) {
        return found;
    }
    if (path.empty() || path.size() > kMaxPathBytes || resources_.size() >= kMaxResources) {
        return kNoResource;
    }
    const uint16_t index = static_cast<uint16_t>(resources_.size());
    resources_.push_back(path);
    folded_.emplace(Fold(path), index);
    return index;
}

void History::ClearMonthBucket(int bucket) {
    for (Site& site : sites_) {
        for (Cell& cell : site.cells) {
            cell.counts[bucket] = 0;
        }
    }
}

void History::ClearTrafficBucket(int bucket) {
    for (size_t i = static_cast<size_t>(bucket); i < traffic_.size(); i += kTrafficBuckets) {
        traffic_[i] = 0;
    }
}

void History::DropEmptyCells() {
    for (Site& site : sites_) {
        auto empty = [](const Cell& c) {
            for (int b = 0; b < kMonths; ++b) {
                if (c.counts[b] != 0) {
                    return false;
                }
            }
            return true;
        };
        site.cells.erase(std::remove_if(site.cells.begin(), site.cells.end(), empty), site.cells.end());
    }
    auto bare = [](const Site& s) { return s.cells.empty(); };
    sites_.erase(std::remove_if(sites_.begin(), sites_.end(), bare), sites_.end());
}

void History::AdvanceTo(int32_t stamp) {
    if (stamp == kNoStamp) {
        return;
    }
    if (now_ != kNoStamp && stamp <= now_) {
        return;
    }
    now_ = stamp;
    // the months now..now-11: one per bucket
    bool cleared = false;
    for (int back = 0; back < kMonths; ++back) {
        const int64_t m = static_cast<int64_t>(stamp) - back;
        const int b = Mod(m, kMonths);
        if (bucketStamp_[b] != m) {
            ClearMonthBucket(b);
            bucketStamp_[b] = static_cast<int32_t>(m);
            cleared = true;
        }
    }
    if (cleared) {
        DropEmptyCells();
    }
    const int64_t season = SeasonOf(stamp);
    for (int back = 0; back < kTrafficBuckets; ++back) {
        const int64_t s = season - back;
        const int b = Mod(s, kTrafficBuckets);
        if (trafficStamp_[b] != s) {
            ClearTrafficBucket(b);
            trafficStamp_[b] = static_cast<int32_t>(s);
        }
    }
    trafficBucket_ = Mod(season, kTrafficBuckets);
}

void History::Add(Flow flow, SiteKey site, uint16_t resource, uint32_t count) {
    if (now_ == kNoStamp || count == 0 || flow >= Flow::Count) {
        return;
    }
    if (site.x >= mapSize_ || site.z >= mapSize_) {
        return;
    }
    if (resource == kNoResource || resource >= resources_.size()) {
        return;
    }
    auto it = std::lower_bound(sites_.begin(), sites_.end(), site,
                               [](const Site& s, SiteKey k) { return SiteLess(s.key, k); });
    if (it == sites_.end() || !(it->key == site)) {
        Site fresh;
        fresh.key = site;
        it = sites_.insert(it, fresh);
    }
    Cell* cell = nullptr;
    for (Cell& c : it->cells) {
        if (c.resource == resource && c.flow == static_cast<uint8_t>(flow)) {
            cell = &c;
            break;
        }
    }
    if (cell == nullptr) {
        Cell fresh;
        fresh.resource = resource;
        fresh.flow = static_cast<uint8_t>(flow);
        it->cells.push_back(fresh);
        cell = &it->cells.back();
    }
    uint32_t& slot = cell->counts[Mod(now_, kMonths)];
    slot = (slot > UINT32_MAX - count) ? UINT32_MAX : slot + count;
}

void History::AddStep(int tileX, int tileZ) {
    if (trafficBucket_ < 0) {
        return;
    }
    if (static_cast<unsigned>(tileX) >= static_cast<unsigned>(mapSize_) ||
        static_cast<unsigned>(tileZ) >= static_cast<unsigned>(mapSize_)) {
        return;
    }
    const size_t cell = static_cast<size_t>(tileZ / kTrafficCell) * TrafficCells() + tileX / kTrafficCell;
    uint16_t& v = traffic_[cell * kTrafficBuckets + trafficBucket_];
    if (v != 0xffff) {
        ++v;
    }
}

void History::SumFlow(Flow flow, const std::vector<uint8_t>& mask, int months, std::vector<float>& out) const {
    const size_t tiles = static_cast<size_t>(mapSize_) * mapSize_;
    out.assign(tiles, 0.0f);
    if (now_ == kNoStamp) {
        return;
    }
    months = std::max(1, std::min(months, kMonths));
    const int64_t first = static_cast<int64_t>(now_) - months + 1;
    bool use[kMonths] = {};
    for (int b = 0; b < kMonths; ++b) {
        use[b] = bucketStamp_[b] >= first && bucketStamp_[b] <= now_;
    }
    for (const Site& site : sites_) {
        float total = 0.0f;
        for (const Cell& cell : site.cells) {
            if (cell.flow != static_cast<uint8_t>(flow)) {
                continue;
            }
            if (!mask.empty() && (cell.resource >= mask.size() || mask[cell.resource] == 0)) {
                continue;
            }
            for (int b = 0; b < kMonths; ++b) {
                if (use[b]) {
                    total += static_cast<float>(cell.counts[b]);
                }
            }
        }
        if (site.key.x < mapSize_ && site.key.z < mapSize_) {
            out[static_cast<size_t>(site.key.z) * mapSize_ + site.key.x] += total;
        }
    }
}

void History::SumTraffic(int months, std::vector<float>& out) const {
    const size_t tiles = static_cast<size_t>(mapSize_) * mapSize_;
    out.assign(tiles, 0.0f);
    if (now_ == kNoStamp || mapSize_ == 0) {
        return;
    }
    months = std::max(1, std::min(months, kMonths));
    const int seasons = (months + kMonthsPerSeason - 1) / kMonthsPerSeason;
    const int64_t season = SeasonOf(now_);
    bool use[kTrafficBuckets] = {};
    for (int b = 0; b < kTrafficBuckets; ++b) {
        use[b] = trafficStamp_[b] > season - seasons && trafficStamp_[b] <= season;
    }
    const int cells = TrafficCells();
    for (int cz = 0; cz < cells; ++cz) {
        for (int cx = 0; cx < cells; ++cx) {
            const uint16_t* counts = &traffic_[(static_cast<size_t>(cz) * cells + cx) * kTrafficBuckets];
            uint32_t sum = 0;
            for (int b = 0; b < kTrafficBuckets; ++b) {
                if (use[b]) {
                    sum += counts[b];
                }
            }
            if (sum == 0) {
                continue;
            }
            const int x0 = cx * kTrafficCell;
            const int z0 = cz * kTrafficCell;
            const int x1 = std::min(x0 + kTrafficCell, mapSize_);
            const int z1 = std::min(z0 + kTrafficCell, mapSize_);
            const float each = static_cast<float>(sum) / static_cast<float>((x1 - x0) * (z1 - z0));
            for (int z = z0; z < z1; ++z) {
                for (int x = x0; x < x1; ++x) {
                    out[static_cast<size_t>(z) * mapSize_ + x] = each;
                }
            }
        }
    }
}

void History::Clear() {
    now_ = kNoStamp;
    trafficBucket_ = -1;
    for (int b = 0; b < kMonths; ++b) {
        bucketStamp_[b] = kNoStamp;
    }
    for (int b = 0; b < kTrafficBuckets; ++b) {
        trafficStamp_[b] = kNoStamp;
    }
    resources_.clear();
    folded_.clear();
    sites_.clear();
    const size_t cells = static_cast<size_t>(TrafficCells());
    traffic_.assign(cells * cells * kTrafficBuckets, 0);
}

bool History::Empty() const {
    if (!sites_.empty()) {
        return false;
    }
    for (uint16_t v : traffic_) {
        if (v != 0) {
            return false;
        }
    }
    return true;
}

void History::Encode(std::vector<uint8_t>& out) const {
    SaveTail::Writer w(out);
    w.U32(static_cast<uint32_t>(mapSize_));
    w.I32(now_);
    for (int b = 0; b < kMonths; ++b) {
        w.I32(bucketStamp_[b]);
    }
    for (int b = 0; b < kTrafficBuckets; ++b) {
        w.I32(trafficStamp_[b]);
    }
    w.U32(static_cast<uint32_t>(resources_.size()));
    for (const std::string& path : resources_) {
        PutU16(w, static_cast<uint16_t>(path.size()));
        w.Bytes(reinterpret_cast<const uint8_t*>(path.data()), path.size());
    }
    w.U32(static_cast<uint32_t>(sites_.size()));
    for (const Site& site : sites_) {
        PutU16(w, site.key.x);
        PutU16(w, site.key.z);
        PutU16(w, static_cast<uint16_t>(site.cells.size()));
        for (const Cell& cell : site.cells) {
            PutU16(w, cell.resource);
            w.Bytes(&cell.flow, 1);
            for (int b = 0; b < kMonths; ++b) {
                w.U32(cell.counts[b]);
            }
        }
    }
    const uint32_t cells = static_cast<uint32_t>(TrafficCells());
    w.U32(cells);
    for (uint16_t v : traffic_) {
        PutU16(w, v);
    }
}

bool History::Decode(const uint8_t* data, size_t size) {
    if (data == nullptr && size != 0) {
        return false;
    }
    SaveTail::Reader r(data, size);
    History h;
    uint32_t mapSize = 0;
    if (!r.U32(mapSize)) {
        return false;
    }
    // 0 is a history that never saw a map (a save before the first frame): only empty sites and traffic
    // follow it. Anything above kMaxMapSize is damage.
    if (mapSize > static_cast<uint32_t>(kMaxMapSize)) {
        return false;
    }
    h.mapSize_ = static_cast<int>(mapSize);
    if (!r.I32(h.now_)) {
        return false;
    }
    for (int b = 0; b < kMonths; ++b) {
        if (!r.I32(h.bucketStamp_[b])) {
            return false;
        }
    }
    for (int b = 0; b < kTrafficBuckets; ++b) {
        if (!r.I32(h.trafficStamp_[b])) {
            return false;
        }
    }
    uint32_t resourceCount = 0;
    if (!r.U32(resourceCount) || resourceCount > kMaxResources || resourceCount > r.left / 3) {
        return false;
    }
    for (uint32_t i = 0; i < resourceCount; ++i) {
        uint16_t len = 0;
        const uint8_t* at = nullptr;
        if (!U16(r, len) || len == 0 || len > kMaxPathBytes || !r.Bytes(len, at)) {
            return false;
        }
        const std::string path(reinterpret_cast<const char*>(at), len);
        if (h.FindResource(path) != kNoResource) {
            return false;   // a path listed twice (compared without case)
        }
        h.folded_.emplace(Fold(path), static_cast<uint16_t>(h.resources_.size()));
        h.resources_.push_back(path);
    }
    uint32_t siteCount = 0;
    if (!r.U32(siteCount) || siteCount > r.left / kSiteBytes) {
        return false;
    }
    h.sites_.reserve(siteCount);
    for (uint32_t i = 0; i < siteCount; ++i) {
        Site site;
        uint16_t cellCount = 0;
        if (!U16(r, site.key.x) || !U16(r, site.key.z) || !U16(r, cellCount)) {
            return false;
        }
        if (site.key.x >= mapSize || site.key.z >= mapSize) {
            return false;   // a tile outside the map
        }
        if (!h.sites_.empty() && !SiteLess(h.sites_.back().key, site.key)) {
            return false;   // out of order, or a tile listed twice
        }
        if (cellCount > r.left / kCellBytes) {
            return false;
        }
        site.cells.resize(cellCount);
        for (uint16_t c = 0; c < cellCount; ++c) {
            Cell& cell = site.cells[c];
            const uint8_t* flow = nullptr;
            if (!U16(r, cell.resource) || !r.Bytes(1, flow)) {
                return false;
            }
            cell.flow = *flow;
            if (cell.resource >= resourceCount || cell.flow >= static_cast<uint8_t>(Flow::Count)) {
                return false;
            }
            for (uint16_t prev = 0; prev < c; ++prev) {
                if (site.cells[prev].resource == cell.resource && site.cells[prev].flow == cell.flow) {
                    return false;   // one (resource, flow) twice at a tile
                }
            }
            for (int b = 0; b < kMonths; ++b) {
                if (!r.U32(cell.counts[b])) {
                    return false;
                }
            }
        }
        h.sites_.push_back(std::move(site));
    }
    uint32_t cells = 0;
    if (!r.U32(cells) || cells != static_cast<uint32_t>(h.TrafficCells())) {
        return false;
    }
    const size_t trafficCount = static_cast<size_t>(cells) * cells;
    if (trafficCount > r.left / kTrafficCellBytes) {
        return false;
    }
    h.traffic_.resize(trafficCount * kTrafficBuckets);
    for (uint16_t& v : h.traffic_) {
        if (!U16(r, v)) {
            return false;
        }
    }
    if (!r.AtEnd()) {
        return false;   // trailing bytes
    }
    h.trafficBucket_ = h.now_ == kNoStamp ? -1 : Mod(SeasonOf(h.now_), kTrafficBuckets);
    *this = std::move(h);
    return true;
}

}  // namespace MapOverlays
}  // namespace Ext
