// ============================================================================
// src/engine/io/compressed_stream_release.cpp — Wave 9 lane PKG2, rung 4b-iii
// ============================================================================
//
// ?Release@CompressedStream@IO@@   0x1800aa100
// ??1CompressedStream@IO@@UEAA@XZ  0x1800aa650
//
// THESE TWO ARE LANE W'S ROWS, CLOSED BY THIS LANE, and they are billed that
// way: lane W is closed and this lane is the one with the file in working
// memory, so re-deriving them elsewhere would be waste. `~CompressedMemoryStream`
// (0x1800a9560) ends in `~CompressedStream`, defined at the bottom of this file.
//
// WHAT RELEASE CONFIRMS. Its field accesses are an independent check on the
// CompressedStream layout in src/engine/io/Stream.h — it walks +0x50/+0x58
// (Array<void*> of block buffers), +0x30/+0x38 (Array<Window>, stride 0x18,
// buffer at +0x10), +0x40/+0x48 (Array<Header>), +0x60 (the Compression*), and
// resets +0x1c, +0x20 and +0x28. Every one agrees with the header, which was
// pinned from the CONSTRUCTOR. Two different functions, same answer.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "io/Stream.h"
#include "system/Compression.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"

// `if (p) { Enter(cs); Free(_singleton, p); Leave(cs); }` -- retail INLINES this
// at every one of Release's nine free sites, but the null test and the lock are
// exactly what the exported `?MemFree@MemoryAllocator@System@@SAXPEAX@Z`
// (0x1800c7ad0) already is: its first instruction is `TEST RCX,RCX; JZ ret`, so
// it takes no lock for a null pointer either. B3R RT-IO: called by name
// (System::MemoryAllocator::MemFree), as are the three Array<T> dtors
// (0x180024aa0 / 0x1800249c0 / 0x1800248e0) ~CompressedStream calls on its
// own members.

// ============================================================================
// ?Release@CompressedStream@IO@@                             retail 0x1800aa100
// ============================================================================
// Nine frees in a fixed order, then a three-field reset. The ORDER is
// transcribed rather than tidied: retail frees the *contents* of all three
// arrays before freeing any of the array storage itself, which is the only
// order in which the element loops can still read their own backing store.
//
// B.4 R-IO: the member; the fields are the class's own, typed (Stream.h).
void IO::CompressedStream::Release() {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVE.md): ext.async_save_compress -- a
    // stream destroyed without a Commit detaches (the worker must be done with the Compression
    // object freed below).
    if (_extAsyncCompress != nullptr) _extAsyncCompress(*this, kExtAsyncEnd, 0, nullptr);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/DECODEAHEAD.md): ext.decode_ahead -- the
    // stream's look-ahead blocks are dropped (none outlives the stream or its package's mapping).
    if (_extDecodeAhead != nullptr) _extDecodeAhead(*this, kExtDecodeRelease, 0, nullptr);

    // 1. the Array<void*> of compressed block buffers, +0x50 / +0x58
    {
        void** const data = _blockData;
        const int32_t n   = _blockSize;
        for (int32_t i = 0; i < n; ++i)             // 1800aa140..1800aa18c
            if (data[i]) ::System::MemoryAllocator::MemFree(data[i]);
    }

    // 2. each Window's 0x8000 buffer, +0x30 / +0x38, stride 0x18, buffer +0x10
    {
        RtIo::CompressedStreamWindow* const data = _windowData;
        const int32_t n = _windowSize;
        for (int32_t i = 0; i < n; ++i) {           // 1800aa1a0..1800aa1ed
            void* const buf = data[i].buffer;
            if (buf) ::System::MemoryAllocator::MemFree(buf);
        }
    }

    // 3. the Compression object at +0x60: free ITS first field (the table),
    //    NULL that field, then free the object. The intermediate store at
    //    1800aa236 is not dead — it is retail's, and it runs between the two
    //    frees. (System::Compression is trivially destructible; this IS its
    //    teardown, Compression.h.)
    {
        System::Compression* const comp = _compression;
        if (comp) {                                  // 1800aa1f3
            if (comp->_table) ::System::MemoryAllocator::MemFree(comp->_table);   // 1800aa1fe..1800aa230
            comp->_table = nullptr;                  // 1800aa236
            ::System::MemoryAllocator::MemFree(comp);                             // 1800aa256
        }
        _compression = nullptr;                      // 1800aa26f
    }

    // 4..6. the three array storages, each followed by zeroing {_data,_size}
    //       (retail's one qword store covers _size and the capacity after it)
    {
        void* const p = _blockData;
        if (p) ::System::MemoryAllocator::MemFree(p);                                   // 1800aa27a
        _blockData = nullptr;                        // 1800aa2b2
        _blockSize = 0;                              // 1800aa2b6
        _blockCap  = 0;
    }
    {
        void* const p = _headerData;
        if (p) ::System::MemoryAllocator::MemFree(p);                                   // 1800aa2c1
        _headerData = nullptr;                       // 1800aa2f9
        _headerSize = 0;                             // 1800aa2fd
        _headerCap  = 0;
    }
    {
        void* const p = _windowData;
        if (p) ::System::MemoryAllocator::MemFree(p);                                   // 1800aa308
        _windowData = nullptr;                       // 1800aa340
        _windowSize = 0;                             // 1800aa344
        _windowCap  = 0;
    }

    // 7. the three scalars. _activeWindow goes back to -1, NOT to 0 — the same
    //    value the constructor writes, which is what makes a Released stream
    //    reusable by a later SetWindowCount.
    _position     = 0;                               // 1800aa348
    _activeWindow = -1;                              // 1800aa34c
    _source       = nullptr;                         // 1800aa353

}

// ENDGAME B.3.9 IO (W2): the ??1CompressedStream door (0x1800aa650) that
// stood here -- vptr stamps of 0x180155640 and 0x1801549a0 by address around
// the body below -- is gone with the minted CompressedStream row and its
// RT_DELDTOR row, its last callers. The body is the class destructor below
// plus ~Stream (the encoding free), run by the compiler.

// ============================================================================
// IO::CompressedStream::~CompressedStream -- ENDGAME B.3.9 IO
// ============================================================================
// The class's own destructor: the middle of 0x1800aa650 (Release, then the
// three Array destructors in retail's order). The compiler's epilogue runs
// ~Stream, which frees the encoding.
IO::CompressedStream::~CompressedStream() {
    Release();                                               // 1800aa670
    // The three Array<T> destructors, each handed its {data, size, capacity}
    // header -- the three fields starting at the named data pointer.
    rtcoll::RtColl_ArrayDtor_180024aa0(&_blockData);         // 1800aa67a  +0x50
    rtcoll::RtColl_ArrayDtor_1800249c0(&_headerData);        // 1800aa684  +0x40
    rtcoll::RtColl_ArrayDtor_1800248e0(&_windowData);        // 1800aa68e  +0x30
}
