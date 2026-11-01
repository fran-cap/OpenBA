// ============================================================================
// src/render/dx11/better_water/scene_depth.cpp -- ext.better_water's scene depth copy on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/better_water/scene_depth_backend.h
// (docs/extensions/handins/BETTERWATER.md section 8).
//
// THE COPY. Copy() runs at the frame-copy seam, while the main colour and depth are still bound as the
// output the water then draws into. The depth bound there (OMGetRenderTargets' DSV, the resource behind
// it) is copied whole with CopyResource into a texture of our own of the same size, sample count and
// typeless family (R24G8_TYPELESS for the device's D24S8). A copy reads its source through no view, so
// the depth may stay bound as the DSV meanwhile (an SRV on it while it is the DSV would be illegal),
// and the main depth needs no readable format: AO's request (ao_backend.h RequestReadableDepth) and its
// rebuild path are not involved, whatever AO does at run time. Both of retail's main-depth formats
// (typed D24_UNORM_S8_UINT, AO's R24G8_TYPELESS) are in that family, so the copy is legal from either.
//   1x:   the copy itself is read, through an R24_UNORM_X8_TYPELESS view (Load returns the 0..1 depth
//         as a float in .r).
//   MSAA: the copy is multisampled; AO's resolve pass (ao/ao_depth_resolve.h, sample 0, its own
//         instance) draws it into a 1x R32_FLOAT target, which is read. That draw changes OM / RS /
//         IA / shader / t0 state; every binding it touches is saved before and put back after, so the
//         engine's ContextData cache of what it bound stays true (the AO pass's rule, ao_pass.cpp).
//
// THE IMAGE. An engine Video::ImageBuffer made once through the class factory (CreateNewObject; its
// PlatformConstruct asks DeviceData for this backend's ImageBufferData companion). It is never Create()d:
// kind 2 and flags 0 keep it off the tracked-image registry and out of ReleaseDynamic / RecreateDynamic,
// so nothing but this file writes its companion. The companion holds a reference to the SRV the water
// reads (and the texture behind it), so the engine's own bind path (ImageBufferData::Bind:
// PSSetSamplers + PSSetShaderResources) binds it at t4; its sampler stays null (the shader Loads).
//
// LIFETIME. Remade when the bound depth's size, sample count or format, or the D3D device, differs from
// what the copy was made for (a window resize, an Options Apply, a device restart: DeviceData::
// CreateDevice makes a new ID3D11Device and the old objects are released here, as the AO pass and the
// better_trees stages do). Request(false) releases everything at once, under the device lock (the
// drawing thread holds it while it records a frame, so no draw of the frame can be binding the copy).
// One [SceneDepth] stderr line per (re)make, none per frame.
// ============================================================================
#include "better_water/scene_depth_backend.h"

#include "ao/ao_depth_resolve.h"
#include "dx11_backend_access.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"

#include <atomic>
#include <cstdio>

namespace {

template <class T> void Rel(T*& p) {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

ID3D11Device* LiveDevice() {
    const ::Video::Device* const dev = ::Video::Device::_singleton;
    if (dev == nullptr || dev->deviceData == nullptr) return nullptr;
    return VideoDX11::Dx11DeviceDataOf(dev)->d3dDevice;
}

// The copy's typeless format and its read view's format for a depth of format `f`; false when the
// depth is in neither family (never seen: the device's depth is D24S8).
bool CopyFormatsFor(DXGI_FORMAT f, DXGI_FORMAT& texture, DXGI_FORMAT& view) {
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

// What the copy was made for. A difference remakes it.
struct Key {
    ID3D11Device* device  = nullptr;   // not AddRef'd: compared only
    unsigned      width   = 0;
    unsigned      height  = 0;
    unsigned      samples = 0;
    DXGI_FORMAT   format  = DXGI_FORMAT_UNKNOWN;

    bool operator==(const Key& o) const {
        return device == o.device && width == o.width && height == o.height && samples == o.samples &&
               format == o.format;
    }
};

struct CopyState {
    Key                       key;
    bool                      made    = false;   // the objects for `key` exist
    bool                      failed  = false;   // making them for `key` failed: wait for the key to change
    bool                      ready   = false;   // a Copy() ran into them: Image() may hand them out
    ID3D11Texture2D*          texture = nullptr; // the CopyResource destination (typeless, key's samples)
    ID3D11ShaderResourceView* view    = nullptr; // on `texture` (TEXTURE2D or TEXTURE2DMS)
    AoStage::DepthResolve     resolve{"SceneDepth"};   // MSAA only: `texture` -> 1x R32_FLOAT
    ::Video::ImageBuffer*     image   = nullptr; // the engine image; made once, never deleted
};
CopyState g_copy;

std::atomic<bool> g_requested{false};

// The SRV the image hands out (and the texture behind it), held by its companion with a reference of
// its own. Null clears it.
void Expose(ID3D11ShaderResourceView* srv) {
    VideoDX11::ImageBufferData* const data = VideoDX11::Dx11ImageBackend(g_copy.image);
    if (data->shaderResourceView == srv) return;
    Rel(data->shaderResourceView);
    Rel(data->texture);
    if (srv == nullptr) return;
    srv->AddRef();
    data->shaderResourceView = srv;
    ID3D11Resource* resource = nullptr;
    srv->GetResource(&resource);
    if (resource != nullptr) {
        resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&data->texture));
        resource->Release();
    }
}

void ReleaseAll() {
    if (g_copy.image != nullptr) Expose(nullptr);
    g_copy.resolve.Release();
    Rel(g_copy.view);
    Rel(g_copy.texture);
    g_copy.key    = Key{};
    g_copy.made   = false;
    g_copy.failed = false;
    g_copy.ready  = false;
}

bool Make(ID3D11Device* dev, const Key& key) {
    if (g_copy.made || g_copy.key.device != nullptr) ReleaseAll();
    g_copy.key = key;
    DXGI_FORMAT texFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
    CopyFormatsFor(key.format, texFormat, viewFormat);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = key.width;
    td.Height           = key.height;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = texFormat;
    td.SampleDesc.Count = key.samples;
    td.Usage            = D3D11_USAGE_DEFAULT;
    // DEPTH_STENCIL too, so the copy matches its source's kind (a depth-stencil resource); nothing binds
    // it as one.
    td.BindFlags        = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = dev->CreateTexture2D(&td, nullptr, &g_copy.texture);
    if (hr == S_OK) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = viewFormat;
        if (key.samples > 1) {
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        } else {
            sd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = 1;
        }
        hr = dev->CreateShaderResourceView(g_copy.texture, &sd, &g_copy.view);
    }
    bool ok = hr == S_OK;
    if (ok && key.samples > 1) ok = g_copy.resolve.Create(dev) && g_copy.resolve.Resize(dev, key.width, key.height);
    std::fprintf(stderr, "[SceneDepth] copy %ux%u x%u (depth format %d -> %s%s): %s (0x%08lx)\n", key.width,
                 key.height, key.samples, static_cast<int>(key.format),
                 texFormat == DXGI_FORMAT_R24G8_TYPELESS ? "R24G8_TYPELESS" : "R32_TYPELESS",
                 key.samples > 1 ? ", sample 0 -> R32_FLOAT" : "", ok ? "made" : "FAILED",
                 static_cast<unsigned long>(hr));
    if (!ok) {
        ReleaseAll();
        g_copy.key    = key;
        g_copy.failed = true;
        return false;
    }
    g_copy.made = true;
    return true;
}

// Every binding the MSAA resolve draw (DepthResolve::Run) touches, saved and put back exactly.
class ResolveStateBackup {
public:
    explicit ResolveStateBackup(ID3D11DeviceContext* dc) : _dc(dc) {
        dc->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _rtvs, &_dsv);
        dc->OMGetBlendState(&_bs, _blendFactor, &_sampleMask);
        dc->OMGetDepthStencilState(&_dss, &_stencilRef);
        _numVp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        dc->RSGetViewports(&_numVp, _vps);
        dc->RSGetState(&_rs);
        dc->IAGetInputLayout(&_il);
        dc->IAGetPrimitiveTopology(&_topo);
        dc->VSGetShader(&_vs, nullptr, nullptr);
        dc->PSGetShader(&_ps, nullptr, nullptr);
        dc->PSGetShaderResources(0, 1, &_srv0);
    }
    ~ResolveStateBackup() {
        ID3D11DeviceContext* const dc = _dc;
        dc->PSSetShaderResources(0, 1, &_srv0);
        dc->VSSetShader(_vs, nullptr, 0);
        dc->PSSetShader(_ps, nullptr, 0);
        dc->IASetInputLayout(_il);
        dc->IASetPrimitiveTopology(_topo);
        dc->RSSetState(_rs);
        dc->RSSetViewports(_numVp, _vps);
        dc->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _rtvs, _dsv);
        dc->OMSetBlendState(_bs, _blendFactor, _sampleMask);
        dc->OMSetDepthStencilState(_dss, _stencilRef);
        for (auto*& p : _rtvs) Rel(p);
        Rel(_dsv);
        Rel(_bs);
        Rel(_dss);
        Rel(_rs);
        Rel(_il);
        Rel(_vs);
        Rel(_ps);
        Rel(_srv0);
    }
    ResolveStateBackup(const ResolveStateBackup&) = delete;
    ResolveStateBackup& operator=(const ResolveStateBackup&) = delete;

private:
    ID3D11DeviceContext*      _dc;
    ID3D11RenderTargetView*   _rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView*   _dsv = nullptr;
    ID3D11BlendState*         _bs  = nullptr;
    FLOAT                     _blendFactor[4] = {};
    UINT                      _sampleMask = 0;
    ID3D11DepthStencilState*  _dss = nullptr;
    UINT                      _stencilRef = 0;
    UINT                      _numVp = 0;
    D3D11_VIEWPORT            _vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    ID3D11RasterizerState*    _rs  = nullptr;
    ID3D11InputLayout*        _il  = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY  _topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11VertexShader*       _vs  = nullptr;
    ID3D11PixelShader*        _ps  = nullptr;
    ID3D11ShaderResourceView* _srv0 = nullptr;
};

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

// The copy itself; Copy() holds the device lock around it.
void CopyLocked() {
    ID3D11Device* const dev = VideoDX11::Dx11Device();
    ID3D11DeviceContext* const dc = VideoDX11::Dx11Context();
    ID3D11Texture2D* const depth = BoundDepth(dc);
    if (depth == nullptr) return;   // no depth bound (not the scene's target): the last copy stays
    D3D11_TEXTURE2D_DESC desc;
    depth->GetDesc(&desc);

    Key key;
    key.device  = dev;
    key.width   = desc.Width;
    key.height  = desc.Height;
    key.samples = desc.SampleDesc.Count;
    key.format  = desc.Format;
    DXGI_FORMAT texFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
    if (!CopyFormatsFor(desc.Format, texFormat, viewFormat)) {
        if (!(g_copy.key == key)) {
            ReleaseAll();
            g_copy.key    = key;
            g_copy.failed = true;
            std::fprintf(stderr, "[SceneDepth] bound depth format %d is not a D24S8 / D32 family: no copy\n",
                         static_cast<int>(desc.Format));
        }
        depth->Release();
        return;
    }
    if (!(g_copy.key == key)) {
        if (g_copy.key.device != nullptr && g_copy.key.device != dev)
            std::fprintf(stderr, "[SceneDepth] device changed: the copy is remade\n");
        if (g_copy.image == nullptr)
            g_copy.image = static_cast<::Video::ImageBuffer*>(::Video::ImageBuffer::CreateNewObject());
        Make(dev, key);
    }
    if (g_copy.failed || g_copy.image == nullptr) {
        depth->Release();
        return;
    }

    dc->CopyResource(g_copy.texture, depth);
    depth->Release();

    ID3D11ShaderResourceView* exposed = g_copy.view;
    if (key.samples > 1) {
        ResolveStateBackup saved(dc);
        exposed = g_copy.resolve.Run(dc, g_copy.view, key.samples);
    }
    Expose(exposed);
    g_copy.image->_width  = static_cast<int32_t>(key.width);
    g_copy.image->_height = static_cast<int32_t>(key.height);
    g_copy.ready = true;
}

}  // namespace

namespace SceneDepthBackend {

bool Supported() {
    ID3D11Device* const d = LiveDevice();
    // The MSAA resolve's shaders are vs_5_0 / ps_5_0 (tools/ao_shaders.py), as AO's.
    return d != nullptr && d->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
}

void Request(bool on) {
    const bool was = g_requested.exchange(on);
    if (on || !was) return;
    ::Video::Device* const dev = ::Video::Device::_singleton;
    if (dev == nullptr) return;
    dev->Lock();
    ReleaseAll();
    dev->Unlock();
    std::fprintf(stderr, "[SceneDepth] copy released (not requested)\n");
}

void Copy(void* /*context: the depth is the one bound on the immediate context*/) {
    if (!g_requested.load() || !Supported()) return;
    ::Video::Device* const dev = ::Video::Device::_singleton;
    dev->Lock();
    CopyLocked();
    dev->Unlock();
}

const Video::ImageBuffer* Image() {
    if (!g_requested.load() || !g_copy.ready || g_copy.image == nullptr) return nullptr;
    // A device restart since the last Copy(): the copy belongs to the old device until the next one.
    if (g_copy.key.device != LiveDevice()) return nullptr;
    return g_copy.image;
}

}  // namespace SceneDepthBackend
