// ============================================================================
// src/render/vulkan/create_on_demand/lazy_create.cpp -- ext.create_on_demand, the Vulkan side
// (docs/extensions/handins/CREATEONDEMAND.md). The contract is in lazy_create.h.
// ============================================================================
// NOT RETAIL (P4). Two record kinds over the backend-neutral table (lazy_table.h), as on DX11 and
// Metal. Each materializes while a Vulkan device is live, with the bytes re-decoded from the package
// into a block of its own, and runs the load's create through seeded_create.h. Publish writes the
// companion's fields under the table's mutex; Evict (the table's mutex, the use gate closed) releases
// the object through seeded_create.h (a shared one drops its use) and empties the fields.
// ============================================================================
#include "create_on_demand/lazy_create.h"

#include "VideoVulkanData.h"
#include "vulkan_entry.h"

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

namespace VideoVulkan {
namespace CreateOnDemand {

std::atomic<::CreateOnDemand::LazyTable*> g_table{nullptr};

namespace {

std::atomic<int64_t> g_notLocatable{0};   // static buffers read as the load reads them
std::atomic<int64_t> g_decodeFailed{0};
::CreateOnDemand::LazyTable::Stats g_lastPrinted;

::CreateOnDemand::Outcome Result(bool ok) {
    return ok ? ::CreateOnDemand::Outcome::Made : ::CreateOnDemand::Outcome::Failed;
}

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
    BufferRecord(bool index, VkBufferUsageFlags usage, ::CreateOnDemand::Locator&& loc, BufferStore* store)
        : _index(index), _usage(usage), _loc(std::move(loc)), _store(store) {}

    ::CreateOnDemand::Outcome Materialize() override {
        if (!DeviceLive()) return ::CreateOnDemand::Outcome::Retry;
        std::unique_ptr<uint8_t[]> bytes(new uint8_t[static_cast<size_t>(std::max(1, _loc.len))]);
        bool ok = ::CreateOnDemand::Redecode(_loc, bytes.get());
        if (ok) {
            std::string why;
            ok = MakeStaticBuffer(_index, bytes.get(), static_cast<uint32_t>(_loc.len), _usage, &_made, &_shared,
                                  &why);
            if (!ok) std::fprintf(stderr, "[CreateOnDemand] buffer of %d bytes: %s\n", _loc.len, why.c_str());
        } else {
            NoteDecodeFailure(_loc);
        }
        MaybePrint();
        return Result(ok);
    }

    // The load's seeded static buffer: IMMUTABLE on DX11 (companions.cpp BufferStore::immutable).
    void Publish() override {
        _store->gpu = _made;
        _store->shared = _shared;
        _store->immutable = true;
        _made = GpuBuffer{};
        _shared = false;
    }

    void Evict() override {
        ReleaseBuffer(&_store->gpu, &_store->shared);
    }

    uint64_t Bytes() const override {
        return static_cast<uint64_t>(_loc.len);
    }

private:
    bool _index;
    VkBufferUsageFlags _usage;
    ::CreateOnDemand::Locator _loc;
    BufferStore* _store;
    GpuBuffer _made;
    bool _shared = false;
};

class TextureRecord final : public ::CreateOnDemand::Record {
public:
    TextureRecord(ImageBufferData* image, const TextureShape& shape, std::vector<::CreateOnDemand::Locator>&& levels)
        : _image(image), _shape(shape), _levels(std::move(levels)) {
        for (const ::CreateOnDemand::Locator& l : _levels)
            _bytes += static_cast<uint64_t>(l.len);
    }

    ::CreateOnDemand::Outcome Materialize() override {
        if (!DeviceLive()) return ::CreateOnDemand::Outcome::Retry;
        const int count = static_cast<int>(_levels.size());
        std::unique_ptr<uint8_t[]> bytes(new uint8_t[_bytes > 0 ? _bytes : 1]);
        std::vector<SourceLevel> source(static_cast<size_t>(count));
        uint64_t at = 0;
        bool ok = true;
        for (int i = 0; i < count && ok; ++i) {
            const ::CreateOnDemand::Locator& l = _levels[static_cast<size_t>(i)];
            ok = ::CreateOnDemand::Redecode(l, bytes.get() + at);
            if (!ok) NoteDecodeFailure(l);
            source[static_cast<size_t>(i)] = SourceLevel{ bytes.get() + at, static_cast<size_t>(l.len) };
            at += static_cast<uint64_t>(l.len);
        }
        if (ok) ok = MakeTexture(_shape, source.data(), count, true, &_made, &_info);
        MaybePrint();
        return Result(ok);
    }

    void Publish() override {
        _image->image = _made;
        _image->shared = _info.shared;
        _image->bc4 = _info.bc4;
        _image->state.image = _made.image;
        _image->state.aspect = _made.aspect;
        _image->state.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        _made = GpuImage{};
        _info = TextureMade{};
    }

    // The samplers stay (made at load, as retail makes them).
    void Evict() override {
        ReleaseImage(&_image->image, &_image->shared);
        _image->state = ImageState{};
    }

    uint64_t Bytes() const override {
        return _bytes;
    }

private:
    ImageBufferData* _image;
    TextureShape _shape;
    std::vector<::CreateOnDemand::Locator> _levels;
    GpuImage _made;
    TextureMade _info;
    uint64_t _bytes = 0;
};

// Two prefetch workers at most, one below eight hardware threads (DX11's rule).
int WorkerCount() {
    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    return std::max(1, std::min(2, hw / 4));
}

void Print(const char* when) {
    ::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed);
    if (t == nullptr) return;
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
    const int64_t now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    int64_t last = g_lastPrintNs.load(std::memory_order_relaxed);
    if (now - last < 30000000000LL) return;
    if (!g_lastPrintNs.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
    Print("in game (run total)");
}

void AfterSweep(int64_t evicted) {
    (void)evicted;
    Print("given back (run total)");
}

}  // namespace

bool DeferBuffer(const void* obj, IO::Stream* stream, bool index, uint32_t size, VkBufferUsageFlags usage,
                 BufferStore* store) {
    ::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed);
    if (t == nullptr) return false;
    ::CreateOnDemand::Locator loc;
    if (!::CreateOnDemand::Locate(stream, static_cast<int32_t>(size), &loc)) {
        g_notLocatable.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    ::CreateOnDemand::Skip(stream, static_cast<int32_t>(size));
    t->Forget(obj);
    t->Add(obj, std::unique_ptr<::CreateOnDemand::Record>(new BufferRecord(index, usage, std::move(loc), store)));
    return true;
}

bool HasPendingLevels(const void* owner) {
    const ::CreateOnDemand::PendingLevels& p = ::CreateOnDemand::ThreadPendingLevels();
    return g_table.load(std::memory_order_relaxed) != nullptr && p.image == owner && !p.levels.empty();
}

void DeferTexture(ImageBufferData* image, const TextureShape& shape) {
    ::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed);
    ::CreateOnDemand::PendingLevels& p = ::CreateOnDemand::ThreadPendingLevels();
    // ext.texture_quality: the level reader left Locators for the kept levels only (level_source.cpp: a
    // dropped level is never re-decoded), which is what `shape` was made for.
    std::vector<::CreateOnDemand::Locator> levels = std::move(p.levels);
    p.Clear();
    t->Forget(image);
    t->Add(image, std::unique_ptr<::CreateOnDemand::Record>(new TextureRecord(image, shape, std::move(levels))));
}

void Pause() {
    if (::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed)) {
        t->Pause();
        Print("device going away");
    }
}

void Resume() {
    if (::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed)) t->Resume();
}

void LoadEnd() {
    Print("load end (run total)");
}

void FinishPrefetches() {
    ::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed);
    if (t == nullptr) return;
    const auto t0 = std::chrono::steady_clock::now();
    t->DrainQueueUnlessPaused();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms >= 1.0) {
        std::fprintf(stderr, "[CreateOnDemand] load end waited %.0f ms for the prefetches (ETC2 transcode)\n", ms);
    }
}

}  // namespace CreateOnDemand
}  // namespace VideoVulkan

// ---- the backend-neutral interface (create_on_demand_backend.h) -----------------------------------
namespace CreateOnDemandBackend {

void Request(bool on, int idleMinutes) {
    if (on && VideoVulkan::CreateOnDemand::g_table.load(std::memory_order_relaxed) == nullptr) {
        // Never destroyed: records and workers live to the end of the process.
        ::CreateOnDemand::LazyTable* const t =
            new ::CreateOnDemand::LazyTable(VideoVulkan::CreateOnDemand::WorkerCount());
        // One epoch is a minute: an object unused for `idleMinutes` epochs is given back.
        t->SetSweepHook(&VideoVulkan::CreateOnDemand::AfterSweep);
        t->SetIdleLimit(idleMinutes > 0 ? static_cast<uint32_t>(idleMinutes) : 0, std::chrono::minutes(1));
        VideoVulkan::CreateOnDemand::g_table.store(t, std::memory_order_release);
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

// Every Vulkan device can: the deferred create is the load's create, made later.
bool Supported() {
    return true;
}

void Prefetch(const void* companion) {
    if (::CreateOnDemand::LazyTable* const t = VideoVulkan::CreateOnDemand::g_table.load(std::memory_order_relaxed)) {
        t->Prefetch(companion);
    }
}

void Keep(const void* companion) {
    if (::CreateOnDemand::LazyTable* const t = VideoVulkan::CreateOnDemand::g_table.load(std::memory_order_relaxed)) {
        t->Keep(companion);
    }
}

bool VertexBufferMade(const void* companion) {
    return companion == nullptr ||
           static_cast<const VideoVulkan::VertexBufferData*>(companion)->store.Buffer() != VK_NULL_HANDLE;
}

// The image's sampled view is the made object.
bool ImageMade(const void* companion) {
    if (companion == nullptr) return true;
    return static_cast<const VideoVulkan::ImageBufferData*>(companion)->image.view != VK_NULL_HANDLE;
}

}  // namespace CreateOnDemandBackend
