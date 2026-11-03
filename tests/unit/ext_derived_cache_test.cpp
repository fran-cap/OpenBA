// ============================================================================
// tests/unit/ext_derived_cache_test.cpp -- src/ext/derived_cache: Hash64 and the Store
// ============================================================================
// docs/extensions/handins/COMPRESSAO.md. Over a fresh temp directory per case:
//   * Hash64 is XXH64 (vectors checked against MSYS2's xxhsum -H1);
//   * a payload stored, flushed, closed and reopened reads back byte for byte;
//   * a torn tail (half a record, stray data bytes) is dropped at Open, the entries before it kept;
//   * a payload whose bytes were damaged is a miss, and is stored again; the next Open serves the
//     good copy and rewrites the files without the bad one (a key's newest record wins);
//   * a new encoder version, or a key equal in hash but not in shape / size, misses;
//   * the cap refuses a payload past it; a full cache evicts the least recently used, the stamps of
//     every session merged, and a lost derived.used does not empty it;
//   * a second Store on the same folder reads the first one's entries (ReadOnly) and writes
//     nothing; a rewrite or a clear with that reader open; a clear request empties the next Open;
//   * the rewrite runs on the writer thread beside Finds; a crash between its two replaces is
//     finished at the next Open, and a mismatched pair keeps the prefix whose bytes match;
//   * the folder's missing parents are created;
//   * a damaged header starts the cache empty.
// ============================================================================
#include "unit.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "ext/derived_cache/derived_cache.h"
#include "ext/derived_cache/hash64.h"

namespace fs = std::filesystem;
using namespace Ext::DerivedCache;

namespace {

struct TempDir {
    fs::path dir;
    TempDir() {
        static int serial = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() / ("banished_dcache_unit_" + std::to_string(static_cast<long long>(stamp)) +
                                           "_" + std::to_string(++serial));
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string Path() const { return dir.u8string(); }
    fs::path File(const char* name) const { return dir / name; }
};

Key MakeKey(uint64_t hash, uint32_t w = 64, uint32_t version = 1) {
    Key k;
    k.producer   = MakeProducer('T', 'E', 'S', 'T');
    k.version    = version;
    k.shape[0]   = w;
    k.shape[1]   = 64;
    k.shape[2]   = 7;
    k.shape[3]   = 4;
    k.sourceSize = 4096;
    k.sourceHash = hash;
    return k;
}

std::vector<uint8_t> Payload(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = static_cast<uint8_t>(seed + i * 31u);
    return v;
}

void Append(const fs::path& p, size_t n) {
    std::ofstream f(p, std::ios::binary | std::ios::app);
    for (size_t i = 0; i < n; ++i)
        f.put(static_cast<char>(0x5A));
}

constexpr uint64_t kBig = 1ull << 40;

}  // namespace

UNIT_TEST(ext_derived_cache_hash64_is_xxh64) {
    CHECK_EQ(Hash64("", 0), 0xEF46DB3751D8E999ull);
    CHECK_EQ(Hash64("abc", 3), 0x44BC2CF5AD770999ull);
    const char* s = "Nobody inspects the spammish repetition";
    CHECK_EQ(Hash64(s, std::strlen(s)), 0xFBCEA83C8A378BF1ull);
    std::vector<uint8_t> v(1003);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<uint8_t>(i * 7 + 3);
    CHECK_EQ(Hash64(v.data(), v.size()), 0x778430D269B09BD7ull);   // every tail path: 31 stripes + 8 + 4 + 3
}

UNIT_TEST(ext_derived_cache_round_trip) {
    TempDir t;
    const std::vector<uint8_t> a = Payload(1000, 1);
    const std::vector<uint8_t> b = Payload(3 * 1024 * 1024 + 7, 2);   // past one 1 MB write chunk
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        std::vector<uint8_t> out;
        CHECK(!s.Find(MakeKey(1), out));
        s.Put(MakeKey(1), a);
        s.Put(MakeKey(2), b);
        s.Put(MakeKey(1), a);   // a duplicate is not stored twice
        s.Flush();
        CHECK(s.Find(MakeKey(1), out));   // this run's entry: read through the handle
        CHECK(out == a);
        const Stats st = s.GetStats();
        CHECK_EQ(st.stored, 2ull);
        CHECK_EQ(st.entries, 2ull);
        CHECK_EQ(s.BytesOnDisk(), 32ull + a.size() + b.size() + 32ull + 2 * sizeof(IndexRecord));
    }
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    std::vector<uint8_t> out;
    CHECK(s.Find(MakeKey(2), out));   // read through the mapping
    CHECK(out == b);
    CHECK(s.Find(MakeKey(1), out));
    CHECK(out == a);
    CHECK_EQ(s.GetStats().hits, 2ull);
    s.Close();
    CHECK_EQ(BytesInDir(t.Path()), 32ull + a.size() + b.size() + 32ull + 2 * sizeof(IndexRecord));
}

UNIT_TEST(ext_derived_cache_torn_tail_is_dropped) {
    TempDir t;
    const std::vector<uint8_t> a = Payload(500, 3);
    uint64_t good = 0;
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        s.Put(MakeKey(1), a);
        s.Flush();
        good = s.BytesOnDisk();
    }
    Append(t.File("derived.dat"), 300);   // a payload written, its record not
    Append(t.File("derived.idx"), 40);    // half a record
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        CHECK_EQ(s.BytesOnDisk(), good);
        std::vector<uint8_t> out;
        CHECK(s.Find(MakeKey(1), out));
        CHECK(out == a);
    }
    // A whole record whose own hash is wrong (a torn write of its bytes) is dropped too.
    {
        std::fstream f(t.File("derived.idx"), std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(32 + 0x30);
        f.put(static_cast<char>(0x77));
    }
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    CHECK_EQ(s.GetStats().entries, 0ull);
    CHECK_EQ(s.BytesOnDisk(), 64ull);
}

UNIT_TEST(ext_derived_cache_damaged_payload_misses_and_is_restored) {
    TempDir t;
    const std::vector<uint8_t> a = Payload(800, 4);
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        s.Put(MakeKey(9), a);
    }
    {
        std::fstream f(t.File("derived.dat"), std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(32 + 100);
        f.put(static_cast<char>(~a[100]));
    }
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    std::vector<uint8_t> out;
    CHECK(!s.Find(MakeKey(9), out));
    CHECK_EQ(s.GetStats().badPayloads, 1ull);
    s.Put(MakeKey(9), a);
    s.Flush();
    CHECK(s.Find(MakeKey(9), out));
    CHECK(out == a);
}

UNIT_TEST(ext_derived_cache_restored_payload_survives_reopen) {
    TempDir t;
    const std::vector<uint8_t> a = Payload(800, 11);
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        s.Put(MakeKey(9), a);
    }
    {
        std::fstream f(t.File("derived.dat"), std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(32 + 100);
        f.put(static_cast<char>(~a[100]));
    }
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        std::vector<uint8_t> out;
        CHECK(!s.Find(MakeKey(9), out));
        s.Put(MakeKey(9), a);   // a second record for the key; the bad one is still in the files
        s.Flush();
        CHECK_EQ(s.BytesOnDisk(), 64ull + 2 * a.size() + 2 * sizeof(IndexRecord));
    }
    // The next start serves the good copy, and the files no longer hold the bad one.
    // (The first start's writer rewrites the files; the second finds them rewritten.)
    for (int run = 0; run < 2; ++run) {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        CHECK_EQ(s.GetStats().entries, 1ull);
        if (run == 0) {
            CHECK_EQ(s.BytesOnDisk(), 64ull + 2 * a.size() + 2 * sizeof(IndexRecord));   // not in Open
            s.RequestCompact();
            s.Flush();
        }
        CHECK_EQ(s.BytesOnDisk(), 64ull + a.size() + sizeof(IndexRecord));
        std::vector<uint8_t> out;
        CHECK(s.Find(MakeKey(9), out));
        CHECK(out == a);
        CHECK_EQ(s.GetStats().badPayloads, 0ull);
        CHECK_EQ(s.GetStats().stored, 0ull);
    }
}

UNIT_TEST(ext_derived_cache_newest_record_wins_and_old_is_dropped) {
    TempDir t;
    const std::vector<uint8_t> a = Payload(600, 12);
    const std::vector<uint8_t> b = Payload(700, 13);
    const std::vector<uint8_t> c = Payload(300, 14);
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        s.Put(MakeKey(5), a);
        s.Put(MakeKey(6), c);
    }
    // A second, valid record for key 5 appended by hand (what a re-store leaves), with other bytes.
    {
        IndexRecord r = {};
        r.magic = 0x31454344u;   // "DCE1"
        const Key k = MakeKey(5);
        r.producer = k.producer;
        r.version  = k.version;
        std::memcpy(r.shape, k.shape, sizeof(r.shape));
        r.sourceSize  = k.sourceSize;
        r.sourceHash  = k.sourceHash;
        r.offset      = 32 + a.size() + c.size();
        r.size        = b.size();
        r.payloadHash = Hash64(b.data(), b.size());
        r.check       = Hash64(&r, offsetof(IndexRecord, check));
        std::ofstream d(t.File("derived.dat"), std::ios::binary | std::ios::app);
        d.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        std::ofstream i(t.File("derived.idx"), std::ios::binary | std::ios::app);
        i.write(reinterpret_cast<const char*>(&r), sizeof(r));
    }
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    CHECK_EQ(s.GetStats().entries, 2ull);
    std::vector<uint8_t> out;
    CHECK(s.Find(MakeKey(5), out));   // the newest copy, before the rewrite too
    CHECK(out == b);
    s.RequestCompact();
    s.Flush();
    // The writer's rewrite dropped the old record and its bytes: key 6, then key 5's newest.
    CHECK_EQ(s.BytesOnDisk(), 64ull + c.size() + b.size() + 2 * sizeof(IndexRecord));
    CHECK(s.Find(MakeKey(5), out));
    CHECK(out == b);
    CHECK(s.Find(MakeKey(6), out));
    CHECK(out == c);
    CHECK(!fs::exists(t.File("derived.dat.new")));
    s.Close();
    CHECK_EQ(BytesInDir(t.Path()), 64ull + c.size() + b.size() + 2 * sizeof(IndexRecord));
}

UNIT_TEST(ext_derived_cache_version_and_shape_are_part_of_the_key) {
    TempDir t;
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    const std::vector<uint8_t> a = Payload(64, 5);
    const std::vector<uint8_t> b = Payload(64, 6);
    s.Put(MakeKey(7, 64, 1), a);
    s.Put(MakeKey(7, 128, 1), b);   // the same source hash, another shape: a separate entry
    s.Flush();
    std::vector<uint8_t> out;
    CHECK(!s.Find(MakeKey(7, 64, 2), out));   // a new encoder version misses
    CHECK(s.Find(MakeKey(7, 64, 1), out));
    CHECK(out == a);
    CHECK(s.Find(MakeKey(7, 128, 1), out));
    CHECK(out == b);
    Key other = MakeKey(7, 64, 1);
    other.sourceSize = 4097;   // the same hash with another size: a collision, not a hit
    CHECK(!s.Find(other, out));
    other = MakeKey(7, 64, 1);
    other.producer = MakeProducer('O', 'T', 'H', 'R');
    CHECK(!s.Find(other, out));
}

UNIT_TEST(ext_derived_cache_cap_refuses_then_keeps_the_used) {
    TempDir t;
    const std::vector<uint8_t> p = Payload(1000, 7);
    {
        Store s;
        CHECK(s.Open(t.Path(), 32 + 2500) == Store::OpenStatus::Opened);   // room for two payloads
        s.Put(MakeKey(1), p);
        s.Put(MakeKey(2), p);
        s.Put(MakeKey(3), p);
        s.Flush();
        CHECK_EQ(s.GetStats().stored, 2ull);
        CHECK_EQ(s.GetStats().refusedFull, 1ull);
    }
    {
        // A larger cap: the last run uses entry 2 and adds 3 and 4; 1 goes unused.
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        std::vector<uint8_t> out;
        CHECK(s.Find(MakeKey(2), out));
        s.Put(MakeKey(3), p);
        s.Put(MakeKey(4), p);
        s.Flush();
        CHECK_EQ(s.GetStats().entries, 4ull);
    }
    // Back under a 3-payload cap: 4000 bytes are over it, so the cache evicts the least recently
    // used (1: stamped by the first run only) down to 90% of the cap and keeps 2, 3, 4 in order.
    Store s;
    CHECK(s.Open(t.Path(), 32 + 3500) == Store::OpenStatus::Opened);
    std::vector<uint8_t> out;
    CHECK_EQ(s.GetStats().entries, 4ull);   // evicted by the writer, not in Open
    s.RequestCompact();
    s.Flush();
    CHECK_EQ(s.GetStats().entries, 3ull);
    CHECK(!s.Find(MakeKey(1), out));
    CHECK(s.Find(MakeKey(2), out));
    CHECK(out == p);
    CHECK(s.Find(MakeKey(3), out));
    CHECK(s.Find(MakeKey(4), out));
    CHECK(out == p);
    CHECK(!fs::exists(t.File("derived.dat.new")));
}

// Two key sets used in alternate sessions (vanilla, then a mod set, then vanilla again) share one
// cache: the stamps of every session are merged, so going over the cap evicts the set used longest
// ago, not everything the last session did not touch. A deleted derived.used evicts the oldest
// entries in the file, not all of them.
UNIT_TEST(ext_derived_cache_eviction_merges_sessions_and_survives_a_lost_used_list) {
    TempDir t;
    const std::vector<uint8_t> p = Payload(1000, 19);
    const auto session = [&](uint64_t cap, std::initializer_list<uint64_t> keys) {
        Store s;
        CHECK(s.Open(t.Path(), cap) != Store::OpenStatus::Failed);
        std::vector<uint8_t> out;
        for (uint64_t k : keys) {
            if (!s.Find(MakeKey(k), out)) s.Put(MakeKey(k), p);
        }
        s.Flush();
        return s.GetStats().entries;
    };
    CHECK_EQ(session(kBig, { 10, 11 }), 2ull);   // set A
    CHECK_EQ(session(kBig, { 20, 21 }), 4ull);   // set B
    CHECK_EQ(session(kBig, { 10, 11 }), 4ull);   // set A again: both hits
    // A 3-payload cap (90% = 3178 bytes): over it at 4032, B's older entry goes first; A's both stay.
    // (The old keep list held only the last session's keys: it kept A and dropped both of B.)
    {
        Store s;
        CHECK(s.Open(t.Path(), 32 + 3500) == Store::OpenStatus::Opened);
        s.RequestCompact();   // the exe: the boot's end
        s.Flush();
        std::vector<uint8_t> out;
        CHECK_EQ(s.GetStats().entries, 3ull);
        CHECK(s.Find(MakeKey(10), out));
        CHECK(s.Find(MakeKey(11), out));
        CHECK(!s.Find(MakeKey(20), out));
        CHECK(s.Find(MakeKey(21), out));
    }
    // Set B again, at the same cap: 21 hits, 20 is derived anew and refused (the cache is full); the
    // next session evicts down to 90% again (less the 1000 bytes refused), A's least recent first,
    // and 20 fits.
    CHECK_EQ(session(32 + 3500, { 20, 21 }), 3ull);
    {
        Store s;
        CHECK(s.Open(t.Path(), 32 + 3500) == Store::OpenStatus::Opened);
        s.RequestCompact();
        s.Flush();
        std::vector<uint8_t> out;
        CHECK_EQ(s.GetStats().entries, 2ull);   // 10 (stamped longest ago) evicted
        CHECK(!s.Find(MakeKey(10), out));
        CHECK(s.Find(MakeKey(21), out));
        s.Put(MakeKey(20), p);
        s.Flush();
        CHECK_EQ(s.GetStats().stored, 1ull);
    }
    // derived.used lost: every entry is equally old, and the oldest in the file go first.
    fs::remove(t.File("derived.used"));
    Store s;
    CHECK(s.Open(t.Path(), 32 + 2500) == Store::OpenStatus::Opened);   // 90% = 2278: two payloads
    s.RequestCompact();
    s.Flush();
    std::vector<uint8_t> out;
    CHECK_EQ(s.GetStats().entries, 2ull);
    CHECK(!s.Find(MakeKey(11), out));
    CHECK(s.Find(MakeKey(21), out));
    CHECK(s.Find(MakeKey(20), out));
    CHECK(out == p);
}

UNIT_TEST(ext_derived_cache_second_instance_and_clear) {
    TempDir t;
    const std::vector<uint8_t> p1 = Payload(100, 8);
    const std::vector<uint8_t> p3 = Payload(300, 16);
    Store a;
    CHECK(a.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    a.Put(MakeKey(1), p1);
    a.Flush();
    // The lock loser reads what the holder had stored, and writes nothing.
    Store b;
    CHECK(b.Open(t.Path(), kBig) == Store::OpenStatus::ReadOnly);
    CHECK(b.IsReadOnly());
    std::vector<uint8_t> out;
    CHECK(b.Find(MakeKey(1), out));
    CHECK(out == p1);
    const uint64_t before = BytesInDir(t.Path());
    b.Put(MakeKey(2), Payload(100, 9));   // a no-op
    b.Flush();                            // returns at once: no writer
    CHECK_EQ(BytesInDir(t.Path()), before);
    CHECK(!b.Find(MakeKey(2), out));
    // The holder appends while the reader is open: the reader's index is the prefix it loaded; a
    // reader opened after the append sees it.
    a.Put(MakeKey(3), p3);
    a.Flush();
    CHECK(a.Find(MakeKey(3), out));
    CHECK(!b.Find(MakeKey(3), out));
    {
        Store c;
        CHECK(c.Open(t.Path(), kBig) == Store::OpenStatus::ReadOnly);
        CHECK_EQ(c.GetStats().entries, 2ull);
        CHECK(c.Find(MakeKey(3), out));
        CHECK(out == p3);
    }
    // The holder closes while the reader still reads; the next opener takes the lock and the files.
    a.Close();
    CHECK(b.Find(MakeKey(1), out));
    CHECK(out == p1);
    CHECK(a.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    a.Put(MakeKey(4), p1);
    a.Flush();
    CHECK(a.Find(MakeKey(4), out));
    CHECK(b.Find(MakeKey(1), out));

    // A clear carried out while the reader still holds the files.
    CHECK(RequestClear(t.Path()));
    CHECK(ClearRequested(t.Path()));
    a.Disable();
    CHECK(!a.Find(MakeKey(1), out));   // disabled at once
    a.Close();
    CHECK(a.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    CHECK(!ClearRequested(t.Path()));
    CHECK_EQ(a.GetStats().entries, 0ull);
    CHECK_EQ(a.BytesOnDisk(), 64ull);
    CHECK(b.Find(MakeKey(1), out));   // the reader keeps what it had open
    CHECK(out == p1);
    b.Close();
}

UNIT_TEST(ext_derived_cache_open_creates_missing_parents) {
    TempDir t;
    const fs::path deep = t.dir / "a" / "b" / "Cache";   // neither a nor b exists yet
    CHECK(!fs::exists(t.dir / "a"));
    Store s;
    CHECK(s.Open(deep.u8string(), kBig) == Store::OpenStatus::Opened);
    CHECK(fs::is_directory(deep));
    s.Put(MakeKey(1), Payload(100, 15));
    s.Flush();
    CHECK_EQ(s.GetStats().stored, 1ull);
    s.Close();
    CHECK(fs::exists(deep / "derived.dat"));
}

// The holder rewrites the files (here: to drop a superseded record) while a read-only
// store holds them open. Windows: the reader's FILE_SHARE_DELETE lets the replace go through; the
// reader keeps the old bytes until it closes. POSIX: the reader keeps the old inode.
UNIT_TEST(ext_derived_cache_rewrite_with_a_reader_open) {
    TempDir t;
    const std::vector<uint8_t> a = Payload(600, 17);
    const std::vector<uint8_t> b = Payload(700, 18);
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        s.Put(MakeKey(5), a);
        s.Put(MakeKey(6), b);
    }
    Store holder;
    CHECK(holder.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    Store reader;
    CHECK(reader.Open(t.Path(), kBig) == Store::OpenStatus::ReadOnly);
    holder.Close();
    // A second record for key 5 (what a re-store leaves), appended while the reader holds the files.
    {
        IndexRecord r = {};
        r.magic = 0x31454344u;   // "DCE1"
        const Key k = MakeKey(5);
        r.producer = k.producer;
        r.version  = k.version;
        std::memcpy(r.shape, k.shape, sizeof(r.shape));
        r.sourceSize  = k.sourceSize;
        r.sourceHash  = k.sourceHash;
        r.offset      = 32 + a.size() + b.size();
        r.size        = b.size();
        r.payloadHash = Hash64(b.data(), b.size());
        r.check       = Hash64(&r, offsetof(IndexRecord, check));
        std::ofstream d(t.File("derived.dat"), std::ios::binary | std::ios::app);
        d.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        std::ofstream i(t.File("derived.idx"), std::ios::binary | std::ios::app);
        i.write(reinterpret_cast<const char*>(&r), sizeof(r));
    }
    const uint64_t rewritten = 64ull + b.size() + b.size() + 2 * sizeof(IndexRecord);
    const uint64_t asItWas   = 64ull + a.size() + b.size() + b.size() + 3 * sizeof(IndexRecord);
    CHECK(holder.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    holder.RequestCompact();
    holder.Flush();
    // POSIX: the rewrite went through (the files hold key 6 and key 5's newest copy only). Windows
    // 10: MoveFileExW refuses to replace a file another handle holds, FILE_SHARE_DELETE or not, so
    // the files stay as they were. Either way the holder serves the newest copy and leaves no
    // half-made rewrite behind.
    const uint64_t onDisk = holder.BytesOnDisk();
    CHECK(onDisk == rewritten || onDisk == asItWas);
    std::fprintf(stderr, "  rewrite with a reader open: %s\n", onDisk == rewritten ? "replaced" : "kept as it was");
    std::vector<uint8_t> out;
    CHECK(holder.Find(MakeKey(5), out));
    CHECK(out == b);
    CHECK(!fs::exists(t.File("derived.dat.new")));
    CHECK(!fs::exists(t.File("derived.idx.new")));
    // The reader still serves what it loaded.
    CHECK(reader.Find(MakeKey(5), out));
    CHECK(out == a);
    CHECK(reader.Find(MakeKey(6), out));
    CHECK(out == b);
    reader.Close();
    holder.Close();
    // With the reader gone the rewrite goes through.
    CHECK(holder.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    holder.RequestCompact();
    holder.Flush();
    CHECK(holder.Find(MakeKey(5), out));
    CHECK(out == b);
    holder.Close();
    CHECK_EQ(BytesInDir(t.Path()), rewritten);
}

namespace {

// Three 1000-byte entries (keys 1, 2, 3, each its own bytes), then a session that uses `fresh` (two
// of them), then a store opened at a two-payload cap (90% = 2278 bytes): its rewrite evicts the
// third key, the one `fresh` leaves out. The store is returned open, the rewrite not yet run.
void ThreeThenTwoFresh(const TempDir& t, std::initializer_list<uint64_t> fresh, Store& s) {
    {
        Store w;
        CHECK(w.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        for (uint64_t k = 1; k <= 3; ++k)
            w.Put(MakeKey(k), Payload(1000, static_cast<uint8_t>(40 + k)));
    }
    {
        Store u;
        CHECK(u.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        std::vector<uint8_t> out;
        for (uint64_t k : fresh)
            CHECK(u.Find(MakeKey(k), out));
    }
    CHECK(s.Open(t.Path(), 32 + 2500) == Store::OpenStatus::Opened);
    CHECK_EQ(s.GetStats().entries, 3ull);
}

uint64_t HeaderGeneration(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    char h[32] = {};
    f.read(h, sizeof(h));
    uint64_t g = 0;
    std::memcpy(&g, h + 16, 8);
    return g;
}

}  // namespace

// A crash between the rewrite's two replaces: the new derived.dat beside the old derived.idx. The
// next Open sees the generations differ and finishes the rewrite from derived.idx.new: every entry
// the rewrite kept survives, though their offsets moved (key 1 was evicted from the front).
UNIT_TEST(ext_derived_cache_crash_between_the_replaces_is_finished) {
    TempDir t;
    {
        Store s;
        ThreeThenTwoFresh(t, { 2, 3 }, s);
        SetCrashBetweenReplacesForTest(true);
        s.RequestCompact();
        s.Flush();
        SetCrashBetweenReplacesForTest(false);
        std::vector<uint8_t> out;
        CHECK(!s.Find(MakeKey(2), out));   // the "dead" store serves nothing more
    }
    CHECK(fs::exists(t.File("derived.idx.new")));
    CHECK(HeaderGeneration(t.File("derived.dat")) == HeaderGeneration(t.File("derived.idx")) + 1);
    Store s;
    CHECK(s.Open(t.Path(), 32 + 2500) == Store::OpenStatus::Opened);
    CHECK_EQ(s.GetStats().entries, 2ull);
    CHECK_EQ(s.BytesOnDisk(), 64ull + 2000 + 2 * sizeof(IndexRecord));
    std::vector<uint8_t> out;
    CHECK(!s.Find(MakeKey(1), out));
    CHECK(s.Find(MakeKey(2), out));
    CHECK(out == Payload(1000, 42));
    CHECK(s.Find(MakeKey(3), out));
    CHECK(out == Payload(1000, 43));
    CHECK_EQ(s.GetStats().badPayloads, 0ull);
    CHECK(!fs::exists(t.File("derived.idx.new")));
    CHECK(HeaderGeneration(t.File("derived.dat")) == HeaderGeneration(t.File("derived.idx")));
}

// The same pair with derived.idx.new gone (put together by hand): nothing to finish, so Open keeps
// the index prefix whose payloads match their hashes, deterministically. Key 3 was evicted from the
// back, so the new data file starts like the old one: keys 1 and 2 survive.
UNIT_TEST(ext_derived_cache_new_data_old_index_keeps_the_matching_prefix) {
    TempDir t;
    {
        Store s;
        ThreeThenTwoFresh(t, { 1, 2 }, s);
        SetCrashBetweenReplacesForTest(true);
        s.RequestCompact();
        s.Flush();
        SetCrashBetweenReplacesForTest(false);
    }
    fs::remove(t.File("derived.idx.new"));
    for (int run = 0; run < 2; ++run) {   // the second Open finds a consistent pair
        Store s;
        CHECK(s.Open(t.Path(), 32 + 2500) == Store::OpenStatus::Opened);
        CHECK_EQ(s.GetStats().entries, 2ull);
        CHECK_EQ(s.BytesOnDisk(), 64ull + 2000 + 2 * sizeof(IndexRecord));
        std::vector<uint8_t> out;
        CHECK(s.Find(MakeKey(1), out));
        CHECK(out == Payload(1000, 41));
        CHECK(s.Find(MakeKey(2), out));
        CHECK(out == Payload(1000, 42));
        CHECK(!s.Find(MakeKey(3), out));
        CHECK_EQ(s.GetStats().badPayloads, 0ull);
    }
}

// The rewrite runs on the writer thread while other threads go on reading: every Find before,
// during and after the swap returns its key's own bytes.
UNIT_TEST(ext_derived_cache_rewrite_runs_beside_finds) {
    TempDir t;
    constexpr uint64_t kKeys = 64;
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        for (uint64_t k = 0; k < kKeys; ++k)
            s.Put(MakeKey(100 + k), Payload(20000, static_cast<uint8_t>(k)));
    }
    {
        // A second record for every even key: half the data file is superseded copies.
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    }
    {
        std::ifstream i(t.File("derived.idx"), std::ios::binary);
        std::vector<char> idx((std::istreambuf_iterator<char>(i)), std::istreambuf_iterator<char>());
        std::ofstream d(t.File("derived.dat"), std::ios::binary | std::ios::app);
        std::ofstream o(t.File("derived.idx"), std::ios::binary | std::ios::app);
        uint64_t at = 32 + kKeys * 20000;
        for (uint64_t k = 0; k < kKeys; k += 2) {
            IndexRecord r;
            std::memcpy(&r, idx.data() + 32 + k * sizeof(IndexRecord), sizeof(r));
            const std::vector<uint8_t> p = Payload(20000, static_cast<uint8_t>(k));
            r.offset = at;
            r.check  = Hash64(&r, offsetof(IndexRecord, check));
            d.write(reinterpret_cast<const char*>(p.data()), static_cast<std::streamsize>(p.size()));
            o.write(reinterpret_cast<const char*>(&r), sizeof(r));
            at += p.size();
        }
    }
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    CHECK_EQ(s.GetStats().entries, kKeys);
    std::atomic<bool> done{false};
    std::atomic<uint64_t> finds{0};
    std::atomic<uint64_t> wrong{0};
    std::vector<std::thread> readers;
    for (int r = 0; r < 3; ++r) {
        readers.emplace_back([&, r] {
            std::vector<uint8_t> out;
            uint64_t k = static_cast<uint64_t>(r);
            while (!done.load()) {
                const uint64_t key = k % kKeys;
                if (!s.Find(MakeKey(100 + key), out) || out != Payload(20000, static_cast<uint8_t>(key)))
                    wrong.fetch_add(1);
                finds.fetch_add(1);
                ++k;
                // A pause between finds: they still overlap the rewrite, but do not spin three cores
                // flat out. On the Windows CI runner the rewrite (background mode there,
                // Platform::ThreadBackgroundMode) took 10.8 s beside 133k spinning finds, the
                // suite's other workers sharing the same four cores.
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        });
    }
    s.RequestCompact();
    s.Flush();
    done.store(true);
    for (std::thread& th : readers)
        th.join();
    CHECK_EQ(wrong.load(), 0ull);
    CHECK(finds.load() > 0);
    CHECK_EQ(s.BytesOnDisk(), 64ull + kKeys * 20000 + kKeys * sizeof(IndexRecord));
    std::fprintf(stderr, "  rewrite beside finds: %llu finds, %llu wrong\n",
                 static_cast<unsigned long long>(finds.load()), static_cast<unsigned long long>(wrong.load()));
}

namespace {

// derived.used's stamp for `key`, or -1 when the file or the key is absent.
long long UsedStamp(const TempDir& t, const Key& key) {
    std::ifstream f(t.File("derived.used"), std::ios::binary);
    std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const uint64_t id = KeyId(key);
    for (size_t at = 32; at + 16 <= b.size(); at += 16) {
        uint64_t got = 0;
        uint32_t stamp = 0;
        std::memcpy(&got, b.data() + at, 8);
        std::memcpy(&stamp, b.data() + at + 8, 4);
        if (got == id) return stamp;
    }
    return -1;
}

}  // namespace

// A consumer that only reads (ext.package_cache on a warm start: a Find hit, no Put, no RequestFlush)
// still gets its stamps on disk: after a Flush, after the quiet spell with no flush at all, and from
// WriteStampsNow (the exe's exit hook) while the store stays open.
UNIT_TEST(ext_derived_cache_stamps_of_a_reader_only_session_reach_the_disk) {
    TempDir t;
    {
        Store w;
        CHECK(w.Open(t.Path(), kBig) == Store::OpenStatus::Opened);   // session 1 stores 1, 2, 3
        for (uint64_t k = 1; k <= 3; ++k)
            w.Put(MakeKey(k), Payload(100, static_cast<uint8_t>(k)));
    }
    std::vector<uint8_t> out;
    {
        Store s;   // session 2: Find only, then Flush
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        CHECK(s.Find(MakeKey(1), out));
        s.Flush();
        CHECK_EQ(UsedStamp(t, MakeKey(1)), 2ll);
        CHECK_EQ(UsedStamp(t, MakeKey(2)), 1ll);
        CHECK_EQ(s.GetStats().stored, 0ull);
    }
    SetQuietSpellForTest(100);
    {
        Store s;   // session 3: Find only, no flush: the quiet spell writes them
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        CHECK(s.Find(MakeKey(2), out));
        bool written = false;
        for (int i = 0; i < 100 && !written; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            written = UsedStamp(t, MakeKey(2)) == 3;
        }
        CHECK(written);
        CHECK_EQ(UsedStamp(t, MakeKey(1)), 2ll);   // the earlier session's stamp is kept
    }
    SetQuietSpellForTest(10000);
    {
        Store s;   // session 4: Find only, then the exit hook's call, store still open
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        CHECK(s.Find(MakeKey(3), out));
        s.WriteStampsNow();
        CHECK_EQ(UsedStamp(t, MakeKey(3)), 4ll);
    }
}

UNIT_TEST(ext_derived_cache_unknown_format_starts_empty) {
    TempDir t;
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        s.Put(MakeKey(1), Payload(100, 10));
    }
    {
        std::fstream f(t.File("derived.idx"), std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(8);
        f.put(static_cast<char>(99));   // the format word
    }
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    CHECK_EQ(s.GetStats().entries, 0ull);
    CHECK_EQ(s.BytesOnDisk(), 64ull);
}
