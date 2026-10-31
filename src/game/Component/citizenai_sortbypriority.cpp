// src/game/Component/citizenai_sortbypriority.cpp -- CitizenAI's candidate sort (0x14002fe10)
//
// Moved out of citizenai_candidatelist.cpp unchanged so the unit target can link it
// without the component (tests/unit/citizenai_sort_test.cpp checks it against the ported
// CRT qsort, RtCrt_Qsort_18007f050). Its one caller is
// CitizenAIComponent::BuildCandidateStateList (citizenai_candidatelist.cpp).

#include <cstdint>
#include "Component/StateCandidate.h"
#include "Component/exe_bodies.h"

// ============================================================
// 1. SortByPriority (0x14002fe10)
// ------------------------------------------------------------
// Sorts the 12-byte {id, priority, unused} candidates DESCENDING by priority, in place.
// Retail's body is the MSVC CRT qsort (qsort.c) instantiated for this record with the
// comparator inlined: "a sorts after b" <=> a.priority < b.priority. It is NOT stable:
// candidates of equal priority leave in an order the partition and the shortsort decide,
// and TransitionState (0x14010db90) enters the first one whose handler takes, so the
// order among ties picks the state (row 290). The steps, each against the disassembly:
//   0x14002fe50  size = (hi - lo) / 12 + 1; size <= 8 -> the shortsort:
//   0x14002fe80    repeatedly find the LOWEST priority in [lo, hi] (the first of equals:
//                  CMOVG only on strictly lower) and swap it to hi, hi -= 12
//   0x14002feef  else mid = lo + (size >> 1) * 12; median of three: swap lo/mid,
//                lo/hi, mid/hi when the second has the higher priority
//   0x14002ff90  partition: loguy runs up while its priority >= mid's (bounded by mid,
//                then by hi); higuy runs down while its priority < mid's (bounded by
//                mid); swap; mid follows the element it named
//   0x14003000c  higuy skips the run equal to mid's priority (bounded by mid, then lo)
//   0x14003004c  push the larger side on the 62-entry stack, loop on the smaller
// The caller only calls it with count > 1 (0x14010f1ba CMP EDX,1; JLE); retail has no test inside.
// ============================================================
// (StateCandidate: Component/StateCandidate.h.)

static inline bool SortsAfter(const StateCandidate* a, const StateCandidate* b) {
    return a->priority < b->priority;   // the inlined comparator's "> 0"
}
static inline bool SameRank(const StateCandidate* a, const StateCandidate* b) {
    return a->priority == b->priority;  // the inlined comparator's "== 0"
}
static inline void SwapCandidates(StateCandidate* a, StateCandidate* b) {
    if (a == b) return;
    StateCandidate t = *a;
    *a = *b;
    *b = t;
}

void CitizenAI_SortByPriority(StateCandidate* base, int count) {
    StateCandidate* loStack[62];   // RSP+0x000
    StateCandidate* hiStack[62];   // RSP+0x1f0
    int64_t top = 0;

    StateCandidate* lo = base;
    StateCandidate* hi = base + (count - 1);
    for (;;) {
        const int64_t size = (hi - lo) + 1;
        if (size <= 8) {
            // shortsort (0x14002fe70)
            while (hi > lo) {
                StateCandidate* max = lo;
                for (StateCandidate* p = lo + 1; p <= hi; ++p)
                    if (SortsAfter(p, max)) max = p;
                SwapCandidates(max, hi);
                --hi;
            }
        } else {
            StateCandidate* mid = lo + (size >> 1);
            if (SortsAfter(lo, mid)) SwapCandidates(lo, mid);
            if (SortsAfter(lo, hi)) SwapCandidates(lo, hi);
            if (SortsAfter(mid, hi)) SwapCandidates(mid, hi);

            StateCandidate* loguy = lo;
            StateCandidate* higuy = hi;
            for (;;) {
                if (mid > loguy) {
                    do { ++loguy; } while (loguy < mid && !SortsAfter(loguy, mid));
                }
                if (mid <= loguy) {
                    do { ++loguy; } while (loguy <= hi && !SortsAfter(loguy, mid));
                }
                do { --higuy; } while (higuy > mid && SortsAfter(higuy, mid));
                if (higuy < loguy) break;
                SwapCandidates(loguy, higuy);
                if (mid == higuy) mid = loguy;
            }

            ++higuy;
            if (mid < higuy) {
                do { --higuy; } while (higuy > mid && SameRank(higuy, mid));
            }
            if (mid >= higuy) {
                do { --higuy; } while (higuy > lo && SameRank(higuy, mid));
            }

            if ((higuy - lo) >= (hi - loguy)) {
                if (lo < higuy) { loStack[top] = lo; hiStack[top] = higuy; ++top; }
                if (loguy < hi) { lo = loguy; continue; }
            } else {
                if (loguy < hi) { loStack[top] = loguy; hiStack[top] = hi; ++top; }
                if (lo < higuy) { hi = higuy; continue; }
            }
        }
        if (--top < 0) return;
        lo = loStack[top];
        hi = hiStack[top];
    }
}
