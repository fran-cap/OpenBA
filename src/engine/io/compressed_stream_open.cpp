// ============================================================================
// src/engine/io/compressed_stream_open.cpp — Wave 9 lane PKG2, rung 4b-iii
// ============================================================================
//
// ?SetWindowCount@CompressedStream@IO@@  0x1800aa020   (52 instructions)
// ?Open@CompressedStream@IO@@            0x1800aa560
//
// These are the last two bodies between `ReadPackfile` and a mounted package.
// `Open` is the one `ReadPackfile` calls at 0x1800b4864, as
// `cms.CompressedStream::Open(&cms.memoryStream, n)` — one object, two halves
// (§48).
//
// Both are read-path, both stand entirely on ports: SetWindowCount's
// Array<Window>::SetSize (0x180024c10) and Array<Header>::SetSize (0x180024d70)
// landed earlier in this rung, GetAvailableWindow (0x1800a9980) in rung 2, and
// the two Stream virtuals Open dispatches through are MemoryStream's Read and
// GetSize, ported in 4b-ii.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "io/Stream.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"

// B3R RT-IO: the two by-address helpers that stood here are direct calls:
// ?MemAlloc@MemoryAllocator@System@@ 0x1800c7a50 -> System::MemoryAllocator::
// MemAlloc (retail INLINES its three steps -- Enter, Allocate, Leave -- and the
// static member IS those three steps), and Array<Window>::SetSize 0x180024c10 /
// Array<Header>::SetSize 0x180024d70 -> rtcoll::RtColl_ArraySetSize_*.
//
// The source's Stream virtuals are called through the OBJECT's vtable exactly
// as retail does -- by name on `IO::Stream` (B3X W0). Not shortcut to our
// MemoryStream bodies: the source is a Stream& and its dynamic type is the
// caller's business, not ours.

// ============================================================================
// ?SetWindowCount@CompressedStream@IO@@                      retail 0x1800aa020
// ============================================================================
// Grow the window table, give every window a fresh zeroed 32 KB buffer, then
// pick the active window.
//
// TWO TRANSCRIPTIONS THAT LOOK LIKE REDUNDANCY AND ARE NOT:
//
//  1. Retail RE-READS `this->windowData` from the object on every iteration
//     (`MOV RCX,[R15+0x30]` at 1800aa0a0 AND again at 1800aa0a9) rather than
//     caching the base. Reproduced. It is observationally identical here
//     because nothing in the loop can move the array, but caching it would be a
//     change to what the code DOES, and this lane does not make those silently.
//  2. It stores the new buffer into the window and then RELOADS IT FROM THE
//     WINDOW to hand to memset (1800aa0b5), instead of reusing the register it
//     just stored. Also reproduced, for the same reason.
//
// The 0x8000 window size and the 0x18 stride are retail's, and both agree with
// src/engine/io/Stream.h — a useful independent check on rung 2's layout
// rather than a restatement of it, since that header was pinned from the
// CONSTRUCTOR and this is a different function.
void IO::CompressedStream::SetWindowCount(int n) {

    // Array<Window>::SetSize, handed the {data, size, capacity} header.
    rtcoll::RtColl_ArraySetSize_180024c10(&_windowData, n);   // 1800aa04e

    for (int32_t i = 0; i < n; ++i) {                     // 1800aa056/aa0c6
        uint8_t* const buf = static_cast<uint8_t*>(::System::MemoryAllocator::MemAlloc(
            RtIo::kCompressedStreamWindowSize, 4));       // 1800aa08f
        _windowData[i].buffer = buf;                      // 1800aa0a0 / 1800aa0a4
        void* const reloaded = _windowData[i].buffer;     // 1800aa0a9 / 1800aa0b5
        std::memset(reloaded, 0, RtIo::kCompressedStreamWindowSize);  // 1800aa0ba
    }

    _activeWindow = GetAvailableWindow();                 // 1800aa0cb/0d0

}

// ============================================================================
// ?Open@CompressedStream@IO@@                                retail 0x1800aa560
// ============================================================================
// Read the block directory off `src` and validate it against the source length.
//
// THE TWO BOUND TESTS HAVE DIFFERENT SIGNEDNESS, and it is not decoration:
//
//   * `CMP RBP,0x4; JNC` (1800aa595) is UNSIGNED — a negative GetSize, once
//     sign-extended, is a huge unsigned value and PASSES this test.
//   * `LEA EAX,[RDX*0x8]; CMP EAX,EBP; JG` (1800aa5cb) is SIGNED and 32-BIT —
//     a blockCount large enough to overflow `blockCount * 8` wraps negative and
//     PASSES this one too.
//
// Both are transcribed as retail spells them. Making either "safe" would reject
// inputs retail accepts, and this function's whole job is to decide whether a
// packfile header is well-formed.
//
// The final `total == len` is the real integrity check, and the per-entry term
// is a 16-bit field read at +0x04 of each 8-byte header with a fallback to the
// one at +0x06 when it is zero — a compressed-size / raw-size pair, where zero
// in the first means "stored uncompressed".
bool IO::CompressedStream::Open(Stream& srcRef, int windowCount) {
    Stream* const src = &srcRef;

    _source    = src;                                     // 1800aa572
    _direction = 0;                                       // 1800aa57c  (+0x08)

    SetWindowCount(windowCount);                          // 1800aa583

    const int64_t len = static_cast<int32_t>(
        _source->GetSize());                              // 1800aa588..58f slot 6, MOVSXD

    if (static_cast<uint64_t>(len) < 4u) {                 // 1800aa595 CMP/JNC (UNSIGNED)
        return false;                                      // 1800aa59b
    }

    int32_t blockCount = 0;
    src->Read(4, &blockCount);                             // 1800aa5bd slot 3

    // NOT RETAIL (review F-032 / F-050 / F-052, UB + plain hardening): retail
    // takes any blockCount. A negative one passes the signed test below and
    // sizes the directory negative (Read(-16, nullptr) follows); one >= 2^28
    // overflows `blockCount * 8` (UB) and sizes a directory of up to 16 GB; and
    // with 65536 blocks the last starts at 0x7fff8000, where the window
    // arithmetic `start + 0x8000` overflows int32. A stream holds at most
    // 0xFFFF blocks of 32 KB (just under 2 GB, the int32 position range), so
    // anything outside [0, 0xFFFF] is a corrupt directory; with that bound
    // neither product below can overflow.
    if (blockCount < 0 || blockCount > 0xFFFF) {
        return false;
    }

    if (static_cast<int32_t>(blockCount * 8) >
        static_cast<int32_t>(len)) {                       // 1800aa5cb CMP/JG (SIGNED, 32-bit)
        return false;
    }

    // Array<Header>::SetSize, handed the {data, size, capacity} header.
    rtcoll::RtColl_ArraySetSize_180024d70(&_headerData, blockCount);   // 1800aa5d8

    src->Read(_headerSize * 8, _headerData);               // 1800aa5ed slot 3

    // Each 8-byte header's compressedSize (+0x04), falling back to its
    // uncompressedSize (+0x06) when it is zero -- Stream.h's StoredLength rule.
    int32_t total = 4 + blockCount * 8;                    // 1800aa5f7 LEA
    const RtIo::CompressedStreamHeader* e = _headerData;   // 1800aa609
    for (int32_t i = 0; i < _headerSize; ++i) {            // 1800aa5fe..aa628
        uint16_t w = e->compressedSize;                    // 1800aa610
        if (w == 0) w = e->uncompressedSize;               // 1800aa618
        total += w;                                        // 1800aa623
        ++e;                                               // 1800aa61f
    }

    const bool ok = (total == static_cast<int32_t>(len));  // 1800aa634 SETZ
    return ok;
}

