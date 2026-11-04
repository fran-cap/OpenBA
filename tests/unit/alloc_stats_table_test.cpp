// ============================================================================
// tests/unit/alloc_stats_table_test.cpp -- the [ALLOCSTATS] block table's delete
// ============================================================================
// src/diag/alloc_stats_table.h is the open-addressing table alloc_stats.cpp keeps
// its live blocks in. A delete that only clears its slot cuts the probe chain, so a
// later block of the same chain can no longer be found: its free is counted as
// untracked and its bytes stay live (review F-278).
// ============================================================================
#include "unit.h"
#include "alloc_stats_table.h"

#include <cstdint>
#include <vector>

namespace {

using AllocStatsTable::BlockRec;

const size_t kSlots = 64;
const size_t kMask  = kSlots - 1;

void* FakePtr(uintptr_t n) { return reinterpret_cast<void*>(0x10000 + n * 16); }  // CAST-OK: test

// `count` distinct pointers that share home slot `home`.
std::vector<void*> SameHome(size_t home, size_t count) {
    std::vector<void*> out;
    for (uintptr_t n = 1; out.size() < count && n < 1000000; ++n) {
        void* p = FakePtr(n);
        if ((AllocStatsTable::HashPtr(p) & kMask) == home) out.push_back(p);
    }
    return out;
}

BlockRec Rec(void* p, uint64_t size) { return BlockRec{p, size, nullptr, nullptr}; }

}  // namespace

UNIT_TEST(alloc_stats_table_free_first_of_a_chain_keeps_the_rest) {
    std::vector<BlockRec> table(kSlots, BlockRec{nullptr, 0, nullptr, nullptr});
    const std::vector<void*> ps = SameHome(5, 3);
    CHECK(ps.size() == 3);
    for (void* p : ps) CHECK(AllocStatsTable::Insert(table.data(), kMask, Rec(p, 100)));

    // Free the first: the other two must still be found.
    ptrdiff_t at = AllocStatsTable::Find(table.data(), kMask, ps[0]);
    CHECK(at == 5);
    AllocStatsTable::Erase(table.data(), kMask, static_cast<size_t>(at));
    CHECK(AllocStatsTable::Find(table.data(), kMask, ps[0]) < 0);
    CHECK(AllocStatsTable::Find(table.data(), kMask, ps[1]) >= 0);
    CHECK(AllocStatsTable::Find(table.data(), kMask, ps[2]) >= 0);

    // Free the rest: nothing is left behind.
    for (size_t i = 1; i < ps.size(); ++i) {
        at = AllocStatsTable::Find(table.data(), kMask, ps[i]);
        CHECK(at >= 0);
        if (at >= 0) AllocStatsTable::Erase(table.data(), kMask, static_cast<size_t>(at));
    }
    for (const BlockRec& b : table) CHECK(b.ptr == nullptr);
}

UNIT_TEST(alloc_stats_table_erase_keeps_other_homes_and_wraps) {
    std::vector<BlockRec> table(kSlots, BlockRec{nullptr, 0, nullptr, nullptr});
    // Two at slot 62, two at slot 63, two at slot 0: the cluster wraps the end.
    const std::vector<void*> a = SameHome(62, 2);
    const std::vector<void*> b = SameHome(63, 2);
    const std::vector<void*> c = SameHome(0, 2);
    CHECK(a.size() == 2 && b.size() == 2 && c.size() == 2);
    std::vector<void*> all;
    for (void* p : a) all.push_back(p);
    for (void* p : b) all.push_back(p);
    for (void* p : c) all.push_back(p);
    for (void* p : all) CHECK(AllocStatsTable::Insert(table.data(), kMask, Rec(p, 8)));

    // Free in insertion order; after each free every remaining one is found.
    for (size_t i = 0; i < all.size(); ++i) {
        const ptrdiff_t at = AllocStatsTable::Find(table.data(), kMask, all[i]);
        CHECK(at >= 0);
        if (at >= 0) AllocStatsTable::Erase(table.data(), kMask, static_cast<size_t>(at));
        for (size_t j = i + 1; j < all.size(); ++j)
            CHECK(AllocStatsTable::Find(table.data(), kMask, all[j]) >= 0);
    }
    for (const BlockRec& r : table) CHECK(r.ptr == nullptr);
}
