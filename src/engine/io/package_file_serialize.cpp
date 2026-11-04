// ============================================================================
// src/engine/io/package_file_serialize.cpp — Wave 27 lane IOTEXT
//   ?Serialize@PackageFile@IO@@UEAA_NAEAVStream@2@@Z   retail 0x1800b60a0
//   `??_7PackageFile@IO@@6B@` slot 5 — THE LAST UNPORTED SLOT OF THAT TABLE.
// ============================================================================
//
// SOURCE OF EVERY FACT BELOW: the disassembly of 0x1800b60a0 in
// **Runtime-x64.dll** (retail), read instruction by instruction. Not the
// profile build, and not Ghidra's decompiled C — this body is nothing but
// indirect vtable calls through byte offsets, which is exactly the shape
// Ghidra's C output drops arguments at
// (reference_ghidra_c_output_vs_disassembly.md), and IO vtable offsets are the
// one family measured to DIFFER between the two builds
// (reference_encoding_vtable_offsets_differ_between_builds.md). Every slot
// number here comes from `rt::io::rw_vtbl` / `rt::io::encoding_vtbl`, which
// Wave 4 pinned against the shipped retail bytes.
//
// ---------------------------------------------------------------------------
// THE RECORD SEQUENCE, AND IT IS THE WHOLE OF THE FILE FORMAT
// ---------------------------------------------------------------------------
// A desync is "a different sequence of typed writes", so the order below is the
// specification and reordering any line to ascending-offset tidiness would
// break every .pkg this build has ever written.
//
//   1800b60ea  String::Serialize  +0x40  name
//   1800b60ff  String::Serialize  +0x48  author
//   1800b610c  String::Serialize  +0x50  description
//   1800b6119  String::Serialize  +0x58  _string58
//   1800b6126  String::Serialize  +0x60  _string60
//   1800b6139  rw kScalarInt      +0x6c  userVersion
//   1800b614d  rw kScalarInt      +0x70  priority
//   1800b6167  rw kScalarInt      (a LOCAL copy of +0x28 — see below)
//   1800b617e  [reading only] Array::SetSize(+0x20, n)
//   1800b618e  StringArray::Serialize(&ser, n, *(char**)(this+0x20))
//   1800b61a7  rw kScalarInt      (a LOCAL copy of +0x38)
//   1800b61be  [reading only] Array::SetSize(+0x30, m)
//   1800b61ce  StringArray::Serialize(&ser, m, *(char**)(this+0x30))
//   1800b61e4  rw kScalarBool     +0xac  overridable
//   1800b61f5  rw kScalarInt      +0x78  iconWidth
//   1800b6209  rw kScalarInt      +0x7c  iconHeight
//   1800b6226  rw kScalarInt      (a LOCAL copy of +0x88)
//   1800b6240  [reading only] Array<uint>::SetSize(+0x80, k)
//   1800b6259  rw kArrayUInt      (k, *(uint**)(this+0x80))
//   1800b626e  rw slot 0 (flags 1) — destroy the codec
//   1800b6270  MOV AL,1 — ALWAYS TRUE. There is no failure path.
//
// `packfileId` (+0xA8) IS NOT SERIALIZED. Neither is `version` (+0x68), nor
// `platform` (+0x74), nor the `FileInfo` the destructor tears down. Stated
// because their ABSENCE is as load-bearing as the presences: a reader that
// expects them desyncs.
//
// ---------------------------------------------------------------------------
// THE THREE COUNT FIELDS GO THROUGH A LOCAL, AND THAT IS NOT A DETAIL
// ---------------------------------------------------------------------------
// Retail copies `[this+0x28]` into the stack slot at `[rbp+0x10]`, serializes
// THE LOCAL, and then uses the local to size the array:
//
//     1800b6153  MOV EAX,dword ptr [RBX + 0x28]
//     1800b6156  MOV dword ptr [RBP + 0x10],EAX
//     1800b6167  CALL qword ptr [RAX + 0xa0]      ; kScalarInt(&local)
//     1800b6171  CMP dword ptr [RAX + 0x8],0x0    ; ser.stream->direction
//     1800b617e  CALL 0x180029a80                 ; SetSize(&this[0x20], local)
//
// On the WRITE path the local carries the field's value out. On the READ path
// the codec writes the incoming count INTO the local, and `SetSize` is what
// puts it back into the Array's own count word — the field is never assigned
// directly. Serializing `&this->count` in place would work on write and, on
// read, would leave a count that disagreed with the allocation for the window
// between the two calls. Transcribed as retail has it.
//
// The `[reading only]` guard is retail's, at 0x1800b6175 / 0x1800b61b5 /
// 0x1800b6234: `CMP dword ptr [ser.stream + 0x8],0x0 ; JNZ past`. `direction ==
// 0` is reading (src/engine/io/Serializer.h:230).
//
// ---------------------------------------------------------------------------
// CALLEES — what is ours, what is a DOOR, and what this port COSTS
// ---------------------------------------------------------------------------
//   0x1800c9930  ?Serialize@String@System@@QEAAXAEAVSerializer@IO@@@Z
//                DOOR, by address. The same door goal_serialize.cpp:147 and
//                elementdescription_serialize.cpp:155 already open — not a new
//                edge on the cutover surface, an existing one reused.
//   0x180029a80  Array<String>::SetSize (unexported, two-argument form)
//                PORTED — src/engine/collection/array.cpp:2055 registers it,
//                so ResolveAddr (registry-first) hands back OUR body.
//   0x180029510  Array<uint>::SetSize (unexported)
//                PORTED — array.cpp:2002.
//   0x180029bb0  Array-of-String serialize (unexported, ~680 bytes)
//                PORTED — BATCH 8b, src/engine/io/text_codec.cpp registers it,
//                so ResolveAddr (registry-first) hands back OUR body. It WAS a
//                new address door this file opened, and the door was honest;
//                it is now closed rather than merely declared.
//
// SO THIS PORT GREW THE CUTOVER SURFACE BY ONE FUNCTION for a day, and that was
// stated here rather than discovered later: the pattern is
// reference_porting_can_grow_the_cutover_surface.md. The trade it weighed —
// "reproducing a 680-byte UTF-8/UTF-16 transcoder to avoid one door is not a
// better trade" — was the right call at the time and is now moot: the
// transcoder came with the four text-codec internals, which had to be ported
// anyway for TextFileStream.
//
// ---------------------------------------------------------------------------
// ORACLE — the emission oracle, WRITING direction, FULL replay
// ---------------------------------------------------------------------------
// "No oracle possible" has been wrong four times across three waves for exactly
// this shape (reference_write_path_serialize_is_replayable.md), and it is wrong
// here too. On the write path this body mutates NOTHING:
//   * the three `SetSize` calls are behind retail's own reading-only guard;
//   * `String::Serialize` on the write path emits and does not touch the field;
//   * 0x180029bb0 on the write path takes its `direction != 0` arm at
//     0x180029c44 and 0x180029c66 and reduces to `kScalarUInt(len|0x80000000)`
//     then `kArrayChar(len, data)` per element — emission only.
// So NO PREFIX STOP is needed; the whole body replays.
//
// **NO GAMEPLAY ROUTE, AND THAT IS NOT THE SAME AS NO ORACLE**
// (reference_no_route_is_not_no_oracle.md). Nothing in the game writes a .pkg —
// that is the ToolKit's job — so this body has no natural caller and would
// report `calls=0` forever. It is therefore driven from
// `RtIo_PackageFile_SweepProbe` (package_file.cpp), which runs at the tail of
// LoadPackages with every mounted package fully populated, i.e. against REAL
// subjects rather than a fabricated one. The probe is gated on
// the old DLL-era switch and does nothing otherwise.
// ============================================================================

#include <cstdint>
#include <cstdio>

#include "io/Serializer.h"
#include "io/Stream.h"        // IO::Stream: the Serializer's stream
#include "io/PackageFile.h"   // B.3.4 PACKAGE+FS: the real class
#include "system/String.h"
#include "collection/Collection.h"

namespace {

// B3R RT-IO: the three by-address doors that stood here are direct calls now:
//   ?Serialize@String@System@@ 0x1800c9930   -> System::String::Serialize
//   the array-of-String walker  0x180029bb0  -> RtIo_SerializeStringArray
//     (text_codec.cpp). Its 4th parameter is not an argument: retail spills
//     R9 to its home slot at 0x180029bbb and only ever WRITES that slot again
//     (0x180029d00) before reading it, so the value passed is never read.
//   Array<T>::SetSize 0x180029a80 / 0x180029510 -> rtcoll::RtColl_ArraySetSize_*

// ---- prologue / epilogue, from 0x1800b60ca and 0x1800b626e ---------------
// B.4 R-IO: the prologue (0x1800b60ca..e6) is `IO::Serializer`'s constructor
// inlined -- the two stores, then the Encoding factory by direction -- with
// the THIRD argument 0: retail zeroes R8D at 0x1800b60ce for the
// `CMP [RDX+0x8],R8D` and never reloads it before the factory call, so the
// save-format version reaching BinaryReader/BinaryWriter's ctor is 0. The
// epilogue (0x1800b6261..6e, `if (rw) delete rw`) is its destructor. The body
// below builds the real class; `MakeCodec` / `DestroyCodec` / `Rw` are gone.

// B3X W0: the ReaderWriter calls by name.
void RecInt(::IO::ReaderWriter* rw, int32_t* v) {
    rw->SerializeInt(*v, nullptr);                               // +0xa0
}

void RecBool(::IO::ReaderWriter* rw, bool* v) {
    rw->SerializeBool(*v, nullptr);                              // +0x78
}

// `SerializeUIntArray` is (count, uint*, name) — EDX, R8, R9 at 0x1800b624c-0x1800b6259.
void RecArrayUInt(::IO::ReaderWriter* rw, int32_t n, uint32_t* data) {
    rw->SerializeUIntArray(n, data, nullptr);                    // +0x40
}

// NOT RETAIL (hardening, review F-072): retail sizes the three arrays below from the counts
// as read, so a .pkm asking for 0x7fffffff strings or icon words ends the game in
// FatalOutOfMemory (and a negative one destroys below the buffer). Every element takes at
// least four bytes of the stream (a string's length word, an icon uint), so a count the
// bytes left cannot hold is a corrupt record: it reads as 0. Valid data always fits. A
// stream that reports no size (GetSize 0) is not capped.
int32_t CountTheStreamCanHold(const ::IO::Stream* s, int32_t n) {
    const int64_t size = static_cast<int64_t>(s->GetSize());
    if (size == 0)
        return n;
    const int64_t left = size - static_cast<int64_t>(s->GetCurrentPos());
    if (n < 0 || static_cast<int64_t>(n) * 4 > left)
        return 0;
    return n;
}

}  // namespace

// ENDGAME B.3.9 IO: the ?Serialize@PackageFile slot door and its registration
// are gone with the minted ??_7PackageFile row, their only reader; the body is
// the member below, reached through the class's own table.

// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS — `IO::PackageFile::Serialize`, vtable slot 5
// ============================================================================
//
// A member of `IO::X` must be DEFINED in a namespace ENCLOSING the class
// (B34_BRIEFS section A.2), so it lives here rather than in the anonymous
// namespace with its helpers.
//
// B.4 R-IO: the file's global `using rt::io::…` declarations are gone with
// the POD Serializer; inside `namespace IO`, `Serializer` and `Stream` are the
// real classes.
//
// THE FIELD OFFSETS ARE GONE, AND THAT IS THE WIN. The body used to reach
// eleven fields through `FieldAt(self, 0xNN)` / `CountAt(self, 0xNN)` /
// `ArrayAt(self, 0xNN)` because no header named them. src/engine/io/
// PackageFile.h names them now, each with a static_assert on its offset, so the
// record's SHAPE is checked by the compiler instead of by four spellings of a
// hex constant. The record ORDER is retail's and is untouched: a Serialize body
// that emits fields in a different order writes a different file.
namespace IO {

bool PackageFile::Serialize(IO::Stream& streamv) {
    // LEFT: the slot's parameter is `void*` (Object::External's slot 5 in
    // src/engine/object/External.h, read-only to this block).
    Serializer ser(streamv, 0);          // 1800b60ca..e6
    ReaderWriter* const rw = ser._readerWriter;

    _name.Serialize(ser);                                      // 1800b60ea
    _author.Serialize(ser);                                    // 1800b60ff
    _description.Serialize(ser);                               // 1800b610c
    _string58.Serialize(ser);                                  // 1800b6119
    _string60.Serialize(ser);                                  // 1800b6126

    RecInt(rw, &_userVersion);                // 1800b6139
    RecInt(rw, &_priority);                   // 1800b614d

    // The three counted arrays. Each: copy the count to a local, serialize the
    // LOCAL, size the array from it on the reading side only, then hand the
    // element walker the count and the data pointer. See the banner.
    {
        int32_t n = _stringArray20._size;                         // 1800b6153
        RecInt(rw, &n);                                           // 1800b6167
        if (ser._stream->_direction == rt::io::kDirectionReading) {  // 1800b6171
            n = CountTheStreamCanHold(ser._stream, n);              // F-072, not retail
            rtcoll::RtColl_ArraySetSize_180029a80(&_stringArray20, n);  // 1800b617e
        }
        RtIo_SerializeStringArray(&ser, n, _stringArray20._data, nullptr);  // 1800b618e
    }
    {
        int32_t m = _stringArray30._size;                         // 1800b6193
        RecInt(rw, &m);                                           // 1800b61a7
        if (ser._stream->_direction == rt::io::kDirectionReading) {  // 1800b61b1
            m = CountTheStreamCanHold(ser._stream, m);              // F-072, not retail
            rtcoll::RtColl_ArraySetSize_180029a80(&_stringArray30, m);  // 1800b61be
        }
        RtIo_SerializeStringArray(&ser, m, _stringArray30._data, nullptr);  // 1800b61ce
    }

    RecBool(rw, &_overridable);               // 1800b61e4
    RecInt(rw, &_iconWidth);                  // 1800b61f5
    RecInt(rw, &_iconHeight);                 // 1800b6209

    {
        int32_t k = _iconData._size;                                 // 1800b620f
        RecInt(rw, &k);                                           // 1800b6226
        if (ser._stream->_direction == rt::io::kDirectionReading) {  // 1800b6230
            k = CountTheStreamCanHold(ser._stream, k);              // F-072, not retail
            rtcoll::RtColl_ArraySetSize_180029510(&_iconData, k);       // 1800b6240
        }
        RecArrayUInt(rw, k, _iconData._data);                          // 1800b6259
    }

    return true;                              // 1800b6270  MOV AL,1
                                              // ~ser: 1800b626e, before the return
}

}  // namespace IO
