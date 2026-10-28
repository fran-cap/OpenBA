// ============================================================================
// src/platform/posix/perf_hint_posix.cpp -- the identity bodies of platform_perf_hint.h
// ============================================================================
// NOT RETAIL. Linux and macOS (no darwin twin: CMake builds this file there) have no hint service
// the game uses.
// Android replaces this file (src/platform/android/perf_hint_android.cpp).
// ============================================================================

#include "platform_perf_hint.h"

namespace Platform {
namespace PerfHint {

void SetTarget(int64_t targetNs) {
    (void)targetNs;
}

void AttachThread(Role role) {
    (void)role;
}

void DetachThread(Role role) {
    (void)role;
}

void WorkBegin(Role role) {
    (void)role;
}

void WorkEnd(Role role) {
    (void)role;
}

}  // namespace PerfHint
}  // namespace Platform
