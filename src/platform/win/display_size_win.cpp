// ============================================================================
// src/platform/win/display_size_win.cpp -- the display's physical size: unknown
// ============================================================================
// NOT RETAIL (platform_display_size.h). Windows has no first-run seed that asks.
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
