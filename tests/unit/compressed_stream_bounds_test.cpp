// ============================================================================
// tests/unit/compressed_stream_bounds_test.cpp -- malformed compressed streams
// ============================================================================
// The package / save read path on hostile input (review 2026-11-03, group
// compressed-stream-bounds): System::Compression::Decompress, IO::MemoryStream's
// Read / Write bound and IO::CompressedStream's Open / SetWindow /
// DecompressFromSource / Read over a real MemoryStream. Every input below is
// malformed; retail overruns a buffer, spins, or does undefined arithmetic on
// it. Each test carries the input inline (hex) and checks that it is rejected
// cleanly. Valid data is covered by decode_exact_test.cpp (byte-identical to
// retail's loops) and by the round trips at the end of this file.
//
// Findings: F-001 F-002 (Decompress), F-016 F-053 (MemoryStream::Read),
// F-032 F-050 F-052 (Open), F-013 F-014 F-015 F-051 (DecompressFromSource),
// F-074 F-075 (Read). F-002 and the zero-length memcpy(nullptr) case are UB
// with no other observable effect: only a UBSan build shows them failing.
// ============================================================================
#include "unit.h"

#include "io/Stream.h"
#include "system/Compression.h"
#include "system/MemoryAllocator.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> Hex(const char* s) {
    std::vector<uint8_t> out;
    int hi = -1;
    for (; *s != 0; ++s) {
        const char c = *s;
        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        if (v < 0)
            continue;
        if (hi < 0) {
            hi = v;
        } else {
            out.push_back(static_cast<uint8_t>((hi << 4) | v));
            hi = -1;
        }
    }
    return out;
}

void Append(std::vector<uint8_t>& v, const std::vector<uint8_t>& tail) {
    v.insert(v.end(), tail.begin(), tail.end());
}

// The host has no seated MemoryAllocator: SetWindowCount's window buffers go
// through the instrument seat, armed with the CRT heap (ext_package_cache_test.cpp
// does the same). The host ~CompressedStream is a stub, so the windows and the
// directory are not released; the tests are few and small.
void* SeatAlloc(uint64_t size, int) {
    return std::malloc(static_cast<size_t>(size));
}
void SeatFree(void* p) {
    std::free(p);
}
struct HeapSeat {
    HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = &SeatAlloc;
        System::g_memoryAllocatorInstrument.memFree = &SeatFree;
    }
    ~HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = nullptr;
        System::g_memoryAllocatorInstrument.memFree = nullptr;
    }
};

// A real MemoryStream over `bytes` and a CompressedMemoryStream opened on it
// with one window, as CompressedMemoryStream::Open and ReadPackfile do.
struct Opened {
    HeapSeat seat;
    std::vector<uint8_t> bytes;
    IO::MemoryStream ms;
    IO::CompressedMemoryStream cs;
    bool ok = false;

    explicit Opened(const std::vector<uint8_t>& b) : bytes(b) {
        ms._data = bytes.data();
        ms._size = static_cast<uint32_t>(bytes.size());
        ok = cs.IO::CompressedStream::Open(ms, 1);
    }
};

// One block: {sourceOffset 12, compressedSize 0, uncompressedSize 4} + "abcd".
const char* const kShortBlock = "01000000 0c000000 0000 0400 61626364";

}  // namespace

// ---------------------------------------------------------------------------
// System::Compression::Decompress
// ---------------------------------------------------------------------------

// F-001: a literal run of 11 bytes (control 0x0a) in a 1-byte source. Retail
// copies the 10 bytes past the source end and returns 11.
UNIT_TEST(cs_bounds_decompress_literal_run_past_source_end) {
    std::vector<uint8_t> src = Hex("0a");
    src.resize(64, 0x5a);   // readable slack: the over-read lands here, not off the heap
    std::vector<uint8_t> dst(64, 0);
    CHECK_EQ(System::Compression::Decompress(src.data(), 1, dst.data(), 64), 0);
}

// F-001: a match whose offset byte (and, for len 7, its length byte) lies past
// the source end.
UNIT_TEST(cs_bounds_decompress_match_bytes_past_source_end) {
    std::vector<uint8_t> dst(64, 0);
    std::vector<uint8_t> a = Hex("00 61 20");   // literal 'a', match without its offset byte
    a.resize(64, 0x00);
    CHECK_EQ(System::Compression::Decompress(a.data(), 3, dst.data(), 64), 0);
    std::vector<uint8_t> b = Hex("00 61 e0 01");   // len-7 match: length byte present, offset not
    b.resize(64, 0x00);
    CHECK_EQ(System::Compression::Decompress(b.data(), 4, dst.data(), 64), 0);
}

// F-001: an empty source. Retail's do-while still decodes one control byte.
UNIT_TEST(cs_bounds_decompress_empty_source) {
    std::vector<uint8_t> src = Hex("00 41");
    std::vector<uint8_t> dst(64, 0);
    CHECK_EQ(System::Compression::Decompress(src.data(), 0, dst.data(), 64), 0);
}

// F-002: a short-distance match (dist 3, the byte loop): op[k - dist] wrapped
// size_t. The result is unchanged; only UBSan sees the old form.
UNIT_TEST(cs_bounds_decompress_short_distance_match_decodes) {
    // literal "abc", then a match: c 0x40 -> len 2 (+2 = 4 bytes), lo 2 -> dist 3
    std::vector<uint8_t> src = Hex("02 61 62 63 40 02");
    std::vector<uint8_t> dst(16, 0);
    CHECK_EQ(System::Compression::Decompress(src.data(), static_cast<uint32_t>(src.size()),
                                             dst.data(), 16), 7);
    CHECK(std::memcmp(dst.data(), "abcabca", 7) == 0);
}

// ---------------------------------------------------------------------------
// IO::MemoryStream::Read / Write
// ---------------------------------------------------------------------------

// F-016: a negative cursor (a sourceOffset >= 0x80000000). Retail's 32-bit
// test `cursor + n <= size` passes and memcpy reads before the blob.
UNIT_TEST(cs_bounds_memorystream_negative_cursor) {
    std::vector<uint8_t> big(128, 0x33);
    IO::MemoryStream ms;
    ms._data = big.data() + 64;   // the "blob" is big[64..80); big[48..64) is the slack
    ms._size = 16;
    uint8_t out[16] = {};
    CHECK(ms.SetCurrentPos(-16));
    CHECK(!ms.Read(16, out));
    CHECK(!ms.Write(16, out));
    ms._data = nullptr;
}

// F-053: cursor + count overflows int32 (sourceOffset 0x7ffffff0, csize 0x100).
UNIT_TEST(cs_bounds_memorystream_cursor_plus_count_overflow) {
    std::vector<uint8_t> blob(16, 0);
    IO::MemoryStream ms;
    ms._data = blob.data();
    ms._size = 16;
    std::vector<uint8_t> out(0x100, 0);
    CHECK(ms.SetCurrentPos(0x7ffffff0));
    CHECK(!ms.Read(0x100, out.data()));
    CHECK(!ms.Write(0x100, out.data()));
    ms._data = nullptr;
}

// F-016 / F-032: a negative count. Retail's test passes (cursor + n < size)
// and memcpy runs with size_t(n), 2^64 - 2 bytes.
UNIT_TEST(cs_bounds_memorystream_negative_count) {
    std::vector<uint8_t> blob(16, 0);
    IO::MemoryStream ms;
    ms._data = blob.data();
    ms._size = 16;
    uint8_t out[16] = {};
    CHECK(ms.SetCurrentPos(4));
    CHECK(!ms.Read(-2, out));
    CHECK(!ms.Write(-2, out));
    CHECK_EQ(ms.GetCurrentPos(), 4u);
    ms._data = nullptr;
}

// F-016 (also): a zero-length read / write with a null buffer (an empty
// SoundBuffer record, Open with blockCount 0) is memcpy(nullptr, p, 0). It
// succeeds and moves nothing.
UNIT_TEST(cs_bounds_memorystream_zero_count_null_buffer) {
    std::vector<uint8_t> blob(16, 0);
    IO::MemoryStream ms;
    ms._data = blob.data();
    ms._size = 16;
    CHECK(ms.SetCurrentPos(4));
    CHECK(ms.Read(0, nullptr));
    CHECK(ms.Write(0, nullptr));
    CHECK_EQ(ms.GetCurrentPos(), 4u);
    ms._data = nullptr;
}

// ---------------------------------------------------------------------------
// IO::CompressedStream::Open
// ---------------------------------------------------------------------------

// F-032: blockCount -2. Retail's signed `blockCount * 8 > len` admits it,
// SetSize(-2) leaves the directory null, and Read(-16, nullptr) is a wild write.
UNIT_TEST(cs_bounds_open_negative_block_count) {
    Opened o(Hex("feffffff 00000000 00000000 00000000 00000000 00000000"));
    CHECK(!o.ok);
    CHECK_EQ(o.cs._headerSize, 0);
}

// F-016 (also): blockCount 0 reads a zero-byte directory into a null array.
UNIT_TEST(cs_bounds_open_zero_block_count) {
    Opened o(Hex("00000000"));
    CHECK(o.ok);   // retail accepts an empty stream: total 4 == len
    CHECK_EQ(o.cs._headerSize, 0);
}

// F-050: blockCount 0x20000001: blockCount * 8 overflows int32 to 8 and passes
// `8 > len`; retail then sizes a 4 GB directory.
UNIT_TEST(cs_bounds_open_block_count_multiply_overflow) {
    Opened o(Hex("01000020 00000000 00000000"));
    CHECK(!o.ok);
    CHECK_EQ(o.cs._headerSize, 0);
}

// F-052: 65536 blocks put the last one at 0x7fff8000, where start + size and
// the window tests `start + 0x8000` overflow int32. The directory is otherwise
// consistent (every block stored and empty, the last one 0x8000 bytes).
UNIT_TEST(cs_bounds_open_top_block_start_overflow) {
    const int32_t count = 0x10000;
    std::vector<uint8_t> b = Hex("00000100");   // blockCount 0x10000
    const uint32_t dataAt = 4u + static_cast<uint32_t>(count) * 8u;
    for (int32_t i = 0; i < count; ++i) {
        const uint16_t usize = (i == count - 1) ? 0x8000 : 0;
        const uint8_t h[8] = {static_cast<uint8_t>(dataAt), static_cast<uint8_t>(dataAt >> 8),
                              static_cast<uint8_t>(dataAt >> 16), static_cast<uint8_t>(dataAt >> 24),
                              0, 0, static_cast<uint8_t>(usize), static_cast<uint8_t>(usize >> 8)};
        b.insert(b.end(), h, h + 8);
    }
    b.resize(b.size() + 0x8000, 0x42);
    Opened o(b);
    CHECK(!o.ok);
}

// ---------------------------------------------------------------------------
// IO::CompressedStream::DecompressFromSource / SetWindow / Read
// ---------------------------------------------------------------------------

// F-013: compressedSize 0x8400 (> the 0x8000 stack scratch), a valid all-literal
// stream that decodes to exactly 0x8000 bytes. Retail reads 0x8400 bytes into
// the 0x8000 scratch.
UNIT_TEST(cs_bounds_block_compressed_size_over_scratch) {
    std::vector<uint8_t> b = Hex("01000000 0c000000 0084 0080");
    for (int run = 0; run < 1024; ++run) {
        b.push_back(0x1f);
        b.resize(b.size() + 32, static_cast<uint8_t>(run));
    }
    Opened o(b);
    CHECK(o.ok);   // the directory adds up: Open is not where this is caught
    uint8_t out[1] = {};
    CHECK(!o.cs.Read(1, out));
}

// F-014: a STORED block of uncompressedSize 0xffff read straight into the
// 0x8000 window buffer.
UNIT_TEST(cs_bounds_block_stored_size_over_window) {
    std::vector<uint8_t> b = Hex("01000000 0c000000 0000 ffff");
    b.resize(b.size() + 0xffff, 0x42);
    Opened o(b);
    CHECK(o.ok);
    uint8_t out[1] = {};
    CHECK(!o.cs.Read(1, out));
}

// F-015: a 33-byte compressed block claiming uncompressedSize 0xffff: the
// window's end lies past its 0x8000 buffer and Read copies from beyond it.
UNIT_TEST(cs_bounds_block_window_end_past_buffer) {
    std::vector<uint8_t> b = Hex("01000000 0c000000 2100 ffff");
    b.push_back(0x1f);
    b.resize(b.size() + 32, 0x61);
    Opened o(b);
    CHECK(o.ok);
    std::vector<uint8_t> out(0x9000, 0);
    CHECK(!o.cs.Read(0x9000, out.data()));
}

// F-051: a negative position: block -1, _headerData[-1], `-1 << 15`.
UNIT_TEST(cs_bounds_setwindow_negative_position) {
    Opened o(Hex(kShortBlock));
    CHECK(o.ok);
    CHECK(!o.cs.SetWindow(-1));
    CHECK(!o.cs.SetWindow(INT32_MIN));
}

// F-074 / F-075: Read(8) over a 4-byte last block. At position 4 the window is
// still active (4 < 0 + 0x8000) but ends at 4: chunk 0, and retail spins forever.
UNIT_TEST(cs_bounds_read_across_short_block_end) {
    Opened o(Hex(kShortBlock));
    CHECK(o.ok);
    uint8_t out[8] = {};
    CHECK(!o.cs.Read(8, out));
    CHECK(std::memcmp(out, "abcd", 4) == 0);   // what lay inside the block was copied
    CHECK_EQ(o.cs._position, 4);
}

// F-015 (also): position 100 inside the active window, past the 4-byte block's
// end: chunk = 4 - 100 = -96, a negative memcpy.
UNIT_TEST(cs_bounds_read_past_short_block_end_negative_chunk) {
    Opened o(Hex(kShortBlock));
    CHECK(o.ok);
    uint8_t out[4] = {};
    CHECK(o.cs.Read(4, out));
    CHECK(o.cs.SetWindow(100));
    CHECK(!o.cs.Read(1, out));
}

// F-075 (also): pos + n overflows int32 for a huge count (41 + INT32_MAX).
UNIT_TEST(cs_bounds_read_position_plus_count_overflow) {
    Opened o(Hex(kShortBlock));
    CHECK(o.ok);
    uint8_t out[8] = {};
    CHECK(o.cs.Read(1, out));
    CHECK(!o.cs.Read(INT32_MAX, out));   // copies the 3 bytes left, then stops
    CHECK(std::memcmp(out, "bcd", 3) == 0);
}

// Valid data still reads: two full blocks (one LZ, one stored) and a short
// third, through Open, read across both block boundaries to the exact end.
UNIT_TEST(cs_bounds_valid_stream_reads_to_the_end) {
    std::vector<uint8_t> plain(0x8000 * 2 + 100);
    for (size_t i = 0; i < plain.size(); ++i)
        plain[i] = static_cast<uint8_t>((i < 0x8000) ? (i % 7) : (i * 2654435761u >> 13));
    // block 0: a literal run of 7, then one long match at distance 7 per 264 bytes
    std::vector<uint8_t> enc0 = {0x06, 0, 1, 2, 3, 4, 5, 6};
    size_t made = 7;
    while (made < 0x8000) {
        const size_t take = std::min<size_t>(264, 0x8000 - made);
        if (take < 9) {   // a len-7 match is at least 9 bytes
            enc0.push_back(static_cast<uint8_t>(take - 1));
            for (size_t k = 0; k < take; ++k)
                enc0.push_back(plain[made + k]);
        } else {
            enc0.push_back(0xe0);   // len 7 + ext, dist hi 0
            enc0.push_back(static_cast<uint8_t>(take - 2 - 7));
            enc0.push_back(6);      // dist 7
        }
        made += take;
    }
    std::vector<uint8_t> b;
    const uint32_t off0 = 4 + 3 * 8;
    const uint32_t off1 = off0 + static_cast<uint32_t>(enc0.size());
    const uint32_t off2 = off1 + 0x8000;
    auto u32 = [&](uint32_t v) {
        for (int k = 0; k < 4; ++k)
            b.push_back(static_cast<uint8_t>(v >> (8 * k)));
    };
    auto u16 = [&](uint32_t v) {
        b.push_back(static_cast<uint8_t>(v));
        b.push_back(static_cast<uint8_t>(v >> 8));
    };
    u32(3);
    u32(off0);
    u16(static_cast<uint32_t>(enc0.size()));
    u16(0x8000);
    u32(off1);
    u16(0);
    u16(0x8000);
    u32(off2);
    u16(0);
    u16(100);
    Append(b, enc0);
    b.insert(b.end(), plain.begin() + 0x8000, plain.end());
    Opened o(b);
    CHECK(o.ok);
    std::vector<uint8_t> out(plain.size(), 0xee);
    CHECK(o.cs.Read(0x7ff0, out.data()));
    CHECK(o.cs.Read(0x8020, out.data() + 0x7ff0));
    CHECK(o.cs.Read(static_cast<int>(plain.size()) - 0x10010, out.data() + 0x10010));
    CHECK(out == plain);
    CHECK_EQ(o.cs._position, static_cast<int32_t>(plain.size()));
}
