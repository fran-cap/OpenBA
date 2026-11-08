#pragma once
// ============================================================================
// src/engine/io/Stream.h — IO::Stream / MemoryStream / CompressedStream /
//                             CompressedMemoryStream   (Wave 4, lane B)
// ============================================================================
//
// The four layouts the IO stream family needs. Every offset below was read out
// of a decompiled Runtime-x64.dll body and the body is named next to it; where
// the retail build inlines something, Runtime-x64-profile.dll is quoted as the
// independent witness.
//
// ORIGINAL SOURCE FILES (profile-build strings, docs/runtime_port/
// original_source_tree.txt): there is no IO\Stream.cpp and no
// IO\CompressedMemoryStream.cpp. The recovered tree has
//     IO\MemoryStream.cpp        — Stream, MemoryStream
//     IO\CompressedStream.cpp    — CompressedStream, CompressedMemoryStream,
//                                  CompressedFileStream, CompressedCopyFileStream
// so the bodies live in src/engine/io/compressedstream.cpp (scaffolded as
// stream.cpp, renamed by the orchestrator: the recovered tree has a separate
// IO\Stream.cpp for the IO::Stream base and the old name would have collided
// with it). Recorded here because
// src/engine/io/README.md otherwise reads as a mirror of the original tree.
//
// LANE BOUNDARY: IO::Encoding and IO::Serializer belong to lane A
// (src/engine/io/Encoding.h, Serializer.h). This header only ever treats an
// Encoding as an opaque void*, so the two headers cannot disagree.
//
// PORTABILITY: no <windows.h> (PLAN.md §4.1 decision 1).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>   // B.3.4 block 4: the "the PODs stay trivial" pins
#include <new>           // ENDGAME B.3.9 IO: MemoryStream places its encoding

// ENDGAME B.3.4 block 4 (STREAMS). `IO::Stream` carries an `IO::Encoding*` at
// +0x10 and `IO::Stream::GetSource()` returns a `System::String` by value, so
// the two class headers come in at GLOBAL scope, above every `namespace`
// below (B31 hygiene, B34_BRIEFS section A.8). Serializer.h does NOT include
// this header back — it keeps its `class Stream;` forward declaration, which
// is what keeps the pair acyclic.
#include "Serializer.h"
#include "../system/String.h"

namespace System { class Compression; }   // system/Compression.h: CompressedStream owns one

namespace RtIo {

// ============================================================
// IO::Stream — 0x18 bytes, abstract base.
//
//   +0x00  void*   vptr        ??_7Stream@IO@@6B@   = 0x1801549a0 (EXPORTED)
//   +0x08  int32   _direction  0 == reading, 1 == writing
//   +0x10  void*   _encoding   IO::Encoding*, owned
//
// Pinned by ??0Stream@IO@@QEAA@XZ (0x1800b99d0), whose whole body is
//   vptr = &Stream::_vftable_;  *(dword*)(this+8) = 0;  *(qword*)(this+0x10) = 0;
//
// _direction: re-verified here rather than taken on trust from Wave 3 lane B.
// ?Create@CompressedStream@IO@@QEAA_NAEAVStream@2@H@Z (0x1800aa4b0) opens with
// *(dword*)(this+8) = 1 and ?Open@CompressedStream (0x1800aa560) with
// *(dword*)(this+8) = 0. Create is the WRITE path, Open the READ path, so
// 0 == reading stands.
//
// _encoding: a heap object the stream owns. Every concrete ctor allocates it
// (Allocate(8,4), then stores the shared Encoding vtable 0x1801554a8 into it)
// and every dtor frees it. So a Stream's encoding pointer is never shared and
// never null in a live object.
//
// VTABLE (0x1801549a0, EXPORTED as ??_7Stream@IO@@6B@) — 7 usable slots, read
// byte-wise from .rdata; slot 7 is a null qword and 0x1801549c0 starts the next
// class's table:
//   [0] +0x00  0x18000c610  deleting destructor thunk
//   [1] +0x08  0x18000c6a0  GetSource
//   [2] +0x10  0x18007ec28  Write         (_purecall)
//   [3] +0x18  0x18007ec28  Read          (_purecall)
//   [4] +0x20  0x18007ec28  SetCurrentPos (_purecall)
//   [5] +0x28  0x18007ec28  GetCurrentPos (_purecall)
//   [6] +0x30  0x18007ec28  GetSize       (_purecall)
// Derived classes add Close at +0x38 and PreCommit at +0x40; see the two tables
// below, both of which have a real body in those slots.
// ============================================================
struct Stream {
    const void* vptr;        // +0x00
    int32_t     direction;   // +0x08   0 = reading, 1 = writing
    uint32_t    pad0c;       // +0x0c   never written by any ctor
    void*       encoding;    // +0x10   IO::Encoding* (lane A owns the type)
};
static_assert(offsetof(RtIo::Stream, vptr) == 0x0, "Stream::vptr");
static_assert(offsetof(RtIo::Stream, pad0c) == 0xC, "Stream::pad0c");
static_assert(offsetof(Stream, direction) == 0x08, "Stream.direction @ 0x08");
static_assert(offsetof(Stream, encoding)  == 0x10, "Stream.encoding @ 0x10");
static_assert(sizeof(Stream) == 0x18, "sizeof(IO::Stream) == 0x18");

// ============================================================
// IO::MemoryStream — 0x30 bytes, Stream over a flat buffer.
//
//   +0x00..0x17  Stream
//   +0x18  void*    _data     the buffer
//   +0x20  bool     _ownsData free _data in the dtor
//   +0x24  uint32   _cursor   read/write position IN THE BUFFER
//   +0x28  uint32   _size     bytes valid in _data
//
// Pinned by ??0MemoryStream@IO@@QEAA@XZ (0x1800ae710): after the Stream ctor it
// writes qword 0 at +0x18, byte 0 at +0x20 and qword 0 at +0x24 (one store
// covering _cursor and _size), then installs its own vtable and its Encoding.
//
// _ownsData / _data / _encoding are confirmed a second time by
// ??1MemoryStream@IO@@UEAA@XZ (0x1800ae890):
//   vptr = &MemoryStream::_vftable_;
//   if (*(bool*)(this+0x20) && *(void**)(this+0x18)) MemFree(*(void**)(this+0x18));
//   vptr = &Stream::_vftable_;
//   if (*(void**)(this+0x10)) MemFree(*(void**)(this+0x10));
// and _cursor at +0x24 by ?Read@MemoryStream@IO@@UEAA_NHPEAX@Z (0x1800ae950),
// which is where engine/types/io_serializer.h already had it.
//
// SIZE. 0x30 is the stride implied by CompressedMemoryStream: its MemoryStream
// sub-object starts at +0x68 and sizeof(CompressedMemoryStream) is 0x98
// (below), leaving exactly 0x30. Nothing writes past +0x2b.
//
// VTABLE (0x1801555b0, EXPORTED as ??_7MemoryStream@IO@@6B@) — 8 slots:
//   [0] 0x1800242a0 deleting dtor   [4] 0x1800ae9b0 SetCurrentPos
//   [1] 0x18000c6a0 GetSource       [5] 0x1800ae9c0 GetCurrentPos
//   [2] 0x1800ae8f0 Write           [6] 0x1800ae9d0 GetSize
//   [3] 0x1800ae950 Read            [7] 0x1800ae9e0 Close   <-- +0x38
// MemoryStream does NOT add PreCommit; 0x1801555f0 is CompressedFileStream's
// table. The +0x38 == Close identity is what the ported dtor and Close call.
// ============================================================
struct MemoryStream {
    Stream   base;        // +0x00
    void*    data;        // +0x18
    uint8_t  ownsData;    // +0x20
    uint8_t  pad21[3];
    uint32_t cursor;      // +0x24
    uint32_t size;        // +0x28
    uint32_t pad2c;
};
static_assert(offsetof(RtIo::MemoryStream, base) == 0x0, "MemoryStream::base");
static_assert(offsetof(RtIo::MemoryStream, pad21) == 0x21, "MemoryStream::pad21");
static_assert(offsetof(RtIo::MemoryStream, pad2c) == 0x2C, "MemoryStream::pad2c");
static_assert(offsetof(MemoryStream, data)     == 0x18, "MemoryStream.data @ 0x18");
static_assert(offsetof(MemoryStream, ownsData) == 0x20, "MemoryStream.ownsData @ 0x20");
static_assert(offsetof(MemoryStream, cursor)   == 0x24, "MemoryStream.cursor @ 0x24");
static_assert(offsetof(MemoryStream, size)     == 0x28, "MemoryStream.size @ 0x28");
static_assert(sizeof(MemoryStream) == 0x30, "sizeof(IO::MemoryStream) == 0x30");

// ============================================================
// IO::CompressedStream — 0x68 bytes. A Stream that codes a windowed,
// block-compressed byte stream onto ANOTHER Stream (_source).
//
//   +0x00..0x17  Stream
//   +0x18  uint32   (never written by the ctor — unknown, left as padding)
//   +0x1c  int32    _position       logical position in the DECOMPRESSED stream
//   +0x20  int32    _activeWindow   ctor writes -1; SetWindowCount overwrites
//                                   it with GetAvailableWindow()
//   +0x24  uint32   (never written by the ctor — unknown)
//   +0x28  Stream*  _source         the stream the compressed bytes live on
//   +0x30  Array<Window>   {data +0x30, size +0x38, cap +0x3c}  0x18-byte elems
//   +0x40  Array<Header>   {data +0x40, size +0x48, cap +0x4c}  8-byte elems
//   +0x50  Array<void*>    {data +0x50, size +0x58, cap +0x5c}  8-byte elems
//   +0x60  void*    _compression    System::Compression*, owned
//
// Pinned by ??0CompressedStream@IO@@QEAA@XZ (0x1800aa3d0), disassembled:
//   MOV [RCX+0x1c],EBP(=0) / MOV dword [RCX+0x20],0xffffffff / eight qword
//   zero stores at +0x28,0x30,0x38,0x40,0x48,0x50,0x58,0x60.
// +0x18 and +0x24 are the two gaps that leaves; NOTHING in this wave writes or
// reads them and they are recorded as unknown rather than guessed.
//
// _source at +0x28 and _compression at +0x60: ?Create@CompressedStream
// (0x1800aa4b0) is  direction=1; _source=arg; _compression=new Compression;
// SetWindowCount(arg2); return true.
//
// The three Arrays follow the Wave 2 Collection layout ({_data, _size:i32,
// _capacity:i32}, WAVE2_COLLECTION.md) and each is confirmed by an indexed walk
// in ?Close@CompressedStream@IO@@UEAAXXZ (0x1800aa8d0): it strides +0x30 by
// 0x18 bounded by *(int*)(this+0x38), and strides +0x40 and +0x50 by 8 bounded
// by *(int*)(this+0x48).
//
// +0x48 IS THE FIELD THE PORTED DTOR AND Close GUARD ON. *(int*)(this+0x48) is
// the header array's element COUNT, i.e. "has anything been written yet".
//
// VTABLE 0x180155640, EXPORTED as ??_7CompressedStream@IO@@6B@ (ordinal 1410).
// Not used by this lane; CompressedMemoryStream overrides both of the slots
// that would matter.
// ============================================================
struct CompressedStream {
    Stream   base;            // +0x00
    uint32_t copiedUnwrittenWord;       // +0x18  never initialised by the ctor
    int32_t  position;        // +0x1c
    int32_t  activeWindow;    // +0x20  ctor: -1
    uint32_t pad24;       // +0x24  never initialised by the ctor
    Stream*  source;          // +0x28
    void*    windowData;      // +0x30  Array<Window>::_data
    int32_t  windowSize;      // +0x38  Array<Window>::_size
    int32_t  windowCap;       // +0x3c
    void*    headerData;      // +0x40  Array<Header>::_data
    int32_t  headerSize;      // +0x48  Array<Header>::_size   <-- the guard
    int32_t  headerCap;       // +0x4c
    void*    blockData;       // +0x50
    int32_t  blockSize;       // +0x58
    int32_t  blockCap;        // +0x5c
    void*    compression;     // +0x60  System::Compression*, owned
};
static_assert(offsetof(RtIo::CompressedStream, base) == 0x0, "CompressedStream::base");
static_assert(offsetof(RtIo::CompressedStream, copiedUnwrittenWord) == 0x18, "CompressedStream::copiedUnwrittenWord");
static_assert(offsetof(RtIo::CompressedStream, pad24) == 0x24, "CompressedStream::pad24");
static_assert(offsetof(RtIo::CompressedStream, windowCap) == 0x3C, "CompressedStream::windowCap");
static_assert(offsetof(RtIo::CompressedStream, headerCap) == 0x4C, "CompressedStream::headerCap");
static_assert(offsetof(RtIo::CompressedStream, blockSize) == 0x58, "CompressedStream::blockSize");
static_assert(offsetof(RtIo::CompressedStream, blockCap) == 0x5C, "CompressedStream::blockCap");
static_assert(offsetof(CompressedStream, position)     == 0x1c, "CompressedStream.position @ 0x1c");
static_assert(offsetof(CompressedStream, activeWindow) == 0x20, "CompressedStream.activeWindow @ 0x20");
static_assert(offsetof(CompressedStream, source)       == 0x28, "CompressedStream.source @ 0x28");
static_assert(offsetof(CompressedStream, windowData)   == 0x30, "CompressedStream.windowData @ 0x30");
static_assert(offsetof(CompressedStream, windowSize)   == 0x38, "CompressedStream.windowSize @ 0x38");
static_assert(offsetof(CompressedStream, headerData)   == 0x40, "CompressedStream.headerData @ 0x40");
static_assert(offsetof(CompressedStream, headerSize)   == 0x48, "CompressedStream.headerSize @ 0x48");
static_assert(offsetof(CompressedStream, blockData)    == 0x50, "CompressedStream.blockData @ 0x50");
static_assert(offsetof(CompressedStream, compression)  == 0x60, "CompressedStream.compression @ 0x60");
static_assert(sizeof(CompressedStream) == 0x68, "sizeof(IO::CompressedStream) == 0x68");

// ---- the two record types the +0x30 and +0x40 arrays hold -------------------
// Wave 9 lane PKG2 rung 2. Wave 4 named the ARRAYS; these are their ELEMENTS.
//
// One decompression window. Stride 24 (0x18), pinned four independent ways:
// SetWindow's `LEA RCX,[R10 + R10*0x2]` then `*8` (0x1800a9acb), Read's identical
// pair (0x1800aa7c5), GetAvailableWindow's `ADD RAX,0x18` walk (0x1800a99c2) and
// SetWindowCount's `LEA RSI,[RSI + 0x18]` (0x1800aa0bf).
struct CompressedStreamWindow {
    // +0x00 — the LRU age. Zeroed on the window that just became active and
    // incremented on every OTHER window, so the largest value is the least
    // recently used. GetAvailableWindow evicts the max.
    int32_t useCount;
    // +0x04 — first stream offset this window covers, or -1 when the window
    // holds nothing. The -1 is load-bearing in two places: it is how
    // GetAvailableWindow finds a free window, and it is what SetWindow's fast
    // path tests before trusting the active window.
    int32_t start;
    int32_t end;          // +0x08 — one past the last offset covered
    uint32_t _pad0c;      // +0x0C — never written by anything this lane read
    // +0x10 — the 0x8000-byte decompressed page. Allocated one per window by
    // SetWindowCount (`MOV EDX,0x8000 ; MOV R8D,4` at 0x1800aa07d) and memset to
    // zero immediately after. B.4 R-IO: typed as the bytes it holds.
    uint8_t* buffer;
};
static_assert(sizeof(CompressedStreamWindow) == 0x18,
              "SetWindow 0x1800a9acb: `LEA RCX,[R10+R10*2]` then scale 8 = stride 24");
static_assert(offsetof(CompressedStreamWindow, useCount) == 0x0, "CompressedStreamWindow::useCount");
static_assert(offsetof(CompressedStreamWindow, _pad0c) == 0xC, "CompressedStreamWindow::_pad0c");
static_assert(offsetof(CompressedStreamWindow, start)  == 0x04,
              "SetWindow 0x1800a9acf: `MOV R8D,[RDI + RCX*8 + 0x4]`");
static_assert(offsetof(CompressedStreamWindow, end)    == 0x08,
              "Read 0x1800aa7cd: `MOV EDX,[RAX + RCX*8 + 0x8]`");
static_assert(offsetof(CompressedStreamWindow, buffer) == 0x10,
              "SetWindowCount 0x1800aa0a4: `MOV [RSI + RCX + 0x10],RBX`");

// One compressed block. Stride 8, and the whole on-disk directory is an array of
// these. Read three ways that agree: Open's `LEA EAX,[RDX*0x8]` size check
// (0x1800aa5c4), its `ADD RAX,8` verification walk (0x1800aa61f), and
// DecompressFromSource's `LEA RDI,[RAX + RCX*0x8]` (0x1800a96ec).
//
// THE STREAM FORMAT, which falls out of ?Open@CompressedStream@IO@@ in full:
//
//     [uint32 blockCount]
//     [blockCount x CompressedStreamHeader]
//     [the blocks themselves, in order]
//
// and Open validates it by summing: `4 + blockCount*8 + sum(block sizes)` must
// equal the source's total size exactly (`SETZ AL` at 0x1800aa63b).
struct CompressedStreamHeader {
    uint32_t sourceOffset;      // +0x00 — where the block starts in the source
    // +0x04 — compressed length, and ZERO IS NOT "empty": it is the STORED
    // (uncompressed) marker. Both DecompressFromSource (0x1800a9723 `TEST DX,DX`)
    // and Open's size walk (0x1800aa613) branch on it, and Open's walk falls back
    // to `uncompressedSize` when it is zero — which is what proves the meaning.
    uint16_t compressedSize;
    uint16_t uncompressedSize;  // +0x06 — always the real size, <= 0x8000
};
static_assert(sizeof(CompressedStreamHeader) == 8,
              "Open 0x1800aa5c4: `LEA EAX,[RDX*0x8]` sizes the directory");
static_assert(offsetof(CompressedStreamHeader, sourceOffset) == 0x0, "CompressedStreamHeader::sourceOffset");
static_assert(offsetof(CompressedStreamHeader, compressedSize) == 4,
              "DecompressFromSource 0x1800a9718: `MOVZX EDX,word ptr [RDI + 0x4]`");
static_assert(offsetof(CompressedStreamHeader, uncompressedSize) == 6,
              "DecompressFromSource 0x1800a9703: `MOVZX EAX,word ptr [RDI + 0x6]`");

// The window size, and the reason every `>> 15` and `<< 15` in this family is
// the same constant. Blocks are a fixed 32 KB of DECOMPRESSED data, so the block
// index for a stream offset is `offset >> 15` (SetWindow 0x1800a9b7d,
// `SAR EDX,0xf`) and a block's first offset is `index << 15`.
constexpr int32_t kCompressedStreamWindowSize = 0x8000;

// ============================================================
// IO::CompressedMemoryStream — 0x98 bytes.
//
//   +0x00..0x67  CompressedStream base
//   +0x68..0x97  MemoryStream _memoryStream  (a MEMBER, not a second base)
//
// SIZE IS MEASURED, NOT INFERRED: the class's deleting-destructor thunk
// (0x1800244a0, vtable slot 0) calls
//     eh_vector_destructor_iterator(p, 0x98, count, ~CompressedMemoryStream)
// and 0x98 is the element stride an array-delete uses, i.e. sizeof.
//
// The +0x68 member offset is pinned three times over:
//   ??0CompressedMemoryStream (0x1800a9520)  LEA RCX,[RBX+0x68] before the
//                                            MemoryStream ctor call
//   ?Create@... (0x1800a9600) / ?Open@... (0x1800a9500) pass this+0x68 as the
//                                            Stream& to the CompressedStream base
//   ?GetMemoryStream@... (0x180024350) is literally LEA RAX,[RCX+0x68]; RET
// and independently by the profile build (0x18005c240 / 0x18005c280), which
// uses the same +0x68 with the calls left OUT of line.
//
// So a CompressedMemoryStream is a CompressedStream whose _source is its own
// embedded MemoryStream: compressed bytes are produced into, or consumed from,
// a buffer that lives inside the same object.
//
// engine/types/io_serializer.h already carried the two derived constants
// kCompressedMemoryStream_MemBase = 0x68 and _Cursor = 0x8c. Both agree with
// this header. NOTE the two different "cursors" — +0x8c is the MemoryStream's
// position in the COMPRESSED buffer, +0x1c is the CompressedStream's position in
// the DECOMPRESSED stream. gameupdateloop_serialize.cpp uses +0x1c and
// componententity.cpp uses +0x8c; both are right for what they measure.
//
// VTABLE (0x180155560, EXPORTED as ??_7CompressedMemoryStream@IO@@6B@,
// ordinal 1409) — 9 slots:
//   [0] 0x1800244a0 deleting dtor       [5] 0x1800aa850 GetCurrentPos (base)
//   [1] 0x18000c6a0 GetSource           [6] 0x1800aa860 GetSize       (base)
//   [2] 0x1800aa6c0 Write   (base)      [7] 0x1800a9620 Close      <-- OVERRIDE
//   [3] 0x1800aa780 Read    (base)      [8] 0x1800a9660 PreCommit  <-- OVERRIDE
//   [4] 0x1800aa830 SetCurrentPos(base)
// The only code xref to ?Close@CompressedMemoryStream (0x1800a9620) anywhere in
// the DLL is slot 7 itself, at 0x180155598 = 0x180155560 + 0x38.
// ============================================================
struct CompressedMemoryStream {
    CompressedStream base;          // +0x00
    MemoryStream     memoryStream;  // +0x68
};
static_assert(offsetof(RtIo::CompressedMemoryStream, base) == 0x0, "CompressedMemoryStream::base");
static_assert(offsetof(CompressedMemoryStream, memoryStream) == 0x68,
              "CompressedMemoryStream.memoryStream @ 0x68");
static_assert(sizeof(CompressedMemoryStream) == 0x98,
              "sizeof(IO::CompressedMemoryStream) == 0x98 (eh_vector_dtor stride)");

// Stream vtable slot 7 (+0x38) is Close() on every concrete Stream in this
// header. The ported dtor and Close both dispatch the embedded MemoryStream's
// Close through it, exactly as retail does -- by name since B3X W0
// (`IO::MemoryStream::Close()`), which retired the `kStreamVtblCloseSlot`
// index constant.

}  // namespace RtIo

// ===========================================================================
// ENDGAME B.3.4 block 4 (STREAMS) — the REAL `IO::Stream` and
// `IO::MemoryStream`.
// ===========================================================================
//
// Design: B34_RULES.md section 1.3 (the slot lists), section 1.5.2 (the two
// duplicate PODs), section 4.3 (no real constructors). Bodies:
// src/engine/io/memory_stream.cpp.
//
// ---------------------------------------------------------------------------
// THERE ARE NO CONSTRUCTORS HERE, AND THAT IS THE RULE, NOT AN OMISSION
// ---------------------------------------------------------------------------
// B34_RULES section 4.3: a stream class gets real members and a real
// declaration in this step; it does NOT get a C++ constructor. Thirteen of the
// fifteen IO vtables are stamped from OUTSIDE `src/engine/io/` — by
// `Object::External`'s load / add / save-text paths, `Save`'s metadata reader,
// `UI` and `Achievement::Goal` — so every live stream object wears the MINTED
// table, and every construction site keeps stamping
// `RT_DATA_SLOT("??_7...@IO@@6B@")`. Two of those sites are stack locals in
// `src/engine/object/`, which block 4 does not own. A C++ constructor here
// would swap the table under an object built in a file we do not touch.
//
// The consequence for this header: the classes are reached ONLY through
// `reinterpret_cast` plus a QUALIFIED, non-virtual call from a registered
// door. A plain virtual call inside a door would dispatch through the minted
// table straight back into that same door — infinite recursion, not
// delegation (B34_BRIEFS section A.1). `sizeof` and `offsetof` work on an
// abstract class, so the layout pins below hold regardless.
//
// ---------------------------------------------------------------------------
// WHICH CLASSES ARE ABSTRACT, AND WHY `MemoryStream` IS NOT
// ---------------------------------------------------------------------------
// Slot 0 (`DeletingDtor`) is left PURE wherever it can be: its body is a
// registered `RT_DELDTOR` / by-address row that section 4.2 forbids retiring
// in this step, and block 2 established the shape (BinaryReader and friends
// are `final` AND abstract).
//
// `IO::MemoryStream` is the EXCEPTION, and it is forced, not chosen:
// `IO::CompressedMemoryStream` holds one BY VALUE at +0x68 (section 2 finding
// 6, pinned three ways in the banner above). A member of abstract type is
// ill-formed, so MemoryStream must be concrete — which means BOTH inherited
// pure virtuals it does not override must be real:
//
//   * slot 0 is the class's own inline vector deleting dtor (0x1800242a0;
//     ENDGAME B.3.9 IO retired the registered door it used to name).
//   * slot 1 `GetSource` is not overridden by MemoryStream at all — its table
//     carries `IO::Stream`'s own 0x18000c6a0 — so `IO::Stream::GetSource` is
//     the one base slot with a real body (see the note on it below).
//
// The by-value holder reaches the protected, non-virtual `~MemoryStream` as a
// friend (B.3.9 IO); the same holds for `IO::BinaryFileStream` inside
// `IO::CompressedFileStream`.
// ===========================================================================

namespace IO {

// ---------------------------------------------------------------------------
// IO::Stream — vtable 0x1801549a0, 7 slots, 0x18 bytes.
// ---------------------------------------------------------------------------
// The slot list is B34_RULES section 1.3: slot 0 is the deleting dtor
// (0x18000c610), slot 1 `GetSource` has a real body (0x18000c6a0) and slots
// 2..6 are `_purecall` in the shipped table — i.e. retail's own base really is
// abstract in exactly those five places.
//
// FIELD NAMES AND OFFSETS come from the `RtIo::Stream` POD above, which is
// pinned by `??0Stream@IO@@QEAA@XZ` (0x1800b99d0). Nothing moves.
class Stream {
public:
    // The retail spelling of the direction enum. ENDGAME B.3.4 callers batch H.
    // The enum itself is declared at `IO` scope in Serializer.h, because
    // `IO::Serializer::GetDirection()` must name it and Serializer.h cannot
    // include this header back without making the pair cyclic. Re-exporting it
    // as a member typedef is what keeps `IO::Stream::Direction` — the name
    // ?GetDirection@Stream@IO@@QEBA?AW4Direction@12@XZ mangles — spellable at
    // every use site.
    using Direction = ::IO::Direction;

    // ?GetDirection@Stream@IO@@QEBA?AW4Direction@12@XZ   0x180006200
    // The WHOLE shipped body is `MOV EAX,dword ptr [RCX + 0x8]; RET`. NON-
    // virtual (there is no slot for it in the 7-slot table above) and const,
    // exactly as retail's `QEBA` says. Stated inline here for the same reason
    // `GetSource` is: two transcriptions of one load is not a divergence risk.
    Direction GetDirection() const { return static_cast<Direction>(_direction); }

    // slot 0  +0x00  0x18000c610 — retail's deleting destructor. Here a PURE
    // virtual destructor (GCC: D1 + D0, two slots) with the body of
    // ??1Stream@IO@@UEAA@XZ below the class: the class stays abstract.
    virtual ~Stream() = 0;

    // Retail's slot-0 free arm: `delete` on any stream frees through
    // MemoryAllocator::MemFree (root_operator_delete.cpp; unit build: free).
    static void operator delete(void* p);

    // slot 1  +0x08  0x18000c6a0 — `String GetSource() const { return String(); }`
    //
    // The nine shipped instructions read NOTHING from `this`: RCX is never
    // touched, and the whole body is `*ret = nullptr`, i.e. a default
    // `System::String`. (ENDGAME B.3.9 IO, W2: the registered door
    // `RtIo_Stream_GetSource` went with the minted Stream row; this member is
    // the one body.)
    //
    // ⚠ NEVER VIRTUAL-CALL THIS ON AN OBJECT WEARING A MINTED TABLE. MinGW GCC
    // passes the hidden return pointer as the FIRST argument and `this` second;
    // the registered door's signature is `(self, ret)`, MSVC's order. Both are
    // self-consistent — our C++ callers reach the member, raw slot callers
    // reach the door — but a virtual call from ported C++ onto a minted vptr
    // would cross between them and write the String through `this`. No caller
    // in the tree does; this comment is so none starts.
    virtual System::String GetSource() const { return System::String(); }

    // slots 2..6 — `_purecall` in retail's own table (0x18007ec28 five times).
    virtual bool     Write(int n, const void* src) = 0;   // slot 2 +0x10
    virtual bool     Read(int n, void* dst) = 0;          // slot 3 +0x18
    virtual bool     SetCurrentPos(int pos) = 0;          // slot 4 +0x20
    virtual uint32_t GetCurrentPos() const = 0;           // slot 5 +0x28
    virtual uint32_t GetSize() const = 0;                 // slot 6 +0x30

    // ---- the 0x18 bytes, at retail's offsets -------------------------------
    // PUBLIC during the migration for the same reason `IO::Serializer`'s two
    // fields are: the unconverted tree reads these through a cast to one of the
    // two PODs, and `offsetof` on them has to be well formed for the pins.
    int32_t   _direction;   // +0x08  0 == reading/decode, non-zero == writing
    uint32_t  _pad0c;       // +0x0C  never written by any ctor
    Encoding* _encoding;    // +0x10  the encoding POLICY object, owned

protected:
    // ENDGAME B.3.9 IO -- ??0Stream@IO@@QEAA@XZ 0x1800b99d0, the base ctor
    // every concrete stream opens with: +0x08 = 0, +0x10 = null (+0x0c is not
    // written). Protected: the class is abstract.
    Stream() : _direction(0), _encoding(nullptr) {}
};

// ??1Stream@IO@@UEAA@XZ 0x1800b9a40: frees the owned encoding with a direct
// MemFree (an Encoding has no destructor slot -- Serializer.h). The body of the
// pure virtual destructor above. ENDGAME B.3.9 IO (W2): the `??1Stream` door and
// the minted Stream row are retired.
inline Stream::~Stream() {
    if (_encoding) RtSystem::RtMemFree(_encoding);
}

// NOT RETAIL (hardening, review 2026-11-03 F-079 / F-080): true when a String length
// word asks for more bytes than a BINARY-encoded reading stream still holds,
// GetSize() - GetCurrentPos(). System::String::Serialize and RtIo_SerializeStringArray
// reject such a record before they size anything from it.
//
// Only a binary encoding is tested, because only there are the size and the position
// byte counts of the payload: MemoryStream (_size / _cursor; Read fails past _size),
// FileStream (the file's or the package entry's size / cursor), CompressedStream (the
// decompressed total the block directory read at Open implies, or a larger window end;
// the logical position) and ext.stream_cache's SegmentStream (the record total, or its
// CompressedStream). Every other encoding answers false (no cap): retail's TextEncoding
// (TextFileStream, never read through: its CreateReader is stripped), VisitorEncoding
// (VisitorStream, 0 / 0) and the ext .rsc text reader's TextStream (0 / 0, its reader
// supplies the chars itself). A null encoding is not tested either.
inline bool StringLengthExceedsStream(const Stream& s, uint64_t need) {
    if (s._encoding == nullptr || !s._encoding->IsBinary())
        return false;
    const uint32_t size = s.GetSize();
    const uint32_t pos = s.GetCurrentPos();
    const uint64_t left = size > pos ? static_cast<uint64_t>(size - pos) : 0u;
    return need > left;
}

static_assert(sizeof(Stream) == 0x18, "IO::Stream is 0x18 bytes");
static_assert(offsetof(Stream, _direction) == 0x08, "??0Stream@IO@@ 0x1800b99d0: +0x08 = 0");
static_assert(offsetof(Stream, _pad0c)     == 0x0c, "the gap the ctor never writes");
static_assert(offsetof(Stream, _encoding)  == 0x10, "?SetEncoding@Stream@IO@@ writes +0x10");

// ---------------------------------------------------------------------------
// IO::MemoryStream — vtable 0x1801555b0, 8 slots, 0x30 bytes.
// ---------------------------------------------------------------------------
// Slot 7 is `Close` (0x1800ae9e0) and MemoryStream adds NO `PreCommit` — the
// Compressed branch is the one with nine slots. Note that the SAME slot 7 is
// `Open` on the FileStream branch (B34_RULES section 1.3); the two are not the
// same slot meaning and `FileStream.h` says so again from its side.
// ENDGAME B.3.9 IO: CONSTRUCTIBLE. The minted ??_7MemoryStream row is retired;
// the public constructor below is retail's and stores the compiler's table.
// The destructor is protected and virtual (B3X W1).: a stack instance
// is built with placement new into raw storage and ended by a qualified
// `T::~T()`, exactly as retail's inlined teardown frees it.
class MemoryStream : public Stream {
public:
    // ENDGAME B.3.9 IO -- ??0MemoryStream@IO@@QEAA@XZ 0x1800ae710: after the
    // Stream base, +0x18 = null, +0x20 = 0 (ONE byte), +0x24..+0x2b = 0 (one
    // qword), then MemAlloc(8,4) and the BINARY encoding (null on a failed
    // allocation), the dead free of the previous encoding, the store.
    // +0x21..0x23 and +0x2c are not written.
    MemoryStream() : _data(nullptr), _ownsData(0), _cursor(0), _size(0) {
        void* encMem = RtSystem::RtMemAlloc(8, 4);
        Encoding* enc = nullptr;
        if (encMem) enc = ::new (encMem) BinaryEncoding();
        if (_encoding) RtSystem::RtMemFree(_encoding);
        _encoding = enc;
    }

    // slot 0  +0x00  0x1800242a0 — MSVC's vector deleting dtor, stride 0x30.
    // ??1MemoryStream@IO@@UEAA@XZ 0x1800ae890: free the buffer when owned
    // (the pointer is left dangling, as in retail), then ~Stream frees the
    // encoding. Virtual (B3X W1); CompressedMemoryStream, which holds one by value
    // at +0x68, reaches it as a friend.
    ~MemoryStream() override {
        if (_ownsData && _data != nullptr) RtSystem::RtMemFree(_data);
    }
    // slot 1 is NOT overridden: 0x1801555b0[1] is IO::Stream's own 0x18000c6a0.
    bool     Write(int n, const void* src) override;   // slot 2 +0x10  0x1800ae8f0
    bool     Read(int n, void* dst) override;          // slot 3 +0x18  0x1800ae950
    bool     SetCurrentPos(int pos) override;          // slot 4 +0x20  0x1800ae9b0
    uint32_t GetCurrentPos() const override;           // slot 5 +0x28  0x1800ae9c0
    uint32_t GetSize() const override;                 // slot 6 +0x30  0x1800ae9d0
    // slot 7 +0x38 — MemoryStream's own first virtual. A BARE RET in retail.
    virtual void Close();                              //              0x1800ae9e0

    // ENDGAME B.3.9 IO (W2) -- the two non-virtual buffer openers, members
    // now (their by-name doors are gone). Out of line in compressedstream.cpp.
    // ?Open@MemoryStream@IO@@QEAA_NH@Z   0x1800ae670: READING (direction 0).
    bool Open(int size);
    // ?Create@MemoryStream@IO@@QEAA_NH@Z 0x1800ae7f0: WRITING (direction 1).
    bool Create(int size);

    void*    _data;        // +0x18  the buffer
    uint8_t  _ownsData;    // +0x20  free _data in the dtor
    uint8_t  _pad21[3];    // +0x21
    uint32_t _cursor;      // +0x24  position IN THE BUFFER
    uint32_t _size;        // +0x28  bytes valid in _data
    uint32_t _pad2c;       // +0x2C

protected:
    friend class CompressedMemoryStream;   // the by-value holder, +0x68
};
static_assert(offsetof(IO::MemoryStream, _pad21) == 0x21, "MemoryStream::_pad21");
static_assert(offsetof(IO::MemoryStream, _pad2c) == 0x2C, "MemoryStream::_pad2c");

static_assert(sizeof(MemoryStream) == 0x30, "IO::MemoryStream is 0x30 bytes");
static_assert(offsetof(MemoryStream, _data)     == 0x18, "MemoryStream.data @ 0x18");
static_assert(offsetof(MemoryStream, _ownsData) == 0x20, "MemoryStream.ownsData @ 0x20");
static_assert(offsetof(MemoryStream, _cursor)   == 0x24, "?Read@MemoryStream 0x1800ae95d");
static_assert(offsetof(MemoryStream, _size)     == 0x28, "?GetSize@MemoryStream 0x1800ae9d0");

// ---------------------------------------------------------------------------
// THE DUPLICATE-POD CROSS-ASSERTS (B34_RULES section 1.5.2 / 1.5.3)
// ---------------------------------------------------------------------------
// There are TWO independently declared `Stream` PODs — `RtIo::Stream` in this
// header and `rt::io::Stream` in `Serializer.h` — written either side of the
// Wave-4 lane boundary, in two namespaces, with neither header including the
// other. `IO::Stream` replaces both. They STAY, trivially destructible, until
// their last user converts (B33 section 7: giving a POD a destructor runs a
// second one — the `RtSystem::String` double-free lesson), and they are pinned
// field for field against the class here, which is what makes every
// `reinterpret_cast` between the three views honest.
//
// GREP BOTH NAMES before declaring any `Stream` caller converted.
static_assert(sizeof(Stream) == sizeof(RtIo::Stream),
              "IO::Stream and the RtIo::Stream POD stay interchangeable");
static_assert(sizeof(Stream) == sizeof(rt::io::Stream),
              "IO::Stream and the rt::io::Stream POD stay interchangeable");
static_assert(offsetof(Stream, _direction) == offsetof(RtIo::Stream, direction), "alias direction");
static_assert(offsetof(Stream, _direction) == offsetof(rt::io::Stream, direction), "alias direction");
static_assert(offsetof(Stream, _encoding)  == offsetof(RtIo::Stream, encoding),  "alias encoding");
static_assert(offsetof(Stream, _encoding)  == offsetof(rt::io::Stream, encoding), "alias encoding");
static_assert(std::is_trivially_destructible<RtIo::Stream>::value,
              "the PODs must stay trivially destructible while both views are live");
static_assert(std::is_trivially_destructible<rt::io::Stream>::value,
              "the PODs must stay trivially destructible while both views are live");

// ENDGAME B.3.4 callers batch H — the direction enum and the pre-existing
// constant are ONE fact in two spellings, so tie them together rather than
// leave a future reader to notice they agree. `rt::io::kDirectionReading` has
// call sites of its own (serializer_class.cpp's constructor picks the
// CreateReader slot with it) and STAYS; this is what stops the two drifting.
static_assert(static_cast<int32_t>(::IO::kReading) == rt::io::kDirectionReading,
              "IO::kReading and rt::io::kDirectionReading are the same retail 0");
static_assert(static_cast<int32_t>(::IO::kWriting) == 1,
              "?Create@CompressedStream@IO@@ 0x1800aa4b0 opens with *(dword*)(this+8) = 1");
static_assert(sizeof(Stream::Direction) == 4,
              "?GetDirection@Stream@IO@@ 0x180006200 returns EAX, a 4-byte enum");
static_assert(std::is_same<Stream::Direction, ::IO::Direction>::value,
              "IO::Stream::Direction must stay the IO-scope enum, not a copy");

static_assert(sizeof(MemoryStream) == sizeof(RtIo::MemoryStream),
              "IO::MemoryStream and the RtIo::MemoryStream POD stay interchangeable");
static_assert(offsetof(MemoryStream, _data) ==
                  offsetof(RtIo::MemoryStream, data), "alias data");
static_assert(offsetof(MemoryStream, _ownsData) ==
                  offsetof(RtIo::MemoryStream, ownsData), "alias ownsData");
static_assert(offsetof(MemoryStream, _cursor) ==
                  offsetof(RtIo::MemoryStream, cursor), "alias cursor");
static_assert(offsetof(MemoryStream, _size) ==
                  offsetof(RtIo::MemoryStream, size), "alias size");
static_assert(std::is_trivially_destructible<RtIo::MemoryStream>::value,
              "the POD must stay trivially destructible while both views are live");

}  // namespace IO

// ===========================================================================
// ENDGAME B.3.4 block 4 (STREAMS) — `IO::CompressedStream` and its two
// concrete subclasses.
// ===========================================================================
//
// Design: B34_RULES.md section 1.3 (the slot lists), section 4.3 (no real
// constructors — stated in full in this header's `namespace IO` banner above),
// and B34_BRIEFS.md section D, which BINDS this family in particular.
//
// ---------------------------------------------------------------------------
// SECTION D: THIS IS THE COMPRESSED-LOAD PATH, AND IT HAS A LIVE DEFECT
// ---------------------------------------------------------------------------
// A pre-existing, intermittent defect lives on this path: roughly two boots in
// six load a resource as NULL, and one of those has been seen to hang in
// teardown with the loader thread still executing inside
// `CompressedStream::Read` 45 seconds after Quit — a spin, measured on an
// UNCHANGED tree. A separate agent is root-causing it.
//
// So the bodies below moved onto these members **byte-faithfully**: no added
// lock, no retry, no loop bound, no null check, no reordering — and equally no
// SILENT REMOVAL of whatever makes the read spin. If a conversion here made the
// symptom stop reproducing that would be a RED FLAG to report, not a win: it
// would mean the conversion changed timing or control flow somewhere nobody
// intended.
//
// ---------------------------------------------------------------------------
// NINE SLOTS: THIS BRANCH ADDS `Close` AT 7 AND `PreCommit` AT 8
// ---------------------------------------------------------------------------
// `IO::FileStream` puts `Open` at slot 7 and stops at eight slots. This branch
// puts `Close` at 7 and adds a ninth, `PreCommit`. Same offsets, different
// meanings, and FileStream.h says the same thing from its side.
//
// ---------------------------------------------------------------------------
// THE NON-VIRTUAL MEMBERS (B.4 R-IO)
// ---------------------------------------------------------------------------
// `Open`, `Create`, `Release`, `Commit`, `CompressWindow`, `SetWindow`,
// `SetWindowCount`, `GetAvailableWindow` and `DecompressFromSource` were free
// `RtIo_CompressedStream_*` doors on the RtIo POD view; they are members now,
// each body moved statement for statement (section D: no lock, retry, loop
// bound, null check or reorder added, and nothing removed), with `self->x`
// become the member's own field and the three arrays and the Compression
// pointer typed.
// ===========================================================================

namespace IO {

// ---------------------------------------------------------------------------
// IO::CompressedStream — vtable 0x180155640, 9 slots, 0x68 bytes.
// ---------------------------------------------------------------------------
// A Stream that codes a windowed, block-compressed byte stream onto ANOTHER
// Stream (`_source`). ABSTRACT: slot 0 (0x180023fd0) is an `RT_DELDTOR` row
// section 4.2 keeps, and nothing holds a `CompressedStream` by value.
//
// Slot 1 is NOT overridden — 0x180155640[1] is `IO::Stream`'s own 0x18000c6a0.
class CompressedStream : public Stream {
public:
    // slot 0 +0x00  0x180023fd0. ??1CompressedStream@IO@@UEAA@XZ 0x1800aa650
    // without its two restamps and the encoding free (that is ~Stream): Release,
    // then the three Array destructors (+0x50, +0x40, +0x30). PURE (the class
    // is abstract); the body is out of line in compressed_stream_release.cpp.
    ~CompressedStream() override = 0;
    bool     Write(int n, const void* src) override;     // slot 2 +0x10  0x1800aa6c0
    bool     Read(int n, void* dst) override;            // slot 3 +0x18  0x1800aa780
    bool     SetCurrentPos(int pos) override;            // slot 4 +0x20  0x1800aa830
    uint32_t GetCurrentPos() const override;             // slot 5 +0x28  0x1800aa850
    uint32_t GetSize() const override;                   // slot 6 +0x30  0x1800aa860
    virtual void Close();                                // slot 7 +0x38  0x1800aa8d0
    virtual void PreCommit();                            // slot 8 +0x40  0x1800aa8c0

    // ---- the non-virtual members (see the banner) -------------------------
    // ?Open@CompressedStream@IO@@QEAA_NAEAVStream@2@H@Z          0x1800aa560
    bool Open(Stream& src, int windowCount);
    // ?Create@CompressedStream@IO@@QEAA_NAEAVStream@2@H@Z        0x1800aa4b0
    bool Create(Stream& src, int windows);
    // ?Commit@CompressedStream@IO@@QEAAXXZ                       0x1800a9ee0
    void Commit();
    // ?Release@CompressedStream@IO@@AEAAXXZ                      0x1800aa100
    void Release();
    // ?CompressWindow@CompressedStream@IO@@AEAAXH@Z              0x1800a9810
    void CompressWindow(int i);
    // ?SetWindowCount@CompressedStream@IO@@AEAAXH@Z              0x1800aa020
    void SetWindowCount(int n);
    // ?SetWindow@CompressedStream@IO@@AEAA_NH@Z                  0x1800a9aa0
    bool SetWindow(int pos);
    // ?GetAvailableWindow@CompressedStream@IO@@AEAAHXZ           0x1800a9980
    int  GetAvailableWindow();
    // ?DecompressFromSource@CompressedStream@IO@@AEAA_NH@Z       0x1800a96b0
    bool DecompressFromSource(int blk);

    // NOT RETAIL (exact perf body, issue #13): `count` back-to-back
    // `Read(elemSize, dst + i * elemSize)` calls with their results ignored --
    // what IO::BinaryReader's array slots do one element at a time -- with the
    // elements that lie wholly inside the active window copied in one memcpy.
    // The proof is at the body (compressed_stream_read.cpp).
    void ReadArray(int count, int elemSize, void* dst);

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVE.md): ext.async_save_compress's hook (DESIGN.md §3 hook rule 5).
    // Null by default; src/ext/async_save_compress sets it while the class A extension
    // ext.async_save_compress has a save attached (Begin, right after Create) and clears it
    // when that save ends (point kExtAsyncEnd). It returns false for any stream other than the
    // attached one. Three points, the same single-pointer shape as Engine::MainLoop::_extFrame:
    //   kExtAsyncCompressWindow  CompressWindow(index): true = the window was queued for the
    //                            worker (retail's Compress, same order, same Compression object);
    //   kExtAsyncCopyQueued      SetWindow's in-memory branch, `index` = the block, `dst` = the
    //                            window buffer: true = filled from a still-queued job's bytes;
    //   kExtAsyncEnd             Commit (after its flush, before PreCommit) and Release: every
    //                            queued block stored, then detach.
    // Null: CompressWindow, SetWindow, Commit and Release are retail's bodies plus one null test.
    static constexpr int32_t kExtAsyncCompressWindow = 0;
    static constexpr int32_t kExtAsyncCopyQueued     = 1;
    static constexpr int32_t kExtAsyncEnd            = 2;
    using ExtAsyncCompressFn = bool (*)(CompressedStream& cs, int32_t point, int32_t index, uint8_t* dst);
    static ExtAsyncCompressFn _extAsyncCompress;   // defined in compressed_stream_write.cpp

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/DECODEAHEAD.md): ext.decode_ahead's hook (DESIGN.md
    // §3 hook rule 5). Null by default; src/ext/decode_ahead sets it once at boot while the class A
    // row is on. Two points, the same single-pointer shape as _extAsyncCompress:
    //   kExtDecodeBlock    DecompressFromSource(index), after the window's start / end are set,
    //                      `dst` = the window buffer: true = the window holds the block, decoded by a
    //                      worker from the same bytes, and the source is where retail's read leaves it;
    //   kExtDecodeRelease  Release: the stream's queued blocks are dropped, running ones waited for.
    // Null: DecompressFromSource and Release are retail's bodies plus one null test.
    static constexpr int32_t kExtDecodeBlock   = 0;
    static constexpr int32_t kExtDecodeRelease = 1;
    using ExtDecodeAheadFn = bool (*)(CompressedStream& cs, int32_t point, int32_t index, uint8_t* dst);
    static ExtDecodeAheadFn _extDecodeAhead;       // defined in compressed_stream_read.cpp
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/STREAMCACHE.md, DESIGN.md hook rule 5):
    // ext.stream_cache's read tap. Null by default and under the faithful profile. Read and
    // ReadArray's batched copy call it after the copy with the position it started at, the byte
    // count, the destination and whether the read succeeded; the extension records what a load
    // consumed. It changes nothing in the stream.
    using ExtReadTapFn = void (*)(CompressedStream& cs, int32_t pos, int32_t n, const void* dst, bool ok);
    static ExtReadTapFn _extReadTap;               // defined in compressed_stream_read.cpp

    // ---- the 0x68 bytes, at retail's offsets -------------------------------
    // `_copiedUnwrittenWord` and `_pad24` are the two gaps `??0CompressedStream`
    // (0x1800aa3d0) leaves unwritten. They are recorded as unknown rather than
    // guessed, and zeroing them would be a behavioural difference from retail.
    uint32_t _copiedUnwrittenWord;      // +0x18  never initialised by the ctor
    int32_t  _position;       // +0x1c  logical position in the DECOMPRESSED stream
    int32_t  _activeWindow;   // +0x20  ctor writes -1
    uint32_t _pad24;      // +0x24  never initialised by the ctor
    Stream*  _source;         // +0x28  the stream the compressed bytes live on
    RtIo::CompressedStreamWindow* _windowData;   // +0x30  Array<Window>::_data
    int32_t  _windowSize;     // +0x38  Array<Window>::_size
    int32_t  _windowCap;      // +0x3c
    RtIo::CompressedStreamHeader* _headerData;   // +0x40  Array<Header>::_data
    int32_t  _headerSize;     // +0x48  Array<Header>::_size   <-- the dtor's guard
    int32_t  _headerCap;      // +0x4c
    void**   _blockData;      // +0x50  Array<void*>::_data, one buffer per block
    int32_t  _blockSize;      // +0x58
    int32_t  _blockCap;       // +0x5c
    System::Compression* _compression;   // +0x60  owned

protected:
    // ENDGAME B.3.9 IO -- ??0CompressedStream@IO@@QEAA@XZ 0x1800aa3d0, the
    // base ctor CompressedFileStream's opens with: after the Stream base,
    // +0x1c = 0, +0x20 = -1, +0x28..+0x67 = 0 (eight qwords), then
    // MemAlloc(8,4) + the BINARY encoding, the dead free of the previous one,
    // the store. +0x18 and +0x24 are not written. Protected: abstract class.
    CompressedStream()
        : _position(0), _activeWindow(-1), _source(nullptr),
          _windowData(nullptr), _windowSize(0), _windowCap(0),
          _headerData(nullptr), _headerSize(0), _headerCap(0),
          _blockData(nullptr), _blockSize(0), _blockCap(0),
          _compression(nullptr) {
        void* encMem = RtSystem::RtMemAlloc(8, 4);
        Encoding* enc = nullptr;
        if (encMem) enc = ::new (encMem) BinaryEncoding();
        if (_encoding) RtSystem::RtMemFree(_encoding);
        _encoding = enc;
    }
};
static_assert(offsetof(IO::CompressedStream, _copiedUnwrittenWord) == 0x18, "CompressedStream::_copiedUnwrittenWord");
static_assert(offsetof(IO::CompressedStream, _pad24) == 0x24, "CompressedStream::_pad24");
static_assert(offsetof(IO::CompressedStream, _windowCap) == 0x3C, "CompressedStream::_windowCap");
static_assert(offsetof(IO::CompressedStream, _headerCap) == 0x4C, "CompressedStream::_headerCap");
static_assert(offsetof(IO::CompressedStream, _blockSize) == 0x58, "CompressedStream::_blockSize");
static_assert(offsetof(IO::CompressedStream, _blockCap) == 0x5C, "CompressedStream::_blockCap");

static_assert(sizeof(CompressedStream) == 0x68, "IO::CompressedStream is 0x68 bytes");
static_assert(offsetof(CompressedStream, _position)     == 0x1c, "+0x1c");
static_assert(offsetof(CompressedStream, _activeWindow) == 0x20, "ctor: -1");
static_assert(offsetof(CompressedStream, _source)       == 0x28, "?Create@ stores the arg");
static_assert(offsetof(CompressedStream, _windowData)   == 0x30, "Array<Window>::_data");
static_assert(offsetof(CompressedStream, _windowSize)   == 0x38, "?Close@ strides on it");
static_assert(offsetof(CompressedStream, _headerData)   == 0x40, "Array<Header>::_data");
static_assert(offsetof(CompressedStream, _headerSize)   == 0x48, "the guard field");
static_assert(offsetof(CompressedStream, _blockData)    == 0x50, "Array<void*>::_data");
static_assert(offsetof(CompressedStream, _compression)  == 0x60, "System::Compression*");

// ---------------------------------------------------------------------------
// IO::CompressedMemoryStream — vtable 0x180155560, 9 slots, 0x98 bytes.
// ---------------------------------------------------------------------------
// A CompressedStream whose `_source` is its own embedded MemoryStream:
// compressed bytes are produced into, or consumed from, a buffer that lives
// inside the same object. The +0x68 member offset is pinned three times over
// (the ctor's `LEA RCX,[RBX+0x68]`, `?Create@`/`?Open@` passing `this+0x68` as
// the base's `Stream&`, and `?GetMemoryStream@` being literally
// `LEA RAX,[RCX+0x68]; RET`), and independently by the profile build.
//
// IT OVERRIDES EXACTLY TWO SLOTS, 7 and 8. Slots 2..6 are the base's, which is
// why a raw call on the ENCLOSING object reaches CompressedStream's Read while
// a raw call on the MEMBER at +0x68 reaches MemoryStream's — two vtables in one
// object at two different offsets, each sub-object carrying its own.
class CompressedMemoryStream : public CompressedStream {
public:
    // ENDGAME B.3.9 IO (W2) -- ??0CompressedMemoryStream@IO@@QEAA@XZ
    // 0x1800a9520: the CompressedStream base ctor (0x1800aa3d0), then this
    // class's vptr, then the embedded MemoryStream's ctor at +0x68 (0x1800ae710)
    // -- the compiler's order for a base, the vptr and one member. No store of
    // its own. CONSTRUCTIBLE: the minted ??_7CompressedMemoryStream row is
    // retired; every builder (the save path, the metadata reader, ReadPackfile,
    // the exe's SaveRestore) spells `::new (p) IO::CompressedMemoryStream()`.
    CompressedMemoryStream() {}

    // slot 0  +0x00  0x1800244a0 -- MSVC's vector deleting dtor, stride 0x98.
    // ??1CompressedMemoryStream@IO@@UEAA@XZ 0x1800a9560 without its restamps:
    // CompressedStream::Close inlined (the guard, then Commit), then the
    // member's Close dispatched VIRTUALLY (+0x38). The compiler then runs
    // ~MemoryStream on the member and ~CompressedStream on the base -- retail's
    // two remaining steps, in retail's order. Out of line in
    // compressedstream.cpp; virtual (B3X W1).
    ~CompressedMemoryStream() override;
    void Close() override;                               // slot 7  0x1800a9620
    void PreCommit() override;                           // slot 8  0x1800a9660

    // ?Open@CompressedMemoryStream@IO@@QEAA_NXZ   0x1800a9500 -- the whole body
    // is `CompressedStream::Open(_memoryStream, 1)` (reading: ONE window).
    bool Open();
    // ?Create@CompressedMemoryStream@IO@@QEAA_NXZ 0x1800a9600 -- the whole body
    // is `CompressedStream::Create(_memoryStream, 2)` (writing: TWO windows).
    // The asymmetry is retail's; do not "fix" it.
    bool Create();
    // ?GetMemoryStream@CompressedMemoryStream@IO@@QEAAAEAVMemoryStream@2@XZ
    // 0x180024350 -- `LEA RAX,[RCX+0x68]; RET`.
    MemoryStream& GetMemoryStream() { return _memoryStream; }

    // +0x68 — a MEMBER, not a second base. See B34_BRIEFS section A.3: this is
    // the by-value containment that forces `IO::MemoryStream` to be concrete,
    // and that this class must be a `friend` of to reach its protected
    // destructor (B.3.9 IO).
    MemoryStream _memoryStream;
};

static_assert(offsetof(CompressedMemoryStream, _memoryStream) == 0x68,
              "?GetMemoryStream@ 0x180024350 is `LEA RAX,[RCX+0x68]; RET`");
static_assert(sizeof(CompressedMemoryStream) == 0x98,
              "0x98 is the eh_vector_dtor element stride at 0x1800244a0");

// ---- the duplicate-POD cross-asserts --------------------------------------
static_assert(sizeof(CompressedStream) == sizeof(RtIo::CompressedStream), "POD agrees");
static_assert(sizeof(CompressedMemoryStream) == sizeof(RtIo::CompressedMemoryStream),
              "POD agrees");
static_assert(offsetof(CompressedStream, _position) ==
                  offsetof(RtIo::CompressedStream, position), "alias position");
static_assert(offsetof(CompressedStream, _source) ==
                  offsetof(RtIo::CompressedStream, source), "alias source");
static_assert(offsetof(CompressedStream, _headerSize) ==
                  offsetof(RtIo::CompressedStream, headerSize), "alias headerSize");
static_assert(offsetof(CompressedMemoryStream, _memoryStream) ==
                  offsetof(RtIo::CompressedMemoryStream, memoryStream), "alias member");
static_assert(std::is_trivially_destructible<RtIo::CompressedStream>::value,
              "the POD must stay trivially destructible while both views are live");
static_assert(std::is_trivially_destructible<RtIo::CompressedMemoryStream>::value,
              "the POD must stay trivially destructible while both views are live");

}  // namespace IO

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

// (B.4 R-IO: the four RtIo_CompressedStream_* bodies that stood here are
// IO::CompressedStream members.)

// ---- io/text_codec.cpp
namespace IO {
namespace Text {
// Retail's `IO::Text::Format` (the name is in the mangling of
// ?Open@TextFileStream@IO@@UEAA_NAEBVFilename@System@@W4Format@Text@4@@Z); the
// enumerator names are not in the binary. Values pinned by the codec's switch
// arms (EncodeOne 0x1800c9b60, DecodeOne 0x1800c9fc0) and DetectFormat's BOMs.
enum class Format : int32_t {
    Latin1  = 0,   // one byte per codepoint, cp < 0x100; DetectFormat's answer on malformed UTF-8
    Ucs2Le  = 1,   // two bytes LE, cp < 0x10000, no surrogates, no BOM (the wide strings)
    Utf8    = 2,   // UTF-8 without a BOM (DetectFormat's valid-UTF-8 answer)
    Utf8Bom = 3,   // UTF-8 with the EF BB BF mark
    Utf16Le = 4,   // UTF-16 LE, mark FF FE
    Utf16Be = 5,   // UTF-16 BE, mark FE FF (bytes swapped)
    Detect  = 6,   // detect from the data (the TextFileStream ctor's default)
};
}  // namespace Text
}  // namespace IO

extern "C" IO::Text::Format RtIo_TextCodec_DetectFormat(const uint8_t* src, int32_t len);  // 0x1800c9d60
extern "C" int32_t RtIo_TextCodec_DecodeOne(IO::Text::Format fmtEcx, const void* srcv, const void* endv, int32_t* cp, IO::Text::Format fmtArg5);  // 0x1800c9fc0
extern "C" int32_t RtIo_TextCodec_EncodeOne(int32_t cp, void* dstv, IO::Text::Format fmt);  // 0x1800c9b60
extern "C" int32_t RtIo_TextCodec_ConvertText(IO::Text::Format srcFmt, uint8_t forceDetect, const uint8_t* src, int32_t srcLen, IO::Text::Format dstFmt, int32_t, uint8_t appendTerminator, uint8_t* dst, int32_t dstCap);  // 0x1800ca260
// ==== end B3R W0-P0c ====
