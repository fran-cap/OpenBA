// ============================================================================
// src/platform/posix/pick_file_posix.cpp -- Platform::PickOpenFile: no file dialog yet
// ============================================================================
// NOT RETAIL. The rules are in platform_pick_file.h. The twin of src/platform/win/pick_file_win.cpp.
// ============================================================================

#include "platform_pick_file.h"

namespace Platform {

bool CanPickFile() {
    return false;
}

bool PickOpenFile(const char* /*title*/, const char* /*filterName*/, const char* /*filterPattern*/,
                  const std::string& /*startDir*/, std::string& /*out*/) {
    return false;
}

}  // namespace Platform
