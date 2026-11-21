#pragma once
// ============================================================================
// src/platform/common/platform_net.h -- one blocking HTTPS GET
// ============================================================================
// NOT RETAIL. Retail never opens a socket. ext.update_check asks the project's releases page for the
// newest release tag with it, once a day at most (src/ext/update_check/update_check.h,
// docs/extensions/handins/UPDATECHECK.md). It is the game's only network call.
//
// One twin per platform, picked by CMake (AGENTS.md section 4):
//   src/platform/win/net_win.cpp        Windows
//   src/platform/posix/net_posix.cpp    Linux, and every platform without a twin of its own
//   src/platform/darwin/net_darwin.cpp  macOS, when it needs its own
//   src/platform/android/net_android.cpp
// A platform that cannot make the request answers CanHttpsGet() false and HttpsGet 0; the caller
// then does nothing. No OS or library header here (D4_RULES C1).
//
// THE CONTRACT every twin keeps:
//   * the system's own TLS stack and trust store; the server's certificate IS verified, and a
//     failed verification is a failed request (0). Never an option to skip it.
//   * nothing is sent but the request line, Host, the given User-Agent and what the stack adds by
//     itself (Accept, Connection); no cookies, no credentials, no proxy credentials prompt.
//   * `timeoutMs` bounds the whole call (resolve + connect + send + receive), within a second or so.
//   * the body is read up to `maxBytes` and the rest dropped (the status is still returned).
//   * no window, no dialog, no message pump: the caller is a worker thread.
//   * no log line per call but on a failure, one `[Net]` line to stderr naming the step that failed.
// ============================================================================

#include <cstddef>
#include <string>

namespace Platform {
namespace Net {

// Whether this build can make the request at all (the stack is present on this machine).
bool CanHttpsGet();

// GET https://<host><path> (port 443; `path` starts with '/'). Blocks the calling thread; any
// thread but never the main one. Returns the HTTP status of the final response (200, 403, 404...)
// with its body's first `maxBytes` bytes in `body`, or 0 with `body` empty when no response was
// read (no network, the name did not resolve, TLS failed, the timeout passed, not supported).
// Redirects may be followed or not: the caller asks for a URL that answers directly.
int HttpsGet(const char* host, const char* path, const char* userAgent, int timeoutMs,
             size_t maxBytes, std::string& body);

}  // namespace Net
}  // namespace Platform
