// src/diag/stress_harness.h
// BANISHED_STRESS — large-scale stress test + performance baseline harness.
//
// One env var (BANISHED_STRESS=1) turns a fresh autostart new game into a stress town:
// a MEDIUM map, three sets of every placeable building, 800 citizens spread over enough
// houses, running at 10x speed (the most stressful mix measured, see perf/README.md), every workplace staffed through the real seat
// cascade, the camera parked over the town, the FPS cap off, and a machine-readable
// per-interval metrics CSV (standalone_stress.csv) written next to the other logs.
// The run ends itself through the in-game pause menu after BANISHED_STRESS_FRAMES.
//
// Knobs (all optional):
//   BANISHED_STRESS=1              enable (implies BANISHED_AUTOSTART=1)
//   BANISHED_STRESS_CITIZENS=N     target population, default 800
//   BANISHED_STRESS_SETS=N         copies of the full building set, default 3; a NEGATIVE
//                                  N = |N| sets in the SPREAD layout (PFBENCH: workplaces and
//                                  storage on one side of the map, houses on the other, so
//                                  path requests dominate; tools/stress_run.sh --spread)
//   BANISHED_STRESS_FRAMES=N       sim frames before the harness quits, default 6000
//                                  (= 60000 sim ticks at the default 10x; see perf/README.md)
//                                  (0 = run forever, quit by hand)
//   BANISHED_STRESS_INTERVAL=N     frames per CSV row, default 600
//   BANISHED_STRESS_SPEED=N        game speed 1/2/5/10/20/50, default 10
//   BANISHED_STRESS_FOOD_MULT=N    starting-stock multiplier, default citizens/10
//                                  (barn volume caps what lands; the town is kept fed by
//                                  the harness food top-up pass, FoodPass, not by this)
//   BANISHED_STRESS_STAFF=0        skip the staffing pass (everyone stays a laborer)
//   BANISHED_STRESS_COMPPROF=N     per-component-type Update profile level, default 0 (off)
//   BANISHED_STRESS_CSV=<path>     CSV output path, default standalone_stress.csv
//
// Anything the harness sets by default (map size, climate, seed, FPS cap, camera pin, the
// water-sited demo buildings, …) can be overridden by setting that env var yourself:
// ApplyStressDefaults (src/app/options.cpp) only fills in fields that are UNSET.
#pragma once
#include <cstdint>

class GameUpdateLoop;
namespace Object { class Base; }

extern "C" {

// WinMain (and the POSIX main), after OptionsInit: reads the stress fields from Opt() and logs
// the [Stress] banner.
void StressHarness_Init();
bool StressHarness_Enabled();

int  StressHarness_TargetCitizens();   // 0 when disabled
int  StressHarness_Sets();             // |BANISHED_STRESS_SETS|
bool StressHarness_Spread();           // BANISHED_STRESS_SETS < 0: the spread layout
int  StressHarness_FoodMult();

// GameUpdateLoop::Update / Draw brackets (gameupdateloop.cpp).
void StressHarness_UpdateBegin(int frameNum);
void StressHarness_UpdateEnd(GameUpdateLoop* gameUpdateLoop, int frameNum);
void StressHarness_DrawBegin();
void StressHarness_DrawEnd();
// RENDERPIPE (ext.render_pipe, docs/extensions/handins/RENDERPIPE0.md): a pipelined
// frame ends its Draw bracket at the end of the main half (pass 1) instead of
// DrawEnd; the packet's GameUpdateLoop::Draw share (pass 2 + the post-scene draws)
// and the whole emit (BeginCommands .. Present) are bracketed separately. The CSV's
// draw_ms_mean is main + packet on those frames; draw_main_ms / draw_packet_ms /
// draw_emit_ms are appended columns (0 in a window with no pipelined frame).
// RENDERPIPE-1: PipePacket* / PipeEmit* run on the emitting thread (the render
// thread when it is up): atomics, folded at the CSV row. Two more appended
// columns, pipe_wait_us (hand-off waits per draw frame) and pipe_latency_us
// (hand-off -> Present done, mean), from Ext::RenderPipe::TakeWindow.
// StressHarness_Verify is callable from any thread (a lock).
void StressHarness_PipeMainEnd();
void StressHarness_PipePacketBegin();
void StressHarness_PipePacketEnd();
void StressHarness_PipeEmitBegin();
void StressHarness_PipeEmitEnd();
// HITCH_TAIL: the main thread's time inside ext.render_pipe's two MainLoop::Frame hooks, per
// Draw-to-Draw period (the hitch rows' fence_ms / pipe_tail_ms): seg 0 = THE FENCE
// (kExtBeforeSteps), seg 1 = the Frame tail (kExtFrameTail: main half + join + publish).
void StressHarness_PipeSegBegin(int seg);
void StressHarness_PipeSegEnd(int seg);

// Per-component-type Update profile. g_compProfOn is the ONLY thing the hot loop in
// EntityList_Update reads when the profile is off (one predictable branch).
extern int g_compProfOn;
// Begin() looks the component's type up WHILE IT IS STILL ALIVE (its Update may free it)
// and returns a slot; End() charges the elapsed ticks to that slot.
int  CompProf_Begin(Object::Base* comp);
int  CompProf_BeginKind(Object::Base* comp, int kind);   // kind 1 = per-frame slot-8 tick ("/tree")
void CompProf_End(int slot, long long tscTicks);   // ticks from CompProf_Now (RDTSC)
// OFFSCREEN (src/diag/char_prof.h, the [COMPPROF] /char rows): called from the
// COMPPROF >= 2 arm of the slot-8 tick only, around the same tick CompProf_BeginKind /
// CompProf_End bracket. Begin reads the character's state BEFORE the tick (class,
// visibility, transform) and returns 0 for a component it does not count; End charges
// the tick and compares the state after it.
int  CharProf_Begin(Object::Base* comp, int slot);
void CharProf_End(int token, long long tscTicks);
#if defined(__aarch64__)
static inline long long CompProf_Now() {
    uint64_t ticks;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(ticks));
    return static_cast<long long>(ticks);
}
#else
#include <x86intrin.h>
static inline long long CompProf_Now() { return (long long)__rdtsc(); }
#endif
// One line of sim context (world frame, component Update in flight) for diagnostics
// that fire from another thread (the redzone heap watchdog).
void StressHarness_Context(char* buf, int n);

// ── ELPROF verification shadows (BANISHED_STRESS_COMPPROF=4) ─────────────────────────
// g_verifyOn is 1 only at COMPPROF level 4 and 0 otherwise; a verified site reads it once
// (one predictable branch) and, when set, computes the retail answer beside the fast one
// and reports the comparison here. Counted per `what` (a static string); the first few
// mismatches print `detail`. Totals go to the [COMPPROF] report and standalone_error.log.
extern int g_verifyOn;
void StressHarness_Verify(const char* what, bool ok, long long detail);

// ── PATHFINDING benchmark (PFBENCH; docs/extensions/handins/PFBENCH.md) ─────────────
// g_pfProfOn is 1 while the stress harness runs (BANISHED_STRESS) and 0 otherwise; it is
// the ONLY thing the pathfinder reads when the harness is off (one predictable branch at
// each hook, none inside the search). Ticks are RDTSC (CompProf_Now), converted with the
// harness's TSC/QPC ratio; enqueue (main thread) and search start (worker) are compared
// across threads, which needs an invariant TSC (every x86-64 desktop CPU since ~2008).
extern int g_pfProfOn;
// Main thread, PathfinderThread::RequestPath, BEFORE the command is queued: stamp the
// result slot's enqueue time and record the queue depth the request lands behind.
void PfProf_Enqueue(int slot, int queueDepth);
// Worker, ApplyCommand case 5, after the search body: t0/t1 bracket the search only;
// cells = the result length (0 = no path); expanded = nodes expanded, read from the
// search's own state AFTER the run (never counted inside the search).
void PfProf_Search(int slot, long long t0, long long t1, int cells, int expanded);
// Main thread, PathfinderThread::TakePath: end-to-end latency (enqueue -> taken).
void PfProf_Taken(int slot);
// E2E SPLIT: the worker's publish (ready = 1) for the slot; ready -> TakePath is the poll.
void PfProf_Ready(int slot);
// Main thread brackets: which = PF_MAIN_PATHDISTANCE / _FILLZONES / _SYNCWAIT.
enum { PF_MAIN_PATHDISTANCE = 0, PF_MAIN_FILLZONES = 1, PF_MAIN_SYNCWAIT = 2 };
void PfProf_Main(int which, long long ticks);
// HITCH_PATHGRAPH (docs/extensions/handins/HITCH_PATHGRAPH.md): one [Stress::pathgraph]
// line per PathGraph drain (ProcessPaths' tail, once per rebuild = every 300 sim s). pairs =
// the rebuild's pair count; the two ticks bracket ReassignWorkers and AssignCitizens; the
// counts are AssignCitizens' cost queries (0x1400d6060): region-pair tree hits, misses,
// the Euclidean / unreachable short-cuts, and (COMPPROF=4 only, else -1) how many cached
// answers differ from the exact per-cell PathDistance cost the port used before.
// The counts are bumped by AssignCitizens (only while g_pfProfOn) and read and zeroed by
// ProcessPaths' tail when it reports.
struct PathGraphCostCounts { int calls, hits, misses, direct, differ; };
extern PathGraphCostCounts g_pathGraphCost;
void StressHarness_PathGraphDrain(int pairs, long long reassignTicks, long long assignTicks,
                                  int costCalls, int cacheHits, int cacheMisses, int direct,
                                  int differ);
// ext.fast_paths's shadow mode (worker thread, FastPaths_ShadowHook in fast_paths.cpp): one
// paired comparison of retail's grid.Run and the extension's body on the same request. Costs
// are the sum of step costs along each path's cells; sameCells = 1 when the cells match exactly.
// The pf_shadow_* columns stay 0 while the shadow mode is off (BANISHED_STRESS_COMPPROF=3 arms it).
void StressHarness_PathShadow(long long retailTicks, long long extTicks,
                              int retailExpanded, int extExpanded,
                              long long retailCost, long long extCost, int sameCells);

}

// Scope bracket for the main-thread pathfinding costs (PathDistance, FillZones, the
// synchronous road-tool / boat spin). Reads g_pfProfOn once; nothing else when it is 0.
struct PfMainTimer {
    int which; long long t0;
    explicit PfMainTimer(int w) : which(w), t0(g_pfProfOn ? CompProf_Now() : 0) {}
    ~PfMainTimer() { if (t0) PfProf_Main(which, CompProf_Now() - t0); }
    PfMainTimer(const PfMainTimer&) = delete;
    PfMainTimer& operator=(const PfMainTimer&) = delete;
};
