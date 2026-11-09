// ============================================================================
// src/render/dx11/boot_device_lost/boot_device_lost.cpp -- fix.boot_device_lost, the DX11 side
// ============================================================================
// NOT RETAIL. boot_device_lost.h has the contract.
// ============================================================================

#include "boot_device_lost/boot_device_lost.h"
#include "boot_device_lost/boot_device_lost_backend.h"

namespace VideoDX11 {

std::atomic<const std::atomic<bool>*> g_extBootDeviceLost{nullptr};

}  // namespace VideoDX11

namespace BootDeviceLostBackend {
BANISHED_RENDER_IMPL_BEGIN

void SetFixFlag(const std::atomic<bool>* live) {
    VideoDX11::g_extBootDeviceLost.store(live, std::memory_order_relaxed);
}

BANISHED_RENDER_IMPL_END
}  // namespace BootDeviceLostBackend
