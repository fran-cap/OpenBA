// ============================================================================
// src/render/dx11/create_on_demand/lazy_create.cpp -- ext.create_on_demand, the DX11 side
// (docs/extensions/handins/CREATEONDEMAND.md). The contract is in lazy_create.h.
// ============================================================================
// NOT RETAIL.
//
// Two record kinds over the backend-neutral table (lazy_table.h). Each materializes on the device
// current at that moment (Dx11Device(), held by a reference for the duration), with the bytes
// re-decoded from the package into a block of its own, and runs the creates the load would have run,
// in the load's order, through the same helpers: ext.compress_ao's ConvertForCreate (the derived
// cache's lookup, else the encode), then ext.share_gpu_data's CreateBuffer / CreateTexture (a plain
// create when that row is off). Publish writes the handles into the companion's fields.
// ============================================================================

#include "create_on_demand/lazy_create.h"

#include "create_on_demand/create_on_demand_backend.h"
#include "create_on_demand/pending_levels.h"
#include "create_on_demand/stream_locate.h"

#include "compress_ao/bc4_create.h"        // ext.compress_ao: the conversion, at materialize
#include "dx11_backend_access.h"           // Dx11Device
#include "VideoDX11Data.h"                 // VertexBufferData::buffer, ImageBufferData::shaderResourceView
#include "share_gpu_data/share_create.h"   // ext.share_gpu_data: the create-or-share, at materialize

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace VideoDX11 {
namespace CreateOnDemand {

std::atomic<::CreateOnDemand::LazyTable*> g_table{nullptr};

namespace {

std::atomic<int64_t> g_notLocatable{0};   // static buffers / textures read as retail does
std::atomic<int64_t> g_decodeFailed{0};
::CreateOnDemand::LazyTable::Stats g_lastPrinted;

// The device a materialization runs against, held for its duration. Null while there is none, or
// while it is lost (its creates would fail; the record waits for the recovered device instead).
ID3D11Device* AcquireDevice() {
    ID3D11Device* const dev = Dx11Device();
    if (dev == nullptr || dev->GetDeviceRemovedReason() != S_OK) return nullptr;
    dev->AddRef();
    return dev;
}

bool Lost(ID3D11Device* dev) {
    return dev->GetDeviceRemovedReason() != S_OK;
}

::CreateOnDemand::Outcome Result(bool ok) {
    return ok ? ::CreateOnDemand::Outcome::Made : ::CreateOnDemand::Outcome::Failed;
}

// After a materialization (no table lock held): the run's summary, at most every 30 s.
void MaybePrint();

void NoteDecodeFailure(const ::CreateOnDemand::Locator& loc) {
    const int64_t n = g_decodeFailed.fetch_add(1, std::memory_order_relaxed);
    if (n < 8)
        std::fprintf(stderr, "[CreateOnDemand] re-decode FAILED: entry 0x%llx size %u pos %d len %d\n",
                     static_cast<unsigned long long>(loc.entry), loc.size, loc.pos, loc.len);
}

class BufferRecord final : public ::CreateOnDemand::Record {
public:
    BufferRecord(const D3D11_BUFFER_DESC& desc, ::CreateOnDemand::Locator&& loc, ID3D11Buffer** out)
        : _desc(desc), _loc(std::move(loc)), _out(out) {}

    ::CreateOnDemand::Outcome Materialize() override {
        ID3D11Device* const dev = AcquireDevice();
        if (dev == nullptr) return ::CreateOnDemand::Outcome::Retry;
        std::unique_ptr<uint8_t[]> bytes(new uint8_t[static_cast<size_t>(_loc.len)]);
        bool ok = ::CreateOnDemand::Redecode(_loc, bytes.get());
        if (ok) {
            D3D11_SUBRESOURCE_DATA init = {};
            init.pSysMem = bytes.get();
            ShareGpuData::CreateBuffer(dev, _desc, &init, &_made);
            ok = _made != nullptr;
        } else {
            NoteDecodeFailure(_loc);
        }
        // The device was lost while this ran: what it made (or failed to make) belongs to a dead
        // device. Nothing is published; the record waits for the recovered device.
        if (Lost(dev)) {
            if (_made != nullptr) {
                ShareGpuData::DropBuffer(_made);
                _made->Release();
                _made = nullptr;
            }
            dev->Release();
            return ::CreateOnDemand::Outcome::Retry;
        }
        dev->Release();
        MaybePrint();
        return Result(ok);
    }

    // Release: a use that sees the handle sees the made buffer (lazy_table.h LoadPublished).
    void Publish() override {
        ::CreateOnDemand::StorePublished(*_out, _made);
    }

    // share_gpu_data's user is dropped before the Release, as ~VertexBufferData orders it.
    void Evict() override {
        ID3D11Buffer* const buffer = *_out;
        ::CreateOnDemand::StorePublished(*_out, static_cast<ID3D11Buffer*>(nullptr));
        _made = nullptr;
        if (buffer == nullptr) return;
        ShareGpuData::DropBuffer(buffer);
        buffer->Release();
    }

    uint64_t Bytes() const override {
        return static_cast<uint64_t>(_loc.len);
    }

private:
    D3D11_BUFFER_DESC         _desc;
    ::CreateOnDemand::Locator _loc;
    ID3D11Buffer**            _out;
    ID3D11Buffer*             _made = nullptr;
};

class TextureRecord final : public ::CreateOnDemand::Record {
public:
    TextureRecord(const D3D11_TEXTURE2D_DESC& desc, const UINT* pitches, std::vector<::CreateOnDemand::Locator>&& levels,
                  const D3D11_SHADER_RESOURCE_VIEW_DESC* srvDesc, uint32_t bc4SourceFormat,
                  ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSrv)
        : _desc(desc), _pitches(pitches, pitches + levels.size()), _levels(std::move(levels)),
          _hasSrvDesc(srvDesc != nullptr), _bc4SourceFormat(bc4SourceFormat), _outTexture(outTexture),
          _outSrv(outSrv) {
        if (srvDesc != nullptr) _srvDesc = *srvDesc;
        for (const ::CreateOnDemand::Locator& l : _levels) {
            _bytes += static_cast<uint64_t>(l.len);
        }
    }

    ::CreateOnDemand::Outcome Materialize() override {
        ID3D11Device* const dev = AcquireDevice();
        if (dev == nullptr) return ::CreateOnDemand::Outcome::Retry;
        const int count = static_cast<int>(_levels.size());
        std::unique_ptr<uint8_t[]> bytes(new uint8_t[_bytes > 0 ? _bytes : 1]);
        std::vector<D3D11_SUBRESOURCE_DATA> subres(static_cast<size_t>(count));
        std::vector<uint64_t> sizes(static_cast<size_t>(count));
        uint64_t at = 0;
        bool ok = true;
        for (int i = 0; i < count && ok; ++i) {
            const ::CreateOnDemand::Locator& l = _levels[static_cast<size_t>(i)];
            ok = ::CreateOnDemand::Redecode(l, bytes.get() + at);
            if (!ok) NoteDecodeFailure(l);
            subres[static_cast<size_t>(i)].pSysMem          = bytes.get() + at;
            subres[static_cast<size_t>(i)].SysMemPitch      = _pitches[static_cast<size_t>(i)];
            subres[static_cast<size_t>(i)].SysMemSlicePitch = 0;
            sizes[static_cast<size_t>(i)] = static_cast<uint64_t>(l.len);
            at += static_cast<uint64_t>(l.len);
        }
        if (ok) {
            D3D11_TEXTURE2D_DESC desc = _desc;
            // ext.compress_ao: `bc4` holds the converted levels until the create has copied them.
            std::vector<uint8_t> bc4;
            if (_bc4SourceFormat != 0)
                CompressAo::ConvertForCreate(&desc, subres.data(), count, &sizes, _bc4SourceFormat, &bc4);
            ShareGpuData::CreateTexture(dev, desc, count > 0 ? subres.data() : nullptr, count, sizes.data(),
                                        _hasSrvDesc ? &_srvDesc : nullptr, &_madeTexture, &_madeSrv);
            ok = _madeTexture != nullptr && _madeSrv != nullptr;
        }
        if (Lost(dev)) {   // as BufferRecord: nothing made on a dead device is published
            ShareGpuData::DropTexture(_madeTexture);
            if (_madeSrv != nullptr) _madeSrv->Release();
            if (_madeTexture != nullptr) _madeTexture->Release();
            _madeSrv     = nullptr;
            _madeTexture = nullptr;
            dev->Release();
            return ::CreateOnDemand::Outcome::Retry;
        }
        dev->Release();
        MaybePrint();
        return Result(ok);
    }

    // Both with release, the view last: Bind tests the view, Update / Read the texture, so a use
    // that sees the view also sees the texture.
    void Publish() override {
        ::CreateOnDemand::StorePublished(*_outTexture, _madeTexture);
        ::CreateOnDemand::StorePublished(*_outSrv, _madeSrv);
    }

    // ImageBufferData::ReleaseInterfaces' order for these two: the share table's user first, the
    // view, then the texture. The sampler stays (made at load, as retail makes it).
    void Evict() override {
        ID3D11Texture2D* const texture = *_outTexture;
        ID3D11ShaderResourceView* const srv = *_outSrv;
        ::CreateOnDemand::StorePublished(*_outTexture, static_cast<ID3D11Texture2D*>(nullptr));
        ::CreateOnDemand::StorePublished(*_outSrv, static_cast<ID3D11ShaderResourceView*>(nullptr));
        _madeTexture = nullptr;
        _madeSrv     = nullptr;
        ShareGpuData::DropTexture(texture);
        if (srv != nullptr) srv->Release();
        if (texture != nullptr) texture->Release();
    }

    uint64_t Bytes() const override {
        return _bytes;
    }

private:
    D3D11_TEXTURE2D_DESC                   _desc;
    std::vector<UINT>                      _pitches;
    std::vector<::CreateOnDemand::Locator> _levels;
    bool                                   _hasSrvDesc;
    D3D11_SHADER_RESOURCE_VIEW_DESC        _srvDesc = {};
    uint32_t                               _bc4SourceFormat;
    ID3D11Texture2D**                      _outTexture;
    ID3D11ShaderResourceView**             _outSrv;
    ID3D11Texture2D*                       _madeTexture = nullptr;
    ID3D11ShaderResourceView*              _madeSrv     = nullptr;
    uint64_t                               _bytes       = 0;
};

// Two prefetch workers at most, one below eight hardware threads (ext.async_gpu_create's rule): the
// workers never crowd the main and render threads.
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
        s.evicted == g_lastPrinted.evicted)
        return;
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
    if (s.evicted != 0 || s.sweeps != 0)
        std::fprintf(stderr,
                     "[CreateOnDemand] %s: given back %lld (%.1f MB) over %lld sweeps (uses held %.1f ms), "
                     "re-made %lld, kept %lld; made and held %.1f MB\n",
                     when, static_cast<long long>(s.evicted), s.evictedBytes * mb, static_cast<long long>(s.sweeps),
                     s.evictNs / 1e6, static_cast<long long>(s.remade), static_cast<long long>(s.kept),
                     (static_cast<double>(s.recordedBytes) - static_cast<double>(s.pendingBytes)) * mb);
}

std::atomic<int64_t> g_lastPrintNs{0};

void MaybePrint() {
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    int64_t last = g_lastPrintNs.load(std::memory_order_relaxed);
    if (now - last < 30000000000LL) return;
    if (!g_lastPrintNs.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
    Print("in game (run total)");
}

}  // namespace

bool DeferBuffer(const void* obj, IO::Stream* stream, const D3D11_BUFFER_DESC& desc, ID3D11Buffer** out) {
    ::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed);
    if (t == nullptr) return false;
    ::CreateOnDemand::Locator loc;
    if (!::CreateOnDemand::Locate(stream, static_cast<int32_t>(desc.ByteWidth), &loc)) {
        g_notLocatable.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    ::CreateOnDemand::Skip(stream, static_cast<int32_t>(desc.ByteWidth));
    t->Forget(obj);
    t->Add(obj, std::unique_ptr<::CreateOnDemand::Record>(new BufferRecord(desc, std::move(loc), out)));
    return true;
}

bool HasPendingLevels(const void* owner) {
    const ::CreateOnDemand::PendingLevels& p = ::CreateOnDemand::ThreadPendingLevels();
    return g_table.load(std::memory_order_relaxed) != nullptr && p.image == owner && !p.levels.empty();
}

void DeferTexture(const void* obj, const void* owner, const D3D11_TEXTURE2D_DESC& desc, const UINT* pitches,
                  int count, const D3D11_SHADER_RESOURCE_VIEW_DESC* srvDesc, uint32_t bc4SourceFormat,
                  ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSrv) {
    ::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed);
    ::CreateOnDemand::PendingLevels& p = ::CreateOnDemand::ThreadPendingLevels();
    std::vector<::CreateOnDemand::Locator> levels = std::move(p.levels);
    p.Clear();
    (void)owner;
    (void)count;   // == levels.size(): the reader left one Locator per level blob
    t->Forget(obj);
    t->Add(obj, std::unique_ptr<::CreateOnDemand::Record>(new TextureRecord(
                    desc, pitches, std::move(levels), srvDesc, bc4SourceFormat, outTexture, outSrv)));
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

void AfterSweep(int64_t evicted) {
    (void)evicted;
    Print("given back (run total)");
}

}  // namespace CreateOnDemand
}  // namespace VideoDX11

// ---- the backend-neutral interface (create_on_demand_backend.h) -----------------------------------
namespace CreateOnDemandBackend {

void Request(bool on, int idleMinutes) {
    if (on && VideoDX11::CreateOnDemand::g_table.load(std::memory_order_relaxed) == nullptr) {
        // Never destroyed: records and workers live to the end of the process.
        ::CreateOnDemand::LazyTable* const t = new ::CreateOnDemand::LazyTable(VideoDX11::CreateOnDemand::WorkerCount());
        // One epoch is a minute: an object unused for `idleMinutes` epochs is given back.
        t->SetSweepHook(&VideoDX11::CreateOnDemand::AfterSweep);
        t->SetIdleLimit(idleMinutes > 0 ? static_cast<uint32_t>(idleMinutes) : 0, std::chrono::minutes(1));
        VideoDX11::CreateOnDemand::g_table.store(t, std::memory_order_release);
    }
    if (on)
        std::fprintf(stderr, "[CreateOnDemand] on: static buffers and textures are created at their first use; "
                             "given back after %d idle minute(s) (0 = never)\n", idleMinutes);
    else
        std::fprintf(stderr, "[CreateOnDemand] off\n");
}

bool Supported() {
    return true;
}

void Prefetch(const void* companion) {
    if (::CreateOnDemand::LazyTable* const t = VideoDX11::CreateOnDemand::g_table.load(std::memory_order_relaxed))
        t->Prefetch(companion);
}

void Keep(const void* companion) {
    if (::CreateOnDemand::LazyTable* const t = VideoDX11::CreateOnDemand::g_table.load(std::memory_order_relaxed))
        t->Keep(companion);
}

// The gather calls these with no gate and no lock while a worker publishes or a sweep evicts: acquire
// loads (lazy_table.h LoadPublished).
bool VertexBufferMade(const void* companion) {
    return companion == nullptr ||
           ::CreateOnDemand::LoadPublished(static_cast<const VideoDX11::VertexBufferData*>(companion)->buffer) !=
               nullptr;
}

bool ImageMade(const void* companion) {
    return companion == nullptr ||
           ::CreateOnDemand::LoadPublished(
               static_cast<const VideoDX11::ImageBufferData*>(companion)->shaderResourceView) != nullptr;
}

}  // namespace CreateOnDemandBackend
