// ============================================================================
// src/render/dx11/dof/dof_pass.cpp -- ext.depth_of_field's depth blur on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/dof/dof_backend.h (docs/extensions/DOF.md section 3.1).
//
// THE PASS, at the scene-draw seam with the main colour and depth bound:
//   the depth     THE SHARED DEPTH COPY (depth_copy/depth_copy.h, Source::Bound): the one an earlier
//                 reader made this frame when nothing has written the depth since, else made here;
//   every binding saved (ao/ao_state_backup.h);
//   the colour    the bound colour's texture copied whole into a 1x twin of ours (ResolveSubresource from
//                 a multisampled one: the frame so far, resolved);
//   prefilter     copy + depth -> half A        (dof.hlsl, as every pass below)
//   gather        A -> half B
//   tent          B -> half A
//   composite     A + depth -> the main colour, blend SRC_ALPHA / INV_SRC_ALPHA, RGB only (the alpha holds
//                 the shore depth the water reads), no depth bound;
//   every binding put back, so the engine's cache of what it bound stays true.
// The half targets are R8G8B8A8 (the Vulkan pass's BGRA8: the CoC rides in an 8-bit alpha on both).
//
// LIFETIME. The programs and states per D3D device; the targets per (device, size, format). A new device
// remakes them (a resize, an Options Apply, a device restart: the old objects are released here, as the
// cloud shadow does); Request(false) releases the targets. One [DepthOfField] stderr line per (re)make,
// none per frame.
// ============================================================================
#include "dof/dof_backend.h"

#include "ao/ao_state_backup.h"
#include "ao/shaders/VSFullscreen.h"          // src/render/common: AO's full-screen triangle
#include "depth_copy/depth_copy.h"
#include "dof/dof_composite_ps.h"             // src/render/common: g_dof_PS* (tools/water_shaders.py)
#include "dof/dof_gather_ps.h"
#include "dof/dof_prefilter_ps.h"
#include "dof/dof_tent_ps.h"
#include "dx11_backend_access.h"
#include "video/Context.h"
#include "video/Device.h"

#include <atomic>
#include <cstdio>
#include <cstring>

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

enum Pass : int {
    kPrefilter,
    kGather,
    kTent,
    kComposite,
    kPassCount
};

// ---- per device: the programs and their states -----------------------------------------------------

struct Program {
    ID3D11Device*            device = nullptr;   // not AddRef'd: compared only
    bool                     failed = false;
    ID3D11VertexShader*      vs = nullptr;
    ID3D11PixelShader*       ps[kPassCount] = {};
    ID3D11Buffer*            cb = nullptr;
    ID3D11BlendState*        opaque = nullptr;    // the half passes: write everything
    ID3D11BlendState*        over = nullptr;      // the composite: alpha over, RGB only
    ID3D11DepthStencilState* dss = nullptr;
    ID3D11RasterizerState*   rs = nullptr;
    ID3D11SamplerState*      ss = nullptr;

    void Release() {
        Rel(ss);
        Rel(rs);
        Rel(dss);
        Rel(over);
        Rel(opaque);
        Rel(cb);
        for (ID3D11PixelShader*& p : ps)
            Rel(p);
        Rel(vs);
        failed = false;
        device = nullptr;
    }

    bool EnsureFor(ID3D11Device* dev) {
        if (dev == device) return !failed;
        if (device != nullptr) std::fprintf(stderr, "[DepthOfField] device changed: the pass is remade\n");
        Release();
        device = dev;
        HRESULT hr = dev->CreateVertexShader(g_ao_VSFullscreen, sizeof(g_ao_VSFullscreen), nullptr, &vs);
        if (hr == S_OK) hr = dev->CreatePixelShader(g_dof_PSPrefilter, sizeof(g_dof_PSPrefilter), nullptr, &ps[kPrefilter]);
        if (hr == S_OK) hr = dev->CreatePixelShader(g_dof_PSGather, sizeof(g_dof_PSGather), nullptr, &ps[kGather]);
        if (hr == S_OK) hr = dev->CreatePixelShader(g_dof_PSTent, sizeof(g_dof_PSTent), nullptr, &ps[kTent]);
        if (hr == S_OK) hr = dev->CreatePixelShader(g_dof_PSComposite, sizeof(g_dof_PSComposite), nullptr, &ps[kComposite]);
        if (hr == S_OK) {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth      = sizeof(DofBackend::Frame);
            bd.Usage          = D3D11_USAGE_DYNAMIC;
            bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            hr = dev->CreateBuffer(&bd, nullptr, &cb);
        }
        if (hr == S_OK) {
            D3D11_BLEND_DESC bd = {};
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            hr = dev->CreateBlendState(&bd, &opaque);
        }
        if (hr == S_OK) {
            // colour = blurred * a + colour * (1 - a)
            D3D11_BLEND_DESC bd = {};
            D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
            rt.BlendEnable           = TRUE;
            rt.SrcBlend              = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOp               = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha         = D3D11_BLEND_ZERO;
            rt.DestBlendAlpha        = D3D11_BLEND_ONE;
            rt.BlendOpAlpha          = D3D11_BLEND_OP_ADD;
            // RGB only: the alpha holds the shore depth the water reads from the frame copy.
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN |
                                       D3D11_COLOR_WRITE_ENABLE_BLUE;
            hr = dev->CreateBlendState(&bd, &over);
        }
        if (hr == S_OK) {
            D3D11_DEPTH_STENCIL_DESC dd = {};
            dd.DepthEnable    = FALSE;
            dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            dd.DepthFunc      = D3D11_COMPARISON_ALWAYS;
            hr = dev->CreateDepthStencilState(&dd, &dss);
        }
        if (hr == S_OK) {
            D3D11_RASTERIZER_DESC rd = {};
            rd.FillMode        = D3D11_FILL_SOLID;
            rd.CullMode        = D3D11_CULL_NONE;
            rd.DepthClipEnable = TRUE;
            hr = dev->CreateRasterizerState(&rd, &rs);
        }
        if (hr == S_OK) {
            D3D11_SAMPLER_DESC sd = {};
            sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sd.MaxLOD         = D3D11_FLOAT32_MAX;
            hr = dev->CreateSamplerState(&sd, &ss);
        }
        std::fprintf(stderr, "[DepthOfField] pass made: 0x%08lx\n", static_cast<unsigned long>(hr));
        if (hr != S_OK) {
            ID3D11Device* const keep = device;
            Release();
            device = keep;   // failed for this device: wait for it to change
            failed = true;
            return false;
        }
        return true;
    }
};
Program g_program;

// ---- the targets: the colour copy and the two half-resolution images ---------------------------------

struct Target {
    ID3D11Texture2D*          tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11RenderTargetView*   rtv = nullptr;

    void Release() {
        Rel(rtv);
        Rel(srv);
        Rel(tex);
    }

    bool Make(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT texFormat, DXGI_FORMAT viewFormat, bool renderTarget) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width            = w;
        td.Height           = h;
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = texFormat;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_DEFAULT;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
        if (renderTarget) td.BindFlags |= D3D11_BIND_RENDER_TARGET;
        HRESULT hr = dev->CreateTexture2D(&td, nullptr, &tex);
        if (hr == S_OK) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format              = viewFormat;
            sd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = 1;
            hr = dev->CreateShaderResourceView(tex, &sd, &srv);
        }
        if (hr == S_OK && renderTarget) hr = dev->CreateRenderTargetView(tex, nullptr, &rtv);
        if (hr != S_OK) Release();
        return hr == S_OK;
    }
};

struct Targets {
    ID3D11Device* device = nullptr;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;   // the main colour texture's
    bool failed = false;
    Target copy;
    Target half[2];

    void Release() {
        copy.Release();
        half[0].Release();
        half[1].Release();
        device = nullptr;
        width = 0;
        height = 0;
        format = DXGI_FORMAT_UNKNOWN;
        failed = false;
    }

    bool EnsureFor(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT texFormat, DXGI_FORMAT viewFormat) {
        if (dev == device && w == width && h == height && texFormat == format) return !failed;
        Release();
        device = dev;
        width = w;
        height = h;
        format = texFormat;
        const UINT hw = (w + 1) / 2;
        const UINT hh = (h + 1) / 2;
        const bool ok = copy.Make(dev, w, h, texFormat, viewFormat, false) &&
                        half[0].Make(dev, hw, hh, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, true) &&
                        half[1].Make(dev, hw, hh, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        std::fprintf(stderr, "[DepthOfField] targets %ux%u (half %ux%u, colour format %d): %s\n", w, h, hw, hh,
                     static_cast<int>(texFormat), ok ? "made" : "FAILED, no depth blur");
        if (!ok) {
            copy.Release();
            half[0].Release();
            half[1].Release();
            failed = true;
        }
        return ok;
    }
};
Targets g_targets;

std::atomic<bool> g_requested{false};

void Fullscreen(ID3D11DeviceContext* dc, Pass pass, ID3D11RenderTargetView* rtv, UINT w, UINT h,
                ID3D11ShaderResourceView* t0, ID3D11ShaderResourceView* t1) {
    // Unbind the inputs first: a target of this pass may be the previous pass's input.
    ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
    dc->PSSetShaderResources(0, 2, none);
    dc->OMSetRenderTargets(1, &rtv, nullptr);
    const D3D11_VIEWPORT vp = {0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
    dc->RSSetViewports(1, &vp);
    dc->OMSetBlendState(pass == kComposite ? g_program.over : g_program.opaque, nullptr, 0xffffffffu);
    dc->PSSetShader(g_program.ps[pass], nullptr, 0);
    ID3D11ShaderResourceView* srvs[2] = {t0, t1};
    dc->PSSetShaderResources(0, 2, srvs);
    dc->Draw(3, 0);
}

}  // namespace

namespace DofBackend {
BANISHED_RENDER_IMPL_BEGIN

bool Supported() {
    ID3D11Device* const d = LiveDevice();
    // AO's full-screen vertex program is vs_5_0 (tools/ao_shaders.py).
    return d != nullptr && d->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
}

// The shared depth copy's reader; off releases the targets.
void Request(bool on) {
    if (g_requested.exchange(on) == on) return;
    VideoDX11::DepthCopy::Request(VideoDX11::DepthCopy::kReaderDof, on);
    if (!on) {
        // Under the device lock: the drawing thread may be inside Draw.
        ::Video::Device* const dev = ::Video::Device::_singleton;
        if (dev == nullptr) return;
        dev->Lock();
        const bool had = g_targets.device != nullptr;
        g_targets.Release();
        dev->Unlock();
        if (had) std::fprintf(stderr, "[DepthOfField] targets released\n");
    }
}

void Draw(Video::Context& context, const Frame& frame) {
    if (!g_requested.load() || !Supported()) return;
    ID3D11Device* const dev = VideoDX11::Dx11Device();
    ID3D11DeviceContext* const dc = VideoDX11::Dx11Context();
    if (!g_program.EnsureFor(dev)) return;

    // Every binding the copy touches is put back by it: the main colour and depth are bound again.
    ID3D11ShaderResourceView* const depthSrv =
        VideoDX11::DepthCopy::Acquire(context, VideoDX11::DepthCopy::Source::Bound);
    if (depthSrv == nullptr) return;

    AoStage::StateBackup saved(dc);
    ID3D11RenderTargetView* const mainRtv = saved.Colour();
    if (mainRtv == nullptr) return;
    ID3D11Resource* mainResource = nullptr;
    mainRtv->GetResource(&mainResource);
    if (mainResource == nullptr) return;
    ID3D11Texture2D* mainTexture = nullptr;
    mainResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&mainTexture));
    mainResource->Release();
    if (mainTexture == nullptr) return;
    D3D11_TEXTURE2D_DESC desc;
    mainTexture->GetDesc(&desc);
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
    mainRtv->GetDesc(&rtvDesc);
    if (!g_targets.EnsureFor(dev, desc.Width, desc.Height, desc.Format, rtvDesc.Format)) {
        mainTexture->Release();
        return;
    }
    if (desc.SampleDesc.Count > 1)
        dc->ResolveSubresource(g_targets.copy.tex, 0, mainTexture, 0, rtvDesc.Format);
    else
        dc->CopyResource(g_targets.copy.tex, mainTexture);
    mainTexture->Release();

    const UINT hw = (desc.Width + 1) / 2;
    const UINT hh = (desc.Height + 1) / 2;
    Frame f = frame;
    f.screen[0] = 1.0f / static_cast<float>(desc.Width);
    f.screen[1] = 1.0f / static_cast<float>(desc.Height);
    f.screen[2] = static_cast<float>(desc.Width);
    f.screen[3] = static_cast<float>(desc.Height);
    f.halfRes[0] = 1.0f / static_cast<float>(hw);
    f.halfRes[1] = 1.0f / static_cast<float>(hh);
    f.halfRes[2] = static_cast<float>(hw);
    f.halfRes[3] = static_cast<float>(hh);
    f.lens[2] = MaxCocFor(frame.lens[2], static_cast<int>(desc.Height));
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (dc->Map(g_program.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped) != S_OK) return;
    std::memcpy(mapped.pData, &f, sizeof(f));
    dc->Unmap(g_program.cb, 0);

    dc->RSSetState(g_program.rs);
    dc->OMSetDepthStencilState(g_program.dss, 0);
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(g_program.vs, nullptr, 0);
    dc->PSSetConstantBuffers(0, 1, &g_program.cb);
    dc->PSSetSamplers(0, 1, &g_program.ss);

    Target* const a = &g_targets.half[0];
    Target* const b = &g_targets.half[1];
    Fullscreen(dc, kPrefilter, a->rtv, hw, hh, g_targets.copy.srv, depthSrv);
    Fullscreen(dc, kGather, b->rtv, hw, hh, a->srv, nullptr);
    Fullscreen(dc, kTent, a->rtv, hw, hh, b->srv, nullptr);
    Fullscreen(dc, kComposite, mainRtv, desc.Width, desc.Height, a->srv, depthSrv);
}

BANISHED_RENDER_IMPL_END
}  // namespace DofBackend
