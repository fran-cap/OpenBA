// src/game/Action/region_pair_index.cpp -- the hash mirror of RegionPairCache::index (NOT RETAIL).
// region_pair_index.h has the identity argument.
//
// Open addressing with linear probing and backward-shift deletion (no tombstones, so a long run of
// inserts and expiries never degrades the probe length). The cache holds at most 0x20000 pairs (one
// per ring slot), so 2^18 cells keep the load at or under one half. 16 bytes per cell: 4 MB,
// allocated once at the first Reset and kept for the process.
#include "Action/region_pair_index.h"

#include <cstdint>
#include <vector>

namespace RegionPairIndex {
namespace {

constexpr int      kBits = 18;
constexpr uint32_t kCap  = 1u << kBits;
constexpr uint32_t kMask = kCap - 1;

struct Cell {
    uint64_t key;    // (uint32 hi << 32) | uint32 lo
    int32_t  slot;   // ring slot; -1 = empty
    int32_t  pad;
};

std::vector<Cell>      g_cells;
const RegionPairCache* g_owner = nullptr;
int                    g_count = 0;

inline uint64_t Key(int hi, int lo) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(hi)) << 32) | static_cast<uint32_t>(lo);
}

inline uint32_t Home(uint64_t key) {
    return static_cast<uint32_t>((key * 0x9E3779B97F4A7C15ull) >> (64 - kBits));
}

}  // namespace

void Reset(const RegionPairCache* owner) {
    if (g_cells.empty()) {
        g_cells.resize(kCap);
    }
    for (Cell& c : g_cells) {
        c.slot = -1;
    }
    g_count = 0;
    g_owner = owner;
}

bool Owns(const RegionPairCache* cache) {
    return cache != nullptr && cache == g_owner;
}

int Find(int hi, int lo) {
    const uint64_t key = Key(hi, lo);
    uint32_t i = Home(key);
    while (true) {
        const Cell& c = g_cells[i];
        if (c.slot < 0) {
            return -1;
        }
        if (c.key == key) {
            return c.slot;
        }
        i = (i + 1) & kMask;
    }
}

void Insert(int hi, int lo, int slot) {
    const uint64_t key = Key(hi, lo);
    uint32_t i = Home(key);
    while (true) {
        Cell& c = g_cells[i];
        if (c.slot < 0) {
            c.key  = key;
            c.slot = slot;
            ++g_count;
            return;
        }
        if (c.key == key) {   // never happens: Store inserts only absent keys
            c.slot = slot;
            return;
        }
        i = (i + 1) & kMask;
    }
}

void Erase(int hi, int lo) {
    const uint64_t key = Key(hi, lo);
    uint32_t i = Home(key);
    while (true) {
        if (g_cells[i].slot < 0) {
            return;   // not indexed (the caller's descent found nothing either)
        }
        if (g_cells[i].key == key) {
            break;
        }
        i = (i + 1) & kMask;
    }
    // Backward-shift: pull later members of the probe run into the hole when their home allows.
    uint32_t hole = i;
    uint32_t j = i;
    while (true) {
        j = (j + 1) & kMask;
        const Cell& c = g_cells[j];
        if (c.slot < 0) {
            break;
        }
        const uint32_t home = Home(c.key);
        // c may move to `hole` iff its home is not cyclically inside (hole, j].
        const bool inRange = hole <= j ? (home > hole && home <= j) : (home > hole || home <= j);
        if (!inRange) {
            g_cells[hole] = c;
            hole = j;
        }
    }
    g_cells[hole].slot = -1;
    --g_count;
}

int Count() {
    return g_count;
}

}  // namespace RegionPairIndex
