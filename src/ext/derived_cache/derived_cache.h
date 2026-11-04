#pragma once
// ============================================================================
// src/ext/derived_cache/derived_cache.h -- the derived-data cache (shared infrastructure)
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/COMPRESSAO.md). Data an extension DERIVES from the
// game's content at load (today: ext.compress_ao's BC4 encode of the baked AO maps), kept on the
// user's machine so the next start finds it instead of recomputing it -- the shape of UE5's derived
// data cache. Ruled allowed by the user 2026-10-22: it lives only in the user's documents folder,
// is built from the user's own install, and is never shipped (DESIGN.md section 3 rule 7).
// Standard C++ over the Platform file seam (platform_file.h): the unit tests drive it over a temp
// directory (tests/unit/ext_derived_cache_test.cpp). The exe's one shared store and its folder are
// derived_cache_shared.cpp.
//
// CONTENT-ADDRESSED. A Key is what the payload was derived FROM and HOW: the producer (a FourCC),
// the producer's encoder version, four shape words (for a texture: width, height, levels, source
// format), the source's byte count and Hash64 (hash64.h, XXH64) of the source bytes. Nothing names a
// package or a path: a changed mod is a different hash and simply misses; a new encoder version
// misses every old entry (they age out through the cap, below). Every field is compared on a
// lookup, so a hash collision would also need the same producer, version, shape and size.
//
// THE FILES, in <dir> (the exe: <Documents>\Banished\Cache\, beside settings.cfg):
//   derived.lock   held exclusively while a store is open (Platform::FileOpenExclusive): a second
//                  running instance gets OpenStatus::ReadOnly (below)
//   derived.dat    32-byte header, then the payloads back to back, append-only
//   derived.idx    32-byte header, then one 80-byte IndexRecord per payload, in payload order
//   derived.used   per entry, the session that last read or wrote it (the cap's eviction order),
//                  and the session counter; rewritten at every Flush / RequestFlush (the exe: each
//                  load's end) and at Close
//   derived.clear  the Options dialog's "Clear cache": the next Open deletes the cache first
// Little-endian on disk (x86-64 and arm64 both are). The header's format word changing resets both
// files.
//
// CRASH SAFETY. A record is written after its payload, and carries Hash64 of its own 72 bytes and of
// the payload. Open walks the index and keeps the longest valid prefix (magic, own hash, contiguous
// offset, inside the data file), then truncates both files to it: a torn tail from a crash or a kill
// is dropped. A payload whose bytes do not match its record's hash on a read (the OS wrote the
// record but not the payload before a power loss) is a miss, and its key is stored again: a second
// record for the key. Open keeps only a key's last record, and the next rewrite (below) takes the
// earlier ones out of the files, so the bad payload is read and rejected once, not at every start.
//
// THE REWRITE (Compact) makes derived.dat.new / derived.idx.new with the live records in file order
// (the least recently used left out when the cache is full, every payload checked against its record
// on the way), flushes both, then replaces derived.dat and then derived.idx. It runs on the writer
// thread, never in Open: once the writer's queue is empty, either at once (RequestCompact: the exe's
// boot end) or once per run after the cache has gone unused for 10 s since a load's end (RequestFlush).
// Finds go on meanwhile and are paused only for the swap. Both headers carry a generation, and a
// rewrite writes the next one into both new files. A crash between the two replaces leaves the new
// derived.dat beside the old derived.idx: the next Open sees the generations differ and finishes the
// rewrite from derived.idx.new (whole: it was flushed before the first replace). Without it (a pair
// put together by hand) Open keeps the index prefix whose payloads match their hashes. Leftover .new
// files of a rewrite that never reached its replaces are deleted at Open.
//
// THE CAP. Open with `capBytes`: a payload that would take the data file past it is not stored (one
// log line). Every holder Open is a new session (a counter in derived.used); a Find hit or a store
// stamps the entry with it, and the stamps of earlier sessions are kept (merged, not replaced). A
// cache past its cap, or one that refused a payload (this session or the one before), is rewritten
// (THE REWRITE) without its least recently used entries (the oldest stamp first, then the oldest in
// the file; an entry with no stamp counts as the oldest) until it is at 90% of the cap (lower by
// what was refused, so that fits; never below half the cap), the rest in their order. A missing
// derived.used therefore evicts the oldest-written entries, not all of them.
//
// A SECOND INSTANCE. The lock holder opens derived.dat / derived.idx so that others may read them
// (Platform::FileOpenWriteShared). The instance that loses the lock opens them read-only
// (OpenStatus::ReadOnly): it loads the index's valid prefix as it is at that moment, serves Find
// through the file handle (no mapping: the holder may truncate a torn tail under it), and never
// writes, truncates or rewrites anything and has no writer thread; Put / Flush are no-ops. A live
// append by the holder is safe to read: a record is written after its payload, every record is
// self-hashed and every payload is hashed, so a half-written tail is simply not in the prefix.
//
// THREADS. Find is called from any thread (the loader's texture creates); its copy out of the
// mapping (or read through the handle) holds the file lock, so ReleaseMapping is safe at any time.
// Put queues the payload and returns, and one writer thread appends it (at most 256 MB queued, then
// Put waits). A write that fails (the disk is full) turns storing off for the run with one log line;
// reads go on.
// ============================================================================
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "platform_file.h"
#include "system/annotated_mutex.h"   // Sys::Mutex: std::mutex the thread-safety analysis can check

namespace Ext {
namespace DerivedCache {

struct Key {
    uint32_t producer   = 0;    // FourCC of the producer (MakeProducer('A','O','B','4'))
    uint32_t version    = 0;    // the producer's encoder version
    uint32_t shape[4]   = {};   // the producer's parameters
    uint64_t sourceSize = 0;    // bytes hashed
    uint64_t sourceHash = 0;    // Hash64 of those bytes
};

bool SameKey(const Key& a, const Key& b);

constexpr uint32_t MakeProducer(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

// One record of derived.idx (80 bytes).
struct IndexRecord {
    uint32_t magic;         // +0x00 kRecordMagic
    uint32_t producer;      // +0x04
    uint32_t version;       // +0x08
    uint32_t reserved;      // +0x0C 0
    uint32_t shape[4];      // +0x10
    uint64_t sourceSize;    // +0x20
    uint64_t sourceHash;    // +0x28
    uint64_t offset;        // +0x30 the payload's offset in derived.dat
    uint64_t size;          // +0x38 the payload's bytes
    uint64_t payloadHash;   // +0x40 Hash64 of the payload
    uint64_t check;         // +0x48 Hash64 of +0x00..+0x47
};
static_assert(sizeof(IndexRecord) == 80, "derived.idx records are 80 bytes");

// The three files' header. A change resets the cache. 2: the generation in the dat / idx headers,
// derived.used's stamps.
constexpr uint32_t kFormatVersion = 2;

struct Stats {
    uint64_t entries     = 0;   // records in the index (this run's included)
    uint64_t dataBytes   = 0;   // derived.dat's size
    uint64_t hits        = 0;
    uint64_t misses      = 0;
    uint64_t stored      = 0;   // payloads this run appended
    uint64_t refusedFull = 0;   // payloads not stored: the cap
    uint64_t refusedBytes = 0;  // their bytes
    uint64_t badPayloads = 0;   // reads whose bytes did not match their record
    bool     writeFailed = false;
};

class Store {
public:
    // ReadOnly: another instance holds the lock; this store reads what it had stored (above).
    // InUse: it holds the lock and its files cannot be read.
    enum class OpenStatus { Opened, ReadOnly, InUse, Failed };

    Store() = default;
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // `dir` is UTF-8, created when absent, its missing parents too. On InUse / Failed nothing is held
    // and every call below is a no-op miss.
    OpenStatus Open(const std::string& dir, uint64_t capBytes);
    // Writes what is queued, stops the writer, closes the files and releases the lock.
    void Close();
    bool IsOpen() const { return _open; }
    bool IsReadOnly() const { return _readOnly; }

    // The payload stored under `key`, verified against its record. false = a miss (`out` cleared).
    bool Find(const Key& key, std::vector<uint8_t>& out);
    // Drops the record stored under `key`: a payload whose bytes match their record but which the
    // consumer refuses (ext.package_cache's Decode), so the consumer's next Put replaces it.
    void Forget(const Key& key);
    // Queues `payload` under `key` (taken by move). Dropped when disabled, after a failed write, or
    // when the key is already stored.
    void Put(const Key& key, std::vector<uint8_t> payload);
    // Waits until every queued payload is written and the keep list is current.
    void Flush();
    // The same without waiting: the writer rewrites the keep list once its queue is empty. It also
    // marks a load's end: a rewrite the files need runs once the cache has been idle a while.
    void RequestFlush();
    // Runs the rewrite the files need (THE REWRITE) as soon as the writer's queue is empty, without
    // waiting for the quiet spell; a later Flush waits for it. The exe: the boot's last load.
    void RequestCompact();
    // Writes the usage stamps (derived.used) now, on the calling thread. The exe: at exit (the
    // shared store is never closed). Also written by the writer after a Flush / RequestFlush and
    // after the quiet spell, so a consumer that never sends either (ext.package_cache) persists them.
    void WriteStampsNow();
    // Unmaps derived.dat: later reads go through the file handle. The mapping's pages count in the
    // process's working set while it is held (file-backed, ~1 GB after a MegaMod boot), so the exe
    // drops it once the load that read the cache is over.
    void ReleaseMapping();
    // Stops every read and write for the rest of the run (the clear request: the files go at the
    // next Open).
    void Disable();

    Stats GetStats() const;
    uint64_t BytesOnDisk() const;   // derived.dat + derived.idx

private:
    struct Pending {
        Key                  key;
        std::vector<uint8_t> payload;
    };

    // THE LOCKS, as clang's thread-safety analysis checks them (system/thread_annotations.h): the
    // fields below say which lock is theirs. Open and its helpers (OpenReadOnly, LoadIndex,
    // FinishInterruptedRewrite, ResetFiles, ReadUsedList) run before the writer thread starts and
    // before the store answers any call (_open is false until Open's end), so no other thread can
    // reach the fields they set: they are not analysed (NO_THREAD_SAFETY_ANALYSIS in the .cpp).
    OpenStatus OpenReadOnly(const std::string& dir);
    bool LoadIndex();
    bool FinishInterruptedRewrite(uint64_t datGeneration);
    // the writer thread; true when it rewrote the files
    bool Compact() EXCLUDES(_mutex, _fileMutex);
    bool NeedsCompact() const REQUIRES(_mutex);
    bool EvictWanted() const REQUIRES(_mutex);
    uint64_t RefusedBytes() const REQUIRES(_mutex);
    void ReadUsedList();
    void Stamp(uint64_t id) REQUIRES(_mutex);
    void ResetFiles();
    void WriterMain() EXCLUDES(_mutex);
    void WriteOne(Pending& p) EXCLUDES(_mutex, _fileMutex);
    void WriteUsedList() EXCLUDES(_mutex, _usedFileMutex);
    bool ReadAt(Platform::FileHandle h, uint64_t offset, void* dst, uint64_t size);
    bool WriteAt(Platform::FileHandle h, uint64_t offset, const void* src, uint64_t size);
    const IndexRecord* Lookup(const Key& key) const REQUIRES(_mutex);

    std::string _dir;
    uint64_t    _cap  = 0;
    bool        _open = false;
    bool        _readOnly = false;   // the lock is another instance's (OpenStatus::ReadOnly)

    Platform::FileHandle  _lock = Platform::kInvalidFileHandle;
    Platform::FileHandle  _dat GUARDED_BY(_fileMutex) = Platform::kInvalidFileHandle;
    Platform::FileHandle  _idx GUARDED_BY(_fileMutex) = Platform::kInvalidFileHandle;
    // derived.dat as it was at Open (until ReleaseMapping / Close)
    Platform::FileMapping _map GUARDED_BY(_fileMutex);
    Sys::Mutex _usedFileMutex ACQUIRED_BEFORE(_mutex);   // derived.used's writes (the writer, WriteStampsNow)
    Sys::Mutex _fileMutex ACQUIRED_BEFORE(_mutex);       // the two handles, their file positions, and the
                                                         // mapping; taken before _mutex when both are held

    mutable Sys::Mutex                             _mutex;   // everything below
    std::condition_variable                        _cv;
    std::unordered_multimap<uint64_t, IndexRecord> _records GUARDED_BY(_mutex);   // by sourceHash
    std::unordered_map<uint64_t, uint32_t>         _stamps GUARDED_BY(_mutex);    // KeyId() -> the session that last used it
    uint32_t _session GUARDED_BY(_mutex) = 0;                // this Open's session number
    uint64_t _lastRefusedBytes GUARDED_BY(_mutex) = 0;       // the bytes the previous session refused
    std::deque<Pending> _queue GUARDED_BY(_mutex);
    uint64_t _queuedBytes GUARDED_BY(_mutex) = 0;
    uint64_t _dataEnd GUARDED_BY(_mutex) = 0;
    uint64_t _indexEnd GUARDED_BY(_mutex) = 0;
    uint64_t _generation GUARDED_BY(_mutex) = 0;     // the files' (both headers)
    uint64_t _garbageBytes GUARDED_BY(_mutex) = 0;   // payload bytes in derived.dat no record serves (superseded, bad)
    uint64_t _layout GUARDED_BY(_mutex) = 0;         // bumped when a rewrite moves the records (Find's erase checks it)
    std::chrono::steady_clock::time_point _lastActivity GUARDED_BY(_mutex);   // the last Find / Put
    bool     _usedDirty GUARDED_BY(_mutex) = false;
    bool     _writing GUARDED_BY(_mutex) = false;          // the writer is writing a payload or the keep list
    bool     _flushRequested GUARDED_BY(_mutex) = false;   // a Flush waits: the writer rewrites the keep list
    bool     _stop GUARDED_BY(_mutex) = false;
    bool     _loggedFull GUARDED_BY(_mutex) = false;
    bool     _compactArmed GUARDED_BY(_mutex) = false;   // a load's end was seen (RequestFlush)
    bool     _compactNow GUARDED_BY(_mutex) = false;     // RequestCompact
    bool     _compactDone GUARDED_BY(_mutex) = false;    // the quiet-spell rewrite ran this run
    std::atomic<bool> _disabled{false};
    std::atomic<bool> _closing{false};   // Close: a rewrite in progress gives up
    Stats    _stats GUARDED_BY(_mutex);
    std::thread _writer;
};

// A key's identity in derived.used.
uint64_t KeyId(const Key& key);

// The unit test's crash injection: a rewrite stops right after replacing derived.dat, its store
// disabled and its files left as a killed process would leave them.
void SetCrashBetweenReplacesForTest(bool on);
// The unit test's quiet spell (default 10000 ms): the idle time after which the stamps are written
// and the rewrite runs.
void SetQuietSpellForTest(int ms);

// The Options dialog's "Clear cache": writes `dir`\derived.clear (the next Open deletes the cache).
bool RequestClear(const std::string& dir);
bool ClearRequested(const std::string& dir);
// derived.dat + derived.idx in `dir`, from the files themselves (0 when absent or held open by a
// store: ask that store's BytesOnDisk instead).
uint64_t BytesInDir(const std::string& dir);

// ---- the exe's shared store (derived_cache_shared.cpp; not in the unit build) -------------------
// <Documents>\Banished\Cache: the folder settings.cfg is in (IO::SettingsStore::DefaultPath), plus
// "Cache". false when the documents folder is unknown.
bool CacheDir(std::string& out);
// Opens the shared store once (later calls return it, or null): null under the faithful profile,
// when the folder is unknown, or when the cache cannot be opened; read-only when another instance
// holds it. The cap is Val().derived_cache_mb.
Store* OpenShared();
Store* Shared();   // the store OpenShared opened, or null
// The button: the clear request for the shared folder, and the shared store disabled at once.
void ClearShared();
bool SharedClearPending();
uint64_t SharedBytes();   // the cache's size on disk (0 once a clear is pending)

}  // namespace DerivedCache
}  // namespace Ext
