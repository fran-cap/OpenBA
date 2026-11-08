// ============================================================================
// src/render/dx11/render_backend_dx11.cpp -- the DX11 backend's src/render/common/render_backend.h
// ============================================================================
// NOT RETAIL. Thin forwarders from the backend-neutral entry points to the DX11
// plugin's own (src/render/dx11/VideoDX11Plugin.h): VideoDX11-x64.dll's two exports
// (src/render/dx11/exports.cpp, 0x1800011a0 / 0x180001230) and the cutover-only DX9
// placeholder (src/render/dx11/dx9_placeholder.cpp). Built only with
// BANISHED_RENDER_BACKEND=dx11 (src/render/dx11/backend.cmake).
// ============================================================================

#include "render_backend.h"     // src/render/common: the declarations
#include "VideoDX11Plugin.h"    // VideoDX11::Dx11_* / Dx9Placeholder_*

namespace Render {
BANISHED_RENDER_IMPL_BEGIN

const char* DeviceName() {
    return VideoDX11::Dx11_GetDeviceName();
}

::Video::DeviceData* CreateDeviceData(::Video::Device* device) {
    return VideoDX11::Dx11_CreateDeviceData(device);
}

uint64_t GraphicsWindowFlags() {
    return 0;
}

const char* PlaceholderDeviceName() {
    return VideoDX11::Dx9Placeholder_GetDeviceName();
}

::Video::DeviceData* PlaceholderCreateDeviceData(::Video::Device* device) {
    return VideoDX11::Dx9Placeholder_CreateDeviceData(device);
}

BANISHED_RENDER_IMPL_END
}  // namespace Render

#if defined(BANISHED_RENDER_IMPL_NS)
// ---- the dual exe (BANISHED_RENDER_BACKEND=dx11+vulkan) ----------------------------------------
// This backend's BackendTable (src/render/common/render_backend_table.h): its entry points behind the
// backend-neutral headers, for src/render/common/multi/render_backend_dispatch.cpp. DX11 has no probe
// cheaper than its device: D3D11 is part of every Windows this exe runs on, and a machine whose
// adapter cannot make a device is judged by CreateDevice (Video::Device::PlatformStartup's loop).
#include "render_backend_table_fill.h"

const Render::BackendTable& RenderBackendTable_dx11() {
    static const Render::BackendTable table = MakeBackendTable("dx11", [] { return true; });
    return table;
}
#endif
