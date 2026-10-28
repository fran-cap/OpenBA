// ============================================================================
// src/platform/posix/memory_posix.cpp -- platform_memory.h on Linux and macOS
// ============================================================================
// NOT RETAIL. Linux and macOS (no darwin twin: CMake builds this file there) keep every desktop
// default. Android replaces this file (src/platform/android/memory_android.cpp).
// ============================================================================

#include "platform_memory.h"

namespace Platform {
namespace Memory {

bool LowMemoryDevice() {
    return false;
}

}  // namespace Memory
}  // namespace Platform
