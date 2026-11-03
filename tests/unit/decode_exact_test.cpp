// ============================================================================
// tests/unit/decode_exact_test.cpp -- issue #13: the two exact perf bodies on
//   the package read path are byte-identical to retail's loops
// ============================================================================
// 1. System::Compression::Decompress (src/engine/system/compression.cpp) copies
//    literal runs and matches in memcpy pieces. The oracle below is retail's
//    byte-at-a-time loop (0x1800c4660) as the tree carried it before issue #13,
//    without its dead branch counters. Both run on the same inputs -- real
//    encoder output, truncated streams, short destinations, hand-built
//    overlapping matches, bit-flipped and random garbage -- and must agree on
//    the return value and on EVERY byte of the destination (not only the
//    returned prefix), with the source padded so an overshooting run reads the
//    same bytes in both.
// 2. IO::CompressedStream::ReadArray (compressed_stream_read.cpp), reached from
//    IO::BinaryReader's array slots (serialize_family.cpp), batches the element
//    reads that are a plain copy inside the active window. The oracle is
//    retail's array loop: one Read(sizeof(T)) per element, result ignored. Two
//    identically built CompressedMemoryStreams over the same multi-block
//    package data run the same random sequence of seeks and array reads, one
//    through each body, and after every step the destination, _position,
//    _activeWindow, every window's start / end / useCount / buffer and the log
//    of calls on the SOURCE stream (which is where DecompressFromSource's work
//    shows) must be equal. The sequences read across 32 KB boundaries with
//    1..8-byte elements, past the end of the stream (the failing SetWindow,
//    with elements still to go), and over corrupt blocks.
// ============================================================================
#include "unit.h"

#include "io/Serializer.h"
#include "io/Stream.h"
#include "system/Compression.h"
#include "system/string_internal.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// The oracle: retail's Decompress (0x1800c4660), byte at a time. `overread`
// reports whether it read past srcEnd (the read cursor only moves forward and
// every read advances it, so that is `sp > srcEnd` wherever it stops).
// ---------------------------------------------------------------------------
int RetailDecompressBody(const uint8_t*& sp, const uint8_t* const srcEnd, void* dst, uint32_t dstCap) {
    uint8_t* const dstBase      = static_cast<uint8_t*>(dst);
    uint8_t* const dstEnd       = dstBase + dstCap;
    uint8_t* op                 = dstBase;
    do {
        const uint32_t c = *sp++;
        if (c < 0x20u) {
            uint32_t n = c + 1u;
            if (op + n > dstEnd)
                return 0;
            do {
                *op++ = *sp++;
            } while (--n);
        } else {
            uint32_t len = c >> 5;
            const uint8_t* mp = op - ((c & 0x1fu) << 8) - 1;
            if (len == 7u)
                len = 7u + *sp++;
            mp -= *sp++;
            if (op + 2 + len > dstEnd)
                return 0;
            if (reinterpret_cast<uintptr_t>(mp) < reinterpret_cast<uintptr_t>(dstBase))
                return 0;
            op[0] = mp[0];
            op[1] = mp[1];
            op += 2;
            mp += 2;
            do {
                *op++ = *mp++;
            } while (--len);
        }
    } while (sp < srcEnd);
    return static_cast<int>(op - dstBase);
}

int RetailDecompress(const void* src, uint32_t srcLen, void* dst, uint32_t dstCap, bool* overread) {
    const uint8_t* sp           = static_cast<const uint8_t*>(src);
    const uint8_t* const srcEnd = sp + srcLen;
    const int r = RetailDecompressBody(sp, srcEnd, dst, dstCap);
    *overread = sp > srcEnd;
    return r;
}

constexpr uint32_t kPad = 512;   // readable slack past srcEnd (a run overshoots by <= 33)

struct DecodeTally {
    int cases = 0;
    int ok = 0;        // returned > 0
    int failed = 0;    // returned 0
    int overread = 0;  // retail read past srcEnd: the port must return 0
    int mismatches = 0;
};

// Runs both decoders on src[0..srcLen) (padded) into identically pre-filled
// destinations of dstCap bytes and compares everything. Where retail reads past
// srcEnd (a truncated or corrupt stream, or srcLen 0), the port rejects the
// stream instead (NOT RETAIL, review F-001): it must return 0, and the bytes are
// not compared (retail went on decoding the padding).
void CompareDecode(const std::vector<uint8_t>& srcIn, uint32_t srcLen, uint32_t dstCap,
                   DecodeTally& t, uint8_t fill) {
    std::vector<uint8_t> src(static_cast<size_t>(srcLen) + kPad, 0x5a);
    std::memcpy(src.data(), srcIn.data(), std::min<size_t>(srcLen, srcIn.size()));
    // A cap of 0 still needs a valid pointer; +16 of guard checks no write past dstCap.
    std::vector<uint8_t> a(static_cast<size_t>(dstCap) + 16, fill);
    std::vector<uint8_t> b(static_cast<size_t>(dstCap) + 16, fill);
    bool overread = false;
    const int ra = RetailDecompress(src.data(), srcLen, a.data(), dstCap, &overread);
    const int rb = System::Compression::Decompress(src.data(), srcLen, b.data(), dstCap);
    ++t.cases;
    if (overread) {
        ++t.overread;
        if (rb != 0) {
            if (t.mismatches < 5)
                std::printf("  decode over-read not rejected: srcLen=%u dstCap=%u new=%d\n", srcLen, dstCap, rb);
            ++t.mismatches;
        }
        return;
    }
    if (ra > 0) ++t.ok; else ++t.failed;
    if (ra != rb || a != b) {
        if (t.mismatches < 5)
            std::printf("  decode mismatch: srcLen=%u dstCap=%u retail=%d new=%d\n", srcLen, dstCap, ra, rb);
        ++t.mismatches;
    }
}

std::vector<uint8_t> Encode(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out(in.size() + in.size() / 16 + 64);
    System::Compression c;
    const int n = c.Compress(in.data(), static_cast<uint32_t>(in.size()), out.data(),
                             static_cast<uint32_t>(out.size()));
    RtSystem::RtMemFree(c._table);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    return out;
}

// Inputs of very different shapes: random, small alphabets (short matches at
// every distance), runs (distance 1, extended lengths), text-like repeats.
std::vector<uint8_t> MakeInput(std::mt19937& rng, int kind, size_t n) {
    std::vector<uint8_t> v(n);
    std::uniform_int_distribution<int> byte(0, 255);
    switch (kind) {
    case 0:
        for (auto& x : v) x = static_cast<uint8_t>(byte(rng));
        break;
    case 1:
        for (auto& x : v) x = static_cast<uint8_t>(byte(rng) & 3);
        break;
    case 2: {
        size_t i = 0;
        while (i < n) {
            const uint8_t b = static_cast<uint8_t>(byte(rng));
            size_t run = 1 + static_cast<size_t>(byte(rng)) * (byte(rng) & 7);
            for (; run && i < n; --run) v[i++] = b;
        }
        break;
    }
    default: {
        const char* words[] = {"Banished ", "citizen ", "MaterialInstance ", "\x00\x00\x00\x00",
                               "Template\\Tree.rsc:", "0123456789", "aaaaaaaaaaaaaaaaaaaa"};
        size_t i = 0;
        while (i < n) {
            const char* w = words[byte(rng) % 7];
            const size_t len = (byte(rng) % 7 == 3) ? 4 : std::strlen(w);
            for (size_t k = 0; k < len && i < n; ++k) v[i++] = static_cast<uint8_t>(w[k]);
        }
        break;
    }
    }
    return v;
}

}  // namespace

UNIT_TEST(decode_exact_matches_retail_on_encoder_output) {
    std::mt19937 rng(1301);
    DecodeTally t;
    for (int iter = 0; iter < 400; ++iter) {
        const int kind = iter % 4;
        const size_t n = 1 + static_cast<size_t>(rng() % 0x8000);
        const std::vector<uint8_t> in = MakeInput(rng, kind, n);
        const std::vector<uint8_t> enc = Encode(in);
        if (enc.empty())
            continue;
        const uint32_t elen = static_cast<uint32_t>(enc.size());
        // exact destination, a roomy one, and the round trip
        CompareDecode(enc, elen, static_cast<uint32_t>(n), t, 0xcd);
        CompareDecode(enc, elen, 0x8000, t, 0x11);
        {
            std::vector<uint8_t> src(enc);
            src.resize(enc.size() + kPad, 0);
            std::vector<uint8_t> out(n);
            const int r = System::Compression::Decompress(src.data(), elen, out.data(),
                                                          static_cast<uint32_t>(n));
            CHECK_EQ(r, static_cast<int>(n));
            CHECK(out == in);
        }
        // short destinations: every cap near the end, and a few far below
        for (uint32_t d = 1; d <= 40 && d <= n; ++d)
            CompareDecode(enc, elen, static_cast<uint32_t>(n) - d, t, 0xee);
        CompareDecode(enc, elen, static_cast<uint32_t>(rng() % (n + 1)), t, 0x00);
        // truncated sources
        for (int k = 0; k < 6; ++k)
            CompareDecode(enc, 1 + static_cast<uint32_t>(rng() % elen), static_cast<uint32_t>(n), t, 0x77);
        // bit flips: corrupt control bytes, lengths and offsets
        for (int k = 0; k < 6; ++k) {
            std::vector<uint8_t> bad(enc);
            const int flips = 1 + static_cast<int>(rng() % 4);
            for (int f = 0; f < flips; ++f)
                bad[rng() % bad.size()] ^= static_cast<uint8_t>(1u << (rng() % 8));
            CompareDecode(bad, elen, 0x8000, t, 0x33);
        }
    }
    std::printf("  [decode] encoder corpus: %d cases, %d ok, %d failed, %d over-read (rejected), %d mismatches\n",
                t.cases, t.ok, t.failed, t.overread, t.mismatches);
    CHECK_EQ(t.mismatches, 0);
    CHECK(t.ok > 1000);
    CHECK(t.failed > 1000);
}

UNIT_TEST(decode_exact_overlapping_matches_every_distance_and_length) {
    // Hand-built streams: a literal prefix of 40 distinct bytes, then one match
    // at every distance 1..40 for every length 3..264, then a literal tail.
    DecodeTally t;
    for (uint32_t dist = 1; dist <= 40; ++dist) {
        for (uint32_t total = 3; total <= 264; ++total) {
            std::vector<uint8_t> s;
            s.push_back(0x1f);   // 32 literals
            for (int i = 0; i < 32; ++i) s.push_back(static_cast<uint8_t>(0x40 + i * 3));
            s.push_back(0x07);   // 8 more
            for (int i = 0; i < 8; ++i) s.push_back(static_cast<uint8_t>(0xa0 + i));
            const uint32_t len = total - 2;
            const uint32_t d1 = dist - 1;
            if (len < 7) {
                s.push_back(static_cast<uint8_t>((len << 5) | (d1 >> 8)));
            } else {
                s.push_back(static_cast<uint8_t>((7u << 5) | (d1 >> 8)));
                s.push_back(static_cast<uint8_t>(len - 7));
            }
            s.push_back(static_cast<uint8_t>(d1 & 0xff));
            s.push_back(0x02);
            s.push_back(0x01);
            s.push_back(0x02);
            s.push_back(0x03);
            const uint32_t need = 40 + total + 3;
            CompareDecode(s, static_cast<uint32_t>(s.size()), need, t, 0xcc);
            CompareDecode(s, static_cast<uint32_t>(s.size()), need - 4, t, 0xcc);   // the match itself fails
            CompareDecode(s, static_cast<uint32_t>(s.size()), 0x8000, t, 0xcc);
        }
    }
    // distances that reach before the buffer (mp < dst) by one and by many
    for (uint32_t over = 1; over <= 300; over += 7) {
        std::vector<uint8_t> s = {0x03, 1, 2, 3, 4};
        const uint32_t d1 = 4 + over - 1;
        s.push_back(static_cast<uint8_t>((2u << 5) | (d1 >> 8)));
        s.push_back(static_cast<uint8_t>(d1 & 0xff));
        CompareDecode(s, static_cast<uint32_t>(s.size()), 64, t, 0x99);
    }
    std::printf("  [decode] overlap corpus: %d cases, %d ok, %d failed, %d over-read (rejected), %d mismatches\n",
                t.cases, t.ok, t.failed, t.overread, t.mismatches);
    CHECK_EQ(t.mismatches, 0);
}

UNIT_TEST(decode_exact_random_garbage_and_empty_source) {
    // Garbage is what the .pkm AIO aliasing case feeds the decoder.
    std::mt19937 rng(77);
    DecodeTally t;
    for (int iter = 0; iter < 20000; ++iter) {
        const uint32_t n = 1 + static_cast<uint32_t>(rng() % 600);
        std::vector<uint8_t> s(n);
        for (auto& x : s) x = static_cast<uint8_t>(rng());
        if (iter % 3 == 0)
            for (auto& x : s) x &= 0x3f;   // mostly literals and short near matches
        const uint32_t cap = (iter % 5 == 0) ? 0x8000 : static_cast<uint32_t>(rng() % 2000);
        CompareDecode(s, n, cap, t, static_cast<uint8_t>(iter));
    }
    // srcLen == 0: retail's do-while still decodes one control byte (from the pad); the port rejects it
    for (int c = 0; c < 256; ++c) {
        std::vector<uint8_t> s(1, static_cast<uint8_t>(c));
        CompareDecode(s, 0, 64, t, 0x42);
    }
    std::printf("  [decode] garbage corpus: %d cases, %d ok, %d failed, %d over-read (rejected), %d mismatches\n",
                t.cases, t.ok, t.failed, t.overread, t.mismatches);
    CHECK_EQ(t.mismatches, 0);
    // Most garbage that decoded "ok" in retail did so by reading past srcEnd
    // (the over-read exit); the port rejects those, so few clean decodes remain.
    CHECK(t.ok > 10);
    CHECK(t.overread > 100);
    CHECK(t.failed > 1000);
}

// ===========================================================================
// 2. CompressedStream::ReadArray against retail's per-element loop
// ===========================================================================
namespace {

constexpr int32_t kWin = 0x8000;

// The compressed bytes live here. Every call on it is logged: that log is the
// externally visible trace of DecompressFromSource (which block, when).
struct SourceStream final : IO::Stream {
    std::vector<uint8_t> bytes;
    int32_t pos = 0;
    std::vector<int64_t> log;

    bool Write(int, const void*) override { return false; }
    bool Read(int n, void* dst) override {
        log.push_back((static_cast<int64_t>(pos) << 20) | n);
        if (n < 0 || static_cast<size_t>(pos) + static_cast<size_t>(n) > bytes.size())
            return false;
        std::memcpy(dst, bytes.data() + pos, static_cast<size_t>(n));
        pos += n;
        return true;
    }
    bool SetCurrentPos(int p) override {
        log.push_back(-1 - static_cast<int64_t>(p));
        pos = p;
        return true;
    }
    uint32_t GetCurrentPos() const override { return static_cast<uint32_t>(pos); }
    uint32_t GetSize() const override { return static_cast<uint32_t>(bytes.size()); }
};

struct Fixture {
    SourceStream src;
    std::vector<RtIo::CompressedStreamHeader> headers;
    std::vector<RtIo::CompressedStreamWindow> windows;
    std::vector<std::vector<uint8_t>> buffers;
    alignas(IO::CompressedMemoryStream) unsigned char mem[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* cs = nullptr;

    void Build(const std::vector<uint8_t>& sourceBytes,
               const std::vector<RtIo::CompressedStreamHeader>& hdr, int windowCount) {
        src.bytes = sourceBytes;
        headers = hdr;
        windows.assign(static_cast<size_t>(windowCount), RtIo::CompressedStreamWindow{});
        buffers.assign(static_cast<size_t>(windowCount), std::vector<uint8_t>(kWin, 0));
        for (int i = 0; i < windowCount; ++i) {
            windows[i].useCount = 0;
            windows[i].start = -1;
            windows[i].end = -1;
            windows[i]._pad0c = 0;
            windows[i].buffer = buffers[i].data();
        }
        cs = ::new (static_cast<void*>(mem)) IO::CompressedMemoryStream();
        cs->_direction = 0;
        cs->_position = 0;
        cs->_activeWindow = 0;   // what Open leaves: window 0, empty
        cs->_source = &src;
        cs->_windowData = windows.data();
        cs->_windowSize = windowCount;
        cs->_windowCap = windowCount;
        cs->_headerData = headers.data();
        cs->_headerSize = static_cast<int32_t>(headers.size());
        cs->_headerCap = static_cast<int32_t>(headers.size());
    }
    ~Fixture() {
        if (cs != nullptr) {
            cs->_windowData = nullptr;
            cs->_headerData = nullptr;
            cs->_windowSize = 0;
            cs->_headerSize = 0;
            cs->_source = nullptr;
            cs->IO::CompressedMemoryStream::~CompressedMemoryStream();
        }
    }
};

// Splits `plain` into 32 KB blocks; each is LZ-compressed (or STORED when that
// does not pay), optionally corrupted.
void MakePackage(std::mt19937& rng, const std::vector<uint8_t>& plain, bool corrupt,
                 std::vector<uint8_t>& source, std::vector<RtIo::CompressedStreamHeader>& hdr) {
    source.clear();
    hdr.clear();
    for (size_t off = 0; off < plain.size(); off += kWin) {
        const size_t n = std::min<size_t>(kWin, plain.size() - off);
        std::vector<uint8_t> blk(plain.begin() + static_cast<std::ptrdiff_t>(off),
                                 plain.begin() + static_cast<std::ptrdiff_t>(off + n));
        std::vector<uint8_t> enc = Encode(blk);
        RtIo::CompressedStreamHeader h{};
        h.sourceOffset = static_cast<uint32_t>(source.size());
        h.uncompressedSize = static_cast<uint16_t>(n);
        if (!enc.empty() && enc.size() < n) {
            if (corrupt && (rng() % 3 == 0))
                for (int f = 0; f < 8; ++f) enc[rng() % enc.size()] ^= static_cast<uint8_t>(rng());
            h.compressedSize = static_cast<uint16_t>(enc.size());
            source.insert(source.end(), enc.begin(), enc.end());
        } else {
            h.compressedSize = 0;   // STORED
            source.insert(source.end(), blk.begin(), blk.end());
        }
        hdr.push_back(h);
    }
}

bool SameState(const Fixture& a, const Fixture& b) {
    if (a.cs->_position != b.cs->_position) return false;
    if (a.cs->_activeWindow != b.cs->_activeWindow) return false;
    for (size_t i = 0; i < a.windows.size(); ++i) {
        if (a.windows[i].useCount != b.windows[i].useCount) return false;
        if (a.windows[i].start != b.windows[i].start) return false;
        if (a.windows[i].end != b.windows[i].end) return false;
        if (a.buffers[i] != b.buffers[i]) return false;
    }
    return a.src.log == b.src.log && a.src.pos == b.src.pos;
}

// Retail's array loop for one element size: Read(size) per element, ignored.
void RetailArrayRead(IO::Stream* s, int count, int size, uint8_t* dst) {
    for (int i = 0; i < count; ++i)
        s->Read(size, dst + static_cast<size_t>(i) * static_cast<size_t>(size));
}

struct ReadTally {
    int steps = 0;
    int pastEnd = 0;
    int mismatches = 0;
};

// One random scenario: a package, two fixtures, a sequence of seeks + reads.
void RunScenario(std::mt19937& rng, size_t plainSize, int windowCount, bool corrupt,
                 bool lastBlockFull, ReadTally& t) {
    std::vector<uint8_t> plain = MakeInput(rng, static_cast<int>(rng() % 4), plainSize);
    std::vector<uint8_t> source;
    std::vector<RtIo::CompressedStreamHeader> hdr;
    MakePackage(rng, plain, corrupt, source, hdr);
    Fixture a;
    Fixture b;
    a.Build(source, hdr, windowCount);
    b.Build(source, hdr, windowCount);
    const int64_t total = static_cast<int64_t>(plainSize);
    for (int step = 0; step < 60; ++step) {
        const uint32_t seek = rng() % 8;
        if (seek < 2) {
            // CompressedStream::SetCurrentPos is SetWindow(pos), result dropped;
            // past the end of a full last block that SetWindow fails.
            const uint32_t span = static_cast<uint32_t>(total) + (lastBlockFull ? 2u * kWin : 0u);
            const int32_t p = static_cast<int32_t>(rng() % span);
            a.cs->SetWindow(p);
            b.cs->SetWindow(p);
        }
        // (No raw `_position` stores: with the active window loaded and another
        // window free, retail's SetWindow scan tests the ACTIVE window's start
        // and then accepts the free window (start -1) for any pos < 0x7fff, and
        // Read then memcpys a negative chunk. Only a store that bypasses
        // SetWindow reaches that, and both bodies fault on it alike.)
        if (a.cs->_position >= total && !lastBlockFull)
            continue;
        static const int kSizes[] = {1, 1, 1, 2, 3, 4, 8};
        const int size = kSizes[rng() % 7];
        const int64_t left = total - a.cs->_position;
        int64_t maxCount = left > 0 ? left / size : 0;
        // Reading past the end of a stream whose last block is FULL fails in
        // SetWindow (block >= headerSize) and retail keeps going element by
        // element; past a SHORT last block retail spins forever on a zero
        // chunk, so those reads stay inside the data.
        const bool past = lastBlockFull && (rng() % 6 == 0 || left <= 0);
        if (past) maxCount += 1 + static_cast<int64_t>(rng() % 50);
        if (maxCount <= 0)
            continue;
        int count = static_cast<int>(1 + rng() % static_cast<uint32_t>(std::min<int64_t>(maxCount, 120000)));
        if (past) count = static_cast<int>(maxCount);
        std::vector<uint8_t> da(static_cast<size_t>(count) * size, 0xab);
        std::vector<uint8_t> db(static_cast<size_t>(count) * size, 0xab);
        a.cs->ReadArray(count, size, da.data());
        RetailArrayRead(b.cs, count, size, db.data());
        ++t.steps;
        if (past) ++t.pastEnd;
        if (da != db || !SameState(a, b)) {
            if (t.mismatches < 5)
                std::printf("  ReadArray mismatch: size=%d count=%d windows=%d corrupt=%d\n", size, count,
                            windowCount, corrupt ? 1 : 0);
            ++t.mismatches;
            return;
        }
        if (!corrupt && !past) {
            // and the bytes are the package's
            const int64_t start = a.cs->_position - static_cast<int64_t>(count) * size;
            if (start >= 0 && std::memcmp(da.data(), plain.data() + start, da.size()) != 0) {
                ++t.mismatches;
                return;
            }
        }
    }
}

}  // namespace

UNIT_TEST(decode_exact_readarray_matches_per_element_reads) {
    std::mt19937 rng(4242);
    ReadTally t;
    for (int sc = 0; sc < 60; ++sc) {
        const int windows = 1 + sc % 3;
        const bool corrupt = (sc % 5 == 4);
        const bool full = (sc % 2 == 0);
        const size_t blocks = 1 + static_cast<size_t>(rng() % 6);
        size_t size = blocks * kWin;
        if (!full) size -= 1 + rng() % (kWin - 1);
        RunScenario(rng, size, windows, corrupt, full, t);
    }
    std::printf("  [readarray] %d read steps (%d past the end), %d mismatches\n", t.steps, t.pastEnd,
                t.mismatches);
    CHECK_EQ(t.mismatches, 0);
    CHECK(t.steps > 1000);
    CHECK(t.pastEnd > 50);
}

// The slot itself: BinaryReader's array forms over a real CompressedMemoryStream
// against its own scalar slot called per element (retail's array body).
UNIT_TEST(decode_exact_binaryreader_array_slots_match_scalar_loop) {
    std::mt19937 rng(9);
    std::vector<uint8_t> plain = MakeInput(rng, 3, 3 * kWin + 1000);
    std::vector<uint8_t> source;
    std::vector<RtIo::CompressedStreamHeader> hdr;
    MakePackage(rng, plain, false, source, hdr);
    Fixture a;
    Fixture b;
    a.Build(source, hdr, 2);
    b.Build(source, hdr, 2);

    // BinaryReader's constructor consumes a record header; build each over a
    // throwaway stream and then point it at the fixture.
    SourceStream hdrA;
    SourceStream hdrB;
    const int32_t rec[4] = {static_cast<int32_t>(0xaaaaaaaau), 8, 1, 0};
    hdrA.bytes.assign(reinterpret_cast<const uint8_t*>(rec), reinterpret_cast<const uint8_t*>(rec) + 16);
    hdrB.bytes = hdrA.bytes;
    alignas(8) uint8_t sa[16] = {};
    alignas(8) uint8_t sb[16] = {};
    IO::Serializer& serA = *reinterpret_cast<IO::Serializer*>(sa);
    IO::Serializer& serB = *reinterpret_cast<IO::Serializer*>(sb);
    serA._stream = &hdrA;
    serB._stream = &hdrB;
    IO::BinaryReader ra(serA, 0);
    IO::BinaryReader rb(serB, 0);
    ra.stream = a.cs;
    rb.stream = b.cs;

    int mismatches = 0;
    for (int step = 0; step < 200 && mismatches == 0; ++step) {
        const int remaining = static_cast<int>(plain.size()) - a.cs->_position;
        if (remaining < 64) {
            a.cs->_position = 0;
            b.cs->_position = 0;
            continue;
        }
        const int kind = static_cast<int>(rng() % 4);
        const int bytes = 1 + static_cast<int>(rng() % static_cast<uint32_t>(std::min(remaining, 50000)));
        std::vector<uint8_t> da(static_cast<size_t>(bytes) + 8, 0x5c);
        std::vector<uint8_t> db(static_cast<size_t>(bytes) + 8, 0x5c);
        if (kind == 0) {
            ra.SerializeUCharArray(bytes, da.data(), "x");
            for (int i = 0; i < bytes; ++i) rb.SerializeUChar(db[static_cast<size_t>(i)], nullptr);
        } else if (kind == 1) {
            ra.SerializeCharArray(bytes, reinterpret_cast<char*>(da.data()), nullptr);
            for (int i = 0; i < bytes; ++i) rb.SerializeChar(reinterpret_cast<char*>(db.data())[i], nullptr);
        } else if (kind == 2) {
            const int n = bytes / 2;
            uint16_t* pa = reinterpret_cast<uint16_t*>(da.data());
            uint16_t* pb = reinterpret_cast<uint16_t*>(db.data());
            ra.SerializeUShortArray(n, pa, nullptr);
            for (int i = 0; i < n; ++i) rb.SerializeUShort(pb[i], nullptr);
        } else {
            const int n = bytes / 4;
            float* pa = reinterpret_cast<float*>(da.data());
            float* pb = reinterpret_cast<float*>(db.data());
            ra.SerializeFloatArray(n, pa, nullptr);
            for (int i = 0; i < n; ++i) rb.SerializeFloat(pb[i], nullptr);
        }
        if (da != db || !SameState(a, b))
            ++mismatches;
    }
    // the count guard: nothing read for count <= 0
    const int32_t before = a.cs->_position;
    uint8_t dummy[4] = {};
    ra.SerializeUCharArray(0, dummy, nullptr);
    ra.SerializeIntArray(-5, reinterpret_cast<int32_t*>(dummy), nullptr);
    CHECK_EQ(a.cs->_position, before);
    CHECK_EQ(mismatches, 0);

    ra.stream = &hdrA;   // the destructors read their 4-byte trailers
    rb.stream = &hdrB;
}
