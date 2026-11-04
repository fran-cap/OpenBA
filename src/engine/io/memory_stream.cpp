// ============================================================================
// src/engine/io/memory_stream.cpp — Wave 9 lane PKG2, rung 4b-ii
// ============================================================================
//
// IO::MemoryStream's six virtual bodies: Write, Read, SetCurrentPos,
// GetCurrentPos, GetSize and Close.
//
// WHY THESE, AND WHY FIRST
//
// `CompressedStream::Open` — 4b's actual target — reads its header and block
// directory THROUGH a Stream, and in `ReadPackfile` that Stream is a
// MemoryStream over the freshly-read file header. So `Open` cannot be diffed at
// all until the stream it reads through is ours on both sides: an oracle that
// runs ours and theirs side by side would otherwise have one of them advancing
// a cursor the other cannot see. That is sequencing by what the ORACLE needs
// rather than by what the call graph shows, and it is the same reasoning the
// whole bottom-up decomposition rests on.
//
// THE LAYOUT WAS ALREADY IN THE TREE. src/engine/io/Stream.h carries the
// complete, disassembly-pinned MemoryStream — _data +0x18, _ownsData +0x20,
// _cursor +0x24, _size +0x28, stride 0x30 — and the full vtable including
// `[7] 0x1800ae9e0 Close <-- +0x38`. Not one byte of it needed re-deriving.
// This file exists partly as evidence for the rule 4b-i arrived at: grep the
// types header before opening the PE.
//
// All six mangled names were checked against the shipped export table before a
// line was written (reference_resolveexport_phantom_names). Two of them are
// spelled `UEBAI` — const, returning UNSIGNED int — which is why the two
// getters below return uint32_t and not int32_t.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "io/Stream.h"

// ============================================================================
// ENDGAME B.3.9 IO: THE CLASS IS CONCRETE.
// ============================================================================
//
// `??_7MemoryStream@IO@@6B@` is the compiler's table now: `IO::MemoryStream`'s
// real constructor (Stream.h) stores it, so the six slot doors that used to sit
// here -- one qualified call each into the members below, registered by name
// and by address so the minted table could name them -- have no reader and are
// gone, with their registration. Slot 0 (0x1800242a0) is the inline
// `MemoryStream::DeletingDtor` in Stream.h. The by-name ??0/??1/Open/Create
// doors stay in compressedstream.cpp for their callers outside the class.
namespace IO {

// ============================================================================
// ?Read@MemoryStream@IO@@UEAA_NHPEAX@Z                       retail 0x1800ae950
// ============================================================================
// THE BOUND CHECK IS 32-BIT AND SIGNED, and that is not an accident of the
// decompiler's rendering. Retail sign-extends both the cursor and the count
// into 64-bit registers (`MOVSXD RCX,[RCX+0x24]` / `MOVSXD RDI,EDX`) and then
// deliberately narrows again for the test: `LEA EAX,[RCX + RDI*1]` is a 32-BIT
// add, and `CMP EAX,[RBX+0x28]; JG` is a 32-BIT SIGNED compare. Writing the
// check in 64-bit — the obvious "cleanup" — changes behaviour for a cursor+count
// that overflows int32, because retail's sum WRAPS and then compares small.
// The offset arithmetic that follows really is 64-bit, so the two widths are
// transcribed separately rather than unified.
//
// NOT RETAIL (review F-016 / F-053, UB + plain hardening): that wrap is signed
// overflow, and retail's test also admits a negative cursor (a block
// sourceOffset >= 0x80000000) and a negative count, which memcpy then takes as
// a read before the buffer or a 2^64-byte copy. All three are rejected and the
// sum is taken in 64 bits; a zero count skips the memcpy, whose buffer may be
// null (UB even for 0 bytes). For a cursor in [0, size] and a count >= 0 that
// fits, the outcome is retail's.
bool MemoryStream::Read(int count, void* dst) {

    const int64_t cursor = static_cast<int32_t>(_cursor);        // 1800ae95d MOVSXD
    const int64_t n      = count;                                // 1800ae961 MOVSXD
    if (cursor < 0 || n < 0)                                     // NOT RETAIL (F-016)
        return false;
    const int64_t end    = cursor + n;                           // 1800ae964 LEA (64-bit here, F-053)

    if (end > static_cast<int32_t>(_size)) {                     // 1800ae96a CMP/JG
        return false;                                            // 1800ae991
    }
    if (n == 0)                                                  // NOT RETAIL (UB): memcpy(nullptr, p, 0)
        return true;
    std::memcpy(dst, static_cast<const uint8_t*>(_data) + cursor,  // CAST-OK: byte_io
                static_cast<size_t>(n));                         // 1800ae97c
    _cursor += static_cast<uint32_t>(n);                         // 1800ae981
    return true;
}

// ============================================================================
// ?Write@MemoryStream@IO@@UEAA_NHPEBX@Z                      retail 0x1800ae8f0
// ============================================================================
// Byte-for-byte the same body as Read with the memcpy operands swapped —
// including the same 32-bit signed bound check, and including the fact that it
// will NOT grow the buffer: a write past _size fails rather than reallocating.
// NOT RETAIL: the same three rejections and the zero-count skip as Read (F-016 / F-053).
bool MemoryStream::Write(int count, const void* src) {

    const int64_t cursor = static_cast<int32_t>(_cursor);        // 1800ae8fd
    const int64_t n      = count;                                // 1800ae901
    if (cursor < 0 || n < 0)                                     // NOT RETAIL (F-016)
        return false;
    const int64_t end    = cursor + n;                           // 1800ae904 (64-bit here, F-053)

    if (end > static_cast<int32_t>(_size)) {                     // 1800ae90a
        return false;                                            // 1800ae92e
    }
    if (n == 0)                                                  // NOT RETAIL (UB): memcpy(p, nullptr, 0)
        return true;
    std::memcpy(static_cast<uint8_t*>(_data) + cursor, src,  // CAST-OK: byte_io
                static_cast<size_t>(n));                         // 1800ae919
    _cursor += static_cast<uint32_t>(n);                         // 1800ae91e
    return true;
}

// ============================================================================
// ?SetCurrentPos@MemoryStream@IO@@UEAA_NH@Z                  retail 0x1800ae9b0
// ============================================================================
// Three instructions, and the notable thing is what is ABSENT: there is NO
// bounds check. Retail stores whatever it is given and returns true, so a seek
// past the end succeeds here and the following Read is what fails. A port that
// "helpfully" validated would change which of the two calls reports the error.
bool MemoryStream::SetCurrentPos(int pos) {
    _cursor = static_cast<uint32_t>(pos);        // 1800ae9b0
    return true;                                 // 1800ae9b3
}

// ============================================================================
// ?GetCurrentPos@MemoryStream@IO@@UEBAIXZ                    retail 0x1800ae9c0
// ?GetSize@MemoryStream@IO@@UEBAIXZ                          retail 0x1800ae9d0
// ============================================================================
// Two instructions each. `UEBAI` — const, returning unsigned.
uint32_t MemoryStream::GetCurrentPos() const {
    const uint32_t v = _cursor;                  // 1800ae9c0
    return v;
}

uint32_t MemoryStream::GetSize() const {
    const uint32_t v = _size;                    // 1800ae9d0
    return v;
}

// ============================================================================
// ?Close@MemoryStream@IO@@UEAAXXZ                            retail 0x1800ae9e0
// ============================================================================
// A BARE RET. This is vtable slot 7 (+0x38) — the slot `ReadPackfile` calls
// virtually at 0x1800b6042 on its MemoryStream, which is therefore a provable
// no-op at that site.
//
// It is worth recording HOW this was nearly got wrong, because the shape is the
// one that has produced three Slot6/Slot7 mix-ups on this project already.
// MemoryStream's constructor stores TWO vtables into the same slot, thirteen
// bytes apart:
//     1800ae72d  LEA RAX,[0x1801549a0]   ; the BASE (Stream) table
//     1800ae734  MOV qword ptr [RCX],RAX
//     1800ae740  LEA RAX,[0x1801555b0]   ; the DERIVED table, overwrites it
//     1800ae747  MOV qword ptr [RCX],RAX
// Slot 7 of the BASE table is NULL. Slot 7 of the DERIVED table is this
// function. Stopping at the first store — the one that looks like "the vtable
// store" — yields a confident "slot 7 is a null call at cutover", and the base
// table's slots 2..6 all being one address reads like a plausible stub table,
// which CORROBORATES the wrong reading. A self-consistent wrong answer.
//
// (And it is the SAME +0x38 that means `Open` on the FileStream branch — the
// one place the two halves of the family genuinely disagree about a slot.)
//
// Retail's is a no-op, so ours must be too: a Close that actually released
// anything would double-free against the dtor, which is where the release
// really lives (0x1800ae890).
void MemoryStream::Close() {
    // 1800ae9e0: RET — deliberately empty.
}

}  // namespace IO
