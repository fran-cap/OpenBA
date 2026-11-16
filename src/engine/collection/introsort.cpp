// ============================================================================
// src/engine/collection/introsort.cpp — Wave 9 lane COLL
//   0x1800264a0 — the packfile sort, and 0x180051650 / 0x180050d30, the last
//   two element copy-assign helpers lane Q left on the DLL.
// ============================================================================
//
// ============================================================================
// PART 1 — 0x1800264a0, the packfile introsort
// ============================================================================
//
// WHY IT IS HERE, GIVEN LANE PKG DELIBERATELY DECLINED IT
// -------------------------------------------------------
// src/engine/io/filesystem_packages.cpp, above the SortByKey70 call, quotes the
// paragraph that declined it, and it was right at the time:
//
//     "DELIBERATELY NOT PORTED ... every PackageFile arrives here with the
//      SAME +0x70 ... A tie-heavy population is exactly where an unstable
//      sort's exact swap sequence decides the answer, and that answer decides
//      which package owns a colliding path in LOOP 3. A 'clean' std::sort here
//      would compile, run, and silently pick different winners. With no run
//      and no oracle available in this lane, transcribing it would be an
//      unverified guess at a precedence rule."
//
// Every clause of that is correct and none of it is an argument against
// porting; it is an argument against porting it BY RESEMBLANCE and without an
// oracle. Both objections are answerable, and this file answers them:
//
//   * It is transcribed instruction by instruction from 0x1800264a0, not
//     written as "an introsort". The pending-range stack, the median-of-3
//     order, the two equal-run scans and the push-the-larger-half rule are all
//     retail's, and each is annotated with the instruction it came from.
//   * A sort IS replayable, which makes it one of the easiest things in this
//     wave to verify: it is a pure permutation of a buffer the caller owns.
//     The unit test SortByKey70TieReplay (tests/unit/list_test.cpp) compares
//     the resulting PERMUTATION, not the sortedness, against permutations
//     recorded from this body. On a tie-heavy population that is exactly the
//     "which package wins" question lane PKG could not answer, and the test's
//     corpus is mostly ties.
//
// THE ORDER IS DESCENDING, and that is the first thing to get wrong.
// `CMOVG RDX,R8` at 0x1800264fc moves the tracked position to R8 when the
// TRACKED element's key is GREATER, so the small-range pass tracks the
// MINIMUM and swaps it to the HIGH end (0x180026509..0x180026516), then
// shrinks the high bound. The partition's scans agree: the left scan advances
// while `pivot->key <= (*i)->key`, i.e. while the element belongs on the left,
// which for a descending order means "is at least the pivot".
//
// THE COMPARATOR IS THE WHOLE OF `CMP dword ptr [RCX+0x70],EAX` — a SIGNED
// 32-bit compare of one field. There is no tiebreak, no secondary key, and no
// pointer comparison. Every ordering decision below is that one compare.
//
// THE THIRD ARGUMENT IS DEAD. The call site passes `LEA R8,[RBP+0x48]`; the
// callee never reads R8. The signature keeps two parameters, as Collection.h
// declares it and filesystem_packages.cpp calls it.
//
// THE PENDING-RANGE STACK IS 62 ENTRIES AND HAS NO BOUNDS CHECK
// (`SUB RSP,0x3e0` gives 0x1f0 bytes of lo and 0x1f0 of hi). That is safe only
// because the split always PUSHES THE LARGER half and iterates on the smaller
// (0x180026653 `CMP RCX,RDX / JL`), which bounds the depth by log2(n). Written
// as retail wrote it — including the absent bounds check.
//
// ============================================================================
// PART 2 — 0x180051650 (and 0x180050d30, now Collection::Array<T>::operator=)
// ============================================================================
//
// src/engine/collection/array.cpp, in the banner above Assign38Call, records
// both rows. 0x180051650 closes here;
// 0x180050d30 is the header's Array<EmitterInstance>::operator= (B9 RT, D4).
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "collection/Collection.h"
#include "avl_trees.h"
#include "system/MemoryAllocator.h"   // B3R: MemAlloc / MemFree by name
#include "audio/Audio.h"   // B.4: Audio::Scene::EmitterInstance / AttenutedEffect
#include "io/PackageFile.h"   // B.4 S4: the sorted element, PackageFile::_priority

namespace rtsort {

// ---------------------------------------------------------------------------
// EFFECTIVENESS COUNTERS — branches taken INSIDE this body.
//
// A sort is the easiest place in the world to write a control that measures
// nothing: "it came out sorted" is true of a bubble sort, a std::sort and a
// broken transcription alike, and on the four-element all-ties population the
// boot actually runs it is true of doing NOTHING AT ALL. So these count the
// structural branches whose absence would make a green permutation comparison
// vacuous — the big-range path, the pivot fixup, the two equal-run walks and
// the pending stack. Nothing in the tree reads them.
// ---------------------------------------------------------------------------
struct SortCounters {
    uint64_t calls, smallPasses, smallSwaps, medianSwaps;
    uint64_t partitions, partitionSwaps, pivotFixups;
    uint64_t eqRunA, eqRunB, pushes, pops;
};
SortCounters g_sc = {};

// The one field the comparator reads: PackageFile::_priority at +0x70, a
// SIGNED dword the PackageFile constructor seeds with 0x2710 and only
// ReadPackfile changes (filesystem_packages.cpp, above the SortByKey70 call).
static inline int32_t Key(const IO::PackageFile* elem) {
    return elem->_priority;
}

// ---------------------------------------------------------------------------
// 0x1800264a0 — sort `count` pointers at `first`, DESCENDING by +0x70.
//
// The register mapping, kept in the local names so the disassembly can be read
// next to this:
//
//   R11 -> lo     the low bound of the range being worked, inclusive
//   R10 -> hi     the high bound, INCLUSIVE (retail seeds it first+count-1)
//   RBX -> depth  the pending-range stack cursor
//   R9  -> pivot  a POINTER TO A SLOT, not a value — the partition moves it
//   RAX -> i      the forward cursor
//   R8  -> j      the backward cursor
// ---------------------------------------------------------------------------
void SortByKey70(IO::PackageFile**first, int32_t count) {
    ++g_sc.calls;
    IO::PackageFile**stackLo[62];
    IO::PackageFile**stackHi[62];
    int64_t depth = 0;

    IO::PackageFile**lo = first;
    IO::PackageFile**hi = first + (static_cast<int64_t>(count) - 1);   // MOVSXD / DEC

    for (;;) {
        // 0x1800264c0 — `(hi - lo)/8 + 1`, truncated to 32 bits by `INC EAX`.
        const int32_t n = static_cast<int32_t>(hi - lo) + 1;

        if (n <= 8) {
            // ---- small range: selection sort, minimum to the HIGH end -----
            // 0x1800264d1 `CMP R10,R11 / JBE` — an empty or single-element
            // range pops immediately. The comparison is UNSIGNED on the
            // pointers, which is why it is written as `>` on pointers here.
            if (hi > lo) {
                IO::PackageFile**const scanFrom = lo + 1;      // 0x1800264d6, HOISTED —
                                                     // it is NOT recomputed as
                                                     // `hi` shrinks
                do {
                    ++g_sc.smallPasses;
                    IO::PackageFile**best = lo;                // 0x1800264e0
                    for (IO::PackageFile**p = scanFrom; p <= hi; ++p) {
                        // 0x1800264f0: CMOVG — move to p only when the tracked
                        // element is STRICTLY greater, so among equal keys the
                        // EARLIEST position wins. That tie rule is the whole
                        // of the "which package" question.
                        if (Key(*best) > Key(*p)) best = p;
                    }
                    // 0x180026509 — swap *best with *hi, then hi--.
                    ++g_sc.smallSwaps;
                    IO::PackageFile* a = *hi;
                    IO::PackageFile* b = *best;
                    --hi;
                    *best     = a;
                    *(hi + 1) = b;
                } while (hi > lo);
            }
            // 0x18002651f — pop.
            --depth;
            if (depth < 0) return;
            ++g_sc.pops;
            lo = stackLo[depth];
            hi = stackHi[depth];
            continue;
        }

        // ---- median of three: lo, mid, hi ---------------------------------
        // 0x180026539 `CDQE / SAR RAX,1` — mid is `lo + n/2`, an ARITHMETIC
        // shift of the SIGN-EXTENDED count, not `lo + (hi-lo)/2`. For a
        // positive count they agree; the sign extension is transcribed because
        // a negative `count` is already a corrupt call and this port should
        // fail the same way.
        IO::PackageFile**pivot = lo + (static_cast<int64_t>(n) >> 1);

        {   // 0x180026542 — if (mid > lo) swap
            IO::PackageFile* a = *lo; IO::PackageFile* b = *pivot;
            if (Key(b) > Key(a)) { ++g_sc.medianSwaps; *lo = b; *pivot = a; }
        }
        {   // 0x180026553 — if (hi > lo) swap
            IO::PackageFile* a = *lo; IO::PackageFile* b = *hi;
            if (Key(b) > Key(a)) { ++g_sc.medianSwaps; *lo = b; *hi = a; }
        }
        {   // 0x180026567 — if (hi > mid) swap
            IO::PackageFile* a = *pivot; IO::PackageFile* b = *hi;
            if (Key(b) > Key(a)) { ++g_sc.medianSwaps; *pivot = b; *hi = a; }
        }

        IO::PackageFile**i = lo;    // RAX
        IO::PackageFile**j = hi;    // R8

        // ---- the partition loop, 0x180026581 ------------------------------
        ++g_sc.partitions;
        for (;;) {
            bool toJ2 = false;      // reached 0x1800265c0 directly

            if (pivot > i) {
                // 0x180026586 — advance i while the element belongs left,
                // bounded by the PIVOT slot.
                for (;;) {
                    ++i;
                    if (i >= pivot) break;                  // JNC 0x1800265a2
                    if (Key(*pivot) <= Key(*i)) continue;   // JLE, keep going
                    // 0x18002659d — stopped on an element that belongs right.
                    if (pivot > i) { toJ2 = true; }
                    break;
                }
            }

            if (!toJ2) {
                // 0x1800265a2 — the same advance, bounded by `hi`. Retail
                // reaches this both by falling out of the loop above and by
                // the `pivot <= i` test at the top of the partition.
                for (;;) {
                    ++i;
                    if (i > hi) break;                      // JA 0x1800265c0
                    if (Key(*pivot) <= Key(*i)) continue;   // JLE, keep going
                    break;
                }
            }

            // 0x1800265c0 — retreat j while the element belongs right.
            for (;;) {
                --j;
                if (j <= pivot) break;                      // JBE 0x1800265d7
                if (Key(*pivot) > Key(*j)) continue;        // JG, keep going
                break;
            }

            // 0x1800265d7
            if (j < i) break;                               // JC 0x1800265f2

            ++g_sc.partitionSwaps;
            IO::PackageFile* a = *j;
            IO::PackageFile* b = *i;
            *i = a;
            *j = b;
            // 0x1800265e8 — if the pivot SLOT was the one just moved out of
            // `j`, follow it to `i`. There is no matching fixup for the pivot
            // sitting at `i`; retail has none and neither does this.
            if (pivot == j) { ++g_sc.pivotFixups; pivot = i; }
        }

        // ---- walk j back over the run of keys EQUAL to the pivot ----------
        // 0x1800265f2. Both loops below continue only while the two keys are
        // EQUAL (`JG` out on greater, `JLE` back on less-or-equal, which after
        // the first test can only be equality).
        ++j;
        bool split = false;
        if (pivot < j) {
            for (;;) {
                --j;
                if (j <= pivot) break;                      // JBE 0x180026622
                ++g_sc.eqRunA;
                const int32_t a = Key(*j), b = Key(*pivot);
                if (a > b) break;                           // JG 0x18002661d
                if (b <= a) continue;                       // JLE 0x180026600
                break;
            }
            // 0x18002661d
            if (pivot < j) split = true;                    // JC 0x18002663f
        }
        if (!split) {
            // 0x180026622 — the same equal-run walk, bounded by `lo`.
            for (;;) {
                --j;
                if (j <= lo) break;                         // JBE 0x18002663f
                ++g_sc.eqRunB;
                const int32_t a = Key(*j), b = Key(*pivot);
                if (a > b) break;                           // JG 0x18002663f
                if (b <= a) continue;                       // JLE 0x180026622
                break;
            }
        }

        // ---- 0x18002663f: push the larger half, iterate on the smaller ----
        // The two lengths are computed in BYTES and masked with `AND ...,-8`,
        // which is a no-op on well-formed pointers and is kept because it is
        // what fixes the sign of the comparison retail then makes with `JL`
        // (SIGNED) rather than `JB`.
        const int64_t rightBytes = (reinterpret_cast<int64_t>(hi) -  // CAST-OK: ptr_int
                                    reinterpret_cast<int64_t>(i)) & ~static_cast<int64_t>(7);  // CAST-OK: ptr_int
        const int64_t leftBytes  = (reinterpret_cast<int64_t>(j) -  // CAST-OK: ptr_int
                                    reinterpret_cast<int64_t>(lo)) & ~static_cast<int64_t>(7);  // CAST-OK: ptr_int

        if (leftBytes >= rightBytes) {
            // 0x180026658
            if (lo < j) { ++g_sc.pushes; stackLo[depth] = lo; stackHi[depth] = j; ++depth; }
            if (i >= hi) {                       // 0x18002666c JNC -> pop
                --depth;
                if (depth < 0) return;
                ++g_sc.pops;
                lo = stackLo[depth];
                hi = stackHi[depth];
                continue;
            }
            lo = i;
            continue;
        }

        // 0x18002667d
        if (i < hi) { ++g_sc.pushes; stackLo[depth] = i; stackHi[depth] = hi; ++depth; }
        if (lo >= j) {                           // 0x180026691 JNC -> pop
            --depth;
            if (depth < 0) return;
            ++g_sc.pops;
            lo = stackLo[depth];
            hi = stackHi[depth];
            continue;
        }
        hi = j;
    }
}

// ============================================================================
// PART 2 — the EffectSlot element copy-assign
// ============================================================================
//
// B9 RT (D4): 0x180050d30, `Collection::Array<Audio::Scene::EmitterInstance>
// ::operator=`, the nested `positions` assign below, was a hand port here
// (`rtsort::Array16Assign`). It is the header's `Array<T>::operator=` /
// `Assign` on the trait-derived row, read against the instructions:
//   CMP RCX,RDX / JZ                        the self-assign early-out
//   CALL 0x180051d80                        the J1 ~Array (trivial element)
//   _size = src._size; the grow rule with the dead `old+1 && old!=0` treble
//   SHL RCX,0x4 / MOV EDX,0x4 / CALL MemAlloc   stride 0x10, ALIGN 4
//                                           (= sizeof / alignof EmitterInstance)
//   ADD R9,RDX / JZ                         the computed-address test
//   four DWORD MOVs 0x180050d99..0x180050db6, +0x00 / +0x04 / +0x08 / +0x0c
//                                           (EmitterInstance's copy: a
//                                           Vector3 and a float, nothing else)
// No free after the copy: the clear already freed the buffer.

// ---------------------------------------------------------------------------
// 0x180051650 — the 0x40-byte `Audio::EffectSlot` element copy-assign.
//
// array.cpp's `Assign40Call` calls this through rtsort::EffectSlotAssign; its
// sibling 0x180004220 is RtColl_SpriteEntryAssign_180004220 (array_destroy.cpp).
//
//   dst[+0x00..+0x0f] = src, as FOUR dword MOVs
//   dst[+0x10] = src[+0x10]   (QWORD)
//   dst[+0x18] = src[+0x18]   (QWORD)
//   dst[+0x20], [+0x24], [+0x28] = src, as THREE dword MOVs
//   dst[+0x30] = 0 ; dst[+0x38] = 0
//   Array<T16>::operator=(dst+0x30, src+0x30)      ; 0x180050d30
//   return dst
//
// *** BYTES +0x2c .. +0x2f ARE NEVER WRITTEN. *** The scalar block stops at
// +0x2b and the nested Array starts at +0x30. array.cpp, above EffectSlotInit, records the same
// hole from the `else` arm's side ("BYTES +0x2c .. +0x2f ARE NEVER WRITTEN"),
// which is an independent second reading of it. Any oracle over this element
// must exclude that window or it fails 100% of calls.
//
// The nested Array header is ZEROED BEFORE the operator= runs, and that is
// load-bearing: on the realloc arm the destination is raw MemAlloc memory and
// `operator=` starts by destroying it.
// ---------------------------------------------------------------------------
static void EffectSlotCopy(Audio::Scene::AttenutedEffect* d, const Audio::Scene::AttenutedEffect* s) {
    d->pos.x          = s->pos.x;          // four DWORD moves
    d->pos.y          = s->pos.y;
    d->pos.z          = s->pos.z;
    d->soundType      = s->soundType;
    d->soundEffectRef = s->soundEffectRef; // QWORD
    d->startTime      = s->startTime;      // QWORD
    d->channelHandle  = s->channelHandle;  // three DWORD moves
    d->idTag          = s->idTag;
    d->baseVolume     = s->baseVolume;
    /* +0x2c .. +0x2f deliberately untouched — retail does not write them */
    d->positions._data     = nullptr;
    d->positions._size     = 0;            // one qword (H7)
    d->positions._capacity = 0;

    // 0x1800516af: CALL 0x180050d30, Array<EmitterInstance>::operator= (the
    // banner above).
    d->positions = s->positions;
}
void EffectSlotAssign(uint8_t* d, const uint8_t* s) {
    rtcoll::AssignAs<Audio::Scene::AttenutedEffect, &EffectSlotCopy>(d, s);
}

}  // namespace rtsort
