// ============================================================================
// src/render/dx11/map_edge/map_edge_pass.cpp -- ext.map_edge_fade's rim fade on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/map_edge/map_edge_backend.h
// (docs/extensions/handins/MAPEDGE.md).
//
// THE PASS, at the scene-draw seam with the main colour and depth bound:
//   the bound depth's copy: THE SHARED DEPTH COPY (depth_copy/depth_copy.h), taken as it is when nothing
//     has written the depth since it was made, else made here;
//   every binding saved (ao/ao_state_backup.h);
//   the colour and depth still bound (the depth untouched: our depth state tests and writes nothing);
//   AO's full-screen triangle with our program (map_edge.hlsl), blend ZERO / SRC_COLOR: the fade
//     multiplies into the colour;
//   every binding put back, so the engine's cache of what it bound stays true.
//
// LIFETIME. The shaders and states per D3D device (the depth copy is depth_copy.cpp's). A new device
// remakes them (a window resize, an Options Apply, a device restart: DeviceData::CreateDevice makes a new
// ID3D11Device; the old objects are released here, as the cloud shadow pass does). One [MapEdgeFade]
// stderr line per (re)make, none per frame.
// ============================================================================
#include "map_edge/map_edge_backend.h"

#include "ao/ao_state_backup.h"
#include "ao/shaders/VSFullscreen.h"     // src/render/common: AO's full-screen triangle
#include "map_edge/map_edge_ps.h"        // src/render/common: g_me_PSMapEdge (tools/water_shaders.py)
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

    void Release() {
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
        if (device != nullptr) std::fprintf(stderr, "[MapEdgeFade] device changed: the pass is remade\n");
        Release();
        device = dev;
        HRESULT hr = dev->CreateVertexShader(g_ao_VSFullscreen, sizeof(g_ao_VSFullscreen), nullptr, &vs);
        if (hr == S_OK) hr = dev->CreatePixelShader(g_me_PSMapEdge, sizeof(g_me_PSMapEdge), nullptr, &ps);
        if (hr == S_OK) {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth      = sizeof(MapEdgeBackend::Frame);
            bd.Usage          = D3D11_USAGE_DYNAMIC;
            bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            hr = dev->CreateBuffer(&bd, nullptr, &cb);
        }
        if (hr == S_OK) {
            // colour = colour * fade
            D3D11_BLEND_DESC bd = {};
            D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
            rt.BlendEnable           = TRUE;
            rt.SrcBlend              = D3D11_BLEND_ZERO;
            rt.DestBlend             = D3D11_BLEND_SRC_COLOR;
            rt.BlendOp               = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha         = D3D11_BLEND_ZERO;
            rt.DestBlendAlpha        = D3D11_BLEND_ONE;
            rt.BlendOpAlpha          = D3D11_BLEND_OP_ADD;
            // RGB only, as the other full-screen multiplies write.
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
        std::fprintf(stderr, "[MapEdgeFade] pass made: 0x%08lx\n", static_cast<unsigned long>(hr));
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

std::atomic<bool> g_requested{false};

}  // namespace

namespace MapEdgeBackend {
BANISHED_RENDER_IMPL_BEGIN

bool Supported() {
    ID3D11Device* const d = LiveDevice();
    // AO's full-screen vertex program is vs_5_0 (tools/ao_shaders.py).
    return d != nullptr && d->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
}

// The shared depth copy's reader.
void Request(bool on) {
    if (g_requested.exchange(on) == on) return;
    VideoDX11::DepthCopy::Request(VideoDX11::DepthCopy::kReaderMapEdge, on);
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
    dc->PSSetShaderResources(0, 1, &depthSrv);
    dc->Draw(3, 0);
}

BANISHED_RENDER_IMPL_END
}  // namespace MapEdgeBackend
