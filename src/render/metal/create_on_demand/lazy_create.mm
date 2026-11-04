// ============================================================================
// src/render/metal/create_on_demand/lazy_create.mm -- ext.create_on_demand, the Metal side
// (docs/extensions/handins/CREATEONDEMAND.md). The contract is in lazy_create.h. Compiled with ARC.
// ============================================================================
// NOT RETAIL.
//
// Two record kinds over the backend-neutral table (lazy_table.h), as on DX11. Each materializes while
// a Metal device is live (LiveDevice(); the table is paused while there is none), with the bytes
// re-decoded from the package into a block of its own, and runs the load's create through
// seeded_create.h: ext.compress_ao's conversion (PlanUploads), then ext.share_gpu_data's
// create-or-share (MakeTexture / MakeBuffer). Materialize runs inside an autorelease pool: the table's
// prefetch workers are plain threads with no pool of their own. Publish writes the companion's strong
// member (ARC retains it there) and its `shared` flag.
// ============================================================================

// First: it imports Metal with Carbon's global Collection typedef renamed (the engine has a namespace
// Collection); every later Metal import is then a no-op.
#include "VideoMetalData.h"

#include "create_on_demand/lazy_create.h"

#include "create_on_demand/create_on_demand_backend.h"
#include "create_on_demand/pending_levels.h"
#include "create_on_demand/stream_locate.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace VideoMetal {
namespace CreateOnDemand {

std::atomic<::CreateOnDemand::LazyTable*> g_table{nullptr};

namespace {

std::atomic<int64_t> g_notLocatable{0};   // static buffers read as the load reads them
std::atomic<int64_t> g_decodeFailed{0};
::CreateOnDemand::LazyTable::Stats g_lastPrinted;

::CreateOnDemand::Outcome Result(bool ok) {
    return ok ? ::CreateOnDemand::Outcome::Made : ::CreateOnDemand::Outcome::Failed;
}

// After a materialization (no table lock held): the run's summary, at most every 30 s.
void MaybePrint();

void NoteDecodeFailure(const ::CreateOnDemand::Locator& loc) {
    const int64_t n = g_decodeFailed.fetch_add(1, std::memory_order_relaxed);
    if (n < 8) {
        std::fprintf(stderr, "[CreateOnDemand] re-decode FAILED: entry 0x%llx size %u pos %d len %d\n",
                     static_cast<unsigned long long>(loc.entry), loc.size, loc.pos, loc.len);
    }
}

class BufferRecord final : public ::CreateOnDemand::Record {
public:
    BufferRecord(bool index, ::CreateOnDemand::Locator&& loc, id<MTLBuffer> __strong* out, bool* outShared)
        : _index(index), _loc(std::move(loc)), _out(out), _outShared(outShared) {}

    ::CreateOnDemand::Outcome Materialize() override {
        @autoreleasepool {
            if (LiveDevice() == nil) {
                return ::CreateOnDemand::Outcome::Retry;
            }
            std::unique_ptr<uint8_t[]> bytes(new uint8_t[static_cast<size_t>(std::max(1, _loc.len))]);
            bool ok = ::CreateOnDemand::Redecode(_loc, bytes.get());
            if (ok) {
                _made = MakeBuffer(_index, false, bytes.get(), static_cast<uint32_t>(_loc.len), &_shared);
                ok = _made != nil;
            } else {
                NoteDecodeFailure(_loc);
            }
            MaybePrint();
            return Result(ok);
        }
    }

    void Publish() override {
        *_outShared = _shared;   // before the handle: a use that sees it sees the flag
        CreateOnDemand::StorePublished(_out, _made);
    }

    // share_gpu_data's user is dropped before the strong member lets go (the destructor's order,
    // share_create.h); ARC releases the buffer with the nil store.
    void Evict() override {
        id<MTLBuffer> const buffer = *_out;
        CreateOnDemand::StorePublished(_out, nil);
        *_outShared = false;
        _made       = nil;
        _shared     = false;
        if (buffer == nil) return;
        ForgetBufferUses(buffer);
        ShareGpuData::DropBuffer(buffer);
    }

    uint64_t Bytes() const override {
        return static_cast<uint64_t>(_loc.len);
    }

private:
    bool                      _index;
    ::CreateOnDemand::Locator _loc;
    id<MTLBuffer> __strong*   _out;
    bool*                     _outShared;
    id<MTLBuffer>             _made   = nil;
    bool                      _shared = false;
};

class TextureRecord final : public ::CreateOnDemand::Record {
public:
    TextureRecord(const TextureShape& shape, MTLTextureDescriptor* desc, std::vector<::CreateOnDemand::Locator>&& levels,
                  id<MTLTexture> __strong* out, bool* outShared)
        : _shape(shape), _desc([desc copy]), _levels(std::move(levels)), _out(out), _outShared(outShared) {
        for (const ::CreateOnDemand::Locator& l : _levels) {
            _bytes += static_cast<uint64_t>(l.len);
        }
    }

    ::CreateOnDemand::Outcome Materialize() override {
        @autoreleasepool {
            if (LiveDevice() == nil) {
                return ::CreateOnDemand::Outcome::Retry;
            }
            const int count = static_cast<int>(_levels.size());
            std::unique_ptr<uint8_t[]> bytes(new uint8_t[_bytes > 0 ? _bytes : 1]);
            std::vector<SourceLevel> source(static_cast<size_t>(count));
            uint64_t at = 0;
            bool ok = true;
            for (int i = 0; i < count && ok; ++i) {
                const ::CreateOnDemand::Locator& l = _levels[static_cast<size_t>(i)];
                ok = ::CreateOnDemand::Redecode(l, bytes.get() + at);
                if (!ok) {
                    NoteDecodeFailure(l);
                }
                source[static_cast<size_t>(i)] = SourceLevel{ bytes.get() + at, static_cast<size_t>(l.len) };
                at += static_cast<uint64_t>(l.len);
            }
            if (ok) {
                // The load's chain: ext.compress_ao may change the pixel format, so on a copy.
                MTLTextureDescriptor* d = [_desc copy];
                UploadPlan plan;
                PlanUploads(_shape, d, source.data(), count, &plan);
                _made = MakeTexture(d, plan, true, &_shared);
                ok = _made != nil;
            }
            MaybePrint();
            return Result(ok);
        }
    }

    void Publish() override {
        *_outShared = _shared;   // before the handle: a use that sees it sees the flag
        CreateOnDemand::StorePublished(_out, _made);
    }

    // As the buffer's: the share table's user first, then the strong member lets the texture go. The
    // sampler stays (made at load, as retail makes it).
    void Evict() override {
        id<MTLTexture> const texture = *_out;
        CreateOnDemand::StorePublished(_out, nil);
        *_outShared = false;
        _made       = nil;
        _shared     = false;
        if (texture == nil) return;
        ShareGpuData::DropTexture(texture);
    }

    uint64_t Bytes() const override {
        return _bytes;
    }

private:
    TextureShape                           _shape;
    MTLTextureDescriptor*                  _desc;
    std::vector<::CreateOnDemand::Locator> _levels;
    id<MTLTexture> __strong*               _out;
    bool*                                  _outShared;
    id<MTLTexture>                         _made   = nil;
    bool                                   _shared = false;
    uint64_t                               _bytes  = 0;
};

// Two prefetch workers at most, one below eight hardware threads (DX11's rule).
int WorkerCount() {
    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    return std::max(1, std::min(2, hw / 4));
}

void Print(const char* when) {
    ::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed);
    if (t == nullptr) {
        return;
    }
    static std::mutex printMutex;   // the loader's LoadEnd, the workers and the using threads
    std::lock_guard<std::mutex> lock(printMutex);
    const ::CreateOnDemand::LazyTable::Stats s = t->GetStats();
    if (s.recorded == g_lastPrinted.recorded && s.onUse == g_lastPrinted.onUse &&
        s.prefetched == g_lastPrinted.prefetched && s.forgotten == g_lastPrinted.forgotten &&
        s.evicted == g_lastPrinted.evicted) {
        return;
    }
    g_lastPrinted = s;
    const ::CreateOnDemand::PackageMaps::Stats m = ::CreateOnDemand::Maps().GetStats();
    const double mb = 1.0 / (1024.0 * 1024.0);
    const int64_t made = s.onUse + s.prefetched;
    std::fprintf(stderr,
                 "[CreateOnDemand] %s: deferred %lld (%.1f MB), pending %lld (%.1f MB); made %lld (%.1f MB: "
                 "%lld on use, %lld prefetched; %.0f ms, on use %.0f ms, worst %.2f ms), waited %lld, "
                 "failed %lld, dropped unmade %lld; read at load %lld; packages mapped %lld (%lld detached)\n",
                 when, static_cast<long long>(s.recorded), s.recordedBytes * mb, static_cast<long long>(s.pending),
                 s.pendingBytes * mb, static_cast<long long>(made), s.materialBytes * mb,
                 static_cast<long long>(s.onUse), static_cast<long long>(s.prefetched), s.materialNs / 1e6,
                 s.onUseNs / 1e6, s.worstOnUseNs / 1e6, static_cast<long long>(s.waited),
                 static_cast<long long>(s.failed), static_cast<long long>(s.forgotten),
                 static_cast<long long>(g_notLocatable.load(std::memory_order_relaxed)),
                 static_cast<long long>(m.live), static_cast<long long>(m.detached));
    if (s.evicted != 0 || s.sweeps != 0) {
        std::fprintf(stderr,
                     "[CreateOnDemand] %s: given back %lld (%.1f MB) over %lld sweeps (uses held %.1f ms), "
                     "re-made %lld, kept %lld; made and held %.1f MB\n",
                     when, static_cast<long long>(s.evicted), s.evictedBytes * mb, static_cast<long long>(s.sweeps),
                     s.evictNs / 1e6, static_cast<long long>(s.remade), static_cast<long long>(s.kept),
                     (static_cast<double>(s.recordedBytes) - static_cast<double>(s.pendingBytes)) * mb);
    }
}

std::atomic<int64_t> g_lastPrintNs{0};

void MaybePrint() {
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    int64_t last = g_lastPrintNs.load(std::memory_order_relaxed);
    if (now - last < 30000000000LL) {
        return;
    }
    if (!g_lastPrintNs.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
        return;
    }
    Print("in game (run total)");
}

}  // namespace

bool DeferBuffer(const void* obj, IO::Stream* stream, bool index, uint32_t size, id<MTLBuffer> __strong* out,
                 bool* outShared) {
    ::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed);
    if (t == nullptr) {
        return false;
    }
    ::CreateOnDemand::Locator loc;
    if (!::CreateOnDemand::Locate(stream, static_cast<int32_t>(size), &loc)) {
        g_notLocatable.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    ::CreateOnDemand::Skip(stream, static_cast<int32_t>(size));
    t->Forget(obj);
    t->Add(obj, std::unique_ptr<::CreateOnDemand::Record>(new BufferRecord(index, std::move(loc), out, outShared)));
    return true;
}

bool HasPendingLevels(const void* owner) {
    const ::CreateOnDemand::PendingLevels& p = ::CreateOnDemand::ThreadPendingLevels();
    return g_table.load(std::memory_order_relaxed) != nullptr && p.image == owner && !p.levels.empty();
}

void DeferTexture(const void* obj, const TextureShape& shape, MTLTextureDescriptor* desc,
                  id<MTLTexture> __strong* outTexture, bool* outShared) {
    ::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed);
    ::CreateOnDemand::PendingLevels& p = ::CreateOnDemand::ThreadPendingLevels();
    std::vector<::CreateOnDemand::Locator> levels = std::move(p.levels);
    p.Clear();
    t->Forget(obj);
    t->Add(obj, std::unique_ptr<::CreateOnDemand::Record>(
                    new TextureRecord(shape, desc, std::move(levels), outTexture, outShared)));
}

void Pause() {
    if (::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed)) {
        t->Pause();
        Print("device going away");
    }
}

void Resume() {
    if (::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed)) {
        t->Resume();
    }
}

void LoadEnd() {
    Print("load end (run total)");
}

void AfterSweep(int64_t evicted) {
    (void)evicted;
    Print("given back (run total)");
}

}  // namespace CreateOnDemand
}  // namespace VideoMetal

// ---- the backend-neutral interface (create_on_demand_backend.h) -----------------------------------
namespace CreateOnDemandBackend {

void Request(bool on, int idleMinutes) {
    if (on && VideoMetal::CreateOnDemand::g_table.load(std::memory_order_relaxed) == nullptr) {
        // Never destroyed: records and workers live to the end of the process.
        ::CreateOnDemand::LazyTable* const t =
            new ::CreateOnDemand::LazyTable(VideoMetal::CreateOnDemand::WorkerCount());
        // One epoch is a minute: an object unused for `idleMinutes` epochs is given back.
        t->SetSweepHook(&VideoMetal::CreateOnDemand::AfterSweep);
        t->SetIdleLimit(idleMinutes > 0 ? static_cast<uint32_t>(idleMinutes) : 0, std::chrono::minutes(1));
        VideoMetal::CreateOnDemand::g_table.store(t, std::memory_order_release);
    }
    if (on) {
        std::fprintf(stderr,
                     "[CreateOnDemand] on: static buffers and textures are created at their first use; "
                     "given back after %d idle minute(s) (0 = never)\n",
                     idleMinutes);
    } else {
        std::fprintf(stderr, "[CreateOnDemand] off\n");
    }
}

// Every Metal device can: the deferred create is the load's create, made later.
bool Supported() {
    return true;
}

void Prefetch(const void* companion) {
    if (::CreateOnDemand::LazyTable* const t = VideoMetal::CreateOnDemand::g_table.load(std::memory_order_relaxed)) {
        t->Prefetch(companion);
    }
}

void Keep(const void* companion) {
    if (::CreateOnDemand::LazyTable* const t = VideoMetal::CreateOnDemand::g_table.load(std::memory_order_relaxed)) {
        t->Keep(companion);
    }
}

bool VertexBufferMade(const void* companion) {
    return companion == nullptr ||
           VideoMetal::CreateOnDemand::Made(&static_cast<const VideoMetal::VertexBufferData*>(companion)->buffer);
}

// A Metal texture has no separate view: the texture is the made object.
bool ImageMade(const void* companion) {
    return companion == nullptr ||
           VideoMetal::CreateOnDemand::Made(&static_cast<const VideoMetal::ImageBufferData*>(companion)->texture);
}

}  // namespace CreateOnDemandBackend
