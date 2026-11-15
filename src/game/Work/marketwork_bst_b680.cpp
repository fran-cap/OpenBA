// src/game/Work/marketwork_bst_b680.cpp
// Faithful reimplementation of Application-x64.exe 0x14002b680 + 0x14002bd80.
//
// ============================================================================
// GHIDRA MISNOMER — THIS CLUSTER IS NOT A BST
// ============================================================================
// Both functions carry `MarketWork_helper_*` labels and were batched with the
// MarketWork 0x38-byte BST helpers, but neither touches a tree node.  They are
// the two halves of one ARRAY SORT:
//
//   0x14002b680  quicksort  (median-of-3 pivot, explicit lo/hi stack, CUTOFF 8)
//   0x14002bd80  selection sort, the small-range fallback the quicksort calls
//
// The element is a flat 0x20-byte (32-byte) record and the sort key is the
// FLOAT at record+0x14.  Nothing at +0x18/+0x20/+0x28 is followed as a pointer;
// every one of those bytes is merely part of the 8-dword payload the swap
// shuffles wholesale.  The 0x38-byte MarketWork node layout documented at the
// top of marketwork.cpp is irrelevant here.
//
// This is, instruction for instruction, the MSVC CRT `qsort` / `shortsort`
// pair with the comparator and the swap inlined:
//   - CUTOFF == 8            (0x14002b6cc  CMP EAX,0x8 / JG)
//   - STKSIZ == 62           (RSP+0x20 lo array and RSP+0x210 hi array are
//                             0x1F0 bytes each = 62 pointers)
//   - comp(a,b) > 0          becomes  COMISS Key(a), Key(b) / JA
//   - swap(a,b,width)        becomes  8 interleaved dword MOVs
// It is a template/inline instantiation, so it has NO callee outside the pair
// (b680 -> bd80).
//
// ============================================================================
// TRUE SIGNATURES  (Ghidra's `undefined` return + `undefined4*` params mislead)
// ============================================================================
//   void __fastcall b680(void* base [RCX], int count [EDX])
//        RDX is read as a 32-bit signed COUNT (0x14002b69f MOVSXD RAX,EDX;
//        SHL RAX,5), NOT a pointer.  Nothing meaningful is left in RAX/XMM0 at
//        the RET (0x14002ba68) — the epilogue only restores callee-saved regs.
//   void __fastcall bd80(void* first [RCX], void* last [RDX])
//        `last` is the INCLUSIVE last element, not one-past-end.
// Ghidra's `undefined4*` typing makes its pointer arithmetic read in 4-byte
// ELEMENTS: `p[5]` in the decompiler is byte offset 0x14 (the key) and
// `p + 8` is byte offset 0x20 (one record).  Every offset below was
// re-derived from the disassembly, not from that output.
//
// ============================================================================
// SOLE CALLER / WHAT IS BEING SORTED
// ============================================================================
// b680 has exactly one call site: 0x1400f0e82, inside the function Ghidra names
// MarketWork_Serialize (entry 0x1400eff80 — itself a misnomer; it is the market
// hauling planner).  There:
//     if (1 < count) MarketWork_helper_b680(local_170, count);
// `local_170` is a scratch array of 0x20-byte per-candidate records built in the
// loop just above; +0x00 is a flags mask ANDed against ConsumeProduceComponent
// +0x5c, +0x18 is a running quantity accumulator, and +0x14 is the float score
// this sort orders by.  The `count > 1` guard in the caller is the hoisted
// `if (num < 2) return` of the CRT original — b680 itself has no such early-out
// (with count == 0 it forms hi = base - 0x20 and bd80's first compare bails).
// A sibling instantiation for 0x18-byte records, MarketWork_helper_ba70, is
// called from the same function at 0x1400f168d; it is not part of this port.
//
// Sort order is ASCENDING: bd80 selects the MAXIMUM of the remaining range and
// swaps it to the high end.
//
// Verified offset-by-offset against the disassembly of both bodies
// (0x14002b680-0x14002ba68 and 0x14002bd80-0x14002bedd).

#include "Work/marketwork_bst_b680.h"

#include <cstdint>

namespace {

// One sorted element: MarketWork::Demand, 0x20 bytes; retail moves it as 8 dwords
// (e.g. 0x14002be4a..0x14002beb8), so a whole-record copy is byte-identical.
using MwSortRec = MarketWork::Demand;

// The sort key: the float at record+0x14 (satisfaction).
// Disassembly: every comparison is `MOVSS XMM0,[reg + 0x14]` — 0x14002bde0,
// 0x14002b6f4, 0x14002b708, 0x14002b8c9, 0x14002b8fe, 0x14002b9a9, ...
inline float Key(const MwSortRec* r) {
    return r->satisfaction;
}

// comp(a, b) > 0.  Retail spells this COMISS Key(a), Key(b) + JA, i.e. an
// ORDERED strictly-greater test: a NaN on either side takes the false arm.
// Plain C++ `>` on float lowers to exactly that, so this stays faithful.
inline bool KeyGreater(const MwSortRec* a, const MwSortRec* b) {
    return Key(a) > Key(b);
}

// comp(a, b) == 0, as retail actually implements it at 0x14002b9b5 and
// 0x14002b9d9: two COMISS in both directions, continue only when NEITHER is
// above.  Deliberately NOT written as `Key(a) == Key(b)` — for a NaN pair this
// form yields TRUE where `==` yields false, and the equal-run scan below
// depends on retail's answer.
inline bool KeyEquiv(const MwSortRec* a, const MwSortRec* b) {
    return !(Key(a) > Key(b)) && !(Key(b) > Key(a));
}

inline void SwapRec(MwSortRec* a, MwSortRec* b) {
    MwSortRec t = *a;
    *a = *b;
    *b = t;
}

// CUTOFF (0x14002b6cc: CMP EAX,0x8 / JG) and STKSIZ (0x1F0 bytes / 8 per array).
constexpr int kCutoff = 8;
constexpr int kStackDepth = 62;

} // namespace

// ============================================================================
// 0x14002bd80 — selection sort over the INCLUSIVE range [first, last]
// ============================================================================
// Retail (register map: RBP = lo, R14 = hi, RSI = max, R8 = scan cursor):
//   0x14002bd80  CMP RDX,RCX / JBE ret     -> bail when last <= first
//   0x14002bdb0  max = lo
//   0x14002bdb6  scan p = lo+0x20 .. hi, tracking the maximum
//   0x14002be47  swap(max, hi)
//   0x14002be6b  hi -= 0x20
//   0x14002bebc  loop while hi > lo
// The 0x14002bdd1..0x14002be29 block is nothing but a 4x unrolled copy of the
// scan (candidates at +0x14/+0x34/+0x54/+0x74, cursor += 0x80) emitted when the
// remaining span is >= 4 records; 0x14002be30 is the identical scalar tail.
// The unroll is order-preserving and picks the same element, so the plain loop
// below is equivalent — the *result* is what has to match, and CMOVA/JBE both
// keep the EARLIER element on a tie exactly as `>` does here.
//
// Pointer comparisons are unsigned in retail (JBE/JA); pointers into one array
// compare the same either way.
extern "C" void MarketBst_bd80(MarketWork::Demand* first, MarketWork::Demand* last) {
    MwSortRec* lo = first;
    MwSortRec* hi = last;

    while (hi > lo) {
        MwSortRec* max = lo;
        for (MwSortRec* p = lo + 1; p <= hi; ++p) {
            if (KeyGreater(p, max)) {
                max = p;
            }
        }
        SwapRec(max, hi);   // 0x14002be47 — self-swap when max == hi, as in retail
        --hi;               // 0x14002be6b  SUB R14,0x20
    }
}

// ============================================================================
// 0x14002b680 — quicksort of `count` records at `base`
// ============================================================================
// Register map: R12 = lo, RBP = hi, R14 = mid (the pivot, TRACKED because the
// partition may move it), RSI = loguy, R15 = higuy, R13 = stack pointer.
//
// The `goto`s below are not stylistic: they mirror retail's own control flow
// (0x14002b6c0 recurse head, 0x14002b6dc pop, 0x14002b8b6 partition head,
// 0x14002b9e3 the recurse-smaller-half decision) so the port can be diffed
// against the listing line for line.
extern "C" void MarketBst_b680(MarketWork::Demand* base, int count) {
    MwSortRec* lo = base;
    // 0x14002b69f: MOVSXD RAX,EDX / SHL RAX,5 / LEA RBP,[RCX-0x20] / ADD RBP,RAX
    // i.e. hi = base + (count - 1) records.  count == 0 leaves hi = base - 1
    // record; the size test below then routes to bd80, which returns at once.
    MwSortRec* hi = lo + static_cast<int64_t>(count) - 1;

    MwSortRec* loStk[kStackDepth];   // RSP+0x20  (0x1F0 bytes)
    MwSortRec* hiStk[kStackDepth];   // RSP+0x210 (0x1F0 bytes)
    int64_t sp = 0;                  // R13, zeroed at 0x14002b6b0

recurse:
    for (;;) {
        // 0x14002b6c0: size = ((hi - lo) >> 5) + 1, computed in 32 bits (INC EAX).
        const int size = static_cast<int>(hi - lo) + 1;
        if (size <= kCutoff) {          // 0x14002b6cc CMP EAX,8 / JG
            MarketBst_bd80(lo, hi);     // 0x14002b6d7 CALL 0x14002bd80
            break;                      // -> pop
        }

        // ---- median-of-three, leaving the median at `mid` -------------------
        // 0x14002b6fb: mid = lo + (size >> 1) records (MOVSXD/SAR 1/SHL 5).
        MwSortRec* mid = lo + (static_cast<int64_t>(size) >> 1);
        if (KeyGreater(lo, mid)) SwapRec(lo, mid);   // 0x14002b708 COMISS/JBE
        if (KeyGreater(lo, hi))  SwapRec(lo, hi);    // 0x14002b7a6
        if (KeyGreater(mid, hi)) SwapRec(mid, hi);   // 0x14002b838

        // ---- partition ------------------------------------------------------
        MwSortRec* loguy = lo;   // 0x14002b8b0 MOV RSI,R12
        MwSortRec* higuy = hi;   // 0x14002b8b3 MOV R15,RBP
        for (;;) {
            // 0x14002b8b6: skip the bounded scan entirely when mid <= loguy.
            if (mid > loguy) {
                // 0x14002b8c0: advance while below mid and key <= pivot key.
                do { ++loguy; } while (loguy < mid && !KeyGreater(loguy, mid));
            }
            // 0x14002b8d5 / 0x14002b8e0: once loguy has reached or passed mid,
            // keep advancing under the `hi` bound instead.
            if (mid <= loguy) {
                do { ++loguy; } while (loguy <= hi && !KeyGreater(loguy, mid));
            }
            // 0x14002b8f5: walk higuy down past everything greater than pivot.
            do { --higuy; } while (higuy > mid && KeyGreater(higuy, mid));

            if (higuy < loguy) break;               // 0x14002b90b JC -> 0x14002b995
            SwapRec(loguy, higuy);                  // 0x14002b914
            // 0x14002b984: the pivot itself just moved — follow it.
            if (mid == higuy) mid = loguy;
        }

        // ---- collapse the run of pivot-equal elements ------------------------
        ++higuy;                                    // 0x14002b995 ADD R15,0x20
        // 0x14002b999 / 0x14002b9a0
        if (mid < higuy) {
            do { --higuy; } while (higuy > mid && KeyEquiv(higuy, mid));
        }
        // 0x14002b9bf / 0x14002b9c4 — note this re-tests the higuy the loop
        // above just moved, exactly as retail's CMP R14,R15 does.
        if (mid >= higuy) {
            do { --higuy; } while (higuy > lo && KeyEquiv(higuy, mid));
        }

        // ---- recurse into the smaller half, iterate on the larger ------------
        // 0x14002b9e3: both spans are masked with -0x20 before the compare;
        // with a 0x20 stride that mask is a no-op, so this is the plain
        // element-count comparison below.
        if ((higuy - lo) >= (hi - loguy)) {         // 0x14002b9f7 CMP RAX,RCX / JL
            if (lo < higuy) {                       // 0x14002b9fc
                loStk[sp] = lo;
                hiStk[sp] = higuy;
                ++sp;
            }
            if (loguy < hi) {                       // 0x14002ba11 CMP RSI,RBP / JNC
                lo = loguy;
                goto recurse;
            }
            break;                                  // -> pop
        } else {                                    // 0x14002ba22
            if (loguy < hi) {
                loStk[sp] = loguy;
                hiStk[sp] = hi;
                ++sp;
            }
            if (lo < higuy) {                       // 0x14002ba37 CMP R12,R15 / JNC
                hi = higuy;
                goto recurse;
            }
            break;                                  // -> pop
        }
    }

    // 0x14002b6dc: pop one pending range, or return when the stack underflows.
    --sp;
    if (sp < 0) return;                             // 0x14002b6df JS -> 0x14002ba48
    lo = loStk[sp];                                 // 0x14002b6e5
    hi = hiStk[sp];                                 // 0x14002b6ea
    goto recurse;
}
