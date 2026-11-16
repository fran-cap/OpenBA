// src/game/Work/marketwork_bst_b680.h
// Declarations for the two MarketWork "helper_b680" cluster functions.
//
// NAMING WARNING: these are NOT BST helpers despite the file name / the
// MarketWork_helper_* Ghidra labels.  0x14002b680 is a QUICKSORT and
// 0x14002bd80 is its selection-sort small-range fallback, both operating on a
// flat array of 0x20-byte records keyed on a float at record+0x14.
//
// See marketwork_bst_b680.cpp for the offset-by-offset verification notes.

#pragma once

#include <cstdint>
#include "Work/MarketWork.h"   // MarketWork::Demand

extern "C" {

// 0x14002b680 — void __fastcall(void* base [RCX], int count [EDX])
// Sorts `count` records of 0x20 bytes at `base` ASCENDING by the float at +0x14.
// Retail returns nothing meaningful in RAX; the sole caller (0x1400f0e82 inside
// MarketWork_Serialize) discards it and pre-guards with `if (count > 1)`.
void MarketBst_b680(MarketWork::Demand* base, int count);

// 0x14002bd80 — void __fastcall(void* first [RCX], void* last [RDX])
// Selection sort of the INCLUSIVE range [first, last] (last points AT the final
// record, not one past it).  Returns immediately when last <= first.
void MarketBst_bd80(MarketWork::Demand* first, MarketWork::Demand* last);

} // extern "C"
