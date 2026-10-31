// ============================================================================
// tests/unit/citizenai_sort_test.cpp -- CitizenAI_SortByPriority (0x14002fe10) is retail's qsort
// ============================================================================
// Retail's 0x14002fe10 is the MSVC CRT qsort instantiated for the 12-byte {id, priority,
// unused} candidate with the comparator inlined ("a sorts after b" <=> a.priority <
// b.priority, descending). The order among equal priorities is observable -- TransitionState
// (0x14010db90) enters the first candidate whose handler takes (known_port_issues row
// 290) -- so the port must reproduce the CRT's swap sequence, not merely sort.
//
// The expected order never comes from the port under test:
//   * a hand-traced shortsort case (below), worked from qsort.c's shortsort, and
//   * a cross-check against RtCrt_Qsort_18007f050, the separately ported CRT qsort
//     (0x18007f050, hash-identical to the exe's 0x14003ae10), itself tested by
//     msvc_sort_skeleton_test.cpp, called with the same comparator as a function.
// Covered: sizes 2..8 (the shortsort), 9 and up (median of three, partition, the stack),
// all-equal priorities, few-valued priorities, and input already descending, ascending and
// organ-pipe shaped. Every record carries its start index in `unused`, so a permutation
// that differs only among ties is caught.
// ============================================================================
#include "unit.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Component/StateCandidate.h"
#include "Component/exe_bodies.h"   // CitizenAI_SortByPriority (0x14002fe10)
#include "rt_libm.h"                // RtCrt_Qsort_18007f050

namespace {

// The inlined comparator as the CRT calls it: > 0 when a sorts after b.
int CandidateCompare(const void* a, const void* b) {
    const int32_t pa = static_cast<const StateCandidate*>(a)->priority;
    const int32_t pb = static_cast<const StateCandidate*>(b)->priority;
    if (pa < pb) return 1;
    if (pa > pb) return -1;
    return 0;
}

struct Lcg {
    uint32_t s;
    uint32_t Next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
};

std::vector<StateCandidate> Make(int n, int pool, Lcg& r) {
    std::vector<StateCandidate> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] = StateCandidate{ 100 + i, static_cast<int32_t>(r.Next() % pool), i };
    return v;
}

bool SameBytes(const std::vector<StateCandidate>& a, const std::vector<StateCandidate>& b) {
    return a.size() == b.size() &&
           (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(StateCandidate)) == 0);
}

bool MatchesCrtQsort(const std::vector<StateCandidate>& in) {
    std::vector<StateCandidate> viaQsort = in;
    std::vector<StateCandidate> viaPort = in;
    RtCrt_Qsort_18007f050(viaQsort.data(), viaQsort.size(), sizeof(StateCandidate), &CandidateCompare);
    CitizenAI_SortByPriority(viaPort.data(), static_cast<int>(viaPort.size()));
    return SameBytes(viaQsort, viaPort);
}

}  // namespace

// Hand trace, qsort.c's shortsort (size <= 8): repeatedly find the element that sorts
// LAST in [lo, hi] -- here the lowest priority, the first of equals, since only a strictly
// lower one replaces it -- swap it to hi, and shrink hi.
//   start            ids 0 1 2 3, priorities 5 5 3 5
//   [0..3] last = 2  (the 3)       swap 2,3 -> ids 0 1 3 2
//   [0..2] last = 0  (all 5, first) swap 0,2 -> ids 3 1 0 2
//   [0..1] last = 0                swap 0,1 -> ids 1 3 0 2
// A stable descending sort would give 0 1 3 2.
UNIT_TEST(citizenai_sort_shortsort_hand_trace) {
    StateCandidate c[4] = { { 0, 5, 0 }, { 1, 5, 0 }, { 2, 3, 0 }, { 3, 5, 0 } };
    CitizenAI_SortByPriority(c, 4);
    CHECK_EQ(c[0].id, 1);
    CHECK_EQ(c[1].id, 3);
    CHECK_EQ(c[2].id, 0);
    CHECK_EQ(c[3].id, 2);
    std::vector<StateCandidate> again = { { 0, 5, 0 }, { 1, 5, 0 }, { 2, 3, 0 }, { 3, 5, 0 } };
    CHECK(MatchesCrtQsort(again));
}

UNIT_TEST(citizenai_sort_matches_crt_qsort_random) {
    Lcg r{ 290u };
    int mismatches = 0;
    int shortCases = 0;
    int longCases = 0;
    for (int n = 2; n <= 300; ++n) {
        for (int pool : { 1, 2, 3, 7, 50, 1000000 }) {
            if (!MatchesCrtQsort(Make(n, pool, r))) ++mismatches;
            if (n <= 8) ++shortCases;
            else ++longCases;
        }
    }
    CHECK(shortCases > 0);
    CHECK(longCases > 0);
    CHECK_EQ(mismatches, 0);
}

UNIT_TEST(citizenai_sort_matches_crt_qsort_shaped) {
    for (int n : { 2, 3, 8, 9, 10, 16, 17, 64, 257 }) {
        std::vector<StateCandidate> equal(static_cast<size_t>(n));
        std::vector<StateCandidate> descending(static_cast<size_t>(n));
        std::vector<StateCandidate> ascending(static_cast<size_t>(n));
        std::vector<StateCandidate> organ(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const size_t k = static_cast<size_t>(i);
            equal[k] = StateCandidate{ i, 7, i };
            descending[k] = StateCandidate{ i, (n - i) / 3, i };   // already in the sort's order
            ascending[k] = StateCandidate{ i, i / 3, i };          // the reverse of it
            organ[k] = StateCandidate{ i, i < n / 2 ? i % 4 : (n - i) % 4, i };
        }
        CHECK(MatchesCrtQsort(equal));
        CHECK(MatchesCrtQsort(descending));
        CHECK(MatchesCrtQsort(ascending));
        CHECK(MatchesCrtQsort(organ));
    }
}

UNIT_TEST(citizenai_sort_is_descending) {
    Lcg r{ 291u };
    for (int n : { 2, 8, 9, 40, 300 }) {
        std::vector<StateCandidate> v = Make(n, 5, r);
        CitizenAI_SortByPriority(v.data(), n);
        bool ordered = true;
        for (int i = 1; i < n; ++i)
            if (v[static_cast<size_t>(i - 1)].priority < v[static_cast<size_t>(i)].priority) ordered = false;
        CHECK(ordered);
    }
}
