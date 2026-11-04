#pragma once
// ============================================================================
// src/engine/video/refresh_rate_at.h -- Device::refreshRateIndex read against a mode
// ============================================================================
// The DX11 swap-chain description (VideoDX11 FillSwapChainDesc) and its rebuild
// (Resize 0x180004340: `local_38 + *(int*)(Device+0x10)*8`) read the refresh rate
// of the current display mode at Device::refreshRateIndex.
// ============================================================================

#include <cstdint>

#include "video/Device.h"

namespace Video {

// NOT RETAIL (review F-171, out-of-bounds read): only Device::Startup sets refreshRateIndex, for
// the boot mode (or -1 when no rate matched); ApplyChangedSettings (0x1800f8110) changes
// displayModeIndex and leaves it, so after an Options resolution change retail indexes another
// mode's refresh list with it, unchecked. An index outside the list reads entry 0, and an empty
// list 0 / 1 (DXGI: unspecified). An index inside it is retail's read.
inline Device::RefreshRate RefreshRateAt(const Device::DisplayMode& mode, int32_t index) {
    if (index >= 0 && index < mode.refreshCount)
        return mode.refreshData[index];
    if (mode.refreshCount > 0)
        return mode.refreshData[0];
    Device::RefreshRate none;
    none.numerator = 0;
    none.denominator = 1;
    return none;
}

}  // namespace Video
