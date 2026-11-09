#pragma once
// ============================================================================
// src/render/dx11/tweak_overlay_dx11.h -- the debug tweak overlay's DX11 renderer (NOT RETAIL)
// ============================================================================
// Debug flavour only (BANISHED_TWEAK_OVERLAY; src/diag/tweak_overlay.h). Dear ImGui's own DX11
// backend (third_party/imgui/backends/imgui_impl_dx11) behind Diag::TweakOverlay::Renderer.
// ============================================================================
#if BANISHED_TWEAK_OVERLAY

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

namespace Dx11TweakOverlay {

// The presenting thread, the device locked, right before Present: the overlay over the back buffer.
// One atomic test while the overlay is closed.
void BeforePresent(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain);

// DeviceData::Shutdown (main thread; the render thread idle): the renderer's objects and its device
// references go before the device does.
void OnDeviceShutdown();

}  // namespace Dx11TweakOverlay

#endif  // BANISHED_TWEAK_OVERLAY
