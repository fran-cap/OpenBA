// ============================================================================
// tests/unit/region_pair_index_test.cpp -- PATHBURST: the hash mirror of RegionPairCache::index
// ============================================================================
// src/game/Action/region_pair_index.cpp is linked on its own. The driver replays the cache's own
// use of its index (RegionPairCache::Store 0x1400a5b30 and Expire 0x1400a5d10,
// action_pf_region.cpp / region_paircache.cpp): a lookup; on a miss the ring's write index
// advances, a full ring evicts its oldest pair, the new pair is inserted; the expiry sweep erases
// from the oldest end. The reference is a std::map keyed (hi, lo) -> ring slot, which is what the
// AVL tree holds. After every operation the mirror's answer for the touched key and for a random
// sample of keys (present and absent) must equal the reference's, and every few thousand
// operations every live key is checked plus the count. The runtime layer of the proof, the real
// AVL beside the mirror, is the COMPPROF=4 shadow (StressHarness_Verify "PairCacheIndex").
// ============================================================================
#include "unit.h"
#include "Action/region_pair_index.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

namespace {

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t Next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<uint32_t>(s >> 11);
    }
    int Below(int n) { return static_cast<int>(Next() % static_cast<uint32_t>(n)); }
};

struct Sim {
    std::vector<std::pair<int, int>> ring;   // slot -> key, {-1,-1} empty
    std::map<std::pair<int, int>, int> ref;  // what the AVL index holds
    int newest = 0;
    int oldest = 0;
    long long mismatches = 0;
    long long hits = 0, inserts = 0, evictions = 0, expiries = 0, checks = 0;

    explicit Sim(int ringSize) : ring(static_cast<size_t>(ringSize), std::make_pair(-1, -1)) {}

    int RefFind(int hi, int lo) const {
        auto it = ref.find(std::make_pair(hi, lo));
        return it == ref.end() ? -1 : it->second;
    }
    void Check(int hi, int lo) {
        ++checks;
        if (RegionPairIndex::Find(hi, lo) != RefFind(hi, lo)) ++mismatches;
    }
    void Erase(int slot) {
        const std::pair<int, int> k = ring[static_cast<size_t>(slot)];
        if (k.first >= 0 && ref.erase(k) == 1) {
            RegionPairIndex::Erase(k.first, k.second);
        }
        ring[static_cast<size_t>(slot)] = std::make_pair(-1, -1);
    }
    // Store's lookup / evict / insert.
    void Store(int a, int b) {
        const int hi = a < b ? b : a;
        const int lo = a < b ? a : b;
        if (RegionPairIndex::Find(hi, lo) != RefFind(hi, lo)) ++mismatches;
        if (RefFind(hi, lo) >= 0) {
            ++hits;
            return;
        }
        const int slot = newest;
        newest = newest + 1 >= static_cast<int>(ring.size()) ? 0 : newest + 1;
        if (newest == oldest) {
            Erase(oldest);
            ++evictions;
            oldest = oldest + 1 >= static_cast<int>(ring.size()) ? 0 : oldest + 1;
        }
        ring[static_cast<size_t>(slot)] = std::make_pair(hi, lo);
        ref[std::make_pair(hi, lo)] = slot;
        RegionPairIndex::Insert(hi, lo, slot);
        ++inserts;
        Check(hi, lo);
    }
    // Expire's sweep: n entries from the oldest end.
    void Expire(int n) {
        for (int i = 0; i < n && oldest != newest; ++i) {
            Erase(oldest);
            ++expiries;
            oldest = oldest + 1 >= static_cast<int>(ring.size()) ? 0 : oldest + 1;
        }
    }
    void FullCheck() {
        for (const auto& kv : ref) {
            ++checks;
            if (RegionPairIndex::Find(kv.first.first, kv.first.second) != kv.second) ++mismatches;
        }
        if (RegionPairIndex::Count() != static_cast<int>(ref.size())) ++mismatches;
    }
};

void Run(Sim& s, Rng& r, int ops, int regions, int expireEvery) {
    for (int i = 0; i < ops; ++i) {
        s.Store(r.Below(regions), r.Below(regions));
        if (expireEvery > 0 && r.Below(expireEvery) == 0) {
            s.Expire(1 + r.Below(64));
        }
        for (int k = 0; k < 2; ++k) {
            const int a = r.Below(regions + 8) - 4;   // includes ids never stored
            const int b = r.Below(regions + 8) - 4;
            s.Check(a < b ? b : a, a < b ? a : b);
        }
        if (i % 4096 == 4095) s.FullCheck();
    }
    s.FullCheck();
}

void Report(const char* name, const Sim& s) {
    std::printf("  %s: %lld inserts, %lld hits, %lld evictions, %lld expiries, %lld checks, %d live, %lld mismatches\n",
                name, s.inserts, s.hits, s.evictions, s.expiries, s.checks, RegionPairIndex::Count(), s.mismatches);
}

}  // namespace

UNIT_TEST(region_pair_index_small_ring_matches_the_tree) {
    int owner = 0;
    RegionPairIndex::Reset(reinterpret_cast<const RegionPairCache*>(&owner));   // CAST-OK: an opaque owner token
    CHECK(RegionPairIndex::Owns(reinterpret_cast<const RegionPairCache*>(&owner)));   // CAST-OK: as above
    CHECK(!RegionPairIndex::Owns(nullptr));
    Rng r(0x9A1B0001);
    Sim s(4096);
    Run(s, r, 200000, 300, 50);
    Report("small ring", s);
    CHECK_EQ(s.mismatches, 0LL);
    CHECK(s.evictions > 1000);
    CHECK(s.expiries > 1000);
    CHECK(s.hits > 10000);
}

UNIT_TEST(region_pair_index_full_retail_ring_matches_the_tree) {
    int owner = 0;
    RegionPairIndex::Reset(reinterpret_cast<const RegionPairCache*>(&owner));   // CAST-OK: an opaque owner token
    Rng r(0x9A1B0002);
    Sim s(0x20000);   // retail's ring: the table runs at its design load (one half)
    Run(s, r, 400000, 2000, 0);
    Report("full ring", s);
    CHECK_EQ(s.mismatches, 0LL);
    CHECK(s.evictions > 100000);
    CHECK_EQ(RegionPairIndex::Count(), 0x20000 - 1);
}

UNIT_TEST(region_pair_index_reset_empties_and_changes_owner) {
    int a = 0;
    int b = 0;
    RegionPairIndex::Reset(reinterpret_cast<const RegionPairCache*>(&a));   // CAST-OK: an opaque owner token
    RegionPairIndex::Insert(7, 3, 11);
    CHECK_EQ(RegionPairIndex::Find(7, 3), 11);
    RegionPairIndex::Reset(reinterpret_cast<const RegionPairCache*>(&b));   // CAST-OK: as above
    CHECK_EQ(RegionPairIndex::Find(7, 3), -1);
    CHECK_EQ(RegionPairIndex::Count(), 0);
    CHECK(!RegionPairIndex::Owns(reinterpret_cast<const RegionPairCache*>(&a)));   // CAST-OK: as above
    CHECK(RegionPairIndex::Owns(reinterpret_cast<const RegionPairCache*>(&b)));    // CAST-OK: as above
}
