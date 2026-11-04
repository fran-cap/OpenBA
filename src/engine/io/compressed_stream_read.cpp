// ============================================================================
// src/engine/io/compressed_stream_read.cpp — Wave 9 lane PKG2, rung 2
// IO::CompressedStream — the READ path under ReadPackfile
// ============================================================================
//
//   | Export                                          | Retail      | Ord  | Instrs |
//   |-------------------------------------------------|-------------|------|--------|
//   | ?Read@CompressedStream@IO@@UEAA_NHPEAX@Z        | 0x1800aa780 | 3449 |   51   |
//   | ?SetWindow@CompressedStream@IO@@AEAA_NH@Z       | 0x1800a9aa0 | 4016 |  296   |
//   | ?GetAvailableWindow@CompressedStream@IO@@AEAAHXZ| 0x1800a9980 | 2458 |   79   |
//   | ?DecompressFromSource@CompressedStream@IO@@AEAA_NH@Z | 0x1800a96b0 | 2152 | 52 |
//
// Names and ordinals printed out of Runtime-x64.dll's export directory with
// `pefile`. `tools/export_name_audit.py` re-checks them.
//
//
// ---------------------------------------------------------------------------
// 1. WHERE THIS SITS
// ---------------------------------------------------------------------------
// Rung 2 of docs/history/runtime_port/WAVE9_READPACKFILE_PLAN.md. `?ReadPackfile@` does
// ~25 windowed reads through a CompressedStream, and MSVC inlined
// `CompressedStream::Read` at every one of them — which is where 1,864 of its
// instructions went. Nothing above this can be verified until this works.
//
// It also gives rung 1 its first real caller: `DecompressFromSource` calls
// `?Decompress@Compression@System@@` directly (0x1800a9746), so from here the
// codec ported last iteration decodes actual package bytes rather than probe
// corpora.
//
// ---------------------------------------------------------------------------
// 2. THE FORMAT AND THE MACHINE, both read out of the disassembly
// ---------------------------------------------------------------------------
// A CompressedStream is a fixed set of 32 KB decompression WINDOWS over a
// source stream carrying a directory of blocks. `?Open@CompressedStream@IO@@`
// spells the format out completely (see src/engine/io/Stream.h):
//
//     [uint32 blockCount][blockCount x Header{srcOffset, compSize, uncompSize}][blocks]
//
// A block is a fixed 32 KB of DECOMPRESSED data, so `blockIndex = offset >> 15`
// everywhere, and `compSize == 0` means the block is STORED, not empty.
//
// `Read(n, dst)` loops: point the window machinery at `position`, copy as much
// as that window holds, advance, repeat. `SetWindow(pos)` is the machinery:
// fast path if the active window already covers `pos`, else scan the loaded
// windows, else evict one (`GetAvailableWindow`, an LRU) and fill it
// (`DecompressFromSource`).
//
// ---------------------------------------------------------------------------
// 3. THREE QUIRKS THAT A TIDIED-UP PORT WOULD "FIX" AND BREAK
// ---------------------------------------------------------------------------
//  1. **SetWindow's scan tests the ACTIVE window's `start`, not the candidate's.**
//     `TEST R8D,R8D ; JS` at 0x1800a9b08 is inside the loop, but R8D is loaded
//     ONCE before it (0x1800a9acf) and is never rewritten in the body. So when
//     the active window is FREE (`start < 0`), the scan skips every window and
//     falls through to eviction — even if a loaded window covers `pos`. That
//     reads exactly like a copy-paste bug and it is retail's behaviour; a port
//     that "corrects" it to `wins[i].start` changes which window gets evicted
//     and therefore what the LRU does for the rest of the stream.
//  2. **GetAvailableWindow's free-window search keeps the LAST match, not the
//     first.** `CMOVL EBX,ECX` at 0x1800a99c6 runs on every iteration with no
//     break, so the highest-indexed free window wins.
//  3. **Its LRU tie-break keeps the EARLIER index.** `JLE` at 0x1800a99f8 means
//     strictly-greater replaces, so equal use counts leave the incumbent. With
//     a fresh stream every window has useCount 0, so this decides the very first
//     eviction — and every window assignment after it.
//
// ---------------------------------------------------------------------------
// 4. WHAT IS PORTED AND WHAT IS NOT — the direction split
// ---------------------------------------------------------------------------
// `SetWindow`'s miss path forks on `Stream::direction` (+0x08):
//
//   direction == 0 (READING, what ?Open@ sets):  GetAvailableWindow, then
//       DecompressFromSource(position >> 15) — pull the block from the source
//       stream. **PORTED.**
//   direction != 0 (WRITING/in-memory) and blockIndex < headerSize: the same
//       eviction, then decode from `blockData` (+0x50) instead of the source.
//       ~30 instructions. **PORTED** — it is cheap and it is what a
//       CompressedMemoryStream that has been written and re-read takes.
//   direction != 0 and blockIndex >= headerSize: GROW the header and block
//       arrays until they cover blockIndex, then claim a window for it.
//       **PORTED 2026-09-13** (0x1800a9ba0..0x1800a9e3a). It is two inlined
//       `Collection::Array<T>::SetSize(size+1)` growth loops (8-byte elements,
//       zero-filled, capacity policy `n` then `n*3`) followed by the same
//       eviction as the other miss arms. Every save takes it on the first write
//       into each new block; until it was ported the DLL-absent save died here
//       (the load/resave gate, run 1).
//
// `CompressWindow` (0x1800a9810), which `GetAvailableWindow` calls to flush a
// dirty window before evicting it, lives in compressed_stream_write.cpp.
//
// ---------------------------------------------------------------------------
// 5. THE ORACLE — LOCKSTEP, AND STATE NOT JUST BYTES
// ---------------------------------------------------------------------------
// Both requirements come from lane PICK and both are live here, which is why
// this rung's oracle is a bespoke probe rather than the usual replay:
//
//  * **State, not just bytes.** A read mutates `position` (+0x1c),
//    `activeWindow` (+0x20) and EVERY window's `useCount`. Two implementations
//    can return identical bytes and leave the object in different states, and
//    the next read then diverges for reasons the previous comparison called
//    fine. Rung 1 was exempt because `Decompress` is static with a local cursor;
//    this rung has no such excuse.
//  * **Lockstep, not end-to-end.** A CompressedStream carries state between
//    blocks. A defect that cancels out compares equal at the end while every
//    intermediate read differed.
//
// So `RtIo_CompressedStreamReadProbe()` builds TWO CompressedMemoryStreams over
// byte-identical content, drives one entirely through our ports and the other
// entirely through the DLL's, and after EVERY read compares: the returned bool,
// the bytes, `position`, `activeWindow`, and the full window array — each
// window's `useCount`, `start`, `end`, and the CONTENT of every loaded window's
// 32 KB page. It stops at the first mismatch and names which term failed.
//
// The two stacks are genuinely independent: our `Read` calls our `SetWindow` by
// direct C++ call and the DLL's calls its own by a direct relative CALL, so
// neither can be entered by accident.
//
// The content is produced by the DLL's own WRITER (Create + Write + Close),
// the same trick rung 1 used with `Compress` — valid input by construction,
// and the writer is deliberately not ported so a shared misreading cannot pass
// its own test.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "io/Stream.h"
#include "system/Compression.h"
#include "system/MemoryAllocator.h"

namespace RtIo {

using Window = CompressedStreamWindow;
using Header = CompressedStreamHeader;

namespace {

constexpr int32_t kWin = kCompressedStreamWindowSize;   // 0x8000

// `Collection::Array<T>::SetSize(size + 1)` as MSVC inlined it twice into
// SetWindow's grow path (headers at 1800a9bb0, blocks at 1800a9cb1): 8-byte
// elements, new slots zero-filled, capacity `n` on the first growth and `n*3`
// after. Retail's lock / Allocate(_singleton, cap*8, align) / unlock triple is
// the exported MemAlloc (compressed_stream_write.cpp says why they are the same
// function). The element copy loop only runs over a FRESH allocation; when the
// capacity already covers `n` it starts at `old` and just zeroes the new slot.
template <class T>
inline void GrowByOne(T*& data, int32_t& size, int32_t& cap, int32_t align) {
    const size_t elem = sizeof(T);
    const int32_t old = size;
    const int32_t n   = old + 1;
    T* const oldData = data;
    size = n;
    if (cap < n) {                                      // 1800a9bd4 / 1800a9cd5
        cap = (old != 0) ? n * 3 : n;
        data = static_cast<T*>(::System::MemoryAllocator::MemAlloc(
                static_cast<uint64_t>(cap) * elem, align));
    }
    if (data != nullptr) {
        const int32_t first = (oldData == data) ? old : 0;
        for (int32_t i = first; i < n; ++i) {
            if (i < old) std::memcpy(data + i, oldData + i, elem);
            else         std::memset(data + i, 0, elem);
        }
    }
    if (oldData != data && oldData != nullptr)
        ::System::MemoryAllocator::MemFree(oldData);
}

}  // namespace

// SetWindow's branch histogram. Five outcomes, counted rather than inferred,
// because the probe cannot see from outside which one a read took — and the
// first two versions of that probe both tried to infer it from window state and
// both measured a proxy that was constant by construction. A branch that reads
// zero is an UNVERIFIED branch, and only a counter inside the body can say so.
struct SetWindowBranchCounts {
    int64_t fastPath   = 0;   // the active window already covers pos
    int64_t scanHit    = 0;   // another LOADED window covers pos (the LRU re-use path)
    int64_t readMiss   = 0;   // direction == 0: evict + DecompressFromSource
    int64_t inMemory   = 0;   // direction != 0, block resident: evict + decode blockData
    int64_t grow       = 0;   // direction != 0, block beyond the end: extend the arrays
};
static SetWindowBranchCounts g_swBranch;

const SetWindowBranchCounts& RtIo_SetWindowBranchCounts() { return g_swBranch; }

int64_t RtIo_CompressedStreamGrowDelegations() { return g_swBranch.grow; }

}  // namespace RtIo

// B.4 R-IO: the three bodies below are `IO::CompressedStream` members (they
// were `RtIo_CompressedStream_*` doors on the POD view), moved statement for
// statement; section D binds them (see Read's banner at the bottom).

// ============================================================================
// ?GetAvailableWindow@CompressedStream@IO@@AEAAHXZ           retail 0x1800a9980
// ============================================================================
// Pick a window to evict. Free windows first (the LAST free one — §3.2), else
// the largest useCount (ties keep the earlier index — §3.3).
int IO::CompressedStream::GetAvailableWindow() {

    RtIo::Window* const w = _windowData;
    const int32_t n = _windowSize;                      // 1800a999a
    int32_t best = -1;                                  // 1800a99a0  OR EBX,-1

    if (n > 0) {
        // `CMOVL EBX,ECX` with NO break: every free window overwrites the
        // previous choice, so the highest-indexed one wins.
        for (int32_t i = 0; i < n; ++i)                 // 1800a99c0
            if (w[i].start < 0) best = i;
    }
    if (best < 0) {                                     // 1800a99d2  JNS skips this
        best = 0;                                       // 1800a99d9
        // Strictly greater (JLE at 1800a99f8 skips), so equal counts keep the
        // incumbent. Every window is 0 on a fresh stream, which makes this the
        // rule that decides the FIRST eviction.
        for (int32_t i = 1; i < n; ++i)                 // 1800a99f0
            if (w[i].useCount > w[best].useCount) best = i;
    }

    // Flush before evicting, but only when WRITING. On the read path
    // `direction` is 0 and CompressWindow is never reached — which is why this
    // lane does not port it.
    if (w[best].start >= 0 && _direction == 1) {        // 1800a9a1d / 1800a9a24
        CompressWindow(best);                           // 1800a9a2f
    }

    w[best].start    = -1;                              // 1800a9a38
    w[best].end      = -1;                              // 1800a9a45
    w[best].useCount = 0;                               // 1800a9a52
    for (int32_t i = 0; i < _windowSize; ++i)           // 1800a9a59
        if (i != best) ++w[i].useCount;                 // 1800a9a6a

    // No oracle here: this MUTATES the window array, so calling the shipped body
    // to compare would evict a second window and corrupt the state being
    // compared. It is verified through the lockstep probe in §5, which compares
    // the whole window array after every read — that is where its effect is
    // visible, and comparing it here would destroy what the probe measures.
    return best;                                        // 1800a9a8a
}

// ============================================================================
// ?DecompressFromSource@CompressedStream@IO@@AEAA_NH@Z       retail 0x1800a96b0
// ============================================================================
// Fill the ACTIVE window from block `blk` of the source stream.
bool IO::CompressedStream::DecompressFromSource(int blk) {

    // NOT RETAIL (review F-051, UB): retail has no lower bound, so a negative
    // position (a seek to -1) reads _headerData[-1] and shifts a negative value.
    if (blk < 0 || blk >= _headerSize) return false;    // 1800a96c2  JL continues

    // Retail's `_alloca(0x8020)` (0x1800a96b7 via 0x180095060, the stack probe).
    // The extra 0x20 is MSVC's home space for the calls below, not part of the
    // block; the block itself can never exceed 0x8000.
    uint8_t scratch[RtIo::kWin];

    const RtIo::Header* const h = &_headerData[blk];    // 1800a96ec
    RtIo::Window* const w = &_windowData[_activeWindow];   // 1800a96f0..fc

    // NOT RETAIL (review F-013 / F-014 / F-015, plain hardening): retail trusts
    // the directory's two u16 sizes. compressedSize > 0x8000 is read into the
    // 0x8000-byte scratch above (a stack overflow), a STORED uncompressedSize
    // > 0x8000 into the 0x8000-byte window (a heap overflow), and any
    // uncompressedSize > 0x8000 puts the window's end past its buffer, which
    // Read then copies from. No block of a valid stream exceeds 0x8000 either
    // way, so this rejects only a corrupt directory, before the window is
    // claimed (it stays free, start / end -1, as for blk >= _headerSize).
    if (h->compressedSize > RtIo::kWin || h->uncompressedSize > RtIo::kWin) return false;

    w->start = blk << 15;                               // 1800a96d9 / 1800a9700
    w->end   = w->start + h->uncompressedSize;          // 1800a9703 / 1800a9709

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/DECODEAHEAD.md): ext.decode_ahead -- a block a
    // worker already decoded from the mapped package bytes is copied into the window.
    if (_extDecodeAhead != nullptr && _extDecodeAhead(*this, kExtDecodeBlock, blk, w->buffer))
        return true;

    // The source's Stream virtuals, through the object's own vtable as retail
    // does (`[RAX+0x20]` SetCurrentPos, `[RAX+0x18]` Read).
    Stream* const src = _source;                        // 1800a970c
    src->SetCurrentPos(
        static_cast<int32_t>(h->sourceOffset));         // 1800a9715  CALL [RAX+0x20]

    if (h->compressedSize != 0) {                       // 1800a9723  TEST DX,DX
        src->Read(h->compressedSize, scratch);          // 1800a9730  CALL [RAX+0x18]
        // Rung 1's port -- this is the call that finally gives
        // Compression::Decompress real package bytes.
        ::System::Compression::Decompress(
                scratch, h->compressedSize, w->buffer,
                static_cast<uint32_t>(RtIo::kWin));     // 1800a9746
    } else {
        // STORED: read straight into the window. compressedSize == 0 is the
        // stored marker, not an empty block (§2).
        src->Read(h->uncompressedSize, w->buffer);      // 1800a9755  CALL [RAX+0x18]
    }

    return true;                                        // 1800a9768
}

// ============================================================================
// ?SetWindow@CompressedStream@IO@@AEAA_NH@Z                  retail 0x1800a9aa0
// ============================================================================
bool IO::CompressedStream::SetWindow(int pos) {
    using RtIo::g_swBranch;
    using RtIo::kWin;

    _position = pos;                                    // 1800a9ac0
    const int32_t active = _activeWindow;               // 1800a9ac3
    RtIo::Window* const w = _windowData;

    // Loaded ONCE, and deliberately not reloaded inside the scan below — see
    // §3.1. This single value is both the fast-path test and the scan's guard.
    const int32_t activeStart = w[active].start;        // 1800a9acf

    if (activeStart >= 0 && pos >= activeStart && pos < activeStart + kWin) {
        ++g_swBranch.fastPath;
        return true;                                    // 1800a9ae7
    }

    const int32_t n = _windowSize;                      // 1800a9af3
    int32_t found = -1;
    for (int32_t i = 0; i < n; ++i) {                   // 1800a9b03
        if (i == active)      continue;                 // 1800a9b06
        if (activeStart < 0)  continue;                 // 1800a9b0b — NOT w[i].start (§3.1)
        const int32_t s = w[i].start;                   // 1800a9b0d
        if (pos < s)          continue;                 // 1800a9b12
        if (pos < s + kWin) { found = i; break; }       // 1800a9b1c
    }

    if (found >= 0) {                                   // 1800a9b2e
        ++g_swBranch.scanHit;
        w[found].useCount = 0;                          // 1800a9b35
        for (int32_t k = 0; k < _windowSize; ++k)       // 1800a9b3c
            if (k != found) ++w[k].useCount;            // 1800a9b4d
        _activeWindow = found;                          // 1800a9b61
        return true;
    }

    // ---- miss ------------------------------------------------------------
    if (_direction == 0) {                              // 1800a9b69
        ++g_swBranch.readMiss;
        // READING: evict and pull the block from the source.
        _activeWindow = GetAvailableWindow();                 // 1800a9b72
        const bool ok = DecompressFromSource(_position >> 15);   // 1800a9b7d/83
        return ok;                                            // 1800a9b88
    }

    const int32_t blk = pos >> 15;                      // 1800a9b8d
    if (blk >= _headerSize) {                           // 1800a9b94  JL -> in-memory
        // ---- GROW (direction != 0, block beyond the end) ---- 1800a9ba0..1800a9e3a
        // Append one header and one block slot per iteration until the arrays
        // cover `blk`, claiming a window each time. Two quirks kept on purpose:
        //  * every claimed window gets the SAME start (`blk << 15`, held in a
        //    stack slot for the whole loop), not its own block's start; only the
        //    last iteration's window matters, and it is the one that ends at
        //    `position`. Sequential writes take exactly one iteration.
        //  * the retail `if (n != old)` around each SetSize is `old+1 != old`,
        //    always true, and is not reproduced.
        ++g_swBranch.grow;
        const int32_t blockStart = blk << 15;           // 1800a9ba7  local_res10
        do {
            RtIo::GrowByOne(_headerData, _headerSize, _headerCap, 4);   // 1800a9bb0..1800a9cae
            RtIo::GrowByOne(_blockData, _blockSize, _blockCap, 8);      // 1800a9cb1..1800a9dc7
            const int32_t aw = GetAvailableWindow();    // 1800a9dc7
            _activeWindow = aw;                         // 1800a9dcc
            RtIo::Window* const w = &_windowData[aw];
            w->start = blockStart;                      // 1800a9dd3
            if (blk == _headerSize - 1)                 // 1800a9ddd
                w->end = _position;                     // 1800a9df0
            else
                w->end = w->start + kWin;               // 1800a9e0d
        } while (_headerSize < blk + 1);                // 1800a9e19
        return true;                                    // 1800a9ebb
    }

    // ---- in-memory block (direction != 0, block already present) ---------
    ++g_swBranch.inMemory;
    _activeWindow = GetAvailableWindow();               // 1800a9e3f
    const int32_t b = _position >> 15;                  // 1800a9e47/4a
    const RtIo::Header* const h  = &_headerData[b];
    void* const* const blocks = _blockData;             // +0x50
    RtIo::Window* const aw = &_windowData[_activeWindow];

    aw->start = b << 15;                                // 1800a9e6a/6d
    aw->end   = aw->start + h->uncompressedSize;        // 1800a9e71/79

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVE.md): ext.async_save_compress -- a
    // block still queued for the worker is decoded from the job's copy of its bytes (what the
    // decode below returns once the job lands).
    if (_extAsyncCompress != nullptr && _extAsyncCompress(*this, kExtAsyncCopyQueued, b, aw->buffer))
        return true;

    if (h->compressedSize != 0) {                       // 1800a9e83  TEST AX,AX
        ::System::Compression::Decompress(
                blocks[b], h->compressedSize, aw->buffer,
                static_cast<uint32_t>(kWin));           // 1800a9e9d
    } else {
        std::memcpy(aw->buffer, blocks[b], h->uncompressedSize);  // 1800a9eb6
    }
    return true;                                        // 1800a9ebb
}

// ============================================================================
// `IO::CompressedStream::Read` — B.3.4 block 4 (STREAMS), and SECTION D
// ============================================================================
// THE ONE SLOT THIS FILE OWNS, AND THE MOST CONSTRAINED BODY IN THE BLOCK.
//
// B34_BRIEFS section D: an intermittent defect on the compressed-load path has
// been measured on an UNCHANGED tree — roughly two boots in six load a resource
// as NULL, and one of those hung in teardown with the loader thread still
// EXECUTING inside this very function 45 seconds after Quit. A separate agent
// is root-causing it.
//
// So the body below is the door's former body, transcribed statement for
// statement, with only `self->X` becoming the member's own field. Nothing was
// added: no lock, no retry, no loop bound, no null check, no reordering. And
// nothing was quietly removed — in particular the loop's exit condition and the
// unguarded `SetWindow` failure return are exactly as they were, because
// whatever makes the read spin must still be able to.
//
// If this conversion made the symptom STOP reproducing, that is a red flag to
// report, not a win.
namespace IO {

// EXT SEAM (NOT RETAIL, docs/extensions/handins/DECODEAHEAD.md): ext.decode_ahead (Stream.h).
CompressedStream::ExtDecodeAheadFn CompressedStream::_extDecodeAhead = nullptr;
// EXT SEAM (NOT RETAIL, docs/extensions/handins/STREAMCACHE.md): ext.stream_cache (Stream.h).
CompressedStream::ExtReadTapFn CompressedStream::_extReadTap = nullptr;

// ?Read@CompressedStream@IO@@UEAA_NHPEAX@Z                   retail 0x1800aa780
//   while (n > 0) {
//       if (!SetWindow(position)) return false;
//       chunk = min(n, window.end - position);
//       memcpy(dst, window.buffer + (position - window.start), chunk);
//       position += chunk; n -= chunk; dst += chunk;
//   }
// Note the loop re-enters SetWindow every iteration with the UPDATED position,
// which is what makes a read spanning several blocks work.
bool CompressedStream::Read(int n, void* dst) {

    uint8_t* out = static_cast<uint8_t*>(dst);          // CAST-OK: byte_io
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/STREAMCACHE.md): what this read started at,
    // for the tap below. Reads nothing retail does not read.
    const int32_t tapPos = _position;
    const int32_t tapN   = n;

    while (n > 0) {                                     // 1800aa7a2 / 1800aa806
        if (!SetWindow(_position)) {                    // 1800aa7b6
            if (_extReadTap != nullptr) _extReadTap(*this, tapPos, tapN, dst, false);
            return false;                               // 1800aa825
        }
        const RtIo::CompressedStreamWindow* const w =
            &_windowData[_activeWindow];                // 1800aa7bf
        int32_t chunk = n;                              // 1800aa7c3  ESI = EBP
        const int32_t pos = _position;                  // 1800aa7d5
        // NOT RETAIL (review F-075, UB): `pos + n` in int64; retail's int32 sum
        // overflows for a huge count and then copies all n bytes.
        if (static_cast<int64_t>(pos) + n > w->end) chunk = w->end - pos;   // 1800aa7db/7df/7e1
        // NOT RETAIL (review F-074 / F-075 / F-015, plain hardening): at the end
        // of a short (< 0x8000) block the window is still active but chunk is 0,
        // so retail spins here forever; past that end inside the window it is
        // negative and memcpy takes it as 2^64 - n. Either is a read past the
        // end of the stream (or of a corrupt block): it fails, as a read past a
        // full last block does in SetWindow.
        if (chunk <= 0) {
            if (_extReadTap != nullptr) _extReadTap(*this, tapPos, tapN, dst, false);
            return false;
        }
        std::memcpy(out, w->buffer + (pos - w->start),
                    static_cast<size_t>(chunk));        // 1800aa7e3..f7
        _position += chunk;                             // 1800aa7fc
        n   -= chunk;                                   // 1800aa7ff
        out += chunk;                                   // 1800aa801
    }

    if (_extReadTap != nullptr) _extReadTap(*this, tapPos, tapN, dst, true);   // EXT SEAM (STREAMCACHE)
    return true;                                        // 1800aa808
}

// ============================================================================
// CompressedStream::ReadArray -- NOT RETAIL: an exact perf body (issue #13)
// ============================================================================
// Retail's IO::BinaryReader array slots (0x1800a8410.., serialize_family.cpp
// shape 3) read an array ONE ELEMENT PER VIRTUAL CALL: SerializeX(arr[i]) ->
// Stream::Read(sizeof(T), &arr[i]), result ignored. Over a package that is one
// SetWindow + one memcpy per byte of every byte array, about a third of the
// loader thread's time on a large mod set. This body is that loop with the
// calls that provably do nothing but copy batched into one memcpy.
//
// WHY THE BATCH IS THE SAME AS THE CALLS IT REPLACES. Take an element at q with
// the active window w (`_activeWindow` in range, w.start >= 0), and
//     w.start <= q   and   q + elemSize <= min(w.end, w.start + kWin).
// Read(elemSize) at q then runs exactly this, and nothing else:
//   * SetWindow(q) takes the fast path (activeStart >= 0, q >= activeStart,
//     q < activeStart + kWin): it stores _position = q (unchanged), bumps the
//     g_swBranch.fastPath tally (read by nothing) and returns true. No window,
//     useCount or _activeWindow changes, no decode.
//   * chunk = elemSize, because q + elemSize <= w.end.
//   * memcpy(dst, w.buffer + (q - w.start), elemSize); _position += elemSize.
// So every element up to `limit = min(w.end, w.start + kWin)` costs a copy of
// the bytes right after the previous element's, and m of them in a row are one
// memcpy of m * elemSize bytes and _position += m * elemSize. Every element
// that does NOT satisfy that condition -- the one that straddles a window end,
// the first one after it, a miss, an eviction, the DecompressFromSource at a
// 32 KB boundary, a failing SetWindow, the zero-chunk spin past a short last
// block -- goes through the real Read, one element, exactly as retail calls it.
// After it the state is whatever retail's would be, and the loop re-derives the
// condition from that state. There is no case analysis of the failure paths to
// get wrong: they are never batched.
//
// The failure case in particular. Retail keeps calling Read for every element
// after a failed SetWindow (each fails again, at the same _position, and each
// runs GetAvailableWindow, which ages the useCounts). Here a failed element
// leaves the active window with start -1 (GetAvailableWindow freed it and the
// failing DecompressFromSource returned before refilling it), so the batch
// condition is false and every remaining element goes through Read: the same
// calls, the same useCount ageing, the same untouched destination bytes.
//
// Two notes on the arithmetic: limit and the room are int64 so `start + kWin`
// cannot wrap, and a window whose start is within kWin of INT32_MAX (where
// retail's int32 `activeStart + kWin` would wrap) is never batched. The
// destination is never inside a window buffer (the callers deserialize into
// their own objects), so one memcpy and m small ones copy the same bytes.
void CompressedStream::ReadArray(int count, int elemSize, void* dst) {
    uint8_t* out = static_cast<uint8_t*>(dst);          // CAST-OK: byte_io
    int32_t i = 0;
    while (i < count) {
        const int32_t a = _activeWindow;
        if (elemSize > 0 && a >= 0 && a < _windowSize) {
            const RtIo::CompressedStreamWindow& w = _windowData[a];
            const int64_t start = w.start;
            const int64_t pos   = _position;
            if (start >= 0 && start <= INT32_MAX - RtIo::kWin && pos >= start) {
                int64_t limit = start + RtIo::kWin;
                if (w.end < limit) limit = w.end;
                int64_t m = (limit - pos) / elemSize;   // limit - pos may be <= 0
                if (m > count - i) m = count - i;
                if (m > 0) {
                    const int64_t bytes = m * elemSize;
                    std::memcpy(out, w.buffer + (pos - start), static_cast<size_t>(bytes));
                    _position = static_cast<int32_t>(pos + bytes);
                    // EXT SEAM (NOT RETAIL, docs/extensions/handins/STREAMCACHE.md): the batched copy,
                    // as the m Reads it stands for would each have been tapped.
                    if (_extReadTap != nullptr)
                        _extReadTap(*this, static_cast<int32_t>(pos), static_cast<int32_t>(bytes), out, true);
                    out += bytes;
                    i += static_cast<int32_t>(m);
                    continue;
                }
            }
        }
        // Not provably a plain copy: retail's own call, for one element.
        Read(elemSize, out);
        out += elemSize;
        ++i;
    }
}

}  // namespace IO
