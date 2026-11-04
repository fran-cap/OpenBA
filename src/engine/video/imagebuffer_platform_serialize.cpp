// ============================================================================
// src/engine/video/imagebuffer_platform_serialize.cpp — Wave 18 lane VRES,
//   post-report tranche 2.
//
//   ?PlatformSerialize@ImageBuffer@Video@@AEAAXAEAVSerializer@IO@@@Z
//   retail 0x1800f3130, ordinal 3387.   void(IO::Serializer&)
// ============================================================================
//
// THE ROW THIS WAVE CREATED FOR ITSELF. It entered the census's `called` column
// only because lane VDATA's ImageBuffer work reaches it, and nothing ported it.
// It is the last row between Wave 18 and Video's ceiling identity.
//
// `AEAA` MEANS PRIVATE, NOT UNEXPORTED. Checked against the shipped export
// directory before writing anything: ordinal 3387, address 0x1800f3130, sitting
// in a run with its four siblings (GraphicsMesh, ShaderProgram, SoundBuffer,
// VertexLayout). So this is a plain NAME door and there is no address-door
// justification to make (`reference_aeaa_means_private_not_unexported.md`).
//
// Confirmed absent before writing: `PlatformSerialize` appears NOWHERE in
// `code/ engine/ runtime/ standalone/ src/render/dx11/`. Not ported, not gated, not
// half-registered.
//
// ---------------------------------------------------------------------------
// WHAT IT DOES
// ---------------------------------------------------------------------------
// Its only caller is `?Serialize@ImageBuffer@Video@@` (0x1800f26f0), which has
// already serialized width, height, dimension, kind, format, flags, the three
// address modes, the filter and the refcounted `pixelData` at +0x48 by the time
// it calls this at 0x1800f281a. So this function does exactly two things: it
// carries the PER-MIP-LEVEL pixel blobs, and it re-Creates the platform
// resource from the fields that were just read.
//
//   kind = this->kind                        (+0x2c, read ONCE at entry)
//
//   if (kind == 4 || kind == 5)              // IsGenerated() / IsRendered()
//       // no serialized pixels exist for these
//       if (stream->direction != 0) return;  // SAVING: nothing to do at all
//       Create(..., initialData = nullptr);
//       return;
//
//   if (!encoding->vtable[0]()) return;      // the gate; see below
//   Array<Array<uint8>> levels = {};
//   int count = levels._size;                // == 0
//   rw->Serialize(count)                     // slot 0xA0, kScalarInt
//   if (stream->direction == 0)              // READING
//       Array<Array<uint8>>::SetSize(&levels, count);
//   SerializeLevelBlobs(ser, count, levels._data);
//   Create(..., initialData = &levels);
//   destroy every element's buffer, then free the outer buffer
//
// `Stream::direction` is 0 = READING, 1 = WRITING — already pinned in
// `src/engine/io/Stream.h`, not re-derived here. That is what makes the two
// `direction` tests readable: the generated/rendered arm does nothing on save,
// and the outer array is sized only on load.
//
// THE ENCODING GATE IS `IO::Encoding::IsBinary()`, SLOT 0.
//
// This banner used to carry a caution here — "the gate is not named, and this
// file does not pretend otherwise" — on the grounds that no
// `??_7…Encoding@IO@@6B@` is exported under any name, so slot 0 could not be
// named the way `Serializer.h` names slots 3 and 4. THAT CAUTION IS RETIRED
// (ENDGAME B.3.5 block C, confirmed by block VC): B.3.4 block 1 recovered all
// five `Encoding` slots from the three CONCRETE tables instead of from an export,
// and slot 0 is `?IsBinary@Encoding@IO@@UEBA_NXZ` — `BinaryEncoding` 0x1800a81e0
// returns true, `TextEncoding` 0x1800b9ab0 and `VisitorEncoding` 0x1800bafd0
// return false (`src/engine/io/Serializer.h`). So the gate reads "carry the
// per-mip-level pixel blobs only for a BINARY encoding", which is what the
// behaviour always was: text serialization is stripped from the shipped DLL and
// the Visitor pass is resource-reference discovery, and neither has any business
// moving pixels.
//
// The caution was right for its instrument and is superseded by a READ, not by a
// guess — which is the distinction it was written to protect. The call site below
// says the same thing next to the code.
//
// ---------------------------------------------------------------------------
// A FAITHFULNESS DETAIL THAT LOOKS LIKE A TIDY-UP AND IS NOT
// ---------------------------------------------------------------------------
// `kind` is read ONCE at entry into R8D (0x1800f3154) and the branch is taken on
// that value. The generated/rendered arm then passes THAT SAME REGISTER to
// Create (0x1800f331d never reloads it), while the pixel arm RELOADS
// `[this+0x2c]` at 0x1800f3217. Both are reproduced separately below. They can
// only differ if something between the two points writes `this->kind` — nothing
// in this body does — but collapsing them to one read would be a silent change
// to which value reaches Create, and this project has paid for exactly that
// class of "equivalent" simplification before.
//
// ---------------------------------------------------------------------------
// THE TWO Collection HELPERS, AND WHY ONE OF THEM COST NOTHING
// ---------------------------------------------------------------------------
// Retail reaches both by in-DLL `rel32`, which our registry cannot intercept,
// so this body has to call something. Both go through
// `ResolveAddr(Module::Runtime, …)`, which is REGISTRY-FIRST and is therefore
// not "the DLL" (`reference_resolveaddr_consults_port_registry_first.md`):
//
//   0x180029630  Array<uint8>::SetSize        **ALREADY PORTED** — lane Q
//                registers it as `J3_ENTRY2(180029630, kSpec_Bool)`
//                (array.cpp:1469, RegisterRuntimeAddr at :1686), so this call
//                reaches OUR body today. Found by grepping the address before
//                writing a line, which is the whole of
//                `reference_grep_for_the_port_before_writing_it.md`.
//
//   0x180053f00  Array<Array<uint8>>::SetSize  UNPORTED. 107 instructions, the
//                non-trivial J3 shape with inner-array element operations.
//                It is COLLECTION lane work, not Video: porting it here would
//                mean adding an `ArraySpec` row to lane Q's `array.cpp`, which
//                another lane owns. It is named here so the Collection wave
//                inherits it as ONE address rather than rediscovering it.
//
// So this row grows the cutover surface by exactly ONE address, and that
// address has an owner (`reference_porting_can_grow_the_cutover_surface.md`).
//
// THE CLAIM THAT USED TO STAND HERE WAS WRONG, TWICE OVER, AND IS WITHDRAWN
// (ENDGAME B.3.5 block VC). It read: "The per-element serialize loop at
// 0x180054080 IS written out below rather than called by address: it is 20
// instructions, it is this function's own helper, and nothing else in the tree
// resolves it. It is deliberately NOT registered." In fact
// `src/engine/video/closure_batch2b.cpp` registers it at that address, and it has
// THREE retail callers of which this function is only one. It is a SHARED helper
// and there is now exactly one body of it. See the note at `SerializeLevelBlobs`
// below for the full read.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "io/Serializer.h"
#include "io/Stream.h"
#include "video/ImageBuffer.h"
#include "../collection/array_internal.h"
#include "../collection/array_destroy_internal.h"

// `kSpec_InnerArrayU8` (0x180053f00) is defined in
// src/engine/collection/array.cpp but, like `kSpec_EmitterInstance`
// (src/engine/audio/audio_effectslot_serialize.cpp), was never added to
// array_internal.h's `extern` list — declared locally rather than editing
// that shared header, which this batch does not own.
namespace rtcoll { extern const ArraySpec kSpec_InnerArrayU8; }

// The door over `Video::ImageBuffer::PlatformSerialize`, which is PRIVATE in
// retail (`AEAA`) and `protected` on the class. See the banner on
// `struct ImageBufferDoorAccess;` in src/engine/video/ImageBuffer.h — the
// class friends it, and `imagebuffer_vtable.cpp` declares the same struct to
// call the same member from `Serialize`. Two translation units, one global type,
// one friend declaration.
struct ImageBufferDoorAccess {
    static void PlatformSerialize(Video::ImageBuffer* p, IO::Serializer& ser) {
        p->Video::ImageBuffer::PlatformSerialize(ser);
    }
};

namespace {

// ---------------------------------------------------------------------------
// ENDGAME B.3.5 block C — FOUR duplicate views are GONE from this file.
// ---------------------------------------------------------------------------
//   * `struct SerializerRaw { void* stream; void* readerWriter; }` and its size
//     assert: the member takes the REAL `IO::Serializer&` (B.3.4 block 3) and
//     reads `_stream` / `_readerWriter` by name.
//   * `kStreamDirection` / `kStreamEncoding`: `IO::Stream::GetDirection()` and
//     `IO::Stream::GetEncoding()` say the same thing from `src/engine/io/
//     Stream.h`, which owns the evidence.
//   * `kRwScalarInt` (+0xa0) / `kRwArrayUChar` (+0x60) and their two `using`
//     typedefs: `IO::Serializer::Serialize(int&, name)` and
//     `Serialize(int, unsigned char*, name)` are the non-virtual forwarders that
//     reach exactly those two slots. This file was the last caller spelling
//     either offset.
//   * `kImageBufferCreateSlot` (+0x48) and `CreateFn`: slot 9 is
//     `Video::ImageBuffer::Create`, so `self->Create(...)` IS retail's
//     `CALL [R10+0x48]` — a C++ virtual call reads the object's own vptr, which
//     on every live image is still the MINTED table, so the dispatch is
//     unchanged and the slot INDEX is now checked by the compiler instead of by
//     a comment.
// ---------------------------------------------------------------------------

using SetSizeFn = void (*)(rtcoll::ArrayHdr* self, int32_t newSize);

// ENDGAME B.3.2 step 2 — both forwarders above (`OuterSetSize`, `InnerSetSize`)
// are GONE. STALE COMMENT KEPT FOR THE RECORD: "0x180053f00 ... unported
// today" stopped being true once array.cpp's `kSpec_InnerArrayU8` row was
// registered (array.cpp:4028) — it is a registered port, not a DLL call.
//
//   0x180053f00  kSpec_InnerArrayU8 {0x10,8,AssignInnerArrayU8,
//                InitInnerArrayU8,DestroyInnerArrayU8Range,byteCopy=false}
//                NON-TRIVIAL (the assign zeroes-then-conditionally-calls a
//                real deep `Array<uint8>::operator=`), so `levels` (still a
//                plain `rtcoll::ArrayHdr`/`Array<uint8_t>` locally — its
//                nominal element type is a byte cursor, not the semantic
//                `Array<Array<uint8>>`) reaches it through a hand-derived
//                `rtcoll::AsArray<&kSpec_InnerArrayU8>` bridge, not `.SetSize`.
//   0x180029630  kSpec_Bool == `SpecFor<uint8_t>()` field for field (see
//                achievement_manager.cpp's `completed` for the same row),
//                trait-derived. Each per-level element `e` in
//                `SerializeLevelBlobs` IS ALREADY a real
//                `Collection::Array<uint8_t>*` (retail's Array<uint8>
//                instantiation), so `e->SetSize(size)` is that exact body —
//                no bridge needed.

// ---- 0x180054080 — serialize N per-level byte blobs ------------------------
//
// ENDGAME B.3.5 block VC: THE SECOND BODY OF THIS ADDRESS IS GONE. This file used
// to write the loop out, on the strength of a banner claim that 0x180054080 "is
// this function's own helper, nothing else in the tree resolves it" and "is
// deliberately NOT registered". BOTH HALVES WERE FALSE.
// `src/engine/video/closure_batch2b.cpp` registers it at that exact address, and
// Ghidra `getReferencesTo` on the retail DLL (15290 functions) gives it THREE
// callers -- 0x180053d90 in FUN_180053d40, 0x180053eeb in FUN_180053ea0, and
// 0x1800f31d0 in this very function -- plus a `.rdata` reference at 0x180207870.
// Two of the three are unexported entries of a three-qword dispatch table at
// 0x180207840/858/870, so it is the SHARED `Array<Array<uint8>>` blob serializer
// and belongs to no one class.
//
// The two bodies were behaviourally identical, which is the dangerous case: two
// spellings of one address that agree by luck are one edit from disagreeing, and
// this tree has paid for exactly that shape before (the shadowed
// `RenderBuffer::PlatformDestruct` copy, context_lifecycle.cpp's Wave 20 note).
// So there is ONE body now, `RtVideo_SerializeBlobArray`, and the call below IS
// retail's `CALL 0x180054080` at 0x1800f31d0.
//
// Retail passes a NULL `name` at every call site (`XOR R8D,R8D` / `XOR R9D,R9D`),
// so the blobs are unnamed in the stream; the one body reproduces that.
// (Declared in ImageBuffer.h.)
inline void SerializeLevelBlobs(IO::Serializer& ser, int32_t count,
                                rtcoll::ArrayHdr* elems) {
    RtVideo_SerializeBlobArray(ser, count, elems);
}

// The eleven-argument Create call, spelled once so the two arms cannot drift.
// `kind` is a PARAMETER rather than a field read, because the two arms source
// it differently — see the banner.
//
// ENDGAME B.3.5 block C: this is now slot 9 by NAME. `Create` is a virtual of
// the real `Video::ImageBuffer`, declared at index 9, so `self->Create(...)` is
// retail's `CALL [R10+0x48]` with the index checked by the compiler. The virtual
// call reads the object's own vptr, which on every live image is the MINTED
// table, so the dispatch is exactly what it was.
void RecreateFromFields(Video::ImageBuffer* self, int32_t kind,
                        Video::ImageBuffer::LevelArray* initialData) {
    self->Create(
        self->_dimension,  // EDX            +0x28  ImageType
        kind,              // R8D            +0x2c  Usage
        self->_format,     // R9D            +0x30  Format
        self->_flags,      // [RSP+0x20]     +0x34
        self->_width,      // [RSP+0x28]     +0x20
        self->_height,     // [RSP+0x30]     +0x24
        self->_filter,     // [RSP+0x38]     +0x44  FilterMode
        self->_addressU,   // [RSP+0x40]     +0x38
        self->_addressV,   // [RSP+0x48]     +0x3c
        self->_addressW,   // [RSP+0x50]     +0x40
        initialData);      // [RSP+0x58]
}

// Live coverage counter. `calls=` is the only thing that separates "verified
// clean" from "never ran", and lane VDATA's warning applies directly here: its
// save-trace oracle recorded NO ENTRY at all for the seven ImageBuffer vtable
// slots, which is the calls=0 column and not a pass. Static storage duration,
// per array.cpp's 144-dangling-registrations note.

}  // namespace

// ============================================================================
// The body — now a MEMBER of the real `Video::ImageBuffer` (B.3.5 block C).
// ============================================================================
namespace Video {

ImageBuffer::ExtLevelSourceFn ImageBuffer::_extLevelSource = nullptr;

void ImageBuffer::PlatformSerialize(IO::Serializer& ser) {
    // 0x1800f3154: read ONCE, at entry. The generated/rendered arm reuses this
    // value; the pixel arm reloads. See the banner.
    const int32_t kindAtEntry = _kind;
    IO::Stream* stream = ser._stream;

    // ---- the generated / rendered arm --------------------------------------
    // `LEA EAX,[R8-4] / CMP EAX,1 / JBE` — an UNSIGNED compare, so this is
    // exactly kind == 4 || kind == 5, i.e. IsGenerated() || IsRendered().
    // NOT RETAIL (UB, signed overflow): the subtraction is unsigned
    // (KindIsGeneratedOrRendered); `kindAtEntry - 4` in int overflowed for 0x80000000.
    if (KindIsGeneratedOrRendered(kindAtEntry)) {
        if (stream->GetDirection() != IO::kReading) {  // 0x1800f32d5: saving
            return;
        }
        RecreateFromFields(this, kindAtEntry, nullptr);
        return;
    }

    // ---- the gate ----------------------------------------------------------
    // ENDGAME B.3.5 block C FINDING — THE GATE HAS A NAME, and this file's
    // banner (written before `IO::Encoding` was a class) says it does not.
    // `stream->encoding->vtable[0]()` returning a bool is slot 0 of
    // `??_7…Encoding@IO@@6B@`, and B.3.4 block 1 named all five of those slots
    // from the three concrete tables: slot 0 is
    // `?IsBinary@Encoding@IO@@UEBA_NXZ` (`BinaryEncoding` 0x1800a81e0 returns
    // true, `TextEncoding` 0x1800b9ab0 and `VisitorEncoding` 0x1800bafd0 return
    // false — src/engine/io/Serializer.h). So the gate is "carry the
    // per-mip-level pixel blobs only for a BINARY encoding", which is what the
    // behaviour always was: text serialization is stripped from the shipped DLL
    // and the Visitor pass is the resource-reference discovery pass, and neither
    // has any business moving pixels. The banner's caution was right for its
    // instrument and is now superseded by a read, not by a guess.
    // `MOV RCX,[stream+0x10]` then `CALL [[RCX]]`: retail loads the encoding off
    // the stream itself rather than through `?GetEncoding@Serializer@IO@@`, so
    // this reads `_encoding` directly too.
    if (!stream->_encoding->IsBinary()) {
        return;
    }

    // ---- the per-level pixel blobs ----------------------------------------
    // ENDGAME B.3.2: Array is a real class; the default ctor is these same
    // three zero stores. B.4 S4 GFX2: the local is the `LevelArray` retail's
    // is, and its teardown is its destructor (see the end of the body).
    LevelArray levels;

    // 0x1800f3185 reads the count out of the array it has just zeroed, so on
    // the WRITE path the value serialised here is 0. Transcribed, not
    // "corrected" to the level count: this is what the shipped body emits.
    int32_t count = levels.GetSize();

    ser.Serialize(count, nullptr);                   // rw slot +0xa0, kScalarInt

    // 0x1800f31a8 RE-READS ser->stream rather than reusing the pointer it
    // loaded at entry. Reproduced — `ser._stream`, not the `stream` local above.
    // retail 0x180053f00 — kSpec_InnerArrayU8; see the banner.
    if (ser._stream->GetDirection() == IO::kReading) {
        // NOT RETAIL (hardening, review F-072): retail sizes the level array from the
        // count as read, so a malformed .rsc asking for 2^31 levels ends the game in
        // FatalOutOfMemory. Every level carries at least its four-byte length word, so a
        // count the bytes left cannot hold (or a negative one) reads as 0 levels. Valid
        // data always fits; a stream that reports no size (GetSize 0) is not capped.
        const int64_t size = static_cast<int64_t>(ser._stream->GetSize());
        const int64_t left = size - static_cast<int64_t>(ser._stream->GetCurrentPos());
        if (size != 0 && (count < 0 || static_cast<int64_t>(count) * 4 > left))
            count = 0;
        rtcoll::AsArray<&rtcoll::kSpec_InnerArrayU8>(&levels)->SetSize(count);
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): ext.create_on_demand -- on a
    // read, the extension's level reader may take the blobs (ImageBuffer.h). Null: retail's read.
    if (ser._stream->GetDirection() != IO::kReading || _extLevelSource == nullptr ||
        !_extLevelSource(ser, *this, count, levels))
        SerializeLevelBlobs(ser, count, levels.GetData());

    // 0x1800f31e2: the field reads for Create happen HERE, after the
    // serialization, and `kind` is RELOADED. Both are faithful.
    RecreateFromFields(this, _kind, &levels);

    // ---- destroy the temporary --------------------------------------------
    // B.4 S4 GFX2 (behaviour): `levels`'s own destructor at the closing brace.
    // Retail inlines ~Array<Array<uint8>> right after the Create call
    // (0x1800f3226..0x1800f32ce, inside the local's EH scope: the NOP at
    // 0x1800f3225): for each level, ASCENDING, `if (data) Free(data)` under the
    // allocator lock and the 16-byte header zeroed; then the outer buffer
    // freed the same way and its header zeroed. `LevelArray`'s trait-derived
    // spec destroys each `Array<uint8_t>` element in ascending order (its
    // ~Array: ArrayDtorImpl, MemFree with retail's null test inside, then the
    // three header fields, H7) and then frees the outer buffer the same way.
    // DESTROYS EVERY ELEMENT and then frees the block, in that order.
}

}  // namespace Video
