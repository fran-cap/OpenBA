#pragma once
// ============================================================================
// src/render/common/boot_device_lost/boot_device_lost_backend.h -- fix.boot_device_lost's render
// side, as the registry sees it (docs/retail_bugs.md section 10, known_port_issues rows 1332 / 1334)
// ============================================================================
// NOT RETAIL. No graphics types: src/ext/registry_boot.cpp includes it. The DX11 body is
// src/render/dx11/boot_device_lost/, the Metal body src/render/metal/boot_device_lost/.
//
// Retail enters its device-lost recovery only from Present. A swap-chain reset that fails because
// the device has been REMOVED (a GPU driver reset) never reaches Present again: every retry fails
// the same way, and the frame loop spins in the reset or the first read-back crashes on the dead
// device. With the row on, a backend that can tell a removed device from a refused reset runs the
// same recovery from the failed reset.
// ============================================================================

#include <atomic>

namespace BootDeviceLostBackend {

// The row's live atomic (Ext::LiveFix, set by registry_boot.cpp on the main thread once at boot;
// the menu toggle is read through it, from the render thread under ext.render_pipe: review
// F-262). Null, or false: retail's behaviour.
void SetFixFlag(const std::atomic<bool>* live);

}  // namespace BootDeviceLostBackend
