#pragma once
// ============================================================================
// src/engine/io/FileStream.h — IO::FileStream / BinaryFileStream /
//        CompressedFileStream / TextFileStream / VisitorStream
//        (Wave 11 follow-up 2, lane IOSTREAM)
// ============================================================================
//
// src/engine/io/Stream.h pins the four MEMORY-backed streams. This header
// pins the five that were missing, and they are the ones three Object rows
// stack-construct by value:
//
//     ?Load@External@Object@@         CompressedFileStream over BinaryFileStream
//     ?SaveText@External@Object@@     TextFileStream
//     ?Add@ExternalFactory@Object@@   VisitorStream
//
// ---------------------------------------------------------------------------
// WHY THIS HEADER IS THE WHOLE BLOCKER, AND WHY IT IS NOT "no constructor"
// ---------------------------------------------------------------------------
// Lane OBJEXT recorded these three rows as blocked because the classes have
// "no layout OR a constructor in the tree". Only the first half is true, and
// the distinction decides how big the work is.
//
// EVERY constructor and destructor in this family is EXPORTED:
//
//     ??0FileStream@IO@@QEAA@XZ                              ordinal  188
//     ??0BinaryFileStream@IO@@QEAA@XZ                         ordinal   55
//     ??0CompressedFileStream@IO@@QEAA@XZ                     ordinal  104
//     ??0TextFileStream@IO@@QEAA@AEBVString@System@@0H@Z      ordinal  441
//     ??0VisitorStream@IO@@QEAA@XZ                            ordinal  511
//     ??1FileStream / ??1BinaryFileStream / ??1CompressedFileStream /
//     ??1TextFileStream / ??1VisitorStream                    all UEAA, all exported
//
// (Run against the export table, not grepped. `AEAA`/`IEAA` mean PRIVATE and
// PROTECTED, not unexported — this DLL exports both, and
// reference_aeaa_is_private_not_unexported.md is the lane that learned it.)
//
// So a caller can construct any of these by calling retail's own ctor on a
// stack buffer. What it CANNOT do without this header is size that buffer or
// reach the fields the caller itself reads — and all three callers read fields
// directly, because MSVC inlined each destructor into the caller's epilogue.
//
// The technique is not new here either: src/engine/save/saveload_meta.cpp
// already stack-constructs an IO::CompressedMemoryStream this way, piecemeal
// through exported ctors with the vtable stamped by RT_VTABLE_ONCE.
//
// ---------------------------------------------------------------------------
// EVERY SIZE BELOW IS MEASURED, NOT INFERRED
// ---------------------------------------------------------------------------
// Each class's deleting-destructor thunk (vtable slot 0) calls
// `_eh_vector_destructor_iterator_(p, STRIDE, count, ~Class)`, and that stride
// is the element size an array-delete uses, i.e. sizeof. This is the same
// method Stream.h used to pin sizeof(CompressedMemoryStream) == 0x98.
//
//     FileStream / BinaryFileStream   0x180023d80   stride 0x50
//     CompressedFileStream            0x180024100   stride 0xb8
//     TextFileStream                  0x18002ba90   stride 0x80
//     VisitorStream                   0x18002bf80   stride 0x18
//
// PORTABILITY: no <windows.h> (PLAN.md §4.1 decision 1).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <new>          // ENDGAME B.3.9 IO: VisitorStream's ctor places its encoding

#include "Stream.h"      // RtIo::Stream, RtIo::CompressedStream, IO::Stream
#include "FileSystem.h"  // RtIO::FileInfo   (note the capital O — a different
                         // namespace spelling, and both are long-standing)
#include "../system/Filename.h"   // System::Filename — FileStream slot 7's argument

namespace RtIo {

// ============================================================
// IO::FileStream — 0x50 bytes. A Stream over a FileSystem file.
//
//   +0x00..0x17  Stream
//   +0x18  String   _filename   the path Create/Open last bound, refcounted
//   +0x20  FileInfo _file       0x30 bytes — RtIO::FileInfo, ALREADY PINNED
//
// THE +0x20 IDENTIFICATION IS THE LOAD-BEARING CLAIM IN THIS HEADER, and it
// collapses most of the layout work, so it is pinned four independent ways
// rather than asserted:
//
//  1. ??1FileStream@IO@@UEAA@XZ (0x1800aaab0) does
//         if (*(int*)(this+0x20) != -1) FileSystem::CloseFile(_singleton, this+0x20);
//         FUN_18000c180(this + 0x20);
//     — it passes `this+0x20` to CloseFile AS a FileInfo*, and FUN_18000c180
//     is ~FileInfo: its whole body releases two refcounted strings at +0x28 and
//     +0x20 of its argument, which are exactly FileInfo::_name1 and _name0.
//
//  2. ?Write@FileStream@IO@@UEAA_NHPEBX@Z (0x1800aab90) writes through
//     `*(HANDLE*)(this+0x38)` and then adds the byte count to
//     `*(int*)(this+0x2c)`. Against FileInfo at +0x20 those are winHandle
//     (+0x18) and cursor (+0x0C) — the two fields a write would touch, at the
//     two offsets FileSystem.h already asserts, with no fudge factor.
//
//  3. ?Create@FileStream@IO@@IEAA_NAEBVFilename@System@@@Z (0x1800aa8f0)
//     assigns the CreateFileW result into `this+0x20` through FUN_18000c200,
//     which filesystem_openfile_probe.cpp has already identified as
//     `FileInfo::operator=`, and then returns `*(int*)(this+0x20) != -1` —
//     FileInfo::handle's documented "nothing opened" sentinel.
//
//  4. The arithmetic closes exactly: 0x20 + sizeof(FileInfo) 0x30 == 0x50,
//     the measured size, with no tail padding.
//
// THE CONSTRUCTOR LEAVES ONE FIELD UNWRITTEN. ??0FileStream (0x1800aaa10)
// writes +0x18 q, +0x20 d, +0x28 q, +0x30 d, +0x38 q(-1), +0x40 q, +0x48 q.
// That never touches +0x24 — which is FileInfo::copiedUnwrittenWord, the field
// FileSystem.h ALREADY records as "never initialised by OpenFile" and which
// filesystem_openfile.cpp's oracle already excludes. Two unrelated
// functions leave the same field alone; that agreement is the point.
//
// Recorded rather than zeroed: a `= {}` in a port would be a behavioural
// difference from retail, not tidiness (saveload_meta.cpp takes the same
// position on CompressedStream +0x18/+0x24).
//
// VTABLE 0x180154960, EXPORTED as ??_7FileStream@IO@@6B@ (ordinal 1435) —
// 8 slots. The table ends at +0x38 because 0x180154960+0x40 == 0x1801549a0,
// which Stream.h already pins as ??_7Stream@IO@@6B@:
//   [0] +0x00  0x18000c090  deleting destructor thunk
//   [1] +0x08  0x1800aab50  GetSource
//   [2] +0x10  0x1800aab90  Write
//   [3] +0x18  0x1800aabf0  Read
//   [4] +0x20  0x1800aac10  SetCurrentPos
//   [5] +0x28  0x1800aac50  GetCurrentPos
//   [6] +0x30  0x1800aac60  GetSize
//   [7] +0x38  0x1800aac70  Open           <-- FileStream's own first virtual
//
// NOTE ON SLOT 7. Stream.h says "derived classes add Close at +0x38 and
// PreCommit at +0x40", which is true of the MemoryStream/CompressedStream
// branch and NOT of this one: IO::Stream declares only 7 virtuals, so slot 7
// is whatever the derived class declares first. MemoryStream puts Close there;
// FileStream puts Open there. The two are not the same slot meaning, and a
// port that dispatched +0x38 on a FileStream expecting Close would call Open.
// ============================================================
struct FileStream {
    Stream           base;       // +0x00
    char*            filename;   // +0x18  System::String, refcounted
    RtIO::FileInfo   file;       // +0x20
};
static_assert(offsetof(RtIo::FileStream, base) == 0x0, "FileStream::base");
static_assert(offsetof(FileStream, filename) == 0x18,
              "FileStream.filename @ 0x18 (~FileStream 0x1800aaad4 releases it)");
// VERIFIED BY AN INDEPENDENT ROUTE, not merely asserted. Ghidra's decompiler on
// ?Write@FileStream@IO@@UEAA_NHPEBX@Z (0x1800aab90) -- a body that has nothing to
// do with the destructor this claim was originally read off -- gives
//     WriteFile(*(HANDLE *)(this + 0x38), buf, len, &written, 0);
//     *(DWORD *)(this + 0x2c) = *(int *)(this + 0x2c) + written;
// so +0x38 is the HANDLE and +0x2c is the byte counter. Against `FileInfo` at
// +0x20 those are winHandle (+0x18) and cursor (+0x0C) at exactly the offsets
// FileSystem.h already asserts: ONE ALREADY-PINNED STRUCT PREDICTING TWO
// OFFSETS, BOTH LANDING. A third route agrees from a third direction --
// ?SaveText@External@Object@@ (0x1800bb9d0) reaches the same two fields from a
// frame pointer of rsp-0x37, so every offset in its epilogue is odd and none of
// it could have been copied from either of the other two bodies.
static_assert(offsetof(FileStream, file)     == 0x20,
              "FileStream.file @ 0x20 (~FileStream passes this+0x20 to CloseFile; "
              "confirmed independently by ?Write@FileStream's +0x38/+0x2c pair "
              "and again by ?SaveText@External's epilogue)");
static_assert(offsetof(FileStream, file) + offsetof(RtIO::FileInfo, cursor) == 0x2c,
              "?Write@FileStream 0x1800aabb8: `ADD dword [RCX+0x2c],EAX`; and "
              "?GetCurrentPos@FileStream 0x1800aac50 is `MOV EAX,[RCX+0x2c]; RET`");
static_assert(offsetof(FileStream, file) + offsetof(RtIO::FileInfo, size) == 0x28,
              "?GetSize@FileStream 0x1800aac60: `MOV EAX,dword ptr [RCX+0x28]; RET`");
static_assert(offsetof(FileStream, file) + offsetof(RtIO::FileInfo, winHandle) == 0x38,
              "?Write@FileStream 0x1800aaba0: `MOV RCX,[RCX+0x38]` into WriteFile");
static_assert(sizeof(FileStream) == 0x50,
              "sizeof(IO::FileStream) == 0x50 (eh_vector_dtor stride at 0x180023d80)");

// ============================================================
// IO::BinaryFileStream — 0x50 bytes. A FileStream and NOTHING ELSE.
//
// ??0BinaryFileStream@IO@@QEAA@XZ (0x1800a8200) runs the FileStream ctor
// inline, stamps its own vtable, and then installs an Encoding at +0x10 — a
// Stream field, not a new one. It writes no offset >= 0x50, and sizeof is the
// same 0x50 the FileStream thunk reports (the two classes share the deleting
// destructor thunk at 0x180023d80, which is itself the tell).
//
// The class exists to swap the vtable, and the ONLY slot it swaps is the
// destructor. Its table is byte-identical to FileStream's in slots 1..7:
//
//   ??_7BinaryFileStream@IO@@6B@ 0x1801554d0 (ordinal 1390) — 8 slots
//   [0] +0x00  0x180023d80  deleting dtor   <-- the only difference
//   [1..7]     identical to ??_7FileStream@IO@@6B@ above
//
// It ends at +0x38 because 0x1801554d0+0x40 == 0x180155510 ==
// ??_7CompressedCopyFileStream@IO@@6B@ (ordinal 1407) — the same adjacency
// argument compressedstream.cpp:725 uses to bound the table above it.
//
// DECLARED AS A DISTINCT TYPE ANYWAY, not a typedef, because the vtable it
// carries is a different pointer and a caller that stamps the wrong one gets a
// destructor mismatch that nothing else would catch.
// ============================================================
struct BinaryFileStream {
    FileStream base;  // +0x00 — and that is the whole class
};
static_assert(offsetof(RtIo::BinaryFileStream, base) == 0x0, "BinaryFileStream::base");
static_assert(sizeof(BinaryFileStream) == 0x50,
              "sizeof(IO::BinaryFileStream) == 0x50 (eh_vector_dtor stride 0x180023d80)");

// ============================================================
// IO::CompressedFileStream — 0xb8 bytes.
//
//   +0x00..0x67  CompressedStream base
//   +0x68..0xb7  BinaryFileStream _file   (a MEMBER, not a second base)
//
// THE SAME SHAPE AS CompressedMemoryStream, one class over: a CompressedStream
// whose _source is a concrete stream embedded in the same object. Stream.h
// already spells that pattern out at +0x68 for the memory case; this is the
// file case, at the same offset, for the same reason (CompressedStream is 0x68
// bytes, so the member lands right after it).
//
// PINNED BY THE CONSTRUCTOR, ??0CompressedFileStream@IO@@QEAA@XZ (0x1800a93b0),
// which is three lines:
//     CompressedStream::CompressedStream(this);
//     this->vptr = &CompressedFileStream::_vftable_;
//     BinaryFileStream::BinaryFileStream(this + 0x68);
//
// AND INDEPENDENTLY BY ITS ONE REAL CALLER. ?Load@External@Object@@
// (0x1800bc620) does not call that ctor — MSVC inlined it — and the inlined
// copy constructs the BinaryFileStream into a stack slot exactly 0x68 bytes
// above the CompressedStream's. Its destructor epilogue then reads the
// FileInfo handle at (object + 0x68 + 0x20) == +0x88 before calling
// FileSystem::CloseFile on it. Both agree with +0x68, from a body that has no
// idea what the constructor looked like.
//
// SIZE: 0x68 + 0x50 == 0xb8, which is the measured eh_vector stride. No padding.
//
// VTABLE 0x1801555f0, EXPORTED as ??_7CompressedFileStream@IO@@6B@
// (ordinal 1408) — 9 slots, the CompressedStream shape (Close at +0x38,
// PreCommit at +0x40), NOT the FileStream shape:
//   [0] +0x00  0x180024100  deleting dtor
//   [1] +0x08  0x18000c6a0  GetSource      (Stream's)
//   [2] +0x10  0x1800aa6c0  Write          (CompressedStream's)
//   [3] +0x18  0x1800aa780  Read           (CompressedStream's)
//   [4] +0x20  0x1800aa830  SetCurrentPos  (CompressedStream's)
//   [5] +0x28  0x1800aa850  GetCurrentPos  (CompressedStream's)
//   [6] +0x30  0x1800aa860  GetSize        (CompressedStream's)
//   [7] +0x38  0x1800a9490  Close          <-- ?Close@CompressedFileStream, its own
//   [8] +0x40  0x1800aa8c0  PreCommit      (CompressedStream's)
//
// TWO vtables live in one object: this one at +0x00 and ??_7BinaryFileStream
// at +0x68. That is not the two-vtables-on-one-CLASS hazard Wave 11 tracks —
// they are on two different sub-objects at two different offsets, and each
// ctor stamps its own.
// ============================================================
struct CompressedFileStream {
    CompressedStream base;  // +0x00
    BinaryFileStream file;  // +0x68
};
static_assert(offsetof(RtIo::CompressedFileStream, base) == 0x0, "CompressedFileStream::base");
static_assert(offsetof(CompressedFileStream, file) == 0x68,
              "CompressedFileStream.file @ 0x68 (ctor 0x1800a93c8: LEA RCX,[RBX+0x68])");
static_assert(sizeof(CompressedFileStream) == 0xb8,
              "sizeof(IO::CompressedFileStream) == 0xb8 (eh_vector_dtor stride 0x180024100)");

// ============================================================
// IO::TextFileStream — 0x80 bytes. A FileStream that writes/parses text.
//
//   +0x00..0x4f  FileStream
//   +0x50  int32   _objectVersion   SetObject's third argument
//   +0x54  (never written by either constructor)
//   +0x58  String  _objectName      SetObject's SECOND argument  <-- see below
//   +0x60  String  _objectType      SetObject's FIRST argument   <-- see below
//   +0x68  int32   _requestedFormat ctor: 6
//   +0x6c  int32   _activeEncoding  ctor: 6        <-- the BOM selector
//   +0x70  void*   _buffer          owned, plain MemFree in the dtor
//   +0x78  void*   (ctor zeroes it; not freed by the destructor)
//
// PINNED BY BOTH CONSTRUCTORS AND BY ITS CALLER, which is worth having because
// the two constructors disagree by exactly one store:
//
//   ??0TextFileStream@IO@@QEAA@XZ                    (0x1800ba530) writes +0x50 = 0
//   ??0TextFileStream@IO@@QEAA@AEBVString@..@0H@Z    (0x1800ba340) does NOT,
//       and instead tail-calls SetObject(type, name, version) — which is what
//       sets +0x50, +0x58 and +0x60. So +0x50 is the VERSION, not a flag: the
//       default ctor's 0 and SetObject's third argument write the same slot.
//
// +0x58 vs +0x60 — THE TYPE AND THE NAME ARE THE OTHER WAY ROUND FROM THE
// OBVIOUS READING, and this header had them swapped until retail's own
// accessors were run against it:
//
//     ?GetObjectType@TextFileStream@IO@@QEBAAEBVString@System@@XZ   0x1800ba260
//         is `LEA RAX,[RCX + 0x60] ; RET`
//     ?GetObjectName@TextFileStream@IO@@QEBAAEBVString@System@@XZ   0x1800ba270
//         is `LEA RAX,[RCX + 0x58] ; RET`
//     ?GetObjectVersion@TextFileStream@IO@@QEBAHXZ                  0x1800ba250
//         is `MOV EAX,dword ptr [RCX + 0x50] ; RET`
//
// and ?SetObject@ (0x1800ba280) agrees from the write side, independently:
// its FIRST String argument lands at +0x60 and its SECOND at +0x58, with the
// int at +0x50. Two unrelated bodies, same answer.
//
// The swap was not catchable by inspection. Both fields are refcounted
// `String`s written by the same idiom in the same function, both are read by
// one-instruction accessors, and the constructor's argument list is
// `(String const&, String const&, int)` with no name attached to either. A pair
// of same-typed adjacent fields is a coin flip, and the coin came up wrong
// here: the only thing that settles it is a body that NAMES one of them, which
// is what the accessors do. Recorded rather than quietly fixed because
// "guessed a symmetric pair" is a defect class, not a slip.
//
// +0x68 vs +0x6c, which are both 6 and would otherwise be indistinguishable:
// ?SaveText@External@Object@@ (0x1800bb9d0) writes BOTH to 2 and then branches
// on +0x6c ALONE to decide the byte-order mark —
//     3 -> EF BB BF (UTF-8)   4 -> FF FE   5 -> FE FF   0 or 2 -> none
// — so +0x6c is the encoding actually in force and +0x68 is the one requested.
// Nothing this lane touches writes them to different values; the distinction is
// recorded because guessing them the same way round is a coin flip.
//
// THE DESTRUCTOR IS WHAT PROVES THE THREE POINTERS ARE DIFFERENT KINDS.
// SaveText's inlined epilogue (and ??1TextFileStream 0x1800ba490) frees +0x70
// with a BARE MemFree, and releases +0x58 and +0x60 through the refcounted
// String idiom (decrement the int at p[-4], free p-8 at zero). +0x78 is
// touched by neither. A port that used one idiom for all three would leak two
// strings and double-free a buffer.
//
// VTABLE 0x180155980, EXPORTED as ??_7TextFileStream@IO@@6B@ (ordinal 1516) —
// 10 slots. It is bounded not by the next class's table but by an ENCODING
// table: 0x180155980+0x50 == 0x1801559d0, which is the very Encoding vtable
// this class's own constructor installs at +0x10. That is a tighter bound than
// adjacency-to-a-class, because the constructor names the address.
//   [0] +0x00  0x18002ba90  deleting dtor
//   [1] +0x08  0x1800aab50  GetSource       (FileStream's)
//   [2] +0x10  0x1800ba9f0  Write
//   [3] +0x18  0x1800ba900  Read
//   [4] +0x20  0x1800babd0  SetCurrentPos
//   [5] +0x28  0x1800bac30  GetCurrentPos
//   [6] +0x30  0x1800ba8e0  GetSize
//   [7] +0x38  0x1800aac70  Open            (FileStream's — slot 7 is Open here)
//   [8] +0x40  0x1800ba640  Open(Filename, Text::Format)   <-- its own overload
//   [9] +0x48  0x1800ba940  Create(Filename, Text::Format, Filename)
// ============================================================
struct TextFileStream {
    FileStream base;             // +0x00
    int32_t    objectVersion;    // +0x50
    int32_t    _pad54;           // +0x54 — never written by either ctor
    char*      objectName;       // +0x58  System::String, refcounted
    char*      objectType;       // +0x60  System::String, refcounted
    int32_t    requestedFormat;  // +0x68  ctor: 6
    int32_t    activeEncoding;   // +0x6c  ctor: 6 — the BOM selector
    void*      buffer;           // +0x70  owned, bare MemFree
    void*      bufferSizeAndCursor;           // +0x78  zeroed by the ctor, never freed
};
static_assert(offsetof(RtIo::TextFileStream, base) == 0x0, "TextFileStream::base");
static_assert(offsetof(RtIo::TextFileStream, _pad54) == 0x54, "TextFileStream::_pad54");
static_assert(offsetof(TextFileStream, objectVersion)   == 0x50,
              "??0TextFileStream 0x1800ba55d writes +0x50 = 0; SetObject writes the version there");
static_assert(offsetof(TextFileStream, objectName)      == 0x58,
              "?GetObjectName@TextFileStream 0x1800ba270: `LEA RAX,[RCX+0x58]`");
static_assert(offsetof(TextFileStream, objectType)      == 0x60,
              "?GetObjectType@TextFileStream 0x1800ba260: `LEA RAX,[RCX+0x60]`");
static_assert(offsetof(TextFileStream, requestedFormat) == 0x68, "ctor: `MOV dword [RCX+0x68],6`");
static_assert(offsetof(TextFileStream, activeEncoding)  == 0x6c,
              "ctor: `MOV dword [RCX+0x6c],6`; SaveText branches on THIS one for the BOM");
static_assert(offsetof(TextFileStream, buffer)          == 0x70,
              "~TextFileStream frees +0x70 with a bare MemFree, not the String idiom");
static_assert(sizeof(TextFileStream) == 0x80,
              "sizeof(IO::TextFileStream) == 0x80 (eh_vector_dtor stride 0x18002ba90)");

// ============================================================
// IO::VisitorStream — 0x18 bytes. A Stream and NOTHING ELSE.
//
// This is the smallest class in the family and the most surprising one, so the
// claim is stated plainly: VisitorStream adds NO fields to IO::Stream, and its
// stream methods do nothing.
//
//   ??0VisitorStream@IO@@QEAA@XZ (0x1800bb090): run the Stream ctor, stamp its
//     own vtable, set _direction = 1 (writing), install an Encoding at +0x10
//     whose vtable is 0x1801559f8. No other store.
//   ??1VisitorStream@IO@@UEAA@XZ (0x1800bb040): stamp its vtable, stamp
//     Stream's, delete the Encoding at +0x10. No other read.
//   ?Write@VisitorStream@IO@@UEAA_NHPEBX@Z (0x1800bb030): `return true;`
//     — the entire body. It accepts every byte and stores none.
//
// IT IS A NULL SINK, and that is the mechanism of the whole reference-visit
// pass. `ExternalFactory::Add` constructs one and then calls the External's
// own Serialize (vtable +0x28) against it. The object walks itself exactly as
// it would when saving, every byte it emits is swallowed, and the only thing
// that actually happens is the side effect in the Encoding at +0x10 — the
// Visitor encoding at 0x1801559f8, which records the resource references the
// walk passes through. Serializer.h:154 and serialize_family.cpp:125 both
// describe that pass from the other end; this is the stream half of it.
//
// SIZE 0x18 is measured (eh_vector stride at 0x18002bf80) and cross-checked by
// its one caller: Add's stack slot spans exactly vptr / direction / encoding
// and the inlined destructor reads only +0x10.
//
// VTABLE 0x180155a20, EXPORTED as ??_7VisitorStream@IO@@6B@ (ordinal 1528) —
// 7 slots, the bare IO::Stream shape with no Open, Close or PreCommit. It is
// bounded by a STRING: the qword at 0x180155a58 is ASCII "External", i.e. the
// table runs straight into .rdata string data.
//   [0] +0x00  0x18002bf80  deleting dtor
//   [1] +0x08  0x18000c6a0  GetSource      (Stream's)
//   [2] +0x10  0x1800bb030  Write          — `return true`
//   [3] +0x18  0x1800bb020  Read
//   [4] +0x20  0x1800bb010  SetCurrentPos
//   [5] +0x28  0x1800bb000  GetCurrentPos
//   [6] +0x30  0x1800baff0  GetSize
//
// DO NOT CONFUSE THIS WITH IO::Visitor (??_7Visitor@IO@@6B@, 0x1801558b0),
// which is a 24+-slot SERIALIZER subclass with the whole Serialize overload
// set. VisitorStream is the Stream; Visitor is the Serializer that rides on it.
// The names are one token apart and the two are not related by inheritance.
// ============================================================
struct VisitorStream {
    Stream base;  // +0x00 — and that is the whole class
};
static_assert(offsetof(RtIo::VisitorStream, base) == 0x0, "VisitorStream::base");
static_assert(sizeof(VisitorStream) == 0x18,
              "sizeof(IO::VisitorStream) == 0x18 (eh_vector_dtor stride 0x18002bf80)");

// FileStream's slot 7 is Open, NOT Close (the MemoryStream / CompressedStream
// branch puts Close there). B3X W0: callers name `IO::FileStream::Open`; the
// `kFileStreamVtblOpenSlot` index constant had no reader and is retired.

}  // namespace RtIo

// ===========================================================================
// ENDGAME B.3.4 block 4 (STREAMS) — the REAL `IO::FileStream` and
// `IO::BinaryFileStream`.
// ===========================================================================
//
// Design: B34_RULES.md section 1.3 (the slot lists and the slot-7 trap),
// section 4.1 (`NoteSlotDispatch` dies with this commit), section 4.3 (no real
// constructors). Bodies: src/engine/io/file_stream.cpp and file_stream_write.cpp.
// The rules that govern the whole family are stated once, in Stream.h's
// `namespace IO` banner; only what is specific to this pair is repeated here.
//
// ---------------------------------------------------------------------------
// SLOT 7 IS `Open`, AND IT IS THE ONE PLACE THE FAMILY DISAGREES WITH ITSELF
// ---------------------------------------------------------------------------
// `IO::Stream` declares seven virtuals, so slot 7 is whatever the derived class
// declares FIRST. On the MemoryStream / CompressedStream branch that is
// `Close()`; here it is `Open(const System::Filename&)`. A port that dispatched
// +0x38 on a FileStream expecting `Close` would call `Open` — same offset, same
// table shape, completely different behaviour, and nothing would diagnose it.
// The C++ declaration below is what now makes the two unmistakable.
//
// `IO::TextFileStream` then OVERLOADS `Open` across the base/derived boundary
// (its own `(Filename, Format)` form is slot 8), which is why section 1.3's
// rule names that one `OpenWithFormat` and leaves this one `Open`.
//
// ---------------------------------------------------------------------------
// `_filename` IS A `System::String` MEMBER (B4 X-RH, H5)
// ---------------------------------------------------------------------------
// Retail's ctor 0x1800aaa10 stores +0x18 = 0 (String's default ctor, inlined:
// `MOV qword ptr [RCX+0x18],R8` at 0x1800aaa43); retail's dtor 0x1800aaab0
// closes the file (0x1800aaae6), runs ~FileInfo (0x1800aaaf0), releases +0x18
// with ~String's inlined body (0x1800aaaf6..0x1800aab18), THEN restamps the
// Stream table and frees the encoding (~Stream). Declared as a String, the
// member is default-constructed before the ctor body and destroyed after the
// dtor body and before ~Stream: exactly that order. The dtor body therefore no
// longer releases it by hand (the B33 double-free rule is met by having ONE
// release, the compiler's).
// ===========================================================================

namespace IO {

// ---------------------------------------------------------------------------
// IO::FileStream — vtable 0x180154960, 8 slots, 0x50 bytes.
// ---------------------------------------------------------------------------
// ABSTRACT: slot 0 (0x18000c090) is left PURE because its body is the
// by-address registration at file_stream.cpp, filling slot 0 of the live minted
// table (rt_data_mint_tables.inc:2423), and nothing holds a `FileStream` by
// value. `IO::BinaryFileStream` is the one that must be concrete.
class FileStream : public Stream {
public:
    // ENDGAME B.3.9 IO -- ??0FileStream@IO@@QEAA@XZ 0x1800aaa10, eleven
    // stores and nothing else (no encoding): the Stream base, `_filename`
    // null, then the FileInfo's handle -1, size/cursor (one qword) 0,
    // baseOffset 0, both names null, winHandle -1. FileInfo::copiedUnwrittenWord (+0x24)
    // and the padding at +0x34 are NOT written -- a `= {}` would be a
    // divergence. B.4 Z LAST IO: the FileInfo's stores (1800a8243..1800a8268)
    // are its own default ctor, run as the member's construction after
    // `_filename`'s.
    FileStream() {}

    // slot 0  +0x00  0x18000c090 — MSVC's vector deleting dtor, stride 0x50.
    // A plain FileStream IS built (the persistence loader, Audio::Stream::Run).
    // ??1FileStream@IO@@UEAA@XZ 0x1800aaab0 (file_stream.cpp): close the file
    // if open (through the FileSystem singleton), ~FileInfo, release
    // `_filename`; then ~Stream frees the encoding. Virtual (B3X W1).
    ~FileStream() override;
    // slot 1  +0x08  0x1800aab50 — an ADD-REF of `_filename`, not a copy.
    System::String GetSource() const override;
    bool     Write(int n, const void* src) override;   // slot 2 +0x10  0x1800aab90
    bool     Read(int n, void* dst) override;          // slot 3 +0x18  0x1800aabf0
    bool     SetCurrentPos(int pos) override;          // slot 4 +0x20  0x1800aac10
    uint32_t GetCurrentPos() const override;           // slot 5 +0x28  0x1800aac50
    uint32_t GetSize() const override;                 // slot 6 +0x30  0x1800aac60
    // slot 7 +0x38 — FileStream's own first virtual. NOT `Close`.
    virtual bool Open(const System::Filename& path);   //              0x1800aac70

    // ?Create@FileStream@IO@@IEAA_NAEBVFilename@System@@@Z 0x1800aa8f0 --
    // NON-virtual, `IEAA` (protected) in retail. ENDGAME B.3.9 IO (W2): a
    // member now (its by-name door is gone); PUBLIC because the callers retail
    // compiled as friends/derived code (SaveImageToFile, External::SaveText,
    // the persistence SAVE job, TextFileStream::Create) reach it from outside
    // this hierarchy in our tree. Out of line in file_stream.cpp.
    bool Create(const System::Filename& path);

    // ---- the 0x50 bytes, at retail's offsets -------------------------------
    System::String _filename;   // +0x18  see the banner (ctor 0x1800aaa43, dtor 0x1800aaaf6)
    RtIO::FileInfo _file;       // +0x20  0x30 bytes; ctor 1800a8243..68, dtor 1800aaaf0 (0x18000c180)
};

static_assert(sizeof(FileStream) == 0x50,
              "sizeof(IO::FileStream) == 0x50 (eh_vector_dtor stride 0x180023d80)");
static_assert(offsetof(FileStream, _filename) == 0x18,
              "~FileStream 0x1800aaad4 releases +0x18");
static_assert(offsetof(FileStream, _file) == 0x20,
              "~FileStream passes this+0x20 to CloseFile; ?Write@FileStream's "
              "+0x38/+0x2c pair and ?SaveText@External's epilogue agree");
static_assert(offsetof(FileStream, _file) + offsetof(RtIO::FileInfo, cursor) == 0x2c,
              "?GetCurrentPos@FileStream 0x1800aac50: `MOV EAX,[RCX+0x2c]; RET`");
static_assert(offsetof(FileStream, _file) + offsetof(RtIO::FileInfo, size) == 0x28,
              "?GetSize@FileStream 0x1800aac60: `MOV EAX,[RCX+0x28]; RET`");
static_assert(offsetof(FileStream, _file) + offsetof(RtIO::FileInfo, winHandle) == 0x38,
              "?Write@FileStream 0x1800aaba0: `MOV RCX,[RCX+0x38]` into WriteFile");

// ---------------------------------------------------------------------------
// IO::BinaryFileStream — vtable 0x1801554d0, 8 slots, 0x50 bytes.
// ---------------------------------------------------------------------------
// A `FileStream` AND NOTHING ELSE. It adds no field and overrides exactly ONE
// slot: `??_7BinaryFileStream@` and `??_7FileStream@` are byte-identical in
// slots 1..7 (re-verified from .rdata, B34_RULES section 1.3) and differ only
// at slot 0, 0x180023d80 against 0x18000c090.
//
// THAT IS THE RETAIL FACT `file_stream.cpp`'s `NoteSlotDispatch` EXISTED FOR:
// with two byte-identical tables a per-body counter could not say which row had
// dispatched, so an `if (vptr == ours)` attendance counter stood in for the
// answer. The question is answered STRUCTURALLY once the two classes exist —
// there is one table per class, the compiler's — so section 4.1 retires the
// whole instrument (the struct, the global, `OurBfsTable`, `NoteBfsStamp`,
// `NoteSlotDispatch`, all seven call sites and `RtIoBfsPromoteCounters`) in
// this commit rather than leaving it counting a constant.
//
// ENDGAME B.3.9 IO: built by its REAL public constructor (retail's
// 0x1800a8200) and wearing the compiler's table; the minted ??_7FileStream /
// ??_7BinaryFileStream / ??_7TextFileStream rows are retired. The destructor
// is virtual (B3X W1); `CompressedFileStream` holds one BY VALUE at +0x68.
class BinaryFileStream : public FileStream {
public:
    // ENDGAME B.3.9 IO -- ??0BinaryFileStream@IO@@QEAA@XZ 0x1800a8200: the
    // FileStream prologue, then MemAlloc(8,4) and the BINARY encoding (null on
    // a failed allocation, retail's branch), the dead free of the previous
    // encoding, the store.
    BinaryFileStream() {
        void* encMem = RtSystem::RtMemAlloc(8, 4);                  // 1800a82a5
        Encoding* enc = nullptr;
        if (encMem) enc = ::new (encMem) BinaryEncoding();                   // 1800a82bb
        if (_encoding) RtSystem::RtMemFree(_encoding);           // 1800a82d3
        _encoding = enc;                 // 1800a82d8
    }

    // slot 0 +0x00  0x180023d80 — the ONLY difference from FileStream's table:
    // the vector deleting dtor naming ~BinaryFileStream, stride 0x50.
    // ??1BinaryFileStream@IO@@UEAA@XZ 0x180023c80 is a bare JMP to
    // ~FileStream: nothing of its own. Virtual (B3X W1); the by-value holder
    // (CompressedFileStream +0x68) reaches it as a friend.
    ~BinaryFileStream() override = default;

protected:
    friend class CompressedFileStream;
};

static_assert(sizeof(BinaryFileStream) == 0x50,
              "IO::BinaryFileStream adds no field: same 0x50 as IO::FileStream");
static_assert(sizeof(BinaryFileStream) == sizeof(FileStream),
              "the class exists to swap slot 0, not to add state");

// ---- the duplicate-POD cross-asserts (B34_RULES section 1.5.3) -------------
static_assert(sizeof(FileStream) == sizeof(RtIo::FileStream),
              "IO::FileStream and the RtIo::FileStream POD stay interchangeable");
static_assert(sizeof(BinaryFileStream) == sizeof(RtIo::BinaryFileStream),
              "IO::BinaryFileStream and its POD stay interchangeable");
static_assert(offsetof(FileStream, _filename) ==
                  offsetof(RtIo::FileStream, filename), "alias filename");
static_assert(offsetof(FileStream, _file) ==
                  offsetof(RtIo::FileStream, file), "alias file");
// B.4 Z LAST IO: the RtIo POD view holds a FileInfo, which has real special
// members now; the view is never built or destroyed by value (grepped), so
// its old "trivially destructible" pin is gone.

// ---------------------------------------------------------------------------
// IO::TextFileStream — vtable 0x180155980, 10 slots, 0x80 bytes.
// ---------------------------------------------------------------------------
// THE FAMILY'S ONE OVERLOAD-ORDER CASE, and the reason B34_RULES section 1.3
// gives slot 8 a different NAME rather than a second `Open`:
//
//   slot 7  +0x38  0x1800aac70  Open(const Filename&)           INHERITED from
//                                                               IO::FileStream
//   slot 8  +0x40  0x1800ba640  Open(const Filename&, Format)   TextFileStream's
//                                                               OWN overload
//
// So `Open` is overloaded ACROSS the base/derived boundary and BOTH entries are
// live in the shipped table. Declaring the slot-8 form `Open` here would HIDE
// the base's name — a compile error at every inherited-`Open` call site rather
// than a silent fault, but decision 2 asks for distinct names anyway and
// `OpenWithFormat` is what makes slot 8 unmistakable at a glance.
//
// Slot 1 is FileStream's `GetSource` (0x1800aab50), also inherited and NOT
// overridden, so this class overrides seven of the ten and adds two.
//
// ⚠ ITS CONSTRUCTION SITE IS NOT IN THIS BLOCK'S FILE SET. `External::SaveText`
// stack-builds one — `RtIo::TextFileStream ts;` at
// src/engine/io/external_savetext.cpp:286, with a real ctor door at
// :299 and an independent re-stamp at :362. That file is not block 4's, which
// is exactly why section 4.3's "no real constructors" is non-negotiable here:
// a C++ constructor would swap the table under an object built elsewhere.
// ABSTRACT (slot 0 pure, body registered by RT_DELDTOR row 41 in
// deleting_dtor.cpp) because nothing holds a `TextFileStream` by value.
//
// ---------------------------------------------------------------------------
// +0x78 IS TWO int32s, NOT THE POD'S `void* bufferSizeAndCursor`
// ---------------------------------------------------------------------------
// The `RtIo::TextFileStream` POD above models +0x78 as `void* bufferSizeAndCursor`, "zeroed
// by the ctor, never freed", and text_file_stream.cpp reached the two real
// fields through explicit offsets rather than edit that POD — deliberately, so
// the disagreement stayed visible at the point of use. The CLASS resolves it,
// because the bodies that pin it now live on the class:
//
//   `GetSize`       returns +0x78 or the inherited FileInfo::size   (+0x28)
//   `GetCurrentPos` returns +0x7c or the inherited FileInfo::cursor (+0x2c)
//
// from the two arms of one branch each, so each buffer field is identified by
// the inherited field it stands in for, and both of those are independently
// asserted above. `Open` (slot 8) then WRITES +0x78 with the transcoded byte
// count. So +0x78 is an int32 SIZE and +0x7c an int32 CURSOR; the POD's "never
// freed" stops being an observation and becomes a consequence — it is not a
// pointer, so there is nothing to free. Same eight bytes either way, and the
// cross-assert below pins the class's pair against the POD's single field.
class TextFileStream : public FileStream {
public:
    // ENDGAME B.3.9 IO -- ??0TextFileStream@IO@@QEAA@AEBVString@System@@0H@Z
    // 0x1800ba340 (file_stream.cpp): the FileStream prologue, +0x58/+0x60
    // null, both formats 6, +0x70 null, +0x78 (one qword) 0, the TEXT
    // encoding, then `SetObject(type, name, version)`. +0x50/+0x54 are left to
    // SetObject / never written.
    TextFileStream(const System::String& type, const System::String& name, int version);

    // slot 0  +0x00  0x18002ba90 — the vector deleting dtor, stride 0x80.
    // ??1TextFileStream@IO@@UEAA@XZ 0x1800ba490 (file_stream.cpp): bare
    // MemFree of `_buffer` (not nulled), release `_objectType` then
    // `_objectName`, then ~FileStream. Virtual (B3X W1).
    ~TextFileStream() override;
    // slot 1  +0x08  0x1800aab50 — IO::FileStream::GetSource, NOT overridden.
    bool     Write(int n, const void* src) override;   // slot 2 +0x10  0x1800ba9f0
    bool     Read(int n, void* dst) override;          // slot 3 +0x18  0x1800ba900
    bool     SetCurrentPos(int pos) override;          // slot 4 +0x20  0x1800babd0
    uint32_t GetCurrentPos() const override;           // slot 5 +0x28  0x1800bac30
    uint32_t GetSize() const override;                 // slot 6 +0x30  0x1800ba8e0
    // slot 7  +0x38  0x1800aac70 — IO::FileStream::Open, NOT overridden.

    // slot 8  +0x40  0x1800ba640 — `?Open@TextFileStream@IO@@UEAA_NAEBVFilename@
    // System@@W4Format@Text@4@@Z`. Named `OpenWithFormat`, not `Open`: see the
    // banner. It is the body that INSTALLS `_buffer`, and it CLOSES the file
    // before returning, which is why `Read` never touches the handle.
    virtual bool OpenWithFormat(const System::Filename& path, Text::Format format);

    // slot 9  +0x48  0x1800ba940 — `(Filename const&, Text::Format,
    // Text::Format)`. THE LAST TWO ARGUMENTS ARE BOTH FORMATS: the mangled
    // name's trailing `1` back-references `W4Format@Text@4@`, and
    // `MOV [RCX+0x68],R8D ; MOV [RCX+0x6c],R9D` stores both as DWORDS, which a
    // `Filename const&` in R9 could not be.
    virtual bool Create(const System::Filename& path,
                        Text::Format requestedFormat, Text::Format activeEncoding);

    // ---- the 0x80 bytes, at retail's offsets -------------------------------
    int32_t _objectVersion;    // +0x50  ?GetObjectVersion@ is `MOV EAX,[RCX+0x50]`
    int32_t _pad54;            // +0x54  never written by either constructor
    // B.4 Y RT (`behaviour:`): the two object names are real Strings. Ctor
    // 0x1800ba340 nulls +0x58 then +0x60 (their own ctors, declaration
    // order); dtor 0x1800ba490 frees `_buffer` (the body), then releases +0x60
    // then +0x58 (0x1800ba4bf / 0x1800ba4e9) before ~FileStream: the members'
    // own ~Strings, reverse declaration order, after the body.
    System::String _objectName;  // +0x58  ?GetObjectName@ LEAs it
    System::String _objectType;  // +0x60  ?GetObjectType@ LEAs it
    Text::Format _requestedFormat;  // +0x68  ctor: Text::Format::Detect (6)
    Text::Format _activeEncoding;   // +0x6c  ctor: Text::Format::Detect (6) — THE BOM SELECTOR
    uint8_t* _buffer;          // +0x70  owned, bare MemFree in the dtor door (the transcoded bytes OpenWithFormat installs)
    int32_t _bufSize;          // +0x78  bytes in _buffer — see the banner
    int32_t _bufCursor;        // +0x7c  position IN _buffer
};
static_assert(offsetof(IO::TextFileStream, _pad54) == 0x54, "TextFileStream::_pad54");

static_assert(sizeof(TextFileStream) == 0x80,
              "sizeof(IO::TextFileStream) == 0x80 (eh_vector_dtor stride 0x18002ba90)");
static_assert(offsetof(TextFileStream, _objectVersion)  == 0x50,
              "?GetObjectVersion@TextFileStream 0x1800ba250: `MOV EAX,[RCX+0x50]`");
static_assert(offsetof(TextFileStream, _objectName)     == 0x58,
              "?GetObjectName@TextFileStream 0x1800ba270: `LEA RAX,[RCX+0x58]`");
static_assert(offsetof(TextFileStream, _objectType)     == 0x60,
              "?GetObjectType@TextFileStream 0x1800ba260: `LEA RAX,[RCX+0x60]`");
static_assert(offsetof(TextFileStream, _requestedFormat) == 0x68,
              "?Create@TextFileStream 0x1800ba949: `MOV [RCX+0x68],R8D`");
static_assert(offsetof(TextFileStream, _activeEncoding)  == 0x6c,
              "?Create@TextFileStream 0x1800ba94d: `MOV [RCX+0x6c],R9D`; "
              "?SaveText@External branches on THIS one for the BOM");
static_assert(offsetof(TextFileStream, _buffer)   == 0x70,
              "the discriminator four bodies branch on; ~TextFileStream frees it "
              "with a bare MemFree, not the String idiom");
static_assert(offsetof(TextFileStream, _bufSize)   == 0x78,
              "?GetSize@TextFileStream 0x1800ba8e7: `MOV EAX,[RCX+0x78]` in the "
              "buffer arm, against FileInfo::size in the file arm");
static_assert(offsetof(TextFileStream, _bufCursor) == 0x7c,
              "?GetCurrentPos@TextFileStream 0x1800bac37: `MOV EAX,[RCX+0x7c]`");

// ---------------------------------------------------------------------------
// IO::VisitorStream — vtable 0x180155a20, 7 slots, 0x18 bytes.
// ---------------------------------------------------------------------------
// A `Stream` AND NOTHING ELSE, and a NULL SINK: its five stream slots are two
// instructions each and its `Read` returns true WITHOUT touching the caller's
// buffer. That is the mechanism of the whole reference-visit pass —
// `?Add@ExternalFactory@Object@@` stack-constructs one, the ctor sets
// `_direction` to 1 (WRITING) and installs the VISITOR Encoding at +0x10
// (0x1801559f8, NOT the shared BinaryEncoding 0x1801554a8), then runs the
// External's own `Serialize` against it. The object walks itself exactly as a
// save would, every byte is swallowed, and the only thing that happens is the
// Encoding's side effect: recording the resource references the walk passes.
//
// SEVEN SLOTS, the bare `IO::Stream` shape — no `Open`, no `Close`, no
// `PreCommit`. Slot 1 is `IO::Stream`'s OWN `GetSource` (0x18000c6a0), not
// overridden, so this class overrides exactly slots 2..6 and adds nothing.
// The table is bounded by a STRING: the qword at 0x180155a58 is "External".
//
// ⚠ ITS CONSTRUCTION SITE IS ITS OWN PORTED CTOR (0x1800bb090,
// iobase_vtables.cpp), which does the base->derived vptr restamp by hand
// against `RT_DATA_SLOT("??_7VisitorStream@IO@@6B@")`. Section 4.3: that ctor
// does NOT convert, and `??_7VisitorStream@` is additionally stamped from
// OUTSIDE src/engine/io (external_add.cpp), so its minted row stays.
// ABSTRACT (slot 0 pure, 0x18002bf80 registered by address at
// iobase_vtables.cpp) because nothing holds a `VisitorStream` by value.
//
// DO NOT CONFUSE THIS WITH `IO::Visitor` (??_7Visitor@IO@@6B@, 0x1801558b0),
// which is a 26-slot `ReaderWriter` subclass. VisitorStream is the Stream;
// Visitor is the serializer that rides on it. The names are one token apart and
// the two are not related by inheritance.
class VisitorStream : public Stream {
public:
    // ENDGAME B.3.9 IO -- ??0VisitorStream@IO@@QEAA@XZ 0x1800bb090, in retail's
    // store order after the base ctor: `_direction = 1` (WRITING), then
    // MemAlloc(8,4) and the VISITOR encoding in it (null on a failed
    // allocation -- retail's branch), then the dead free of the previous
    // encoding (always null here) and the store.
    VisitorStream() {
        _direction = 1;                                         // 1800bb0d3
        void* eMem = RtSystem::RtMemAlloc(8, 4);                   // 1800bb107
        Encoding* e = nullptr;
        if (eMem) e = ::new (eMem) VisitorEncoding();                     // 1800bb118
        if (_encoding) RtSystem::RtMemFree(_encoding);          // 1800bb127
        _encoding = e;                  // 1800bb135
    }

    // slot 0  +0x00  0x18002bf80 — MSVC's vector deleting dtor, stride 0x18
    // (the eh_vector_dtor element size). `?Add@ExternalFactory@` destroys its
    // stack instance through the inlined dtor, i.e. flags 0.
    // ??1VisitorStream@IO@@UEAA@XZ 0x1800bb040: two table stamps, then
    // ~Stream frees the encoding -- exactly the compiler's chain.
    ~VisitorStream() override = default;
    // slot 1  +0x08  0x18000c6a0 — IO::Stream::GetSource, NOT overridden.
    bool     Write(int n, const void* src) override;   // slot 2 +0x10  0x1800bb030
    bool     Read(int n, void* dst) override;          // slot 3 +0x18  0x1800bb020
    bool     SetCurrentPos(int pos) override;          // slot 4 +0x20  0x1800bb010
    uint32_t GetCurrentPos() const override;           // slot 5 +0x28  0x1800bb000
    uint32_t GetSize() const override;                 // slot 6 +0x30  0x1800baff0

    // NO FIELDS. sizeof is IO::Stream's 0x18, measured from the eh_vector stride
    // at 0x18002bf80 and cross-checked by `?Add@ExternalFactory@`'s stack slot,
    // which spans exactly vptr / direction / encoding.
};

static_assert(sizeof(VisitorStream) == 0x18,
              "sizeof(IO::VisitorStream) == 0x18 (eh_vector_dtor stride 0x18002bf80)");
static_assert(sizeof(VisitorStream) == sizeof(Stream),
              "IO::VisitorStream adds no field to IO::Stream");

// ---- the duplicate-POD cross-asserts, TextFileStream and VisitorStream -----
static_assert(sizeof(TextFileStream) == sizeof(RtIo::TextFileStream),
              "IO::TextFileStream and its POD stay interchangeable");
static_assert(offsetof(TextFileStream, _objectVersion) ==
                  offsetof(RtIo::TextFileStream, objectVersion), "alias objectVersion");
static_assert(offsetof(TextFileStream, _objectName) ==
                  offsetof(RtIo::TextFileStream, objectName), "alias objectName");
static_assert(offsetof(TextFileStream, _objectType) ==
                  offsetof(RtIo::TextFileStream, objectType), "alias objectType");
static_assert(offsetof(TextFileStream, _requestedFormat) ==
                  offsetof(RtIo::TextFileStream, requestedFormat), "alias requestedFormat");
static_assert(offsetof(TextFileStream, _activeEncoding) ==
                  offsetof(RtIo::TextFileStream, activeEncoding), "alias activeEncoding");
static_assert(offsetof(TextFileStream, _buffer) ==
                  offsetof(RtIo::TextFileStream, buffer), "alias buffer");
// The one place the class says MORE than the POD: the POD's single `void*
// bufferSizeAndCursor` covers the class's `_bufSize` + `_bufCursor` pair. Same eight bytes,
// and the pin is that the pair STARTS where the POD's field does.
static_assert(offsetof(TextFileStream, _bufSize) ==
                  offsetof(RtIo::TextFileStream, bufferSizeAndCursor),
              "the class's int32 pair starts at the POD's _unk78");
static_assert(sizeof(RtIo::TextFileStream::bufferSizeAndCursor) ==
                  sizeof(TextFileStream::_bufSize) + sizeof(TextFileStream::_bufCursor),
              "two int32s cover exactly the POD's one pointer-sized field");
// B.4 Z LAST IO: the RtIo POD view holds a FileInfo, which has real special
// members now; the view is never built or destroyed by value (grepped), so
// its old "trivially destructible" pin is gone.

static_assert(sizeof(VisitorStream) == sizeof(RtIo::VisitorStream),
              "IO::VisitorStream and its POD stay interchangeable");
static_assert(std::is_trivially_destructible<RtIo::VisitorStream>::value,
              "the POD must stay trivially destructible while both views are live");

}  // namespace IO

// ===========================================================================
// ENDGAME B.3.4 block 4 (STREAMS) — `IO::CompressedFileStream`.
// ===========================================================================
// It lives in THIS header rather than beside its base in Stream.h for one
// reason: it holds an `IO::BinaryFileStream` by value, and that class is
// declared here. Stream.h cannot include FileStream.h (FileStream.h already
// includes Stream.h), so the class follows its member.
//
// Section D binds it: this is the class `Object::External::Load` stack-builds,
// and it sits directly on the intermittent compressed-load defect. The one
// body that moved (slot 7, `Close`) moved byte-faithfully.

namespace IO {

// ---------------------------------------------------------------------------
// IO::CompressedFileStream — vtable 0x1801555f0, 9 slots, 0xb8 bytes.
// ---------------------------------------------------------------------------
// THE SAME SHAPE ONE CLASS OVER: a CompressedStream whose `_source` is a
// concrete stream embedded in the same object, here a `BinaryFileStream` at
// +0x68 (0x68 + 0x50 == 0xb8, the measured stride, no padding).
//
// IT OVERRIDES EXACTLY ONE SLOT. Slot 7 is its own `Close` (0x1800a9490);
// slot 8 `PreCommit` is CompressedStream's (0x1800aa8c0), inherited. That
// asymmetry with CompressedMemoryStream, which overrides both, is retail's.
//
// ENDGAME B.3.9 IO: CONCRETE. Retail inlines this constructor into
// `External::Load` (our src/engine/object/external_load.cpp, the section D
// loader path), which now builds the class with it on stack storage and ends
// it with a qualified `T::~T()` at the same position.
class CompressedFileStream : public CompressedStream {
public:
    // ??0CompressedFileStream@IO@@QEAA@XZ 0x1800a93b0: ??0CompressedStream,
    // the vptr, ??0BinaryFileStream on +0x68. `_source` is not set.
    CompressedFileStream() : CompressedStream(), _file() {}

    // slot 0  0x180024100 — MSVC's vector deleting dtor, stride 0xb8.
    // ??1CompressedFileStream@IO@@UEAA@XZ 0x1800a9330: Commit when a header
    // was written on a WRITING stream, close the member's FileInfo if open;
    // then the member's ~BinaryFileStream and ~CompressedStream. Out of line
    // in file_stream.cpp; virtual (B3X W1).
    ~CompressedFileStream() override;
    void Close() override;                               // slot 7  0x1800a9490
    // slot 8 PreCommit is NOT overridden: 0x1801555f0[8] is 0x1800aa8c0.

    BinaryFileStream _file;    // +0x68 — a MEMBER, not a second base
};

static_assert(offsetof(CompressedFileStream, _file) == 0x68,
              "ctor 0x1800a93c8: `LEA RCX,[RBX+0x68]` before the BinaryFileStream ctor");
static_assert(sizeof(CompressedFileStream) == 0xb8,
              "0xb8 is the eh_vector_dtor element stride at 0x180024100");

static_assert(sizeof(CompressedFileStream) == sizeof(RtIo::CompressedFileStream),
              "IO::CompressedFileStream and its POD stay interchangeable");
static_assert(offsetof(CompressedFileStream, _file) ==
                  offsetof(RtIo::CompressedFileStream, file), "alias member");
// B.4 Z LAST IO: the RtIo POD view holds a FileInfo, which has real special
// members now; the view is never built or destroyed by value (grepped), so
// its old "trivially destructible" pin is gone.

}  // namespace IO
