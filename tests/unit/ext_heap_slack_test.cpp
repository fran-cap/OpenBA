// ============================================================================
// tests/unit/ext_heap_slack_test.cpp -- ext.heap_slack (docs/extensions/handins/HEAPSLACK.md)
// ============================================================================
// The real allocator (src/engine/system/allocator.cpp) over the real platform seam, run twice
// on the same scripted alloc / free sequence: once with MemoryAllocator::_extCommitSeam null
// (retail's policy, the reference) and once with ext.heap_slack's seam at several budgets. Every
// block is hashed when it is handed out, BEFORE the script writes it, then filled with a pattern,
// so a stale page handed back where retail would have handed a fresh (zero) one changes a hash.
// The trace of (range, offset, size, hash) must be identical, and so must every range's retail
// committed mark at the end. The script frees mostly from the top of the heap, which is what
// moves the marks, and grows past the first range so a second range is used.
// Runs on every platform the unit target builds on (the seam sits above the platform twins). Every
// size, the range size and the budgets are in units of the platform's page (MemoryAllocator::
// pageSize after Startup: 4 KB on Windows and x64 Linux, 16 KB on Apple silicon), so the script
// crosses page marks on each; a script that never made retail decommit fails, never passes silently.
// ============================================================================
#include "unit.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "ext/heap_slack/heap_slack.h"
#include "system/MemoryAllocator.h"

namespace {

struct Handed {
    int32_t  range;
    uint64_t offset;
    uint64_t size;
    uint64_t hash;
};

struct Outcome {
    std::vector<Handed>   trace;
    std::vector<uint64_t> committed;   // retail's mark per range at the end
    uint64_t              page = 0;    // the allocator's page granularity
};

// The platform's page granularity, as the allocator sees it (Startup reads it from the seam).
uint64_t PlatformPage() {
    RtSystemAllocatorInstallPlatform();
    System::MemoryAllocator a{};
    a.minBin = 0xbf;
    CHECK(a.Startup(1, 0));
    const uint64_t page = a.pageSize;
    a.Shutdown();
    return page;
}

// FNV-1a over 8-byte words, then the tail bytes: the script hashes every handed byte (megabytes per
// run, five runs); in the -O0 unit build the byte-at-a-time loop was ~80% of this file's time. Each
// step is a bijection of h, so two blocks that differ in any word hash differently unless a later
// difference cancels it.
uint64_t Fnv(const uint8_t* p, uint64_t n) {
    uint64_t h = 1469598103934665603ull;
    uint64_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        std::memcpy(&w, p + i, sizeof(w));
        h ^= w;
        h *= 1099511628211ull;
        h ^= h >> 29;
    }
    for (; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

int32_t RangeOf(const System::MemoryAllocator& a, const uint8_t* p, uint64_t* offset) {
    for (int32_t i = 0; i < a.rangeCount; ++i) {
        if (a.ranges[i].base <= p && p < a.ranges[i].end) {
            *offset = static_cast<uint64_t>(p - a.ranges[i].base);
            return i;
        }
    }
    return -1;
}

// Sizes in pages: under one page, 2 .. 66 pages, 64 .. 320 pages, 256 .. 1024 pages.
uint64_t DrawSize(std::mt19937& rng, uint64_t page) {
    const uint32_t kind = rng() % 100;
    if (kind < 55) return 1 + rng() % page;
    if (kind < 90) return 2 * page + rng() % (64 * page);
    if (kind < 98) return 64 * page + rng() % (256 * page);
    return 256 * page + rng() % (768 * page);
}

// The script: `steps` operations from a fixed seed. Allocation sizes and the choice of what to
// free are drawn from the seed only, never from addresses, so both runs make the same calls.
Outcome RunScript(const rt::system::MemoryAllocatorCommitSeam* seam, uint64_t budget, int steps,
                  uint64_t page) {
    RtSystemAllocatorInstallPlatform();
    if (seam != nullptr) Ext::HeapSlack::Reset(budget);
    System::MemoryAllocator::_extCommitSeam = seam;

    System::MemoryAllocator a{};
    a.minBin = 0xbf;
    // 2048-page ranges (8 MB at 4 KB, 32 MB at 16 KB): the script's peak needs at least two.
    CHECK(a.Startup((2048 * page) >> 20, 0));
    CHECK_EQ(a.pageSize, page);

    std::mt19937 rng(20261024);
    std::vector<uint8_t*> live;
    Outcome out;
    out.page = page;
    uint8_t fill = 1;
    for (int step = 0; step < steps; ++step) {
        const uint32_t op = rng() % 100;
        const bool doFree = !live.empty() && (op < 45 || live.size() > 60);
        if (doFree) {
            // Mostly the newest block (the top of the heap), sometimes any.
            size_t idx = live.size() - 1;
            if (rng() % 4 == 0) idx = rng() % live.size();
            a.Free(live[idx]);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
            continue;
        }
        const uint64_t size = DrawSize(rng, page);
        const int align = (rng() % 3 == 0) ? 16 : ((rng() % 2) ? 8 : 4);
        uint8_t* p = static_cast<uint8_t*>(a.Allocate(size, align));
        Handed h = {};
        h.size = size;
        h.range = RangeOf(a, p, &h.offset);
        h.hash = Fnv(p, size);
        out.trace.push_back(h);
        std::memset(p, fill, static_cast<size_t>(size));   // stale bytes for whoever reuses them
        fill = static_cast<uint8_t>(fill == 255 ? 1 : fill + 1);
        live.push_back(p);
    }
    for (int32_t i = 0; i < a.rangeCount; ++i) out.committed.push_back(a.ranges[i].committed);
    a.Shutdown();
    System::MemoryAllocator::_extCommitSeam = nullptr;
    return out;
}

bool SameOutcome(const Outcome& x, const Outcome& y) {
    if (x.trace.size() != y.trace.size() || x.committed != y.committed) return false;
    for (size_t i = 0; i < x.trace.size(); ++i) {
        const Handed& a = x.trace[i];
        const Handed& b = y.trace[i];
        if (a.range != b.range || a.offset != b.offset || a.size != b.size || a.hash != b.hash) return false;
    }
    return true;
}

constexpr int kSteps = 6000;

}  // namespace

UNIT_TEST(ext_heap_slack_every_handed_byte_matches_retail) {
    const uint64_t page = PlatformPage();
    std::printf("  heap_slack: page %llu bytes\n", static_cast<unsigned long long>(page));
    const Outcome reference = RunScript(nullptr, 0, kSteps, page);
    CHECK(reference.committed.size() >= 2);   // the script did reach a second range

    // 0 first: it is retail's policy with every call counted, so it says whether the script made
    // retail decommit and recommit at all. If not, nothing below proves anything: fail loudly.
    const uint64_t budgets[] = { 0, Ext::HeapSlack::kBudgetBytes, 256 * page, 3 * page };
    for (uint64_t budget : budgets) {
        const Outcome slack = RunScript(Ext::HeapSlack::Seam(), budget, kSteps, page);
        CHECK(SameOutcome(reference, slack));
        const Ext::HeapSlack::Stats s = Ext::HeapSlack::Read();
        CHECK_EQ(s.untracked, 0ull);
        CHECK(s.peakSlack <= budget);
        if (s.retailDecommits == 0 || s.retailCommits == 0) {
            std::printf("  FAIL %s:%d: the script never crossed a page mark (page %llu, budget %llu: "
                        "retail commits %llu, decommits %llu); resize it for this platform\n",
                        __FILE__, __LINE__, static_cast<unsigned long long>(page),
                        static_cast<unsigned long long>(budget),
                        static_cast<unsigned long long>(s.retailCommits),
                        static_cast<unsigned long long>(s.retailDecommits));
            ++unit::Failures();
            return;
        }
        if (budget == 0) {
            // A zero budget is retail's policy: every call reaches the platform, nothing is zeroed.
            CHECK_EQ(s.commits, s.retailCommits);
            CHECK_EQ(s.decommits, s.retailDecommits);
            CHECK_EQ(s.zeroedBytes, 0ull);
        } else {
            // The marks were crossed: pages kept and zeroed instead of a platform round trip.
            CHECK(s.zeroedBytes > 0);
            CHECK(s.decommits < s.retailDecommits);
            CHECK(s.commits < s.retailCommits);
        }
    }
}

UNIT_TEST(ext_heap_slack_kept_pages_read_zero_when_retail_recommits) {
    // One block at the top of a fresh range: write it, free it (retail decommits down to the range's
    // first page), allocate it again: retail hands out fresh pages, so must the seam.
    const uint64_t page = PlatformPage();
    Ext::HeapSlack::Reset(Ext::HeapSlack::kBudgetBytes);
    System::MemoryAllocator::_extCommitSeam = Ext::HeapSlack::Seam();
    System::MemoryAllocator a{};
    a.minBin = 0xbf;
    CHECK(a.Startup((1024 * page) >> 20, 0));   // 4 MB at 4 KB pages, 16 MB at 16 KB
    CHECK_EQ(a.pageSize, page);
    const uint64_t n = 128 * page;
    uint8_t* p = static_cast<uint8_t*>(a.Allocate(n, 8));
    std::memset(p, 0xAB, static_cast<size_t>(n));
    a.Free(p);
    const Ext::HeapSlack::Stats afterFree = Ext::HeapSlack::Read();
    CHECK(afterFree.retailDecommits >= 1);
    CHECK_EQ(afterFree.decommits, 0ull);   // kept
    uint8_t* q = static_cast<uint8_t*>(a.Allocate(n, 8));
    CHECK(q == p);
    // The first page stayed committed in retail too (the range's head tag lives there): it keeps
    // the stale bytes. Every byte past it reads zero.
    const uint64_t firstFresh = page - static_cast<uint64_t>(q - a.ranges[0].base);
    bool zero = true;
    for (uint64_t i = firstFresh; i < n; ++i) zero = zero && q[i] == 0;
    CHECK(zero);
    CHECK(Ext::HeapSlack::Read().zeroedBytes > 0);
    a.Shutdown();
    System::MemoryAllocator::_extCommitSeam = nullptr;
}
