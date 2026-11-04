#pragma once
// ============================================================================
// src/render/dx11/boot_device_lost/boot_device_lost.h -- fix.boot_device_lost, the DX11 side
// ============================================================================
// NOT RETAIL (docs/retail_bugs.md section 10). The registry hands the row's live bool to
// BootDeviceLostBackend::SetFixFlag (src/render/common/boot_device_lost/boot_device_lost_backend.h);
// ContextData::BeginCommands asks On() after a failed reset.
// ============================================================================

#include <atomic>

namespace VideoDX11 {

// The row's live bool, null until the boot sets it (and under the faithful profile it reads false).
extern std::atomic<const std::atomic<bool>*> g_extBootDeviceLost;

namespace BootDeviceLost {

// True while the row is on.
inline bool On() {
    const std::atomic<bool>* const live = g_extBootDeviceLost.load(std::memory_order_relaxed);
    return live != nullptr && live->load(std::memory_order_relaxed);
}

}  // namespace BootDeviceLost
}  // namespace VideoDX11
