// ============================================================================
// src/engine/io/serializer_class.cpp — `IO::Serializer`'s member bodies.
// ENDGAME B.3.4 block 3.
// ============================================================================
//
// THIS FILE IS DELIBERATELY SELF-CONTAINED, and that is its whole reason to
// exist as a separate translation unit.
// ---------------------------------------------------------------------------
// Nothing in the BODIES needs more than the two layout headers: the constructor
// reaches the factory through the stream's Encoding VTABLE (a virtual call),
// every Serialize member reaches its target through the ReaderWriter's vtable,
// `GetEncoding` is two loads and the destructor is one slot-0 call. So the
// bodies live here, this file links into BOTH the exe (CMakeLists.txt's
// `GLOB_RECURSE` over `src/engine/*.cpp`) and the unit build
// (tests/unit/sources.cmake), and `tests/unit/io_vtable_abi_test.cpp`
// exercises the REAL class rather than a local mirror of it. A mirror tests itself.
//
// ---------------------------------------------------------------------------
// WHAT MAY NOT CHANGE IN THIS FILE, EVER
// ---------------------------------------------------------------------------
// Constructing an `IO::Serializer` consumes (reading) or emits (writing) a
// TWELVE-BYTE stream header, and destroying one reads or back-patches FOUR
// more. The header block of src/engine/io/Serializer.h carries the retail
// disassembly of both. Therefore:
//
//   * the constructor makes exactly ONE factory call, with exactly the three
//     arguments retail passes, in retail's order, with `version` reaching the
//     factory as the third;
//   * the destructor makes exactly ONE slot-0 call with flags == 1, under
//     retail's own `TEST RCX,RCX` and nothing else;
//   * no null check, no guard, no early-out and no reordering is added
//     anywhere (CLAUDE.md: retail's unguarded derefs are load-bearing, and a
//     guard skips the behaviour rather than restoring it).
//
// THE 22 BODIES CALL THE READERWRITER BY NAME (B3X W0, the normal-virtual
// cutover's "by name first" wave). B.3.4's §3.1 kept raw `rw_vtbl::k*` slot
// calls here while live ReaderWriters wore the MINTED tables; B.3.9 IO retired
// those rows and every ReaderWriter is now built by its real constructor, so
// `_readerWriter->SerializeInt(v, name)` dispatches through exactly the slot
// the old `rw_vtbl::kScalarInt` read did (Serializer.h:489-533 declares the 26
// slots in retail order, the unit tests pin each offset). The by-name call is
// what lets the class take `virtual ~ReaderWriter()` without a slot moving
// under a caller.
//
// THE `rt::io::Stream` BRIDGE CASTS ARE GONE (B.3.4 block 4). Block 3 read
// `direction` and `encoding` through the POD because `IO::Stream` was only
// forward-declared; it is a real class now, so the constructor and
// `GetEncoding` read `_direction` / `_encoding` off the class directly. NO
// SIGNATURE MOVED — block 3 already carried retail's final `(Stream&, int)`,
// exactly as block 2 did for `IO::ReaderWriter`.
//
// PORTABILITY: no <windows.h>, no port registry, no allocator. The only
// includes are the two layout headers (PLAN §4.1 decision 1).
// ============================================================================

#include "io/Serializer.h"
#include "io/Stream.h"

#include <cstdint>

// Every forwarder below is retail's `MOV RCX,[RCX+0x08]; MOV RAX,[RCX];
// JMP [RAX+SLOT]` spelled as a virtual call on `_readerWriter`. Faithfully
// UNGUARDED: retail dereferences `_readerWriter` and its vtable with no null
// test, and that fault is how a Serializer used before construction announces
// itself. `name` is forwarded although every retail body ignores it (the
// scalar bodies clobber R8 before the tail jump); the exported signature
// carries it and the binary format has no field names either way.
namespace IO {

// ---------------------------------------------------------------------------
// IO::rw_stream -- the four IO::Stream slots the ReaderWriter family's inline
// constructors and destructors drive (Serializer.h). Out of line HERE because
// Serializer.h only forward-declares `IO::Stream` (Stream.h includes it, not
// the reverse). By name: each is the slot the old byte offset named --
// +0x10 Write, +0x18 Read, +0x20 SetCurrentPos, +0x28 GetCurrentPos.
// ---------------------------------------------------------------------------
namespace rw_stream {
bool Write(Stream* s, int32_t n, void* src) {
    return s->Write(n, src);   // slot 2 +0x10
}
bool Read(Stream* s, int32_t n, void* dst) {
    return s->Read(n, dst);    // slot 3 +0x18
}
void SetPos(Stream* s, int32_t pos) {
    s->SetCurrentPos(pos);     // slot 4 +0x20
}
int32_t GetPos(Stream* s) {
    return static_cast<int32_t>(s->GetCurrentPos());  // slot 5 +0x28
}
}  // namespace rw_stream

// ============================================================================
// Lifetime — 0x1800b9960 / 0x1800b99b0
// ============================================================================
//
// THE CONSTRUCTOR, transcribed instruction for instruction (re-read from the
// shipped bytes 2026-09-19):
//
//     MOV [RCX],RDX                 _stream = &stream
//     MOV qword [RCX+0x08],0        _readerWriter = nullptr
//     CMP dword [RDX+0x08],0        stream.direction
//     MOV RCX,[RDX+0x10]            the Encoding policy object
//     MOV RAX,[RCX]                 its vtable
//     MOV RDX,RBX                   arg 2 = this
//     JNZ writing
//     CALL [RAX+0x18] / [RAX+0x20]  arg 3 = R8D = version, NEVER TOUCHED
//     MOV [RBX+0x08],RAX
//     MOV RAX,RBX ; RET             (MSVC ctors return `this`; ours cannot and
//                                    does not need to — no caller reads it)
//
// The `_readerWriter = nullptr` store before the factory call is retail's and
// is NOT dead: the factory is handed `this` and can read the field.
//
// ARITY 3 is pinned by R8D's untouched liveness across the whole body, and the
// thing being called is
// `??0Binary{Reader,Writer}@IO@@QEAA@AEAVSerializer@1@H@Z` — `(Serializer&,
// int)`. `BinaryWriter` stores that int and WRITES IT TO THE STREAM, so losing
// the third argument would silently change the save format's version word.
Serializer::Serializer(Stream& stream, int version) {
    _stream       = &stream;
    _readerWriter = nullptr;

    // B.4 R-IO: the factory is called BY NAME. `IO::Encoding` declares the
    // five slots in retail's order (Serializer.h: slot 3 +0x18 CreateReader,
    // slot 4 +0x20 CreateWriter, no destructor slot), and B.3.9 IO retired the
    // minted tables, so the virtual call reads the same slot the old
    // `encoding_vtbl` constant named. Direction 0 is reading.
    Encoding* factory = stream._encoding;
    _readerWriter = (stream._direction == rt::io::kDirectionReading)
                        ? factory->CreateReader(*this, version)
                        : factory->CreateWriter(*this, version);
}

// THE DESTRUCTOR, 0x1800b99b0:
//     MOV RCX,[RCX+0x08] ; TEST RCX,RCX ; JZ out
//     MOV RAX,[RCX] ; MOV EDX,1 ; JMP [RAX]
// Flags == 1 is MSVC's scalar deleting destructor: it runs
// `~Binary{Reader,Writer}` — which reads four further bytes on the read side
// and back-patches the length placeholder on the write side — AND frees the
// block. So this is a stream operation with a POSITION, not a cleanup.
//
// The `if` is RETAIL'S OWN `TEST RCX,RCX`, not a guard we added.
// Retail does NOT null `_readerWriter` afterwards, so neither do we: the
// destructor is NOT idempotent and nothing may make it look as though it were.
Serializer::~Serializer() {
    ReaderWriter* rw = _readerWriter;
    if (rw) {
        delete rw;   // slot 0, flags == 1
    }
}

// ============================================================================
// Accessors
// ============================================================================
// 0x1800070b0 — the same three-instruction forwarder, tail-jumping +0x08.
int Serializer::GetVersion() const {
    return _readerWriter->GetVersion();   // slot 1 +0x08
}

// 0x180006210 — `MOV RAX,[RCX]; MOV EAX,[RAX+0x8]; RET`. Two loads, no
// dispatch, no side effect, and — like `GetEncoding` — it answers about the
// STREAM, not about this Serializer. Faithfully UNGUARDED: retail dereferences
// `_stream` with no null test here, and so does every one of the ~37 caller
// sites this member replaces (they are transcriptions of the exe's INLINED
// copy of these same two loads).
//
// ENDGAME B.3.4 callers batch H. serializer.cpp's banner listed this under
// "NOT PORTED, because our tree does not call them"; the tree calls it now.
Direction Serializer::GetDirection() {
    return static_cast<Direction>(_stream->_direction);
}

// 0x180006290 — `MOV RAX,[RCX]; MOV RAX,[RAX+0x10]; RET`. Two loads, no
// dispatch, no side effect. It answers with the STREAM's encoding policy
// object, which is a different class from this Serializer's ReaderWriter.
const Encoding& Serializer::GetEncoding() {
    return *_stream->_encoding;
}

// ============================================================================
// The 18 ported `Serialize` overloads + SerializeBytes
// ============================================================================
// Each is retail's three instructions and nothing else. The slot constant on
// each line is the tail-jump displacement decoded from that member's own bytes
// in the shipped DLL, independently confirmed by the contents of
// ??_7BinaryReader@IO@@6B@ (0x1801552e0) and ??_7BinaryWriter@IO@@6B@
// (0x180155210): the entry at that offset is the exported
// `?Serialize@Binary{Reader,Writer}@IO@@` whose mangled parameter type matches.

// ?Serialize@Serializer@IO@@QEAAXAEA_JPEBD@Z  0x180035000  +0x90
// ENDGAME B.3.4b SYSTEM (5). The 18th overload, and the only one added since
// block 3. Its justifying call site is `System::Time::Serialize` (retail
// 0x1800ca740), which hand-inlined this very slot until this member existed —
// see the declaration's comment in src/engine/io/Serializer.h. Identical in
// shape to every scalar below it; the width is the whole difference, and it is
// the reason the site could not use the `int&` door.
void Serializer::Serialize(int64_t& v, const char* name) {
    _readerWriter->SerializeInt64(v, name);
}
void Serializer::Serialize(bool& v, const char* name) {
    _readerWriter->SerializeBool(v, name);
}
void Serializer::Serialize(signed char& v, const char* name) {
    _readerWriter->SerializeSChar(v, name);
}
void Serializer::Serialize(unsigned char& v, const char* name) {
    _readerWriter->SerializeUChar(v, name);
}
void Serializer::Serialize(short& v, const char* name) {
    _readerWriter->SerializeShort(v, name);
}
void Serializer::Serialize(unsigned short& v, const char* name) {
    _readerWriter->SerializeUShort(v, name);
}
void Serializer::Serialize(int& v, const char* name) {
    _readerWriter->SerializeInt(v, name);
}
void Serializer::Serialize(unsigned int& v, const char* name) {
    _readerWriter->SerializeUInt(v, name);
}
void Serializer::Serialize(float& v, const char* name) {
    _readerWriter->SerializeFloat(v, name);
}

void Serializer::Serialize(int n, char* d, const char* name) {
    _readerWriter->SerializeCharArray(n, d, name);
}
void Serializer::Serialize(int n, signed char* d, const char* name) {
    _readerWriter->SerializeSCharArray(n, d, name);
}
void Serializer::Serialize(int n, bool* d, const char* name) {
    _readerWriter->SerializeBoolArray(n, d, name);
}
void Serializer::Serialize(int n, unsigned char* d, const char* name) {
    _readerWriter->SerializeUCharArray(n, d, name);
}
void Serializer::Serialize(int n, short* d, const char* name) {
    _readerWriter->SerializeShortArray(n, d, name);
}
void Serializer::Serialize(int n, unsigned short* d, const char* name) {
    _readerWriter->SerializeUShortArray(n, d, name);
}
void Serializer::Serialize(int n, int* d, const char* name) {
    _readerWriter->SerializeIntArray(n, d, name);
}
void Serializer::Serialize(int n, unsigned int* d, const char* name) {
    _readerWriter->SerializeUIntArray(n, d, name);
}
void Serializer::Serialize(int n, float* d, const char* name) {
    _readerWriter->SerializeFloatArray(n, d, name);
}

// 0x1800346d0 — the same three instructions, but the payload is `void*`, which
// is why retail gives it its own name instead of a nineteenth overload.
void Serializer::SerializeBytes(int n, void* buf, const char* name) {
    _readerWriter->SerializeBytes(n, buf, name);   // slot 25 +0xC8
}

// ??1ReaderWriter@IO@@UEAA@XZ 0x1800b9150: a bare base-table stamp, which the
// compiler emits. The body of the PURE virtual destructor (Serializer.h).
ReaderWriter::~ReaderWriter() = default;

}  // namespace IO
