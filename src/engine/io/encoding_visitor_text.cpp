// ============================================================================
// src/engine/io/encoding_visitor_text.cpp — the two remaining IO::Encoding
// vtables: VISITOR (0x1801559f8) and TEXT (0x1801559d0). 2026-09-12 cutover walk.
// ============================================================================
//
// Both tables are UNEXPORTED, 5 slots each, the same shape as the BINARY table
// 0x1801554a8 ported in encoding.cpp: three self-ignoring constants, then
// CreateReader (+0x18) and CreateWriter (+0x20). Neither table was minted, so
// at cutover `??0VisitorStream` installed an Encoding whose vtable resolved to
// NULL and the gate died in MakeCodec reading vtable+0x20 — the "READ of 0x20"
// wall recorded in STATUS.md.
//
// Every body below is transcribed from the shipped RETAIL bytes (pefile +
// objdump on Runtime-x64.dll, not the profile build, not decompiled C — Ghidra
// has no function at any of the ten addresses).
//
// VISITOR table 0x1801559f8 (bounded by ??_7VisitorStream@IO@@6B@ at 0x180155a20):
//   slot 0  0x1800bafe0   32 C0 C3           -> false
//   slot 1  0x1800bafd0   32 C0 C3           -> false
//   slot 2  0x1800bafc0   B8 03 00 00 00 C3  -> 3
//   slot 3  0x1800baf00   CreateReader — IDENTICAL BYTES to slot 4
//   slot 4  0x1800bae40   CreateWriter
//     EnterCriticalSection(_singleton+0x640) / MemoryAllocator::Allocate(0x28,4)
//     / LeaveCriticalSection  — the inlined ?MemAlloc@MemoryAllocator@System@@
//     trio, same identification as encoding.cpp. Then, if non-null:
//       MOV [RDI],0x1801553b0        ??_7ReaderWriter  (inlined base ctor)
//       MOV byte [RDI+0x08],0        allowCustomSerialize
//       MOV [RDI+0x10],RSI           serializer
//       MOV RCX,[RSI]; MOV [RDI+0x18],RCX   serializer->stream
//       MOV [RDI],0x1801558b0        ??_7Visitor@IO@@6B@
//       MOV [RDI+0x20],EBP           version (R8D)
//     No stream header is consumed or emitted — unlike the Binary pair. RCX
//     (the Encoding) is never read. Allocation failure returns NULL unconstructed.
//
// TEXT table 0x1801559d0 (bounded by the Visitor table):
//   slot 0  0x1800b9ab0   32 C0 C3           -> false
//   slot 1  0x1800b9aa0   B0 01 C3           -> true
//   slot 2  0x1800b9a90   B8 01 00 00 00 C3  -> 1
//   slot 3  0x1800b9a80   33 C0 C3           -> NULL   (text serialization was
//   slot 4  0x1800b9a70   33 C0 C3           -> NULL    stripped from the DLL)
//
// The {IsBinary, IsText, GetKind} names of slots 0..2 are Serializer.h's
// descriptive naming, not attested: Binary = {true,false,2},
// Text = {false,true,1}, Visitor = {false,false,3}.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "io/Serializer.h"
#include "system/MemoryAllocator.h"
#include <new>

namespace RtIo {

namespace {

// The shared body of 0x1800baf00 and 0x1800bae40 (byte-identical):
// MemAlloc(0x28,4), then (ENDGAME B.3.9 IO) the real `IO::Visitor` ctor in
// place -- the ReaderWriter base stores (1800baeb3..c1) and the version dword
// (1800baecf). A failed allocation returns null unconstructed.
IO::ReaderWriter* MakeVisitor(IO::Serializer& ser, int32_t version) {
    void* p = ::System::MemoryAllocator::MemAlloc(sizeof(IO::Visitor), 4);
    if (p == nullptr) return nullptr;                      // XOR EAX,EAX path
    return ::new (p) IO::Visitor(ser, version);
}

}  // namespace

// ---------------------------------------------------------------------------
// ENDGAME B.3.9 IO -- the ten slots are `IO::VisitorEncoding`'s and
// `IO::TextEncoding`'s methods (src/engine/io/Serializer.h; the four factory
// bodies are below, the six constants inline in the header). The stream
// constructors build the encodings as the real classes, so both unexported
// minted tables (0x1801559f8, 0x1801559d0) and the ten by-address slot doors
// that filled them were retired together.
// ---------------------------------------------------------------------------

}  // namespace RtIo

// ===========================================================================
// IO::VisitorEncoding / IO::TextEncoding — the four factory bodies.
// ===========================================================================
// Unchanged in substance from the doors above: Visitor's two are the shared
// `MakeVisitor` (retail's 0x1800baf00 and 0x1800bae40 are BYTE-IDENTICAL, which
// is why one helper serves both), and Text's two are retail's bare
// `XOR EAX,EAX; RET` — text serialization was stripped from the shipped DLL, so
// returning null here is the faithful body and not a stub.
namespace IO {

ReaderWriter* VisitorEncoding::CreateReader(Serializer& ser, int version) {
    return RtIo::MakeVisitor(ser, version);
}

ReaderWriter* VisitorEncoding::CreateWriter(Serializer& ser, int version) {
    return RtIo::MakeVisitor(ser, version);
}

ReaderWriter* TextEncoding::CreateReader(Serializer& ser, int version) {
    (void)ser; (void)version;
    return nullptr;     // 0x1800b9a80 is `XOR EAX,EAX; RET`
}

ReaderWriter* TextEncoding::CreateWriter(Serializer& ser, int version) {
    (void)ser; (void)version;
    return nullptr;     // 0x1800b9a70 is `XOR EAX,EAX; RET`
}

}  // namespace IO
