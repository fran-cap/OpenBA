// ============================================================================
// src/platform/android/memory_android.cpp -- platform_memory.h on Android
// ============================================================================
// NOT RETAIL. Replaces src/platform/posix/memory_posix.cpp. The total RAM is the kernel's MemTotal
// (sysconf: physical pages times the page size); the verdict is logged once.
// ============================================================================

#include "platform_memory.h"

#include <android/log.h>
#include <unistd.h>

namespace Platform {
namespace Memory {

bool LowMemoryDevice() {
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    const uint64_t total = pages > 0 && pageSize > 0 ? static_cast<uint64_t>(pages) * static_cast<uint64_t>(pageSize) : 0;
    const bool low = IsLowMemory(total);
    static bool logged = false;
    if (!logged) {
        logged = true;
        __android_log_print(ANDROID_LOG_INFO, "OpenBA", "[Memory] total RAM %.2f GiB: %s", static_cast<double>(total) / 1073741824.0,
                            low ? "low memory (textures at Half by default)" : "not low memory");
    }
    return low;
}

}  // namespace Memory
}  // namespace Platform
