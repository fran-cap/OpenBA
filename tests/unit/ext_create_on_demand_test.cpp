// ============================================================================
// tests/unit/ext_create_on_demand_test.cpp -- ext.create_on_demand (block CREATEONDEMAND)
// ============================================================================
// The registry row (class A, default ON (ruled 2026-10-23), off under /profile:faithful, never a
// pseudo-mod); the backend-neutral parts the DX11 backend drives (src/render/common/create_on_demand/):
//   - payload.h: RedecodeRange over a compressed entry built by the real encoder (compressed,
//     stored and short blocks, ranges across block edges, malformed entries), the package mappings
//     (shared per handle, detached on close, alive while a Locator holds them);
//   - lazy_table.h: one materialization per record whoever asks first (a use, a worker), a use that
//     meets a running record waits for it, Forget drops an unrun record, Pause / Resume, a Retry keeps
//     the record, and four threads racing uses, prefetches and forgets over 2,000 records;
//   - the production re-decode over a real MegaMod package (skips without one): payloads read by the
//     engine's own CompressedStream::Read, then re-produced through Maps() + Locator + Redecode.
// docs/extensions/handins/CREATEONDEMAND.md.
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "io/settings_file.h"
#include "io/Stream.h"
#include "platform_file.h"
#include "system/Compression.h"
#include "system/string_internal.h"   // RtSystem::RtMemFree: the table Compress allocated
#include "create_on_demand/lazy_table.h"
#include "create_on_demand/payload.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace CreateOnDemand;

namespace {

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const IO::SettingsFile::Entries* entries = static_cast<const IO::SettingsFile::Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State Resolved(const char* text, const char* cmd = nullptr) {
    IO::SettingsFile::Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, cmd, nullptr);
}

void PutI32(std::vector<uint8_t>& v, size_t at, int32_t x) {
    std::memcpy(v.data() + at, &x, 4);
}

// A compressed entry as the packer writes it: the block count, the directory, the blocks.
// Block `storedBlock` is stored (compressedSize 0); the last one is short.
std::vector<uint8_t> BuildEntry(const std::vector<uint8_t>& plain, int storedBlock) {
    const int n = static_cast<int>((plain.size() + kBlockBytes - 1) / kBlockBytes);
    std::vector<uint8_t> out(4 + static_cast<size_t>(n) * 8);
    PutI32(out, 0, n);
    System::Compression comp;
    std::vector<uint8_t> packed(kBlockBytes + 64);
    for (int b = 0; b < n; ++b) {
        const size_t from = static_cast<size_t>(b) * kBlockBytes;
        const uint32_t len = static_cast<uint32_t>(std::min<size_t>(kBlockBytes, plain.size() - from));
        int csize = 0;
        if (b != storedBlock) csize = comp.Compress(plain.data() + from, len, packed.data(), len);
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

std::vector<uint8_t> Plain(size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    std::mt19937 rng(seed);
    for (size_t i = 0; i < n; ++i) {
        // Compressible runs with some noise.
        v[i] = static_cast<uint8_t>(((i / 7) % 31) ^ ((rng() % 13 == 0) ? rng() : 0));
    }
    return v;
}

struct TempFile {
    fs::path path;
    explicit TempFile(const std::vector<uint8_t>& bytes) {
        static int serial = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("banished_cod_unit_" + std::to_string(static_cast<long long>(stamp)) +
                                            "_" + std::to_string(++serial) + ".pkm");
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));   // CAST-OK: byte_io
    }
    ~TempFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
    Platform::FileHandle Open() const {
        const std::u16string wide = path.u16string();
        return Platform::FileOpenRead(wide.c_str());
    }
};

// A record whose materialization the test controls and counts.
struct Counters {
    std::atomic<int> entered{0};   // Materialize calls begun
    std::atomic<int> made{0};
    std::atomic<int> published{0};
    std::atomic<int> destroyed{0};
    std::atomic<int> evicted{0};
};

class FakeRecord final : public Record {
public:
    FakeRecord(Counters* c, int* field, int value, std::atomic<bool>* gate = nullptr,
               Outcome outcome = Outcome::Made)
        : _c(c), _field(field), _value(value), _gate(gate), _outcome(outcome) {}
    ~FakeRecord() override { ++_c->destroyed; }
    Outcome Materialize() override {
        ++_c->entered;
        if (_gate != nullptr) {
            while (!_gate->load(std::memory_order_acquire)) std::this_thread::yield();
        }
        if (_outcome == Outcome::Made) ++_c->made;
        return _outcome;
    }
    void Publish() override {
        ++_c->published;
        *_field = _value;
    }
    void Evict() override {
        ++_c->evicted;
        *_field = 0;
    }
    uint64_t Bytes() const override { return 100; }

private:
    Counters*          _c;
    int*               _field;
    int                _value;
    std::atomic<bool>* _gate;
    Outcome            _outcome;
};

std::unique_ptr<Record> Fake(Counters* c, int* field, int value, std::atomic<bool>* gate = nullptr,
                             Outcome outcome = Outcome::Made) {
    return std::unique_ptr<Record>(new FakeRecord(c, field, value, gate, outcome));
}

}  // namespace

UNIT_TEST(ext_create_on_demand_row) {
    const Ext::Entry* e = Ext::FindEntry("create_on_demand");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);   // class A ships ON (the user ruled 2026-10-23)
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.create_on_demand");

    CHECK(Resolved("").ext.create_on_demand);
    CHECK(!Resolved("ext.create_on_demand=0\n").ext.create_on_demand);
    CHECK(!Resolved("ext.create_on_demand=1\nprofile=faithful\n").ext.create_on_demand);
    CHECK(!Resolved("ext.create_on_demand=1\n", "faithful").ext.create_on_demand);   // the gate's /profile:faithful

    Ext::State on{};
    on.ext.create_on_demand = true;
    Ext::Install(on, Ext::Profile::PerEntry, false);
    CHECK(Ext::Ext().create_on_demand);
    CHECK(Ext::PseudoModsForSave(nullptr, 0) == 0);   // class A: never in the save
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_create_on_demand_idle_key) {
    const Ext::ValueKey* k = Ext::FindValueKey("create_on_demand_idle_min");
    CHECK(k != nullptr);
    if (k == nullptr) return;
    CHECK(std::string(k->owner) == "create_on_demand");
    CHECK_EQ(k->minValue, 0);   // 0 = never give an object back
    CHECK_EQ(k->maxValue, 120);
    CHECK_EQ(k->defaultValue, 0);   // never, until re-makes are prefetched
    CHECK(Ext::SettingsKey(*k) == "ext.create_on_demand_idle_min");
}

UNIT_TEST(ext_create_on_demand_redecode_matches_every_range) {
    const std::vector<uint8_t> plain = Plain(5 * kBlockBytes + 1234, 7);
    const std::vector<uint8_t> entry = BuildEntry(plain, 2);
    CHECK(entry.size() < plain.size());   // the encoder compressed something
    std::mt19937 rng(99);
    std::vector<uint8_t> out;
    int checked = 0;
    for (int k = 0; k < 400; ++k) {
        const int32_t pos = static_cast<int32_t>(rng() % plain.size());
        const int32_t len = static_cast<int32_t>(rng() % std::min<size_t>(plain.size() - pos + 1, 3 * kBlockBytes));
        out.assign(static_cast<size_t>(len) + 1, 0xCD);
        CHECK(RedecodeRange(entry.data(), entry.size(), pos, len, out.data()));
        CHECK(std::memcmp(out.data(), plain.data() + pos, static_cast<size_t>(len)) == 0);
        CHECK_EQ(out[static_cast<size_t>(len)], static_cast<uint8_t>(0xCD));   // nothing written past the range
        ++checked;
    }
    // Block edges exactly, the whole stream, the empty range at the end.
    out.assign(plain.size(), 0);
    CHECK(RedecodeRange(entry.data(), entry.size(), 0, static_cast<int32_t>(plain.size()), out.data()));
    CHECK(out == plain);
    CHECK(RedecodeRange(entry.data(), entry.size(), kBlockBytes - 1, 2, out.data()));
    CHECK(out[0] == plain[kBlockBytes - 1] && out[1] == plain[kBlockBytes]);
    CHECK(RedecodeRange(entry.data(), entry.size(), static_cast<int32_t>(plain.size()), 0, out.data()));
    CHECK_EQ(checked, 400);
}

UNIT_TEST(ext_create_on_demand_redecode_refuses_bad_input) {
    const std::vector<uint8_t> plain = Plain(2 * kBlockBytes + 100, 3);
    std::vector<uint8_t> entry = BuildEntry(plain, -1);
    std::vector<uint8_t> out(plain.size() + 16);
    // Past the stream's end, negative, a short entry.
    CHECK(!RedecodeRange(entry.data(), entry.size(), static_cast<int32_t>(plain.size()) - 10, 11, out.data()));
    CHECK(!RedecodeRange(entry.data(), entry.size(), -1, 4, out.data()));
    CHECK(!RedecodeRange(entry.data(), 3, 0, 1, out.data()));
    CHECK(!RedecodeRange(entry.data(), 4 + 8, 0, 1, out.data()));   // the directory runs past the entry
    // A block whose source runs past the entry.
    std::vector<uint8_t> cut(entry.begin(), entry.end() - 10);
    CHECK(!RedecodeRange(cut.data(), cut.size(), static_cast<int32_t>(plain.size()) - 5, 5, out.data()));
    // A block that decodes to another size than its header says.
    std::vector<uint8_t> bad = entry;
    uint16_t u = 0;
    std::memcpy(&u, bad.data() + 4 + 6, 2);
    u = static_cast<uint16_t>(u - 1);
    std::memcpy(bad.data() + 4 + 6, &u, 2);
    CHECK(!RedecodeRange(bad.data(), bad.size(), 0, 10, out.data()));
}

UNIT_TEST(ext_create_on_demand_package_maps_share_detach_and_outlive_the_handle) {
    const std::vector<uint8_t> plain = Plain(3 * kBlockBytes + 77, 11);
    const std::vector<uint8_t> entry = BuildEntry(plain, 1);
    std::vector<uint8_t> file(4096, 0x5A);   // the entry sits at offset 4096 of the "package"
    file.insert(file.end(), entry.begin(), entry.end());
    TempFile tmp(file);
    Platform::FileHandle h = tmp.Open();
    CHECK(h != nullptr && h != Platform::kInvalidFileHandle);
    if (h == nullptr || h == Platform::kInvalidFileHandle) return;

    PackageMaps maps;
    PackageMap* const a = maps.Acquire(h);
    PackageMap* const b = maps.Acquire(h);
    CHECK(a != nullptr && a == b);
    CHECK_EQ(a->users, int64_t(2));
    CHECK_EQ(maps.GetStats().live, int64_t(1));

    // The engine closes its handle: the mapping is detached, still readable by its users.
    maps.Detach(h);
    Platform::FileClose(h);
    CHECK(a->handle == nullptr);
    CHECK_EQ(maps.GetStats().detached, int64_t(1));
    std::vector<uint8_t> out(1000);
    CHECK(RedecodeRange(a->map.data + 4096, entry.size(), kBlockBytes - 500, 1000, out.data()));
    CHECK(std::memcmp(out.data(), plain.data() + kBlockBytes - 500, 1000) == 0);

    // A new handle (possibly the same value) gets a new mapping.
    Platform::FileHandle h2 = tmp.Open();
    PackageMap* const c = maps.Acquire(h2);
    CHECK(c != nullptr && c != a);
    CHECK_EQ(maps.GetStats().live, int64_t(2));
    maps.Release(a);
    maps.Release(b);   // the detached one's last user: unmapped
    CHECK_EQ(maps.GetStats().live, int64_t(1));
    CHECK_EQ(maps.GetStats().detached, int64_t(0));
    maps.Release(c);   // attached: kept for the next payload of the same package
    CHECK_EQ(maps.GetStats().live, int64_t(1));
    maps.Detach(h2);   // unused: unmapped at once
    CHECK_EQ(maps.GetStats().live, int64_t(0));
    Platform::FileClose(h2);
    CHECK(maps.Acquire(nullptr) == nullptr);
    CHECK(maps.Acquire(Platform::kInvalidFileHandle) == nullptr);
}

UNIT_TEST(ext_create_on_demand_locator_redecodes_through_the_process_maps) {
    const std::vector<uint8_t> plain = Plain(2 * kBlockBytes + 900, 5);
    const std::vector<uint8_t> entry = BuildEntry(plain, -1);
    std::vector<uint8_t> file(100, 0);
    file.insert(file.end(), entry.begin(), entry.end());
    TempFile tmp(file);
    Platform::FileHandle h = tmp.Open();
    CHECK(h != nullptr && h != Platform::kInvalidFileHandle);
    if (h == nullptr || h == Platform::kInvalidFileHandle) return;
    const int64_t before = Maps().GetStats().live;
    {
        Locator loc;
        loc.map   = Maps().Acquire(h);
        loc.entry = 100;
        loc.size  = static_cast<uint32_t>(entry.size());
        loc.pos   = 30000;
        loc.len   = 5000;
        Locator moved(std::move(loc));   // the reference travels with the move
        CHECK(loc.map == nullptr);
        std::vector<uint8_t> out(5000);
        CHECK(Redecode(moved, out.data()));
        CHECK(std::memcmp(out.data(), plain.data() + 30000, 5000) == 0);
        Maps().Detach(h);
        CHECK(Redecode(moved, out.data()));   // detached: still mapped while held
        Locator outside;
        outside.map   = moved.map;
        outside.entry = file.size();   // the entry past the mapping's end is refused
        outside.size  = 10;
        CHECK(!Redecode(outside, out.data()));
        outside.map = nullptr;
    }
    CHECK_EQ(Maps().GetStats().live, before);   // the last Locator unmapped it
    Platform::FileClose(h);
}

UNIT_TEST(ext_create_on_demand_table_materializes_once) {
    LazyTable t(1);
    Counters c;
    int field = 0;
    const int obj = 0;
    t.Ensure(&obj);   // nothing recorded: a no-op
    t.Add(&obj, Fake(&c, &field, 7));
    CHECK_EQ(t.GetStats().pending, int64_t(1));
    t.Ensure(&obj);
    CHECK_EQ(field, 7);
    CHECK_EQ(c.made.load(), 1);
    CHECK_EQ(c.destroyed.load(), 0);   // made: the record (its Locator) is kept for a re-make
    t.Ensure(&obj);
    t.Prefetch(&obj);
    t.Forget(&obj);
    CHECK_EQ(c.made.load(), 1);
    const LazyTable::Stats s = t.GetStats();
    CHECK_EQ(s.onUse, int64_t(1));
    CHECK_EQ(s.pending, int64_t(0));
    CHECK_EQ(s.materialBytes, uint64_t(100));
}

UNIT_TEST(ext_create_on_demand_table_forget_drops_unrun) {
    LazyTable t(1);
    Counters c;
    int field = 0;
    const int a = 0;
    const int b = 0;
    t.Add(&a, Fake(&c, &field, 1));
    t.Add(&b, Fake(&c, &field, 2));
    t.Forget(&a);
    CHECK_EQ(c.destroyed.load(), 1);
    CHECK_EQ(c.published.load(), 0);
    t.Ensure(&a);
    CHECK_EQ(field, 0);
    // A queued record forgotten before its worker reaches it is skipped.
    std::atomic<bool> gate{false};
    const int blocker = 0;
    t.Add(&blocker, Fake(&c, &field, 9, &gate));
    t.Prefetch(&blocker);   // occupies the one worker
    t.Prefetch(&b);
    t.Forget(&b);
    gate.store(true, std::memory_order_release);
    t.DrainQueue();
    CHECK_EQ(c.made.load(), 1);   // the blocker only
    CHECK_EQ(field, 9);
    CHECK_EQ(t.GetStats().forgotten, int64_t(2));
}

UNIT_TEST(ext_create_on_demand_table_use_waits_for_a_running_worker) {
    LazyTable t(1);
    Counters c;
    int field = 0;
    const int obj = 0;
    std::atomic<bool> gate{false};
    t.Add(&obj, Fake(&c, &field, 5, &gate));
    t.Prefetch(&obj);
    // The worker has claimed it (it is inside Materialize, held by the gate): a use must wait for
    // it, not run it a second time.
    while (c.entered.load() == 0) std::this_thread::yield();
    std::thread opener([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        gate.store(true, std::memory_order_release);
    });
    t.Ensure(&obj);
    CHECK_EQ(field, 5);   // published before Ensure returned
    opener.join();
    CHECK_EQ(c.made.load(), 1);
    const LazyTable::Stats s = t.GetStats();
    CHECK_EQ(s.prefetched, int64_t(1));
    CHECK_EQ(s.onUse, int64_t(0));
    CHECK_EQ(s.waited, int64_t(1));
}

UNIT_TEST(ext_create_on_demand_table_pause_resume_and_retry) {
    LazyTable t(1);
    Counters c;
    int field = 0;
    const int obj = 0;
    t.Add(&obj, Fake(&c, &field, 3));
    t.Pause();
    t.Ensure(&obj);   // no device: the use goes without
    CHECK_EQ(field, 0);
    t.Prefetch(&obj);   // queued, not run while paused
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_EQ(c.made.load(), 0);
    t.Resume();
    t.DrainQueue();
    CHECK_EQ(field, 3);

    // A Retry keeps the record unpublished for the next use.
    const int lost = 0;
    int lostField = 0;
    t.Add(&lost, Fake(&c, &lostField, 4, nullptr, Outcome::Retry));
    t.Ensure(&lost);
    CHECK_EQ(lostField, 0);
    CHECK_EQ(t.GetStats().retried, int64_t(1));
    CHECK_EQ(t.GetStats().pending, int64_t(1));
    t.Forget(&lost);
    CHECK_EQ(t.GetStats().pending, int64_t(0));

    // A failed create is published as such and the record goes.
    const int broken = 0;
    int brokenField = -1;
    t.Add(&broken, Fake(&c, &brokenField, 0, nullptr, Outcome::Failed));
    t.Ensure(&broken);
    CHECK_EQ(brokenField, 0);
    CHECK_EQ(t.GetStats().failed, int64_t(1));
    CHECK_EQ(t.GetStats().pending, int64_t(0));
}

// P13 (P13_MEM.md §9): the loader's wait for its prefetches returns once they are made, and at once on
// a paused table (a device going away) with the queue still holding them.
UNIT_TEST(ext_create_on_demand_table_drain_unless_paused) {
    LazyTable t(1);
    Counters c;
    int a = 0;
    int b = 0;
    const int objA = 0;
    const int objB = 0;
    t.Add(&objA, Fake(&c, &a, 1));
    t.Add(&objB, Fake(&c, &b, 2));
    t.Prefetch(&objA);
    t.Prefetch(&objB);
    t.DrainQueueUnlessPaused();
    CHECK_EQ(a, 1);
    CHECK_EQ(b, 2);

    const int objC = 0;
    int cField = 0;
    t.Add(&objC, Fake(&c, &cField, 3));
    t.Pause();
    t.Prefetch(&objC);
    t.DrainQueueUnlessPaused();   // returns: paused, the queue unworked
    CHECK_EQ(cField, 0);
    t.Resume();
    t.DrainQueueUnlessPaused();
    CHECK_EQ(cField, 3);
}

UNIT_TEST(ext_create_on_demand_table_four_threads) {
    constexpr int kObjects = 2000;
    Counters c;
    std::vector<int> fields(kObjects, 0);
    std::vector<char> objs(kObjects);
    {
        LazyTable t(2);
        for (int i = 0; i < kObjects; ++i) {
            t.Add(&objs[i], Fake(&c, &fields[i], i + 1));
        }
        std::vector<std::thread> threads;
        for (int k = 0; k < 4; ++k) {
            threads.emplace_back([&, k] {
                std::mt19937 rng(static_cast<uint32_t>(k + 1));
                for (int n = 0; n < 6000; ++n) {
                    const int i = static_cast<int>(rng() % kObjects);
                    const uint32_t op = rng() % 10;
                    if (op < 5) {
                        t.Ensure(&objs[i]);
                    } else if (op < 9) {
                        t.Prefetch(&objs[i]);
                    } else if (i % 50 == 0) {
                        t.Forget(&objs[i]);
                    }
                }
            });
        }
        for (std::thread& th : threads) {
            th.join();
        }
        t.DrainQueue();
        for (int i = 0; i < kObjects; ++i) {
            t.Ensure(&objs[i]);
        }
        const LazyTable::Stats s = t.GetStats();
        CHECK_EQ(s.pending, int64_t(0));
        CHECK_EQ(c.made.load(), c.published.load());   // every run published, none twice
        CHECK_EQ(static_cast<int64_t>(c.made.load()) + s.forgotten, int64_t(kObjects));
    }
    CHECK_EQ(c.destroyed.load(), kObjects);   // every record went: forgotten, or with the table
    int wrong = 0;
    for (int i = 0; i < kObjects; ++i) {
        if (fields[i] != 0 && fields[i] != i + 1) ++wrong;
    }
    CHECK_EQ(wrong, 0);
}

// ---- the production re-decode over a real package (MegaMod; skips without it) ------------------

namespace {

int32_t ReadI32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// An entry's bytes served from the mapping as FileStream serves a packed entry.
struct MappedEntryStream final : IO::Stream {
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
    MappedEntryStream src;
    std::vector<RtIo::CompressedStreamHeader> headers;
    std::vector<RtIo::CompressedStreamWindow> windows;
    std::vector<uint8_t> buffer;
    alignas(IO::CompressedMemoryStream) unsigned char mem[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* cs = nullptr;
    int64_t length = 0;

    EntryReader(const uint8_t* base, int32_t size) {
        src.base = base;
        src.size = size;
        if (size < 4) return;
        const int32_t n = ReadI32(base);
        if (n <= 0 || 4 + int64_t(n) * 8 > size) return;
        headers.resize(static_cast<size_t>(n));
        std::memcpy(headers.data(), base + 4, static_cast<size_t>(n) * 8);
        int64_t total = 4 + int64_t(n) * 8;
        for (const auto& h : headers) {
            total += h.compressedSize ? h.compressedSize : h.uncompressedSize;
            length += h.uncompressedSize;
        }
        if (total != size) return;
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

// PackageFile::ReadPackfile's directory, the entry offsets and sizes only.
bool ReadDirectory(const uint8_t* d, uint64_t mapSize, std::vector<std::pair<uint32_t, uint32_t>>* entries) {
    const int32_t hdr = ReadI32(d);
    const bool compressed = (hdr & int32_t(0x80000000)) != 0;
    const int32_t at = hdr & 0x7fffffff;
    if (static_cast<uint64_t>(at) + 4 > mapSize) return false;
    EntryReader dir(d + at + 4, ReadI32(d + at));
    if (dir.cs == nullptr) return false;
    IO::CompressedStream* const cs = dir.cs;
    std::vector<uint8_t> tmp;
    auto rd = [&](int n, void* dst) { cs->IO::CompressedStream::Read(n, dst); };
    auto i32 = [&]() {
        int32_t v = 0;
        rd(4, &v);
        return v;
    };
    auto skip = [&](int32_t n) {
        if (n <= 0) return;
        tmp.resize(static_cast<size_t>(n));
        rd(n, tmp.data());
    };
    const int32_t fileCount = i32();
    if (compressed) {
        const int32_t formatVersion = i32();
        for (int i = 0; i < 4; ++i) i32();   // version, userVersion, priority, platform
        skip(1);                             // overridable
        i32();                               // crc
        const int32_t w = i32();
        const int32_t h = i32();
        skip(w * h * 4);
        if (formatVersion >= 2) skip(i32());
        for (int s = 0; s < 3; ++s) skip(i32());   // name, author, description
    }
    for (int32_t i = 0; i < fileCount; ++i) {
        skip(i32());   // the name
        const uint32_t offset = static_cast<uint32_t>(i32());
        const uint32_t size   = static_cast<uint32_t>(i32());
        entries->emplace_back(offset, size);
    }
    return !entries->empty();
}

std::string FindMegaModPackage() {
    std::vector<std::string> dirs;
    if (const char* env = std::getenv("BANISHED_DIR"); env && *env) dirs.push_back(env);
    dirs.push_back("C:/GOG Games/Banished");
    dirs.push_back("/mnt/c/GOG Games/Banished");
    for (const std::string& dir : dirs) {
        const std::string p = dir + "/MegaMod.X.Complete.Build.261018/MegaMod1.pkm";
        if (FILE* f = std::fopen(p.c_str(), "rb")) {
            std::fclose(f);
            return p;
        }
    }
    return std::string();
}

}  // namespace

UNIT_TEST(ext_create_on_demand_megamod_payloads_redecode_through_the_production_path) {
    const std::string path = FindMegaModPackage();
    if (path.empty()) UNIT_SKIP("MegaMod1.pkm not found (no MegaMod install)");
    const std::u16string wide(path.begin(), path.end());
    Platform::FileHandle h = Platform::FileOpenRead(wide.c_str());
    if (h == nullptr || h == Platform::kInvalidFileHandle) UNIT_SKIP("MegaMod1.pkm did not open");

    // The process's table, as the backend uses it: one mapping per package handle.
    PackageMap* const probe = Maps().Acquire(h);
    CHECK(probe != nullptr);
    if (probe == nullptr) {
        Platform::FileClose(h);
        return;
    }
    std::vector<std::pair<uint32_t, uint32_t>> entries;
    CHECK(ReadDirectory(probe->map.data, probe->map.size, &entries));

    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> logLen(8.0, 20.0);   // 256 B .. 1 MB payloads
    int64_t payloads = 0;
    int64_t mismatches = 0;
    std::vector<uint8_t> engine;
    std::vector<uint8_t> again;
    for (size_t k = 0; k < entries.size() && payloads < 4000; k += 3) {
        const uint32_t offset = entries[k].first;
        const uint32_t size   = entries[k].second;
        if (static_cast<uint64_t>(offset) + size > probe->map.size) continue;
        EntryReader r(probe->map.data + offset, static_cast<int32_t>(size));
        if (r.cs == nullptr) continue;
        int32_t pos = 0;
        int step = 0;
        while (pos < r.length) {
            const bool payload = (++step % 5) == 0;
            int32_t n = payload ? static_cast<int32_t>(std::exp2(logLen(rng))) : 1 + static_cast<int32_t>(rng() % 48);
            if (pos + n > r.length) n = static_cast<int32_t>(r.length - pos);
            engine.resize(static_cast<size_t>(n));
            if (!r.cs->IO::CompressedStream::Read(n, engine.data())) break;
            if (payload) {
                Locator loc;
                loc.map   = Maps().Acquire(h);   // what Locate does for every payload of the package
                loc.entry = offset;
                loc.size  = size;
                loc.pos   = pos;
                loc.len   = n;
                again.assign(static_cast<size_t>(n), 0xCD);
                if (!Redecode(loc, again.data()) || again != engine) ++mismatches;
                ++payloads;
            }
            pos += n;
        }
    }
    Maps().Release(probe);
    Maps().Detach(h);
    Platform::FileClose(h);
    CHECK(payloads > 1000);
    CHECK_EQ(mismatches, int64_t(0));
    std::printf("  [create_on_demand] %s: %lld payloads re-decoded through Maps/Locator/Redecode, %lld mismatches\n",
                path.c_str(), static_cast<long long>(payloads), static_cast<long long>(mismatches));
}

// ---- eviction (CREATEONDEMAND.md section 7) ------------------------------------------------------

UNIT_TEST(ext_create_on_demand_use_stamps) {
    UseStamps st;
    const int a = 0;
    const int b = 0;
    uint32_t e = 0;
    CHECK(!st.LastUse(&a, &e));
    st.Touch(&a, 9);   // untracked: nothing
    CHECK(!st.LastUse(&a, &e));
    CHECK(st.Insert(&a, 3));
    CHECK(st.LastUse(&a, &e) && e == 3);
    st.Touch(&a, 5);
    CHECK(st.LastUse(&a, &e) && e == 5);
    CHECK(st.Insert(&b, 4));
    st.Remove(&a);
    CHECK(!st.LastUse(&a, &e));
    CHECK(st.LastUse(&b, &e) && e == 4);   // found past the tombstone
    CHECK(st.Insert(&a, 7));               // the tombstone is reused
    CHECK(st.LastUse(&a, &e) && e == 7);
}

UNIT_TEST(ext_create_on_demand_table_evicts_idle_and_remakes) {
    LazyTable t(0);
    t.SetIdleLimit(2, std::chrono::milliseconds(0));
    Counters c;
    int fa = 0;
    int fb = 0;
    const int a = 0;
    const int b = 0;
    t.Add(&a, Fake(&c, &fa, 1));
    t.Add(&b, Fake(&c, &fb, 2));
    t.Ensure(&a);
    t.Ensure(&b);   // both made at epoch 1
    CHECK_EQ(t.Sweep(), int64_t(0));   // epoch 2: last used 1 epoch ago, kept
    t.Use(&a);                          // a used at epoch 2
    CHECK_EQ(t.Sweep(), int64_t(0));   // epoch 3: b 2 epochs ago, the limit itself: kept
    CHECK_EQ(t.Sweep(), int64_t(1));   // epoch 4: b 3 epochs ago, given back; a 2 ago, kept
    CHECK_EQ(fa, 1);
    CHECK_EQ(fb, 0);
    CHECK_EQ(c.evicted.load(), 1);
    LazyTable::Stats s = t.GetStats();
    CHECK_EQ(s.evicted, int64_t(1));
    CHECK_EQ(s.pending, int64_t(1));
    t.Ensure(&b);   // the next use makes it again, from the same record
    CHECK_EQ(fb, 2);
    s = t.GetStats();
    CHECK_EQ(s.remade, int64_t(1));
    CHECK_EQ(s.pending, int64_t(0));
    CHECK_EQ(c.made.load(), 3);
    t.Forget(&a);   // a made record goes without counting as dropped unmade
    CHECK_EQ(t.GetStats().forgotten, int64_t(0));
    CHECK_EQ(c.destroyed.load(), 1);
    t.Sweep();
    t.Sweep();
    t.Sweep();   // b, remade at epoch 4 and unused since, goes again at epoch 7
    CHECK_EQ(c.evicted.load(), 2);
}

UNIT_TEST(ext_create_on_demand_table_kept_is_never_evicted) {
    LazyTable t(1);
    t.SetIdleLimit(1, std::chrono::milliseconds(0));
    Counters c;
    int fa = 0;
    int fb = 0;
    int fc = 0;
    const int a = 0;
    const int b = 0;
    const int d = 0;
    t.Add(&a, Fake(&c, &fa, 1));
    t.Add(&b, Fake(&c, &fb, 2));
    t.Add(&d, Fake(&c, &fc, 3));
    t.Keep(&a);       // unmade: queued for the worker, and kept
    t.DrainQueue();
    CHECK_EQ(fa, 1);
    t.Ensure(&b);
    t.Ensure(&d);
    t.Keep(&b);       // made already: it leaves the sweep's set
    t.Keep(&b);       // a second Keep counts once
    for (int i = 0; i < 4; ++i) {
        t.Sweep();
    }
    CHECK_EQ(fa, 1);
    CHECK_EQ(fb, 2);
    CHECK_EQ(fc, 0);   // the one not kept is given back
    CHECK_EQ(c.evicted.load(), 1);
    LazyTable::Stats s = t.GetStats();
    CHECK_EQ(s.kept, int64_t(2));
    CHECK_EQ(s.prefetched, int64_t(1));
    t.Forget(&a);
    t.Add(&a, Fake(&c, &fa, 4));   // a new record for the same object is not kept
    t.Ensure(&a);
    for (int i = 0; i < 3; ++i) {
        t.Sweep();
    }
    CHECK_EQ(fa, 0);
}

UNIT_TEST(ext_create_on_demand_table_idle_zero_never_evicts) {
    LazyTable t(0);
    t.SetIdleLimit(0, std::chrono::milliseconds(0));
    Counters c;
    int f = 0;
    const int a = 0;
    t.Add(&a, Fake(&c, &f, 1));
    t.Ensure(&a);
    for (int i = 0; i < 10; ++i) {
        CHECK_EQ(t.Sweep(), int64_t(0));
    }
    CHECK_EQ(f, 1);
    CHECK_EQ(c.evicted.load(), 0);
}

UNIT_TEST(ext_create_on_demand_table_eviction_waits_for_a_use_in_progress) {
    LazyTable t(0);
    t.SetIdleLimit(1, std::chrono::milliseconds(0));
    Counters c;
    int f = 0;
    const int a = 0;
    t.Add(&a, Fake(&c, &f, 1));
    t.Ensure(&a);
    t.Sweep();   // epoch 2: one epoch idle is not over the limit yet
    CHECK_EQ(f, 1);
    t.Enter();   // a use in progress (another object: the stamp of `a` stays old)
    std::atomic<bool> swept{false};
    std::thread sweeper([&] {
        t.Sweep();
        swept.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(!swept.load(std::memory_order_acquire));   // held at the gate
    CHECK_EQ(f, 1);
    t.Leave();
    sweeper.join();
    CHECK(swept.load());
    CHECK_EQ(f, 0);   // given back once no use was in progress
    // A use after the gate reopened enters at once and remakes it.
    t.Enter();
    t.Use(&a);
    t.Ensure(&a);
    t.Leave();
    CHECK_EQ(f, 1);
}

UNIT_TEST(ext_create_on_demand_table_timed_sweep_on_worker) {
    LazyTable t(1);
    Counters c;
    int f = 0;
    const int a = 0;
    t.Add(&a, Fake(&c, &f, 1));
    t.Ensure(&a);
    t.SetIdleLimit(1, std::chrono::milliseconds(20));   // worker 0 sweeps every 20 ms
    for (int i = 0; i < 200 && c.evicted.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK_EQ(c.evicted.load(), 1);
    CHECK_EQ(f, 0);
    CHECK(t.GetStats().sweeps >= 1);
}
