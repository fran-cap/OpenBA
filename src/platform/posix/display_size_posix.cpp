// ============================================================================
// src/platform/posix/display_size_posix.cpp -- the display's physical size: unknown
// ============================================================================
// NOT RETAIL (platform_display_size.h). Linux and macOS (no darwin twin: CMake builds this file
// there) have no first-run seed that asks.
// Android replaces this file (src/platform/android/display_size_android.cpp).
// ============================================================================

#include "platform_display_size.h"

namespace Platform {
namespace Display {

bool PhysicalScreenInches(float* widthInches, float* heightInches) {
    *widthInches = 0.0f;
    *heightInches = 0.0f;
    return false;
}

}  // namespace Display
}  // namespace Platform
