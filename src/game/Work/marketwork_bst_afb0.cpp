// src/game/Work/marketwork_bst_afb0.cpp
// Faithful 1:1 port of exe 0x14002afb0 (body 0x14002afb0-0x14002b28b, 228 instructions).
//
// Ghidra name: MarketWork_helper_afb0
// Ghidra sig:  undefined __fastcall MarketWork_helper_afb0(longlong *param_1, int param_2)
// True sig:    void __fastcall (ArrayDesc* arr [RCX], int newCount [EDX])
//
// ------------------------------------------------------------------
// WHAT THIS ACTUALLY IS (the batch premise was wrong)
// ------------------------------------------------------------------
// The batch brief describes this block as MarketWork's 0x38-byte AVL/BST helper
// set. This function is NOT part of that tree. There is not a single node-link
// deref in it: no +0x18 parent, no +0x20/+0x28 child loads, no comparisons of
// InternalBasePtr keys, no rotations, no recursion. What it does have is:
//
//   14002afb8  MOVSXD RBP,dword ptr [RCX + 0x8]      count   (dword, at +0x08)
//   14002afda  CMP    ESI,dword ptr [RCX + 0xc]      capacity(dword, at +0x0C)
//   14002afcf  MOV    RDI,qword ptr [RCX]            data    (qword, at +0x00)
//   14002affc  SHL    RCX,0x5                        capacity * 0x20 bytes
//   14002b000  CALL   qword ptr [0x140152b60]        MemAlloc(size, align=4)
//   14002b279  CALL   qword ptr [0x140152b68]        MemFree(old)
//
// i.e. it is the classic "grow a dynamic array of 32-byte PODs" routine:
//     ArrayDesc { void* data; int32 count; int32 capacity; }   // 0x10 bytes
//     Resize(ArrayDesc* arr, int newCount)
//
// The 0x20 element stride is proven three ways: SHL RCX,0x5 on the allocation,
// the 8x dword element copy (offsets 0x00..0x1C), and the scalar loop stride
// ADD RCX,0x20 at 0x14002b263.
//
// ------------------------------------------------------------------
// THE SINGLE CALL SITE CONFIRMS IT (0x1400eff80 MarketWork_Serialize/Slot4)
// ------------------------------------------------------------------
//   1400f021f  MOV qword ptr [RBP + -0x78],RDI   ; RDI == 0 -> data     = null
//   1400f0223  MOV qword ptr [RBP + -0x70],RDI   ;         -> count/cap = 0,0
//   1400f0227  MOV EDX,dword ptr [R14 + 0x30]    ; newCount from a description field
//   1400f022b  LEA RCX,[RBP + -0x78]             ; &stack ArrayDesc
//   1400f022f  CALL 0x14002afb0
//   1400f0234  MOV EBX,dword ptr [RBP + -0x70]   ; reads count back out
//   1400f0250..1400f0265                         ; fills elem[i]+0x00 (dword),
//                                                ; src stride 8, dst stride 0x20
// A zero-initialised stack descriptor being passed straight in settles the
// layout: +0x08 and +0x0C are two 4-byte ints inside one 8-byte slot, and the
// descriptor is 0x10 bytes total. param_2 is therefore NOT a count/index/mode
// into a tree -- it is the REQUESTED NEW ELEMENT COUNT.
//
// ------------------------------------------------------------------
// GHIDRA-VS-DISASSEMBLY DISAGREEMENTS (all checked, all benign)
// ------------------------------------------------------------------
// 1. ELEMENTS-vs-BYTES (the standing project trap). The decompiler prints
//    `iVar1 = (int)param_1[1]` for the count. On a longlong* that is BYTE
//    offset 0x08 -- and retail loads a *dword* there (MOVSXD ... [RCX+0x8]),
//    not the 8 bytes the [1] subscript implies. Likewise `puVar6[-0x10]` on
//    an undefined4* is -0x40 BYTES, and `puVar6 = puVar6 + 0x20` is +0x80
//    bytes (disasm: SUB RCX,-0x80). Every offset below is taken from the
//    disassembly, not from the subscripts.
// 2. `undefined` return type: verified void. No path sets RAX deliberately;
//    the EAX left over at 0x14002b242 is the last copy temporary, and the
//    epilogue at 0x14002b284 restores and returns without touching it.
// 3. The `if (ptr != 0)` tests the decompiler emits around every element write
//    are REAL retail code, not decompiler noise: they are the ZF from the
//    `ADD RDX,R9` / `ADD RDX,RCX` that forms the destination pointer
//    (0x14002b07d + JZ 0x14002b080, 0x14002b20e + JZ 0x14002b211, and their
//    siblings). They are an inlined element-writer's own null check, so they
//    are ported verbatim -- this is NOT a defensive guard added by the port.
// 4. Retail unrolls the fill loop 4x (0x14002b071-0x14002b1e5, guarded by
//    i < newCount-3) and then runs a scalar remainder loop
//    (0x14002b206-0x14002b26a). The unrolled block's four dst/src pairs were
//    checked to resolve to exactly i, i+1, i+2, i+3 (the +2 bias baked into the
//    base pointer cancels the -0x40 / -0x20 / 0 / +0x20 displacements), so a
//    single scalar loop is behaviourally identical. Ported as the scalar loop.
//
// ------------------------------------------------------------------
// THE ODD DEFAULT-INIT: BYTES 0..3 ARE DELIBERATELY LEFT ALONE
// ------------------------------------------------------------------
// The "clear a new element" path writes 28 bytes, not 32:
//   14002b0b8  MOV qword ptr [RDX + 0x4],R13     (R13 == 0)
//   14002b0bc  MOV qword ptr [RDX + 0xc],R13
//   14002b0c0  MOV qword ptr [RDX + 0x14],R13
//   14002b0c4  MOV dword ptr [RDX + 0x1c],R13D
// elem+0x00..+0x03 is skipped. That is not a decompiler artefact -- all four
// copies of the clear block in the unrolled body do the same thing. It is the
// element type's default constructor: field 0 (a 4-byte id/index) has no member
// initialiser because every caller assigns it immediately, which is precisely
// what the call site does at 0x1400f0250-0x1400f0265. Ported byte-exactly:
// widening this to a 32-byte zero would be a deviation, not a cleanup.
//
// ------------------------------------------------------------------
// GROWTH POLICY (exact)
// ------------------------------------------------------------------
//   if (newCount > capacity) {
//       capacity = newCount;                                 // exact fit...
//       if (newCount == oldCount + 1 && oldCount != 0)       // ...unless this
//           capacity = newCount * 3;                         // is a push_back
//       data = MemAlloc(capacity * 0x20, 4);
//   }
// LEA EAX,[RSI + RSI*0x2] at 0x14002afed is the *3. The "append one to a
// non-empty array" case is the only one that over-allocates -- a bulk resize
// gets an exact-fit buffer. Note the old buffer is NOT freed at the allocation
// site; it is freed once at the very end, after the copy, and only if the
// pointer actually changed.
//
// Callees: MemAlloc [0x140152b60] and MemFree [0x140152b68] only. Both are
// Runtime statics, called by name as System::MemoryAllocator::MemAlloc / MemFree.

#include "Work/marketwork_bst_afb0.h"
#include "system/MemoryAllocator.h"

#include <cstddef>
#include <cstdint>

namespace {

// The 0x10-byte descriptor retail passes in RCX is MarketWork::DemandList: count and
// capacity are 4-byte ints and MUST stay 4-byte (retail writes both with MOV dword).
using ArrayDesc = MarketWork::DemandList;
using Demand = MarketWork::Demand;

// Element copy as retail does it: eight dword moves, 0x00..0x1C (a whole-record copy).
inline void CopyElement(Demand* dst, const Demand* src) {
    *dst = *src;
}

// Default-init as retail does it: 28 bytes from +0x04, leaving +0x00..+0x03
// untouched (see the header comment -- this asymmetry is intentional). Retail's three
// qword stores and one dword store are the seven named fields, in address order.
inline void ClearElement(Demand* dst) {
    dst->inMarket = 0;
    dst->inTransit = 0;
    dst->inTown = 0;
    dst->stockedFrac = 0.0f;
    dst->satisfaction = 0.0f;
    dst->stage4Scratch = 0;
    dst->shortfall = 0;
}

}  // namespace

extern "C" void MarketBst_afb0(MarketWork::DemandList* arr, int32_t newCount) {

    // 14002afb8 / 14002afc2: nothing to do when the count is already right.
    // Note this is an exact-equality test, so a "resize" to the current count
    // is a no-op even when capacity is oversized -- no shrink-to-fit happens.
    const int32_t oldCount = arr->count;
    if (newCount == oldCount) return;

    Demand* const oldData = arr->data;  // 14002afcf, saved before any realloc

    arr->count = newCount;  // 14002afd2 -- MOV dword, written before the grow

    if (newCount > arr->capacity) {              // 14002afda
        arr->capacity = newCount;                // 14002afe2  exact fit
        if (newCount == oldCount + 1 && oldCount != 0) {
            arr->capacity = newCount * 3;        // 14002afed  push_back growth
        }
        // 14002aff3-14002b006: MemAlloc(capacity << 5, align 4). The shift runs
        // on the sign-extended 64-bit capacity, matching MOVSXD + SHL RCX,0x5.
        const int64_t bytes = static_cast<int64_t>(arr->capacity) << 5;
        arr->data = static_cast<Demand*>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(bytes), 4));
    }

    // 14002b009-14002b01b: if the buffer pointer did not change (the request
    // fitted in the existing capacity) only the newly exposed tail needs
    // touching, so start at oldCount. If it did change, every live element must
    // be re-emitted into the fresh buffer, so start at 0.
    //   - grow with realloc : start 0        -> copy [0,oldCount), clear the rest
    //   - grow in place     : start oldCount -> clear the new tail only
    //   - shrink            : start oldCount >= newCount -> loop skipped
    //                         (count drops; buffer and capacity are untouched)
    int64_t i = (oldData == arr->data) ? static_cast<int64_t>(oldCount) : 0;

    for (; i < static_cast<int64_t>(newCount); ++i) {
        Demand* dst = arr->data + i;
        if (i < static_cast<int64_t>(oldCount)) {
            // 14002b213: carried over from the old buffer.
            if (dst != nullptr) CopyElement(dst, oldData + i);
        } else {
            // 14002b250: brand-new element, default-constructed.
            if (dst != nullptr) ClearElement(dst);
        }
    }

    // 14002b271-14002b279: free the old buffer exactly once, and only if it was
    // really replaced. A no-realloc resize must not free the live buffer.
    if (oldData != arr->data) {
        System::MemoryAllocator::MemFree(oldData);
    }
}
