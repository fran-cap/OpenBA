// ============================================================================
// src/render/metal/boot_device_lost/boot_device_lost.cpp -- fix.boot_device_lost, the Metal side
// ============================================================================
// NOT RETAIL (src/render/common/boot_device_lost/boot_device_lost_backend.h). The row's backend
// half recovers a swap-chain reset that failed on a REMOVED DXGI device; this backend has no
// swap-chain reset that reports a removed device, so the flag is accepted and has nothing to act
// on. The row's engine half (the SaveLoad-walk skip in Engine::MainLoop) applies on every backend.
// ============================================================================

#include "boot_device_lost/boot_device_lost_backend.h"

namespace BootDeviceLostBackend {

void SetFixFlag(const std::atomic<bool>* /*live*/) {
}

}  // namespace BootDeviceLostBackend
