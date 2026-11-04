// ============================================================================
// src/ext/stream_cache/stream_cache.cpp -- ext.stream_cache (class A, next start)
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/STREAMCACHE.md). The contract is in the header.
//
// THREADS. Loads run on the loader thread and, between brackets, on the main thread; a load nests
// (a reference inside a resource loads the referenced resource before the outer one continues), so
// each thread keeps a stack of the loads it is inside, and the read tap attributes a read to the
// stream on top of that stack. The package table, the bundles and the pending records are under one
// mutex (a lookup per load; nothing heavy under it).
//
// LIFETIME. The records serve the BOOT: its brackets end with the menu's (the MenuDialogLoopT load),
// after which the bundles are dropped (150 MB on a MegaMod boot) and no later bracket is served or
// recorded -- a new game's or a save's bracket loads a few hundred resources, far below the cost of
// reading the chunks back, and the resources a device restart reloads are the same few.
//
// GENERATIONS. Before the bundles are dropped, a package whose bundle carries superseded records
// (widened ones) is rewritten as one chunk of its live records plus a marker naming it as the first
// chunk to read (stream_record.h, GENERATIONS): the next start reads only that generation.
// ============================================================================
#include "ext/stream_cache/stream_cache.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "ext/stream_cache/segment_stream.h"
#include "ext/stream_cache/stream_record.h"
#include "ext/derived_cache/derived_cache.h"
#include "ext/registry.h"
#include "options.h"                  // Opt().streamCacheShadow (read once at boot)
#include "platform_file.h"            // Platform::FileSize64 / FileLastWrite: the package's identity
#include "create_on_demand/payload.h"
#include "create_on_demand/stream_locate.h"
#include "texture_quality/texture_quality.h"   // ext.texture_quality: the drop the records are keyed on
#include "engine/UpdateLoop.h"
#include "io/FileStream.h"
#include "io/FileSystem.h"
#include "io/PackageFile.h"
#include "io/Stream.h"
#include "object/External.h"
#include "system/string_internal.h"   // RtSystem::RtMemAlloc: the stream's storage, freed by IO::Stream::operator delete
#include "Game/MenuDialogLoopT.h"     // the boot's last load bracket

namespace Ext {
namespace StreamCache {

namespace {

constexpr uint32_t kProducer = DerivedCache::MakeProducer('S', 'T', 'R', 'M');
constexpr uint32_t kVersion  = kChunkVersion;   // the record / chunk format

// One package (one identity), across remounts.
struct Package {
    uint64_t identity  = 0;
    uint32_t crc       = 0;
    uint32_t fileCount = 0;
    uint32_t chunks    = 0;   // the next chunk ordinal: past the chunks read back + put this run
    uint32_t markers   = 0;   // the next generation-marker ordinal
    Bundle   bundle;
    std::vector<uint8_t> pending;        // the chunk under construction (this bracket's records)
    uint32_t pendingCount = 0;
};

struct Slot {
    const IO::PackageFile* pkg = nullptr;   // the FileSystem's PackageFile at that pack index
    Package* package = nullptr;
};

struct Stats {
    uint64_t hitsWhole = 0, hitsElided = 0, misses = 0, loose = 0, afterBoot = 0;
    uint64_t recorded = 0, recordedBytes = 0, extended = 0;
    uint64_t tooBig = 0, mismatch = 0, readFailed = 0, loadFailed = 0;
    uint64_t servedFailed = 0;    // the fallback could not open retail's stream
    uint64_t chunksPut = 0, chunkBytesPut = 0;
    uint64_t chunksRead = 0, chunksSkipped = 0;          // read back; below a marker's start, not read
    uint64_t consolidated = 0, consolidatedBytes = 0;    // live-record chunks put at the boot's end
    double   chunkLoadMs = 0;     // reading the stored chunks back (Find + parse)
    uint64_t shadowChecked = 0, shadowMismatch = 0;      // BANISHED_STREAMCACHE_SHADOW
};

DerivedCache::Store* g_store = nullptr;
std::mutex g_mu;
std::unordered_map<uint64_t, std::unique_ptr<Package>> g_packages;   // by identity
std::vector<Slot> g_slots;                                            // by pack index
Stats g_stats;
bool g_payloadsSkipped = false;   // ext.create_on_demand on: what a load consumes depends on it
int  g_droppedLevels = 0;         // ext.texture_quality's drop: so does it (TEXQUALITY.md)
bool g_bootDone = false;          // the menu's bracket has closed: nothing served or recorded after

// A load this thread is inside.
struct Active {
    IO::CompressedFileStream* cfs = nullptr;
    SegmentStream*            stream = nullptr;   // serving from a record
    Package*                  package = nullptr;
    Recorder                  recorder;           // recording the retail read (or the fallback's)
};

thread_local std::vector<Active> t_stack;

// shape[2] is the entry's kind: 0 a chunk of records, 1 a generation marker (stream_record.h).
DerivedCache::Key PackageKey(const Package& p, uint32_t kind, uint32_t ordinal) {
    DerivedCache::Key k;
    k.producer   = kProducer;
    k.version    = kVersion;
    k.shape[0]   = p.crc;
    k.shape[1]   = (g_payloadsSkipped ? 1u : 0u) | (static_cast<uint32_t>(g_droppedLevels) << 1);
    k.shape[2]   = kind;
    k.shape[3]   = ordinal;
    k.sourceSize = p.fileCount;
    k.sourceHash = p.identity;
    return k;
}

DerivedCache::Key ChunkKey(const Package& p, uint32_t chunk) {
    return PackageKey(p, 0, chunk);
}

DerivedCache::Key MarkerKey(const Package& p, uint32_t marker) {
    return PackageKey(p, 1, marker);
}

// The package's identity (stream_record.h, PackageIdentity): its header fields, its name, its file
// size and last write time and its whole entry table (offset and size of every entry), so a rebuilt
// or replaced package is another identity even at the same path. Formed once per package: the
// header and the table are what the engine already holds, the size and the time two queries on the
// package's open handle -- nothing of the package's bytes is read. A record is then trusted by
// (identity, entry offset, size) alone: the ruling of 2026-10-24 (STREAMCACHE2.md).
uint64_t Identity(const IO::PackageFile& pkg) {
    PackageStamp s;
    s.crc         = pkg._crc;
    s.version     = static_cast<uint32_t>(pkg._version);
    s.userVersion = static_cast<uint32_t>(pkg._userVersion);
    s.fileCount   = static_cast<uint32_t>(pkg._entries._size);
    s.name        = pkg._name._data;
    const Platform::FileHandle h = pkg._file.winHandle;
    if (h != nullptr && h != Platform::kInvalidFileHandle) {
        s.fileSize = Platform::FileSize64(h);
        uint64_t t = 0;
        if (Platform::FileLastWrite(h, &t) != 0) s.writeTime = t;
    }
    if (pkg._entries._data != nullptr && pkg._entries._size > 0) {
        // Offset and size of every entry (not the entry's name, a String pointer per run).
        s.table.resize(static_cast<size_t>(pkg._entries._size) * 2);
        for (int32_t i = 0; i < pkg._entries._size; ++i) {
            s.table[static_cast<size_t>(i) * 2]     = pkg._entries._data[i].offset;
            s.table[static_cast<size_t>(i) * 2 + 1] = pkg._entries._data[i].size;
        }
    }
    return PackageIdentity(s);
}

// Under g_mu, BANISHED_STREAMCACHE_SHADOW only (a harness check, STREAMCACHE2.md): the hit's entry
// read from the package as the miss path reads it, compared with the record's cached count and
// directory. A difference is a package the identity failed to tell apart.
void ShadowCompare(const RecordView& rec, const RtIO::FileInfo& fi, uint32_t crc) {
    IO::FileSystem* const sys = IO::FileSystem::_singleton;
    ++g_stats.shadowChecked;
    const uint8_t* const live = sys->PackedMapSpan(fi, 0, static_cast<int32_t>(rec.HeadBytes()));
    if (live != nullptr && std::memcmp(live, rec.head, rec.HeadBytes()) == 0) return;
    if (g_stats.shadowMismatch < 16) {
        std::fprintf(stderr, "[StreamCache] SHADOW MISMATCH package %08x entry %llu size %u blocks %u%s\n", crc,
                     static_cast<unsigned long long>(rec.id.offset), rec.id.size, rec.blockCount,
                     live == nullptr ? " (not mapped)" : "");
    }
    ++g_stats.shadowMismatch;
}

// Under g_mu. The Package of pack index `index`, made (and its stored chunks loaded) on first use.
Package* PackageAt(uint32_t index) {
    IO::FileSystem* const sys = IO::FileSystem::_singleton;
    if (index >= static_cast<uint32_t>(sys->_packfiles._size)) return nullptr;
    const IO::PackageFile* const pkg = sys->_packfiles[index];
    if (pkg == nullptr) return nullptr;
    if (g_slots.size() <= index) g_slots.resize(index + 1);
    Slot& slot = g_slots[index];
    if (slot.pkg == pkg && slot.package != nullptr) return slot.package;
    const uint64_t id = Identity(*pkg);
    std::unique_ptr<Package>& p = g_packages[id];
    if (p == nullptr) {
        p.reset(new Package());
        p->identity  = id;
        p->crc       = pkg->_crc;
        p->fileCount = static_cast<uint32_t>(pkg->_entries._size);
        const auto t0 = std::chrono::steady_clock::now();
        Package* const q = p.get();
        const ReadBackResult r = ReadBack(
            q->bundle, [q](uint32_t i, std::vector<uint8_t>& out) { return g_store->Find(ChunkKey(*q, i), out); },
            [q](uint32_t i, std::vector<uint8_t>& out) { return g_store->Find(MarkerKey(*q, i), out); });
        for (const uint32_t bad : r.malformed) {
            std::fprintf(stderr, "[StreamCache] package %08x chunk %u malformed: ignored\n", p->crc, bad);
        }
        if (r.startMissing) {
            std::fprintf(stderr, "[StreamCache] package %08x: the newest generation's first chunk is not stored; "
                         "read from chunk %u\n", p->crc, r.start);
        }
        p->chunks  = r.next;
        p->markers = r.markers;
        g_stats.chunksRead += r.read;
        g_stats.chunksSkipped += r.start;
        g_stats.chunkLoadMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    slot.pkg     = pkg;
    slot.package = p.get();
    return p.get();
}

// The record stream's fallback: retail's compressed stream, opened on the load's own
// CompressedFileStream (its packed FileStream is open; this is the Open External::Load skipped).
IO::CompressedStream* OpenFallback(SegmentStream& self, void* context) {
    (void)context;
    // The load this stream serves: on this thread's stack (the top, unless a nested load is inside
    // a read of it, which the serializer never does; found by the stream to be sure).
    Active* a = nullptr;
    for (size_t i = t_stack.size(); i > 0; --i) {
        if (t_stack[i - 1].stream == &self) {
            a = &t_stack[i - 1];
            break;
        }
    }
    if (a == nullptr) return nullptr;
    if (!a->cfs->IO::CompressedStream::Open(a->cfs->_file, 1)) return nullptr;
    // What the rest of the load reads widens the record: seed the recorder with it and let the tap
    // (the fallback's reads go through CompressedStream::Read) add the rest.
    a->recorder.Seed(*a->stream->Record());
    return a->cfs;
}

IO::Stream* LoadBegin(IO::CompressedFileStream& cfs) {
    t_stack.emplace_back();
    Active& a = t_stack.back();
    a.cfs = &cfs;
    if (g_bootDone) {
        ++g_stats.afterBoot;
        return nullptr;
    }
    const RtIO::FileInfo& fi = cfs._file._file;
    IO::FileSystem* const sys = IO::FileSystem::_singleton;
    Platform::FileHandle handle = nullptr;
    int64_t entry = 0;
    if (!sys->PackedEntryOrigin(fi, &handle, &entry) || fi.size < 4) {
        ++g_stats.loose;
        return nullptr;
    }
    EntryId id;
    id.offset = static_cast<uint64_t>(entry);
    id.size   = static_cast<uint32_t>(fi.size);

    {
        // A hit reads nothing of the package: the record is the entry's (package identity, offset,
        // size) and carries the entry's directory.
        std::lock_guard<std::mutex> lk(g_mu);
        Package* const pkg = PackageAt(static_cast<uint32_t>(fi.handle) >> 24);
        if (pkg == nullptr) {
            ++g_stats.loose;
            return nullptr;
        }
        a.package = pkg;
        const RecordView* const rec = pkg->bundle.Find(id);
        if (rec != nullptr) {
            if (rec->segCount == 1 && rec->byteCount == rec->total) ++g_stats.hitsWhole;
            else ++g_stats.hitsElided;
            if (Opt().streamCacheShadow) ShadowCompare(*rec, fi, pkg->crc);
            // From the engine allocator: IO::Stream's class operator delete frees there
            // (root_operator_delete.cpp), as every stream the engine makes.
            void* const mem = RtSystem::RtMemAlloc(sizeof(SegmentStream), 8);
            a.stream = ::new (mem) SegmentStream(rec, handle, entry, &OpenFallback, nullptr);
            return a.stream;
        }
    }

    // A miss: the entry's block count and directory from the package mapping, recorded with the
    // stream. (The package lock is not held over the mapping's page faults.)
    const uint8_t* const head = sys->PackedMapSpan(fi, 0, 4);
    if (head == nullptr) {
        ++g_stats.loose;
        a.package = nullptr;
        return nullptr;
    }
    int32_t n = 0;
    std::memcpy(&n, head, 4);
    if (n <= 0 || static_cast<int64_t>(n) * 8 + 4 > fi.size) {
        ++g_stats.loose;
        a.package = nullptr;
        return nullptr;
    }
    // The count and the directory are contiguous in the mapping: one span.
    const uint8_t* const entryHead = sys->PackedMapSpan(fi, 0, 4 + n * 8);
    if (entryHead == nullptr) {
        ++g_stats.loose;
        a.package = nullptr;
        return nullptr;
    }
    const uint32_t total = DirectoryTotal(entryHead);
    std::lock_guard<std::mutex> lk(g_mu);
    ++g_stats.misses;
    a.recorder.Begin(id, total, entryHead);
    return nullptr;
}

void ReadTap(IO::CompressedStream& cs, int32_t pos, int32_t n, const void* dst, bool ok) {
    if (t_stack.empty()) return;
    Active& a = t_stack.back();
    if (static_cast<IO::CompressedStream*>(a.cfs) != &cs || !a.recorder.Active()) return;
    if (!ok || pos < 0 || n < 0) {
        a.recorder.Fail();
        return;
    }
    a.recorder.Note(static_cast<uint32_t>(pos), static_cast<uint32_t>(n), static_cast<const uint8_t*>(dst));
}

void LoadEnd(IO::CompressedFileStream& cfs, IO::Stream* alt, const Object::External* result) {
    (void)alt;
    if (t_stack.empty() || t_stack.back().cfs != &cfs) return;   // not a pair this extension opened
    Active a = std::move(t_stack.back());
    t_stack.pop_back();
    if (a.stream != nullptr) {
        if (a.stream->Failed()) {
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_stats.servedFailed == 0) {
                std::fprintf(stderr, "[StreamCache] a load read outside its record and retail's stream would not open "
                             "(package %08x, entry %lld)\n", a.package->crc, static_cast<long long>(a.stream->Entry()));
            }
            ++g_stats.servedFailed;
        }
        const bool extended = a.stream->Extended();
        delete a.stream;
        a.stream = nullptr;
        if (!extended) return;
        std::lock_guard<std::mutex> lk(g_mu);
        ++g_stats.extended;
        // falls through to the recorder below: the widened record replaces the old one
    }
    if (!a.recorder.Active()) return;
    if (result == nullptr) a.recorder.Fail();
    std::unique_lock<std::mutex> lk(g_mu, std::defer_lock);
    if (!lk.owns_lock()) lk.lock();
    if (a.recorder.Overflowed()) {
        ++g_stats.tooBig;
    } else if (a.recorder.Bad()) {
        if (result == nullptr) ++g_stats.loadFailed;
        else ++g_stats.mismatch;
    }
    Package& p = *a.package;
    if (p.pendingCount == 0) BeginChunk(p.pending);
    const uint32_t consumed = a.recorder.Consumed();
    if (a.recorder.Finish(p.pending)) {
        ++p.pendingCount;
        ++g_stats.recorded;
        g_stats.recordedBytes += consumed;
    }
}

// Under g_mu, at the boot's last bracket. A bundle carrying superseded records (or too many chunks)
// goes out as one chunk of its live records at the next ordinal, then a marker naming that ordinal:
// the next start reads from it and never asks for the chunks below. The chunk is queued before the
// marker (the writer stores in queue order), and ReadBack checks the chunk is there anyway.
void Consolidate(Package& p) {
    if (!ShouldConsolidate(p.bundle)) return;
    std::vector<uint8_t> merged = p.bundle.Consolidate();
    if (merged.empty()) return;
    const size_t bytes = merged.size();
    const uint32_t start = p.chunks;
    std::fprintf(stderr, "[StreamCache] package %08x: %zu live records of %zu chunks (%.1f MB) rewritten as chunk "
                 "%u (%.1f MB), marker %u\n", p.crc, p.bundle.Records(), p.bundle.Chunks(),
                 p.bundle.Bytes() / 1048576.0, start, bytes / 1048576.0, p.markers);
    g_store->Put(ChunkKey(p, start), std::move(merged));
    g_store->Put(MarkerKey(p, p.markers), EncodeMarker(start));
    ++p.chunks;
    ++p.markers;
    ++g_stats.consolidated;
    g_stats.consolidatedBytes += bytes;
}

void LoadDone(const std::type_info& loop) {
    std::lock_guard<std::mutex> lk(g_mu);
    const bool bootEnds = !g_bootDone && loop == typeid(MenuDialogLoopT);
    size_t bundleBytes = 0;
    size_t records = 0;
    for (auto& kv : g_packages) {
        Package& p = *kv.second;
        if (p.pendingCount > 0) {
            EndChunk(p.pending, p.pendingCount);
            std::vector<uint8_t> copy = p.pending;
            g_store->Put(ChunkKey(p, p.chunks), std::move(copy));
            ++g_stats.chunksPut;
            g_stats.chunkBytesPut += p.pending.size();
            if (!p.bundle.AddChunk(std::move(p.pending))) {
                std::fprintf(stderr, "[StreamCache] package %08x: the chunk just made does not parse\n", p.crc);
            }
            p.pending = std::vector<uint8_t>();
            p.pendingCount = 0;
            ++p.chunks;
        }
        bundleBytes += p.bundle.Bytes();
        records += p.bundle.Records();
        if (bootEnds) Consolidate(p);
    }
    g_store->RequestFlush();
    std::fprintf(stderr, "[StreamCache] load end (run total): hits %llu (whole %llu, elided %llu), extended %llu, misses "
                 "%llu, not packed %llu, after boot %llu; recorded %llu (%.1f MB; too big %llu, overlap mismatch %llu, read "
                 "failed %llu, load failed %llu); fallback failed %llu; packages %zu, records %zu (%.1f MB in memory, "
                 "read back in %.0f ms), chunks put %llu (%.1f MB); chunks read %llu, superseded skipped %llu, "
                 "consolidated %llu (%.1f MB)\n",
                 (unsigned long long)(g_stats.hitsWhole + g_stats.hitsElided), (unsigned long long)g_stats.hitsWhole,
                 (unsigned long long)g_stats.hitsElided, (unsigned long long)g_stats.extended,
                 (unsigned long long)g_stats.misses, (unsigned long long)g_stats.loose,
                 (unsigned long long)g_stats.afterBoot, (unsigned long long)g_stats.recorded,
                 g_stats.recordedBytes / 1048576.0, (unsigned long long)g_stats.tooBig,
                 (unsigned long long)g_stats.mismatch, (unsigned long long)g_stats.readFailed,
                 (unsigned long long)g_stats.loadFailed, (unsigned long long)g_stats.servedFailed, g_packages.size(),
                 records, bundleBytes / 1048576.0, g_stats.chunkLoadMs, (unsigned long long)g_stats.chunksPut,
                 g_stats.chunkBytesPut / 1048576.0, (unsigned long long)g_stats.chunksRead,
                 (unsigned long long)g_stats.chunksSkipped, (unsigned long long)g_stats.consolidated,
                 g_stats.consolidatedBytes / 1048576.0);
    if (Opt().streamCacheShadow) {
        std::fprintf(stderr,
                     "[StreamCache] shadow (run total): %llu hits checked against the package, %llu mismatches\n",
                     (unsigned long long)g_stats.shadowChecked, (unsigned long long)g_stats.shadowMismatch);
    }
    if (bootEnds) {
        // The boot's last bracket: the derived cache may rewrite its files now (derived_cache.h,
        // "THE REWRITE"), on its writer thread.
        g_store->RequestCompact();
        // The records have done their work for this run.
        for (auto& kv : g_packages) kv.second->bundle.Clear();
        g_bootDone = true;
        std::fprintf(stderr, "[StreamCache] boot over: %.1f MB of records dropped; later loads take retail's path\n",
                     bundleBytes / 1048576.0);
    }
}

// ext.create_on_demand's Locate / Skip over a record stream: the same Locator the compressed stream
// gives (the package mapping, the entry, the decompressed position), the range checked against the
// entry's block directory as the record caches it -- RangeInStream reads the same 4 + 8n bytes it
// would read from the mapping, without touching the package. The payload's bytes are read from the
// mapping only when the renderer materialises it (Redecode).
bool AltLocate(IO::Stream* stream, int32_t len, ::CreateOnDemand::Locator* out) {
    SegmentStream* const ss = dynamic_cast<SegmentStream*>(stream);
    if (ss == nullptr || len < 0) return false;
    ::CreateOnDemand::PackageMap* const map = ::CreateOnDemand::Maps().Acquire(ss->Package());
    if (map == nullptr) return false;
    ::CreateOnDemand::Locator loc;
    loc.map   = map;
    loc.entry = static_cast<uint64_t>(ss->Entry());
    loc.size  = ss->Record()->id.size;
    loc.pos   = static_cast<int32_t>(ss->GetCurrentPos());
    loc.len   = len;
    if (loc.entry + loc.size > map->map.size) return false;
    if (!::CreateOnDemand::RangeInStream(ss->Record()->head, loc.size, loc.pos, loc.len)) return false;
    *out = std::move(loc);
    return true;
}

bool AltSkip(IO::Stream* stream, int32_t len) {
    SegmentStream* const ss = dynamic_cast<SegmentStream*>(stream);
    if (ss == nullptr) return false;
    // Within the record's total, or nothing done (SkipIfWithin then reads the bytes). A Skip after
    // AltLocate always passes: RangeInStream checked the same position and length.
    return ss->SkipWithin(len);
}

}  // namespace

void Boot() {
    if (!Ext().stream_cache) return;
    g_store = DerivedCache::OpenShared();
    if (g_store == nullptr) {
        std::fprintf(stderr, "[StreamCache] no derived cache (faithful profile, no documents folder, or the cache "
                     "cannot be opened): off for this run\n");
        return;
    }
    g_payloadsSkipped = Ext().create_on_demand;
    g_droppedLevels   = ::TextureQuality::Drop();   // set by Ext::TextureQuality::Boot, which runs first
    ::Object::ExternalFactory::_extLoadBegin = &LoadBegin;
    ::Object::ExternalFactory::_extLoadEnd   = &LoadEnd;
    ::IO::CompressedStream::_extReadTap      = &ReadTap;
    ::Engine::LoaderLoop::_extLoadDone       = &LoadDone;
    ::CreateOnDemand::g_altLocate            = &AltLocate;
    ::CreateOnDemand::g_altSkip              = &AltSkip;
}

}  // namespace StreamCache
}  // namespace Ext
