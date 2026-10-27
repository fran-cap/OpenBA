// ============================================================================
// src/platform/darwin/cpu_darwin.cpp -- Platform:: CPU affinity / topology over Darwin
// ============================================================================
// The Darwin twin of src/platform/posix/cpu_posix.cpp (src/platform/darwin/README.md).
// NOT RETAIL. Darwin exposes scheduling hints, not Linux-style hard CPU affinity, so
// CpuSetProcessAffinity answers "not supported"; the counts come from sysctl.
// ============================================================================

#include "platform_cpu.h"

#include <sys/sysctl.h>

#include <cstdint>

namespace Platform {

bool CpuAffinity(uint64_t* processMask, uint64_t* systemMask) {
    int count = 0;
    size_t bytes = sizeof(count);
    if (sysctlbyname("hw.logicalcpu", &count, &bytes, nullptr, 0) != 0 || count <= 0)
        return false;
    const uint64_t all = count >= 64 ? ~uint64_t{ 0 } : (uint64_t{ 1 } << count) - 1;
    *processMask = all;
    *systemMask = all;
    return true;
}

bool CpuSetProcessAffinity(uint64_t) {
    return false;
}

bool CpuPhysicalCores(uint64_t mask, int* cores, const char** reason) {
    uint64_t process = 0;
    uint64_t system = 0;
    if (!CpuAffinity(&process, &system) || mask != system) {
        *reason = "darwin-subset-topology-unavailable";
        return false;
    }
    size_t bytes = sizeof(*cores);
    return sysctlbyname("hw.physicalcpu", cores, &bytes, nullptr, 0) == 0;
}

// No per-core capacity is read on Darwin: the roster treats every core as the same size
// (platform_cpu.h).
bool CpuCoreCapacity(int, int*) {
    return false;
}

}  // namespace Platform
