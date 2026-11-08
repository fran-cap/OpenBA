// ============================================================================
// src/render/dx11/share_gpu_data/share_create.cpp -- ext.share_gpu_data, the DX11 side (NOT RETAIL)
// ============================================================================
// share_create.h has the contract; docs/extensions/handins/SHAREGPUDATA.md.
// ============================================================================

#include "share_gpu_data/share_create.h"

#include "share_gpu_data/share_gpu_data_backend.h"
#include "share_gpu_data/share_table.h"
#include "ext/derived_cache/hash64.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace VideoDX11 {
namespace ShareGpuData {

namespace {

std::atomic<bool>     g_requested{false};
std::atomic<uint64_t> g_hashNs{0};      // time spent hashing seed bytes, all threads
std::atomic<uint64_t> g_refused{0};     // immutable textures whose subresources did not qualify
GpuShare::Stats       g_lastPrinted;    // LoadEnd only (loader thread)

GpuShare::Table& Table() {
    static GpuShare::Table t;
    return t;
}

uint64_t NowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

template <class T>
void AppendDesc(GpuShare::Key* key, const T& value) {
    static_assert(sizeof(T) <= GpuShare::kMaxDesc, "descriptor blob too large");
    std::memcpy(key->desc + key->descSize, &value, sizeof(T));
    key->descSize += static_cast<uint32_t>(sizeof(T));
}

bool IsBlockCompressed(DXGI_FORMAT f) {
    return (f >= DXGI_FORMAT_BC1_TYPELESS && f <= DXGI_FORMAT_BC5_SNORM) ||
           (f >= DXGI_FORMAT_BC6H_TYPELESS && f <= DXGI_FORMAT_BC7_UNORM_SRGB);
}

// The number of subresources D3D11 reads from pInitialData for `desc`.
int SubresourceCount(const D3D11_TEXTURE2D_DESC& desc, int* mipsOut) {
    int mips = static_cast<int>(desc.MipLevels);
    if (mips == 0) {   // the full chain
        uint32_t m = desc.Width > desc.Height ? desc.Width : desc.Height;
        mips = 1;
        while (m > 1) {
            m >>= 1;
            ++mips;
        }
    }
    *mipsOut = mips;
    return mips * static_cast<int>(desc.ArraySize);
}

// Every subresource D3D11 reads lies inside the bytes the key hashes: the count matches the
// descriptor, and each level's size covers pitch x rows. Anything else (a retail over-read) is
// created unshared.
bool TextureQualifies(const D3D11_TEXTURE2D_DESC& desc, const D3D11_SUBRESOURCE_DATA* subres, int count,
                      const uint64_t* sizes) {
    if (desc.Usage != D3D11_USAGE_IMMUTABLE || desc.BindFlags != D3D11_BIND_SHADER_RESOURCE ||
        desc.CPUAccessFlags != 0 || desc.SampleDesc.Count != 1 || subres == nullptr)
        return false;
    if (desc.MiscFlags != 0 && desc.MiscFlags != D3D11_RESOURCE_MISC_TEXTURECUBE) return false;
    int mips = 0;
    if (count != SubresourceCount(desc, &mips)) return false;
    const bool bc = IsBlockCompressed(desc.Format);
    for (int i = 0; i < count; ++i) {
        if (subres[i].pSysMem == nullptr) return false;
        const int mip = i % mips;
        uint32_t h = desc.Height >> mip;
        if (h == 0) h = 1;
        const uint64_t rows = bc ? (h + 3) / 4 : h;
        if (sizes[i] < static_cast<uint64_t>(subres[i].SysMemPitch) * rows) return false;
    }
    return true;
}

}  // namespace

bool Requested() {
    return g_requested.load(std::memory_order_relaxed);
}

HRESULT CreateBuffer(ID3D11Device* dev, const D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA* init,
                     ID3D11Buffer** out) {
    if (!Requested() || desc.Usage != D3D11_USAGE_IMMUTABLE || desc.CPUAccessFlags != 0 || desc.ByteWidth == 0 ||
        init == nullptr || init->pSysMem == nullptr)
        return dev->CreateBuffer(&desc, init, out);

    GpuShare::Key key;
    key.device = dev;
    key.kind   = (desc.BindFlags & D3D11_BIND_INDEX_BUFFER) != 0 ? GpuShare::kIndexBuffer : GpuShare::kVertexBuffer;
    AppendDesc(&key, desc);
    key.payloadBytes = desc.ByteWidth;
    const uint64_t t0 = NowNs();
    key.payloadHash  = Ext::DerivedCache::Hash64(init->pSysMem, desc.ByteWidth);
    g_hashNs.fetch_add(NowNs() - t0, std::memory_order_relaxed);

    void* handles[GpuShare::kHandles] = {};
    if (Table().Acquire(key, handles)) {
        ID3D11Buffer* const shared = static_cast<ID3D11Buffer*>(handles[0]);
        shared->AddRef();
        *out = shared;
        return S_OK;
    }
    const HRESULT hr = dev->CreateBuffer(&desc, init, out);
    if (hr != S_OK || *out == nullptr) return hr;
    (*out)->AddRef();   // the table's reference
    handles[0] = *out;
    if (Table().Insert(key, handles)) return hr;
    // Two creates of an equal buffer raced (the two async workers) and the other one's is in the
    // table: nobody has seen ours yet (its user waits for this job), so it goes and the winner's is
    // handed out instead.
    (*out)->Release();
    if (Table().Acquire(key, handles)) {
        ID3D11Buffer* const shared = static_cast<ID3D11Buffer*>(handles[0]);
        shared->AddRef();
        (*out)->Release();
        *out = shared;
    }
    return hr;
}

HRESULT CreateTexture(ID3D11Device* dev, const D3D11_TEXTURE2D_DESC& desc, const D3D11_SUBRESOURCE_DATA* subres,
                      int subresCount, const uint64_t* levelSizes, const D3D11_SHADER_RESOURCE_VIEW_DESC* srvDesc,
                      ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSrv) {
    const bool share = Requested() && TextureQualifies(desc, subres, subresCount, levelSizes);
    if (Requested() && !share && desc.Usage == D3D11_USAGE_IMMUTABLE)
        g_refused.fetch_add(1, std::memory_order_relaxed);

    GpuShare::Key key;
    void* handles[GpuShare::kHandles] = {};
    if (share) {
        key.device = dev;
        key.kind   = GpuShare::kTexture;
        AppendDesc(&key, desc);
        // The SRV's descriptor: whether there is one and its meaningful fields (the union's other
        // bytes are never written by ImageBufferData::Create, so they are not compared).
        uint32_t view[4] = {};
        if (srvDesc != nullptr) {
            view[0] = static_cast<uint32_t>(srvDesc->Format) + 1;
            view[1] = static_cast<uint32_t>(srvDesc->ViewDimension);
            view[2] = srvDesc->TextureCube.MostDetailedMip;
            view[3] = srvDesc->TextureCube.MipLevels;
        }
        AppendDesc(&key, view);
        const uint64_t t0 = NowNs();
        uint64_t h     = 0;
        uint64_t total = 0;
        for (int i = 0; i < subresCount; ++i) {
            const uint64_t meta[2] = { subres[i].SysMemPitch, levelSizes[i] };
            h = Ext::DerivedCache::Hash64(meta, sizeof(meta), h);
            h = Ext::DerivedCache::Hash64(subres[i].pSysMem, static_cast<size_t>(levelSizes[i]), h);
            total += levelSizes[i];
        }
        g_hashNs.fetch_add(NowNs() - t0, std::memory_order_relaxed);
        key.payloadBytes = total;
        key.payloadHash  = h;
        if (Table().Acquire(key, handles)) {
            ID3D11Texture2D* const texture = static_cast<ID3D11Texture2D*>(handles[0]);
            ID3D11ShaderResourceView* const srv = static_cast<ID3D11ShaderResourceView*>(handles[1]);
            texture->AddRef();
            srv->AddRef();
            *outTexture = texture;
            *outSrv     = srv;
            return S_OK;
        }
    }

    HRESULT hr = dev->CreateTexture2D(&desc, subres, outTexture);
    if (hr != S_OK) return hr;
    hr = dev->CreateShaderResourceView(*outTexture, srvDesc, outSrv);
    if (!share || hr != S_OK || *outTexture == nullptr || *outSrv == nullptr) return hr;
    (*outTexture)->AddRef();   // the table's references
    (*outSrv)->AddRef();
    handles[0] = *outTexture;
    handles[1] = *outSrv;
    if (Table().Insert(key, handles)) return hr;
    // A raced equal create is in the table: ours goes, the winner's is handed out (as CreateBuffer).
    (*outSrv)->Release();
    (*outTexture)->Release();
    if (Table().Acquire(key, handles)) {
        ID3D11Texture2D* const texture = static_cast<ID3D11Texture2D*>(handles[0]);
        ID3D11ShaderResourceView* const srv = static_cast<ID3D11ShaderResourceView*>(handles[1]);
        texture->AddRef();
        srv->AddRef();
        (*outSrv)->Release();
        (*outTexture)->Release();
        *outTexture = texture;
        *outSrv     = srv;
    }
    return hr;
}

void DropBuffer(ID3D11Buffer* buffer) {
    void* handles[GpuShare::kHandles] = {};
    if (!Table().Drop(buffer, handles)) return;
    static_cast<ID3D11Buffer*>(handles[0])->Release();
}

void DropTexture(ID3D11Texture2D* texture) {
    void* handles[GpuShare::kHandles] = {};
    if (!Table().Drop(texture, handles)) return;
    // The view first, as ImageBufferData::ReleaseInterfaces orders its own.
    if (handles[1] != nullptr) static_cast<ID3D11ShaderResourceView*>(handles[1])->Release();
    static_cast<ID3D11Texture2D*>(handles[0])->Release();
}

void Clear() {
    std::vector<void*> handles;
    Table().Clear(&handles);
    for (void* h : handles) {
        static_cast<IUnknown*>(h)->Release();
    }
    if (!handles.empty())
        std::fprintf(stderr, "[ShareGpuData] device going away: the table released %zu reference(s)\n",
                     handles.size());
}

void LoadEnd() {
    if (!Requested()) return;
    const GpuShare::Stats s = Table().GetStats();
    uint64_t hits = 0;
    for (uint32_t k = 0; k < GpuShare::kKindCount; ++k) {
        hits += s.hits[k] - g_lastPrinted.hits[k];
    }
    if (hits == 0 && s.inserts[GpuShare::kTexture] == g_lastPrinted.inserts[GpuShare::kTexture] &&
        s.inserts[GpuShare::kVertexBuffer] == g_lastPrinted.inserts[GpuShare::kVertexBuffer])
        return;
    g_lastPrinted = s;
    const double mb = 1.0 / (1024.0 * 1024.0);
    std::fprintf(stderr,
                 "[ShareGpuData] shared (run total): vb hits=%llu (%.1f MB) ib hits=%llu (%.1f MB) "
                 "tex hits=%llu (%.1f MB); entries=%llu (vb %llu / ib %llu / tex %llu made), freed=%llu, "
                 "raced=%llu, tex refused=%llu, hash %.0f ms\n",
                 static_cast<unsigned long long>(s.hits[GpuShare::kVertexBuffer]),
                 s.hitBytes[GpuShare::kVertexBuffer] * mb,
                 static_cast<unsigned long long>(s.hits[GpuShare::kIndexBuffer]),
                 s.hitBytes[GpuShare::kIndexBuffer] * mb,
                 static_cast<unsigned long long>(s.hits[GpuShare::kTexture]), s.hitBytes[GpuShare::kTexture] * mb,
                 static_cast<unsigned long long>(s.live),
                 static_cast<unsigned long long>(s.inserts[GpuShare::kVertexBuffer]),
                 static_cast<unsigned long long>(s.inserts[GpuShare::kIndexBuffer]),
                 static_cast<unsigned long long>(s.inserts[GpuShare::kTexture]),
                 static_cast<unsigned long long>(s.freed), static_cast<unsigned long long>(s.raced),
                 static_cast<unsigned long long>(g_refused.load(std::memory_order_relaxed)),
                 static_cast<double>(g_hashNs.load(std::memory_order_relaxed)) / 1e6);
}

}  // namespace ShareGpuData
}  // namespace VideoDX11

// ---- the backend-neutral interface (share_gpu_data_backend.h) -------------------------------------
namespace ShareGpuDataBackend {
BANISHED_RENDER_IMPL_BEGIN

void Request(bool on) {
    VideoDX11::ShareGpuData::g_requested.store(on, std::memory_order_relaxed);
    std::fprintf(stderr, "[ShareGpuData] %s\n",
                 on ? "on: equal immutable buffers and textures share one GPU object" : "off");
}

bool Supported() {
    return true;
}

BANISHED_RENDER_IMPL_END
}  // namespace ShareGpuDataBackend
