// ============================================================================
// src/diag/posix/sampler_posix.cpp -- the sampling profiler's POSIX twin
// ============================================================================
// Phase D.4 LINUX-1 (D4_RULES C10). src/diag/win/sampler.cpp (BANISHED_SAMPLE=<hz>)
// suspends the sim thread, reads its context and walks it; that is Win32
// (SuspendThread / GetThreadContext). The tag the game writes (g_samplerTag, the
// phase marker gameupdateloop.cpp and entitylist.cpp set) is defined here so the
// portable tree links; Sampler_Init says, once, that the profiler is Windows-only
// when BANISHED_SAMPLE asked for it.
// ============================================================================

#include "sampler.h"
#include "options.h"

#include <cstdio>

volatile int g_samplerTag = static_cast<int>(SamplerTag::OutsideUpdate);

void Sampler_Init() {
    if (Opt().sample > 0) {
        std::fprintf(stderr, "[Sampler] BANISHED_SAMPLE=%d requested, but the sampling "
                             "profiler is Windows-only (src/diag/win/sampler.cpp): NOT "
                             "started on this platform\n", Opt().sample);
        std::fflush(stderr);
    }
}

void Sampler_Flush() {}
