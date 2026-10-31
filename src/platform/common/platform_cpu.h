#pragma once
// ============================================================================
// src/platform/common/platform_cpu.h -- CPU affinity and core topology (block CPULIMIT)
// ============================================================================
// NOT RETAIL. Retail never touches its affinity and never asks how many cores it has.
// The seam under src/platform/common/cpu/ (the `sys.cpu_limit` settings key and the thread-count
// roster, docs/extensions/handins/CPULIMIT.md). D4_RULES C1: namespace `Platform`,
// free functions, NO OS header here. Bodies: src/platform/win/cpu_win.cpp,
// src/platform/posix/cpu_posix.cpp.
//
// MASKS are the first 64 logical CPUs, bit i = CPU i. On Windows that is processor
// group 0 (a process starts in one group; a machine with more than 64 logical CPUs is
// read as its first group). On Linux it is CPUs 0-63 of the cpu_set_t.
// ============================================================================

#include <cstdint>

namespace Platform {

// The process's affinity mask and the mask it may widen to.
//   Windows: GetProcessAffinityMask (process, system).
//   Linux:   sched_getaffinity(0) of the CALLING thread (at boot, the only thread), and
//            /sys/devices/system/cpu/online (the process mask when that file is unreadable).
bool CpuAffinity(uint64_t* processMask, uint64_t* systemMask);

// Restrict the process to `mask` (non-zero, inside the system mask).
//   Windows: SetProcessAffinityMask -- every thread of the process, existing or later.
//   Linux:   sched_setaffinity(0) -- the CALLING thread and the threads it creates later
//            ONLY. Call it before the process creates its first thread.
bool CpuSetProcessAffinity(uint64_t mask);

// The physical cores with at least one logical CPU in `mask`.
//   Windows: GetLogicalProcessorInformationEx(RelationProcessorCore), group 0.
//   Linux:   /sys/devices/system/cpu/cpuN/topology/{physical_package_id,core_id},
//            distinct (package, core) pairs over the CPUs of `mask`.
// false (and a short static `reason`) when the query fails; the caller then counts
// logical CPUs instead.
bool CpuPhysicalCores(uint64_t mask, int* cores, const char** reason);

// The relative compute capacity of logical CPU `cpu`, as the scheduler sees it (the kernel's
// cpu_capacity: the biggest core on the machine is 1024, a little core a few hundred).
//   Linux / Android: /sys/devices/system/cpu/cpuN/cpu_capacity (a heterogeneous SoC's file;
//                    absent or uniform on an ordinary PC).
//   Windows, macOS:  false (no per-core capacity is read there; the roster treats the cores as
//                    uniform, as it did before).
// false when unknown; the caller then treats every core as the same size (roster.h, BigCoreMask).
bool CpuCoreCapacity(int cpu, int* capacity);

}  // namespace Platform
