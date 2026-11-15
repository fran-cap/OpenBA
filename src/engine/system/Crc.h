#pragma once
// ============================================================================
// src/engine/system/Crc.h — the REAL `System::CRC`
// ============================================================================
//
// ENDGAME B.3.4b SYSTEM (4). Design: B34B_RULES.md §1.1.
//
// *** IT IS A CLASS WITH STATIC MEMBERS. *** Same test as `Timer.h`'s, same
// answer, and it is the smallest instance of it in the namespace:
//
//   `SA` = PUBLIC STATIC member function
//       ?Compute@CRC@System@@SAIPEBXHI@Z   0x1800c5490   — PORTED, R + A
//       ?Compute@CRC@System@@SAIPEBDI@Z    0x1800c5430   — the `const char*`
//                                                          overload, UNPORTED
//   `0` = PRIVATE STATIC DATA member
//       ?_crcTable@CRC@System@@0PAIA       0x1801f9080
//
// A free function cannot carry `@CRC@System@@SA…`, so the class is retail's,
// not an invention of this header. Note the CLASS NAME IS `CRC`, all three
// letters capital — not `Crc`. The file is named `Crc.h` to match this tree's
// header convention; the class inside is spelled retail's way (decision 5).
//
// *** NO INSTANCE STATE, SO NO LAYOUT AND NO `sizeof` ASSERT. *** One static
// function over one static table.
//
// *** THE SECOND OVERLOAD IS NOT DECLARED. *** `?Compute@CRC@System@@SAIPEBDI@Z`
// (0x1800c5430) exists in retail and has no body, no registration and no caller
// anywhere in this tree. An uncalled port is an unverified port; its address is
// recorded in B34B_RULES §1.1 for whoever needs it.
//
// *** `_crcTable` IS NOT A CUTOVER ROW, unlike Timer's and Error's statics. ***
// It is in the retail export table but carries no `CUTOVER_REQUIRED` entry, and
// `crc.cpp` does not read it: the table is GENERATED LOCALLY, verified
// byte-for-byte against the textbook reflected CRC-32 table, so the body
// survives cutover on its own. Rule B4b-4 therefore has nothing to say about
// this class, which is why it is the one of the three with no redirect debt.
//
// *** WHAT `Compute` DOES NOT DO. *** It never complements the seed on entry.
// It DOES complement on exit (`NOT EAX` before the `RET`). A caller wanting a
// standard CRC-32 passes 0xFFFFFFFF itself; a caller chaining chunks into one
// standard CRC passes `~previousResult`. `src/engine/io/persistence_worker.cpp`'s
// `Crc32Chunked` does not: it continues each chunk from the previous chunk's
// complemented result.
// ============================================================================

#include <cstdint>

namespace System {

class CRC {
public:
    // ?Compute@CRC@System@@SAIPEBXHI@Z  0x1800c5490
    // RCX = data, EDX = byte count, R8D = running crc. `count <= 0` skips the
    // loop entirely and still complements — retail's `TEST EDX,EDX / JLE done`.
    static uint32_t Compute(const void* data, int count, uint32_t crc);
};

}  // namespace System
