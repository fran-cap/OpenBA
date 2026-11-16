// src/game/Work/marketwork_bst_ba70.h
// 0x14002ba70 -- NOT a BST helper.  Ghidra's name `MarketWork_helper_ba70` is a
// MISNOMER inherited from the surrounding MarketWork AVL/BST block: the body is a
// fully inlined median-of-three QUICKSORT with an explicit 62-deep work stack and a
// selection-sort cutoff at 8 elements.  It touches no tree node, allocates nothing,
// and calls nothing (verified leaf -- zero CALL instructions in 14002ba70-14002bd74).
//
// It sorts a flat, caller-owned array of 0x18-byte (24-byte) records in place,
// ascending by the SIGNED int32 sort key stored at byte offset +0x08 of each record.
// The remaining 20 bytes of each record are opaque payload and are only ever moved
// as three whole qwords, never interpreted.
//
//   record layout (0x18 bytes, only +0x08 is ever read):
//     +0x00  qword  payload
//     +0x08  int32  SORT KEY  (signed; compared with CMP/JGE/JL)
//     +0x0C  ...    payload
//     +0x10  qword  payload
//
// RELATION TO 0x14002b680 / 0x14002bd80 (marketwork_bst_b680.cpp): those are the
// SAME sort SHAPE -- median-of-three quicksort, cutoff 8, equal-run collapse, 62-deep
// explicit lo/hi stack -- but a DIFFERENT instantiation.  Do not assume the sibling's
// record parameters here; they were checked and they differ:
//     0x14002b680/bd80 : 0x20-byte records, FLOAT key, compared with COMISS
//     0x14002ba70      : 0x18-byte records, SIGNED INT32 key at +0x08, compared with
//                        CMP/JGE/JL/CMOVL
// This function contains ZERO SSE instructions -- a mnemonic filter for
// COMISS/UCOMISS/COMISD/UCOMISD/MOVSS/MOVSD/ADDSS/MULSS/CVTSI2SS over the whole body
// returns nothing, and no XMM register appears anywhere in the 220 instructions.  So
// the COMISS/NaN porting caveat that applies to the sibling lane does NOT apply here.
// Likewise there is no SHL count-scaling: the count is scaled by the
// LEA [RBX+RBX*2] / LEA [RCX+RBX*8] pair (x3 then x8 = x24).  The only IMULs in the
// body (0x14002bab9, 0x14002bce8, 0x14002bcff) are the /0x18 magic-multiply.
//
// See marketwork_bst_ba70.cpp for the offset-by-offset verification notes.
#pragma once

#include <cstdint>
#include "Work/MarketWork.h"   // MarketWork::Take

extern "C" {

// 0x14002ba70 -- in-place ascending sort of `count` 24-byte records at `base`,
// keyed on the int32 at record+0x08.
//   base  [RCX] : first record of the array (24-byte stride)
//   count [EDX] : ELEMENT COUNT (not a byte size, not a tree node count).
//                 Sign-extended by MOVSXD at 0x14002ba87, then `last = base +
//                 (count-1)*0x18`.  The sole retail caller (0x1400f168d) guards
//                 the call with `CMP R13D,0x1 / JLE`, so retail never passes
//                 count <= 1; passing 0 here would underflow `last` exactly as
//                 retail would.  This port keeps that faithful -- no guard added.
// Returns nothing (RAX is left holding scratch at the RET at 0x14002bd74).
void MarketBst_ba70(MarketWork::Take* base, int count);

} // extern "C"
