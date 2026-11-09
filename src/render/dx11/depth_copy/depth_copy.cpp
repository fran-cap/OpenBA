// ============================================================================
// src/render/dx11/depth_copy/depth_copy.cpp -- THE SHARED DEPTH COPY on this backend (depth_copy.h)
// ============================================================================
// NOT RETAIL. The contract and the reuse rule: src/render/common/depth_copy/depth_copy_backend.h.
//
// THE TWO SOURCES (depth_copy.h THE COPY). The readable path is the AO pass's old one (ao_pass.cpp):
// the main depth's own R24_UNORM_X8 SRV, which exists only while AO asked for a readable depth, drawn by
// AO's resolve into R32_FLOAT. The twin path is the water's and the clouds' old one: CopyResource of
// the bound depth into a typeless texture of the same shape (R24G8_TYPELESS for D24S8, R32_TYPELESS for
// D32), then the resolve on a multisampled twin, or the twin's own read view on a single-sampled one.
// The AO pass asks for the main depth by name (Source::Main: the default render buffer's depth, as it
// always did); the water and the clouds for the depth bound as the DSV (Source::Bound). A Bound request
// takes the readable path when the bound depth is the main depth and that has its SRV, which skips the
// CopyResource of the whole depth (at 2560x1440 x4 about 59 MB). Sample 0 of the same bytes, or the same
// single-sampled value (a 24-bit UNORM is exact in a float), lands either way.
//
// THE REUSE. The copy carries its stamp (depth_copy_backend.h Stamp): the engine context's frame
// (Context::_frameIndex, advanced once per Finish), the main-depth write count (depth_copy.h Count:
// every engine draw under a depth-writing state, every depth clear), the depth texture copied, its size
// and samples. A later request with the same stamp gets the same view without a draw.
//
// THE STATE. The resolve draw binds its own RTV, viewport, shaders and states; every binding it touches
// is saved before and put back after (ao/ao_state_backup.h), so the engine's ContextData cache of what it
// bound stays true and the main colour and depth are bound again as they were.
//
// LIFETIME. The resolve and the twin per (D3D device, size, samples, format); a difference remakes them
// (a resize, an Options Apply, a device restart). Request(false) by the last reader releases everything
// under the device lock. One [DepthCopy] stderr line per (re)make, none per frame.
// ============================================================================
#include "depth_copy/depth_copy.h"

#include "ao/ao_depth_resolve.h"
#include "ao/ao_state_backup.h"
#include "depth_copy/depth_copy_backend.h"
#include "dx11_backend_access.h"
#include "video/Context.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"

#include <atomic>
#include <cstdio>

namespace VideoDX11 {
namespace DepthCopy {

namespace {

template <class T> void Rel(T*& p) {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

// The twin's typeless format and its read view's format for a depth of format `f`; false when the depth
// is in neither family (never seen: the device's depth is D24S8).
bool TwinFormatsFor(DXGI_FORMAT f, DXGI_FORMAT& texture, DXGI_FORMAT& view) {
    switch (f) {
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
        texture = DXGI_FORMAT_R24G8_TYPELESS;
        view    = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        return true;
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
        texture = DXGI_FORMAT_R32_TYPELESS;
        view    = DXGI_FORMAT_R32_FLOAT;
        return true;
    default:
        return false;
    }
}

// What the twin was made for. A difference remakes it.
struct TwinKey {
    ID3D11Device* device  = nullptr;   // not AddRef'd: compared only
    unsigned      width   = 0;
    unsigned      height  = 0;
    unsigned      samples = 0;
    DXGI_FORMAT   format  = DXGI_FORMAT_UNKNOWN;

    bool operator==(const TwinKey& o) const {
        return device == o.device && width == o.width && height == o.height && samples == o.samples &&
               format == o.format;
    }
};

struct State {
    ID3D11Device*             device        = nullptr;    // the device every object below belongs to
    bool                      resolveMade   = false;      // `resolve` created on `device`
    bool                      resolveFailed = false;      // on `device`: wait for it to change
    AoStage::DepthResolve     resolve{"DepthCopy"};
    TwinKey                   twinKey;
    bool                      twinFailed = false;         // for twinKey: wait for the key to change
    ID3D11Texture2D*          twin       = nullptr;       // the CopyResource destination
    ID3D11ShaderResourceView* twinView   = nullptr;       // on `twin` (TEXTURE2D or TEXTURE2DMS)
    ID3D11ShaderResourceView* current    = nullptr;       // the copy's view (not AddRef'd)
    DepthCopyBackend::Stamp   made;                       // frame ~0: nothing copied
};
State g;

std::atomic<uint32_t> g_readers{0};

void ReleaseAll() {
    g.resolve.Release();
    Rel(g.twinView);
    Rel(g.twin);
    g.device        = nullptr;
    g.resolveMade   = false;
    g.resolveFailed = false;
    g.twinKey       = TwinKey{};
    g.twinFailed    = false;
    g.current       = nullptr;
    g.made          = DepthCopyBackend::Stamp{};
}

// Everything made on another device is released first (a device restart makes a new ID3D11Device).
void EnsureDevice(ID3D11Device* dev) {
    if (g.device == dev) return;
    if (g.device != nullptr) std::fprintf(stderr, "[DepthCopy] device changed: the copy is remade\n");
    ReleaseAll();
    g.device = dev;
}

bool EnsureResolve(ID3D11Device* dev, unsigned w, unsigned h) {
    if (!g.resolveMade && !g.resolveFailed) {
        g.resolveFailed = !g.resolve.Create(dev);
        g.resolveMade   = !g.resolveFailed;
    }
    return g.resolveMade && g.resolve.Resize(dev, w, h);
}

bool EnsureTwin(ID3D11Device* dev, const TwinKey& key) {
    if (g.twinKey == key) return !g.twinFailed && g.twin != nullptr;
    Rel(g.twinView);
    Rel(g.twin);
    g.twinKey    = key;
    g.twinFailed = true;
    DXGI_FORMAT texFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
    if (!TwinFormatsFor(key.format, texFormat, viewFormat)) {
        std::fprintf(stderr, "[DepthCopy] bound depth format %d is not a D24S8 / D32 family: no copy\n",
                     static_cast<int>(key.format));
        return false;
    }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = key.width;
    td.Height           = key.height;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = texFormat;
    td.SampleDesc.Count = key.samples;
    td.Usage            = D3D11_USAGE_DEFAULT;
    // DEPTH_STENCIL too, so the twin matches its source's kind (a depth-stencil resource); nothing binds
    // it as one.
    td.BindFlags        = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = dev->CreateTexture2D(&td, nullptr, &g.twin);
    if (hr == S_OK) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = viewFormat;
        if (key.samples > 1) {
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        } else {
            sd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = 1;
        }
        hr = dev->CreateShaderResourceView(g.twin, &sd, &g.twinView);
    }
    std::fprintf(stderr, "[DepthCopy] twin %ux%u x%u (depth format %d): %s (0x%08lx)\n", key.width, key.height,
                 key.samples, static_cast<int>(key.format), hr == S_OK ? "made" : "FAILED",
                 static_cast<unsigned long>(hr));
    if (hr != S_OK) {
        Rel(g.twinView);
        Rel(g.twin);
        return false;
    }
    g.twinFailed = false;
    return true;
}

// The depth texture behind the bound DSV (AddRef'd), or null when no depth is bound.
ID3D11Texture2D* BoundDepth(ID3D11DeviceContext* dc) {
    ID3D11DepthStencilView* dsv = nullptr;
    dc->OMGetRenderTargets(0, nullptr, &dsv);
    if (dsv == nullptr) return nullptr;
    ID3D11Resource* resource = nullptr;
    dsv->GetResource(&resource);
    dsv->Release();
    if (resource == nullptr) return nullptr;
    ID3D11Texture2D* texture = nullptr;
    resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
    resource->Release();
    return texture;
}

// The main depth's readable SRV and its texture (the default render buffer's depth), or nulls while the
// rebuild that makes the depth readable has not run (or AO never asked).
void MainDepth(const ::Video::Context& context, ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv) {
    *texture = nullptr;
    *srv     = nullptr;
    const ::Video::RenderBuffer* const rb = context._defaultRenderBuffer;
    if (rb == nullptr) return;
    const ::Video::ImageBuffer* const depthImage = rb->depthBuffer;
    if (depthImage == nullptr || depthImage->_data == nullptr) return;
    ImageBufferData* const depth = Dx11ImageBackend(depthImage);
    if (depth->shaderResourceView == nullptr || depth->texture == nullptr) return;
    *texture = depth->texture;
    *srv     = depth->shaderResourceView;
}

ID3D11ShaderResourceView* Make(ID3D11Device* dev, ID3D11DeviceContext* dc, ID3D11Texture2D* depth,
                               ID3D11ShaderResourceView* readable, const D3D11_TEXTURE2D_DESC& desc) {
    const unsigned samples = desc.SampleDesc.Count;
    EnsureDevice(dev);
    if (readable != nullptr) {
        if (!EnsureResolve(dev, desc.Width, desc.Height)) return nullptr;
        AoStage::StateBackup saved(dc);
        return g.resolve.Run(dc, readable, samples);
    }
    TwinKey key;
    key.device  = dev;
    key.width   = desc.Width;
    key.height  = desc.Height;
    key.samples = samples;
    key.format  = desc.Format;
    if (samples > 1 && !EnsureResolve(dev, desc.Width, desc.Height)) return nullptr;
    if (!EnsureTwin(dev, key)) return nullptr;
    dc->CopyResource(g.twin, depth);
    if (samples <= 1) return g.twinView;
    AoStage::StateBackup saved(dc);
    return g.resolve.Run(dc, g.twinView, samples);
}

}  // namespace

void Request(uint32_t reader, bool on) {
    // A reader may repeat its request every frame: only the last reader going away releases.
    if (on) {
        g_readers.fetch_or(reader);
        return;
    }
    const uint32_t was = g_readers.fetch_and(~reader);
    if ((was & reader) == 0 || (was & ~reader) != 0) return;
    ::Video::Device* const dev = ::Video::Device::_singleton;
    if (dev == nullptr) return;
    dev->Lock();
    const bool had = g.device != nullptr;
    ReleaseAll();
    dev->Unlock();
    if (had) std::fprintf(stderr, "[DepthCopy] released (no reader)\n");
}

ID3D11ShaderResourceView* Acquire(const ::Video::Context& context, Source source) {
    if (g_readers.load() == 0) return nullptr;
    ID3D11Device* const dev = Dx11Device();
    ID3D11DeviceContext* const dc = Dx11Context();
    if (dev == nullptr || dc == nullptr) return nullptr;

    ID3D11Texture2D* mainTexture = nullptr;
    ID3D11ShaderResourceView* mainSrv = nullptr;
    MainDepth(context, &mainTexture, &mainSrv);
    ID3D11Texture2D* depth = nullptr;   // AddRef'd
    if (source == Source::Main) {
        if (mainSrv == nullptr) return nullptr;
        depth = mainTexture;
        depth->AddRef();
    } else {
        depth = BoundDepth(dc);
        if (depth == nullptr) return nullptr;   // no depth bound (not the scene's target)
    }
    D3D11_TEXTURE2D_DESC desc;
    depth->GetDesc(&desc);

    DepthCopyBackend::Stamp now;
    now.frame       = static_cast<uint32_t>(context._frameIndex);
    now.depthWrites = Count::g_depthWrites;
    now.depth       = depth;
    now.width       = desc.Width;
    now.height      = desc.Height;
    now.samples     = desc.SampleDesc.Count;
    if (g.current != nullptr && g.device == dev && DepthCopyBackend::Fresh(g.made, now)) {
        depth->Release();
        return g.current;
    }
    ID3D11ShaderResourceView* const readable = depth == mainTexture ? mainSrv : nullptr;
    g.current = Make(dev, dc, depth, readable, desc);
    depth->Release();
    g.made = g.current != nullptr ? now : DepthCopyBackend::Stamp{};
    return g.current;
}

}  // namespace DepthCopy
}  // namespace VideoDX11
