// ============================================================================
// src/render/dx11/swap_probe_dx11.cpp -- the DX11 side of Render::DumpSwapChainState /
// Render::BootedExclusiveFullscreen (src/render/common/render_backend.h)
// ============================================================================
// NOT RETAIL (diagnostics). These two bodies lived in src/app/main.cpp and read the DX11
// DeviceData's swap chain by name (VideoDX11::Dx11SwapChain); they moved here when the
// Vulkan backend arrived so that main.cpp names no backend namespace. The text they print
// is unchanged: the [SwapProbe] lines are read by hand when the fullscreen-on-focus-loss
// bug (DXGI silently dropping exclusive fullscreen) is being chased.
//
// The probe reads IDXGISwapChain at Device::_singleton->deviceData (+0x100), the active
// DeviceData's swap chain pointer (populated by Dx11 CreateDevice, retail
// VideoDX11!FUN_180004e10, during Engine::Startup), then calls GetFullscreenState
// (vtable +0x58) and GetDesc (vtable +0x60). The dx11 backend is Windows-only (Vulkan P5), so
// the POSIX arm these bodies had is gone.
// ============================================================================

#include "render_backend.h"     // src/render/common: the declarations
#include "VideoDX11Plugin.h"    // VideoDX11::Dx11SwapChain
#include "video/Device.h"

#include <cstdio>

#include <windows.h>
#include <dxgi.h>   // IDXGISwapChain / DXGI_SWAP_CHAIN_DESC

// DXGI_SWAP_CHAIN_DESC is 72 bytes (OutputWindow at 48, Windowed at 56), the layout the
// probes' former local copy asserted.
static_assert(sizeof(DXGI_SWAP_CHAIN_DESC) == 72,
              "DXGI_SWAP_CHAIN_DESC layout mismatch (expected 72 bytes)");
static_assert(offsetof(DXGI_SWAP_CHAIN_DESC, OutputWindow) == 48,
              "OutputWindow offset mismatch");
static_assert(offsetof(DXGI_SWAP_CHAIN_DESC, Windowed) == 56,
              "Windowed offset mismatch");

namespace Render {
BANISHED_RENDER_IMPL_BEGIN

void DumpSwapChainState(const char* tag) {
    if (!::Video::Device::_singleton) {
        printf("[SwapProbe %s] device singleton not populated\n", tag);
        return;
    }
    ::Video::Device* device = ::Video::Device::_singleton;
    ::Video::DeviceData* activeDD = device->deviceData;   // +0x100
    if (!activeDD) {
        printf("[SwapProbe %s] activeDeviceData NULL (Device+0x100)\n", tag);
        return;
    }
    // B5 DEV-A (B5_RULES D7): the swap chain is a field of the DX11 DeviceData
    // subclass (+0x18), not of Video::DeviceData; dx11 hands it out by name.
    IDXGISwapChain* swapChain = VideoDX11::Dx11SwapChain();
    if (!swapChain) {
        printf("[SwapProbe %s] swapChain NULL (activeDD+0x18)\n", tag);
        return;
    }
    [[maybe_unused]] void* vt = *reinterpret_cast<void**>(swapChain);   // the vtable word, logged only  // CAST-OK: crash_walk

    BOOL fullscreen = -1;
    IDXGIOutput* output = nullptr;
    long hr1 = swapChain->GetFullscreenState(&fullscreen, &output);   // vtable +0x58

    DXGI_SWAP_CHAIN_DESC desc{};
    long hr2 = swapChain->GetDesc(&desc);                              // vtable +0x60

    HWND hwnd = desc.OutputWindow;
    RECT cr{};
    RECT wr{};
    if (hwnd) {
        GetClientRect(hwnd, &cr);
        GetWindowRect(hwnd, &wr);
    }
    BOOL visible = hwnd ? IsWindowVisible(hwnd) : FALSE;
    BOOL minimized = hwnd ? IsIconic(hwnd) : FALSE;

    printf("[SwapProbe %s] sc=%p vt=%p\n", tag, static_cast<void*>(swapChain), vt);
    printf("[SwapProbe %s]   GetFullscreenState hr=0x%08lX fs=%d output=%p\n",
           tag, hr1, fullscreen, static_cast<void*>(output));
    printf("[SwapProbe %s]   GetDesc hr=0x%08lX Windowed=%d %ux%u fmt=%u "
           "BufferCount=%u SwapEffect=%u Flags=0x%X\n",
           tag, hr2, desc.Windowed, desc.BufferDesc.Width, desc.BufferDesc.Height,
           desc.BufferDesc.Format, desc.BufferCount, desc.SwapEffect, desc.Flags);
    printf("[SwapProbe %s]   hwnd=%p visible=%d iconic=%d "
           "client=%ldx%ld window=%ld,%ld %ldx%ld\n",
           tag, static_cast<void*>(hwnd), visible, minimized,
           cr.right - cr.left, cr.bottom - cr.top,
           wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top);
    fflush(stdout);
}

// DXGI's SetFullscreenState is the exclusive mode; a windowed swap chain in a popup is the
// borderless one (ext.fullscreen_mode, src/render/dx11/devicedata.cpp).
bool SupportsExclusiveFullscreen() {
    return true;
}

bool BootedExclusiveFullscreen() {
    if (!::Video::Device::_singleton) return false;
    ::Video::Device* device = ::Video::Device::_singleton;
    ::Video::DeviceData* activeDD = device->deviceData;   // +0x100
    if (!activeDD) return false;
    // B5 DEV-A (B5_RULES D7): the DX11 DeviceData's swap chain (+0x18), by name.
    IDXGISwapChain* swapChain = VideoDX11::Dx11SwapChain();
    if (!swapChain) return false;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (swapChain->GetDesc(&desc) != 0) return false;                  // vtable +0x60
    return desc.Windowed == 0;
}

BANISHED_RENDER_IMPL_END
}  // namespace Render
