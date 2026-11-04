// ============================================================================
// src/ext/derived_cache/derived_cache.cpp -- the derived-data cache (NOT RETAIL)
// ============================================================================
// derived_cache.h has the contract and the file layout. Issue #13, docs/extensions/handins/COMPRESSAO.md.
// ============================================================================
#include "ext/derived_cache/derived_cache.h"

#include "ext/derived_cache/hash64.h"
#include "platform_thread.h"   // ThreadBackgroundMode: the rewrite

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace Ext {
namespace DerivedCache {

namespace {

constexpr uint32_t kRecordMagic = 0x31454344u;            // "DCE1"
constexpr uint64_t kHeaderSize  = 32;
constexpr uint64_t kMaxQueued   = 256ull * 1024 * 1024;   // Put waits past this
constexpr uint32_t kChunk       = 1u << 20;               // one FileRead / FileWrite at most
constexpr uint32_t kUsedRecord  = 16;                     // derived.used: KeyId, stamp, 0

// What an eviction brings derived.dat down to: 90% of the cap, lower when the session before refused
// more than the other 10% (room for what it derived anew), but never below half the cap.
uint64_t EvictTarget(uint64_t capBytes, uint64_t refusedBytes) {
    uint64_t target = capBytes - capBytes / 10;
    if (refusedBytes < capBytes && capBytes - refusedBytes < target) target = capBytes - refusedBytes;
    return std::max(target, capBytes / 2);
}

// The quiet spell: this long after the cache's last Find / Put the loads are over. The usage stamps
// are written then (whoever the consumer is: ext.package_cache never sends RequestFlush), and the
// rewrite runs then unless asked for at once (RequestCompact). 10 s; the unit test shortens it.
std::atomic<int> g_quietMs{10000};

std::chrono::milliseconds QuietSpell() {
    return std::chrono::milliseconds(g_quietMs.load(std::memory_order_relaxed));
}

// Set by the unit test: the rewrite stops after replacing derived.dat, as a crash there would.
std::atomic<bool> g_crashBetweenReplaces{false};

// All three files start with this.
struct FileHeader {
    char     magic[8];     // "OBADCDAT" / "OBADCIDX" / "OBADCUSE"
    uint32_t format;       // kFormatVersion
    uint32_t recordSize;   // 80 for the index, kUsedRecord for derived.used, 0 for the data
    // derived.dat / derived.idx: [0] the generation (the same in both; every rewrite makes a new
    // one), [1] 0. derived.used: [0] its session, [1] the payload bytes that session refused.
    uint64_t reserved[2];
};
static_assert(sizeof(FileHeader) == kHeaderSize, "a cache file header is 32 bytes");

FileHeader MakeHeader(const char* magic, uint32_t recordSize, uint64_t generation = 0) {
    FileHeader h = {};
    std::memcpy(h.magic, magic, 8);
    h.format      = kFormatVersion;
    h.recordSize  = recordSize;
    h.reserved[0] = generation;
    return h;
}

bool HeaderIs(const FileHeader& h, const char* magic, uint32_t recordSize) {
    return std::memcmp(h.magic, magic, 8) == 0 && h.format == kFormatVersion && h.recordSize == recordSize;
}

// UTF-8 -> the Platform path spelling (UTF-16). A malformed byte becomes U+FFFD.
std::u16string ToPath(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        uint32_t c = 0xFFFD;
        size_t extra = 0;
        if (b < 0x80) {
            c = b;
        } else if ((b & 0xE0) == 0xC0) {
            c = b & 0x1F;
            extra = 1;
        } else if ((b & 0xF0) == 0xE0) {
            c = b & 0x0F;
            extra = 2;
        } else if ((b & 0xF8) == 0xF0) {
            c = b & 0x07;
            extra = 3;
        }
        bool ok = (b < 0x80) || extra != 0;
        for (size_t k = 1; ok && k <= extra; ++k) {
            if (i + k >= s.size() || (static_cast<uint8_t>(s[i + k]) & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            c = (c << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
        }
        if (!ok) {
            out.push_back(u'\xFFFD');
            ++i;
            continue;
        }
        i += extra + 1;
        if (c >= 0x10000) {
            c -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(c));
        }
    }
    return out;
}

std::u16string FilePath(const std::string& dir, const char* name) {
    return ToPath(dir + "\\" + name);
}

bool Valid(Platform::FileHandle h) {
    return h != Platform::kInvalidFileHandle && h != nullptr;
}

void CloseHandle(Platform::FileHandle& h) {
    if (Valid(h)) Platform::FileClose(h);
    h = Platform::kInvalidFileHandle;
}

uint64_t RecordCheck(const IndexRecord& r) {
    return Hash64(&r, offsetof(IndexRecord, check));
}

IndexRecord MakeRecord(const Key& k, uint64_t offset, uint64_t size, uint64_t payloadHash) {
    IndexRecord r = {};
    r.magic       = kRecordMagic;
    r.producer    = k.producer;
    r.version     = k.version;
    std::memcpy(r.shape, k.shape, sizeof(r.shape));
    r.sourceSize  = k.sourceSize;
    r.sourceHash  = k.sourceHash;
    r.offset      = offset;
    r.size        = size;
    r.payloadHash = payloadHash;
    r.check       = RecordCheck(r);
    return r;
}

Key RecordKey(const IndexRecord& r) {
    Key k;
    k.producer   = r.producer;
    k.version    = r.version;
    std::memcpy(k.shape, r.shape, sizeof(k.shape));
    k.sourceSize = r.sourceSize;
    k.sourceHash = r.sourceHash;
    return k;
}

// Whole-file helpers over the Platform seam (used for the small files: the keep list, the marker).
bool ReadWhole(const std::u16string& path, std::vector<uint8_t>& out) {
    Platform::FileHandle h = Platform::FileOpenReadShared(path.c_str());
    if (!Valid(h)) return false;
    const int64_t size = Platform::FileSize64(h);
    bool ok = size >= 0 && size < (int64_t{1} << 31);
    if (ok) {
        out.resize(static_cast<size_t>(size));
        uint32_t got = 0;
        ok = size == 0 || (Platform::FileRead(h, out.data(), static_cast<uint32_t>(size), &got) != 0 &&
                           got == static_cast<uint32_t>(size));
    }
    Platform::FileClose(h);
    return ok;
}

bool WriteWhole(const std::u16string& path, const void* data, size_t size) {
    const std::u16string tmp = path + u".tmp";
    Platform::FileHandle h = Platform::FileCreateWrite(tmp.c_str());
    if (!Valid(h)) return false;
    uint32_t wrote = 0;
    bool ok = size == 0 || (Platform::FileWrite(h, data, static_cast<uint32_t>(size), &wrote) != 0 &&
                            wrote == static_cast<uint32_t>(size));
    Platform::FileClose(h);
    // Windows: MoveFileExW cannot replace a file another process holds open without
    // FILE_SHARE_DELETE (a virus scanner, the search indexer, a plain fopen reader); the hold is
    // usually brief. The replace is tried again for about 100 ms before the write is given up: a
    // given-up derived.used write loses the session's stamps (the writer has cleared _usedDirty).
    // POSIX rename replaces an open file, so the first try succeeds there.
    if (ok) {
        ok = Platform::FileReplace(tmp.c_str(), path.c_str()) != 0;
        for (int retry = 0; !ok && retry < 20; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ok = Platform::FileReplace(tmp.c_str(), path.c_str()) != 0;
        }
    }
    if (!ok) Platform::FileDelete(tmp.c_str());
    return ok;
}

uint64_t FileBytes(const std::u16string& path) {
    Platform::FileHandle h = Platform::FileOpenReadShared(path.c_str());
    if (!Valid(h)) return 0;
    const int64_t size = Platform::FileSize64(h);
    Platform::FileClose(h);
    return size > 0 ? static_cast<uint64_t>(size) : 0;
}

// Keeps the last record of each key, in file order; returns how many earlier ones it dropped.
size_t DropSuperseded(std::vector<IndexRecord>& records) {
    std::unordered_multimap<uint64_t, size_t> last;   // sourceHash -> the index of the key's latest record
    std::vector<bool> dropped(records.size(), false);
    size_t count = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        const Key key = RecordKey(records[i]);
        bool seen = false;
        auto range = last.equal_range(key.sourceHash);
        for (auto it = range.first; it != range.second; ++it) {
            if (!SameKey(RecordKey(records[it->second]), key)) continue;
            dropped[it->second] = true;
            it->second = i;
            seen = true;
            ++count;
            break;
        }
        if (!seen) last.emplace(key.sourceHash, i);
    }
    if (count == 0) return 0;
    size_t kept = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        if (!dropped[i]) records[kept++] = records[i];
    }
    records.resize(kept);
    return count;
}

}  // namespace

bool SameKey(const Key& a, const Key& b) {
    return a.producer == b.producer && a.version == b.version && a.shape[0] == b.shape[0] &&
           a.shape[1] == b.shape[1] && a.shape[2] == b.shape[2] && a.shape[3] == b.shape[3] &&
           a.sourceSize == b.sourceSize && a.sourceHash == b.sourceHash;
}

uint64_t KeyId(const Key& key) {
    uint8_t bytes[40];
    std::memcpy(bytes, &key.producer, 4);
    std::memcpy(bytes + 4, &key.version, 4);
    std::memcpy(bytes + 8, key.shape, 16);
    std::memcpy(bytes + 24, &key.sourceSize, 8);
    std::memcpy(bytes + 32, &key.sourceHash, 8);
    return Hash64(bytes, sizeof(bytes));
}

void SetCrashBetweenReplacesForTest(bool on) {
    g_crashBetweenReplaces.store(on);
}

void SetQuietSpellForTest(int ms) {
    g_quietMs.store(ms, std::memory_order_relaxed);
}

bool RequestClear(const std::string& dir) {
    const char mark[] = "clear\n";
    return WriteWhole(FilePath(dir, "derived.clear"), mark, sizeof(mark) - 1);
}

bool ClearRequested(const std::string& dir) {
    return Platform::PathAttributes(FilePath(dir, "derived.clear").c_str()) != Platform::kInvalidPathAttributes;
}

uint64_t BytesInDir(const std::string& dir) {
    return FileBytes(FilePath(dir, "derived.dat")) + FileBytes(FilePath(dir, "derived.idx"));
}

Store::~Store() {
    Close();
}

// Not analysed: no other thread reaches the store until Open returns (derived_cache.h, THE LOCKS).
Store::OpenStatus Store::Open(const std::string& dir, uint64_t capBytes) NO_THREAD_SAFETY_ANALYSIS {
    Close();
    _dir = dir;
    _cap = capBytes;
    // The parents too: on a fresh profile <Documents>\Banished does not exist until the first
    // settings.cfg or save write, and a cache consumer opens before that.
    if (!Platform::EnsureDirChain(ToPath(dir).c_str())) {
        std::fprintf(stderr, "[DerivedCache] cannot create %s: no cache this run\n", dir.c_str());
        return OpenStatus::Failed;
    }
    _lock = Platform::FileOpenExclusive(FilePath(dir, "derived.lock").c_str());
    if (!Valid(_lock)) {
        _lock = Platform::kInvalidFileHandle;
        return OpenReadOnly(dir);
    }
    if (ClearRequested(dir)) {
        Platform::FileDelete(FilePath(dir, "derived.dat").c_str());
        Platform::FileDelete(FilePath(dir, "derived.idx").c_str());
        Platform::FileDelete(FilePath(dir, "derived.used").c_str());
        Platform::FileDelete(FilePath(dir, "derived.clear").c_str());
        std::fprintf(stderr, "[DerivedCache] cleared (requested from the Options dialog)\n");
    }
    ReadUsedList();
    _dat = Platform::FileOpenWriteShared(FilePath(dir, "derived.dat").c_str(), true);
    _idx = Platform::FileOpenWriteShared(FilePath(dir, "derived.idx").c_str(), false);
    if (!Valid(_dat) || !Valid(_idx) || !LoadIndex()) {
        CloseHandle(_dat);
        CloseHandle(_idx);
        CloseHandle(_lock);
        std::fprintf(stderr, "[DerivedCache] cannot open the cache files in %s: no cache this run\n", dir.c_str());
        return OpenStatus::Failed;
    }
    // What a rewrite that never reached its replaces left (a crash, a kill: up to the cache's size).
    Platform::FileDelete(FilePath(dir, "derived.dat.new").c_str());
    Platform::FileDelete(FilePath(dir, "derived.idx.new").c_str());
    if (_dataEnd > kHeaderSize) Platform::FileMapOpen(_dat, &_map);
    _open         = true;
    _stop         = false;
    _disabled     = false;
    _closing      = false;
    _compactArmed = false;
    _compactNow   = false;
    _compactDone  = false;
    _lastActivity = std::chrono::steady_clock::now();
    const bool rewrite = NeedsCompact();
    _writer = std::thread([this] { WriterMain(); });
    std::fprintf(stderr, "[DerivedCache] %s: %llu entries, %.1f MB (cap %llu MB)%s\n", dir.c_str(),
                 static_cast<unsigned long long>(_records.size()),
                 static_cast<double>(_dataEnd + _indexEnd) / 1048576.0,
                 static_cast<unsigned long long>(capBytes >> 20),
                 rewrite ? "; rewritten on the writer thread once the loads are over" : "");
    return OpenStatus::Opened;
}

// The lock is another instance's: read what it has stored, write nothing (derived_cache.h, "A
// SECOND INSTANCE"). A clear request is the holder's to carry out.
Store::OpenStatus Store::OpenReadOnly(const std::string& dir) NO_THREAD_SAFETY_ANALYSIS {   // Open's
    _readOnly = true;
    _dat = Platform::FileOpenReadShared(FilePath(dir, "derived.dat").c_str());
    _idx = Platform::FileOpenReadShared(FilePath(dir, "derived.idx").c_str());
    if (!Valid(_dat) || !Valid(_idx) || !LoadIndex()) {
        CloseHandle(_dat);
        CloseHandle(_idx);
        _readOnly = false;
        std::fprintf(stderr, "[DerivedCache] %s is in use by another instance and cannot be read: no cache this run\n",
                     dir.c_str());
        return OpenStatus::InUse;
    }
    _open     = true;
    _disabled = false;
    std::fprintf(stderr, "[DerivedCache] %s is in use by another instance: reading its %llu entries (%.1f MB), "
                         "storing nothing this run\n",
                 dir.c_str(), static_cast<unsigned long long>(_records.size()),
                 static_cast<double>(_dataEnd) / 1048576.0);
    return OpenStatus::ReadOnly;
}

void Store::ResetFiles() NO_THREAD_SAFETY_ANALYSIS {   // Open's (LoadIndex)
    ++_generation;
    Platform::FileTruncate(_dat, 0);
    Platform::FileTruncate(_idx, 0);
    const FileHeader dh = MakeHeader("OBADCDAT", 0, _generation);
    const FileHeader ih = MakeHeader("OBADCIDX", sizeof(IndexRecord), _generation);
    WriteAt(_dat, 0, &dh, sizeof(dh));
    WriteAt(_idx, 0, &ih, sizeof(ih));
    _dataEnd  = kHeaderSize;
    _indexEnd = kHeaderSize;
}

// A rewrite that stopped between its two replaces (a crash, a kill) left the new derived.dat beside
// the old derived.idx; its new index is still derived.idx.new. Finish the rewrite when that file is
// the data file's generation. The holder only.
bool Store::FinishInterruptedRewrite(uint64_t datGeneration) NO_THREAD_SAFETY_ANALYSIS {   // Open's
    const std::u16string idxNew = FilePath(_dir, "derived.idx.new");
    std::vector<uint8_t> head;
    Platform::FileHandle h = Platform::FileOpenReadShared(idxNew.c_str());
    if (!Valid(h)) return false;
    head.resize(sizeof(FileHeader));
    uint32_t got = 0;
    const bool read = Platform::FileRead(h, head.data(), sizeof(FileHeader), &got) != 0 && got == sizeof(FileHeader);
    Platform::FileClose(h);
    if (!read) return false;
    FileHeader nh = {};
    std::memcpy(&nh, head.data(), sizeof(nh));
    if (!HeaderIs(nh, "OBADCIDX", sizeof(IndexRecord)) || nh.reserved[0] != datGeneration) return false;
    const std::u16string idx = FilePath(_dir, "derived.idx");
    CloseHandle(_idx);
    const bool replaced = Platform::FileReplace(idxNew.c_str(), idx.c_str()) != 0;
    _idx = Platform::FileOpenWriteShared(idx.c_str(), false);
    return replaced && Valid(_idx);
}

// The longest valid prefix of the index; both files truncated to it (the holder). A rewrite the
// files need (past the cap, superseded copies) is left to the writer thread (NeedsCompact).
bool Store::LoadIndex() NO_THREAD_SAFETY_ANALYSIS {   // Open's
    _records.clear();
    int64_t datSize = Platform::FileSize64(_dat);
    int64_t idxSize = Platform::FileSize64(_idx);
    if (datSize < 0 || idxSize < 0) return false;
    FileHeader dh = {};
    FileHeader ih = {};
    const bool headersOk = datSize >= static_cast<int64_t>(kHeaderSize) &&
                           idxSize >= static_cast<int64_t>(kHeaderSize) && ReadAt(_dat, 0, &dh, sizeof(dh)) &&
                           ReadAt(_idx, 0, &ih, sizeof(ih)) && HeaderIs(dh, "OBADCDAT", 0) &&
                           HeaderIs(ih, "OBADCIDX", sizeof(IndexRecord));
    if (!headersOk) {
        if (_readOnly) {
            // The holder's files as they are; an empty or foreign cache is simply no entries.
            _dataEnd  = 0;
            _indexEnd = 0;
            return true;
        }
        if (datSize != 0 || idxSize != 0)
            std::fprintf(stderr, "[DerivedCache] unknown or damaged cache format: starting empty\n");
        ResetFiles();
        return true;
    }
    // A pair from two different rewrites. Finished when the new index is still there; otherwise only
    // the prefix whose payloads match their records is kept (the offsets alone prove nothing here).
    bool verifyPayloads = false;
    if (dh.reserved[0] != ih.reserved[0]) {
        if (!_readOnly && FinishInterruptedRewrite(dh.reserved[0])) {
            idxSize = Platform::FileSize64(_idx);
            if (idxSize < static_cast<int64_t>(kHeaderSize) || !ReadAt(_idx, 0, &ih, sizeof(ih)) ||
                !HeaderIs(ih, "OBADCIDX", sizeof(IndexRecord)) || ih.reserved[0] != dh.reserved[0])
                return false;
            std::fprintf(stderr, "[DerivedCache] finished a rewrite that was interrupted between its two files\n");
        } else {
            verifyPayloads = true;
        }
    }
    _generation = dh.reserved[0];
    const uint64_t count = (static_cast<uint64_t>(idxSize) - kHeaderSize) / sizeof(IndexRecord);
    // Read in bounded chunks, never sized by the file's length (review F-240): a valid header over a
    // huge or sparse tail made one allocation of the whole length (std::bad_alloc at every boot).
    // Every kept record backs at least one byte of derived.dat, so the kept list stays bounded.
    constexpr uint64_t kChunkRecords = 4096;
    std::vector<IndexRecord> chunk;
    std::vector<IndexRecord> records;
    uint64_t end = kHeaderSize;
    std::vector<uint8_t> payload;
    bool damaged = false;
    for (uint64_t first = 0; first < count && !damaged; first += kChunkRecords) {
        const uint64_t n = count - first < kChunkRecords ? count - first : kChunkRecords;
        chunk.resize(static_cast<size_t>(n));
        if (!ReadAt(_idx, kHeaderSize + first * sizeof(IndexRecord), chunk.data(), n * sizeof(IndexRecord)))
            return false;
        for (const IndexRecord& r : chunk) {
            // The size is tested against the room left, not offset + size against the file: the sum
            // wraps on a forged size near 2^64 (the record check is a plain XXH64), and every later
            // reader trusts r.size. r.offset == end <= datSize here.
            if (r.magic != kRecordMagic || r.check != RecordCheck(r) || r.offset != end || r.size == 0 ||
                r.size > static_cast<uint64_t>(datSize) - r.offset) {
                damaged = true;
                break;
            }
            if (verifyPayloads) {
                payload.resize(static_cast<size_t>(r.size));
                if (!ReadAt(_dat, r.offset, payload.data(), r.size) ||
                    Hash64(payload.data(), payload.size()) != r.payloadHash) {
                    damaged = true;
                    break;
                }
            }
            records.push_back(r);
            end += r.size;
        }
    }
    const uint64_t indexEnd = kHeaderSize + records.size() * sizeof(IndexRecord);
    if (_readOnly) {
        // A tail past the prefix is the holder's append in flight, or its torn tail: not ours to drop.
        _dataEnd  = end;
        _indexEnd = indexEnd;
        DropSuperseded(records);
        for (const IndexRecord& r : records)
            _records.emplace(r.sourceHash, r);
        _stats.entries   = _records.size();
        _stats.dataBytes = _dataEnd;
        return true;
    }
    if (verifyPayloads) {
        std::fprintf(stderr, "[DerivedCache] derived.dat and derived.idx are from different rewrites: kept the %llu "
                             "of %llu entries whose bytes match\n",
                     static_cast<unsigned long long>(records.size()), static_cast<unsigned long long>(count));
        ih.reserved[0] = _generation;
        if (!WriteAt(_idx, 0, &ih, sizeof(ih))) return false;
    }
    if (records.size() != count || static_cast<uint64_t>(datSize) != end ||
        static_cast<uint64_t>(idxSize) != indexEnd) {
        if (!verifyPayloads)
            std::fprintf(stderr, "[DerivedCache] dropped a damaged tail: kept %llu of %llu entries\n",
                         static_cast<unsigned long long>(records.size()), static_cast<unsigned long long>(count));
        Platform::FileTruncate(_dat, static_cast<int64_t>(end));
        Platform::FileTruncate(_idx, static_cast<int64_t>(indexEnd));
    }
    _dataEnd  = end;
    _indexEnd = indexEnd;
    // A key stored twice (its first payload failed its hash on a read, and the caller stored it
    // again): the later record is the live one. The earlier ones are dropped here; their bytes stay
    // in the files until the writer's rewrite takes them out.
    const size_t before = records.size();
    uint64_t live = 0;
    const size_t superseded = DropSuperseded(records);
    for (const IndexRecord& r : records)
        live += r.size;
    _garbageBytes = (end - kHeaderSize) - live;
    if (superseded != 0)
        std::fprintf(stderr, "[DerivedCache] %llu of %llu entries were stored again since: the old copies go at the "
                             "next rewrite\n",
                     static_cast<unsigned long long>(superseded), static_cast<unsigned long long>(before));
    for (const IndexRecord& r : records)
        _records.emplace(r.sourceHash, r);
    _stats.entries   = _records.size();
    _stats.dataBytes = _dataEnd;
    return true;
}

// The payload bytes the session before and this one refused at the cap (the larger).
uint64_t Store::RefusedBytes() const {
    return std::max(_lastRefusedBytes, _stats.refusedBytes);
}

// Full: past the cap (it was lowered), or a payload was refused at it (this session or the one
// before) and the cache is above the eviction target. Under _mutex.
bool Store::EvictWanted() const {
    const uint64_t refused = RefusedBytes();
    return _dataEnd > _cap || (refused != 0 && _dataEnd > EvictTarget(_cap, refused));
}

bool Store::NeedsCompact() const {
    return _garbageBytes != 0 || EvictWanted();
}

// The rewrite, on the writer thread (derived_cache.h, "THE REWRITE"): the live records in file
// order, the least recently used left out first when the cache is full (derived_cache.h, "THE
// CAP"), every payload checked against its record on the way. Finds go on meanwhile; this run's
// Puts wait in the queue, and the rewrite gives way (to run again after the next quiet spell) once
// half the queue's room is taken.
bool Store::Compact() {
    std::vector<IndexRecord> records;
    std::vector<bool> evicted;
    size_t evictedCount = 0;
    bool evict = false;
    uint64_t generation = 0;
    {
        Sys::MutexLock lock(_mutex);
        if (!NeedsCompact()) return false;
        evict = EvictWanted();
        records.reserve(_records.size());
        for (const auto& kv : _records)
            records.push_back(kv.second);
        std::sort(records.begin(), records.end(),
                  [](const IndexRecord& x, const IndexRecord& y) { return x.offset < y.offset; });
        evicted.assign(records.size(), false);
        if (evict) {
            uint64_t total = 0;
            for (const IndexRecord& r : records)
                total += r.size;
            struct Age {
                uint32_t stamp;
                size_t   at;   // the record's place in the file
            };
            std::vector<Age> order;
            order.reserve(records.size());
            for (size_t i = 0; i < records.size(); ++i) {
                const auto it = _stamps.find(KeyId(RecordKey(records[i])));
                order.push_back(Age{ it == _stamps.end() ? 0u : it->second, i });
            }
            std::sort(order.begin(), order.end(), [](const Age& x, const Age& y) {
                return x.stamp != y.stamp ? x.stamp < y.stamp : x.at < y.at;
            });
            const uint64_t limit  = EvictTarget(_cap, RefusedBytes());
            const uint64_t target = limit > kHeaderSize ? limit - kHeaderSize : 0;   // payload bytes
            for (const Age& age : order) {
                if (total <= target) break;
                evicted[age.at] = true;
                total -= records[age.at].size;
                ++evictedCount;
            }
        }
        generation = _generation + 1;
    }
    const uint64_t capBytes = _cap;
    const std::u16string datNew = FilePath(_dir, "derived.dat.new");
    const std::u16string idxNew = FilePath(_dir, "derived.idx.new");
    Platform::FileHandle nd = Platform::FileOpenExclusive(datNew.c_str());
    Platform::FileHandle ni = Platform::FileOpenExclusive(idxNew.c_str());
    const auto dropNew = [&] {
        CloseHandle(nd);
        CloseHandle(ni);
        Platform::FileDelete(datNew.c_str());
        Platform::FileDelete(idxNew.c_str());
    };
    if (!Valid(nd) || !Valid(ni)) {
        dropNew();
        return false;
    }
    Platform::FileTruncate(nd, 0);
    Platform::FileTruncate(ni, 0);
    const FileHeader dh = MakeHeader("OBADCDAT", 0, generation);
    const FileHeader ih = MakeHeader("OBADCIDX", sizeof(IndexRecord), generation);
    bool ok = WriteAt(nd, 0, &dh, sizeof(dh)) && WriteAt(ni, 0, &ih, sizeof(ih));
    std::vector<IndexRecord> kept;
    uint64_t end = kHeaderSize;
    size_t bad = 0;
    std::vector<uint8_t> payload;
    for (size_t i = 0; i < records.size() && ok; ++i) {
        const IndexRecord& r = records[i];
        if (evicted[i]) continue;
        if (end + r.size > capBytes) continue;
        if (_disabled.load(std::memory_order_acquire) || _closing.load(std::memory_order_acquire)) {
            ok = false;
            break;
        }
        {
            Sys::MutexLock lock(_mutex);
            if (_queuedBytes > kMaxQueued / 2) {
                std::fprintf(stderr, "[DerivedCache] the rewrite gives way to new entries: it runs again later\n");
                ok = false;
                break;
            }
        }
        payload.resize(static_cast<size_t>(r.size));
        bool read;
        {
            Sys::MutexLock files(_fileMutex);
            read = ReadAt(_dat, r.offset, payload.data(), r.size);
        }
        if (!read || Hash64(payload.data(), payload.size()) != r.payloadHash) {
            ++bad;
            continue;
        }
        IndexRecord n = r;
        n.offset = end;
        n.check  = RecordCheck(n);
        ok = WriteAt(nd, end, payload.data(), r.size) &&
             WriteAt(ni, kHeaderSize + kept.size() * sizeof(IndexRecord), &n, sizeof(n));
        kept.push_back(n);
        end += r.size;
    }
    // On disk before either replace: a crash after the first one finds the second file whole.
    ok = ok && Platform::FileFlush(nd) != 0 && Platform::FileFlush(ni) != 0;
    if (!ok) {
        dropNew();
        return false;
    }
    CloseHandle(nd);
    CloseHandle(ni);
    const std::u16string dat = FilePath(_dir, "derived.dat");
    const std::u16string idx = FilePath(_dir, "derived.idx");
    // The swap: no Find reads meanwhile (it holds _fileMutex across its lookup and copy).
    Sys::MutexLock files(_fileMutex);
    Platform::FileMapClose(&_map);
    CloseHandle(_dat);
    CloseHandle(_idx);
    const bool datReplaced = Platform::FileReplace(datNew.c_str(), dat.c_str()) != 0;
    if (datReplaced && g_crashBetweenReplaces.load()) {
        // The unit test's crash: the files stay as a dead process would leave them.
        _disabled.store(true, std::memory_order_release);
        return false;
    }
    const bool idxReplaced = datReplaced && Platform::FileReplace(idxNew.c_str(), idx.c_str()) != 0;
    _dat = Platform::FileOpenWriteShared(dat.c_str(), true);
    _idx = Platform::FileOpenWriteShared(idx.c_str(), false);
    Sys::MutexLock lock(_mutex);
    if (!Valid(_dat) || !Valid(_idx) || (datReplaced && !idxReplaced)) {
        // A new data file beside the old index (or no files): nothing more this run. The next Open
        // finishes the rewrite from derived.idx.new, or keeps the prefix whose bytes match.
        _disabled.store(true, std::memory_order_release);
        std::fprintf(stderr,
                     "[DerivedCache] the rewrite could not replace derived.idx: the cache is off for this run\n");
        return false;
    }
    if (!datReplaced) {
        // Nothing was replaced (Windows: another instance reading the files; MoveFileExW does not
        // rename over a file another handle holds, FILE_SHARE_DELETE or not): kept as it was.
        std::fprintf(stderr, "[DerivedCache] the rewrite could not replace derived.dat (another instance reading "
                             "it?): the cache is kept as it was\n");
        Platform::FileDelete(datNew.c_str());
        Platform::FileDelete(idxNew.c_str());
        return false;
    }
    const size_t before = records.size();
    _records.clear();
    for (const IndexRecord& r : kept)
        _records.emplace(r.sourceHash, r);
    _generation   = generation;
    _dataEnd      = end;
    _indexEnd     = kHeaderSize + kept.size() * sizeof(IndexRecord);
    _garbageBytes = 0;
    ++_layout;
    if (evict)
        std::fprintf(stderr,
                     "[DerivedCache] at the cap (%llu MB): evicted the %llu least recently used of %llu entries, "
                     "%llu entries (%.1f MB) kept\n",
                     static_cast<unsigned long long>(capBytes >> 20), static_cast<unsigned long long>(evictedCount),
                     static_cast<unsigned long long>(before), static_cast<unsigned long long>(kept.size()),
                     static_cast<double>(end) / 1048576.0);
    else
        std::fprintf(stderr, "[DerivedCache] rewritten without the old copies: %llu entries, %.1f MB\n",
                     static_cast<unsigned long long>(kept.size()), static_cast<double>(end) / 1048576.0);
    if (bad != 0)
        std::fprintf(stderr, "[DerivedCache] the rewrite dropped %llu entries whose bytes did not match\n",
                     static_cast<unsigned long long>(bad));
    return true;
}

void Store::Close() {
    if (_writer.joinable()) {
        _closing.store(true, std::memory_order_release);   // a rewrite in progress gives up
        {
            Sys::MutexLock lock(_mutex);
            _stop = true;
        }
        _cv.notify_all();
        _writer.join();
    }
    {
        // Uncontended (the writer is joined and Close is the store's last call); taken so the
        // handles keep their one lock.
        Sys::MutexLock files(_fileMutex);
        Platform::FileMapClose(&_map);
        CloseHandle(_dat);
        CloseHandle(_idx);
    }
    CloseHandle(_lock);
    Sys::MutexLock lock(_mutex);
    _records.clear();
    _stamps.clear();
    _session          = 0;
    _lastRefusedBytes = 0;
    _garbageBytes     = 0;
    _generation       = 0;
    _queue.clear();
    _queuedBytes = 0;
    _usedDirty   = false;
    _flushRequested = false;
    _loggedFull  = false;
    _open        = false;
    _readOnly    = false;
    _stats       = Stats();
}

// The newest record of `key` (the highest offset: the files are append-only). Open leaves one per
// key; preferring the newest keeps a re-stored entry ahead of its bad copy even if both were held.
const IndexRecord* Store::Lookup(const Key& key) const {
    const IndexRecord* best = nullptr;
    auto range = _records.equal_range(key.sourceHash);
    for (auto it = range.first; it != range.second; ++it) {
        if (!SameKey(RecordKey(it->second), key)) continue;
        if (best == nullptr || it->second.offset > best->offset) best = &it->second;
    }
    return best;
}

bool Store::Find(const Key& key, std::vector<uint8_t>& out) {
    out.clear();
    if (!_open || _disabled.load(std::memory_order_acquire)) return false;
    IndexRecord rec;
    uint64_t layout = 0;
    bool ok = true;
    {
        // Held across the lookup and the copy: the writer's rewrite swaps the files and the records
        // under it, so an offset found here is read from the file it belongs to. It also keeps the
        // mapping until the copy is done.
        Sys::MutexLock files(_fileMutex);
        {
            Sys::MutexLock lock(_mutex);
            _lastActivity = std::chrono::steady_clock::now();
            const IndexRecord* r = Lookup(key);
            if (r == nullptr) {
                ++_stats.misses;
                return false;
            }
            rec    = *r;
            layout = _layout;
        }
        out.resize(static_cast<size_t>(rec.size));
        if (_map.data != nullptr && rec.offset + rec.size <= _map.size)
            std::memcpy(out.data(), _map.data + rec.offset, static_cast<size_t>(rec.size));
        else
            ok = ReadAt(_dat, rec.offset, out.data(), rec.size);
    }
    ok = ok && Hash64(out.data(), out.size()) == rec.payloadHash;
    Sys::MutexLock lock(_mutex);
    if (!ok) {
        // A payload the OS never wrote (a power loss after its record): forget it; the caller
        // derives it again and Put stores it anew. (A rewrite since the read has already dropped it.)
        if (_stats.badPayloads++ == 0)
            std::fprintf(stderr, "[DerivedCache] an entry's bytes do not match its record: re-deriving it\n");
        auto range = _records.equal_range(key.sourceHash);
        for (auto it = range.first; it != range.second && layout == _layout; ++it) {
            if (it->second.offset == rec.offset) {
                _records.erase(it);
                if (!_readOnly) _garbageBytes += rec.size;
                break;
            }
        }
        ++_stats.misses;
        out.clear();
        return false;
    }
    ++_stats.hits;
    Stamp(KeyId(key));
    return true;
}

void Store::Forget(const Key& key) {
    if (!_open || _disabled.load(std::memory_order_acquire)) return;
    Sys::MutexLock lock(_mutex);
    const IndexRecord* r = Lookup(key);
    if (r == nullptr) return;
    // The record goes as a payload whose bytes did not match it does (Find): its bytes become
    // garbage, the next rewrite drops them, and a later Put of the key is stored.
    const uint64_t offset = r->offset;
    const uint64_t size   = r->size;
    auto range = _records.equal_range(key.sourceHash);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second.offset == offset) {
            _records.erase(it);
            if (!_readOnly) _garbageBytes += size;
            break;
        }
    }
}

void Store::Put(const Key& key, std::vector<uint8_t> payload) {
    if (!_open || _readOnly || _disabled.load(std::memory_order_acquire) || payload.empty()) return;
    Sys::MutexLock lock(_mutex);
    if (_stats.writeFailed) return;
    _lastActivity = std::chrono::steady_clock::now();
    _cv.wait(lock.Native(),
             [&]() REQUIRES(_mutex) { return _queuedBytes == 0 || _queuedBytes + payload.size() <= kMaxQueued; });
    _queuedBytes += payload.size();
    _queue.push_back(Pending{ key, std::move(payload) });
    lock.Unlock();
    _cv.notify_all();
}

void Store::Flush() {
    if (!_open || _readOnly) return;
    Sys::MutexLock lock(_mutex);
    for (;;) {
        if (_stop || (_queue.empty() && !_writing && !_usedDirty && !_compactNow)) return;
        // Asked again on every wake. A Find on another thread stamps a key the moment after the
        // writer saw a clean store and dropped the request; without a new one the writer waits for
        // a quiet spell those Finds keep pushing back (the macOS CI hang, thread stacks in
        // docs/extensions/handins/CACHEFIX.md). Each key is stamped once a session, so this ends.
        _flushRequested = true;
        _cv.notify_all();
        _cv.wait(lock.Native());
    }
}

void Store::RequestFlush() {
    if (!_open || _readOnly) return;
    {
        Sys::MutexLock lock(_mutex);
        _flushRequested = true;
        _compactArmed   = true;
    }
    _cv.notify_all();
}

void Store::RequestCompact() {
    if (!_open || _readOnly) return;
    {
        Sys::MutexLock lock(_mutex);
        _compactNow = true;
    }
    _cv.notify_all();
}

void Store::ReleaseMapping() {
    Sys::MutexLock files(_fileMutex);
    Platform::FileMapClose(&_map);
}

void Store::Disable() {
    _disabled.store(true, std::memory_order_release);
    Sys::MutexLock lock(_mutex);
    for (const Pending& p : _queue)
        _queuedBytes -= p.payload.size();   // a payload the writer holds is subtracted by the writer
    _queue.clear();
    _usedDirty = false;
    _cv.notify_all();
}

Stats Store::GetStats() const {
    Sys::MutexLock lock(_mutex);
    Stats s = _stats;
    s.entries   = _records.size();
    s.dataBytes = _dataEnd;
    return s;
}

uint64_t Store::BytesOnDisk() const {
    Sys::MutexLock lock(_mutex);
    return _open ? _dataEnd + _indexEnd : 0;
}

// The usage stamps are rewritten on a Flush / RequestFlush (a load's end), after the quiet spell, at
// Close and at exit (WriteStampsNow), never per payload: a first launch stores thousands of entries,
// and a list rewritten after each would be quadratic. The rewrite (Compact) runs when the queue is
// empty: at once after RequestCompact, otherwise once per run after the quiet spell, once a load's
// end (RequestFlush) was seen.
void Store::WriterMain() {
    Sys::MutexLock lock(_mutex);
    for (;;) {
        if (!_queue.empty()) {
            Pending p = std::move(_queue.front());
            _queue.pop_front();
            _writing = true;
            lock.Unlock();
            WriteOne(p);
            lock.Lock();
            _queuedBytes -= p.payload.size();
            _writing = false;
            _cv.notify_all();
            continue;
        }
        const auto deadline = _lastActivity + QuietSpell();
        const bool quiet    = std::chrono::steady_clock::now() >= deadline;
        if (_usedDirty && (_flushRequested || _stop || quiet)) {
            _writing = true;
            const bool write = !_disabled.load(std::memory_order_acquire);
            lock.Unlock();
            if (write) WriteUsedList();
            lock.Lock();
            // WriteUsedList cleared _usedDirty when it took its snapshot; a stamp made while the file
            // was being written set it again and must stay set (clearing it here lost those stamps:
            // the flaky eviction test). Only a disabled store, which writes nothing, clears it here.
            // The flush request stays up until the store is clean (below): cleared here, a stamp
            // landing during this write left Flush waiting on _usedDirty while this thread waited
            // for a quiet spell that readers kept pushing back (the macOS CI hang).
            if (!write) _usedDirty = false;
            _writing = false;
            _cv.notify_all();
            continue;
        }
        if (_stop) {
            _cv.notify_all();
            return;
        }
        const bool disabled = _disabled.load(std::memory_order_acquire);
        const bool wanted   = !disabled && NeedsCompact();
        if (_compactNow && !wanted) {
            _compactNow = false;   // nothing to rewrite
            _cv.notify_all();
            continue;
        }
        const bool waiting = wanted && _compactArmed && !_compactDone;
        if (wanted && (_compactNow || (waiting && quiet))) {
            _writing = true;
            lock.Unlock();
            // Background mode (Windows: low I/O and memory priority) for the rewrite only: the
            // appends are on the loader's path once the queue fills, and must not be starved.
            Platform::ThreadBackgroundMode(true);
            // The evicted entries' stamps go with them (derived.used lists the live entries only).
            if (Compact() && !_disabled.load(std::memory_order_acquire)) WriteUsedList();
            Platform::ThreadBackgroundMode(false);
            lock.Lock();
            _writing     = false;
            _compactNow  = false;
            // Given way to new entries: the next quiet spell tries again. Otherwise once is enough.
            _compactDone = !NeedsCompact() || _queuedBytes <= kMaxQueued / 2;
            _cv.notify_all();
            continue;
        }
        // Nothing left to do: the queue is empty, the stamps are on disk (or the store is disabled)
        // and no rewrite is pending, so a Flush in progress is satisfied.
        _flushRequested = false;
        _cv.notify_all();
        if (waiting || (_usedDirty && !disabled))
            _cv.wait_until(lock.Native(), deadline);
        else
            _cv.wait(lock.Native());
    }
}

void Store::WriteOne(Pending& p) {
    if (_disabled.load(std::memory_order_acquire)) return;
    const uint64_t size = p.payload.size();
    uint64_t at = 0;
    uint64_t indexAt = 0;
    {
        Sys::MutexLock lock(_mutex);
        if (_stats.writeFailed || Lookup(p.key) != nullptr) return;
        if (_dataEnd + size > _cap) {
            ++_stats.refusedFull;
            _stats.refusedBytes += size;
            _usedDirty = true;   // derived.used carries the refusal: the next Open evicts
            if (!_loggedFull) {
                _loggedFull = true;
                std::fprintf(stderr, "[DerivedCache] the cache is at its cap (%llu MB): new entries are not stored "
                                     "this run\n",
                             static_cast<unsigned long long>(_cap >> 20));
            }
            return;
        }
        at      = _dataEnd;
        indexAt = _indexEnd;
    }
    const uint64_t payloadHash = Hash64(p.payload.data(), p.payload.size());
    const IndexRecord rec = MakeRecord(p.key, at, size, payloadHash);
    bool ok;
    {
        Sys::MutexLock files(_fileMutex);
        ok = WriteAt(_dat, at, p.payload.data(), size) && WriteAt(_idx, indexAt, &rec, sizeof(rec));
        if (!ok) {
            // Best effort: the next Open drops whatever tail is left anyway.
            Platform::FileTruncate(_dat, static_cast<int64_t>(at));
            Platform::FileTruncate(_idx, static_cast<int64_t>(indexAt));
        }
    }
    Sys::MutexLock lock(_mutex);
    if (!ok) {
        _stats.writeFailed = true;
        std::fprintf(stderr, "[DerivedCache] a write failed (is the disk full?): the cache stores nothing more "
                             "this run\n");
        return;
    }
    _dataEnd  = at + size;
    _indexEnd = indexAt + sizeof(rec);
    _records.emplace(rec.sourceHash, rec);
    ++_stats.stored;
    Stamp(KeyId(p.key));
}

void Store::Stamp(uint64_t id) {
    if (_readOnly) return;   // the holder's stamps are not ours to write
    uint32_t& stamp = _stamps[id];
    if (stamp == _session) return;
    stamp      = _session;
    _usedDirty = true;
    _cv.notify_all();   // the writer starts the quiet-spell wait for the stamps
}

void Store::WriteStampsNow() {
    if (!_open || _readOnly || _disabled.load(std::memory_order_acquire)) return;
    WriteUsedList();
}

// The stamps and the session counter of the sessions before (derived.used). A file that is missing
// or not in this format is no stamps: session 1, every entry as old as the oldest.
void Store::ReadUsedList() NO_THREAD_SAFETY_ANALYSIS {   // Open's
    _stamps.clear();
    _session     = 1;
    _lastRefusedBytes = 0;
    std::vector<uint8_t> used;
    if (!ReadWhole(FilePath(_dir, "derived.used"), used) || used.size() < kHeaderSize) return;
    FileHeader h = {};
    std::memcpy(&h, used.data(), sizeof(h));
    if (!HeaderIs(h, "OBADCUSE", kUsedRecord)) return;
    for (size_t at = kHeaderSize; at + kUsedRecord <= used.size(); at += kUsedRecord) {
        uint64_t id = 0;
        uint32_t stamp = 0;
        std::memcpy(&id, used.data() + at, 8);
        std::memcpy(&stamp, used.data() + at + 8, 4);
        _stamps[id] = stamp;
    }
    const uint64_t last = h.reserved[0];
    _session     = last < 0xFFFFFFFEull ? static_cast<uint32_t>(last) + 1 : 0xFFFFFFFFu;
    _lastRefusedBytes = h.reserved[1];
}

void Store::WriteUsedList() {
    Sys::MutexLock file(_usedFileMutex);   // the writer and WriteStampsNow share the .tmp
    std::vector<uint8_t> bytes;
    {
        Sys::MutexLock lock(_mutex);
        _usedDirty = false;
        // Every entry the index still holds that a session stamped, this one's and the earlier ones'
        // merged; an evicted entry's stamp goes with it.
        bytes.reserve(kHeaderSize + _records.size() * kUsedRecord);
        bytes.resize(kHeaderSize);
        FileHeader h = MakeHeader("OBADCUSE", kUsedRecord);
        h.reserved[0] = _session;
        h.reserved[1] = _stats.refusedBytes;
        std::memcpy(bytes.data(), &h, sizeof(h));
        for (const auto& kv : _records) {
            const uint64_t id = KeyId(RecordKey(kv.second));
            const auto it = _stamps.find(id);
            if (it == _stamps.end()) continue;
            uint8_t rec[kUsedRecord] = {};
            std::memcpy(rec, &id, 8);
            std::memcpy(rec + 8, &it->second, 4);
            bytes.insert(bytes.end(), rec, rec + kUsedRecord);
        }
    }
    WriteWhole(FilePath(_dir, "derived.used"), bytes.data(), bytes.size());
}

bool Store::ReadAt(Platform::FileHandle h, uint64_t offset, void* dst, uint64_t size) {
    if (Platform::FileSeek64(h, static_cast<int64_t>(offset)) == 0) return false;
    uint8_t* p = static_cast<uint8_t*>(dst);
    while (size != 0) {
        const uint32_t n = size < kChunk ? static_cast<uint32_t>(size) : kChunk;
        uint32_t got = 0;
        if (Platform::FileRead(h, p, n, &got) == 0 || got != n) return false;
        p += n;
        size -= n;
    }
    return true;
}

bool Store::WriteAt(Platform::FileHandle h, uint64_t offset, const void* src, uint64_t size) {
    if (Platform::FileSeek64(h, static_cast<int64_t>(offset)) == 0) return false;
    const uint8_t* p = static_cast<const uint8_t*>(src);
    while (size != 0) {
        const uint32_t n = size < kChunk ? static_cast<uint32_t>(size) : kChunk;
        uint32_t wrote = 0;
        if (Platform::FileWrite(h, p, n, &wrote) == 0 || wrote != n) return false;
        p += n;
        size -= n;
    }
    return true;
}

}  // namespace DerivedCache
}  // namespace Ext
