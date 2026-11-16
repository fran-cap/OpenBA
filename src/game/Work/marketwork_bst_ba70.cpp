// src/game/Work/marketwork_bst_ba70.cpp
// Faithful 1:1 reimplementation of Application-x64.exe 0x14002ba70
// (body 0x14002ba70 - 0x14002bd74, 0x305 bytes, 220 instructions).
//
// GHIDRA MISNOMER: the function is named `MarketWork_helper_ba70` because it sits
// inside the MarketWork AVL/BST block (0x14002a9f0 .. 0x14002c8a0).  It is NOT a
// tree helper.  It never touches the 0x38-byte market node layout, never allocates
// or frees, and is a true LEAF -- disassemble_function(filter_mnemonics="CALL")
// over the whole body returns ZERO instructions, so there is no direct call, no
// recursion, and no indirect call through an .rdata dispatch slot.
//
// WHAT IT ACTUALLY IS
//   An inlined median-of-three QUICKSORT over a flat array of 0x18-byte records,
//   ascending by the SIGNED int32 at record+0x08, with:
//     * an explicit 62-entry work stack (two parallel arrays: lo at RSP+0x00,
//       hi at RSP+0x1F0, 62 * 8 = 0x1F0 bytes each, 0x3E0 total -- matches
//       `SUB RSP,0x3e0` at 0x14002ba80).  No recursion, no depth check.
//     * a SELECTION-sort cutoff for ranges of <= 8 elements (0x14002bacc:
//       `CMP EDX,0x8 / JG`).  Note it is a selection sort (repeatedly swap the
//       max into the tail), not the insertion sort the MSVC STL std::sort uses.
//     * tail-recursion elimination: the LARGER half is pushed and the smaller
//       half is iterated (0x14002bd10 `CMP RDX,R9 / JL`).
//     * a pivot-equal skip-back scan (0x14002bca0 / 0x14002bcc0) that walks the
//       lower cursor past runs of keys EQUAL to the pivot key.
//
// PARAM_2 IS AN ELEMENT COUNT.  Established from the prologue and the caller:
//   0x14002ba87  MOVSXD RBX,EDX          ; sign-extend count
//   0x14002ba94  DEC    RBX              ; count-1
//   0x14002ba99  LEA    RBX,[RBX+RBX*2]  ; *3
//   0x14002ba9d  LEA    RBX,[RCX+RBX*8]  ; base + (count-1)*24  -> `last` (INCLUSIVE)
//   caller 0x1400f168d: CMP R13D,0x1 / JLE skip ; MOV EDX,R13D ; MOV RCX,R12 ; CALL
//   i.e. the caller skips the sort entirely for count <= 1.
//
// STRIDE / KEY OFFSET -- verified against the DISASSEMBLY, not the decompiler.
//   The decompiler types the array as `undefined8*` and therefore prints ELEMENT
//   indices: `param_1[1]` is BYTE +0x08 and `puVar5 + 3` is BYTE +0x18.  Every one
//   of those was cross-checked against the raw encoding:
//     stride  : ADD RCX,0x18 (0x14002bc10, 0x14002bc27) / SUB R10,0x18 (0x14002bc40)
//     key     : MOV EAX,dword ptr [RCX + 0x8]  (0x14002baf0, 0x14002bc19, ...)
//               -> a 4-byte DWORD read at +0x08, compared signed (JGE / JL / CMOVL)
//     swap    : three 8-byte MOVs at +0x00 / +0x08 / +0x10 (0x14002bb04 ff.)
//               -- all qword.  Nothing here is narrowed to 4 bytes.
//     /0x18   : RBP = 0x2AAAAAAAAAAAAAAB, IMUL + SAR 2 + sign-fixup.  That is a
//               SIGNED division by 24.  The decompiler renders one of these as
//               `.../6 + (... >> 0x3f)` then `>> 2` at 0x14002bcd9 -- that is the
//               same magic-multiply, just un-folded.  Both are (ptrdiff)/24.
//
// RETURN VALUE: none.  The epilogue at 0x14002bd5c restores RBX/RBP/RSI/RDI and
// RETs without setting RAX (it holds partition scratch), so `void` is correct --
// this is one of the cases where Ghidra's `void` really is void.  Checked
// explicitly because 0x1400b0f90 in this tree returns a float Ghidra typed void.
//
// POINTER COMPARISONS: retail uses UNSIGNED forms (JA/JBE/JC/JNC) throughout.
// All pointers live inside one array, so unsigned and signed orderings agree;
// this port uses ordinary C++ pointer relationals on Take*.
//
// NO DEFENSIVE GUARDS ADDED.  Every read is inside the array the caller supplied,
// and the count <= 1 case is the caller's contract (see header).  Per CLAUDE.md, a
// null/size check here would only mask an upstream bug rather than reproduce retail.

#include "Work/marketwork_bst_ba70.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

using Take = MarketWork::Take;   // one 0x18-byte record; the sort key is the int32 at +0x08

// Selection-sort cutoff, 0x14002bacc: `CMP EDX,0x8 / JG quicksort`.
constexpr int kSmallRange = 8;

// Work-stack depth: 0x3E0 bytes of locals split into two 0x1F0 arrays => 62 each.
// Retail performs NO overflow check; neither do we.
constexpr int kStackDepth = 62;

inline int32_t Key(const Take* rec)
{
    // MOV EAX,dword ptr [rec + 0x8] -- 4-byte signed key.
    return rec->rank;
}

// The three-qword record swap emitted at 0x14002bb04 / 0x14002bb6b / 0x14002bb9f /
// 0x14002bbd4 / 0x14002bc58.  Retail loads one side's three qwords before storing,
// so a self-swap (a == b) is a no-op; a whole-record swap behaves identically.
inline void SwapRec(Take* a, Take* b)
{
    Take t = *a;
    *a = *b;
    *b = t;
}

} // namespace

// ============================================================
// 0x14002ba70 -- in-place quicksort of 24-byte records by int32 key at +0x08
// ============================================================
extern "C" void MarketBst_ba70(MarketWork::Take* base, int count)
{
    // Register map used in the comments below, taken from the disassembly:
    //   RDI = first   RBX = last (INCLUSIVE)   RSI = depth
    //   R11 = mid/pivot   RCX = up cursor   R10 = down cursor
    Take* first = base;
    Take* last  = first + static_cast<ptrdiff_t>(count - 1);             // 0x14002ba87..ba9d
    intptr_t depth = 0;                                                  // XOR ESI,ESI

    Take* loStack[kStackDepth];
    Take* hiStack[kStackDepth];

    // Declared up front so the address-labelled gotos below never jump across an
    // initialisation, and so the control flow can mirror the retail CFG exactly.
    Take* mid  = nullptr;
    Take* down = nullptr;
    Take* up   = nullptr;
    int      n    = 0;

LAB_bab0:
    // 0x14002bab0: n = (last - first)/0x18 + 1   (signed magic-multiply by 24)
    n = static_cast<int>(last - first) + 1;
    if (n > kSmallRange) goto LAB_bb54;              // 0x14002bacc CMP EDX,8 / JG

    // ---- selection sort for <= 8 elements (0x14002bad5 .. 0x14002bb38) ----
    if (last <= first) goto LAB_bb3a;                // 0x14002bad5 CMP RBX,RDI / JBE
    {
        // R10 = first + 0x18, hoisted OUT of the outer loop by retail (0x14002bada)
        // and never updated: every pass rescans from first+1 up to the shrinking end.
        Take* const scanStart = first + 1;
        do
        {
            Take* maxp = first;                   // 0x14002bae0 MOV R9,RDI
            Take* p    = scanStart;               // 0x14002bae3 MOV RCX,R10
            if (p <= last)                           // 0x14002bae6 CMP R10,RBX / JA
            {
                do
                {
                    // 0x14002baf0: CMP [maxp+8],[p+8] / CMOVL R9,RCX
                    if (Key(maxp) < Key(p)) maxp = p;
                    p += 1;
                } while (p <= last);                 // 0x14002bb02 JBE
            }
            SwapRec(maxp, last);                     // 0x14002bb04 (3 qword moves)
            last -= 1;                           // 0x14002bb21 SUB RBX,0x18
        } while (last > first);                      // 0x14002bb38 JA
    }
    goto LAB_bb3a;

LAB_bb3a:
    // 0x14002bb3a: pop the next pending range, or return when the stack empties.
    --depth;
    if (depth < 0) return;                           // DEC RSI / JS -> 0x14002bd5c RET
    first = loStack[depth];                          // [RSP + RSI*8]
    last  = hiStack[depth];                          // [RSP + RSI*8 + 0x1F0]
    goto LAB_bab0;

LAB_bb54:
    // ---- median-of-three pivot selection (0x14002bb54 .. 0x14002bc00) ----
    // MOVSXD RAX,EDX / SAR RAX,1 / LEA RAX,[RAX+RAX*2] / LEA R11,[RDI+RAX*8]
    // i.e. mid = first + (n >> 1) * 0x18.  n > 8 here, so the arithmetic shift is
    // an ordinary halving.
    mid = first + static_cast<ptrdiff_t>(n >> 1);

    if (Key(mid)  < Key(first)) SwapRec(first, mid);   // 0x14002bb62 JGE skips
    if (Key(last) < Key(first)) SwapRec(first, last);  // 0x14002bb97 JGE skips
    if (Key(last) < Key(mid))   SwapRec(mid,  last);   // 0x14002bbcb JGE skips
    // *mid is now the median of the three and is the pivot ELEMENT.  Retail tracks
    // the pivot by POINTER, not by value -- hence the `mid = up` fixup below.

    up   = first;                                     // 0x14002bc00 MOV RCX,RDI
    down = last;                                      // 0x14002bc03 MOV R10,RBX

LAB_bc06:
    // ---- partition (0x14002bc06 .. 0x14002bc90) ----
    if (mid > up)                                     // 0x14002bc06 CMP R11,RCX / JBE
    {
        for (;;)
        {
            up += 1;                              // 0x14002bc10 ADD RCX,0x18
            if (up >= mid) goto LAB_bc27;             // 0x14002bc14 JNC
            if (Key(mid) < Key(up)) break;            // 0x14002bc1c JGE -> keep scanning
        }
        if (mid > up) goto LAB_bc40;                  // 0x14002bc22 CMP R11,RCX / JA
        // else fall through into LAB_bc27
    }

LAB_bc27:
    // Continue the upward scan, now bounded by `last` instead of by `mid`.
    for (;;)
    {
        up += 1;                                  // 0x14002bc27
        if (up > last) break;                         // 0x14002bc2b JA
        if (Key(mid) < Key(up)) break;                // 0x14002bc33 JGE -> keep scanning
    }

LAB_bc40:
    // Downward scan: walk back while the pivot key is strictly less than *down.
    for (;;)
    {
        down -= 1;                                // 0x14002bc40 SUB R10,0x18
        if (down <= mid) break;                       // 0x14002bc44 JBE
        if (!(Key(mid) < Key(down))) break;           // 0x14002bc51 JL -> keep scanning
    }

    // 0x14002bc53: have the cursors crossed?
    if (down < up) goto LAB_bc95;                     // CMP R10,RCX / JC
    SwapRec(up, down);                                // 0x14002bc58
    // 0x14002bc84: the pivot is tracked by POINTER, so if the swap moved the pivot
    // record itself (mid == down) the pivot pointer follows it to `up`.  Retail does
    // NOT handle the symmetric mid == up case; that asymmetry is reproduced here.
    if (mid == down) mid = up;                        // 0x14002bc8d MOV R11,RCX
    goto LAB_bc06;

LAB_bc95:
    // ---- pivot-equal skip-back (0x14002bc95 .. 0x14002bcd7) ----
    down += 1;                                    // 0x14002bc95 ADD R10,0x18
    if (mid >= down) goto LAB_bcc0;                   // 0x14002bc99 CMP R11,R10 / JNC
    for (;;)
    {
        down -= 1;                                // 0x14002bca0
        if (down <= mid) goto LAB_bcc0;               // 0x14002bca4 JBE
        {
            const int32_t a = Key(down);              // 0x14002bca9 EAX
            const int32_t b = Key(mid);               // 0x14002bcad EDX
            if (a < b) break;                         // 0x14002bcb3 JL
            if (b >= a) continue;                     // 0x14002bcb7 JGE -> a == b, skip it
            break;                                    // a > b
        }
    }
    // 0x14002bcb9: if the cursor is still above the pivot we are done partitioning.
    if (mid < down) goto LAB_bcd9;                    // CMP R11,R10 / JC
    // else fall through into LAB_bcc0

LAB_bcc0:
    // Same skip-back loop, now bounded by `first` rather than by `mid`.
    for (;;)
    {
        down -= 1;                                // 0x14002bcc0
        if (down <= first) break;                     // 0x14002bcc4 CMP R10,RDI / JBE
        {
            const int32_t a = Key(down);
            const int32_t b = Key(mid);
            if (a < b) break;                         // 0x14002bcd3 JL
            if (b >= a) continue;                     // 0x14002bcd7 JGE -> a == b, skip it
            break;
        }
    }

LAB_bcd9:
    // ---- push the larger half, iterate the smaller (0x14002bcd9 .. 0x14002bd57) ----
    {
        // Both divisions are the RBP=0x2AAAAAAAAAAAAAAB magic-multiply => /0x18 signed.
        const ptrdiff_t rightCount = (last - up);      // R9  @ 0x14002bce8
        const ptrdiff_t leftCount  = (down - first);   // RDX @ 0x14002bcff

        if (leftCount < rightCount) goto LAB_bd37;             // 0x14002bd13 JL (signed)

        // Left half is the larger one: stack it, keep working on the right half.
        if (first < down)                                      // 0x14002bd15 JNC skips
        {
            loStack[depth] = first;                            // 0x14002bd1a
            hiStack[depth] = down;                             // 0x14002bd1e
            ++depth;                                           // 0x14002bd26 INC RSI
        }
        if (up >= last) goto LAB_bb3a;                         // 0x14002bd29 JNC
        first = up;                                            // 0x14002baa1 MOV RDI,RCX
        goto LAB_bab0;
    }

LAB_bd37:
    // Right half is the larger one: stack it, keep working on the left half.
    if (up < last)                                             // 0x14002bd37 JNC skips
    {
        loStack[depth] = up;                                   // 0x14002bd3c
        hiStack[depth] = last;                                 // 0x14002bd40
        ++depth;                                               // 0x14002bd48
    }
    if (first >= down) goto LAB_bb3a;                          // 0x14002bd4b JNC
    last = down;                                               // 0x14002bd54 MOV RBX,R10
    goto LAB_bab0;
}
