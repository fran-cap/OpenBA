// ============================================================================
// tests/unit/allocator_hot_path_test.cpp -- the optimised allocator hot path against the
//   frozen transcription (tests/unit/allocator_oracle.h)
// ============================================================================
// src/engine/system/allocator.cpp computes retail's results with cheaper instructions (a
// count-leading-zeros for the bin bit scan, shifts for power-of-two DIVs, the binGroups
// index of non-empty bins). The claim is exactness: for any sequence of Allocate / Free the
// optimised allocator returns the same addresses and leaves the same heap as the literal
// transcription. These tests run both side by side and compare after EVERY call:
//   * the returned block's offset from its range's base (and the range index);
//   * every scalar (range count / capacity / sizes, minBin, allocatedBytes, allocCount,
//     freeTagCount, pageSize), every range's base / end offsets and committed mark, and
//     every one of the 192 free-list heads (as offsets);
//   * the sequence of commit / decommit calls handed to the platform;
//   * the binGroups invariant (bit g == any head of bins 8g..8g+7 non-null);
// and, every few hundred calls and at the end, every committed byte of every range (tags,
// free-list links, footers, and the bytes the script wrote into the blocks).
//
// Both allocators run over a FAKE platform seam: each reserves its ranges from its own
// buffer at the same offsets, so relative free-list links (which may span ranges) are the
// same numbers on both sides and the arenas compare with memcmp. A decommit zeroes its
// pages, as a fresh commit hands back zeroed pages. The real seam is restored afterwards.
//
// Not covered, on purpose: align == 0 and negative aligns. align == 0 makes align8 == 0 and
// retail's DIV RCX (0x1800c6f66) divides by zero; the transcription and the optimised body
// both keep that crash (the optimised body takes the DIV for any non-power-of-two divisor).
// ============================================================================
#include "unit.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "allocator_oracle.h"
#include "system/MemoryAllocator.h"

using rt::system::MemoryRange;
using rt::system::RtSysMemPlatform;

namespace {

// ---- the fake platform ------------------------------------------------------------------
constexpr uint64_t kFakePage   = 4096;
constexpr uint64_t kArenaBytes = 64ull << 20;   // per side; ranges are reserved from it
constexpr uint64_t kTableBytes = 1ull << 20;    // per side; the range tables
// Retail's alignment pad depends on the ABSOLUTE address modulo align8 (and keeps only its
// low 32 bits). For a non-power-of-two align8 (24, 40, 104 below) the two arenas must
// therefore be congruent modulo every align8 the script uses and modulo 2^16: side 1's
// arena sits a multiple of lcm(2^16, 3, 5, 13) = 0xc30000 above side 0's.
constexpr uint64_t kCongruence = 0x10000ull * 3 * 5 * 13;
constexpr uint64_t kSideSpan   = ((kArenaBytes + kCongruence - 1) / kCongruence) * kCongruence;
std::vector<uint8_t> g_arenaStore;   // both arenas, kSideSpan apart

struct CommitEvent {
    int      kind;     // 1 commit, 2 decommit
    uint64_t offset;   // from the side's arena base
    uint64_t bytes;
};

struct Side {
    std::vector<uint8_t>     tableStore;
    uint8_t*                 arena = nullptr;
    uint8_t*                 table = nullptr;
    uint64_t                 arenaUsed = 0;
    uint64_t                 tableUsed = 0;
    std::vector<CommitEvent> events;

    void Init(uint8_t* arenaBase) {
        tableStore.assign(kTableBytes + 0x10000, 0);
        arena = arenaBase;
        table = Align(tableStore.data());
        arenaUsed = 0;
        tableUsed = 0;
        events.clear();
    }
    static uint8_t* Align(uint8_t* p) {
        const uintptr_t v = reinterpret_cast<uintptr_t>(p);  // CAST-OK: ptr_int
        return p + ((0x10000 - (v & 0xffff)) & 0xffff);
    }
    bool OwnsArena(const void* p) const {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        return b >= arena && b < arena + kArenaBytes;
    }
};

Side  g_sides[2];
Side* g_active = nullptr;   // the side whose allocator is running

void* FakeReserve(void* /*addr*/, uint64_t bytes) {
    Side& s = *g_active;
    if (s.arenaUsed + bytes > kArenaBytes) {
        std::printf("  allocator_hot_path: the fake arena is full (%llu bytes asked)\n",
                    static_cast<unsigned long long>(bytes));
        return nullptr;
    }
    void* p = s.arena + s.arenaUsed;
    s.arenaUsed += (bytes + 0xffff) & ~0xffffull;   // VirtualAlloc's 64 KB reservation granularity
    return p;
}
void* FakeReserveCommit(void* /*addr*/, uint64_t bytes) {
    Side& s = *g_active;
    if (s.tableUsed + bytes > kTableBytes) return nullptr;
    void* p = s.table + s.tableUsed;
    s.tableUsed += (bytes + 0xffff) & ~0xffffull;
    return p;
}
void* FakeCommit(void* addr, uint64_t bytes) {
    Side& s = *g_active;
    if (s.OwnsArena(addr))
        s.events.push_back({1, static_cast<uint64_t>(static_cast<uint8_t*>(addr) - s.arena), bytes});
    return addr;
}
void FakeDecommit(void* addr, uint64_t bytes) {
    Side& s = *g_active;
    if (s.OwnsArena(addr))
        s.events.push_back({2, static_cast<uint64_t>(static_cast<uint8_t*>(addr) - s.arena), bytes});
    std::memset(addr, 0, static_cast<size_t>(bytes));   // a later commit hands back zero pages
}
void FakeRelease(void* /*addr*/) {}
uint64_t FakePageSize() { return kFakePage; }
void FakeLock(Platform::LockStorage* /*cs*/) {}

const RtSysMemPlatform kFake = {
    &FakeReserve, &FakeReserveCommit, &FakeCommit, &FakeDecommit, &FakeRelease, &FakePageSize,
    &FakeLock, &FakeLock, &FakeLock, &FakeLock,
};

// ---- the pair under test ----------------------------------------------------------------
struct Pair {
    System::MemoryAllocator  ref{};   // driven by the oracle (side 0)
    System::MemoryAllocator  opt{};   // driven by allocator.cpp (side 1)
    alloc_oracle::Oracle     oracle{};
    const rt::system::MemoryAllocatorCommitSeam* savedSeam = nullptr;
    bool                     ok = true;
    long                     step = 0;
    const char*              trace = "";

    Pair(const char* name, uint64_t minRangeMB) {
        trace = name;
        savedSeam = System::MemoryAllocator::_extCommitSeam;
        System::MemoryAllocator::_extCommitSeam = nullptr;
        RtSysMemPlatformInstall(&kFake);
        g_arenaStore.assign(2 * kSideSpan + 0x10000, 0);
        uint8_t* base = Side::Align(g_arenaStore.data());
        g_sides[0].Init(base);
        g_sides[1].Init(base + kSideSpan);
        oracle.self = &ref;
        oracle.plat = &kFake;
        ref.minBin = 0xbf;
        opt.minBin = 0xbf;
        g_active = &g_sides[0];
        CHECK(alloc_oracle::Startup(oracle, minRangeMB, 0));
        g_active = &g_sides[1];
        CHECK(opt.Startup(minRangeMB, 0));
        Compare(nullptr, nullptr);
    }
    ~Pair() {
        RtSystemAllocatorInstallPlatform();   // the real seam back
        System::MemoryAllocator::_extCommitSeam = savedSeam;
    }

    void Fail(const char* what) {
        if (ok) std::printf("  FAIL allocator_hot_path [%s] step %ld: %s\n", trace, step, what);
        ok = false;
        ++unit::Failures();
    }

    uint64_t Off(int side, const void* p) {
        if (p == nullptr) return ~0ull;
        return static_cast<uint64_t>(static_cast<const uint8_t*>(p) - g_sides[side].arena);
    }

    void Compare(void* a, void* b) {
        if (!ok) return;
        if (Off(0, a) != Off(1, b)) return Fail("returned address");
        const System::MemoryAllocator& r = ref;
        const System::MemoryAllocator& o = opt;
        if (r.rangeCount != o.rangeCount || r.rangeCapacity != o.rangeCapacity) return Fail("range count");
        if (r.minRangeSize != o.minRangeSize || r.maxTotalSize != o.maxTotalSize) return Fail("range sizes");
        if (r.totalReserved != o.totalReserved) return Fail("totalReserved");
        if (r.minBin != o.minBin) return Fail("minBin");
        if (r.allocatedBytes != o.allocatedBytes) return Fail("allocatedBytes");
        if (r.allocCount != o.allocCount) return Fail("allocCount");
        if (r.freeTagCount != o.freeTagCount) return Fail("freeTagCount");
        if (r.pageSize != o.pageSize) return Fail("pageSize");
        for (int i = 0; i < r.rangeCount; ++i) {
            if (Off(0, r.ranges[i].base) != Off(1, o.ranges[i].base)) return Fail("range base");
            if (Off(0, r.ranges[i].end) != Off(1, o.ranges[i].end)) return Fail("range end");
            if (r.ranges[i].committed != o.ranges[i].committed) return Fail("range committed");
        }
        uint32_t groups = 0;
        for (int bin = 0; bin < rt::system::kFreeListBinCount; ++bin) {
            if (Off(0, r.freeLists[bin]) != Off(1, o.freeLists[bin])) return Fail("free-list head");
            if (o.freeLists[bin] != nullptr) groups |= 1u << (bin >> 3);
        }
        if (groups != o.binGroups) return Fail("binGroups invariant");
        const std::vector<CommitEvent>& e0 = g_sides[0].events;
        const std::vector<CommitEvent>& e1 = g_sides[1].events;
        if (e0.size() != e1.size()) return Fail("commit / decommit count");
        for (size_t i = 0; i < e0.size(); ++i) {
            if (e0[i].kind != e1[i].kind || e0[i].offset != e1[i].offset || e0[i].bytes != e1[i].bytes)
                return Fail("commit / decommit call");
        }
    }

    // Every committed byte of every range.
    void CompareArenas() {
        if (!ok) return;
        for (int i = 0; i < ref.rangeCount; ++i) {
            const uint64_t n = ref.ranges[i].committed;
            if (std::memcmp(ref.ranges[i].base, opt.ranges[i].base, static_cast<size_t>(n)) != 0)
                return Fail("arena bytes");
        }
    }

    // The script writes the block's first and last bytes (what a user does), the same on
    // both sides: the tags beside it must not depend on, or clobber, user data.
    static void Scribble(void* p, uint64_t size, uint32_t stamp) {
        if (p == nullptr || size == 0) return;
        uint8_t* b = static_cast<uint8_t*>(p);
        const uint64_t head = size < 64 ? size : 64;
        for (uint64_t i = 0; i < head; ++i) b[i] = static_cast<uint8_t>(stamp + i);
        if (size > 64) {
            const uint64_t tail = size - 64 < 8 ? size - 64 : 8;
            for (uint64_t i = 0; i < tail; ++i) b[size - 1 - i] = static_cast<uint8_t>(stamp ^ i);
        }
    }

    struct Block {
        void*    a;
        void*    b;
        uint64_t size;
    };

    Block Alloc(uint64_t size, int align) {
        ++step;
        g_active = &g_sides[0];
        void* a = alloc_oracle::Allocate(oracle, size, align);
        g_active = &g_sides[1];
        void* b = opt.Allocate(size, align);
        const uint32_t stamp = static_cast<uint32_t>(step * 2654435761u);
        Scribble(a, size, stamp);
        Scribble(b, size, stamp);
        Compare(a, b);
        if ((step & 255) == 0) CompareArenas();
        return {a, b, size};
    }

    void Free(const Block& blk) {
        ++step;
        g_active = &g_sides[0];
        alloc_oracle::Free(oracle, blk.a);
        g_active = &g_sides[1];
        opt.Free(blk.b);
        Compare(nullptr, nullptr);
        if ((step & 255) == 0) CompareArenas();
    }
};

const int kAligns[] = {1, 2, 4, 8, 8, 8, 8, 16, 16, 32, 64, 128, 4096, 3, 12, 24, 40, 100};

int DrawAlign(std::mt19937_64& rng) {
    return kAligns[rng() % (sizeof(kAligns) / sizeof(kAligns[0]))];
}

// Sizes over every bin: the exact bins (<= 0x200 bytes), the power-of-two bins up to
// several MB (past the 1 MB range, so the arena grows), and 0.
uint64_t DrawSize(std::mt19937_64& rng) {
    const uint32_t kind = static_cast<uint32_t>(rng() % 1000);
    if (kind < 5) return 0;
    if (kind < 550) return 1 + rng() % 0x200;
    if (kind < 850) return 0x200 + rng() % 0x2000;
    if (kind < 970) return 0x2000 + rng() % 0x40000;
    if (kind < 995) return 0x40000 + rng() % 0x100000;
    return 0x100000 + rng() % 0x300000;
}

}  // namespace

// ---- the bin arithmetic, every boundary -----------------------------------------------------
UNIT_TEST(allocator_bin_for_size_matches_the_bit_scan_loop) {
    System::MemoryAllocator a{};
    long bad = 0;
    for (uint64_t n = 0; n < (1ull << 22); ++n) {
        if (a.GetBinForSize(n) != alloc_oracle::GetBinForSize(n)) ++bad;
        if (a.GetBinForLargeSize(n) != alloc_oracle::GetBinForLargeSize(n)) ++bad;
    }
    for (int k = 0; k < 64; ++k) {
        const uint64_t p = 1ull << k;
        const uint64_t probes[] = {p - 2, p - 1, p, p + 1, p + 2, p | (p >> 1), ~0ull >> (63 - k)};
        for (uint64_t n : probes) {
            if (a.GetBinForSize(n) != alloc_oracle::GetBinForSize(n)) ++bad;
            if (a.GetBinForLargeSize(n) != alloc_oracle::GetBinForLargeSize(n)) ++bad;
        }
    }
    std::mt19937_64 rng(0x5eed0b1);
    for (int i = 0; i < 1000000; ++i) {
        const uint64_t n = rng() >> (rng() % 64);
        if (a.GetBinForSize(n) != alloc_oracle::GetBinForSize(n)) ++bad;
        if (a.GetBinForLargeSize(n) != alloc_oracle::GetBinForLargeSize(n)) ++bad;
    }
    CHECK_EQ(bad, 0L);
}

// ---- randomized: mixed sizes and aligns, frees anywhere ------------------------------------
UNIT_TEST(allocator_hot_path_matches_transcription_random) {
    for (uint64_t seed = 1; seed <= 3; ++seed) {
        Pair pair("random", 1);
        std::mt19937_64 rng(seed * 0x9e3779b97f4a7c15ull);
        std::vector<Pair::Block> live;
        uint64_t liveBytes = 0;
        for (int i = 0; i < 60000 && pair.ok; ++i) {
            const bool doFree = !live.empty() && (rng() % 100 < 48 || liveBytes > (16ull << 20));
            if (doFree) {
                const size_t idx = static_cast<size_t>(rng() % live.size());
                liveBytes -= live[idx].size;
                pair.Free(live[idx]);
                live[idx] = live.back();
                live.pop_back();
            } else {
                const uint64_t size = DrawSize(rng);
                live.push_back(pair.Alloc(size, DrawAlign(rng)));
                liveBytes += size;
            }
        }
        while (!live.empty() && pair.ok) {
            pair.Free(live.back());
            live.pop_back();
        }
        pair.CompareArenas();
        CHECK(pair.ok);
    }
}

// ---- game-shaped: the work-search candidate tree (MemAlloc(0x30, 8) per node, the whole
// tree freed after the search), over a heap with long-lived blocks of other sizes ----------
UNIT_TEST(allocator_hot_path_matches_transcription_work_tree) {
    Pair pair("work_tree", 1);
    std::mt19937_64 rng(20261118);
    std::vector<Pair::Block> longLived;
    for (int round = 0; round < 1500 && pair.ok; ++round) {
        // Some long-lived traffic between searches (actions, strings, arrays).
        const int other = static_cast<int>(rng() % 6);
        for (int i = 0; i < other; ++i) {
            if (!longLived.empty() && rng() % 2 == 0) {
                const size_t idx = static_cast<size_t>(rng() % longLived.size());
                pair.Free(longLived[idx]);
                longLived[idx] = longLived.back();
                longLived.pop_back();
            } else {
                const uint64_t sizes[] = {0x18, 0x20, 0x28, 0x40, 0x58, 0x90, 0x100, 0x400, 0x1000};
                longLived.push_back(pair.Alloc(sizes[rng() % 9], 8));
            }
        }
        // The tree: N nodes, then freed in a post-order-like shuffle (left subtree, right
        // subtree, node) -- here: random halves, which mixes both coalescing directions.
        const int n = 10 + static_cast<int>(rng() % 400);
        std::vector<Pair::Block> nodes;
        for (int i = 0; i < n; ++i) nodes.push_back(pair.Alloc(0x30, 8));
        while (!nodes.empty() && pair.ok) {
            size_t idx = nodes.size() - 1;
            if (rng() % 3 == 0) idx = static_cast<size_t>(rng() % nodes.size());
            pair.Free(nodes[idx]);
            nodes.erase(nodes.begin() + static_cast<long>(idx));
        }
    }
    for (const Pair::Block& b : longLived) pair.Free(b);
    pair.CompareArenas();
    CHECK(pair.ok);
}

// ---- adversarial shapes ---------------------------------------------------------------------
UNIT_TEST(allocator_hot_path_matches_transcription_adversarial) {
    Pair pair("adversarial", 1);
    std::mt19937_64 rng(77);

    // (1) Coalescing in both directions: A B C, free A and C, then B (backward + forward),
    // over every small size, with the 3-bit prevSize field and the footer both in play.
    for (uint64_t size = 0; size <= 0x220 && pair.ok; size += 4) {
        Pair::Block a = pair.Alloc(size, 8);
        Pair::Block b = pair.Alloc(size + 8, 8);
        Pair::Block c = pair.Alloc(size, 8);
        Pair::Block guard = pair.Alloc(8, 8);
        pair.Free(a);
        pair.Free(c);
        pair.Free(b);
        pair.Free(guard);
    }

    // (2) Holes under 16 bytes: alignment pads of 8..56 bytes are split off as free blocks,
    // the ones under 16 bytes never linked; then everything freed in reverse and in order.
    {
        std::vector<Pair::Block> v;
        for (int i = 0; i < 2000 && pair.ok; ++i) {
            const int aligns[] = {16, 32, 64, 8};
            v.push_back(pair.Alloc(1 + rng() % 40, aligns[i % 4]));
        }
        for (size_t i = 0; i < v.size(); i += 2) pair.Free(v[i]);
        for (size_t i = v.size(); i-- > 0;) {
            if (i % 2 == 1) pair.Free(v[i]);
        }
    }

    // (3) Many blocks of one large bin, freed with live separators so they stay apart: long
    // first-fit walks inside a power-of-two bin, with requests that fit only deep in the list.
    {
        std::vector<Pair::Block> holes;
        std::vector<Pair::Block> seps;
        for (int i = 0; i < 300 && pair.ok; ++i) {
            holes.push_back(pair.Alloc(0x1000 + static_cast<uint64_t>(i) * 8, 8));
            seps.push_back(pair.Alloc(0x10, 8));
        }
        for (const Pair::Block& h : holes) pair.Free(h);
        std::vector<Pair::Block> refill;
        for (int i = 0; i < 300 && pair.ok; ++i)
            refill.push_back(pair.Alloc(0x1000 + rng() % 0xa00, rng() % 2 ? 8 : 16));
        for (const Pair::Block& r : refill) pair.Free(r);
        for (const Pair::Block& s : seps) pair.Free(s);
    }

    // (4) Range growth: blocks larger than the range, then the tail freed (decommit), then
    // a fill that reuses the first range again; minBin running up to 192 when a bin empties.
    {
        std::vector<Pair::Block> big;
        for (int i = 0; i < 12 && pair.ok; ++i) big.push_back(pair.Alloc((1ull << 20) + rng() % (3ull << 20), 8));
        for (size_t i = 0; i < big.size(); i += 3) pair.Free(big[i]);
        std::vector<Pair::Block> fill;
        for (int i = 0; i < 4000 && pair.ok; ++i) fill.push_back(pair.Alloc(DrawSize(rng) % 0x8000, DrawAlign(rng)));
        for (size_t i = 0; i < big.size(); ++i) {
            if (i % 3 != 0) pair.Free(big[i]);
        }
        for (size_t i = fill.size(); i-- > 0;) pair.Free(fill[i]);
    }

    // (5) LIFO and FIFO stacks of the same size class.
    for (uint64_t size : {0x8ull, 0x30ull, 0x1f8ull, 0x200ull, 0x208ull, 0x7f8ull, 0x10000ull}) {
        std::vector<Pair::Block> v;
        for (int i = 0; i < 200 && pair.ok; ++i) v.push_back(pair.Alloc(size, 8));
        for (size_t i = v.size(); i-- > 0;) pair.Free(v[i]);
        v.clear();
        for (int i = 0; i < 200 && pair.ok; ++i) v.push_back(pair.Alloc(size, 16));
        for (const Pair::Block& b : v) pair.Free(b);
    }
    pair.CompareArenas();
    CHECK(pair.ok);
}

// ---- the minBin walk: the floor bin emptied on a heap with no smaller holes ----------------
// On a fresh heap the only free block is the range's tail. Allocate A and a separator, free A
// (A's bin becomes the floor), allocate A's size again: the floor bin empties and minBin
// walks up to the tail's bin -- for every block size of the exact bins and the power-of-two
// bins up to 256 KB. Then the walk off the end: a request that takes a whole range leaves no
// free block, minBin goes to 192 (one past the last bin), and the next request adds a range.
UNIT_TEST(allocator_hot_path_matches_transcription_min_bin_walk) {
    {
        // One Pair at a time: a Pair owns the fake arenas and the platform seam.
        Pair pair("min_bin_walk", 1);
        std::vector<uint64_t> sizes;
        for (uint64_t s = 0; s <= 0x200; s += 8) sizes.push_back(s);
        for (uint64_t s = 0x200; s <= 0x40000; s *= 2) {
            sizes.push_back(s - 8);
            sizes.push_back(s);
            sizes.push_back(s + 8);
        }
        for (uint64_t s : sizes) {
            if (!pair.ok) break;
            Pair::Block a = pair.Alloc(s, 8);
            Pair::Block sep = pair.Alloc(8, 8);
            pair.Free(a);
            Pair::Block again = pair.Alloc(s, 8);
            pair.Free(again);
            pair.Free(sep);
        }
        pair.CompareArenas();
        CHECK(pair.ok);
    }
    {
        Pair whole("min_bin_walk_off_the_end", 1);
        Pair::Block all = whole.Alloc((1ull << 20) - 8, 8);   // want == the whole first range
        CHECK_EQ(whole.opt.minBin, rt::system::kFreeListBinCount);
        Pair::Block next = whole.Alloc(0x30, 8);              // a second range
        CHECK_EQ(whole.opt.rangeCount, 2);
        whole.Free(next);
        whole.Free(all);
        whole.CompareArenas();
        CHECK(whole.ok);
    }
}

// ---- MemFreeBatch: the same Free calls, in order, under one lock hold ---------------------
UNIT_TEST(allocator_mem_free_batch_matches_one_free_per_pointer) {
    Pair pair("batch", 1);
    std::mt19937_64 rng(4242);
    System::MemoryAllocator* const savedSingleton = System::MemoryAllocator::_singleton;
    const System::MemoryAllocatorInstrument savedSeat = System::g_memoryAllocatorInstrument;
    System::g_memoryAllocatorInstrument = {nullptr, nullptr};
    for (int round = 0; round < 200 && pair.ok; ++round) {
        const int n = 1 + static_cast<int>(rng() % 300);
        std::vector<Pair::Block> nodes;
        for (int i = 0; i < n; ++i) nodes.push_back(pair.Alloc(0x30, 8));
        std::vector<void*> ptrs;
        for (const Pair::Block& b : nodes) {
            ptrs.push_back(b.b);
            if (rng() % 17 == 0) ptrs.push_back(nullptr);   // a null in the batch is skipped
        }
        // Reference: one oracle Free per pointer, in order.
        g_active = &g_sides[0];
        for (const Pair::Block& b : nodes) alloc_oracle::Free(pair.oracle, b.a);
        // Under test: the batch entry point over the optimised allocator.
        g_active = &g_sides[1];
        System::MemoryAllocator::_singleton = &pair.opt;
        System::MemoryAllocator::MemFreeBatch(ptrs.data(), static_cast<int>(ptrs.size()));
        System::MemoryAllocator::_singleton = savedSingleton;
        ++pair.step;
        pair.Compare(nullptr, nullptr);
    }
    pair.CompareArenas();
    System::g_memoryAllocatorInstrument = savedSeat;
    CHECK(pair.ok);
}
