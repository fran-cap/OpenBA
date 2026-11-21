// ============================================================================
// src/platform/posix/net_posix.cpp -- Platform::Net::HttpsGet: not supported yet
// ============================================================================
// NOT RETAIL. The rules are in platform_net.h. The twin of src/platform/win/net_win.cpp.
// ============================================================================

#include "platform_net.h"

namespace Platform {
namespace Net {

bool CanHttpsGet() {
    return false;
}

int HttpsGet(const char* /*host*/, const char* /*path*/, const char* /*userAgent*/, int /*timeoutMs*/,
             size_t /*maxBytes*/, std::string& body) {
    body.clear();
    return 0;
}

}  // namespace Net
}  // namespace Platform
