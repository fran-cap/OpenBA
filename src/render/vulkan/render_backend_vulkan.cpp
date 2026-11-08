// ============================================================================
// src/render/vulkan/render_backend_vulkan.cpp -- the Vulkan backend's src/render/common/render_backend.h
// ============================================================================
// NOT RETAIL. Plugs the Vulkan DeviceData into the interface slots that
// src/app/video_interface.cpp fills. The main slot creates the real device data; the second
// slot is the cutover-only placeholder (the role of src/render/dx11/dx9_placeholder.cpp): it
// lists the same adapters and modes under its own name and never creates a device, so
// choosing it in Options steps back to the main slot. Built only with
// BANISHED_RENDER_BACKEND=vulkan (src/render/vulkan/backend.cmake).
// ============================================================================

#include "render_backend.h"   // src/render/common: the declarations
#include "vulkan_entry.h"

#include <SDL3/SDL_video.h>
#include "video/CompanionData.h"   // Video::DeviceData

#include <cstdio>

namespace Render {
BANISHED_RENDER_IMPL_BEGIN
namespace {

class PlaceholderDeviceData final : public ::Video::DeviceData {
public:
    explicit PlaceholderDeviceData(::Video::Device* owner)
        : ::Video::DeviceData(owner) {}

    bool EnumerateInterfaces() override {
        return VideoVulkan::Enumerate(_owner, PlaceholderDeviceName());
    }

    bool CreateDevice() override {
        return false;
    }

    void Shutdown() override {}
};

}  // namespace

const char* DeviceName() {
    return "Vulkan";
}

::Video::DeviceData* CreateDeviceData(::Video::Device* device) {
    return VideoVulkan::NewDeviceData(device);
}

uint64_t GraphicsWindowFlags() {
    return SDL_WINDOW_VULKAN;
}

// The boot diagnostics (main.cpp's [SwapProbe] lines): no DXGI swap chain here.
void DumpSwapChainState(const char* tag) {
    printf("[SwapProbe %s] not probed on this backend (Vulkan: no DXGI swap chain)\n", tag);
}

bool BootedExclusiveFullscreen() {
    return false;
}

// Fullscreen here is always SDL's popup window over the monitor: no display mode switch.
bool SupportsExclusiveFullscreen() {
    return false;
}

const char* PlaceholderDeviceName() {
    return "Vulkan (compatibility slot)";
}

::Video::DeviceData* PlaceholderCreateDeviceData(::Video::Device* device) {
    return new PlaceholderDeviceData(device);
}

BANISHED_RENDER_IMPL_END
}  // namespace Render

#if defined(BANISHED_RENDER_IMPL_NS)
// ---- the dual exe (BANISHED_RENDER_BACKEND=dx11+vulkan) ----------------------------------------
// This backend's BackendTable (src/render/common/render_backend_table.h): its entry points behind the
// backend-neutral headers, for src/render/common/multi/render_backend_dispatch.cpp. The probe is the
// instance the adapter list needs anyway (VideoVulkan::Available, device.cpp): no loader, a loader
// older than 1.1 or no adapter with a graphics queue answers false before any device is made.
#include "render_backend_table_fill.h"

const Render::BackendTable& RenderBackendTable_vulkan() {
    static const Render::BackendTable table = MakeBackendTable("vulkan", &VideoVulkan::Available);
    return table;
}
#endif
