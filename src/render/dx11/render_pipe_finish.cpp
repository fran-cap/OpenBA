// ============================================================================
// src/render/dx11/render_pipe_finish.cpp -- RENDERPIPE-1: ContextData::Finish
// cut in two (ext.render_pipe's render thread presents, the main thread recovers)
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/extensions/handins/RENDERPIPE1.md section 4;
// the declarations and the why: src/ext/render_pipe/finish_split.h).
//
// A COPY of `ContextData::Finish` (contextdata.cpp, retail 0x1800017c0), cut at
// the `onLost` read. Every statement is the faithful body's, in its order; the
// faithful Finish is untouched and finishes every frame the extension does not
// emit on its thread. The one difference is the BANISHED_FORCE_DEVICE_LOST
// drive: the faithful body's counter is file-local to contextdata.cpp, so this
// copy keeps its own (it fires on the Nth PACKET Present -- the render thread's
// -- and only once; with the row on from boot, the faithful counter, which then
// counts only the sequential frames, stays far below a run's N). This file
// lives in src/render/dx11 because only the dx11 TUs see the D3D headers (MinGW-w64's).
// ============================================================================
#include "ext/render_pipe/finish_split.h"

#include "options.h"
#include "clamped_atoi.h"     // review F-087: atoi on BANISHED_* text
#include "device_lost_drive.h"   // BANISHED_FORCE_DEVICE_LOST=bgsave (src/diag)
#include "dx11_backend_access.h"
#include "async_gpu_create/async_create.h"   // NOT RETAIL: ext.async_gpu_create
#include "system/Error.h"
#include "video/Context.h"
#include "video/CompanionData.h"
#include "video/Device.h"
#if BANISHED_TWEAK_OVERLAY
#include "tweak_overlay.h"   // the debug tweak overlay (debug flavour only, src/diag)
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace Ext {
namespace RenderPipe {
namespace {

// contextdata.cpp's ForceDeviceLostThisFrame, with this file's own count (see the banner).
bool ForceDeviceLostThisPacket() {
    // A DeviceLostDrive::Wanted mode, or > 0: the packet Present # to inject on.
    static int  wanted = static_cast<int>(DeviceLostDrive::Wanted::NotRead);
    static long frame  = 0;
    static bool fired  = false;
    if (wanted == static_cast<int>(DeviceLostDrive::Wanted::NotRead)) {
        const char* v = Opt().forceDeviceLost;
        wanted = DeviceLostDrive::Armed() ? static_cast<int>(DeviceLostDrive::Wanted::OnRequest)
                 : (v && *v)               ? ClampedAtoi(v)
                                           : static_cast<int>(DeviceLostDrive::Wanted::Disabled);
        if (wanted > 0)
            std::fprintf(stderr,
                "[DeviceLost] ARMED (render thread): a synthetic DXGI_ERROR_DEVICE_RESET will be "
                "injected on packet Present #%d\n", wanted);
    }
    ++frame;
    // BANISHED_FORCE_DEVICE_LOST=bgsave (src/diag/device_lost_drive.h): once, on request.
    if (wanted == static_cast<int>(DeviceLostDrive::Wanted::OnRequest)) {
        if (fired || !DeviceLostDrive::TakeRequest()) return false;
        fired = true;
        return true;
    }
    if (wanted <= 0 || fired || frame < wanted) return false;
    fired = true;
    return true;
}

}  // namespace

// Finish steps 1-4 (contextdata.cpp 0x1800017c0 .. the `hr == 0` return after
// GetDeviceRemovedReason). Runs on the render thread.
int32_t FinishPresent(Video::Context& ctx) {
    ::Video::Device*        const device = ctx._device;
    VideoDX11::DeviceData*  const dd     = VideoDX11::Dx11DeviceDataOf(device);

    const UINT sync = ::Video::Configuration::_singleton->vsync != 0 ? 1u : 0u;

#if BANISHED_TWEAK_OVERLAY
    // The debug tweak overlay, on the thread that presents (the render thread here).
    Diag::TweakOverlay::BeforePresent(dd->d3dDevice, dd->d3dContext, dd->swapChain);
#endif
    HRESULT hr = dd->swapChain->Present(sync, 0);
    device->Unlock();

    if (ForceDeviceLostThisPacket()) {
        std::fprintf(stderr, "[DeviceLost] INJECTING DXGI_ERROR_DEVICE_RESET "
                             "(real Present returned 0x%08lx; render thread)\n",
                     static_cast<unsigned long>(hr));
        std::fflush(stderr);
        hr = DXGI_ERROR_DEVICE_RESET;
    }

    if (hr == 0) return 0;

    ID3D11Device* const d3dDevice = dd->d3dDevice;
    if (hr != DXGI_ERROR_DEVICE_REMOVED && hr != DXGI_ERROR_DEVICE_RESET)
        hr = d3dDevice->GetDeviceRemovedReason();
    return static_cast<int32_t>(hr);
}

// Finish step 5 (from the `onLost` read to the end). Runs on the MAIN thread,
// the render thread idle.
void FinishRecover(Video::Device& device, int32_t hrIn) {
    const HRESULT hr = static_cast<HRESULT>(hrIn);
    VideoDX11::DeviceData* const dd = VideoDX11::Dx11DeviceDataOf(&device);
    ID3D11Device* const d3dDevice = dd->d3dDevice;

    ::Video::DeviceData* const ddBase = device.deviceData;
    ::Video::Device*     const owner  = ddBase->_owner;   // DeviceData+0x08

    void (*const onLost)() = owner->onVideoError;          // Device+0x50
    std::fprintf(stderr, "[DeviceLost] hr=0x%08lx onLost=%p -> %s (main thread, after the render thread's Present)\n",
                 static_cast<unsigned long>(hr), (void*)onLost,
                 onLost ? "calling" : "*** NULL, notification SKIPPED ***");
    std::fprintf(stderr, "[DeviceLost] removed reason=0x%08lx\n",
                 static_cast<unsigned long>(d3dDevice->GetDeviceRemovedReason()));
    std::fflush(stderr);
    if (onLost) onLost();

    // NOT RETAIL (ext.async_gpu_create): creates queued against the lost device finish first.
    ::VideoDX11::AsyncCreate::Drain();
    owner->ReleaseDynamic();

    dd->d3dDevice = nullptr;

    ddBase->Shutdown();                           // retail slot 3
    const bool created = ddBase->CreateDevice();  // retail slot 2

    if (!created) {
        const char* msg;
        if      (hr == DXGI_ERROR_INVALID_CALL)           msg = "D3D11 Error: DXGI_ERROR_INVALID_CALL!";
        else if (hr == DXGI_ERROR_DEVICE_REMOVED)         msg = "D3D11 Error: DXGI_ERROR_DEVICE_REMOVED!";
        else if (hr == DXGI_ERROR_DEVICE_HUNG)            msg = "D3D11 Error: DXGI_ERROR_DEVICE_HUNG!";
        else if (hr == DXGI_ERROR_DEVICE_RESET)           msg = "D3D11 Error: DXGI_ERROR_DEVICE_RESET!";
        else if (hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR)  msg = "D3D11 Error: DXGI_ERROR_DRIVER_INTERNAL_ERROR!";
        else                                              msg = "D3D11 Error: UNKNOWN!";
        System::Error::SetText(msg, true);
        System::Error::Display();
        return;
    }

    owner->RecreateDynamic();

    void (*const onRestored)() = owner->onVideoRestart;    // Device+0x58
    std::fprintf(stderr, "[DeviceLost] recreate OK; onRestored=%p -> %s\n",
                 (void*)onRestored,
                 onRestored ? "calling" : "*** NULL, notification SKIPPED ***");
    std::fflush(stderr);
    if (onRestored) onRestored();
    std::fprintf(stderr, "[DeviceLost] device-lost round trip COMPLETE\n");
    std::fflush(stderr);
}

}  // namespace RenderPipe
}  // namespace Ext
