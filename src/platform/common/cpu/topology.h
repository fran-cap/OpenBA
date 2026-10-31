// ============================================================================
// src/platform/common/cpu/topology.h -- the process's CPUs, and the `sys.cpu_limit` boot step (CPULIMIT)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/CPULIMIT.md is the hand-in; roster.h has the
// thread-count rules that read this struct and the key's grammar.
//
// Cpu::Boot() runs ONCE, first thing in WinMain / main (src/app/main.cpp), before
// any thread of ours exists:
//   1. reads `sys.cpu_limit` from settings.cfg with a pre-read of the file for that
//      one key (IO::SettingsStore, file only, no registry import): the engine's
//      Persistence::ReadSettings runs much later, inside Engine::Startup, after the
//      watchdog / sampler / SDL threads exist;
//   2. clamps it to the system mask and applies it (Platform::CpuSetProcessAffinity:
//      SetProcessAffinityMask / sched_setaffinity);
//   3. queries the topology inside the resulting mask and caches it; on a heterogeneous SoC
//      (Android P12, roster.h THE BIG CORES) `physical` counts the big cores only;
//   4. prints one stderr line (standalone_error.log):
//        [Cpu] mask=0x.. logical=N physical=M limit=<applied|none|clamped>
//              source=<topology|fallback:reason>[ requested=0x..][ big=0x..]
// Cpu::Get() returns the cached struct from then on (read-once, like Ext::Ext()).
// No getenv, no BANISHED_* flag.
// ============================================================================
#pragma once

#include "platform/common/cpu/roster.h"

#include <cstdint>

namespace Cpu {

struct Topology {
    uint64_t mask      = 0;    // the process affinity mask (after the limit); 0 = unknown
    int      logical   = 1;    // popcount(mask)
    int      physical  = 1;    // physical cores with a CPU in bigMask (== logical on fallback)
    uint64_t bigMask   = 0;    // the big CPUs of mask (roster.h BigCoreMask); == mask on a uniform machine
    bool     heterogeneous = false;   // bigMask != mask: the roster is sized on the big cores
    bool     topology  = false;   // physical came from the OS topology query
    const char* reason = "not-booted";   // the fallback's reason (static string)
    Limit    limit     = Limit::None;
    uint64_t requested = 0;    // sys.cpu_limit as parsed (0 = absent / 0 / junk)
};

// The process mask and the cores inside it, now. Calls no setter.
Topology QueryTopology();

// The boot step above. Once; a second call does nothing.
void Boot();

// The cached result of Boot (the struct's defaults before it: 1 core).
const Topology& Get();

// "mask=0x.. logical=N physical=M limit=.. source=..[ requested=0x..]" into `buf`.
void Describe(const Topology& t, char* buf, int cap);

}  // namespace Cpu
