// ============================================================================
// src/render/dx11/tweak_overlay_dx11.cpp -- tweak_overlay_dx11.h (NOT RETAIL; debug flavour only)
// ============================================================================
// The overlay's frame is built by src/diag/win/tweak_overlay.cpp; this file only renders it, with
// ImGui's DX11 backend, into a render-target view of the swap chain's back buffer. The game's own
// render targets are put back afterwards (ImGui_ImplDX11_RenderDrawData restores the rest).
// ============================================================================
#if BANISHED_TWEAK_OVERLAY

#include "tweak_overlay_dx11.h"

#include "tweak_overlay.h"   // src/diag: Diag::TweakOverlay::Renderer

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

namespace Dx11TweakOverlay {

namespace {

class Dx11Renderer final : public Diag::TweakOverlay::Renderer {
public:
    ID3D11Device*           device  = nullptr;   // the device Init registers with (set before each frame)
    ID3D11DeviceContext*    context = nullptr;
    ID3D11RenderTargetView* target  = nullptr;   // this frame's back-buffer view
    ID3D11Device*           initDevice = nullptr;   // the device ImGui_ImplDX11_Init ran on

    const char* Name() const override { return "DX11"; }

    bool Init() override {
        if (!ImGui_ImplDX11_Init(device, context)) return false;
        initDevice = device;
        return true;
    }

    void NewFrame() override { ImGui_ImplDX11_NewFrame(); }

    void Render(ImDrawData* drawData) override {
        ID3D11RenderTargetView* oldRtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        ID3D11DepthStencilView* oldDsv = nullptr;
        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtv, &oldDsv);
        context->OMSetRenderTargets(1, &target, nullptr);
        ImGui_ImplDX11_RenderDrawData(drawData);   // backs up and restores the rest of the state
        context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtv, oldDsv);
        for (ID3D11RenderTargetView*& v : oldRtv)
            if (v) v->Release();
        if (oldDsv) oldDsv->Release();
    }

    void Shutdown() override {
        ImGui_ImplDX11_Shutdown();
        initDevice = nullptr;
    }
};

Dx11Renderer g_renderer;

}  // namespace

void BeforePresent(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain) {
    if (!Diag::TweakOverlay::IsOpen()) return;
    if (device == nullptr || context == nullptr || swapChain == nullptr) return;
    // A new device (device lost, a settings rebuild) without a shutdown seen: register again on it.
    if (g_renderer.initDevice != nullptr && g_renderer.initDevice != device)
        Diag::TweakOverlay::OnRendererShutdown(g_renderer);

    ID3D11Texture2D* back = nullptr;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) ||  // CAST-OK: com_iid
        back == nullptr)
        return;
    D3D11_TEXTURE2D_DESC td;
    back->GetDesc(&td);
    ID3D11RenderTargetView* rtv = nullptr;
    const HRESULT hr = device->CreateRenderTargetView(back, nullptr, &rtv);
    back->Release();
    if (FAILED(hr) || rtv == nullptr) return;

    g_renderer.device  = device;
    g_renderer.context = context;
    g_renderer.target  = rtv;
    Diag::TweakOverlay::BeforePresent(g_renderer, td.Width, td.Height);
    g_renderer.target = nullptr;
    rtv->Release();
}

void OnDeviceShutdown() {
    Diag::TweakOverlay::OnRendererShutdown(g_renderer);
}

}  // namespace Dx11TweakOverlay

#endif  // BANISHED_TWEAK_OVERLAY
