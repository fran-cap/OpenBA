// ============================================================================
// src/ext/derived_cache/hash64.cpp -- XXH64 (NOT RETAIL); hash64.h has the contract
// ============================================================================
#include "ext/derived_cache/hash64.h"

#include <cstring>

namespace Ext {
namespace DerivedCache {

namespace {

constexpr uint64_t kP1 = 0x9E3779B185EBCA87ull;
constexpr uint64_t kP2 = 0xC2B2AE3D27D4EB4Full;
constexpr uint64_t kP3 = 0x165667B19E3779F9ull;
constexpr uint64_t kP4 = 0x85EBCA77C2B2AE63ull;
constexpr uint64_t kP5 = 0x27D4EB2F165667C5ull;

inline uint64_t Rotl(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

// Little-endian loads (x86-64 and arm64 are both little-endian; the cache file is too).
inline uint64_t Read64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

inline uint32_t Read32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

inline uint64_t Round(uint64_t acc, uint64_t input) {
    acc += input * kP2;
    acc = Rotl(acc, 31);
    return acc * kP1;
}

inline uint64_t MergeRound(uint64_t acc, uint64_t lane) {
    acc ^= Round(0, lane);
    return acc * kP1 + kP4;
}

}  // namespace

uint64_t Hash64(const void* data, size_t size, uint64_t seed) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    const uint8_t* const end = p + size;
    uint64_t h;
    if (size >= 32) {
        uint64_t v1 = seed + kP1 + kP2;
        uint64_t v2 = seed + kP2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - kP1;
        const uint8_t* const limit = end - 32;
        do {
            v1 = Round(v1, Read64(p));
            v2 = Round(v2, Read64(p + 8));
            v3 = Round(v3, Read64(p + 16));
            v4 = Round(v4, Read64(p + 24));
            p += 32;
        } while (p <= limit);
        h = Rotl(v1, 1) + Rotl(v2, 7) + Rotl(v3, 12) + Rotl(v4, 18);
        h = MergeRound(h, v1);
        h = MergeRound(h, v2);
        h = MergeRound(h, v3);
        h = MergeRound(h, v4);
    } else {
        h = seed + kP5;
    }
    h += static_cast<uint64_t>(size);
    // The tails compare what is left, not p + 8 / p + 4 against end: that sum forms a pointer up to
    // 7 bytes past one-past-the-end (and nullptr + 8 for an empty buffer), undefined behaviour even
    // when it is never dereferenced.
    while (static_cast<size_t>(end - p) >= 8) {
        h ^= Round(0, Read64(p));
        h = Rotl(h, 27) * kP1 + kP4;
        p += 8;
    }
    if (static_cast<size_t>(end - p) >= 4) {
        h ^= static_cast<uint64_t>(Read32(p)) * kP1;
        h = Rotl(h, 23) * kP2 + kP3;
        p += 4;
    }
    while (p < end) {
        h ^= static_cast<uint64_t>(*p) * kP5;
        h = Rotl(h, 11) * kP1;
        ++p;
    }
    h ^= h >> 33;
    h *= kP2;
    h ^= h >> 29;
    h *= kP3;
    h ^= h >> 32;
    return h;
}

}  // namespace DerivedCache
}  // namespace Ext
