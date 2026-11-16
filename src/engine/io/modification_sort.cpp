// ============================================================================
// src/engine/io/modification_sort.cpp — BATCH 8b
//
//   IO::Modification::SortMods       retail 0x180028760  (1381 bytes)
//   IO::Modification::SortModsSmall  retail 0x180028fc0  ( 267 bytes)
//
// The sort at the tail of `?RefreshList@Modification@IO@@` (0x1800b0338). Both
// addresses are unexported — no name in retail and none in the profile build —
// so both are named here `RtIo_Modification_SortMods` /
// `RtIo_Modification_SortModsSmall`.
//
// ---------------------------------------------------------------------------
// WHY IT WAS DECLINED TWICE, AND WHY THAT NO LONGER APPLIES
// ---------------------------------------------------------------------------
// modification_refresh.cpp's banner held this row back, correctly, on the
// ground that "its comparator is `order` ascending with a `name` tie-break over
// a population where every record can tie — the exact 'which package wins'
// shape ... decided by the SWAP SEQUENCE, not by sortedness", and that porting
// it as some other sort would silently pick different winners. BATCH 8 declined
// it again for the same reason.
//
// That is an argument against porting it BY RESEMBLANCE, not against porting
// it. It is answered the same way lane COLL answered it for 0x1800264a0: the
// skeleton is TRANSCRIBED, instruction by instruction, in
// ../collection/introsort_msvc.h — the 8-element threshold, the median-of-three
// order, the pivot-slot fixup, the two equal-run walks, the push-the-larger-
// half rule and the 62-entry stack are all retail's — and this file supplies
// only the two things that are specific to `Mod`: the comparator and the
// element swap. The same header is instantiated for Graphics' draw-item sort
// (0x180062d60 / 0x180063b90), which is the same MSVC template with a different
// element.
//
// ---------------------------------------------------------------------------
// THE COMPARATOR, read at 0x1800287ce..0x1800287f2 and repeated identically at
// six more sites (both bodies inline it everywhere):
//
//     CMP EAX,ECX / JL <less> / JG <not less>     ; +0x20, SIGNED 32-bit
//     MOV RDX,[b] / TEST RDX,RDX / JE <not less>  ; b->name == 0  -> NOT less
//     MOV RCX,[a] / TEST RCX,RCX / JE <less>      ; a->name == 0  -> less
//     CALL 0x1800c98a0 / TEST EAX,EAX / JNS <not less>
//
// The last call is `?StrCompare@String@System@@SAHPEBD0@Z`, which this tree
// already ports (src/engine/system/string.cpp). The quicksort body INLINES it
// at its six other sites as a byte-wise signed-char difference; the inline and
// the call are the same function, so the ported one is used at every site.
//
// Note the null order: `b->name` is tested FIRST. A record with no name sorts
// AFTER one that has a name at equal `order`, and two nameless records at equal
// `order` compare neither-less — a tie, resolved by the swap sequence, which is
// exactly why the skeleton had to be retail's.
//
// THE SWAP is a real object swap through the three ported Mod lifecycle
// functions, in retail's order (0x1800287f4):
//
//     Mod tmp;  tmp = *a   via 0x1800101e0 (copy ctor into raw storage)
//               *a  = *b   via 0x180026ef0
//               *b  = tmp  via 0x180026ef0
//               ~tmp       via 0x180011460
//
// The copy ctor zeroes each member of the destination immediately before
// filling it, which is what makes the raw stack storage safe; that is retail's
// own arrangement (modification.cpp's `Mod::Mod(const Mod&)`) and `Modification::Swap` already
// relies on it. Block PKGCACHE trades the bytes instead (ModSwap's note).
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>   // block PKGCACHE: ModSwap

#include "io/Modification.h"
#include "../collection/introsort_msvc.h"
#include "system/string_internal.h"

namespace RtIO {

// The three Mod lifecycle bodies, ported in modification.cpp (lane MOD), are
// declared by Modification.h.

namespace {

// +0x20 is compared SIGNED (`JL`/`JG`), the name pointers with a plain
// null test, and the names with String::StrCompare.
bool ModLess(const Mod& a, const Mod& b) {
    if (a.order != b.order) return a.order < b.order;    // CMP EAX,ECX / JL
    if (b.name._data == nullptr) return false;           // TEST RDX,RDX / JE
    if (a.name._data == nullptr) return true;            // TEST RCX,RCX / JE
    return RtSystem::RtString_StrCompare(a.name._data, b.name._data) < 0;   // JNS -> not less
}

// 0x1800287f4 — retail: `Mod tmp(*a); *a = *b; *b = tmp;` (0x1800101e0, 0x180026ef0 twice,
// ~tmp 0x180011460).
//
// BLOCK PKGCACHE (NOT RETAIL, exact): the two Mods trade their bytes instead. Retail's three copies
// clone each Mod's path set node by node (a MegaMod part holds 50,000-90,000 paths) only to leave
// each slot holding what the other held: the same strings (a copy shares the buffer), the same
// arrays' contents, a set of the same paths in the same shape. A Mod's members are a String /
// Filename buffer pointer each, plain ints, Array headers and the set's {root, count} header;
// nothing points back into a Mod (the set's root has no parent), so a Mod is relocated by copying
// its bytes. What differs is only which allocations hold the values (the clones' node addresses,
// the arrays' capacities), and nothing reads those. A self-swap (the selection pass swaps an
// element already in place) left everything as it was in retail too: each assignment guards its
// self-assign.
void ModSwap(Mod* a, Mod* b) {
    if (a == b)
        return;
    alignas(Mod) unsigned char tmp[sizeof(Mod)];
    std::memcpy(tmp, static_cast<void*>(a), sizeof(Mod));
    std::memcpy(static_cast<void*>(a), static_cast<const void*>(b), sizeof(Mod));
    std::memcpy(static_cast<void*>(b), tmp, sizeof(Mod));
}

}  // namespace

// 0x180028fc0 — the small-range pass, INCLUSIVE bounds (`CMP RDX,RCX / JBE`
// before the prologue is the empty-range early-out).
extern "C" void RtIo_Modification_SortModsSmall(Mod* lo, Mod* hi) {
    rtsort::SelectionSortMaxToHigh(lo, hi, ModLess, ModSwap);
}

// 0x180028760 — `(Mod* first, int32_t count, void* scratch)`. RefreshList
// passes a stack slot as the third argument (`LEA R8,[RBP+0x48]`) and the body
// never reads R8.
extern "C" void RtIo_Modification_SortMods(Mod* data, int32_t count, void*) {
    rtsort::Introsort(data, count, ModLess, ModSwap,
                      [](Mod* lo, Mod* hi) {
                          RtIo_Modification_SortModsSmall(lo, hi);
                      });
}

}  // namespace RtIO
