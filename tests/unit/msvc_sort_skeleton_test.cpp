// ============================================================================
// tests/unit/msvc_sort_skeleton_test.cpp -- the two ported MSVC sorts agree, ties included
// ============================================================================
// The exe sorts with two copies of one MSVC algorithm: the CRT qsort (0x14003ae10,
// hash-identical to the Runtime's 0x18007f050, ported as RtCrt_Qsort_18007f050) and the
// inlined template instantiations (0x14002c070 pickup candidates, 0x1400302a0 workplaces
// by profession) that rtsort::Introsort + rtsort::SelectionSortMaxToHigh transcribe.
// Both are unstable; where two elements compare equal it is the swap sequence that decides
// which lands first, and the game reads that order (which pile is hauled first, which
// workplace is filled first). These tests sort records whose key ties often and whose
// payload is never compared, through both ports, and require the same permutation:
//   * an int key ascending (the pickup and tile-distance comparators),
//   * lengths 1..300 (both sides of the 8-element cutoff and deep partitions; the skeleton
//     is never handed a null array, whose `first - 1` would wrap past its unsigned test),
//   * all-equal keys, sorted, reverse-sorted and two-valued inputs;
// and check against std::sort that the tie order genuinely differs somewhere, so a
// regression to std::sort at a game site is a behaviour change, not a no-op.
// ============================================================================
#include "unit.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "collection/introsort_msvc.h"
#include "rt_libm.h"   // RtCrt_Qsort_18007f050

namespace {

struct Rec {
    int32_t key;
    int32_t payload;   // never compared: it records where the element started
};

int RecCompare(const void* a, const void* b) {
    return static_cast<const Rec*>(a)->key - static_cast<const Rec*>(b)->key;
}

void SortSkeleton(Rec* data, int32_t count) {
    auto less = [](const Rec& a, const Rec& b) { return a.key < b.key; };
    auto swp = [](Rec* a, Rec* b) {
        const Rec t = *a;
        *a = *b;
        *b = t;
    };
    rtsort::Introsort(data, count, less, swp,
                      [&](Rec* lo, Rec* hi) { rtsort::SelectionSortMaxToHigh(lo, hi, less, swp); });
}

struct Lcg {
    uint32_t s;
    uint32_t Next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
};

std::vector<Rec> Make(int n, int pool, Lcg& r) {
    std::vector<Rec> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = Rec{ static_cast<int32_t>(r.Next() % pool), i };
    return v;
}

bool SameBytes(const std::vector<Rec>& a, const std::vector<Rec>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(Rec)) == 0);
}

bool BothAgree(const std::vector<Rec>& in) {
    std::vector<Rec> viaQsort = in;
    std::vector<Rec> viaSkeleton = in;
    RtCrt_Qsort_18007f050(viaQsort.data(), viaQsort.size(), sizeof(Rec), &RecCompare);
    SortSkeleton(viaSkeleton.data(), static_cast<int32_t>(viaSkeleton.size()));
    return SameBytes(viaQsort, viaSkeleton);
}

}  // namespace

UNIT_TEST(msvc_sort_skeleton_matches_crt_qsort_random) {
    Lcg r{ 12345u };
    int mismatches = 0;
    for (int n = 1; n <= 300; ++n) {
        for (int pool : { 1, 2, 3, 7, 50, 1000000 }) {
            if (!BothAgree(Make(n, pool, r))) ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}

UNIT_TEST(msvc_sort_skeleton_matches_crt_qsort_shaped) {
    for (int n : { 2, 8, 9, 16, 17, 64, 257 }) {
        std::vector<Rec> sorted(static_cast<size_t>(n));
        std::vector<Rec> reversed(static_cast<size_t>(n));
        std::vector<Rec> organ(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            sorted[static_cast<size_t>(i)] = Rec{ i / 3, i };
            reversed[static_cast<size_t>(i)] = Rec{ (n - i) / 3, i };
            organ[static_cast<size_t>(i)] = Rec{ i < n / 2 ? i % 4 : (n - i) % 4, i };
        }
        CHECK(BothAgree(sorted));
        CHECK(BothAgree(reversed));
        CHECK(BothAgree(organ));
    }
}

UNIT_TEST(msvc_sort_skeleton_tie_order_is_not_std_sort) {
    // On tie-heavy input the MSVC order differs from libstdc++'s std::sort at least once;
    // if it never did, routing a site through the skeleton would change nothing.
    Lcg r{ 777u };
    bool differs = false;
    for (int trial = 0; trial < 200 && !differs; ++trial) {
        std::vector<Rec> in = Make(40, 3, r);
        std::vector<Rec> viaSkeleton = in;
        std::vector<Rec> viaStd = in;
        SortSkeleton(viaSkeleton.data(), static_cast<int32_t>(viaSkeleton.size()));
        std::sort(viaStd.begin(), viaStd.end(), [](const Rec& a, const Rec& b) { return a.key < b.key; });
        differs = !SameBytes(viaSkeleton, viaStd);
    }
    CHECK(differs);
}
