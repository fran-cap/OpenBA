#pragma once
// ============================================================================
// src/engine/collection/introsort_msvc.h — BATCH 8b
//
// THE SHIPPED MSVC SORT SKELETON, transcribed once and instantiated per element type.
//
// Runtime-x64.dll contains several copies of one and the same sort template,
// one per element type, each with its comparator and its element swap inlined.
// Three of them are in this tree:
//
//   | retail      | element                    | stride | small-range callee |
//   |-------------|----------------------------|--------|--------------------|
//   | 0x1800264a0 | IO::PackageFile*           |   0x08 | INLINED            |
//   | 0x180062d60 | Graphics::RendererDrawItem |   0x38 | 0x180063b90        |
//   | 0x180028760 | IO::Mod                    |   0x80 | 0x180028fc0        |
//
// 0x1800264a0 was transcribed by hand in introsort.cpp (Wave 9 lane COLL) with
// its small-range pass inlined exactly as retail inlined it; it is left alone.
// The other two DO call a separate small-range function, and that function is
// itself a row of the closure, so each gets a named body of its own
// and is passed in here rather than being re-inlined.
//
// WHAT THE SKELETON IS. It is NOT libstdc++'s introsort and it is NOT
// `std::sort`:
//
//   * ranges of **8 elements or fewer** (`CMP EDX,0x8 / JG`) go to a SELECTION
//     SORT that repeatedly finds the MAXIMUM of [lo,hi], swaps it to `hi` and
//     shrinks `hi`. Ties keep the EARLIER position as the maximum (the tracked
//     position moves only on the `JB` arms, i.e. on STRICTLY less).
//   * larger ranges take a median-of-three (lo, lo + n/2, hi) as three
//     conditional swaps, partition around the pivot SLOT — a pointer that is
//     FOLLOWED when the element under it is swapped away — and then walk the
//     back cursor over the run of elements EQUAL to the pivot.
//   * the two halves go on a 62-entry explicit stack; the LARGER half is
//     pushed and the loop continues on the smaller, which is what bounds the
//     depth by log2(n). There is NO bounds check on that stack and none is
//     added here.
//   * there is no heap-sort fallback and no depth limit. "Introsort" is the
//     shape of the body, not a guarantee it makes.
//
// WHY NOT `std::sort`. Commit d8865c4f ported 0x180062d60 as `std::sort` over a
// reconstructed comparator, on the argument that the comparator is a total
// order over distinguishable items so any correct sort yields the same array.
// Both halves of that were wrong:
//
//   1. The comparator was reconstructed, not read. Its SECOND term, the float
//      at +0x1c, is `COMISS XMM0,XMM1 / JA <swap>` — "A sorts first when A is
//      GREATER", i.e. DESCENDING. The std::sort port had it ascending, so every
//      frame's draw order was wrong in that term. (The other seven terms and
//      their order were right.)
//   2. Comparing equal does NOT make two elements indistinguishable.
//      RendererDrawItem's `arg8` and `_pad24` (+0x20/+0x24) are never compared,
//      and for IO::Mod almost nothing in the 0x80-byte record is. Where two
//      elements compare equal it is the swap sequence that decides which lands
//      first, so the swap sequence has to be retail's.
//
// The transcription keeps retail's control flow, `goto`s included: the four
// scan loops fall into each other at three different points and do not factor
// into structured loops without changing which comparison runs first.
// ============================================================================

#include <cstdint>

namespace rtsort {

// Retail compares the cursors with JB/JA/JBE/JAE — UNSIGNED — throughout.
// Inside one array the signed and unsigned answers agree; the compares are
// written unsigned because that is what the bytes say.
template <class T>
inline bool PtrLt(const T* a, const T* b) {
    return reinterpret_cast<uintptr_t>(a) < reinterpret_cast<uintptr_t>(b);  // CAST-OK: ptr_int
}
template <class T>
inline bool PtrLe(const T* a, const T* b) {
    return reinterpret_cast<uintptr_t>(a) <= reinterpret_cast<uintptr_t>(b);  // CAST-OK: ptr_int
}

// ---------------------------------------------------------------------------
// The small-range pass — 0x180063b90 and 0x180028fc0 (and the copy inlined at
// 0x1800264d1).
//
//   `lo` and `hi` are INCLUSIVE. `hi <= lo` returns having touched nothing:
//   0x180063b90's first two instructions are `CMP RDX,RCX / JBE <ret>`, before
//   the prologue.
//
//   `scanFrom` is hoisted to lo+1 before the outer loop (`LEA R14,[RCX+0x38]`)
//   and is NOT recomputed as `hi` shrinks.
// ---------------------------------------------------------------------------
template <class T, class Less, class Swap>
void SelectionSortMaxToHigh(T* lo, T* hi, Less less, Swap swp) {
    if (PtrLe(hi, lo)) return;                 // 0x180063b90 CMP RDX,RCX / JBE

    T* const scanFrom = lo + 1;                // 0x180063bae LEA R14,[RCX+0x38]
    T* last = hi;                              // RSI

    do {
        T* best = lo;                          // 0x180063bc0 MOV RDI,RBP
        T* p    = scanFrom;                    //             MOV R8,R14
        if (PtrLe(p, last)) {                  // 0x180063bc6 CMP R14,RSI / JA
            do {
                // 0x180063c47 `MOV RDI,R8` sits on the JB arms only, so the
                // tracked position moves only when the tracked element is
                // STRICTLY less than the candidate: among equals the EARLIEST
                // position stays the maximum.
                if (less(*best, *p)) best = p;
                ++p;                           // 0x180063c4a ADD R8,0x38
            } while (PtrLe(p, last));          // 0x180063c4e CMP R8,RSI / JBE
        }
        swp(best, last);                       // 0x180063c57
        --last;                                // 0x180063c7c SUB RSI,0x38
    } while (PtrLt(lo, last));                 // 0x180063cc8 CMP RSI,RBP / JA
}

// ---------------------------------------------------------------------------
// The sort proper — 0x180062d60 and 0x180028760.
//
// Register map, kept in the local names so the listing reads alongside:
//
//   RBP -> lo     inclusive low bound of the range being worked
//   R15 -> hi     inclusive high bound
//   RDI -> pivot  a POINTER TO A SLOT; the partition follows it on a swap
//   RSI -> i      the forward cursor
//   R14 -> j      the backward cursor
//   R12 -> sp     the pending-range stack cursor
//   R13 -> pushes a counter retail increments and NEVER READS. Kept, because
//                 dropping it would be the one place this transcription
//                 silently edits the body.
//
// `smallSort` is the separate small-range callee, passed in rather than
// re-inlined so that its own body is the one that actually runs.
// ---------------------------------------------------------------------------
template <class T, class Less, class Swap, class SmallSort>
void Introsort(T* first, int32_t count, Less less, Swap swp, SmallSort smallSort) {
    T* stackLo[62];                    // RSP+0x20  .. RSP+0x210
    T* stackHi[62];                    // RSP+0x210 .. RSP+0x400
    uint32_t pushes = 0;               // R13D — written, never read
    int64_t  sp     = 0;               // R12

    T* lo = first;                                                  // RBP
    T* hi = first + (static_cast<int64_t>(count) - 1);              // R15

OUTER:                                                              // 0x180062da0
    {
        // (hi - lo)/stride + 1, truncated to 32 bits by `INC EDX`.
        const int32_t n = static_cast<int32_t>(hi - lo) + 1;

        if (n <= 8) {                                               // CMP EDX,0x8 / JG
            smallSort(lo, hi);                                      // CALL 0x180063b90
            goto POP;
        }

        // 0x180062df2 — MOVSXD RDI,EDX / SAR RDI,1: the SIGN-EXTENDED count,
        // shifted arithmetically, not (hi-lo)/2. Identical for a positive
        // count; transcribed as written so a corrupt count fails the same way.
        T* pivot = lo + (static_cast<int64_t>(n) >> 1);              // RDI

        // The swap ARGUMENT ORDER is retail's: at the Mod instantiation the
        // temporary is copy-constructed from the FIRST argument
        // (0x1800287f4 `MOV RDX,RBP / LEA RCX,[RSP+0x1b0] / CALL <copy ctor>`),
        // so which side is `a` decides the order of the refcount operations.
        // The draw-item instantiation swaps seven qwords and does not care.
        if (less(*pivot, *lo)) swp(lo, pivot);                      // 0x180062dee
        if (less(*hi,    *lo)) swp(lo, hi);                         // 0x180062ef5
        if (less(*hi, *pivot)) swp(pivot, hi);                      // 0x180062ff7

        T* i = lo;                                                  // 0x1800630f6 RSI
        T* j = hi;                                                  //             R14

    L_PART:                                                         // 0x180063100
        if (PtrLe(pivot, i)) goto L_SCAN_HI;                        // CMP RDI,RSI / JBE

    L_SCAN_PIVOT:                                                   // 0x180063110
        ++i;
        if (!PtrLt(i, pivot)) goto L_SCAN_HI;                       // CMP RSI,RDI / JAE
        if (!less(*pivot, *i)) goto L_SCAN_PIVOT;                   // the JA / JAE arms
        // 0x18006318c — stopped on an element that belongs on the right.
        if (PtrLt(i, pivot)) goto L_SCAN_BACK;                      // CMP RDI,RSI / JA

    L_SCAN_HI:                                                      // 0x180063191
        ++i;
        if (PtrLt(hi, i)) goto L_SCAN_BACK;                         // CMP RSI,R15 / JA
        if (!less(*pivot, *i)) goto L_SCAN_HI;
        goto L_SCAN_BACK;

    L_SCAN_BACK:                                                    // 0x180063210
        --j;
        if (PtrLe(j, pivot)) goto L_TRY_SWAP;                       // CMP R14,RDI / JBE
        if (less(*pivot, *j)) goto L_SCAN_BACK;
        goto L_TRY_SWAP;

    L_TRY_SWAP:                                                     // 0x180063290
        if (PtrLt(j, i)) goto L_EQRUN_ENTER;                        // CMP R14,RSI / JB
        swp(i, j);                                                  // 0x180063295
        if (pivot != j) goto L_PART;                                // CMP RDI,R14 / JNE
        pivot = i;                                                  // 0x18006330a MOV RDI,RSI
        goto L_PART;

    L_EQRUN_ENTER:                                                  // 0x180063312
        ++j;
        if (!PtrLt(pivot, j)) goto L_EQRUN_LO;                      // CMP RDI,R14 / JAE

    L_EQRUN_PIVOT:                                                  // 0x180063320
        --j;
        if (PtrLe(j, pivot)) goto L_EQRUN_LO;                       // CMP R14,RDI / JBE
        if (less(*j, *pivot)) goto L_EQRUN_DONE;                    // chain 1 -> 0x18006345e
        if (less(*pivot, *j)) goto L_EQRUN_DONE;                    // chain 2 -> 0x18006345e
        goto L_EQRUN_PIVOT;                                         // equal: keep walking

    L_EQRUN_DONE:                                                   // 0x18006345e
        if (PtrLt(pivot, j)) goto L_SPLIT;                          // CMP RDI,R14 / JB
        goto L_EQRUN_LO;

    L_EQRUN_LO:                                                     // 0x180063470
        --j;
        if (PtrLe(j, lo)) goto L_SPLIT;                             // CMP R14,RBP / JBE
        if (less(*j, *pivot)) goto L_SPLIT;
        if (less(*pivot, *j)) goto L_SPLIT;
        goto L_EQRUN_LO;

    L_SPLIT:                                                        // 0x1800635ae
        {
            // Both lengths are a byte distance divided by the stride; only
            // their ORDER is used, and the compare is SIGNED (`JL`).
            const int64_t rightLen = hi - i;
            const int64_t leftLen  = j  - lo;

            if (leftLen < rightLen) {                               // JL 0x18006361d
                if (PtrLt(i, hi)) {                                 // CMP RSI,R15 / JAE skips
                    stackLo[sp] = i; stackHi[sp] = hi; ++pushes; ++sp;
                }
                if (!PtrLt(lo, j)) goto POP;                        // CMP RBP,R14 / JAE
                hi = j;
                goto OUTER;
            }
            if (PtrLt(lo, j)) {                                     // CMP RBP,R14 / JAE skips
                stackLo[sp] = lo; stackHi[sp] = j; ++pushes; ++sp;
            }
            if (!PtrLt(i, hi)) goto POP;                            // CMP RSI,R15 / JAE
            lo = i;
            goto OUTER;
        }
    }

POP:                                                                // 0x180062dd6
    --sp;
    if (sp < 0) return;                                             // DEC R12 / JS
    lo = stackLo[sp];
    hi = stackHi[sp];
    goto OUTER;
}

}  // namespace rtsort
