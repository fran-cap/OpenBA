// ============================================================================
// src/platform/common/cpu/roster.h -- the thread-count rules and the `sys.cpu_limit` mask (CPULIMIT)
// ============================================================================
// NOT RETAIL. Standard C++ only, no OS call: the unit tests link roster.cpp alone
// (tests/unit/cpu_roster_test.cpp).
//
// THE ROSTER (docs/extensions/handins/THREADRESEARCH.md §7.4, THREADPLAN.md §1 rows
// 4-6). Every thread count of the extensions comes from these three functions of
// `n` = the PHYSICAL cores inside the process affinity mask (Cpu::Get().physical,
// topology.h) and whether ext.render_pipe is on. There are no numeric settings keys:
// the registry stays bools.
//
//   n                      1  2  3  4  5  6  7  8  ...
//   RenderThread(n)        0  0  1  1  1  1  1  1        n >= 3
//   PathWorkers(n, false)  1  1  1  2  3  4  4  4        clamp(n - 2, 1, 4)
//   PathWorkers(n, true)   1  1  1  1  2  3  4  4        clamp(n - 3, 1, 4)
//   CullHelpers(n, false)  0  0  0  0  1  2  2  2        clamp(n - 4, 0, 2)
//   CullHelpers(n, true)   0  0  0  0  0  1  2  2        clamp(n - 5, 0, 2)
//   ParallelWorkers(n)     0  1  2  3  4  5  6  7  7 ..  clamp(n - 1, 0, 7)
//
// ParallelWorkers is the fork-join pool of System::Parallel (src/engine/system/
// parallel_for.h, block WGPAR): the calling thread takes a share, so n cores = the
// caller + n - 1 workers. It runs during loads only (world generation), when the
// render thread / path workers / cull helpers are idle, so it does not subtract them.
//
// `renderPipe` is the caller's: pass the ACTIVE ext.render_pipe (RENDERPIPE-1 wires
// it; until then the path-worker call site passes false). PathWorkers ignores
// whether a render thread really exists at n < 3 (it then is the sequential path and
// costs no core); the clamp to >= 1 covers it.
//
// THE MASK (`sys.cpu_limit` in settings.cfg, read once at boot by Cpu::Boot):
//   0x55 / 0X55   hexadecimal, 1-16 digits after the prefix
//   85            decimal, a uint64
//   0             unrestricted (the same as an absent key)
//   anything else (empty, junk, overflow, a sign, trailing text) is IGNORED: the run is
//   unrestricted and the boot line says so. Leading / trailing spaces are allowed.
// Bit i = logical CPU i (Windows: processor group 0; Linux: CPU i). The mask is
// clamped to the system mask (Platform::CpuAffinity); bits outside it are dropped and
// the boot line says `clamped`. A mask with no bit left inside the system mask is
// ignored like junk.
//
// THE BIG CORES (Android P12). A phone's SoC is heterogeneous (big.LITTLE / DynamIQ):
// its little cores run the same code two to three times slower than its big ones, and
// a fork-join over "n cores" waits for the slowest. `n` above is therefore the physical
// cores among the BIG CPUs of the mask: BigCoreMask keeps a CPU whose capacity (the
// kernel's cpu_capacity, 1024 for the biggest core on the machine) is more than half the
// largest in the mask, which separates a prime or performance cluster from an
// efficiency cluster on every SoC seen (prime 1024 / performance 700-900 / efficiency
// 300-500). Uniform or unknown capacities (every PC, Windows, macOS) keep the whole mask,
// so nothing changes off a phone. The main, render, audio and loader threads are not
// pinned: the mask only sizes the roster.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace Cpu {

constexpr int kMaxPathWorkers = 4;   // == Ext::PathWorkers::kMaxWorkers (asserted in path_workers.cpp)
constexpr int kMaxCullHelpers = 2;
constexpr int kMaxParallelWorkers = 7;   // == System::Parallel::kMaxWorkers

// The CPUs of `mask` that count as big (the banner). `capacity[i]` is CPU i's capacity, 0
// when unknown; one unknown CPU inside `mask` makes the whole mask big (no guess).
uint64_t BigCoreMask(const int* capacity, uint64_t mask);

bool RenderThread(int physicalCores);
int  PathWorkers(int physicalCores, bool renderPipe);
int  CullHelpers(int physicalCores, bool renderPipe);
int  ParallelWorkers(int physicalCores);

// The value grammar above. true + `*mask` (0 allowed) when `text` is a mask; false
// (`*mask` untouched) when it is not.
bool ParseMask(const std::string& text, uint64_t* mask);

int MaskPopcount(uint64_t mask);

// What the limit does at boot.
enum class Limit : uint8_t {
    None,      // absent, 0, junk, or nothing left after the clamp: the process mask is untouched
    Applied,   // the requested mask, unchanged
    Clamped,   // the requested mask AND the system mask (bits were dropped)
};
const char* LimitName(Limit l);   // "none" / "applied" / "clamped"

struct LimitDecision {
    Limit    limit     = Limit::None;
    uint64_t requested = 0;   // the parsed value (0 when absent or junk)
    uint64_t apply     = 0;   // the mask to set; 0 with Limit::None
    bool     junk      = false;   // present but not a mask
    bool     empty     = false;   // a mask with no bit inside the system mask
};

// `present` / `value`: the settings key. `systemMask`: Platform::CpuAffinity's.
LimitDecision DecideLimit(bool present, const std::string& value, uint64_t systemMask);

}  // namespace Cpu
