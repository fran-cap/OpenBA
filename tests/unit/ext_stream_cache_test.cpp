// ============================================================================
// tests/unit/ext_stream_cache_test.cpp -- ext.stream_cache's records, bundles and record stream
// ============================================================================
// Ext::StreamCache (src/ext/stream_cache/) over synthetic streams:
//   * a recording merges adjacent and overlapping reads (the overlap must agree), keeps gaps as
//     separate segments, and the record answers exactly the recorded ranges (a read across a gap
//     or past a segment fails);
//   * a failed read, a failed load, an empty recording, an overlap mismatch or more than kRecordMax
//     bytes drop the record; a recording seeded from a record widens it;
//   * a chunk of several records round-trips through a Bundle, is found by its offset and size, a
//     later chunk's record replaces an earlier one, and a malformed chunk is refused whole;
//   * a record carries the entry's block count and directory byte for byte (through widening and
//     consolidation), a directory that disagrees with the total or the entry is refused, and the
//     package identity changes with every field it is made of (an identical copy with a new
//     modification time included: one cold run, then warm again);
//   * a SegmentStream serves the record's bytes in the decompressed stream's coordinates, and a
//     read outside the record falls back to the engine's own CompressedStream over the same entry
//     (positioned where the read was) for the rest of the load; without an opener it fails;
//   * a bundle consolidates into one chunk of exactly its live records, byte for byte, when its
//     superseded records reach a quarter of it; the generation marker round-trips; reading back
//     follows the newest marker whose chunk is stored, never asks for the chunks below it, and
//     falls back to an older marker or to ordinal 0 when the marked chunk is missing.
// ============================================================================
#include "unit.h"

#include "ext/stream_cache/segment_stream.h"
#include "ext/stream_cache/stream_record.h"
#include "io/Stream.h"
#include "system/Compression.h"
#include "system/string_internal.h"   // RtSystem::RtMemFree: the table Compress allocated

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace Ext::StreamCache;

constexpr int kBlockBytes = 0x8000;

std::vector<uint8_t> Pattern(uint32_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 7 + (i >> 9));
    return v;
}

EntryId Id(uint64_t offset) {
    EntryId id;
    id.offset = offset;
    id.size   = 1000;
    return id;
}

// An entry's first 4 + 8n bytes for a stream of `total` decompressed bytes: n = the 32 KB blocks,
// each stored after the directory (sourceOffset), compressed sizes arbitrary but nonzero.
std::vector<uint8_t> Head(uint32_t total) {
    const uint32_t n = total == 0 ? 1 : (total + kBlockBytes - 1) / kBlockBytes;
    std::vector<uint8_t> h(4 + static_cast<size_t>(n) * 8);
    std::memcpy(h.data(), &n, 4);
    for (uint32_t b = 0; b < n; ++b) {
        const uint32_t src = 4 + n * 8 + b * 1000;
        const uint16_t c16 = static_cast<uint16_t>(500 + b);
        const uint16_t u16 = static_cast<uint16_t>(b + 1 < n ? kBlockBytes : total - b * kBlockBytes);
        std::memcpy(h.data() + 4 + 8 * b, &src, 4);
        std::memcpy(h.data() + 4 + 8 * b + 4, &c16, 2);
        std::memcpy(h.data() + 4 + 8 * b + 6, &u16, 2);
    }
    return h;
}

void BeginT(Recorder& r, const EntryId& id, uint32_t total) {
    const std::vector<uint8_t> h = Head(total);
    r.Begin(id, total, h.data());
}

// A recorder fed the reads [a,b) of `plain` in the given order; the record appended to `chunk`.
bool RecordReads(Recorder& r, const EntryId& id, const std::vector<uint8_t>& plain,
                 const std::vector<std::pair<uint32_t, uint32_t>>& reads, std::vector<uint8_t>& chunk) {
    BeginT(r, id, static_cast<uint32_t>(plain.size()));
    for (const auto& rd : reads) r.Note(rd.first, rd.second, plain.data() + rd.first);
    return r.Finish(chunk);
}

void PutI32(std::vector<uint8_t>& v, size_t at, int32_t x) {
    std::memcpy(v.data() + at, &x, 4);
}

// A compressed entry as the packer writes it: the block count, the directory, the blocks.
std::vector<uint8_t> BuildEntry(const std::vector<uint8_t>& plain) {
    const int n = static_cast<int>((plain.size() + kBlockBytes - 1) / kBlockBytes);
    std::vector<uint8_t> out(4 + static_cast<size_t>(n) * 8);
    PutI32(out, 0, n);
    System::Compression comp;
    std::vector<uint8_t> packed(kBlockBytes + 64);
    for (int b = 0; b < n; ++b) {
        const size_t from = static_cast<size_t>(b) * kBlockBytes;
        const uint32_t len = static_cast<uint32_t>(std::min<size_t>(kBlockBytes, plain.size() - from));
        const int csize = comp.Compress(plain.data() + from, len, packed.data(), len);
        const uint32_t offset = static_cast<uint32_t>(out.size());
        uint16_t c16 = 0;
        if (csize > 0 && csize < static_cast<int>(len)) {
            c16 = static_cast<uint16_t>(csize);
            out.insert(out.end(), packed.begin(), packed.begin() + csize);
        } else {
            out.insert(out.end(), plain.begin() + static_cast<std::ptrdiff_t>(from),
                       plain.begin() + static_cast<std::ptrdiff_t>(from + len));
        }
        const uint16_t u16 = static_cast<uint16_t>(len);
        std::memcpy(out.data() + 4 + 8 * b, &offset, 4);
        std::memcpy(out.data() + 4 + 8 * b + 4, &c16, 2);
        std::memcpy(out.data() + 4 + 8 * b + 6, &u16, 2);
    }
    if (comp._table != nullptr) ::RtSystem::RtMemFree(comp._table);
    comp._table = nullptr;
    return out;
}

// An entry's bytes served as FileStream serves a packed entry.
struct EntryBytes final : IO::Stream {
    const uint8_t* base = nullptr;
    int32_t size = 0;
    int32_t pos  = 0;
    bool Write(int, const void*) override { return false; }
    bool Read(int n, void* dst) override {
        if (n < 0 || pos + n > size) return false;
        std::memcpy(dst, base + pos, static_cast<size_t>(n));
        pos += n;
        return true;
    }
    bool SetCurrentPos(int p) override {
        pos = p;
        return true;
    }
    uint32_t GetCurrentPos() const override { return static_cast<uint32_t>(pos); }
    uint32_t GetSize() const override { return static_cast<uint32_t>(size); }
};

// The engine's reader over one entry: a CompressedMemoryStream in the state CompressedStream::Open
// leaves, with External::Load's one window.
struct EntryReader {
    EntryBytes src;
    std::vector<RtIo::CompressedStreamHeader> headers;
    std::vector<RtIo::CompressedStreamWindow> windows;
    std::vector<uint8_t> buffer;
    alignas(IO::CompressedMemoryStream) unsigned char mem[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* cs = nullptr;
    int opened = 0;

    void Open(const std::vector<uint8_t>& entry) {
        ++opened;
        src.base = entry.data();
        src.size = static_cast<int32_t>(entry.size());
        src.pos  = 0;
        int32_t n = 0;
        std::memcpy(&n, entry.data(), 4);
        headers.resize(static_cast<size_t>(n));
        std::memcpy(headers.data(), entry.data() + 4, static_cast<size_t>(n) * 8);
        windows.assign(1, RtIo::CompressedStreamWindow{});
        buffer.assign(kBlockBytes, 0);
        windows[0].useCount = 0;
        windows[0].start    = -1;
        windows[0].end      = -1;
        windows[0]._pad0c   = 0;
        windows[0].buffer   = buffer.data();
        cs = ::new (static_cast<void*>(mem)) IO::CompressedMemoryStream();
        cs->_direction    = 0;
        cs->_position     = 0;
        cs->_activeWindow = 0;
        cs->_source       = &src;
        cs->_windowData   = windows.data();
        cs->_windowSize   = 1;
        cs->_windowCap    = 1;
        cs->_headerData   = headers.data();
        cs->_headerSize   = n;
        cs->_headerCap    = n;
    }
    ~EntryReader() {
        if (cs == nullptr) return;
        cs->_windowData = nullptr;
        cs->_headerData = nullptr;
        cs->_windowSize = 0;
        cs->_headerSize = 0;
        cs->_source     = nullptr;
        cs->IO::CompressedMemoryStream::~CompressedMemoryStream();
    }
};

struct FallbackContext {
    EntryReader reader;
    const std::vector<uint8_t>* entry = nullptr;
};

IO::CompressedStream* OpenReader(SegmentStream& self, void* context) {
    (void)self;
    FallbackContext* const c = static_cast<FallbackContext*>(context);
    c->reader.Open(*c->entry);
    return c->reader.cs;
}

}  // namespace

UNIT_TEST(ext_stream_cache_record_merges_and_reads) {
    const std::vector<uint8_t> plain = Pattern(100000, 3);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    // header 0..12 in three reads, then a gap (a skipped payload), a 4-byte size field, a gap, a tail.
    CHECK(RecordReads(r, Id(4096), plain, {{0, 4}, {4, 4}, {8, 4}, {70000, 4}, {99990, 10}}, chunk));
    EndChunk(chunk, 1);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    CHECK_EQ(b.Records(), static_cast<size_t>(1));
    const RecordView* v = b.Find(Id(4096));
    CHECK(v != nullptr);
    CHECK_EQ(v->segCount, 3u);
    CHECK_EQ(v->segs[0].pos, 0u);
    CHECK_EQ(v->segs[0].len, 12u);
    CHECK_EQ(v->segs[1].pos, 70000u);
    CHECK_EQ(v->segs[2].len, 10u);
    CHECK_EQ(v->total, 100000u);
    CHECK_EQ(v->byteCount, 26u);
    uint8_t out[16];
    CHECK(v->Read(0, 12, out));
    CHECK(std::memcmp(out, plain.data(), 12) == 0);
    CHECK(v->Read(6, 6, out));
    CHECK(std::memcmp(out, plain.data() + 6, 6) == 0);
    CHECK(v->Read(70000, 4, out));
    CHECK(std::memcmp(out, plain.data() + 70000, 4) == 0);
    CHECK(v->Read(99995, 5, out));
    CHECK(std::memcmp(out, plain.data() + 99995, 5) == 0);
    CHECK(v->Read(99990, 0, out));
    CHECK(!v->Read(10, 4, out));       // past the first segment
    CHECK(!v->Read(12, 1, out));       // inside the gap
    CHECK(!v->Read(69999, 2, out));    // across a gap's end
    CHECK(!v->Read(99995, 6, out));    // past the total
}

UNIT_TEST(ext_stream_cache_record_overlaps_merge_or_drop) {
    const std::vector<uint8_t> plain = Pattern(20000, 9);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    // out of order, overlapping and touching reads: one segment 0..40 and one 100..120
    CHECK(RecordReads(r, Id(1), plain, {{8, 8}, {0, 10}, {30, 10}, {16, 20}, {100, 10}, {110, 10}}, chunk));
    EndChunk(chunk, 1);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    const RecordView* v = b.Find(Id(1));
    CHECK(v != nullptr);
    CHECK_EQ(v->segCount, 2u);
    CHECK_EQ(v->segs[0].pos, 0u);
    CHECK_EQ(v->segs[0].len, 40u);
    CHECK_EQ(v->segs[1].pos, 100u);
    CHECK_EQ(v->segs[1].len, 20u);
    CHECK_EQ(v->byteCount, 60u);
    std::vector<uint8_t> out(40);
    CHECK(v->Read(0, 40, out.data()));
    CHECK(std::memcmp(out.data(), plain.data(), 40) == 0);
    // an overlap whose bytes disagree drops the record
    std::vector<uint8_t> other = plain;
    other[5] ^= 0xff;
    BeginT(r, Id(2), 20000);
    r.Note(0, 10, plain.data());
    r.Note(4, 10, other.data() + 4);
    CHECK(r.Bad());
    std::vector<uint8_t> chunk2;
    BeginChunk(chunk2);
    CHECK(!r.Finish(chunk2));
    CHECK_EQ(chunk2.size(), static_cast<size_t>(kChunkHeader));
}

UNIT_TEST(ext_stream_cache_record_dropped_when_not_storable) {
    const std::vector<uint8_t> plain = Pattern(20000, 9);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    const size_t header = chunk.size();
    Recorder r;
    // a failed read
    BeginT(r, Id(2), 20000);
    r.Note(0, 4, plain.data());
    r.Fail();
    CHECK(!r.Finish(chunk));
    CHECK_EQ(chunk.size(), header);
    // over the limit
    BeginT(r, Id(3), 20000);
    r.Note(0, kRecordMax, plain.data());
    CHECK(r.Storable());
    r.Note(kRecordMax + 8, 1, plain.data() + kRecordMax + 8);
    CHECK(r.Overflowed());
    CHECK(!r.Finish(chunk));
    CHECK_EQ(chunk.size(), header);
    // over the limit by widening
    BeginT(r, Id(3), 20000);
    r.Note(0, kRecordMax, plain.data());
    r.Note(kRecordMax - 4, 8, plain.data() + kRecordMax - 4);
    CHECK(r.Overflowed());
    // nothing read
    BeginT(r, Id(4), 20000);
    CHECK(!r.Finish(chunk));
    // exactly the limit, as one read
    CHECK(RecordReads(r, Id(5), plain, {{0, kRecordMax}}, chunk));
    CHECK(chunk.size() > header);
}

UNIT_TEST(ext_stream_cache_seeded_recording_widens_a_record) {
    const std::vector<uint8_t> plain = Pattern(50000, 2);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    CHECK(RecordReads(r, Id(7), plain, {{0, 16}, {40000, 8}}, chunk));
    EndChunk(chunk, 1);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    const RecordView* old = b.Find(Id(7));
    CHECK(old != nullptr);
    // the next build reads the header, 8 more bytes after it, a new field at 20000 and the tail
    r.Seed(*old);
    CHECK_EQ(r.Consumed(), 24u);
    r.Note(0, 16, plain.data());
    r.Note(16, 8, plain.data() + 16);
    r.Note(20000, 4, plain.data() + 20000);
    r.Note(40000, 8, plain.data() + 40000);
    r.Note(40008, 4, plain.data() + 40008);
    std::vector<uint8_t> chunk2;
    BeginChunk(chunk2);
    CHECK(r.Finish(chunk2));
    EndChunk(chunk2, 1);
    CHECK(b.AddChunk(chunk2));
    CHECK_EQ(b.Records(), static_cast<size_t>(1));   // replaced, not added
    const RecordView* wide = b.Find(Id(7));
    CHECK(wide != nullptr);
    CHECK_EQ(wide->segCount, 3u);
    CHECK_EQ(wide->segs[0].len, 24u);
    CHECK_EQ(wide->segs[1].pos, 20000u);
    CHECK_EQ(wide->segs[2].pos, 40000u);
    CHECK_EQ(wide->segs[2].len, 12u);
    CHECK_EQ(wide->byteCount, 40u);
    uint8_t out[24];
    CHECK(wide->Read(0, 24, out));
    CHECK(std::memcmp(out, plain.data(), 24) == 0);
    CHECK(wide->Read(40000, 12, out));
    CHECK(std::memcmp(out, plain.data() + 40000, 12) == 0);
}

UNIT_TEST(ext_stream_cache_bundle_round_trip_and_identity) {
    const std::vector<uint8_t> plain = Pattern(5000, 1);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    uint32_t count = 0;
    for (uint64_t off = 100; off < 100 + 50 * 64; off += 64) {
        // a whole stream: one read of everything
        CHECK(RecordReads(r, Id(off), plain, {{0, 5000}}, chunk));
        ++count;
    }
    CHECK(RecordReads(r, Id(9999), plain, {{0, 16}, {4000, 8}}, chunk));
    ++count;
    EndChunk(chunk, count);
    CHECK_EQ(ChunkRecordCount(chunk), count);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    CHECK_EQ(b.Records(), static_cast<size_t>(count));
    CHECK_EQ(b.Chunks(), static_cast<size_t>(1));
    const RecordView* whole = b.Find(Id(100 + 64 * 7));
    CHECK(whole != nullptr);
    CHECK_EQ(whole->segCount, 1u);
    CHECK_EQ(whole->byteCount, whole->total);
    std::vector<uint8_t> out(5000);
    CHECK(whole->Read(0, 5000, out.data()));
    CHECK(out == plain);
    // within its package the identity is the offset and the size
    EntryId other = Id(100);
    other.size = 1001;
    CHECK(b.Find(other) == nullptr);
    CHECK(b.Find(Id(101)) == nullptr);
    CHECK(b.Find(Id(100)) != nullptr);
    // a second chunk adds records and the first stays
    std::vector<uint8_t> chunk2;
    BeginChunk(chunk2);
    CHECK(RecordReads(r, Id(777), plain, {{0, 100}}, chunk2));
    EndChunk(chunk2, 1);
    CHECK(b.AddChunk(chunk2));
    CHECK_EQ(b.Records(), static_cast<size_t>(count + 1));
    CHECK(b.Find(Id(777)) != nullptr);
    CHECK(b.Find(Id(9999)) != nullptr);
    b.Clear();
    CHECK_EQ(b.Records(), static_cast<size_t>(0));
    CHECK_EQ(b.Bytes(), static_cast<size_t>(0));
    CHECK(b.Find(Id(777)) == nullptr);
}

UNIT_TEST(ext_stream_cache_bundle_refuses_malformed_chunks) {
    const std::vector<uint8_t> plain = Pattern(3000, 5);
    std::vector<uint8_t> good;
    BeginChunk(good);
    Recorder r;
    CHECK(RecordReads(r, Id(1), plain, {{0, 8}, {2000, 8}}, good));
    CHECK(RecordReads(r, Id(2), plain, {{0, 3000}}, good));
    EndChunk(good, 2);
    Bundle b;
    // bad magic
    std::vector<uint8_t> bad = good;
    bad[0] ^= 0xff;
    CHECK(!b.AddChunk(bad));
    // a count past the end
    bad = good;
    bad[8] = 9;
    CHECK(!b.AddChunk(bad));
    // truncated
    bad = good;
    bad.resize(bad.size() - 8);
    CHECK(!b.AddChunk(bad));
    // segments out of order: swap the first record's two segments
    bad = good;
    {
        uint8_t tmp[8];
        const size_t segs = kChunkHeader + kRecordHeader + 8;   // past the one-block directory
        std::memcpy(tmp, bad.data() + segs, 8);
        std::memcpy(bad.data() + segs, bad.data() + segs + 8, 8);
        std::memcpy(bad.data() + segs + 8, tmp, 8);
    }
    CHECK(!b.AddChunk(bad));
    // empty and header-only
    CHECK(!b.AddChunk(std::vector<uint8_t>()));
    std::vector<uint8_t> empty;
    BeginChunk(empty);
    EndChunk(empty, 0);
    CHECK(!b.AddChunk(empty));
    CHECK_EQ(b.Records(), static_cast<size_t>(0));
    // the good one still parses after all that
    CHECK(b.AddChunk(good));
    CHECK_EQ(b.Records(), static_cast<size_t>(2));
}

UNIT_TEST(ext_stream_cache_segment_stream_serves_then_falls_back) {
    const std::vector<uint8_t> plain = Pattern(70000, 11);   // three blocks, the last short
    const std::vector<uint8_t> entry = BuildEntry(plain);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    CHECK(RecordReads(r, Id(5), plain, {{0, 100}, {65000, 100}}, chunk));
    EndChunk(chunk, 1);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    const RecordView* rec = b.Find(Id(5));
    CHECK(rec != nullptr);

    FallbackContext ctx;
    ctx.entry = &entry;
    SegmentStream ss(rec, nullptr, 5, &OpenReader, &ctx);
    CHECK_EQ(ss.GetDirection(), IO::kReading);
    CHECK_EQ(ss.GetSize(), 70000u);
    std::vector<uint8_t> out(200);
    // the header from the record, in two reads
    CHECK(ss.Read(50, out.data()));
    CHECK(ss.Read(50, out.data() + 50));
    CHECK(std::memcmp(out.data(), plain.data(), 100) == 0);
    CHECK_EQ(ss.GetCurrentPos(), 100u);
    // a skipped payload, then the tail field from the record
    ss.Skip(64900);
    CHECK_EQ(ss.GetCurrentPos(), 65000u);
    CHECK(ss.Read(100, out.data()));
    CHECK(std::memcmp(out.data(), plain.data() + 65000, 100) == 0);
    CHECK(!ss.Extended());
    CHECK_EQ(ss.Consumed(), 200u);
    CHECK_EQ(ctx.reader.opened, 0);
    // a read the record never saw: the engine's stream takes over at that position
    CHECK(ss.Read(100, out.data()));
    CHECK(ss.Extended());
    CHECK_EQ(ctx.reader.opened, 1);
    CHECK(std::memcmp(out.data(), plain.data() + 65100, 100) == 0);
    CHECK_EQ(ss.GetCurrentPos(), 65200u);
    // and serves everything from then on: a read across the block boundary at 65536, a skip, a read
    // (SetCurrentPos / GetSize forward to the engine stream, which the unit build stubs: not called here)
    std::vector<uint8_t> big(400);
    CHECK(ss.Read(400, big.data()));
    CHECK(std::memcmp(big.data(), plain.data() + 65200, 400) == 0);
    CHECK_EQ(ss.GetCurrentPos(), 65600u);
    ss.Skip(100);
    CHECK_EQ(ss.GetCurrentPos(), 65700u);
    CHECK(ss.Read(4, out.data()));
    CHECK(std::memcmp(out.data(), plain.data() + 65700, 4) == 0);
    CHECK(!ss.Failed());
    CHECK_EQ(ss.Consumed(), 200u);   // the record's share only

    // no opener: the read outside the record fails and the stream is marked
    SegmentStream bare(rec, nullptr, 5, nullptr, nullptr);
    CHECK(bare.Read(100, out.data()));
    CHECK(!bare.Read(100, out.data()));
    CHECK(bare.Failed());
    CHECK(!bare.Extended());
}

namespace {

// Every record of `a` is in `b` with the same identity, total, segments and bytes.
bool SameRecords(const Bundle& a, const Bundle& b, const std::vector<EntryId>& ids) {
    if (a.Records() != b.Records()) return false;
    for (const EntryId& id : ids) {
        const RecordView* x = a.Find(id);
        const RecordView* y = b.Find(id);
        if ((x == nullptr) != (y == nullptr)) return false;
        if (x == nullptr) continue;
        if (x->total != y->total || x->segCount != y->segCount || x->byteCount != y->byteCount) return false;
        if (std::memcmp(x->segs, y->segs, static_cast<size_t>(x->segCount) * 8) != 0) return false;
        if (std::memcmp(x->bytes, y->bytes, x->byteCount) != 0) return false;
    }
    return true;
}

// A package's chunks and markers as the derived cache holds them, with the chunk ordinals asked for.
struct FakeStore {
    std::vector<std::vector<uint8_t>> chunks;   // by ordinal; empty = not stored
    std::vector<std::vector<uint8_t>> markers;
    std::vector<uint32_t> chunkAsks;

    bool FindChunk(uint32_t i, std::vector<uint8_t>& out) {
        chunkAsks.push_back(i);
        if (i >= chunks.size() || chunks[i].empty()) return false;
        out = chunks[i];
        return true;
    }
    bool FindMarker(uint32_t i, std::vector<uint8_t>& out) {
        if (i >= markers.size() || markers[i].empty()) return false;
        out = markers[i];
        return true;
    }
    ReadBackResult Read(Bundle& b) {
        chunkAsks.clear();
        return ReadBack(
            b, [this](uint32_t i, std::vector<uint8_t>& out) { return FindChunk(i, out); },
            [this](uint32_t i, std::vector<uint8_t>& out) { return FindMarker(i, out); });
    }
};

// Generation 0: whole-stream records (300 bytes) for 40 entries. Generation 1: every one of them
// widened by one more segment, as a run that read outside its records makes.
void TwoGenerations(const std::vector<uint8_t>& plain, std::vector<uint8_t>& gen0, std::vector<uint8_t>& gen1,
                    std::vector<EntryId>& ids) {
    Recorder r;
    BeginChunk(gen0);
    BeginChunk(gen1);
    for (uint64_t off = 1; off <= 40; ++off) {
        ids.push_back(Id(off * 64));
        CHECK(RecordReads(r, Id(off * 64), plain, {{0, 300}}, gen0));
        CHECK(RecordReads(r, Id(off * 64), plain, {{0, 300}, {4000 + static_cast<uint32_t>(off), 16}}, gen1));
    }
    EndChunk(gen0, 40);
    EndChunk(gen1, 40);
}

}  // namespace

UNIT_TEST(ext_stream_cache_consolidate_keeps_exactly_the_live_records) {
    const std::vector<uint8_t> plain = Pattern(9000, 9);
    std::vector<uint8_t> gen0;
    std::vector<uint8_t> gen1;
    std::vector<EntryId> ids;
    TwoGenerations(plain, gen0, gen1, ids);
    // a third chunk: half the entries replaced again, and one new entry
    std::vector<uint8_t> gen2;
    BeginChunk(gen2);
    Recorder r;
    for (uint64_t off = 1; off <= 20; ++off) {
        CHECK(RecordReads(r, Id(off * 64), plain, {{0, 300}, {6000, 32}}, gen2));
    }
    ids.push_back(Id(50000));
    CHECK(RecordReads(r, Id(50000), plain, {{0, 8000}}, gen2));
    EndChunk(gen2, 21);
    Bundle b;
    CHECK(b.AddChunk(gen0));
    CHECK(!ShouldConsolidate(b));   // one chunk: nothing to merge
    CHECK(b.AddChunk(gen1));
    CHECK(ShouldConsolidate(b));    // generation 0 is all dead: under half the bytes are live
    CHECK(b.AddChunk(gen2));
    CHECK_EQ(b.Records(), static_cast<size_t>(41));
    CHECK_EQ(b.Chunks(), static_cast<size_t>(3));
    const std::vector<uint8_t> merged = b.Consolidate();
    CHECK_EQ(ChunkRecordCount(merged), 41u);
    CHECK_EQ(merged.size(), static_cast<size_t>(kChunkHeader) + b.LiveBytes());
    CHECK(merged.size() < b.Bytes());
    Bundle c;
    CHECK(c.AddChunk(merged));
    CHECK_EQ(c.Chunks(), static_cast<size_t>(1));
    CHECK(SameRecords(b, c, ids));
    CHECK_EQ(c.LiveBytes(), b.LiveBytes());
    CHECK(!ShouldConsolidate(c));
    // the newest record of each entry won: 1..20 from gen2, 21..40 from gen1
    uint8_t out[32];
    CHECK(c.Find(Id(64 * 3))->Read(6000, 32, out));
    CHECK(std::memcmp(out, plain.data() + 6000, 32) == 0);
    CHECK(c.Find(Id(64 * 30))->Read(4030, 16, out));
    CHECK(std::memcmp(out, plain.data() + 4030, 16) == 0);
    CHECK(!c.Find(Id(64 * 30))->Read(6000, 32, out));
    // consolidating the consolidated chunk gives the same bytes
    CHECK(c.Consolidate() == merged);
    // all-live chunks are not rewritten, until there are more than kConsolidateChunks of them
    Bundle d;
    for (uint64_t i = 1; i <= 2; ++i) {
        std::vector<uint8_t> one;
        BeginChunk(one);
        CHECK(RecordReads(r, Id(i), plain, {{0, 100}}, one));
        EndChunk(one, 1);
        CHECK(d.AddChunk(one));
    }
    CHECK(!ShouldConsolidate(d));
    for (uint64_t i = 3; i <= kConsolidateChunks + 1; ++i) {
        std::vector<uint8_t> more;
        BeginChunk(more);
        CHECK(RecordReads(r, Id(i), plain, {{0, 100}}, more));
        EndChunk(more, 1);
        CHECK(d.AddChunk(more));
    }
    CHECK_EQ(d.Chunks(), static_cast<size_t>(kConsolidateChunks + 1));
    CHECK(ShouldConsolidate(d));
    CHECK(Bundle().Consolidate().empty());
}

UNIT_TEST(ext_stream_cache_marker_round_trip) {
    uint32_t start = 99;
    const std::vector<uint8_t> m = EncodeMarker(7);
    CHECK_EQ(m.size(), static_cast<size_t>(kMarkerBytes));
    CHECK(DecodeMarker(m, start));
    CHECK_EQ(start, 7u);
    CHECK(DecodeMarker(EncodeMarker(0), start));
    CHECK_EQ(start, 0u);
    std::vector<uint8_t> bad = m;
    bad[8] ^= 1;   // the start no longer matches its complement
    start = 99;
    CHECK(!DecodeMarker(bad, start));
    CHECK_EQ(start, 99u);
    bad = m;
    bad[0] ^= 1;
    CHECK(!DecodeMarker(bad, start));
    bad = m;
    bad[4] ^= 1;
    CHECK(!DecodeMarker(bad, start));
    bad = m;
    bad.push_back(0);
    CHECK(!DecodeMarker(bad, start));
    CHECK(!DecodeMarker(std::vector<uint8_t>(), start));
}

UNIT_TEST(ext_stream_cache_read_back_follows_the_marker) {
    const std::vector<uint8_t> plain = Pattern(9000, 4);
    std::vector<uint8_t> gen0;
    std::vector<uint8_t> gen1;
    std::vector<EntryId> ids;
    TwoGenerations(plain, gen0, gen1, ids);
    FakeStore s;
    s.chunks = {gen0, gen1};
    // no marker: ordinal 0 until a miss
    Bundle all;
    ReadBackResult r = s.Read(all);
    CHECK_EQ(r.start, 0u);
    CHECK_EQ(r.next, 2u);
    CHECK_EQ(r.read, 2u);
    CHECK_EQ(r.markers, 0u);
    CHECK(!r.startMissing);
    CHECK_EQ(all.Chunks(), static_cast<size_t>(2));
    // the boot's end: the live records as chunk 2, then marker 0 naming it
    CHECK(ShouldConsolidate(all));
    s.chunks.push_back(all.Consolidate());
    s.markers.push_back(EncodeMarker(r.next));
    Bundle latest;
    r = s.Read(latest);
    CHECK_EQ(r.start, 2u);
    CHECK_EQ(r.next, 3u);
    CHECK_EQ(r.read, 1u);
    CHECK_EQ(r.markers, 1u);
    CHECK(!r.startMissing);
    CHECK_EQ(latest.Chunks(), static_cast<size_t>(1));
    CHECK(SameRecords(all, latest, ids));
    // chunks 0 and 1 were never asked for: they leave the keep list
    CHECK(std::find(s.chunkAsks.begin(), s.chunkAsks.end(), 0u) == s.chunkAsks.end());
    CHECK(std::find(s.chunkAsks.begin(), s.chunkAsks.end(), 1u) == s.chunkAsks.end());
    // a chunk after the marked one is read on top of it
    std::vector<uint8_t> extra;
    BeginChunk(extra);
    Recorder rec;
    CHECK(RecordReads(rec, Id(777777), plain, {{0, 50}}, extra));
    EndChunk(extra, 1);
    s.chunks.push_back(extra);
    Bundle more;
    r = s.Read(more);
    CHECK_EQ(r.start, 2u);
    CHECK_EQ(r.next, 4u);
    CHECK_EQ(more.Records(), static_cast<size_t>(41));
    s.chunks.pop_back();
    // a second generation: the newer marker wins
    s.chunks.push_back(latest.Consolidate());
    s.markers.push_back(EncodeMarker(3));
    Bundle second;
    r = s.Read(second);
    CHECK_EQ(r.start, 3u);
    CHECK_EQ(r.next, 4u);
    CHECK_EQ(r.markers, 2u);
    CHECK(SameRecords(all, second, ids));
}

UNIT_TEST(ext_stream_cache_read_back_falls_back_when_the_start_is_missing) {
    const std::vector<uint8_t> plain = Pattern(9000, 6);
    std::vector<uint8_t> gen0;
    std::vector<uint8_t> gen1;
    std::vector<EntryId> ids;
    TwoGenerations(plain, gen0, gen1, ids);
    Bundle all;
    CHECK(all.AddChunk(gen0));
    CHECK(all.AddChunk(gen1));
    // the marker was stored but its chunk was not (the cap refused it, or a crash lost it)
    FakeStore s;
    s.chunks = {gen0, gen1};
    s.markers = {EncodeMarker(2)};
    Bundle b;
    ReadBackResult r = s.Read(b);
    CHECK(r.startMissing);
    CHECK_EQ(r.start, 0u);
    CHECK_EQ(r.next, 2u);   // the consolidated chunk goes at 2 again
    CHECK_EQ(r.markers, 1u);
    CHECK(SameRecords(all, b, ids));
    // an older marker whose chunk is there comes before ordinal 0
    s.chunks.push_back(all.Consolidate());   // chunk 2
    s.markers = {EncodeMarker(2), EncodeMarker(5)};
    Bundle c;
    r = s.Read(c);
    CHECK(r.startMissing);
    CHECK_EQ(r.start, 2u);
    CHECK_EQ(r.next, 3u);
    CHECK_EQ(c.Chunks(), static_cast<size_t>(1));
    CHECK(SameRecords(all, c, ids));
    // a corrupt marker is ignored (and counted: the next marker goes after it)
    std::vector<uint8_t> torn = EncodeMarker(2);
    torn[12] ^= 0xff;
    s.markers = {torn};
    Bundle d;
    r = s.Read(d);
    CHECK(!r.startMissing);
    CHECK_EQ(r.start, 0u);
    CHECK_EQ(r.next, 3u);
    CHECK_EQ(r.markers, 1u);
    CHECK_EQ(d.Chunks(), static_cast<size_t>(3));
    // the chunks below the marked one aged out: the marked generation alone is read
    s.chunks[0].clear();
    s.chunks[1].clear();
    s.markers = {EncodeMarker(2)};
    Bundle e;
    r = s.Read(e);
    CHECK_EQ(r.start, 2u);
    CHECK_EQ(r.next, 3u);
    CHECK(SameRecords(all, e, ids));
    // nothing stored at all
    FakeStore empty;
    Bundle f;
    r = empty.Read(f);
    CHECK_EQ(r.start, 0u);
    CHECK_EQ(r.next, 0u);
    CHECK_EQ(r.read, 0u);
    CHECK_EQ(f.Records(), static_cast<size_t>(0));
}

UNIT_TEST(ext_stream_cache_record_carries_the_directory) {
    // The record holds the entry's block count and directory byte for byte (a hit answers
    // create_on_demand's range checks from it); a real three-block entry's head round-trips.
    const std::vector<uint8_t> plain = Pattern(70000, 21);
    const std::vector<uint8_t> entry = BuildEntry(plain);
    const uint32_t headBytes = 4 + 3 * 8;
    CHECK_EQ(DirectoryTotal(entry.data()), 70000u);
    EntryId id;
    id.offset = 4242;
    id.size   = static_cast<uint32_t>(entry.size());
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    r.Begin(id, DirectoryTotal(entry.data()), entry.data());
    r.Note(0, 64, plain.data());
    r.Note(66000, 8, plain.data() + 66000);
    CHECK(r.Finish(chunk));
    EndChunk(chunk, 1);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    const RecordView* v = b.Find(id);
    CHECK(v != nullptr);
    CHECK_EQ(v->blockCount, 3u);
    CHECK_EQ(v->HeadBytes(), headBytes);
    CHECK(std::memcmp(v->head, entry.data(), headBytes) == 0);
    CHECK_EQ(v->total, 70000u);
    // a seeded (widened) record keeps the same directory
    r.Seed(*v);
    r.Note(30000, 4, plain.data() + 30000);
    std::vector<uint8_t> chunk2;
    BeginChunk(chunk2);
    CHECK(r.Finish(chunk2));
    EndChunk(chunk2, 1);
    CHECK(b.AddChunk(chunk2));
    v = b.Find(id);
    CHECK_EQ(v->segCount, 3u);
    CHECK(std::memcmp(v->head, entry.data(), headBytes) == 0);
    // and so does the consolidated chunk
    Bundle c;
    CHECK(c.AddChunk(b.Consolidate()));
    CHECK(std::memcmp(c.Find(id)->head, entry.data(), headBytes) == 0);
    // a directory that does not describe the recorded total, or does not fit the entry, is refused
    std::vector<uint8_t> bad = chunk;
    bad[kChunkHeader + kRecordHeader + 2 * 8 + 6] ^= 1;   // the last block's uncompressed size
    CHECK(!Bundle().AddChunk(bad));
    bad = chunk;
    const uint32_t tiny = 20;                             // 4 + 3 x 8 > 20
    std::memcpy(bad.data() + kChunkHeader + 8, &tiny, 4);
    CHECK(!Bundle().AddChunk(bad));
    // the old format (version 2) is not read: a stale cache rebuilds
    bad = chunk;
    const uint32_t v2 = 2;
    std::memcpy(bad.data() + 4, &v2, 4);
    CHECK_EQ(ChunkRecordCount(bad), 0u);
    CHECK(!Bundle().AddChunk(bad));
}

UNIT_TEST(ext_stream_cache_package_identity) {
    PackageStamp s;
    s.crc         = 0xdf769ff1u;
    s.version     = 0x10007ff;
    s.userVersion = 10;
    s.fileCount   = 3;
    s.name        = "MegaMod3";
    s.fileSize    = 1910799586;
    s.writeTime   = 0x01dc4e1234567890ull;
    s.table       = {16, 100, 116, 2000, 2116, 64};
    const uint64_t id = PackageIdentity(s);
    CHECK_EQ(PackageIdentity(s), id);   // a function of the stamp alone
    PackageStamp t = s;
    t.fileSize += 1;
    CHECK(PackageIdentity(t) != id);
    t = s;
    t.writeTime += 1;                   // an identical copy with a new modification time
    CHECK(PackageIdentity(t) != id);
    t = s;
    t.crc ^= 1;
    CHECK(PackageIdentity(t) != id);
    t = s;
    t.version += 1;
    CHECK(PackageIdentity(t) != id);
    t = s;
    t.userVersion += 1;
    CHECK(PackageIdentity(t) != id);
    t = s;
    t.name = "MegaMod4";
    CHECK(PackageIdentity(t) != id);
    t = s;
    t.table[3] += 1;                    // one entry's size
    CHECK(PackageIdentity(t) != id);
    t = s;
    t.table[2] += 4;                    // one entry's offset
    CHECK(PackageIdentity(t) != id);
    t = s;
    t.fileCount = 2;
    t.table.resize(4);                  // an entry fewer
    CHECK(PackageIdentity(t) != id);

    // The cache by identity, as the stream cache keys it: the copy with a new time finds nothing
    // (one cold run), records under its own identity, and the next start hits.
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> store;
    auto find = [&store](uint64_t key) -> const std::vector<uint8_t>* {
        for (const auto& kv : store) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    };
    const std::vector<uint8_t> plain = Pattern(5000, 3);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    CHECK(RecordReads(r, Id(116), plain, {{0, 5000}}, chunk));
    EndChunk(chunk, 1);
    store.emplace_back(id, chunk);
    t = s;
    t.writeTime += 10000000;            // touched one second later, same bytes
    const uint64_t copy = PackageIdentity(t);
    CHECK(find(copy) == nullptr);       // cold
    store.emplace_back(copy, chunk);    // the cold run's recording
    Bundle warm;
    CHECK(find(copy) != nullptr);
    CHECK(warm.AddChunk(*find(copy)));
    CHECK(warm.Find(Id(116)) != nullptr);   // warm again
}

// Review F-039: a 16-byte chunk whose header claims 0xFFFFFFFF records. AddChunk reserved the raw
// count (72 bytes each, about 309 GB) before the per-record loop could refuse it: std::bad_alloc
// out of ReadBack, which nothing catches. The fuzz input, without its selector byte.
UNIT_TEST(ext_stream_cache_bundle_refuses_a_count_past_the_chunk) {
    const std::vector<uint8_t> chunk = {
        0x53, 0x54, 0x52, 0x43,   // 'STRC'
        0x03, 0x00, 0x00, 0x00,   // version 3
        0xFF, 0xFF, 0xFF, 0xFF,   // record count
        0x00, 0x00, 0x00, 0x00,
    };
    Bundle b;
    CHECK(!b.AddChunk(chunk));
    CHECK_EQ(b.Records(), static_cast<size_t>(0));
}

// Review phase 2: a record whose directory claims 0x10000 blocks with a full last block describes a
// 0x80000000-byte stream, one past what an int32 position can hold. AddChunk accepted it (and a
// segment ending at 0x80000000), so a SegmentStream read of that segment advanced its int32 _pos
// past INT32_MAX: signed overflow at segment_stream.cpp `_pos += n` (and `_pos += len` in Skip).
UNIT_TEST(ext_stream_cache_bundle_refuses_a_total_past_int32) {
    const uint32_t n = 0x10000;
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    const size_t rec = chunk.size();
    chunk.resize(rec + kRecordHeader + static_cast<size_t>(n) * 8 + 2 * 8 + 8, 0);
    const uint64_t offset = 7;
    const uint32_t size   = 4 + n * 8;
    const uint32_t total  = 0x80000000u;
    const uint32_t segs   = 2;
    std::memcpy(chunk.data() + rec, &offset, 8);
    std::memcpy(chunk.data() + rec + 8, &size, 4);
    std::memcpy(chunk.data() + rec + 12, &total, 4);
    std::memcpy(chunk.data() + rec + 16, &segs, 4);
    std::memcpy(chunk.data() + rec + 20, &n, 4);
    const uint16_t full = 0x8000;
    std::memcpy(chunk.data() + rec + kRecordHeader + static_cast<size_t>(n - 1) * 8 + 6, &full, 2);
    const size_t table = rec + kRecordHeader + static_cast<size_t>(n) * 8;
    const uint32_t seg[4] = {0, 4, 0x7FFFFFFCu, 4};   // [0,4) and [0x7FFFFFFC, 0x80000000)
    std::memcpy(chunk.data() + table, seg, sizeof(seg));
    EndChunk(chunk, 1);
    EntryId id;
    id.offset = offset;
    id.size   = size;
    Bundle b;
    const bool added = b.AddChunk(chunk);
    if (added) {
        const RecordView* v = b.Find(id);
        CHECK(v != nullptr);
        SegmentStream ss(v, nullptr, 7, nullptr, nullptr);
        uint8_t out[4];
        CHECK(ss.Read(4, out));
        ss.Skip(0x7FFFFFF8);
        CHECK_EQ(ss.GetCurrentPos(), 0x7FFFFFFCu);
        CHECK(ss.Read(4, out));   // _pos += 4: 0x7FFFFFFC + 4 overflows int32
    }
    CHECK(!added);
}

// Review phase 2 (F-234, the second route): ext.texture_quality's ReadDropped reads a level's size
// from the stream -- the record's own bytes -- and hands it to SkipIfWithin, which asks g_altSkip
// (AltSkip) first. AltSkip skipped without a range check, so a size of 0x7FFFFFFF at any position
// past 0 overflowed SegmentStream's int32 _pos (or the fallback's _position), whatever the total.
// SkipWithin skips only what lies within the record's total, in both modes.
UNIT_TEST(ext_stream_cache_segment_stream_skip_within_refuses_past_the_end) {
    const std::vector<uint8_t> plain = Pattern(70000, 13);
    const std::vector<uint8_t> entry = BuildEntry(plain);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    CHECK(RecordReads(r, Id(6), plain, {{0, 100}, {65000, 100}}, chunk));
    EndChunk(chunk, 1);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    const RecordView* rec = b.Find(Id(6));
    CHECK(rec != nullptr);

    FallbackContext ctx;
    ctx.entry = &entry;
    SegmentStream ss(rec, nullptr, 6, &OpenReader, &ctx);
    std::vector<uint8_t> out(100);
    CHECK(ss.Read(4, out.data()));
    // from the record: an oversize, a negative and a one-past-the-end length do nothing
    CHECK(!ss.SkipWithin(0x7FFFFFFF));
    CHECK(!ss.SkipWithin(-1));
    CHECK(!ss.SkipWithin(70000 - 4 + 1));
    CHECK_EQ(ss.GetCurrentPos(), 4u);
    CHECK(ss.SkipWithin(64996));
    CHECK_EQ(ss.GetCurrentPos(), 65000u);
    CHECK(ss.Read(100, out.data()));
    CHECK(std::memcmp(out.data(), plain.data() + 65000, 100) == 0);
    // in fallback: the same bound on the engine stream's position
    CHECK(ss.Read(100, out.data()));
    CHECK(ss.Extended());
    CHECK_EQ(ss.GetCurrentPos(), 65200u);
    CHECK(!ss.SkipWithin(0x7FFFFFFF));
    CHECK(!ss.SkipWithin(70000 - 65200 + 1));
    CHECK_EQ(ss.GetCurrentPos(), 65200u);
    CHECK(ss.SkipWithin(70000 - 65200));
    CHECK_EQ(ss.GetCurrentPos(), 70000u);
}
