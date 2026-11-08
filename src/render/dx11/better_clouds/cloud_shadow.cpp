// ============================================================================
// src/render/dx11/better_clouds/cloud_shadow.cpp -- ext.better_clouds Fancy's shadow pass on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/better_clouds/cloud_shadow_backend.h
// (docs/extensions/handins/BETTERCLOUDS.md section 2.6).
//
// THE PASS, at the ground-draw seam with the main colour and depth bound:
//   the bound depth's copy: THE SHARED DEPTH COPY (depth_copy/depth_copy.h), the one the AO pass made
//     at the same seam when nothing has written the depth since, else made here (the bound depth
//     copied, or the readable main depth resolved; sample 0 of a multisampled one);
//   every binding saved (ao/ao_state_backup.h);
//   the colour and depth still bound (the depth untouched: our depth state tests and writes nothing);
//   AO's full-screen triangle with our program (cloud_shadow.hlsl), blend ZERO / SRC_COLOR: the
//     shadow multiplies into the colour;
//   every binding put back, so the engine's cache of what it bound stays true.
//
// LIFETIME. The shaders and states per D3D device (the depth copy is depth_copy.cpp's). A new device
// remakes them (a window resize, an Options Apply, a device restart: DeviceData::CreateDevice
// makes a new ID3D11Device; the old objects are released here, as the AO pass does). One
// [BetterClouds] stderr line per (re)make, none per frame.
// ============================================================================
#include "better_clouds/cloud_shadow_backend.h"

#include "ao/ao_state_backup.h"
#include "ao/shaders/VSFullscreen.h"            // src/render/common: AO's full-screen triangle
#include "better_clouds/cloud_shadow_ps.h"      // src/render/common: g_bc_PSCloudShadow (tools/water_shaders.py)
#include "depth_copy/depth_copy.h"
#include "dx11_backend_access.h"
#include "video/Context.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"

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

// ---- per device: the program and its states --------------------------------------------------------

struct Program {
    ID3D11Device*            device = nullptr;   // not AddRef'd: compared only
    bool                     failed = false;
    ID3D11VertexShader*      vs  = nullptr;
    ID3D11PixelShader*       ps  = nullptr;
    ID3D11Buffer*            cb  = nullptr;
    ID3D11BlendState*        bs  = nullptr;
    ID3D11DepthStencilState* dss = nullptr;
    ID3D11RasterizerState*   rs  = nullptr;
    ID3D11SamplerState*      ss  = nullptr;

    void Release() {
        Rel(ss);
        Rel(rs);
        Rel(dss);
        Rel(bs);
        Rel(cb);
        Rel(ps);
        Rel(vs);
        failed = false;
        device = nullptr;
    }

    bool EnsureFor(ID3D11Device* dev) {
        if (dev == device) return !failed;
        if (device != nullptr) std::fprintf(stderr, "[BetterClouds] device changed: the shadow pass is remade\n");
        Release();
        device = dev;
        HRESULT hr = dev->CreateVertexShader(g_ao_VSFullscreen, sizeof(g_ao_VSFullscreen), nullptr, &vs);
        if (hr == S_OK) hr = dev->CreatePixelShader(g_bc_PSCloudShadow, sizeof(g_bc_PSCloudShadow), nullptr, &ps);
        if (hr == S_OK) {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth      = sizeof(CloudShadowBackend::Frame);
            bd.Usage          = D3D11_USAGE_DYNAMIC;
            bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            hr = dev->CreateBuffer(&bd, nullptr, &cb);
        }
        if (hr == S_OK) {
            // colour = colour * shadow
            D3D11_BLEND_DESC bd = {};
            D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
            rt.BlendEnable           = TRUE;
            rt.SrcBlend              = D3D11_BLEND_ZERO;
            rt.DestBlend             = D3D11_BLEND_SRC_COLOR;
            rt.BlendOp               = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha         = D3D11_BLEND_ZERO;
            rt.DestBlendAlpha        = D3D11_BLEND_ONE;
            rt.BlendOpAlpha          = D3D11_BLEND_OP_ADD;
            // RGB only: the alpha holds the shore depth the water reads from the frame copy.
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN |
                                       D3D11_COLOR_WRITE_ENABLE_BLUE;
            hr = dev->CreateBlendState(&bd, &bs);
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
            sd.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
            sd.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
            sd.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
            sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sd.MaxLOD         = D3D11_FLOAT32_MAX;
            hr = dev->CreateSamplerState(&sd, &ss);
        }
        std::fprintf(stderr, "[BetterClouds] shadow pass made: 0x%08lx\n", static_cast<unsigned long>(hr));
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

}  // namespace

namespace CloudShadowBackend {
BANISHED_RENDER_IMPL_BEGIN

bool Supported() {
    ID3D11Device* const d = LiveDevice();
    // AO's full-screen and resolve shaders are vs_5_0 / ps_5_0 (tools/ao_shaders.py).
    return d != nullptr && d->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
}

// The shared depth copy's reader.
void Request(bool on) {
    VideoDX11::DepthCopy::Request(VideoDX11::DepthCopy::kReaderClouds, on);
}

void Draw(Video::Context& context, const Video::ImageBuffer* noise, const Frame& frame) {
    if (noise == nullptr || noise->_data == nullptr || !Supported()) return;
    ID3D11ShaderResourceView* const noiseSrv =
        VideoDX11::Dx11ImageBackend(noise)->shaderResourceView;
    if (noiseSrv == nullptr) return;

    ID3D11Device* const dev = VideoDX11::Dx11Device();
    ID3D11DeviceContext* const dc = VideoDX11::Dx11Context();
    if (!g_program.EnsureFor(dev)) return;

    // Every binding the copy touches is put back by it: the main colour and depth are bound again.
    ID3D11ShaderResourceView* const depthSrv =
        VideoDX11::DepthCopy::Acquire(context, VideoDX11::DepthCopy::Source::Bound);
    if (depthSrv == nullptr) return;
    ID3D11Resource* copied = nullptr;
    depthSrv->GetResource(&copied);
    if (copied == nullptr) return;
    ID3D11Texture2D* copiedTexture = nullptr;
    copied->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&copiedTexture));
    copied->Release();
    if (copiedTexture == nullptr) return;
    D3D11_TEXTURE2D_DESC desc;
    copiedTexture->GetDesc(&desc);
    copiedTexture->Release();

    Frame f = frame;
    f.screen[0] = 1.0f / static_cast<float>(desc.Width);
    f.screen[1] = 1.0f / static_cast<float>(desc.Height);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (dc->Map(g_program.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped) != S_OK) return;
    std::memcpy(mapped.pData, &f, sizeof(f));
    dc->Unmap(g_program.cb, 0);

    AoStage::StateBackup saved(dc);
    if (saved.Colour() == nullptr) return;

    const D3D11_VIEWPORT vp = {0.0f, 0.0f, static_cast<float>(desc.Width), static_cast<float>(desc.Height),
                               0.0f, 1.0f};
    dc->RSSetViewports(1, &vp);
    dc->RSSetState(g_program.rs);
    dc->OMSetDepthStencilState(g_program.dss, 0);
    dc->OMSetBlendState(g_program.bs, nullptr, 0xffffffffu);
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(g_program.vs, nullptr, 0);
    dc->PSSetShader(g_program.ps, nullptr, 0);
    dc->PSSetConstantBuffers(0, 1, &g_program.cb);
    ID3D11ShaderResourceView* srvs[2] = {depthSrv, noiseSrv};
    dc->PSSetShaderResources(0, 2, srvs);
    dc->PSSetSamplers(1, 1, &g_program.ss);
    dc->Draw(3, 0);
}

BANISHED_RENDER_IMPL_END
}  // namespace CloudShadowBackend
