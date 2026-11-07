// ============================================================================
// src/ext/boat_lane/boat_lane.h -- the trade boats' lane through the water (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BOATLANE.md. Plain arithmetic, no engine include (unit tests:
// tests/unit/ext_boat_lane_test.cpp, tests/unit/ext_winter_ice_test.cpp).
//
// Retail's boat route (BoatComponent::SetDestination 0x1400456e0) is the world's polyline 0 up to the
// map, then two A* legs through the water grid (BoatRoute_FindWaterPath 0x1400a6a30: passable types 7, 8
// and 9, mask 0x380; step costs 5 for 7 and 9, 1 for deep water 8), map entry -> dock and dock -> map
// exit, then the polyline out again. The legs keep to the cheapest water and turn in grid steps.
//
// THE LANE. The same two legs, found under GridPathSearch::FindPath's rules (0x14001d040, with
// BoatRoute_FindWaterPath's flags: diagonal on, blocked cells not respected): eight neighbours, a step
// costing 10 straight / 14 diagonal times the type's cost, a neighbour -- the goal included -- taken only
// when its type is passable (GridPathSearch::Neighbour 0x1400a5ec0). Whether the legs exist is that rule
// alone. The lane then adds a pull towards the middle of the water (a step `d` cells from land costs
// centrePull * 25 / d^2 more: the square keeps the lane off the inside of a wide bend, where a gentler
// pull lost to the shorter way round, the user's report) and is a Catmull-Rom spline through every few
// cells, so it reads as a lane a
// boat keeps, not a staircase.
//
// Two users, one answer: ext.boat_lane (class B) sails the boats along it, and ext.winter_ice keeps it
// open in the ice. Both call Legs() with kLanePull / kLaneSmoothing, so with ext.boat_lane on the boats
// and the open channel are the same line. Deterministic: integer costs, a fixed neighbour order, ties
// broken by cell index.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

namespace Ext {
namespace BoatLane {

// BoatRoute_FindWaterPath's step cost of a terrain type (kCost[], types 7 / 8 / 9 of mask 0x380);
// 0 = not passable.
inline int BoatStepCost(uint8_t type) {
    if (type == 8) return 1;
    if (type == 7 || type == 9) return 5;
    return 0;
}

// Each cell's distance to the nearest cell the boats cannot sail, in cells (a 10 / 14 chamfer, two
// passes); 0 on land. The map's edge counts as open water (boats enter there).
inline std::vector<float> BankDistance(const uint8_t* types, int size) {
    const int count = size * size;
    std::vector<int> d(static_cast<size_t>(count), 0);
    constexpr int kFar = 1 << 28;
    for (int i = 0; i < count; ++i) d[static_cast<size_t>(i)] = BoatStepCost(types[i]) == 0 ? 0 : kFar;
    auto relax = [&](int x, int y, int dx, int dy, int w) {
        const int nx = x + dx;
        const int ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= size || ny >= size) return;
        int& here = d[static_cast<size_t>(y * size + x)];
        const int there = d[static_cast<size_t>(ny * size + nx)] + w;
        if (there < here) here = there;
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            relax(x, y, -1, 0, 10);
            relax(x, y, 0, -1, 10);
            relax(x, y, -1, -1, 14);
            relax(x, y, 1, -1, 14);
        }
    }
    for (int y = size - 1; y >= 0; --y) {
        for (int x = size - 1; x >= 0; --x) {
            relax(x, y, 1, 0, 10);
            relax(x, y, 0, 1, 10);
            relax(x, y, 1, 1, 14);
            relax(x, y, -1, 1, 14);
        }
    }
    std::vector<float> out(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int v = d[static_cast<size_t>(i)];
        out[static_cast<size_t>(i)] = v >= kFar ? 1.0e6f : static_cast<float>(v) * 0.1f;
    }
    return out;
}

// The cells (row * size + col) from `from` to `to`, both included; empty when the water does not join
// them or `to` is not water (the game's goal must pass the type test). `from` may be any type (the
// game seeds the start without testing it). `types` holds size * size terrain types. With `bank`
// (BankDistance) and a `centrePull` above 0, a step into a cell `d` cells from land costs
// centrePull * 25 / d^2 times more on top of the game's cost: the path keeps to the middle of the water.
// With `shared` (size * size, non-zero on cells another leg already takes), a step onto such a cell
// costs kShareCost tenths of its price: a second leg runs in the first one's channel until it must part.
inline constexpr int kShareCost = 3;
inline std::vector<int> BoatWaterPath(const uint8_t* types, int size, int from, int to,
                                      const float* bank = nullptr, float centrePull = 0.0f,
                                      const uint8_t* shared = nullptr) {
    std::vector<int> path;
    const int count = size * size;
    if (size <= 0 || from < 0 || to < 0 || from >= count || to >= count) return path;
    if (BoatStepCost(types[to]) == 0) return path;
    std::vector<int> dist(static_cast<size_t>(count), 0x7fffffff);
    std::vector<int> prev(static_cast<size_t>(count), -1);
    using Item = std::pair<int, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    dist[static_cast<size_t>(from)] = 0;
    open.push(Item{0, from});
    static const int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!open.empty()) {
        const Item top = open.top();
        open.pop();
        const int cell = top.second;
        if (top.first > dist[static_cast<size_t>(cell)]) continue;
        if (cell == to) break;
        const int cx = cell % size;
        const int cy = cell / size;
        for (int k = 0; k < 8; ++k) {
            const int x = cx + kDx[k];
            const int y = cy + kDy[k];
            if (x < 0 || y < 0 || x >= size || y >= size) continue;
            const int next = y * size + x;
            const int cost = BoatStepCost(types[next]);
            if (cost == 0) continue;
            int step = cost * (k < 4 ? 10 : 14);
            if (bank != nullptr && centrePull > 0.0f) {
                const float away = bank[next] < 1.0f ? 1.0f : bank[next];
                step += static_cast<int>(static_cast<float>(k < 4 ? 10 : 14) * centrePull * 25.0f / (away * away) + 0.5f);
            }
            if (shared != nullptr && shared[next] != 0) step = step * kShareCost / 10 + 1;
            const int d = top.first + step;
            if (d < dist[static_cast<size_t>(next)]) {
                dist[static_cast<size_t>(next)] = d;
                prev[static_cast<size_t>(next)] = cell;
                open.push(Item{d, next});
            }
        }
    }
    if (from != to && prev[static_cast<size_t>(to)] < 0) return path;
    for (int c = to; c != -1; c = prev[static_cast<size_t>(c)]) {
        path.push_back(c);
        if (c == from) break;
    }
    std::vector<int> forward(path.rbegin(), path.rend());
    return forward;
}

// A cell path as a polyline through the cell centres (the channel's AddChannel input).
inline std::vector<float> CellPathLine(const std::vector<int>& cells, int size) {
    std::vector<float> xy;
    xy.reserve(cells.size() * 2);
    for (const int c : cells) {
        xy.push_back(static_cast<float>(c % size) + 0.5f);
        xy.push_back(static_cast<float>(c / size) + 0.5f);
    }
    return xy;
}

// A polyline (x, y pairs) as a smooth curve: every `stride`-th point kept (the ends always), then a
// Catmull-Rom spline through them, `pieces` segments per span. A stride below 2 keeps every point.
inline std::vector<float> SmoothLine(const std::vector<float>& xy, int stride, int pieces = 6) {
    const int n = static_cast<int>(xy.size() / 2);
    if (n < 3) return xy;
    std::vector<float> keys;
    const int step = stride < 1 ? 1 : stride;
    for (int i = 0; i < n; i += step) {
        keys.push_back(xy[static_cast<size_t>(i) * 2]);
        keys.push_back(xy[static_cast<size_t>(i) * 2 + 1]);
    }
    if ((n - 1) % step != 0) {
        keys.push_back(xy[static_cast<size_t>(n - 1) * 2]);
        keys.push_back(xy[static_cast<size_t>(n - 1) * 2 + 1]);
    }
    const int k = static_cast<int>(keys.size() / 2);
    if (k < 3) return keys;
    auto at = [&](int i, int c) {
        if (i < 0) i = 0;
        if (i >= k) i = k - 1;
        return keys[static_cast<size_t>(i) * 2 + static_cast<size_t>(c)];
    };
    std::vector<float> out;
    out.reserve(static_cast<size_t>(k * pieces + 1) * 2);
    for (int i = 0; i + 1 < k; ++i) {
        for (int j = 0; j < pieces; ++j) {
            const float t = static_cast<float>(j) / static_cast<float>(pieces);
            const float t2 = t * t;
            const float t3 = t2 * t;
            for (int c = 0; c < 2; ++c) {
                const float p0 = at(i - 1, c);
                const float p1 = at(i, c);
                const float p2 = at(i + 1, c);
                const float p3 = at(i + 2, c);
                out.push_back(0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                                      (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3));
            }
        }
    }
    out.push_back(at(k - 1, 0));
    out.push_back(at(k - 1, 1));
    return out;
}

// The lane's shape, shared by the boats and the ice (the user's tuned set, 2026-11-06).
inline constexpr float kLanePull = 1.0f;
inline constexpr int kLaneSmoothing = 4;

// The two legs of the lane, in cell coordinates (x, y pairs; a cell's centre is its index + 0.5).
struct LaneLegs {
    std::vector<float> in;    // map entry -> dock
    std::vector<float> out;   // dock -> map exit
};

// The lane from `entry` to `dock` and on to `exit` (cells, row * size + col). False, and `legs` left
// empty, when the game's own legs do not exist (retail then keeps the polyline alone).
inline bool Legs(const uint8_t* types, int size, int entry, int dock, int exit, float centrePull, int smoothing,
                 LaneLegs& legs) {
    legs.in.clear();
    legs.out.clear();
    if (BoatWaterPath(types, size, entry, dock).empty() || BoatWaterPath(types, size, dock, exit).empty())
        return false;
    const std::vector<float> bank = BankDistance(types, size);
    const std::vector<int> in = BoatWaterPath(types, size, entry, dock, bank.data(), centrePull);
    // The way out keeps to the way in's channel (the in leg's cells and their neighbours) as long as it
    // can: one channel where the boats come and go the same way, a fork only where their map edges part.
    std::vector<uint8_t> shared(static_cast<size_t>(size) * static_cast<size_t>(size), 0);
    for (const int c : in) {
        const int cx = c % size;
        const int cy = c / size;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x >= 0 && y >= 0 && x < size && y < size) shared[static_cast<size_t>(y * size + x)] = 1;
            }
        }
    }
    const std::vector<int> out = BoatWaterPath(types, size, dock, exit, bank.data(), centrePull, shared.data());
    if (in.empty() || out.empty()) return false;
    legs.in = SmoothLine(CellPathLine(in, size), smoothing);
    legs.out = SmoothLine(CellPathLine(out, size), smoothing);
    return true;
}

}  // namespace BoatLane
}  // namespace Ext
