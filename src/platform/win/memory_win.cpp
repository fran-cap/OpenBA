// ============================================================================
// src/platform/win/memory_win.cpp -- platform_memory.h on Windows
// ============================================================================
// NOT RETAIL. Windows keeps every desktop default.
// ============================================================================

#include "platform_memory.h"

namespace Platform {
namespace Memory {

bool LowMemoryDevice() {
    return false;
}

}  // namespace Memory
}  // namespace Platform
