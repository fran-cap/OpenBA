// ============================================================================
// src/engine/io/serialize_family.cpp — the IO scalar-serialization dispatch
// family: 73 vtable slots across IO::BinaryReader, IO::BinaryWriter and
// IO::Visitor.   Wave 9 lane SERIAL.
// ============================================================================
//
// Lane ENC-2 ported two members of this family (?Serialize@BinaryWriter@IO@@ at
// 0x1800a8fb0 and 0x1800a8fd0, in binary_writer.cpp) and the framing tail around
// them. This file is the other 73. Those two stay where they are and are NOT
// re-registered here — a duplicate registration is last-binding-wins and would
// silently make one of the two bodies dead code.
//
// The three classes are the three concrete IO::ReaderWriter implementations the
// engine produces (src/engine/io/Serializer.h names all three and explains
// why the Visitor exists). Each has the SAME 26-slot vtable shape:
//
//     +0x00        scalar-deleting dtor            (UNEXPORTED, not this lane)
//     +0x08        GetVersion() -> int
//     +0x10        Skip()
//     +0x18..+0x68 Serialize(int n, T* arr, const char* name)   x11 types
//     +0x70..+0xC0 Serialize(T& value,       const char* name)  x11 types
//     +0xC8        SerializeBytes(int n, void* buf, const char* name)
//
// and the eleven types are, in slot order:
//
//     char, bool, float, unsigned __int64, __int64, unsigned int, int,
//     unsigned short, short, unsigned char, signed char
//
// (MSVC manglings D, _N, M, _K, _J, I, H, G, F, E, C.)
//
// ----------------------------------------------------------------------------
// THE SLOT TABLE IS RE-DERIVED HERE, NOT INHERITED
// ----------------------------------------------------------------------------
// Serializer.h's `rw_vtbl` constants were pinned in Wave 4 from the Serializer
// forwarders' tail-jump displacements and from the vtable contents. This lane
// confirmed all eleven scalar slots a THIRD and independent way, out of bytes
// this family carries itself: each ARRAY body ends in a virtual self-call whose
// displacement is the scalar slot for its own type.
//
//     0x1800a8410 Serialize[](char)   ... CALL [RAX+0x70]   == kScalarChar
//     0x1800a8460 Serialize[](bool)   ... CALL [RAX+0x78]   == kScalarBool
//     0x1800a84b0 Serialize[](float)  ... CALL [RAX+0x80]   == kScalarFloat
//     0x1800a8510 Serialize[](u64)    ... CALL [RAX+0x88]   == kScalarUInt64
//     0x1800a8570 Serialize[](i64)    ... CALL [RAX+0x90]   == kScalarInt64
//     0x1800a85d0 Serialize[](uint)   ... CALL [RAX+0x98]   == kScalarUInt
//     0x1800a8630 Serialize[](int)    ... CALL [RAX+0xa0]   == kScalarInt
//     0x1800a8690 Serialize[](ushort) ... CALL [RAX+0xa8]   == kScalarUShort
//     0x1800a86f0 Serialize[](short)  ... CALL [RAX+0xb0]   == kScalarShort
//     0x1800a8750 Serialize[](uchar)  ... CALL [RAX+0xb8]   == kScalarUChar
//     0x1800a87b0 Serialize[](schar)  ... CALL [RAX+0xc0]   == kScalarSChar
//
// All eleven agree with `rw_vtbl`, and the BinaryWriter set is byte-identical to
// the BinaryReader set at the same displacements. That third derivation is worth
// having written down because the profile build DISAGREES about these offsets
// (reference_encoding_vtable_offsets_differ_between_builds.md), so a future lane
// reading a profile decompile will find bool +0x90 / uint +0xb0 / int +0xb8 and
// needs to know which source won and why.
//
// ----------------------------------------------------------------------------
// THE FOUR BODY SHAPES — this whole family is four functions and a table
// ----------------------------------------------------------------------------
//
// (1) SCALAR, BinaryReader (0x1800a8810 + 0x20*i), five instructions:
//
//         MOV RCX,[RCX+0x18]     ; RCX = this->stream
//         MOV R8,RDX             ; R8  = &value   <-- OVERWRITES the `name` arg
//         MOV EDX,<sizeof(T)>
//         MOV RAX,[RCX]
//         JMP [RAX+0x18]         ; TAIL JUMP to Stream::Read(size, buf)
//
//     BinaryWriter's (0x1800a8f10 + 0x20*i) is the same five instructions with
//     `JMP [RAX+0x10]` — Stream::Write. Lane ENC-2 established this shape on two
//     slots; the other nine per class are byte-identical to them apart from the
//     size immediate, which is sizeof(T) for all eleven types.
//
//     `const char* name` is not merely unused, it is CLOBBERED before the call:
//     R8 held it on entry and is overwritten with the value pointer. **The
//     binary format carries no field names and a reader depends entirely on
//     ORDER.** That is a property of the FORMAT, not of these functions.
//
// (2) SerializeBytes (0x1800a8970 / 0x1800a9070), three instructions:
//
//         MOV RCX,[RCX+0x18]
//         MOV RAX,[RCX]
//         JMP [RAX+0x18]         ; reader; writer jumps [RAX+0x10]
//
//     No register shuffling at all, because the incoming (int count, void* buf)
//     ALREADY sit in RDX/R8 exactly as Stream::Read(size, buf) wants them. Same
//     body as the scalar case with the marshalling deleted.
//
// (3) ARRAY (0x1800a8410.. / 0x1800a8b10..), one loop, identical in both
//     classes — the array forms do NOT touch the stream at all:
//
//         TEST EDX,EDX / JLE ret          ; count <= 0 -> nothing, not even a
//                                         ;   vtable load
//         RBX = arr ; RSI = this ; EDI = count
//     loop: MOV RAX,[RSI]                 ; this->vtable  (RELOADED each pass)
//         XOR R8D,R8D                     ; name = nullptr   <-- NOT forwarded
//         MOV RDX,RBX ; MOV RCX,RSI
//         CALL [RAX+<scalar slot>]        ; VIRTUAL self-dispatch
//         ADD RBX,<sizeof(T)> ; DEC RDI ; JNZ loop
//
//     Two things here are load-bearing and neither is guessable:
//
//     * **The array form DROPS `name` and passes nullptr.** It does not forward
//       the array's own name to the elements. Anything downstream that reads the
//       name argument (nothing in this family does — see (1)) sees null for every
//       element of every array.
//     * **The dispatch is VIRTUAL and the vtable is RELOADED INSIDE THE LOOP.**
//       So a subclass overriding only the scalar slot gets its override honoured
//       for array serialization too, and a body that re-stamps `this->vtable`
//       mid-array would be observed. Calling the scalar body directly would be
//       observationally different and is not what is written here.
//
//     The count guard is `<= 0`, signed: a negative count returns rather than
//     looping ~4 billion times.
//
// (4) VISITOR — every one of its 23 Serialize slots, and Skip, is `RET`.
//
//         0x1800bacc0..0x1800bae30, sixteen bytes apart, each `C2 00 00`.
//
//     This is not a stub this lane is declining to port: it is the whole shipped
//     body. IO::Visitor is the resource-reference discovery pass (Serializer.h,
//     and WAVE3.md finding 5: ExternalFactory::Add re-invokes the encoding's
//     factory slot against an IO::VisitorStream, and 5,687 referenced externals
//     were preloaded on the run that surfaced it). It walks the object graph to
//     find REFERENCES and is deliberately blind to scalar data, so every scalar
//     slot returning immediately is the design, not an omission.
//
// ----------------------------------------------------------------------------
// ⚠ THE ONE TRAP IN THE FAMILY: Visitor's version field is at +0x20, NOT +0x28
// ----------------------------------------------------------------------------
//     ?GetVersion@BinaryReader@IO@@UEBAHXZ  0x180023930   MOV EAX,[RCX+0x28]
//     ?GetVersion@BinaryWriter@IO@@UEBAHXZ  0x180023ab0   MOV EAX,[RCX+0x28]
//     ?GetVersion@Visitor@IO@@UEBAHXZ       0x18002b670   MOV EAX,[RCX+0x20]
//
// IO::Visitor is NOT a BinaryCodec. It shares the IO::ReaderWriter base (+0x00
// vtable, +0x08 allowCustomSerialize, +0x10 serializer, +0x18 stream) and then
// diverges: it has no `recordLength`/`recordStart` record framing to carry, so its version
// lands eight bytes earlier. Three identically-named GetVersion functions where
// one reads a different offset is exactly the shape that gets "fixed" into
// uniformity by the next person to tidy this file, so Visitor gets its own
// struct below rather than borrowing BinaryCodec — the type system is made to
// carry the fact instead of a comment.
//
// ----------------------------------------------------------------------------
// BinaryReader::Skip vs BinaryWriter::Skip — asymmetric, and the reader's Skip
// is what finally explains the reader's recordLength/recordStart
// ----------------------------------------------------------------------------
//     0x1800a83f0  ?Skip@BinaryReader@IO@@UEAAXXZ
//         MOV RAX,[RCX+0x18]      ; stream
//         MOV EDX,[RCX+0x24]      ; recordStart
//         ADD EDX,[RCX+0x20]      ; + recordLength
//         MOV R8,[RAX] ; MOV RCX,RAX
//         JMP [R8+0x20]           ; TAIL JUMP to Stream::SetCurrentPos(pos)
//
//     0x1800a8b00  ?Skip@BinaryWriter@IO@@UEAAXXZ      RET
//
// So on the reading side `recordLength + recordStart` is the ONE-PAST-THE-END position of
// the current record, and Skip is "abandon the rest of this record and jump to
// the next one" — the reader's half of the length-prefixed, magic-bracketed
// frame whose writing half lane ENC-2 documented in binary_writer.cpp. Serializer.h
// describes the reader's +0x20 as "a dword read from the stream" and +0x24 as
// "stream->vtable[+0x28]()", i.e. the back-patched LENGTH and the position just
// after it; their sum being the record end is consistent with that and is the
// first thing in the tree to state what the pair is FOR on the read path.
//
// BinaryWriter::Skip is a bare RET because a writer cannot skip: there is
// nothing ahead of the cursor to jump over. Reproduced as an empty body, which
// is byte-faithful, not a stub.
//
// ----------------------------------------------------------------------------
// DOOR ARITHMETIC — this lane opens NOTHING, and that is checkable
// ----------------------------------------------------------------------------
// Every body here is either a field read or a dispatch through a vtable pointer
// the caller already handed us (the stream's, for shapes 1-2; our own, for shape
// 3). **Not one of the 73 calls ResolveExport, the raw export door, ResolveAddr, or
// RT_EXPORT_ONCE.** There is no MemFree, no vtable name, no helper. So the
// predicted door set-diff for this file is the empty set in BOTH directions:
// zero addresses or names OPENED, zero CLOSED. That prediction is recorded in
// WAVE9_LANE_SERIAL.md before it was measured, and it is falsifiable by grepping
// this file for `Resolve`.
//
// ----------------------------------------------------------------------------
// WHY MACROS, IN A TREE THAT PREFERS SPELLING THINGS OUT
// ----------------------------------------------------------------------------
// Forty-four of these bodies differ from one another only in a type name and a
// size immediate. Hand-typing forty-four near-identical functions is precisely
// the situation in which one wrong one hides inside a uniform-looking pass — the
// failure recorded in reference_uniform_population_is_not_a_verification.md,
// where a ported inline strcmp returned 0 for every pair sharing a first byte and
// read as fully verified. A macro cannot get the eleventh one wrong while the
// other ten are right: there is one body, instantiated eleven times, and the
// size immediate is `sizeof(T)` rather than a transcribed number.
//
// The registration table below is spelled out in full, because THAT is where a
// transcription error is possible (a mangled name or an address can be wrong
// without the compiler noticing) and where the probe cannot help — a
// mis-registered body would be verified perfectly and then wired to the wrong
// slot. serialize_family_probe.cpp checks the manglings against the shipped
// export table for exactly this reason.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>   // memcpy (BinaryReader::SerializeBool)

#include "io/Serializer.h"
#include "io/Stream.h"   // B3X W0: the stream is called by name
#include "io/FileStream.h"   // IO::CompressedFileStream (the array slots' exact-type test)

#include <typeinfo>

// ---- stream dispatch ------------------------------------------------------
// Shapes (1) and (2) tail-jump through the STREAM's vtable: Write (+0x10,
// BinaryWriter), Read (+0x18, BinaryReader), SetCurrentPos (+0x20, Skip).
// B3X W0: called by name on `IO::Stream` -- the same slots. `stream` is the
// ReaderWriter's `void*` field (+0x18), hence the one cast per body.
//
// Shape (3), the array loop, is spelled inline in the two macros below:
// faithful to 0x1800a8410's shape in the three ways that are observable --
// the `<= 0` signed guard returns before any dispatch; each element is a
// separate virtual call on `this` (the scalar slot for T); and `name` is
// dropped in favour of nullptr. Retail walks a pointer and counts down a
// register -- index arithmetic on T* is the same sequence of addresses.

// ===========================================================================
// ENDGAME B.3.4 block 2 — the 73 bodies, as IO::BinaryReader's and
// IO::BinaryWriter's methods.
// ===========================================================================
// Byte-for-byte what the doors above held before block 2; only the enclosing
// declaration changed. Three things here are load-bearing and are reproduced,
// not tidied:
//
//   * THE SCALAR BODIES IGNORE `name`. Retail CLOBBERS R8 (which held it) with
//     the value pointer before the tail jump. The binary format carries no
//     field names; a reader depends entirely on ORDER.
//   * THE ARRAY BODIES DROP `name` AND PASS nullptr to the elements. They do
//     not forward the array's own name.
//   * THE ARRAY BODY CALLS THE SCALAR MEMBER ONCE PER ELEMENT. Retail
//     re-dispatches through the vtable each iteration; B3X W0 spells that as
//     `this->Serialize<T>(arr[i], nullptr)`. The minted tables that once made
//     a raw slot read necessary were retired in B.3.9, and both classes are
//     `final`, so the member the call reaches is the one the slot held.
//     (Issue #13: on a compressed stream the READER's arrays take an exact
//     batched body instead; ReadArrayFromWindows below says why it is exact.)
//
// `Visitor`'s 24 bodies are `= {}` inline in the header: the shipped bodies are
// three bytes of `RET` each, and giving each its own out-of-line function would
// invent distinctions the binary does not make.
namespace IO {

// ---------------------------------------------------------------------------
// NOT RETAIL: the reader's array slots, an exact perf body (issue #13)
// ---------------------------------------------------------------------------
// Retail's array body (shape 3 above) is one virtual SerializeX per element,
// each a tail jump to Stream::Read(sizeof(T), &arr[i]) with the result ignored.
// On a compressed stream -- every External::Load and every package read -- that
// is one SetWindow and one memcpy per byte of every byte array (strings,
// ImageBuffer and mesh blobs): a third of the loader thread on a large mod set.
//
// When the stream is EXACTLY a CompressedFileStream or a CompressedMemoryStream,
// the elements go through CompressedStream::ReadArray instead, which makes the
// same Read calls retail makes except the ones it proves are a plain copy
// inside the active window, and batches those into one memcpy (its banner, in
// compressed_stream_read.cpp, has the proof). Why that is not observable here:
//   * The scalar slots are a raw Stream::Read of sizeof(T) bytes: no byte
//     swap, no per-element transform, nothing but the stream sees an element,
//     so the batch is exact for all eleven element types, not only the
//     byte-sized ones.
//   * BinaryReader is `final` and nothing re-stamps its vptr (the minted tables
//     were retired in B.3.9), so the per-element call retail re-dispatches
//     always reaches the scalar body above; there is no override to skip.
//   * The exact-type test keeps the proof honest: neither class overrides Read
//     (Stream.h, FileStream.h: slots 7 and 8 only), and a future subclass that
//     did would not match and would take retail's loop. Every other stream
//     (MemoryStream, FileStream, TextFileStream, the ext text reader) takes
//     retail's loop unchanged.
// The writer's array slots stay per element: a save is small, and the write
// path's SetWindow grows the stream, which this proof does not cover.
static bool ReadArrayFromWindows(Stream* s, int count, int32_t elemSize, void* arr) {
    if (s == nullptr)
        return false;   // retail's loop faults on the first element, as before
    const std::type_info& t = typeid(*s);
    if (t != typeid(CompressedFileStream) && t != typeid(CompressedMemoryStream))
        return false;
    static_cast<CompressedStream*>(s)->ReadArray(count, elemSize, arr);
    return true;
}

#define RT_RW_READER_BODY(METHOD, T)                                           \
    void BinaryReader::Serialize##METHOD(T& v, const char* name) {             \
        (void)name;                 /* CLOBBERED by retail before use */       \
        stream->Read((int32_t)sizeof(T), &v);   /* +0x18 */ \
    }                                                                          \
    void BinaryReader::Serialize##METHOD##Array(int count, T* arr,             \
                                                const char* name) {            \
        (void)name;                 /* dropped, not forwarded */               \
        if (count <= 0) return;                                                \
        if (ReadArrayFromWindows(stream, count, (int32_t)sizeof(T), arr))      \
            return;                 /* exact perf body, see above */           \
        for (int32_t i = 0; i < count; ++i)                                    \
            this->Serialize##METHOD(arr[i], nullptr);                          \
    }

#define RT_RW_WRITER_BODY(METHOD, T)                                           \
    void BinaryWriter::Serialize##METHOD(T& v, const char* name) {             \
        (void)name;                                                            \
        stream->Write((int32_t)sizeof(T), &v);  /* +0x10 */ \
    }                                                                          \
    void BinaryWriter::Serialize##METHOD##Array(int count, T* arr,             \
                                                const char* name) {            \
        (void)name;                                                            \
        if (count <= 0) return;                                                \
        for (int32_t i = 0; i < count; ++i)                                    \
            this->Serialize##METHOD(arr[i], nullptr);                          \
    }

RT_RW_READER_BODY(Char,   char)
RT_RW_READER_BODY(Float,  float)
RT_RW_READER_BODY(UInt64, uint64_t)
RT_RW_READER_BODY(Int64,  int64_t)
RT_RW_READER_BODY(UInt,   uint32_t)
RT_RW_READER_BODY(Int,    int32_t)
RT_RW_READER_BODY(UShort, uint16_t)
RT_RW_READER_BODY(Short,  int16_t)
RT_RW_READER_BODY(UChar,  unsigned char)
RT_RW_READER_BODY(SChar,  signed char)

RT_RW_WRITER_BODY(Char,   char)
RT_RW_WRITER_BODY(Bool,   bool)
RT_RW_WRITER_BODY(Float,  float)
RT_RW_WRITER_BODY(UInt64, uint64_t)
RT_RW_WRITER_BODY(Int64,  int64_t)
RT_RW_WRITER_BODY(UInt,   uint32_t)
RT_RW_WRITER_BODY(Int,    int32_t)
RT_RW_WRITER_BODY(UShort, uint16_t)
RT_RW_WRITER_BODY(Short,  int16_t)
RT_RW_WRITER_BODY(UChar,  unsigned char)
RT_RW_WRITER_BODY(SChar,  signed char)

#undef RT_RW_READER_BODY
#undef RT_RW_WRITER_BODY

// The reader's bool pair: shape 1 / shape 3 like the others, except for one store.
// NOT RETAIL: retail reads the stream byte straight into the field, so any value
// 0..255 lands there, and every retail consumer tests it as zero / non-zero. In
// C++ a bool object holding a byte other than 0 or 1 is an invalid value and
// loading it is undefined behaviour (review F-048: a mod .rsc or a shared .sav
// with a flag byte of 2 reached it). The byte is read as retail reads it and
// stored as `byte != 0`: the same truth value every consumer sees, and the same
// stream position. A failed read leaves the field as it was, as retail's does.
void BinaryReader::SerializeBool(bool& v, const char* name) {
    (void)name;                     // CLOBBERED by retail before use
    uint8_t byte;
    std::memcpy(&byte, &v, 1);
    stream->Read(1, &byte);         // +0x18
    v = (byte != 0);
}
void BinaryReader::SerializeBoolArray(int count, bool* arr, const char* name) {
    (void)name;                     // dropped, not forwarded
    if (count <= 0) return;
    if (ReadArrayFromWindows(stream, count, 1, arr)) {
        // NOT RETAIL: the batched copy (exact perf body, above) writes raw bytes
        // into the bools; normalise them as the scalar slot does (F-048).
        uint8_t* const bytes = reinterpret_cast<uint8_t*>(arr);   // CAST-OK: byte_io
        for (int32_t i = 0; i < count; ++i)
            bytes[i] = (bytes[i] != 0) ? 1u : 0u;
        return;
    }
    for (int32_t i = 0; i < count; ++i)
        this->SerializeBool(arr[i], nullptr);
}

// ?Skip@BinaryReader@IO@@UEAAXXZ   0x1800a83f0
// recordLength + recordStart is the ONE-PAST-THE-END position of the current record, so
// Skip is "abandon the rest of this record and jump to the next".
void BinaryReader::Skip() {
    // NOT RETAIL in spelling only (review F-142, signed overflow): recordLength is the record
    // length read from the stream, and retail's 32-bit ADD wraps; in int the same add is
    // undefined, so it runs in uint32_t. Same position.
    const int32_t end = static_cast<int32_t>(static_cast<uint32_t>(recordStart) +
                                             static_cast<uint32_t>(recordLength));
    stream->SetCurrentPos(end);   // +0x20
}

// The count comes straight through: (int, void*) already sit where
// Stream::Read/Write(size, buf) want them, which is why retail emits no
// marshalling for these two.
void BinaryReader::SerializeBytes(int count, void* buf, const char* name) {
    (void)name;
    stream->Read(count, buf);    // +0x18
}
void BinaryWriter::SerializeBytes(int count, void* buf, const char* name) {
    (void)name;
    stream->Write(count, buf);   // +0x10
}

// ENDGAME B.3.9 IO: slot 0 and the constructors/destructors of all three
// classes are in src/engine/io/Serializer.h. The objects are built by the
// real constructors and wear the compiler's tables, so the 73 by-name and
// by-address slot doors that used to delegate here (their only caller was the
// minted tables) were deleted with those rows. This file registers nothing.

}  // namespace IO
