// src/engine/video/validate_configuration.h -- the two clamps both ValidateConfiguration bodies share
//
// Video::Device::ValidateConfiguration() (retail 0x1800f8830, device_startup.cpp) and the 8-argument
// overload (retail 0x1800f7370, device.cpp) clamp the persisted refresh rate and multisample index the
// same way. The two steps live here, header-only, so tests/unit/video_validate_config_test.cpp can drive
// them without the device (review F-130, F-156).
#pragma once

#include <cstdint>

#include "video/Device.h"

namespace Video {
namespace ValidateConfig {

// True when n / d is defined: not a division by zero, not INT_MIN / -1.
inline bool RatioDefined(int32_t n, int32_t d) {
    return d != 0 && !(n == INT32_MIN && d == -1);
}

// The refresh-rate fallback when no entry of the mode matches (numerator, denominator) exactly: the
// first entry whose INTEGER ratio matches, else entry 0 (0x1800f89a4 / the 8-argument body's loop).
// Both sides are `CDQ; IDIV`, so 59.94 Hz (60000/1001) reduces to 59 and does not match 60/1.
inline int32_t PickRefreshByRatio(const Device::RefreshRate* data, int32_t count, int32_t refreshN,
                                  int32_t refreshD) {
    if (count <= 0) return 0;
    // NOT RETAIL (review F-130, undefined behaviour): retail divides by the persisted denominator
    // (settings.cfg VideoRefreshD) and by each mode's unchecked; 0, or INT_MIN / -1, faults at boot.
    // Such a request matches no ratio (entry 0), and such an entry is skipped.
    if (!RatioDefined(refreshN, refreshD)) return 0;
    const int32_t want = refreshN / refreshD;
    for (int32_t i = 0; i < count; ++i) {
        if (!RatioDefined(data[i].numerator, data[i].denominator)) continue;
        if (data[i].numerator / data[i].denominator == want) return i;
    }
    return 0;
}

// 0x1800f89f3: a multisample index whose adapter entry is FALSE falls back to 0.
inline int32_t ClampMultisample(const uint8_t* data, int32_t count, int32_t index) {
    // NOT RETAIL (review F-156, undefined behaviour): retail reads the entry with no bound against
    // multisampleCount (0x1800f89ff), and settings.cfg VideoMultisample is unchecked. An index outside
    // the list falls back to 0 like a false entry.
    if (index < 0 || index >= count) return 0;
    if (data[index] == 0) return 0;
    return index;
}

}  // namespace ValidateConfig
}  // namespace Video
