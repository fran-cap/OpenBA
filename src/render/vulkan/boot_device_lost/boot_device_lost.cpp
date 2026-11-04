// ============================================================================
// src/render/vulkan/boot_device_lost/boot_device_lost.cpp -- fix.boot_device_lost, the Vulkan side
// ============================================================================
// NOT RETAIL (P4). boot_device_lost.h has the contract.
// ============================================================================

#include "boot_device_lost/boot_device_lost.h"
#include "boot_device_lost/boot_device_lost_backend.h"

#include <atomic>

namespace VideoVulkan {
namespace BootDeviceLost {

namespace {
std::atomic<const std::atomic<bool>*> g_flag{nullptr};
}  // namespace

bool On() {
    const std::atomic<bool>* const live = g_flag.load(std::memory_order_relaxed);
    return live != nullptr && live->load(std::memory_order_relaxed);
}

}  // namespace BootDeviceLost
}  // namespace VideoVulkan

namespace BootDeviceLostBackend {

void SetFixFlag(const std::atomic<bool>* live) {
    VideoVulkan::BootDeviceLost::g_flag.store(live, std::memory_order_relaxed);
}

}  // namespace BootDeviceLostBackend
