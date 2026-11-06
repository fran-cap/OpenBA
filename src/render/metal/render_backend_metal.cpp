// ============================================================================
// src/render/metal/render_backend_metal.cpp -- the Metal backend's src/render/common/render_backend.h
// ============================================================================
// NOT RETAIL. Plugs the Metal DeviceData into the interface slots that
// src/app/video_interface.cpp fills. The main slot creates the real Metal device
// data; the second slot is the cutover-only placeholder (the same role as
// src/render/dx11/dx9_placeholder.cpp): it lists the same adapter and modes under
// its own name and never creates a device, so choosing it in Options steps back to
// the main slot. Built only with BANISHED_RENDER_BACKEND=metal
// (src/render/metal/backend.cmake).
// ============================================================================

#include "render_backend.h"   // src/render/common: the declarations
#include "metal_entry.h"

#include <SDL3/SDL_video.h>
#include <cstdio>
#include "video/CompanionData.h"   // Video::DeviceData

namespace Render {
namespace {

class PlaceholderDeviceData final : public ::Video::DeviceData {
public:
    explicit PlaceholderDeviceData(::Video::Device* owner)
        : ::Video::DeviceData(owner) {}

    bool EnumerateInterfaces() override {
        return VideoMetal::Enumerate(_owner, PlaceholderDeviceName());
    }

    bool CreateDevice() override {
        return false;
    }

    void Shutdown() override {}
};

}  // namespace

const char* DeviceName() {
    return "Metal";
}

::Video::DeviceData* CreateDeviceData(::Video::Device* device) {
    return VideoMetal::NewDeviceData(device);
}

uint64_t GraphicsWindowFlags() {
    return SDL_WINDOW_METAL;
}

// The boot diagnostics (main.cpp's [SwapProbe] lines): no DXGI swap chain here.
void DumpSwapChainState(const char* tag) {
    printf("[SwapProbe %s] not probed on this backend (Metal: no DXGI swap chain)\n", tag);
}

bool BootedExclusiveFullscreen() {
    return false;
}

// Fullscreen here is always SDL's popup window over the monitor: no display mode switch.
bool SupportsExclusiveFullscreen() {
    return false;
}

const char* PlaceholderDeviceName() {
    return "Metal (compatibility slot)";
}

::Video::DeviceData* PlaceholderCreateDeviceData(::Video::Device* device) {
    return new PlaceholderDeviceData(device);
}

}  // namespace Render
