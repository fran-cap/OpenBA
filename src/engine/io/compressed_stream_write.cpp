// ============================================================================
// src/engine/io/compressed_stream_write.cpp
// The WRITER half of IO::CompressedStream, plus the three vtables' remaining
// slots.
// ============================================================================
//
// Every body below was read as DISASSEMBLY from
// Runtime-x64.dll through Ghidra MCP on port 8089
// (reference_ghidra_c_output_vs_disassembly.md — the decompiled C drops
// dereferences, prints MOVSXD as a zero extension, and drops whole arguments at
// a mistyped vtable call, and three of the twelve below make a vtable call).
// Retail addresses are quoted per line, in the style the read path uses.
//
// LAYOUTS ARE NOT RE-DERIVED. src/engine/io/Stream.h pins IO::Stream (0x18),
// IO::MemoryStream (0x30), IO::CompressedStream (0x68) and
// IO::CompressedMemoryStream (0x98) field by field, and src/engine/io/
// FileStream.h pins IO::CompressedFileStream (0xb8, a CompressedStream with a
// BinaryFileStream member at +0x68). This file consumes both and adds nothing.
// That is §2's point: IO is not greenfield and the first hour is grep.
//
// ---------------------------------------------------------------------------
// 1. THE THREE VTABLES
// ---------------------------------------------------------------------------
//   ??_7CompressedStream@IO@@6B@        0x180155640   9 slots
//   ??_7CompressedMemoryStream@IO@@6B@  0x180155560   9 slots
//   ??_7CompressedFileStream@IO@@6B@    0x1801555f0   9 slots
//
// Slot 0 of each is the class's deleting dtor (0x180023fd0 / 0x1800244a0 /
// 0x180024100, in Stream.h and FileStream.h). Slot 1 is IO::Stream's GetSource,
// slot 3 (Read) is in compressed_stream_read.cpp and CompressedMemoryStream's
// Close in compressedstream.cpp; the members below fill the other slots.
//
// ---------------------------------------------------------------------------
// 2. THE BODIES THIS FILE CALLS OUTSIDE ITSELF
// ---------------------------------------------------------------------------
//   ?Create@MemoryStream@IO@@QEAA_NH@Z                 0x1800ae7f0   compressedstream.cpp
//       PreCommit@CompressedMemoryStream tail-jumps into it.
//   ?CloseFile@FileSystem@IO@@QEAAXAEAUFileInfo@12@@Z  0x1800accc0   filesystem_createfile.cpp
//       Close@CompressedFileStream tail-jumps into it.
//   ??0Compression@System@@QEAA@XZ                     0x1800c41a0   src/engine/system/compression.cpp
//       ?Create@CompressedStream constructs one.
//   ?Compress@Compression@System@@QEAAHPEBXIPEAXI@Z    0x1800c4230   src/engine/system/compression.cpp
//       ?CompressWindow calls it.
//
// ---------------------------------------------------------------------------
// 4. A CORRECTION TO src/engine/io/Stream.h's PROSE
// ---------------------------------------------------------------------------
// Stream.h says of the +0x30 / +0x40 / +0x50 arrays that each "is confirmed by
// an indexed walk in ?Close@CompressedStream@IO@@UEAAXXZ (0x1800aa8d0): it
// strides +0x30 by 0x18 bounded by *(int*)(this+0x38) ...".
//
// **0x1800aa8d0 contains no walk.** It is five instructions:
//
//     CMP dword [RCX+0x48],0x0 / JLE out / CMP dword [RCX+0x8],0x1 / JZ 0x1800a9ee0 / RET
//
// The walks Stream.h describes are real and they do pin those three arrays —
// they are in ?Release@ (0x1800aa100) and ?Commit@ (0x1800a9ee0), both of which
// Close reaches. The FACT is unaffected; the CITED ADDRESS is wrong. Recorded
// rather than silently fixed, because a header comment is where the next lane
// will look, and "prose is a hypothesis, a grep is the answer" cuts both ways.
// The layout is independently re-confirmed by Commit below, which walks all
// three from the disassembly quoted line by line.
//
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

#include "io/Stream.h"
#include "io/FileStream.h"   // B.3.4 block 4: IO::CompressedFileStream
#include "io/FileSystem.h"   //                 and RtIO::FileInfo
#include "system/String.h"
#include "system/Compression.h"
#include "system/MemoryAllocator.h"

namespace RtIo {

using Window = CompressedStreamWindow;
using Header = CompressedStreamHeader;

// Bodies this file calls that live in the sibling files are `IO::CompressedStream`
// members (B.4 R-IO); retail reaches every one with a direct rel32 CALL, so a
// direct C++ call is the faithful spelling.

namespace {

constexpr int32_t kWin = kCompressedStreamWindowSize;   // 0x8000

// A header's stored length: compressedSize, or uncompressedSize when
// compressedSize is 0. Zero is NOT "empty" — it is the STORED (uncompressed)
// marker, which Stream.h pins from DecompressFromSource and Open. Four of the
// bodies below repeat this test instruction for instruction; it is one helper
// here so a defect in it cannot be present in three places and absent in one.
inline uint32_t StoredLength(const Header& h) {
    return h.compressedSize != 0 ? h.compressedSize : h.uncompressedSize;
}

// Retail calls [RAX+0x10] (Write, slot 2) and [RAX+0x20] (SetCurrentPos,
// slot 4) on the source and [RAX+0x40] (PreCommit, slot 8) on `this`. B3X W0:
// by name on `IO::Stream` / `IO::CompressedStream`, through each object's own
// vtable as retail does.

// The two System::MemoryAllocator calls. Retail INLINES lock/Allocate/unlock in
// ?Create below (`MOV RBX,[0x1801fc718] / ADD RBX,0x640 / EnterCriticalSection
// / Allocate(_singleton,8,4) / LeaveCriticalSection`) and calls the exported
// MemAlloc directly in ?CompressWindow. Both spellings are the same function:
// System::MemoryAllocator::MemAlloc, called by name rather than pinning
// 0x1801fc718 and the +0x640 lock offset in yet another file.

}  // namespace

// ============================================================================
// ?GetSource@Stream@IO@@UEBA?AVString@System@@XZ  (0x18000c6a0) IS NOT HERE
// ============================================================================
// It is the inline virtual IO::Stream::GetSource in Stream.h (slot 1).

// ============================================================================
// ?CompressWindow@CompressedStream@IO@@AEAAXH@Z              retail 0x1800a9810
// ============================================================================
// Encode ONE resident window into its block, replacing whatever block was
// there. AEAA is PRIVATE, **not unexported** — this DLL exports its private
// members (reference_aeaa_is_private_not_unexported.md).
//
//   w = &window[i];
//   if (w->start < 0) return;                 // an empty window encodes nothing
//   blk = w->start >> 15;                     // SAR — arithmetic, signed
//   h   = &header[blk];
//   if (block[blk]) MemFree(block[blk]);      // replacing, so free the old one
//   h->uncompressedSize = (uint16)(w->end - w->start);
//   h->compressedSize   = (uint16)Compression::Compress(
//                             _compression, w->buffer, h->uncompressedSize,
//                             scratch, 0x8000);
//   if (h->compressedSize > h->uncompressedSize) h->compressedSize = 0;   // UNSIGNED
//   len = StoredLength(*h);
//   block[blk] = MemAlloc(len, 4);
//   memcpy(block[blk], h->compressedSize ? scratch : w->buffer, len);
//
// THE STORED-NOT-SMALLER RULE IS THE POINT OF THIS FUNCTION and it is why
// `compressedSize == 0` means "stored". `JBE` at 1800a98b2 is an UNSIGNED
// compare on 16-bit values; writing it signed would mis-handle a block that
// expanded past 0x7fff, which a 32 KB incompressible window does routinely.
//
// THE SCRATCH BUFFER IS 0x8000 ON THE STACK, exactly as retail allocates it
// (`MOV EAX,0x8030 ; CALL __chkstk ; SUB RSP,RAX`, then uses RSP+0x30). The
// stack depth is reproduced rather than swapped for a static, because a static
// would make two concurrent CompressedStreams share it — retail's does not, and
// System::Thread exists.
//
// NOTE the dst capacity handed to Compress is 0x8000, the same as the maximum
// input. Compress may legitimately need more; it signals that by returning a
// value larger than the input, which is exactly the case the JBE turns into
// "store it raw". So the buffer is not undersized — the protocol handles it.
}  // namespace RtIo

// EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVE.md): null by default (Stream.h).
IO::CompressedStream::ExtAsyncCompressFn IO::CompressedStream::_extAsyncCompress = nullptr;

void IO::CompressedStream::CompressWindow(int i) {
    using RtIo::Window;
    using RtIo::Header;
    using RtIo::kWin;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVE.md): ext.async_save_compress queues
    // the attached save's window for its worker.
    if (_extAsyncCompress != nullptr && _extAsyncCompress(*this, kExtAsyncCompressWindow, i, nullptr))
        return;
    Window* const w = &_windowData[i];                   // 1800a9829..31
    if (w->start < 0) {                                  // 1800a9835/39/3b  TEST/JS
        return;
    }

    uint8_t scratch[kWin];                               // 1800a9816..20  (RSP+0x30)

    const int32_t blk = w->start >> 15;                  // 1800a9841  SAR EAX,0xf
    Header* const h   = &_headerData[blk];               // 1800a9854/62/72
    void** const  blocks = _blockData;                   // 1800a986a

    if (blocks[blk] != nullptr)                          // 1800a986e/75/78
        ::System::MemoryAllocator::MemFree(
            blocks[blk]);                                // 1800a987a

    h->uncompressedSize = static_cast<uint16_t>(w->end - w->start);   // 1800a987f..98

    const int rc = _compression
        ->Compress(w->buffer, h->uncompressedSize,
            scratch, static_cast<uint32_t>(kWin));       // 1800a989d..a5
    h->compressedSize = static_cast<uint16_t>(rc);       // 1800a98aa

    if (h->compressedSize > h->uncompressedSize)         // 1800a98ae/b2  CMP/JBE (unsigned)
        h->compressedSize = 0;                           // 1800a98b4/b6

    const uint32_t len = RtIo::StoredLength(*h);         // 1800a98ba..c7
    void* const dst = ::System::MemoryAllocator::MemAlloc(len, 4);   // 1800a98ce/d3
    blocks[blk] = dst;                                   // 1800a98d8

    std::memcpy(dst,
                h->compressedSize != 0 ? scratch : w->buffer,
                len);                                    // 1800a98dc..909
}

namespace RtIo {

// ============================================================================
// ?Commit@CompressedStream@IO@@QEAAXXZ                       retail 0x1800a9ee0
// ============================================================================
// THE real compression body, and the archive writer. Five phases:
//
//   1. flush every resident window that holds anything    -> CompressWindow
//   2. virtual PreCommit()                                -> slot 8 on THIS
//   3. fill in every block's sourceOffset, running total starting at 4+n*8
//   4. rewind the source and write [count][directory][blocks]
//   5. Release(this)                                      -> frees the windows
//
// PHASE 2 IS A VIRTUAL DISPATCH ON `this` (`MOV RAX,[RBX] ; CALL [RAX+0x40]`),
// which is the ONLY reason ?PreCommit exists as a slot at all, and it is why
// a CompressedMemoryStream sizes its buffer at exactly the right moment —
// after the blocks are final, before anything is written. Reproduced as a
// vtable call, not as a direct call to the base: a direct call would silently
// break CompressedMemoryStream, and it would still pass every test built on a
// plain CompressedStream. Ghidra's C is known to DROP arguments at a mistyped
// vtable call (reference_ghidra_c_output_vs_disassembly.md), so this one came
// off the byte offset 0x40 and not off the pseudocode.
//
// PHASE 4 WRITES THROUGH THE SOURCE'S VTABLE, not through our Write: slots 4
// (SetCurrentPos) then 2 (Write) x (2 + headerSize). The count is written from
// a STACK temporary ([RSP+0x30]) — 4 bytes, by address — and the directory is
// written as one `headerSize*8` byte blob straight out of headerData, which is
// what makes CompressedStreamHeader's 8-byte stride an ON-DISK fact and not
// just an in-memory one.
}  // namespace RtIo

void IO::CompressedStream::Commit() {
    using RtIo::Window;
    using RtIo::Header;
    using RtIo::StoredLength;

    // ---- 1. flush resident windows ---------------------------------------
    if (_windowSize > 0) {                               // 1800a9ef6/f9
        Window* const w = _windowData;
        for (int32_t i = 0; i < _windowSize; ++i) {      // 1800a9f1c  reloaded each pass
            if (w[i].start >= 0)                         // 1800a9f06/0a  JL skips
                CompressWindow(i);                       // 1800a9f11
        }
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVE.md): ext.async_save_compress's
    // queued blocks land before PreCommit reads them.
    if (_extAsyncCompress != nullptr) _extAsyncCompress(*this, kExtAsyncEnd, 0, nullptr);

    // ---- 2. virtual PreCommit --------------------------------------------
    PreCommit();                                         // 1800a9f26..2c  +0x40

    // ---- 3. lay the blocks out in the file --------------------------------
    const int32_t n = _headerSize;                       // 1800a9f2f
    uint32_t off = 4 + static_cast<uint32_t>(n) * 8;     // 1800a9f35  LEA EDX,[4+RAX*8]
    if (n > 0) {                                         // 1800a9f3c/3e
        Header* const h = _headerData;
        for (int32_t i = 0; i < n; ++i) {                // 1800a9f75
            h[i].sourceOffset = off;                     // 1800a9f50
            off += StoredLength(h[i]);                   // 1800a9f54..73
        }
    }

    // ---- 4. write [count][directory][blocks] ------------------------------
    Stream* const src = _source;                         // 1800a9f7b
    src->SetCurrentPos(0);                               // 1800a9f84  +0x20

    int32_t count = _headerSize;                         // 1800a9f87/8e  ([RSP+0x30])
    src->Write(4, &count);                               // 1800a9f9f  +0x10
    src->Write(_headerSize * 8, _headerData);            // 1800a9fa2..b3

    if (_headerSize > 0) {                               // 1800a9fb6/b9
        const Header* const h = _headerData;
        void* const* const blocks = _blockData;
        for (int32_t i = 0; i < _headerSize; ++i)        // 1800a9fee
            src->Write(
                static_cast<int32_t>(StoredLength(h[i])), blocks[i]);   // 1800a9fc0..e5
    }

    // ---- 5. release the working set ---------------------------------------
    Release();                                           // 1800a9ff6
}

namespace RtIo {

// ============================================================================
// ?Create@CompressedStream@IO@@QEAA_NAEAVStream@2@H@Z        retail 0x1800aa4b0
// ============================================================================
// Open a CompressedStream for WRITING onto `src`, with `windows` windows.
//
//     direction    = 1;                       // 1 == writing (Stream.h)
//     _source      = &src;
//     _compression = new System::Compression;
//     SetWindowCount(windows);
//     return true;
//
// The `new` is retail's INLINED lock/Allocate(8,4)/unlock around
// System::MemoryAllocator's singleton followed by ??0Compression, i.e.
// `MemAlloc(8, 4)` then the ctor, with the ctor SKIPPED when the allocation
// returns null (`JZ ... XOR EAX,EAX` at 1800aa525) — so a failed allocation
// stores a NULL _compression and still returns TRUE. Faithful; not guarded.
//
// The `Stream&` parameter is a REFERENCE, so `src` is never null by the
// language's rules and retail never tests it. Phase 0a's subject is the base
// whose vtable is abstract, and nothing here dispatches on `src` — Commit does,
// later, once the object is real.
}  // namespace RtIo

bool IO::CompressedStream::Create(Stream& src, int windows) {

    _direction = 1;                                      // 1800aa4d4
    _source    = &src;                                   // 1800aa4db

    void* const p = ::System::MemoryAllocator::MemAlloc(8, 4);   // 1800aa4df..517
    _compression = (p != nullptr)                        // 1800aa522/25
        ? ::new (p) ::System::Compression()              // 1800aa52a
        : nullptr;                                       // 1800aa531  XOR EAX,EAX

    SetWindowCount(windows);                             // 1800aa53c
    return true;                                         // 1800aa541  MOV AL,0x1
}

// ============================================================================
// The `IO::Compressed*` member bodies this file owns — B.3.4 block 4 (STREAMS)
// ============================================================================
// Six of the family's nine vtable slots. B34_BRIEFS section D
// (docs/history/endgame_b/B34_BRIEFS.md) BINDS this file: nothing here may
// gain a lock, a retry, a loop bound, a null check or a reordering, and nothing
// may quietly lose one either.
//
// `namespace IO` opens after `RtIo` closes, because a member of
// `IO::X` cannot be defined inside the sibling namespace `RtIo` (section A.2).
// The file-local helpers are reached as `RtIo::StoredLength` / `RtIo::kWin` —
// anonymous-namespace members ARE reachable that way from elsewhere in the same
// translation unit, which is what lets the bodies move without moving the
// helpers.
namespace IO {

// ?GetCurrentPos@CompressedStream@IO@@UEBAIXZ                retail 0x1800aa850
//     MOV EAX,[RCX+0x1c] ; RET
// The LOGICAL position in the DECOMPRESSED stream, not the source cursor. The
// two differ and Stream.h says so explicitly (+0x1c versus the embedded
// MemoryStream's +0x8c); both are right for what they measure.
uint32_t CompressedStream::GetCurrentPos() const {
    return static_cast<uint32_t>(_position);             // 1800aa850
}

// ?SetCurrentPos@CompressedStream@IO@@UEAA_NH@Z              retail 0x1800aa830
//     SUB RSP,0x28 ; CALL 0x1800a9aa0 ; MOV AL,1 ; ADD RSP,0x28 ; RET
// A tail-ish call into SetWindow with RCX and RDX untouched, and the return
// value is DISCARDED: this returns true unconditionally even when SetWindow
// failed. That is retail's behaviour and it is reproduced, not corrected.
bool CompressedStream::SetCurrentPos(int pos) {
    SetWindow(pos);                                      // 1800aa834
    return true;                                         // 1800aa839  MOV AL,0x1
}

// ?GetSize@CompressedStream@IO@@UEBAIXZ                      retail 0x1800aa860
// max( the furthest window END currently in memory,
//      the length the committed block directory implies )
//
// Two details that a paraphrase loses and the bytes do not:
//
//  1. `MOVSXD RCX,[RCX+0x38] ; TEST ECX,ECX ; JZ out` — a windowSize of EXACTLY
//     zero returns 0 and NEVER reaches the header half. A NEGATIVE windowSize
//     (`JLE` at 1800aa870) skips only the loop and does reach it. Those are
//     different exits and they are kept different here.
//  2. The directory length is `((headerSize-1) << 15) + lastUncompressedSize`,
//     spelled by retail as `(headerSize<<15) - 0x8000 + last`. Same value; the
//     shift is on the SIGNED count, which is why headerSize is read MOVSXD.
uint32_t CompressedStream::GetSize() const {

    const CompressedStream* const s = this;
    const int32_t nWin = s->_windowSize;                 // 1800aa863
    int32_t best = 0;                                    // 1800aa867  XOR EAX,EAX

    if (nWin == 0) {                                     // 1800aa869/6b  JZ -> RET
        return 0;
    }
    if (nWin > 0) {                                      // 1800aa870  JLE skips the loop
        const RtIo::CompressedStreamWindow* const w = s->_windowData;
        for (int32_t i = 0; i < nWin; ++i)               // 1800aa880
            if (best <= w[i].end) best = w[i].end;       // CMP/CMOVLE — <=, not <
    }

    const int32_t nHdr = s->_headerSize;                 // 1800aa88e
    if (nHdr != 0) {                                     // 1800aa892/95  TEST/JZ
        const RtIo::CompressedStreamHeader* const h = s->_headerData;
        const int32_t last = h[nHdr - 1].uncompressedSize;   // 1800aa89b  [RCX+R10*8-2]
        // NOT RETAIL in spelling only (review F-139, signed overflow): retail's SHL / LEA /
        // ADD are 32-bit and wrap for a directory of 65536 blocks or more; the same
        // arithmetic in int is undefined, so it runs in uint32_t. Same result bits.
        int32_t total = static_cast<int32_t>(
            (static_cast<uint32_t>(nHdr) << 15) -
            static_cast<uint32_t>(RtIo::kCompressedStreamWindowSize) +
            static_cast<uint32_t>(last));                    // 1800aa8a1/a5/ac
        if (total < best) total = best;                      // 1800aa8af/b1  CMOVG
        best = total;
    }
    return static_cast<uint32_t>(best);
}

// ?Write@CompressedStream@IO@@UEAA_NHPEBX@Z                  retail 0x1800aa6c0
// The mirror of ?Read: page the window that covers `position` in, copy as much
// of the caller's buffer as fits inside THAT window, extend the window's high
// water mark, repeat.
//
// THREE THINGS THE DISASSEMBLY SAYS AND A SUMMARY WOULD NOT:
//
//  * The clamp is against `w->start + 0x8000`, the window's CAPACITY, not
//    against `w->end`, its current extent. Writing past `end` is the normal
//    case — that is what growing the stream IS. (Read clamps against `end`,
//    because reading past it is not.)
//  * SetWindow's bool is discarded here too (there is no TEST after the CALL at
//    1800aa6f6), so a failed page-in silently writes into whatever window was
//    active. Faithful; NOT guarded — CLAUDE.md's "don't fix a faithful crash
//    with a null check", and this is not even a crash, it is retail's contract.
//  * `n <= 0` writes nothing and returns true, without touching the object.
//
// On the write path SetWindow takes its GROW branch whenever
// `position >> 15 >= headerSize`, i.e. on the first write into every new block;
// that branch is SetWindow's own (compressed_stream_read.cpp).
bool CompressedStream::Write(int n, const void* src) {

    const uint8_t* in = static_cast<const uint8_t*>(src);   // CAST-OK: byte_io

    while (n > 0) {                                      // 1800aa6d1 / 1800aa758
        // NOT RETAIL (HITCH_SAVE, docs/extensions/handins/HITCH_SAVE.md): SetWindow's own
        // fast path (the active window already covers `_position`: it stores `_position`
        // back unchanged and returns) tested here, so the call is made only when it can do
        // something. A save is ~9 million writes of 1-8 bytes and nearly every one takes
        // that path. SetWindow's `fastPath` counter no longer sees the writes that skip it.
        RtIo::CompressedStreamWindow* w = &_windowData[_activeWindow];
        {
            const int32_t a = w->start;
            if (!(a >= 0 && _position >= a &&
                  _position < a + RtIo::kCompressedStreamWindowSize)) {
                SetWindow(_position);                    // 1800aa6f6
                w = &_windowData[_activeWindow];         // 1800aa6fb..711
            }
        }
        const int32_t pos = _position;                   // 1800aa6ff
        int32_t chunk = n;                               // 1800aa70a  MOV EBX,EBP
        const int32_t cap = w->start +
                            RtIo::kCompressedStreamWindowSize;      // 1800aa718  LEA ECX,[R8+0x8000]
        if (pos + n > cap) chunk = cap - pos;            // 1800aa715/1f/23/25

        // 1800aa727..3c: the memcpy. NOT RETAIL (HITCH_SAVE): the scalar sizes copied
        // inline (a constant-size memcpy), the rest through the CRT; the same bytes.
        uint8_t* const dst = w->buffer + (pos - w->start);
        switch (chunk) {
        case 1:  *dst = *in; break;
        case 2:  std::memcpy(dst, in, 2); break;
        case 4:  std::memcpy(dst, in, 4); break;
        case 8:  std::memcpy(dst, in, 8); break;
        default: std::memcpy(dst, in, static_cast<size_t>(chunk)); break;
        }

        _position += chunk;                              // 1800aa741
        n  -= chunk;                                     // 1800aa747
        in += chunk;                                     // 1800aa749
        if (_position > w->end)                          // 1800aa74c/50
            w->end = _position;                          // 1800aa752
    }

    return true;                                         // 1800aa769  MOV AL,0x1
}

// ?PreCommit@CompressedStream@IO@@UEAAXXZ                    retail 0x1800aa8c0
// One instruction: `RET`. The base class's hook is EMPTY, and slot 8 of BOTH
// ??_7CompressedStream@ and ??_7CompressedFileStream@ points at it — one body,
// two rows. It is not a purecall and it is not a stub: Commit dispatches slot 8
// virtually and the base genuinely has nothing to do, because a plain
// CompressedStream writes onto a source that is already the right size.
void CompressedStream::PreCommit() {
}

// ?PreCommit@CompressedMemoryStream@IO@@UEAAXXZ              retail 0x1800a9660
// The memory subclass's source is its OWN embedded MemoryStream (+0x68), which
// has to be sized before Commit writes the archive into it. So this computes
// the archive's exact length and hands it to MemoryStream::Create:
//
//     total = 4 + headerSize*8 + sum(StoredLength(header[i]))
//     return MemoryStream::Create(this + 0x68, total);        // TAIL JUMP
//
// `4 + n*8` is the four-byte count plus the directory — the same expression
// ?Open@CompressedStream validates the source size against, and the same one
// Commit uses to seed the first block's offset. Three independent sites agree
// on the format, which is what makes it a format and not a guess.
//
// The tail jump means the bool is this function's return value even though the
// slot is typed `void` — harmless, and reproduced by simply discarding it.
void CompressedMemoryStream::PreCommit() {

    const int32_t n = _headerSize;                       // 1800a9660  MOVSXD
    uint32_t total = 4 + static_cast<uint32_t>(n) * 8;   // 1800a9667  LEA R8D,[4+RAX*8]
    if (n > 0) {                                         // 1800a966f/71  TEST/JLE
        const RtIo::CompressedStreamHeader* const h = _headerData;
        for (int32_t k = 0; k < n; ++k)                  // 1800a9680
            total += RtIo::StoredLength(h[k]);           // 1800a9683..93
    }

    // `this + 0x68` is the embedded MemoryStream; ENDGAME B.3.9 IO (W2):
    // ?Create@MemoryStream is its member now (the door is gone).
    _memoryStream.Create(static_cast<int32_t>(total));  // 1800a969b..a2  ADD RCX,0x68 ; JMP
}

// ?Close@CompressedStream@IO@@UEAAXXZ                        retail 0x1800aa8d0
//     if (headerSize > 0 && direction == 1) Commit(this);      // TAIL JUMP
// Nothing else. Note it is `direction == 1` (writing), not `!= 0`: Stream.h
// pins 1 == writing from ?Create and 0 == reading from ?Open. A stream that was
// opened for READING and closed writes nothing back, which is what makes Close
// safe to call unconditionally — and the `headerSize > 0` guard is Stream.h's
// "has anything been written yet".
void CompressedStream::Close() {
    if (_headerSize > 0 && _direction == 1) {            // 1800aa8d0/d6
        Commit();                                        // 1800aa8da  JZ 0x1800a9ee0
        return;
    }
}

// ?Close@CompressedFileStream@IO@@UEAAXXZ                    retail 0x1800a9490
//     if (headerSize > 0 && direction == 1) Commit(this);
//     if (*(int*)(this+0x88) != -1)
//         FileSystem::CloseFile(_singleton, (FileInfo*)(this+0x88));   // TAIL JUMP
//
// this+0x88 = 0x68 (the embedded BinaryFileStream) + 0x20 (its FileInfo, which
// FileStream.h pins four ways). So this body independently corroborates that
// +0x20 claim from a class that header does not discuss — and the `!= -1`
// sentinel at FileInfo+0 is the same one ??1FileStream@IO@@ tests, one class
// up. **Now that both are real classes the address arithmetic is gone**: the
// expression is `_file._file`, and the two offsets it used to spell are pinned
// by static_assert instead.
void CompressedFileStream::Close() {

    if (_headerSize > 0 && _direction == 1)              // 1800a9496/9f
        Commit();                                        // 1800a94a5

    RtIO::FileInfo* const fileInfo = &_file._file;       // 1800a94aa  (== this+0x88)
    if (fileInfo->handle != RtIO::FileInfo::kHandleNone) {   // 1800a94b1/b4
        ::IO::FileSystem* const fs = ::IO::FileSystem::_singleton;   // 1800a94b6  (the class static)
        fs->CloseFile(*fileInfo);                        // 1800a94c2
        return;
    }
}

}  // namespace IO

// The two offsets `?Close@CompressedFileStream` used to spell as the literal
// +0x88, now pinned so the class expression above cannot drift from the body
// the disassembly shows.
static_assert(offsetof(IO::CompressedFileStream, _file) == 0x68, "the member");
static_assert(offsetof(IO::CompressedFileStream, _file) +
                  offsetof(IO::FileStream, _file) == 0x88,
              "?Close@CompressedFileStream 0x1800a94aa reads this+0x88");


