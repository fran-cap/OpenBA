// src/diag/alloc_stats_table.h -- the [ALLOCSTATS] block table (alloc_stats.cpp).
//
// Open addressing with linear probing over a power-of-two table: pointer -> (size,
// caller, ctx). An insert takes the first empty slot within kMaxProbe of the
// pointer's home slot; a lookup stops at the first empty slot. Header-only and
// allocation-free so tests/unit can drive it on a small table.
#pragma once

#include <cstddef>
#include <cstdint>

namespace AllocStatsTable {

struct BlockRec {
    void*    ptr;
    uint64_t size;
    void*    caller;
    void*    ctx;
};

const size_t kMaxProbe = 32;

inline size_t HashPtr(const void* p) {
    uint64_t x = reinterpret_cast<uint64_t>(p) >> 4;  // CAST-OK: hash
    x *= 0x9E3779B97F4A7C15ull;
    x ^= x >> 29;
    return static_cast<size_t>(x);
}

// False when the kMaxProbe slots from the home slot are all taken.
inline bool Insert(BlockRec* table, size_t mask, const BlockRec& rec) {
    const size_t home = HashPtr(rec.ptr) & mask;
    for (size_t probe = 0; probe < kMaxProbe; probe++) {
        BlockRec& b = table[(home + probe) & mask];
        if (!b.ptr) {
            b = rec;
            return true;
        }
    }
    return false;
}

// The slot index holding `p`, or -1.
inline ptrdiff_t Find(const BlockRec* table, size_t mask, const void* p) {
    const size_t home = HashPtr(p) & mask;
    for (size_t probe = 0; probe < kMaxProbe; probe++) {
        const size_t i = (home + probe) & mask;
        if (table[i].ptr == p) return static_cast<ptrdiff_t>(i);
        if (!table[i].ptr) break;
    }
    return -1;
}

// Empty slot `at` without breaking a probe chain that runs through it (review
// F-278: a plain clear cut the chain, so every later entry of it became
// unreachable, its free counted as untracked and its bytes as live for ever).
// Backward-shift deletion: each later entry of the cluster whose home slot is not
// cyclically in (hole, its slot] moves back into the hole. A moved entry only gets
// closer to its home, so it stays within kMaxProbe.
inline void Erase(BlockRec* table, size_t mask, size_t at) {
    size_t hole = at;
    size_t k    = at;
    for (;;) {
        k = (k + 1) & mask;
        if (!table[k].ptr) break;
        const size_t home   = HashPtr(table[k].ptr) & mask;
        const bool   inside = hole < k ? (home > hole && home <= k) : (home > hole || home <= k);
        if (!inside) {
            table[hole] = table[k];
            hole        = k;
        }
    }
    table[hole] = BlockRec{nullptr, 0, nullptr, nullptr};
}

}  // namespace AllocStatsTable
