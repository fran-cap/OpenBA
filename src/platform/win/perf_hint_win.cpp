// ============================================================================
// src/platform/win/perf_hint_win.cpp -- the identity bodies of platform_perf_hint.h
// ============================================================================
// NOT RETAIL. Windows has no hint service the game uses.
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
