// ============================================================================
// src/render/dx11/fancy_terrain/terrain_light.cpp -- ext.fancy_terrain's terrain light on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/fancy_terrain/terrain_light_backend.h; the pass is the
// cloud shadow's shape (better_clouds/cloud_shadow.cpp):
//   the uploads: the horizon planes and the ground height as three DEFAULT textures, all of them when they
//     are new (a new device, a new generation), else the dirty rectangle (UpdateSubresource with a box);
//   the bound depth's copy: THE SHARED DEPTH COPY (depth_copy/depth_copy.h);
//   every binding saved (ao/ao_state_backup.h);
//   AO's full-screen triangle with our program (terrain_light.hlsl), blend ZERO / SRC_COLOR, RGB only;
//   every binding put back, so the engine's cache of what it bound stays true.
//
// LIFETIME. The shaders, states and textures per D3D device (a new device remakes them and uploads
// everything again). One [FancyTerrain] stderr line per (re)make,
// none per frame.
// ============================================================================
#include "fancy_terrain/terrain_light_backend.h"

#include "ao/ao_state_backup.h"
#include "ao/shaders/VSFullscreen.h"              // src/render/common: AO's full-screen triangle
#include "fancy_terrain/terrain_light_ps.h"       // src/render/common: g_ft_PSTerrainLight (tools/water_shaders.py)
#include "depth_copy/depth_copy.h"
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

// One map texture and its view.
struct MapTexture {
    ID3D11Texture2D*          texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;

    void Release() {
        Rel(view);
        Rel(texture);
    }
    bool Make(ID3D11Device* dev, int w, int h, DXGI_FORMAT format) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width            = static_cast<UINT>(w);
        td.Height           = static_cast<UINT>(h);
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = format;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_DEFAULT;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
        if (dev->CreateTexture2D(&td, nullptr, &texture) != S_OK) return false;
        return dev->CreateShaderResourceView(texture, nullptr, &view) == S_OK;
    }
};

// ---- per device: the program, its states and the map textures ------------------------------------

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
    MapTexture               horizon0;
    MapTexture               horizon1;
    MapTexture               ground;
    int                      width = 0;          // the textures' size; 0 = none
    int                      height = 0;
    uint32_t                 generation = 0;

    void ReleaseTextures() {
        ground.Release();
        horizon1.Release();
        horizon0.Release();
        width = 0;
        height = 0;
        generation = 0;
    }

    void Release() {
        ReleaseTextures();
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
        if (device != nullptr) std::fprintf(stderr, "[FancyTerrain] device changed: the terrain light is remade\n");
        Release();
        device = dev;
        HRESULT hr = dev->CreateVertexShader(g_ao_VSFullscreen, sizeof(g_ao_VSFullscreen), nullptr, &vs);
        if (hr == S_OK) hr = dev->CreatePixelShader(g_ft_PSTerrainLight, sizeof(g_ft_PSTerrainLight), nullptr, &ps);
        if (hr == S_OK) {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth      = sizeof(TerrainLightBackend::Frame);
            bd.Usage          = D3D11_USAGE_DYNAMIC;
            bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            hr = dev->CreateBuffer(&bd, nullptr, &cb);
        }
        if (hr == S_OK) {
            // colour = colour * light
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
            sd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sd.MaxLOD         = D3D11_FLOAT32_MAX;
            hr = dev->CreateSamplerState(&sd, &ss);
        }
        std::fprintf(stderr, "[FancyTerrain] terrain light made: 0x%08lx\n", static_cast<unsigned long>(hr));
        if (hr != S_OK) {
            ID3D11Device* const keep = device;
            Release();
            device = keep;   // failed for this device: wait for it to change
            failed = true;
            return false;
        }
        return true;
    }

    // The CPU data into the textures: everything when they are new, else the dirty rectangle.
    bool Upload(ID3D11DeviceContext* dc, const TerrainLightBackend::Textures& t) {
        if (t.width <= 0 || t.height <= 0 || t.horizon0 == nullptr || t.horizon1 == nullptr || t.ground == nullptr)
            return false;
        bool all = false;
        if (width != t.width || height != t.height || horizon0.view == nullptr) {
            ReleaseTextures();
            if (!horizon0.Make(device, t.width, t.height, DXGI_FORMAT_R8G8B8A8_UNORM) ||
                !horizon1.Make(device, t.width, t.height, DXGI_FORMAT_R8G8B8A8_UNORM) ||
                !ground.Make(device, t.width, t.height, DXGI_FORMAT_R16_FLOAT)) {
                std::fprintf(stderr, "[FancyTerrain] map textures %dx%d: not made\n", t.width, t.height);
                ReleaseTextures();
                return false;
            }
            width = t.width;
            height = t.height;
            all = true;
            std::fprintf(stderr, "[FancyTerrain] map textures %dx%d made\n", t.width, t.height);
        }
        if (generation != t.generation) all = true;
        int x0 = t.dirtyX0;
        int y0 = t.dirtyY0;
        int x1 = t.dirtyX1;
        int y1 = t.dirtyY1;
        if (all) {
            x0 = 0;
            y0 = 0;
            x1 = t.width;
            y1 = t.height;
        }
        if (x1 > x0 && y1 > y0) {
            D3D11_BOX box = {};
            box.left   = static_cast<UINT>(x0);
            box.top    = static_cast<UINT>(y0);
            box.right  = static_cast<UINT>(x1);
            box.bottom = static_cast<UINT>(y1);
            box.back   = 1;
            const size_t row4 = static_cast<size_t>(t.width) * 4;
            const size_t row2 = static_cast<size_t>(t.width) * 2;
            const size_t at = static_cast<size_t>(y0) * t.width + x0;
            dc->UpdateSubresource(horizon0.texture, 0, &box, t.horizon0 + at * 4, static_cast<UINT>(row4), 0);
            dc->UpdateSubresource(horizon1.texture, 0, &box, t.horizon1 + at * 4, static_cast<UINT>(row4), 0);
            dc->UpdateSubresource(ground.texture, 0, &box, t.ground + at, static_cast<UINT>(row2), 0);
        }
        generation = t.generation;
        return true;
    }
};
Program g_program;
std::atomic<bool> g_requested{false};

}  // namespace

namespace TerrainLightBackend {
BANISHED_RENDER_IMPL_BEGIN

bool Supported() {
    ID3D11Device* const d = LiveDevice();
    // AO's full-screen shader is vs_5_0 (tools/ao_shaders.py).
    return d != nullptr && d->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
}

// The shared depth copy's reader. The map textures stay until the device changes: the main thread asks
// here while the render thread (ext.render_pipe) may still be drawing with them.
void Request(bool on) {
    if (g_requested.exchange(on) == on) return;
    VideoDX11::DepthCopy::Request(VideoDX11::DepthCopy::kReaderTerrain, on);
}

void Draw(Video::Context& context, const Textures& textures, const Frame& frame) {
    if (!g_requested.load() || !Supported()) return;
    ID3D11Device* const dev = VideoDX11::Dx11Device();
    ID3D11DeviceContext* const dc = VideoDX11::Dx11Context();
    if (!g_program.EnsureFor(dev)) return;
    if (!g_program.Upload(dc, textures)) return;

    // Every binding the copy touches is put back by it: the main colour and depth are bound again.
    ID3D11ShaderResourceView* const depthSrv =
        VideoDX11::DepthCopy::Acquire(context, VideoDX11::DepthCopy::Source::Bound);
    if (depthSrv == nullptr) return;
    ID3D11Resource* copied = nullptr;
    depthSrv->GetResource(&copied);
    if (copied == nullptr) return;
    ID3D11Texture2D* copiedTexture = nullptr;
    copied->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&copiedTexture));  // CAST-OK: com_iid
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
    ID3D11ShaderResourceView* srvs[4] = {depthSrv, g_program.horizon0.view, g_program.horizon1.view,
                                         g_program.ground.view};
    dc->PSSetShaderResources(0, 4, srvs);
    dc->PSSetSamplers(1, 1, &g_program.ss);
    dc->Draw(3, 0);
}

BANISHED_RENDER_IMPL_END
}  // namespace TerrainLightBackend
