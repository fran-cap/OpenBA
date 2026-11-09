#pragma once
// ============================================================================
// src/render/common/render_backend.h -- the backend-neutral renderer entry points
// ============================================================================
// NOT RETAIL. What the code outside a renderer backend calls to plug the renderer
// into Runtime's plugin protocol (`Video::Device::InterfaceSlot`,
// src/engine/video/Device.h): src/app/video_interface.cpp writes these four into
// `Video::Device::dllSlots[kInterfaceDX11]` / `[kInterfaceDX9]`. Each pair has the
// slot's exact types (`Video::Device::GetDeviceNameFn` / `CreateDeviceDataFn`).
//
// One backend defines them, picked by CMake's BANISHED_RENDER_BACKEND
// (src/render/<backend>/backend.cmake): the DX11 backend's are thin forwarders in
// src/render/dx11/render_backend_dx11.cpp to VideoDX11-x64.dll's two exports and the
// cutover-only DX9 placeholder (src/render/dx11/VideoDX11Plugin.h). A second backend
// supplies its own render_backend_<name>.cpp; nothing outside src/render/<backend>/
// names a backend namespace (src/render/common/README.md): the two boot diagnostics
// below are what src/app/main.cpp used to read through VideoDX11::Dx11SwapChain.
//
// In the dual Windows exe (BANISHED_RENDER_BACKEND=dx11+vulkan) each backend defines its own set in
// an inline namespace (render_impl.h) and these names are the dispatchers to the player's pick
// (render_backend_table.h, src/render/common/multi/).
//
// Kept free of every graphics header: the Video types are forward-declared.
// ============================================================================

#include <cstdint>

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)

namespace Video {
class Device;
class DeviceData;
}  // namespace Video

namespace Render {
BANISHED_RENDER_IMPL_BEGIN

// ---- the main interface slot (dllSlots[kInterfaceDX11]) ---------------------------
// The device name shown in the launcher's interface list ("DirectX 11" on DX11).
const char* DeviceName();
// A new backend DeviceData for `device`, returned as its base.
::Video::DeviceData* CreateDeviceData(::Video::Device* device);

// The graphics-API SDL window flag the backend needs when the game window is created
// (SDL_WINDOW_METAL for Metal), as an SDL_WindowFlags value; 0 when the backend needs none.
// The Darwin and Windows window twins ask (src/platform/darwin/window_darwin.cpp,
// src/platform/win/window_win.cpp); the posix twin sets its own (Vulkan on Linux).
uint64_t GraphicsWindowFlags();

// ---- boot diagnostics (src/app/main.cpp) -------------------------------------------
// The swap-chain state probe printed after Engine::Startup and around focus events
// ([SwapProbe <tag>] lines), and whether the device booted in DXGI exclusive fullscreen
// (drives main.cpp's FSE refocus recovery). DX11 reads its IDXGISwapChain
// (src/render/dx11/swap_probe_dx11.cpp); a backend without a DXGI swap chain prints one
// "not probed" line and answers false.
void DumpSwapChainState(const char* tag);
bool BootedExclusiveFullscreen();

// Whether this backend has an exclusive fullscreen (a display mode switch) beside its borderless
// one. DX11 does (DXGI SetFullscreenState); a backend whose fullscreen is always a popup window
// over the monitor (Vulkan and Metal under SDL) answers false. ext.fullscreen_mode offers its
// Off / Exclusive / Borderless combo only where this is true (src/ext/fullscreen_mode/).
bool SupportsExclusiveFullscreen();

// ---- the second slot (dllSlots[kInterfaceDX9]) ------------------------------------
// The cutover-only placeholder that keeps one GraphicsInterface entry per slot
// (B5_RULES D5; src/render/dx11/dx9_placeholder.cpp has the invariant).
const char* PlaceholderDeviceName();
::Video::DeviceData* PlaceholderCreateDeviceData(::Video::Device* device);

BANISHED_RENDER_IMPL_END

// ---- the backends in this exe ----------------------------------------------------------
// One in every build but the dual Windows exe (BANISHED_RENDER_BACKEND=dx11+vulkan), which carries
// two behind the one interface slot above; the entry points above are then the picked backend's
// (src/render/common/multi/render_backend_dispatch.cpp; a single build answers from
// src/render/common/single/render_backend_single.cpp). Indices follow CMake's list, which is also
// the fallback order. ext.render_backend picks one (src/ext/render_backend/).

// How many backends this exe carries (1 except in the dual exe).
int BackendCount();
// The backend's directory name ("dx11", "vulkan", "metal"); nullptr for an index out of range.
const char* BackendId(int index);
// The backend's interface name, as the Options dialog lists it ("DirectX 11", "Vulkan").
const char* BackendName(int index);
// The index of `id` (a BackendId), or -1.
int FindBackend(const char* id);
// The backend the entry points above reach.
int ActiveBackend();
// Whether the backend can start on this machine, without making its device (Vulkan: the loader loads
// and lists an adapter). True for the active backend of a single build.
bool BackendAvailable(int index);
// Makes `index` the active backend. Only while no device exists (Video::Device::deviceData null: at
// boot before the first enumeration, or between Device::Shutdown and Device::Startup): the requests
// the extensions made of the previous backend (the Request / SetFixFlag / SetDrawBloom calls) are
// made again of the new one, so its state matches. False, and nothing changed, for an index out of
// range or while a device exists.
bool SelectBackend(int index);

// True once a backend other than the active one has made a device on the game window this run (only in an
// exe with more than one backend; a one-backend build answers false). A Windows window that has carried a
// Vulkan swap chain no longer shows a blit-model DXGI swap chain presented with a sync interval (the picture
// stays on the last Vulkan frame while Present returns S_OK); the DX11 backend asks this to make a flip-model
// swap chain instead (src/render/dx11/devicedata.cpp, ext.render_backend).
bool WindowPresentedByOtherBackend();

// True once another backend made a device on the game window AFTER the active backend's first device there
// (only in an exe with more than one backend; a one-backend build answers false). Vulkan -> DX11 -> Vulkan:
// the DX11 backend made a flip-model DXGI swap chain on the window (above), and from then on the window
// shows a Vulkan swap chain only in FIFO (NVIDIA, 2026-11-08): MAILBOX and IMMEDIATE presents return
// VK_SUCCESS while the window keeps the last DX11 frame, though the DXGI swap chain is destroyed. A window
// that carried only retail's blit-model chain (DX11 -> Vulkan) shows every mode. The Vulkan backend asks
// this to present in FIFO with VSync off (src/render/vulkan/device.cpp CreateSwapchain).
bool WindowTakenOverByOtherBackend();

}  // namespace Render
