#pragma once
// The renderer plugin's entry points — what Runtime's plugin protocol reaches
// (`Video::Device::InterfaceSlot`, src/engine/video/Device.h) and what
// src/app/video_interface.cpp writes into it.
//
// Retail VideoDX11-x64.dll exports exactly two functions (ordinal 1
// `CreateDeviceData` 0x1800011a0, ordinal 2 `GetDeviceName` 0x180001230); their
// ports are src/render/dx11/exports.cpp. The other pair is the cutover-only DX9
// PLACEHOLDER (src/render/dx11/dx9_placeholder.cpp), which stands in for
// VideoDX9-x64.dll so the Device's GraphicsInterface array keeps one entry per
// slot (B5_RULES D5; the invariant is in that file's banner).
//
// B5 DEV-A: was a local `extern` block in src/app/video_interface.cpp (CLAUDE.md:
// the declaring header, never a local extern). Kept free of <windows.h> /
// <d3d11.h>: everything below is a pointer to a forward-declared class.

namespace Video {
class Device;
class DeviceData;
}  // namespace Video
struct IDXGISwapChain;

namespace VideoDX11 {

// ---- VideoDX11-x64.dll's two exports (src/render/dx11/exports.cpp) ---------------
// 0x1800011a0 — CreateDeviceData(Video::Device*) -> a new VideoDX11::DeviceData,
// returned as its base: exactly `Video::Device::CreateDeviceDataFn`.
::Video::DeviceData* Dx11_CreateDeviceData(::Video::Device* device);
// 0x180001230 — GetDeviceName() -> "DirectX 11".
const char* Dx11_GetDeviceName();

// ---- the cutover-only DX9 placeholder (src/render/dx11/dx9_placeholder.cpp) -------
::Video::DeviceData* Dx9Placeholder_CreateDeviceData(::Video::Device* device);
const char*          Dx9Placeholder_GetDeviceName();

// ---- shared by DeviceData::EnumerateInterfaces and the placeholder ---------
// The DXGI adapter / output / display-mode walk of retail slot 1 (0x180004690),
// appending ONE `Video::Device::GraphicsInterface` named `interfaceName` to
// `device`'s interface array. Defined once, in src/render/dx11/devicedata.cpp; false
// when dxgi.dll / d3d11.dll's two entry points cannot be resolved (B5_RULES D9:
// the "run the original" arm it replaces had no original to run).
bool Dx11EnumerateDxgiInterfaces(::Video::Device* device, const char* interfaceName);

// ---- B5 DEV-A (B5_RULES D7): the active DeviceData's swap chain ------------
// `VideoDX11::DeviceData::swapChain` (+0x18) of `Video::Device::_singleton->
// deviceData`, read by name. Defined in src/render/dx11/dx11_backend_access.cpp; the
// same NO-NULL-CHECK contract as Dx11DeviceData() there (the device singleton
// and its DeviceData must exist). src/render/dx11/swap_probe_dx11.cpp's swap-chain probes
// call it instead of reading `activeDD + 0x18`.
IDXGISwapChain* Dx11SwapChain();

}  // namespace VideoDX11
