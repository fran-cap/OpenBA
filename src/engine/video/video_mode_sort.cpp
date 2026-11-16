// ============================================================================
// src/engine/video/video_mode_sort.cpp — Wave 9 lane EVM
//   The two introsort instantiations EnumerateVideoModes runs over the
//   enumerated tree, their two small-range passes, and the DisplayMode swap.
// ============================================================================
//
//   | Body                                    | Retail      | Exported? |
//   |-----------------------------------------|-------------|-----------|
//   | introsort<DisplayMode>  (stride 0x18)   | 0x180057910 | no        |
//   |   its small-range pass                  | 0x180058120 | no        |
//   |   its element swap                      | 0x1800584c0 | no        |
//   | introsort<RefreshRate>  (stride 0x08)   | 0x180057630 | no        |
//   |   its small-range pass                  | 0x180057fa0 | no        |
//
// None of the five is exported in either build. `EnumerateVideoModes` calls the
// two introsorts by name (enumerate_video_modes.cpp).
//
// THIS IS THE SAME ENGINE INTROSORT LANE COLL ALREADY TRANSCRIBED
// ---------------------------------------------------------------
// `src/engine/collection/introsort.cpp` ported `0x1800264a0`, the packfile
// sort. These two are further instantiations of the same template and the
// skeleton was CHECKED against the disassembly rather than assumed to match:
// same 62-entry pending-range stack, same `n <= 8` cutoff, same descending
// median-of-3, same two equal-run walks, same push-the-LARGER-half rule, same
// pop-or-return. The differences are exactly three: the element size, the
// comparator, and whether the small-range pass is inlined (COLL's) or a
// separate function (both of these).
//
// THE ORDER IS DESCENDING in both, and the small-range pass is the giveaway in
// both: it tracks the MINIMUM and swaps it to the HIGH end, then shrinks the
// high bound.
//
// ============================================================================
// THE REFRESH-RATE COMPARATOR IS ALMOST CERTAINLY A RETAIL BUG, AND IT IS
// TRANSCRIBED AS A BUG
// ============================================================================
// Every comparison in 0x180057630 and 0x180057fa0 has this shape:
//
//     MOVD XMM1,[X+4]      ; X.denominator
//     MOVD XMM2,[X]        ; X.numerator
//     MOVD XMM0,[Y]        ; Y.numerator      <-- Y.denominator is NEVER loaded
//     CVTDQ2PS x3
//     DIVSS XMM2,XMM1      ; X.num / X.den
//     DIVSS XMM0,XMM1      ; Y.num / X.den    <-- X's denominator AGAIN
//     COMISS XMM2,XMM0
//
// Both sides are divided by the SAME value, so the division cancels and the
// comparison reduces to `X.numerator > Y.numerator`. The denominators do not
// affect the order at all. Whoever wrote it meant `Y.num / Y.den`.
//
// It is reproduced exactly, divisions included, for two reasons:
//   * a "corrected" comparator would produce a DIFFERENT permutation of the
//     refresh list, silently, and that list feeds the options dialog;
//   * the divisions are not always inert. A zero denominator makes the result
//     inf or NaN, and `COMISS` with a NaN leaves the flags unordered so `JA`
//     and `CMOVA` are NOT taken — a branch a numerator-only compare would take.
//     For numerators above 2^24 the `CVTDQ2PS` rounding can also tie values
//     that an integer compare would separate.
// Folding it to an integer compare would be right in almost every case and
// wrong in exactly the cases that matter.
//
// ============================================================================
// THE DISPLAYMODE SWAP IS A DEEP COPY, NOT A MEMCPY
// ============================================================================
// `DisplayMode` owns an `Array<RefreshRate>`, and retail's swap moves it
// through `Array<RefreshRate>::operator=` three times plus one `~Array`:
//
//     tmp.w/h = a.w/h;  tmp.arr = {};  tmp.arr = a.arr;
//     a.w/h   = b.w/h;               a.arr   = b.arr;
//     b.w/h   = tmp.w/h;             b.arr   = tmp.arr;
//     ~tmp.arr;
//
// So every swap ALLOCATES three buffers and frees one. A memcpy swap would
// produce the same field VALUES and the same final permutation while leaving
// the ORIGINAL `_data` pointers in place, and no contents-only comparison could
// tell the two apart — which is exactly why this is spelled out rather than
// optimised.
//
// Retail writes this three ways: out-of-lined at 0x1800584c0 (called from the
// median-of-3 sites) and INLINED at 0x180057a98 and 0x18005818e (the partition
// and the small-range pass). All three are the same operation, and the inlined
// copies are `Array<RefreshRate>::operator=` open-coded — the same body lane
// VID2 ported at 0x18000fbf0 (`RtVideo_ArrayRefreshAssign`, device_enum_types.cpp),
// self-assignment guard and per-element address null test included. This file
// therefore has ONE swap helper and calls the ported assignment rather than
// re-deriving 150 instructions of inlined allocator code. That is a deliberate
// substitution of an already-verified equal body, not a simplification.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "video/Device.h"

using DisplayMode = Video::Device::DisplayMode;
using RefreshRate = Video::Device::RefreshRate;

// Lane VID2's Array<RefreshRate> ports (device_enum_types.cpp) are declared in
// Device.h and take the DisplayMode's three named fields.

namespace rtvidsort {

// ---------------------------------------------------------------------------
// EFFECTIVENESS COUNTERS — branches taken INSIDE these bodies.
//
// Lane COLL's reasoning applies unchanged and is the reason these exist: "it
// came out sorted" is true of a bubble sort, a std::sort and a broken
// transcription alike, and on a one- or two-element list it is true of doing
// NOTHING AT ALL. Nothing in the tree reads them.
// ---------------------------------------------------------------------------
struct SortCounters {
    uint64_t calls, smallCalls, smallSwaps, medianSwaps;
    uint64_t partitions, partitionSwaps, pivotFixups;
    uint64_t eqRunA, eqRunB, pushes, pops, deepSwaps;
};

SortCounters g_mode = {};
SortCounters g_rate = {};

}  // namespace rtvidsort

namespace {

using rtvidsort::SortCounters;
using rtvidsort::g_mode;
using rtvidsort::g_rate;

// ---------------------------------------------------------------------------
// The two comparators. `Less(X, Y)` is "X sorts before Y", i.e. the condition
// under which retail SWAPS to put X first. Both are DESCENDING.
// ---------------------------------------------------------------------------

// 0x180057ff0 and friends. See the banner: the divisor is X's denominator on
// BOTH sides. Written with `volatile float` so the compiler cannot algebraically
// cancel the two divisions the way the source author evidently expected the
// hardware to; the cancellation is exactly what must NOT happen if a
// denominator is zero.
inline bool LessRate(const RefreshRate* x, const RefreshRate* y) {
    const volatile float den = static_cast<float>(x->denominator);
    const volatile float xr  = static_cast<float>(x->numerator)   / den;
    const volatile float yr  = static_cast<float>(y->numerator)   / den;   // X's den
    return xr > yr;                       // COMISS + JA / CMOVA: false if unordered
}

// 0x1800579c4: lexicographic descending on (width, height).
//     CMP ECX,EAX / JG take / JL skip / CMP heights / JLE skip / take
inline bool LessMode(const DisplayMode* x, const DisplayMode* y) {
    if (x->width  > y->width)  return true;
    if (x->width  < y->width)  return false;
    return x->height > y->height;
}

// ---------------------------------------------------------------------------
// 0x1800584c0 — swap(DisplayMode&, DisplayMode&), the deep one.
// ---------------------------------------------------------------------------
void SwapMode(DisplayMode* a, DisplayMode* b) {
    ++g_mode.deepSwaps;

    DisplayMode tmp;
    tmp.width  = a->width;                       // 0x1800584df
    tmp.height = a->height;                      // 0x1800584e5
    tmp.refreshData     = nullptr;               // 0x1800584ee — ZEROED before
    tmp.refreshCount    = 0;                     //   the assign, so operator=
    tmp.refreshCapacity = 0;                     //   sees an empty destination
    RtVideo_ArrayRefreshAssign(tmp.refreshData, tmp.refreshCount, tmp.refreshCapacity,
                               a->refreshData, a->refreshCount);     // 0x180058501

    a->width  = b->width;                        // 0x180058507
    a->height = b->height;
    RtVideo_ArrayRefreshAssign(a->refreshData, a->refreshCount, a->refreshCapacity,
                               b->refreshData, b->refreshCount);     // 0x180058519

    b->width  = tmp.width;                       // 0x18005851e
    b->height = tmp.height;
    RtVideo_ArrayRefreshAssign(b->refreshData, b->refreshCount, b->refreshCapacity,
                               tmp.refreshData, tmp.refreshCount);   // 0x180058534

    RtVideo_ArrayRefreshDtor(tmp.refreshData, tmp.refreshCount,
                             tmp.refreshCapacity);                   // 0x18005853f
}

// A plain 8-byte swap; retail moves RefreshRate as one qword (0x1800577d4).
inline void SwapRate(RefreshRate* a, RefreshRate* b) {
    const RefreshRate t = *a;
    *a = *b;
    *b = t;
}

// ---------------------------------------------------------------------------
// The small-range pass, both instantiations.
//   0x180057fa0 (RefreshRate) and 0x180058120 (DisplayMode).
//
// A selection sort that finds the MINIMUM under the comparator and swaps it to
// the HIGH end, then shrinks the high bound — which is what makes the whole
// sort descending. `hi` is INCLUSIVE.
//
// TWO TRANSCRIBED DETAILS:
//   * the empty/single-element test is `CMP RDX,RCX / JBE` — an UNSIGNED
//     pointer compare, so it is written as `>` on pointers here;
//   * `scanFrom` is HOISTED before the loop (`LEA RBX,[RCX+8]` / `ADD RCX,0x18`)
//     and is NOT recomputed as `hi` shrinks. Retail's 4x unrolling of the inner
//     scan changes no result and is not reproduced; the unrolled and tail paths
//     make the identical sequence of comparisons in the identical order.
// ---------------------------------------------------------------------------
void SmallSortRate(RefreshRate* lo, RefreshRate* hi) {
    ++g_rate.smallCalls;
    if (!(hi > lo)) return;                       // 0x180057faa CMP/JBE
    RefreshRate* const scanFrom = lo + 1;         // 0x180057fb7, hoisted
    do {
        RefreshRate* best = lo;                   // 0x180057fc0
        for (RefreshRate* p = scanFrom; p <= hi; ++p) {
            if (LessRate(best, p)) best = p;      // CMOVA — tracks the MINIMUM
        }
        ++g_rate.smallSwaps;
        SwapRate(best, hi);                       // 0x1800580e4
        --hi;
    } while (hi > lo);                            // 0x1800580f5 CMP/JA
}

void SmallSortMode(DisplayMode* lo, DisplayMode* hi) {
    ++g_mode.smallCalls;
    if (!(hi > lo)) return;                       // 0x180058120 CMP/JBE
    DisplayMode* const scanFrom = lo + 1;         // 0x18005814f, hoisted
    do {
        DisplayMode* best = lo;                   // 0x180058160
        for (DisplayMode* p = scanFrom; p <= hi; ++p) {
            if (LessMode(best, p)) best = p;      // 0x180058182
        }
        ++g_mode.smallSwaps;
        SwapMode(best, hi);                       // the inlined deep swap
        --hi;
    } while (hi > lo);
}

// ---------------------------------------------------------------------------
// The introsort skeleton, shared by both instantiations.
//
// Register mapping kept in the local names so the disassembly reads alongside:
//   R11/RSI -> lo      inclusive low bound
//   R10/RDI -> hi      INCLUSIVE high bound
//   RBX/R12 -> depth   pending-range stack cursor
//   R8 /R14 -> pivot   a POINTER TO A SLOT; the partition follows it
//   RDX/RBX -> i       forward cursor
//   R9 /R15 -> j       backward cursor
//
// THE PENDING STACK IS 62 ENTRIES WITH NO BOUNDS CHECK, which is safe only
// because the split always PUSHES THE LARGER half and iterates on the smaller.
// Written as retail wrote it, absent bounds check included.
// ---------------------------------------------------------------------------
template <typename T, bool (*Less)(const T*, const T*), void (*Swap)(T*, T*),
          void (*SmallSort)(T*, T*)>
void IntroSort(T* first, int32_t count, SortCounters* sc) {
    ++sc->calls;
    T* stackLo[62];
    T* stackHi[62];
    int64_t depth = 0;

    T* lo = first;
    T* hi = first + (static_cast<int64_t>(count) - 1);   // MOVSXD / DEC

    for (;;) {
        // `(hi - lo) + 1`, truncated to 32 bits by retail's `INC EAX`.
        const int32_t n = static_cast<int32_t>(hi - lo) + 1;

        if (n <= 8) {                                     // CMP EAX,8 / JG
            SmallSort(lo, hi);
            --depth;                                      // the pop
            if (depth < 0) return;
            ++sc->pops;
            lo = stackLo[depth];
            hi = stackHi[depth];
            continue;
        }

        // ---- median of three: lo, mid, hi ----------------------------------
        // `mid = lo + (n >> 1)` on the SIGN-EXTENDED count, not `lo+(hi-lo)/2`.
        T* pivot = lo + (static_cast<int64_t>(n) >> 1);

        if (Less(pivot, lo)) { ++sc->medianSwaps; Swap(lo, pivot); }
        if (Less(hi, lo))    { ++sc->medianSwaps; Swap(lo, hi); }
        if (Less(hi, pivot)) { ++sc->medianSwaps; Swap(pivot, hi); }

        T* i = lo;
        T* j = hi;

        ++sc->partitions;
        for (;;) {
            bool toRetreat = false;

            if (pivot > i) {
                // Advance `i` while the element belongs left, bounded by the
                // PIVOT SLOT.
                for (;;) {
                    ++i;
                    if (i >= pivot) break;
                    if (!Less(pivot, i)) continue;
                    if (pivot > i) toRetreat = true;
                    break;
                }
            }
            if (!toRetreat) {
                // The same advance, bounded by `hi`. Retail reaches this both
                // by falling out of the loop above and by `pivot <= i` at the
                // top of the partition.
                for (;;) {
                    ++i;
                    if (i > hi) break;
                    if (!Less(pivot, i)) continue;
                    break;
                }
            }

            // Retreat `j` while the element belongs right.
            for (;;) {
                --j;
                if (j <= pivot) break;
                if (Less(pivot, j)) continue;
                break;
            }

            if (j < i) break;

            ++sc->partitionSwaps;
            Swap(i, j);
            // If the pivot SLOT was the one just moved out of `j`, follow it to
            // `i`. There is no matching fixup for a pivot sitting at `i`;
            // retail has none and neither does this.
            if (pivot == j) { ++sc->pivotFixups; pivot = i; }
        }

        // ---- walk j back over the run of elements EQUAL to the pivot -------
        // Both walks continue only while the two compare EQUIVALENT under the
        // comparator, tested BOTH WAYS — `!Less(j,pivot) && !Less(pivot,j)` —
        // which is how retail spells equality when it has only a `Less`.
        //
        // THE SECOND WALK IS CONDITIONAL, and getting that wrong is the easy
        // mistake here: retail reaches it ONLY when the first walk ran out of
        // room (`j <= pivot`), never when the first walk stopped on a
        // non-equivalent element with `pivot < j`. That is the `split` flag,
        // and its shape is taken from lane COLL's already-verified
        // transcription of the SAME template at 0x1800264a0
        // (src/engine/collection/introsort.cpp) rather than re-derived, after
        // checking that this instantiation's branch targets agree with it.
        ++j;
        bool split = false;
        if (pivot < j) {
            for (;;) {
                --j;
                if (j <= pivot) break;
                ++sc->eqRunA;
                if (Less(j, pivot)) break;
                if (!Less(pivot, j)) continue;
                break;
            }
            if (pivot < j) split = true;
        }
        if (!split) {
            // The same walk, bounded by `lo` instead of by the pivot slot.
            for (;;) {
                --j;
                if (j <= lo) break;
                ++sc->eqRunB;
                if (Less(j, pivot)) break;
                if (!Less(pivot, j)) continue;
                break;
            }
        }

        // ---- push the LARGER half, iterate on the smaller ------------------
        // Retail computes both spans in BYTES and masks the low bits off
        // (`AND ...,-8`), then compares with a SIGNED `JL`. Kept in that form:
        // the mask is a no-op on well-formed pointers and the signedness is
        // what the mask exists to make safe. The byte span of two slots of one
        // array is their element distance times the element size.
        const int64_t rightBytes = (static_cast<int64_t>(hi - i) *
                                    static_cast<int64_t>(sizeof(T))) & ~static_cast<int64_t>(7);
        const int64_t leftBytes  = (static_cast<int64_t>(j - lo) *
                                    static_cast<int64_t>(sizeof(T))) & ~static_cast<int64_t>(7);

        if (leftBytes >= rightBytes) {
            if (lo < j) { ++sc->pushes; stackLo[depth] = lo; stackHi[depth] = j; ++depth; }
            if (i >= hi) {
                --depth;
                if (depth < 0) return;
                ++sc->pops;
                lo = stackLo[depth];
                hi = stackHi[depth];
                continue;
            }
            lo = i;
            continue;
        }

        if (i < hi) { ++sc->pushes; stackLo[depth] = i; stackHi[depth] = hi; ++depth; }
        if (lo >= j) {
            --depth;
            if (depth < 0) return;
            ++sc->pops;
            lo = stackLo[depth];
            hi = stackHi[depth];
            continue;
        }
        hi = j;
    }
}

}  // namespace

// ============================================================================
// The two entry points EnumerateVideoModes calls.
// ============================================================================
// Retail's third argument (`LEA R8,[RSP+0x88]`) is DEAD — the callee never
// reads R8. Two parameters, which is what the call site needs and what lane
// COLL's precedent at 0x1800264a0 already established for the same template.

// 0x180057910 — sort `count` DisplayModes descending by (width, height).
extern "C" void RtVideo_SortDisplayModes(DisplayMode* first, int32_t count) {
    IntroSort<DisplayMode, LessMode, SwapMode, SmallSortMode>(first, count, &g_mode);
}

// 0x180057630 — sort `count` RefreshRates descending by numerator (see the
// banner: the denominators cancel, and that is retail's bug, not ours).
extern "C" void RtVideo_SortRefreshRates(RefreshRate* first, int32_t count) {
    IntroSort<RefreshRate, LessRate, SwapRate, SmallSortRate>(first, count, &g_rate);
}
