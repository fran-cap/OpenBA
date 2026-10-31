// ============================================================================
// src/platform/posix/cpu_posix.cpp -- Platform:: CPU affinity / topology over Linux
// ============================================================================
// The POSIX (Linux) half of src/platform/common/platform_cpu.h (block CPULIMIT).
// NOT RETAIL. sched_getaffinity / sched_setaffinity are Linux (glibc, _GNU_SOURCE,
// which g++ defines); the topology comes from sysfs.
//
// sched_setaffinity(0, ...) binds the CALLING thread only; threads it creates later
// inherit the mask. Cpu::Boot calls it first thing in main(), before any thread of
// ours exists, so every later thread (loader, audio, pathfinder, path workers,
// watchdog, SDL's) inherits it.
// ============================================================================

#include "platform_cpu.h"

#include <sched.h>

#include <cstdio>
#include <cstdlib>

namespace Platform {

namespace {

constexpr int kMaxCpus = 64;

uint64_t FromSet(const cpu_set_t& set) {
    uint64_t m = 0;
    for (int i = 0; i < kMaxCpus; ++i)
        if (CPU_ISSET(i, &set)) m |= (uint64_t{1} << i);
    return m;
}

// A sysfs CPU list ("0-7,9,12-13") as a mask of CPUs 0-63; false when unreadable.
bool ReadCpuList(const char* path, uint64_t* out) {
    FILE* f = std::fopen(path, "r");
    if (f == nullptr) return false;
    char line[512] = {0};
    const bool got = std::fgets(line, sizeof(line), f) != nullptr;
    std::fclose(f);
    if (!got) return false;
    uint64_t m = 0;
    const char* p = line;
    while (*p >= '0' && *p <= '9') {
        char* end = nullptr;
        const int a = static_cast<int>(std::strtol(p, &end, 10));
        int b = a;
        p = end;
        if (*p == '-') {
            b = static_cast<int>(std::strtol(p + 1, &end, 10));
            p = end;
        }
        for (int i = a; i <= b && i < kMaxCpus; ++i)
            if (i >= 0) m |= (uint64_t{1} << i);
        if (*p == ',') ++p;
    }
    if (m == 0) return false;
    *out = m;
    return true;
}

bool ReadInt(const char* path, int* out) {
    FILE* f = std::fopen(path, "r");
    if (f == nullptr) return false;
    const bool ok = std::fscanf(f, "%d", out) == 1;
    std::fclose(f);
    return ok;
}

}  // namespace

bool CpuAffinity(uint64_t* processMask, uint64_t* systemMask) {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0) return false;
    *processMask = FromSet(set);
    uint64_t online = 0;
    *systemMask = ReadCpuList("/sys/devices/system/cpu/online", &online) ? online : *processMask;
    return true;
}

bool CpuSetProcessAffinity(uint64_t mask) {
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int i = 0; i < kMaxCpus; ++i)
        if ((mask >> i) & 1u) CPU_SET(i, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;
}

// The kernel's capacity of one CPU (a heterogeneous SoC exposes it for the energy-aware
// scheduler; a PC's kernel has the file too, every CPU at 1024, or not at all).
bool CpuCoreCapacity(int cpu, int* capacity) {
    if (cpu < 0 || cpu >= kMaxCpus) return false;
    char path[128];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpu_capacity", cpu);
    int value = 0;
    if (!ReadInt(path, &value) || value <= 0) return false;
    *capacity = value;
    return true;
}

bool CpuPhysicalCores(uint64_t mask, int* cores, const char** reason) {
    uint64_t seen[kMaxCpus];
    int n = 0;
    for (int i = 0; i < kMaxCpus; ++i) {
        if (((mask >> i) & 1u) == 0) continue;
        char path[128];
        int core = 0;
        int package = 0;
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/core_id", i);
        if (!ReadInt(path, &core)) {
            *reason = "sysfs-core_id";
            return false;
        }
        std::snprintf(path, sizeof(path),
                      "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
        if (!ReadInt(path, &package)) package = 0;
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(package)) << 32) |
                             static_cast<uint32_t>(core);
        bool dup = false;
        for (int k = 0; k < n; ++k) dup = dup || seen[k] == key;
        if (!dup) seen[n++] = key;
    }
    if (n == 0) {
        *reason = "no-core-in-mask";
        return false;
    }
    *cores = n;
    return true;
}

}  // namespace Platform
