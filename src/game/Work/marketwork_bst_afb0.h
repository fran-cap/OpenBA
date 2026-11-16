// src/game/Work/marketwork_bst_afb0.h
// Declaration for the exe helper at 0x14002afb0 (MarketWork batch).
//
// NAMING NOTE: the batch calls this a "BST helper" and Ghidra names it
// MarketWork_helper_afb0, but the body is NOT tree code at all -- it is the
// resize/grow routine for a plain dynamic ARRAY of 0x20-byte elements.
// See marketwork_bst_afb0.cpp for the offset-by-offset proof. Treat "Bst" in the
// export name below as a label, not a description.

#pragma once

#include <cstdint>
#include "Work/MarketWork.h"   // MarketWork::DemandList

// __fastcall void MarketBst_afb0(ArrayDesc* arr [RCX], int newCount [EDX])
//
// arr points at a 0x10-byte array descriptor:
//     +0x00  void*  data       (MemAlloc'd buffer of capacity * 0x20 bytes)
//     +0x08  int32  count      (live element count)
//     +0x0C  int32  capacity   (allocated element count)
// Declared on MarketWork::DemandList* (Ghidra recovered the retail signature as
// undefined __fastcall MarketWork_helper_afb0(longlong*, int)); the true
// return type is void (RAX is never set on purpose -- verified in disasm).
extern "C" void MarketBst_afb0(MarketWork::DemandList* arr, int32_t newCount);
