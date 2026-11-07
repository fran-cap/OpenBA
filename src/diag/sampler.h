// Sampling profiler (env BANISHED_SAMPLE=<hz>, default 1000).
//
// A helper thread suspends the sampled threads on a timer, unwinds each stack, and
// records (thread, time, tag, frames). The main thread is sampled every tick; every
// game thread registered in DiagThreads (diag_threads.h: the engine loader, the
// persistence worker, audio, the pathfinder, path workers, the render thread, cull
// helpers) is sampled on a tick where it is mid-job (System::Thread's run bit) or ran
// since the previous tick. Each line of standalone_samples.txt in the working
// directory is "@<role><tid> <ms since process start> <tag> <n> <frames...>";
// tools/sample_report.py aggregates them (self/inclusive per function, per tag,
// --thread / --from / --to to cut one thread or a time window; it defaults to the
// main thread, which is what the report showed before block LOADSTAMP).
// The tag says what the sim was doing: 0 = outside GameUpdateLoop::Update (draw,
// message pump), 1 = inside Update but no phase, 2+ph = the SIMPROF phase
// (2 PairCache, 3 Animation, 4 EntityList, 5 Weather, 6 CitizenList, 7 PostSim),
// 8 = EntityList bucket ticks; 99 on every other thread (the tag is the main thread's phase).
// SamplerTag below names them.
//
// Why: the per-component profile (COMPPROF) only sees the vtable calls the entity
// list makes; time inside Runtime-x64.dll (allocator, containers, math) and inside
// our own helpers is invisible to it. Sampling sees everything.
#pragma once

// The values of g_samplerTag. They are an external contract: standalone_samples.txt prints
// the number and tools/sample_report.py TAGS maps it, so a value never changes. Every value
// fits the sampler's uint8_t Sample::tag. PairCache..PostSim are 2 + the SimPhase
// (gameupdateloop.cpp, PhaseTimer).
enum class SamplerTag : int {
    OutsideUpdate     = 0,    // outside GameUpdateLoop::Update (draw, message pump)
    UpdateNoPhase     = 1,    // inside Update, no SIMPROF phase
    PairCache         = 2,
    Animation         = 3,
    EntityList        = 4,
    Weather           = 5,
    CitizenList       = 6,
    PostSim           = 7,
    EntityListBuckets = 8,    // EntityList::Update's 5-bucket (slot-9) ticks
    NotMainThread     = 99,   // stored in Sample::tag for every other thread, never in g_samplerTag
};

extern volatile int g_samplerTag;   // holds a SamplerTag

void Sampler_Init();    // no-op unless BANISHED_SAMPLE is set; call after HwWatch_Init
void Sampler_Flush();   // append pending samples to the file (also run at exit)
