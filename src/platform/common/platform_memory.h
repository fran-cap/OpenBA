#pragma once
// ============================================================================
// src/platform/common/platform_memory.h -- whether this is a low-memory device
// ============================================================================
// NOT RETAIL (docs/android/P13_MEM.md §6). The phone defaults (src/platform/android/app_android.cpp)
// load textures at Half (ext.texture_quality) on a device with little RAM, where the app's ~0.6 GB is a
// large share and a likely low-memory-killer victim in the background.
//   Android (src/platform/android/memory_android.cpp): the total RAM the kernel reports, through the
//            rule below.
//   Everywhere else (src/platform/posix/memory_posix.cpp, also macOS's; src/platform/win/
//            memory_win.cpp): never low.
// D4_RULES C1: namespace `Platform`, free functions, NO OS header here.
// ============================================================================

#include <cstdint>

namespace Platform {
namespace Memory {

// Low memory: total RAM, as the kernel reports it, below 6.5 GiB. A phone sold as 8 GB reports
// ~7.2-7.6 GiB (the Galaxy S10: 7.6), one sold as 6 GB ~5.4-5.7 GiB, 4 GB ~3.5-3.8 GiB: firmware
// carve-outs take the rest. 6.5 GiB parts the 6 and 8 GB classes with margin on both sides. 0 (not
// known) is not low.
constexpr uint64_t kLowMemoryBelow = 13ull << 29;   // 6.5 GiB

constexpr bool IsLowMemory(uint64_t totalBytes) {
    return totalBytes != 0 && totalBytes < kLowMemoryBelow;
}

// This device, by the rule above (the platforms that do not ask: false).
bool LowMemoryDevice();

}  // namespace Memory
}  // namespace Platform
