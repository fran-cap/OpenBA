// src/diag/stress_harness.cpp
// BANISHED_STRESS — large-scale stress test + performance baseline. See stress_harness.h
// for the knob list. Three responsibilities live here:
//
//   1. ENV BUNDLE (StressHarness_Init, from WinMain before anything reads env): turn the
//      single BANISHED_STRESS=1 into the autostart/medium-map/uncapped/pinned-camera
//      configuration a baseline needs, without overriding anything the user set.
//      The WORLD content (buildings, citizens, food) is built by starttown.cpp, which
//      asks this module for its targets — see StartTown_StressSpawnStructures and the
//      stress-family loop in StartTown_SpawnCitizens.
//
//   2. METRICS: wall-clock brackets around GameUpdateLoop::Update and ::Draw, the
//      SIMPROF phase split (taken from gameupdateloop.cpp's accumulators), a population/
//      employment/entity census, process memory, and a per-component-type Update profile
//      fed from EntityList_Update's dispatch loop. One CSV row per BANISHED_STRESS_INTERVAL
//      frames to standalone_stress.csv; the component profile goes to
//      standalone_stress_compprof.txt. tools/stress_report.py turns the CSV into a
//      summary and a baseline diff.
//
//   3. STAFFING: a periodic pass that seats workers at every workplace up to its authored
//      _defaultWorkers through the REAL seat cascade (CitizenEmploy_SeatWorkers, the same
//      cbf0->ca20->c5f0->c4b0 chain a spinner click runs). Nothing is assigned by hand;
//      after the seat everything is organic. A workplace that is not yet accepting
//      (state-13 enter for the ResourceProduce family) is simply retried next pass.
//
// Everything here is fprintf(stderr)-based so it survives the release log strip.
#include "stress_harness.h"
#include "options.h"
#include "load_time.h"   // HITCH: LoadTime::CounterToMs (the sampler's clock) for the hitch rows
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName / GetTypeNameById
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>   // std::hash (the /part row's seen table)
#include <vector>
#include "Component/CitizenAIComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/exe_bodies.h"   // AttendanceComponent_GetJobHostComponent (0x140002e10)
#include "Work/exe_bodies.h"
#include "Entity/ComponentEntity.h"         // B4: FindComponent<T>
#include "Entity/EntityList.h"              // B4: GameUpdateLoop::m_entityList
#include "Entity/CitizenBehavior.h"         // B4: GameUpdateLoop::m_citizenList
#include "Component/CharacterComponent.h"   // B4: CitizenComponent::character
#include "Component/CitizenDescription.h"   // B4: CitizenComponent::description
#include "Component/WorkPlaceComponent.h"
#include "Component/StorageComponent.h"        // FOOD: the [Stress::food] census
#include "Component/ResidenceComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/WorkComponent.h"
#include "Component/WorkDescription.h"
#include "Game/PlayerToolManager.h"         // B4: GameUpdateLoop::m_toolManager
#include "ext/fast_paths/fast_paths.h"      // PFBENCH: shadow mode switch (COMPPROF level 3)
#include "Component/ModelComponent.h"       // OFFSCREEN: the character's transform / yaw
#include "Action/MoveTo.h"                  // OFFSCREEN: the class of a character's top chain
#include "Action/PlayAnimation.h"
#include "Action/PerformWork.h"
#include "Action/AnimateNaturalResource.h"
#include "ext/render_culling/render_culling.h"                // OFFSCREEN: DiagPointVisible, RootOnlyNow
#include "char_prof.h"                      // OFFSCREEN: the /char window (header-only, unit-tested)
#include "Component/still_path.h"   // STILLPATH: StillPath::g_fastTicks (the /char stillpath rows)
#include "Component/sm_memo.h"      // SMMEMO: SmMemo::g_skipped / ProfWindow (the /sm row)
#include "Component/still_terrain.h" // STILLTERRAIN: the /still-z row
#include "Component/StateMachineComponent.h"   // SMMEMO: StateMachineComponent::TypeId
#include "Component/ParticleComponent.h"       // PARTREPEAT: the /part row (first vs repeat ticks)
#include "graphics/ParticleInstance.h"         // PARTREPEAT: the emitter instances' pool records
#include "collection/setsize_prof.h"   // DRAWSORT: the SetSizeCore loop counter
#include "graphics/draw_sort.h"        // DRAWSORT: the draw-item sort counters and shadow hook
#include "volume/frustum_cull.h"   // FRUSTUMCULL: the scene cull counters and its COMPPROF=4 shadow hook
#include "graphics/gather_prefetch.h"   // GATHERPF: the faithful gather's counters (the `gather` row)
#include "graphics/skin_memo.h"    // SKINMEMO: the bone memo counters (/skin row) and its COMPPROF=4 shadow hook
#include "ext/draw_gather/draw_gather.h"   // DRAWGATHER: the [Stress::gather] counters
#include "ext/render_pipe/render_pipe.h"   // RENDERPIPE: the [Stress::pipe] counters
#include "platform/common/cpu/topology.h"             // CPULIMIT: the '# cpu' meta line
#include "ext/walk_batch/walk_batch.h"   // WALKBATCH: the /char walkbatch row's counters
#include "ext/spread_warmth/spread_warmth.h"   // HITCH_PFBURST: the [Stress::warmth] counters
#include "ext/drain_split/drain_split.h"       // HITCH_DRAINSPLIT: the [Stress::drainsplit] counters
#define WIN32_LEAN_AND_MEAN
#if defined(_WIN32)
#include <windows.h>
#include <intrin.h>
#elif defined(__x86_64__)
#include <x86intrin.h>   // __rdtsc (Phase D.4 LINUX-1)
#endif
#include "platform_time.h"   // Platform::PerfCounter / PerfFrequency / LocalTimeNow
#if defined(_WIN32)
#define PSAPI_VERSION 2
#include <psapi.h>
#endif
#include "Component/AttendanceComponent.h"   // B4 S4 RT2: InternalPtr<AttendanceComponent>::Get() needs the class complete
#include "pf_stats.h"   // PFBENCH: the pf_* window aggregation (header-only, unit-tested)
#include <atomic>
#include <mutex>

// gameupdateloop.cpp — SIMPROF phase accumulators reserved for this harness.
extern "C" int         GameUpdateLoop_SimProfTake(double* outMs, int maxN);
extern "C" const char* GameUpdateLoop_SimProfName(int phase);

int g_compProfOn = 0;
int g_verifyOn = 0;   // ELPROF verification shadows (COMPPROF level 4)

// ── verification counters ──────────────────────────────────────────────────────────────
// RENDERPIPE-1: ext.render_pipe's render thread reports shadows too (RenderPipe,
// RenderPipeFrame, SkinBones inside pass 2), concurrently with the main thread's
// (DrawItemSort in pass 1, RenderPipeIdem, ...): the table is under a lock.
namespace {
struct VerifySlot { const char* what; long long checks, mismatches; };
constexpr int kVerifySlots = 32;   // 18 names after UNSTAMP; a full table drops new names silently
VerifySlot s_verify[kVerifySlots];
int s_verifyN = 0;
std::mutex s_verifyLock;
}

void StressHarness_Verify(const char* what, bool ok, long long detail) {
    std::lock_guard<std::mutex> lock(s_verifyLock);
    VerifySlot* v = nullptr;
    for (int i = 0; i < s_verifyN; ++i) if (s_verify[i].what == what) { v = &s_verify[i]; break; }
    if (!v) {
        if (s_verifyN >= kVerifySlots) return;
        v = &s_verify[s_verifyN++];
        *v = VerifySlot{ what, 0, 0 };
    }
    ++v->checks;
    if (ok) return;
    ++v->mismatches;
    if (v->mismatches <= 5)
        std::fprintf(stderr, "[VERIFY] MISMATCH %s #%lld detail=%lld\n", what, v->mismatches, detail);
}
int g_pfProfOn = 0;   // PFBENCH: 1 while the harness runs (Resolve); the pathfinder's only read when off
// entitylist.cpp — EntityList_Update sub-phase accumulators (QPC ticks / counts).
extern long long g_elProfDrainTicks, g_elProfPreTicks, g_elProfTreeTicks, g_elProfBucketTicks;
extern long long g_elProfTreeNodes, g_elProfBucketVisits, g_elProfBucketTicked;

namespace {

// ── configuration ────────────────────────────────────────────────────────────
int   s_enabled   = -1;      // -1 unresolved
int   s_citizens  = 800;
int   s_sets      = 3;
int   s_frames    = 6000;
int   s_interval  = 600;
int   s_speed     = 10;
int   s_foodMult  = 0;       // 0 = derive from citizens
int   s_staff     = 1;
char  s_csvPath[260] = "standalone_stress.csv";   // 260 = MAX_PATH (Win32), spelled out for POSIX

// EnvTrue/EnvInt/SetIfUnset and the stress-town default block used to live here;
// they moved to src/app/options.cpp (OptionsInit / ApplyStressDefaults) as
// part of ENDGAME C.3. This just copies the already-resolved fields.
void Resolve() {
    if (s_enabled >= 0) return;
    s_enabled  = Opt().stress ? 1 : 0;
    if (!s_enabled) return;
    s_citizens   = Opt().stressCitizens;
    s_sets       = Opt().stressSets;
    s_frames     = Opt().stressFrames;
    s_interval   = Opt().stressInterval;
    s_speed      = Opt().stressSpeed;
    s_foodMult   = Opt().stressFoodMult;
    s_staff      = Opt().stressStaff;
    {
        // 0 off, 1 bucket Updates, 2 + per-frame slot-8 ticks; 3 = level 1 + ext.fast_paths's
        // SHADOW MODE (retail's search drives the game, the fast search runs on the same
        // request and the pair goes to the pf_shadow_* columns). An existing flag's new
        // level, not a new flag (PATHRESEARCH section 6 Q4).
        const int cp = Opt().stressCompProf;
        g_compProfOn = cp >= 3 ? 1 : cp;
        if (cp == 3) Ext::FastPaths::SetShadowArmed(true);
        g_verifyOn = cp == 4 ? 1 : 0;   // ELPROF verification shadows
        if (cp >= 1) rtcoll::g_setSizeProfOn = 1;   // DRAWSORT: counted on the thread DrawBegin marks
        if (cp >= 1) DrawSort::g_profOn = 1;         // DRAWSORT: the draw-item sort row
        if (cp == 4) {   // FRUSTUMCULL: the engine's scene cull reports through this hook
            g_frustumCullReport = [](bool ok, long long detail) { StressHarness_Verify("FrustumCull", ok, detail); };
            g_frustumCullVerify = 1;
            // SKINMEMO: every memo hit recomputed with the faithful GetBones, all `count` matrices compared
            SkinMemo::g_report = [](bool ok, long long detail) { StressHarness_Verify("SkinBones", ok, detail); };
            SkinMemo::g_verify = 1;
            // DRAWSORT: the draw-item sort's shadow (retail's sort on a copy, whole arrays compared)
            DrawSort::g_report = [](bool ok, long long detail) { StressHarness_Verify("DrawItemSort", ok, detail); };
            DrawSort::g_verify = 1;
        }
        SkinMemo::g_prof = cp >= 1 ? 1 : 0;   // SKINMEMO: the /skin row's RDTSC split and the still census
    }
    g_pfProfOn   = 1;   // PFBENCH: the pathfinding columns are part of every stress row
    if (Opt().stressCsv) {
        std::strncpy(s_csvPath, Opt().stressCsv, sizeof(s_csvPath) - 1);
        s_csvPath[sizeof(s_csvPath) - 1] = 0;
    }
}

// ── timing ───────────────────────────────────────────────────────────────────
long long QpcNow() { int64_t t; Platform::PerfCounter(&t); return t; }
double QpcMs(long long ticks) {
    static long long freq = 0;
    if (!freq) { int64_t f; Platform::PerfFrequency(&f); freq = f; }
    return freq ? 1000.0 * (double)ticks / (double)freq : 0.0;
}
// Per-component ticks are RDTSC (CompProf_Now in stress_harness.h): QueryPerformanceCounter
// goes through KERNELBASE/ntdll and its ~600 calls per iteration were ~4% of the sim at
// 800x10. Convert with a TSC/QPC ratio measured over the process lifetime (first call = t0).
double TscMs(long long tsc) {
    static long long tsc0 = 0, qpc0 = 0;
    if (!tsc0) { int64_t q; Platform::PerfCounter(&q); qpc0 = q; tsc0 = CompProf_Now(); return 0.0; }
    int64_t q; Platform::PerfCounter(&q);
    const long long dt = CompProf_Now() - tsc0, dq = q - qpc0;
    if (dt <= 0 || dq <= 0) return 0.0;
    return QpcMs((long long)((double)tsc * ((double)dq / (double)dt)));
}

long long s_runT0        = 0;     // first Update of the first stress world
long long s_updT0        = 0;
long long s_drawT0       = 0;
long long s_lastDrawEntry = 0;
// Window accumulators (reset every CSV row).
double s_updMsSum = 0, s_drawMsSum = 0;
int    s_updN = 0, s_drawN = 0;
// RENDERPIPE: the split draw column (pipelined frames only; see stress_harness.h).
// The main half's brackets are the main thread's. RENDERPIPE-1: the packet and emit
// brackets run on the EMITTING thread (the render thread, or the main thread in the
// sequential split): their window sums are atomics, folded at EmitRow; their T0s are
// touched only by the emitting thread (successive emits are ordered by the join).
double s_pipeMainSum = 0;
int    s_pipeMainN = 0;
std::atomic<long long> s_pipePacketTicks{0}, s_pipeEmitTicks{0};
std::atomic<int>       s_pipePacketN{0}, s_pipeEmitN{0};
long long s_pipePacketT0 = 0, s_pipeEmitT0 = 0;
void PipeWindowReset() {
    s_pipeMainSum = 0; s_pipeMainN = 0;
    s_pipePacketTicks.store(0, std::memory_order_relaxed); s_pipePacketN.store(0, std::memory_order_relaxed);
    s_pipeEmitTicks.store(0, std::memory_order_relaxed);   s_pipeEmitN.store(0, std::memory_order_relaxed);
}
long long s_simIters = 0;
std::vector<float> s_updSamples;   // per-Update ms, for p95
std::vector<float> s_frameSamples; // Draw entry-to-entry ms, for p95 / fps
long long s_lastRow = 0;           // QPC at the previous row (wall fps)

// ── HITCH: the per-frame hitch recorder (perf/README.md "Hitches") ───────────────────
// A frame is one Draw-entry-to-Draw-entry period: the on-screen gap, which is what a
// hitch IS. Inside it: the previous frame's Draw, the time outside Update/Draw (Present,
// the pump, the limiter) and 0..k Updates (MainLoop::Frame's accumulator runs more than one
// when the previous frame was long, so a hitch cascades). Every period's split is kept in
// 128-deep rings; when a period exceeds the ring's median by more than
// Opt().stressHitchMs (BANISHED_STRESS_HITCH_MS, default one 140 fps frame), the period,
// its split, the rings' medians (so a reader sees WHICH part grew) and the harness's own
// markers go to standalone_stress_hitch.csv. t0/t1 are the [LoadTime] clock (ms since
// process start), the clock the sampler stamps its lines with, so tools/hitch_report.py
// can pull the samples that fell inside a hitch. Cost while armed: one SimProf drain per
// Update and one 128-element nth_element per frame; nothing in tick code.
constexpr int kHitchRing = 128;
constexpr int kHitchWarm = 12;     // periods of a class before its median is trusted (the first ~0.2 s is blind)
constexpr int kHitchPh   = 6;      // SIMPROF phases carried per row (PairCache .. PostSim)
enum HitchMarker { kHmStaff = 1, kHmRow = 2, kHmFrame1800 = 4, kHmMultiUpdate = 8, kHmNewWorld = 16 };
struct HitchRing {
    float v[kHitchRing]; int n = 0, at = 0;
    void Push(double x) { v[at] = (float)x; at = (at + 1) % kHitchRing; if (n < kHitchRing) ++n; }
    double Median() const {
        if (n == 0) return 0.0;
        float t[kHitchRing]; std::memcpy(t, v, sizeof(float) * (size_t)n);
        std::nth_element(t, t + n / 2, t + n);
        return t[n / 2];
    }
};
struct HitchCur {   // the period being accumulated
    int updates = 0; int speed = 0; int frame = 0; int markers = 0;
    double updMs = 0, drawMs = 0, harnessMs = 0; double ph[kHitchPh] = {0}; long long pf[3] = {0};
};
// Two classes of period, with their own rings: MainLoop::Frame draws every loop and Updates
// only when the fixed-step accumulator has a step, so at speed 50 a draw-only period is ~1 ms
// and an Update period ~8 ms; comparing one against the other's median reads the steady
// cadence as a hitch. A period with k >= 2 Updates (the cascade) is held against the
// one-Update median plus (k - 1) more Updates at the one-Update update median.
struct HitchClass { HitchRing period, upd, draw, out, ph[kHitchPh]; };
HitchCur   s_hCur;
HitchClass s_hCls[2];              // [0] no Update in the period, [1] one or more
double    s_phWindow[8] = {0};     // SimProf ms since the last CSV row (drained per Update)
FILE*     s_hitchCsv = nullptr;
// Window stats: the stress CSV's appended columns and the [Stress::hitch] line.
double s_hWinMax = 0, s_hWinExcessMax = 0, s_hWinExcessSum = 0;
int    s_hWinN = 0, s_hWinFrames = 0, s_hWinHist[8] = {0};   // excess in [t,2t) [2t,4t) ... [64t,inf)
long long s_hTotal = 0;

bool HitchOn() { return Opt().stressHitchMs > 0.0; }
void HitchReset() {
    s_hCur = HitchCur();
    for (HitchClass& c : s_hCls) c = HitchClass();
    for (double& p : s_phWindow) p = 0;
}
void HitchDrainPhases() {   // every Update: SimProf -> the period record + the row window
    double ph[8] = {0};
    const int n = GameUpdateLoop_SimProfTake(ph, 8);
    for (int i = 0; i < n; ++i) { s_phWindow[i] += ph[i]; if (i < kHitchPh) s_hCur.ph[i] += ph[i]; }
}
void HitchOpenCsv() {
    if (s_hitchCsv) return;
    s_hitchCsv = std::fopen("standalone_stress_hitch.csv", "w");
    if (!s_hitchCsv) { std::fprintf(stderr, "[Stress::hitch] cannot open standalone_stress_hitch.csv\n"); return; }
    std::fprintf(s_hitchCsv, "# hitch rows: a Draw-to-Draw period more than %.3f ms over its baseline (the rolling median of %d "
                             "periods of the same class: no Update / with Updates; k Updates = median + (k-1) x median update); "
                             "med_* = the class medians; outside = period - update - draw (harness = the stress harness's own "
                             "row / staffing work, inside outside); t0/t1 = [LoadTime] ms\n",
                 Opt().stressHitchMs, kHitchRing);
    std::fprintf(s_hitchCsv, "wall_s,frame,speed,t0_ms,t1_ms,period_ms,med_period_ms,baseline_ms,excess_ms,updates,"
                             "update_ms,med_update_ms,draw_ms,med_draw_ms,outside_ms,med_outside_ms,harness_ms,"
                             "ph_paircache_ms,ph_animation_ms,ph_entitylist_ms,ph_weather_ms,ph_citizenlist_ms,ph_postsim_ms,"
                             "med_paircache_ms,med_animation_ms,med_entitylist_ms,med_weather_ms,med_citizenlist_ms,med_postsim_ms,"
                             "pf_pathdistance_ms,pf_fillzones_ms,pf_syncwait_ms,markers\n");
    std::fflush(s_hitchCsv);
}
// Close the period [t0, t1) (QPC): test it, log it if it is a hitch, push it into the rings.
void HitchPeriod(long long t0, long long t1) {
    const double period = QpcMs(t1 - t0);
    const double out = period - s_hCur.updMs - s_hCur.drawMs;
    const int k = s_hCur.updates;
    if (k > 1) s_hCur.markers |= kHmMultiUpdate;
    HitchClass& cls = s_hCls[k > 0 ? 1 : 0];
    ++s_hWinFrames;
    if (period > s_hWinMax) s_hWinMax = period;
    const double thr = Opt().stressHitchMs;
    if (cls.period.n >= kHitchWarm) {
        const double med = cls.period.Median();
        const double baseline = med + (k > 1 ? (double)(k - 1) * cls.upd.Median() : 0.0);
        const double excess = period - baseline;
        if (excess > thr) {
            ++s_hWinN; ++s_hTotal; s_hWinExcessSum += excess;
            if (excess > s_hWinExcessMax) s_hWinExcessMax = excess;
            int b = 0; while (b < 7 && excess >= thr * (double)(2 << b)) ++b;
            ++s_hWinHist[b];
            HitchOpenCsv();
            if (s_hitchCsv) {
                const double wall = s_runT0 ? QpcMs(t1 - s_runT0) / 1000.0 : 0.0;
                std::fprintf(s_hitchCsv, "%.2f,%d,%d,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,",
                             wall, s_hCur.frame, s_hCur.speed, LoadTime::CounterToMs(t0), LoadTime::CounterToMs(t1),
                             period, med, baseline, excess, k,
                             s_hCur.updMs, cls.upd.Median(), s_hCur.drawMs, cls.draw.Median(), out, cls.out.Median(),
                             s_hCur.harnessMs);
                for (int i = 0; i < kHitchPh; ++i) std::fprintf(s_hitchCsv, "%.3f,", s_hCur.ph[i]);
                for (int i = 0; i < kHitchPh; ++i) std::fprintf(s_hitchCsv, "%.3f,", cls.ph[i].Median());
                std::fprintf(s_hitchCsv, "%.3f,%.3f,%.3f,%d\n",
                             TscMs(s_hCur.pf[PF_MAIN_PATHDISTANCE]), TscMs(s_hCur.pf[PF_MAIN_FILLZONES]),
                             TscMs(s_hCur.pf[PF_MAIN_SYNCWAIT]), s_hCur.markers);
                std::fflush(s_hitchCsv);
            }
        }
    }
    // Only a one-Update period teaches the Update-class rings (a cascade would double them).
    if (k <= 1) {
        cls.period.Push(period); cls.upd.Push(s_hCur.updMs); cls.draw.Push(s_hCur.drawMs); cls.out.Push(out);
        for (int i = 0; i < kHitchPh; ++i) cls.ph[i].Push(s_hCur.ph[i]);
    }
    s_hCur = HitchCur();
}

// World-relative frame counter: s_frameNum is a process counter.
void* s_world       = nullptr;
int   s_worldSeenAt = 0;
int   s_worldFrame  = 0;
int   s_worldNo     = 0;

FILE* s_csv = nullptr;
FILE* s_compOut = nullptr;

// ── PFBENCH: pathfinding window state ──────────────────────────────────────────
// Enqueue stamps, one per result slot (RDTSC). The main thread writes a slot's stamp
// BEFORE RequestPath queues the command (EnqueueNode takes the pathfinder's lock); the
// worker reads it after it dequeued that command under the same lock, so the lock pair
// orders the two. A slot past the table gets no wait sample (counted as a request).
constexpr int kPfSlots = 8192;
long long s_pfEnqTsc[kPfSlots];
long long s_pfReadyTsc[kPfSlots];   // E2E SPLIT: the worker's publish stamp
int       s_pfEnqFrame[kPfSlots];   // E2E SPLIT: Update frame at the enqueue
int       s_pfFrame = 0;            // E2E SPLIT: Update frames since the harness began
// The worker's window and the main thread's e2e / queue window, under one lock taken
// once per request event (enqueue, search done, take) and once per report row; never
// per node. The three main-thread brackets are relaxed atomics (PathDistance runs many
// times a frame; no lock there).
std::mutex            s_pfLock;
PfStats::Window       s_pfWin;
PfStats::MainWindow   s_pfMain;
std::atomic<long long> s_pfMainTicks[3];   // PF_MAIN_PATHDISTANCE / _FILLZONES / _SYNCWAIT
// Simulated time in the window: the per-frame m_gameTime step (System::Time ticks,
// 1,000,000 per second: engine/system/timer_internal.h) times that frame's speed.
long long s_simTicks = 0;
long long s_lastGameTime = -1;
constexpr double kTimeTicksPerSecond = 1000000.0;

void PfResetWindow() {
    std::lock_guard<std::mutex> g(s_pfLock);
    s_pfWin = PfStats::Window();
    s_pfMain = PfStats::MainWindow();
    for (auto& t : s_pfMainTicks) t.store(0, std::memory_order_relaxed);
}

bool Readable(const void* p) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);  // CAST-OK: ptr_int
    return a >= 0x10000 && a < 0x00007FFFFFFFFFFFULL;
}

float Pct(std::vector<float>& v, double p) {
    if (v.empty()) return 0.0f;
    size_t k = (size_t)((v.size() - 1) * p);
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

// ── census ───────────────────────────────────────────────────────────────────
struct Census {
    int citizens = 0, home238 = 0, work248 = 0, busy = 0, entities = -1;
    double ageMean = 0, hungerMean = 0;
};

Census TakeCensus(GameUpdateLoop* gw) {
    Census c;
    if (!Readable(gw)) return c;
    EntityList* el = gw->m_entityList;                 // +0xE8
    if (Readable(el)) {
        int n = el->masterArray._size;                 // +0x10
        if (n >= 0 && n < 10000000) c.entities = n;
    }
    CitizenBehavior* town = gw->m_citizenList;         // +0xF0
    if (!Readable(town)) return c;
    Object::InternalPtr<CitizenComponent>* arr = town->citizens._data;           // +0xA0
    const int n = town->citizens._size;                  // +0xA8
    if (!arr || n <= 0 || n > 100000) return c;
    double age = 0, hunger = 0;
    for (int i = 0; i < n; ++i) {
        CitizenComponent* cz = arr[i].Get();
        if (!Readable(cz)) continue;
        ++c.citizens;
        age    += cz->age;                             // +0x3C
        hunger += cz->hunger;                          // +0x44
        if (cz->home) ++c.home238;                     // +0x238
        if (cz->workplace) ++c.work248;                // +0x248
        CharacterComponent* ch = cz->character.Get();        // +0x278
        if (Readable(ch) && ch->actionStack.current >= 0)    // +0x60
            ++c.busy;
    }
    if (c.citizens) { c.ageMean = age / c.citizens; c.hungerMean = hunger / c.citizens; }
    return c;
}

#if defined(_WIN32)
void MemoryMB(double* ws, double* priv) {
    PROCESS_MEMORY_COUNTERS_EX pm; std::memset(&pm, 0, sizeof(pm)); pm.cb = sizeof(pm);
    *ws = *priv = 0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof(pm))) {  // CAST-OK: win32
        *ws   = (double)pm.WorkingSetSize / (1024.0 * 1024.0);
        *priv = (double)pm.PrivateUsage   / (1024.0 * 1024.0);
    }
}
#else
// POSIX (Phase D.4 LINUX-1): the working set is /proc/self/status VmRSS; the private bytes
// are RssAnon (anonymous resident memory: the nearest Linux figure to PrivateUsage, which
// counts commit charge, so a reserved-and-committed page that was never touched is in the
// Windows number and not in this one).
void MemoryMB(double* ws, double* priv) {
    *ws = *priv = 0;
    FILE* f = std::fopen("/proc/self/status", "r");
    if (f == nullptr) return;
    char line[256];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        unsigned long long kb = 0;
        if (std::sscanf(line, "VmRSS: %llu kB", &kb) == 1) *ws = (double)kb / 1024.0;
        else if (std::sscanf(line, "RssAnon: %llu kB", &kb) == 1) *priv = (double)kb / 1024.0;
    }
    std::fclose(f);
}
#endif

// ── per-component-type profile ───────────────────────────────────────────────
struct CompSlot { void* vtbl; uint32_t typeId; long long ticks; long long calls; int kind; };
constexpr int kCompSlots = 1024;          // power of two; ~200 component types exist
CompSlot s_comp[kCompSlots];

// ── OFFSCREEN: characters by class x visibility (COMPPROF=2; src/diag/char_prof.h) ──
// Fed from the slot-8 tick's profiling arm (entitylist.cpp TickOneComponent). One visit =
// one entity in one walk (keyed on the entity and g_elProfTreeNodes, which the walk bumps
// after each entity): the class is read once per visit, BEFORE the first of its ticks,
// so a ModelComponent ticked before or after its CharacterComponent gets the same class.
CharProf::Window s_charWin;
uint32_t s_tidChar = 0, s_tidModel = 0;
uint32_t s_tidMoveTo = 0, s_tidPlayAnim = 0, s_tidPerformWork = 0, s_tidAnimNR = 0;
bool     s_charTidsResolved = false;
struct CharVisit {
    const ComponentEntity* ent; long long node;
    int cls, vis; uint32_t nodeType;
};
CharVisit s_charVisit = { nullptr, -1, 0, 0, 0 };
struct CharPending {
    CharacterComponent* ch;
    ModelComponent* model;
    Math::Quaternion rot; Math::Vector3 pos; float yaw;
    int current; const Action* head;
    bool last;
    long long fast0;   // STILLPATH: StillPath::g_fastTicks before the tick
    long long tzQ0, tzH0;   // STILLTERRAIN: StillTerrain::g_queries / g_hits before the tick
};
CharPending s_charPend = {};
// SMMEMO (Component/sm_memo.h): the StateMachineComponent slot-8 ticks, skipped vs full.
SmMemo::ProfWindow s_smWin;
uint32_t  s_tidSM = 0;
long long s_smSkip0 = 0;   // SmMemo::g_skipped before the bracketed tick
// STILLTERRAIN (Component/still_terrain.h): the last-iteration character ticks that reached
// the terrain memo test, hit vs full; the floor declines and the epoch bumps of the window.
StillTerrain::ProfWindow s_tzWin;
uint64_t  s_tzEpoch0 = 0;
long long s_tzFloor0 = 0;

// PARTREPEAT Step 0 (docs/extensions/handins/PARTREPEAT.md section 0): the ParticleComponent
// slot-8 ticks split into the first tick of a render frame and the repeat ticks (speed N runs
// the walk N times per render frame), and the "quiet" repeats: every emitter with an instance
// has nodeIndex < 0, and every live one (sceneId >= 0) has fadeRate == 0, opacity > 0 and a
// pool already stamped this ITERATION (resource active, frameTag == ParticleInstance::_frameIndex,
// which gameupdateloop.cpp step 3h advances once per sim iteration), i.e. Update would take its
// SetBounds early exit. Such a tick is NOT a no-op: the pool was re-simulated earlier in the
// iteration by its first toucher, so SetBounds copies new bounds (PARTREPEAT.md section 1, why
// the skip was not built). Read before the tick, outside the bracket.
struct PartWindow {
    long long firstCalls = 0, firstTicks = 0;
    long long repCalls = 0, repTicks = 0;
    long long quietCalls = 0, quietTicks = 0, quietLive = 0;
    // UNSTAMP: the un-stamp's Component::_pad24 frame stamp, read before / after the tick.
    // hit = it already held this frame's token (the un-stamp returned at once); first = the
    // tick wrote it; none = neither (PC_Culled returned first, or kUnstampStamp is off).
    long long stampHits = 0, stampFirst = 0, stampNone = 0, lowered = 0;
    void Reset() { *this = PartWindow(); }
};
PartWindow s_partWin;
uint32_t   s_tidPart = 0;
bool       s_partRepeat = false, s_partQuiet = false;
int        s_partLive = 0;
const ParticleComponent* s_partPc = nullptr;   // UNSTAMP: the bracketed component, its stamp
uint32_t   s_partStamp0 = 0;                   // before the tick, the frame token, and the
int        s_partFrame = 0;                    // lowering count before the tick
long long  s_partLowered0 = 0;
struct PartSeen { const void* key; int frame; };
constexpr int kPartSeenCap = 16384;   // power of two; ~1,000 particle components in the heavy town
PartSeen   s_partSeen[kPartSeenCap];

void CharProfResolveTids() {
    if (s_charTidsResolved) return;
    s_charTidsResolved = true;
    s_tidSM          = StateMachineComponent::TypeId();
    s_tidPart        = ParticleComponent::TypeId();
    s_tidChar        = CharacterComponent::TypeId();
    s_tidModel       = ModelComponent::TypeId();
    s_tidMoveTo      = MoveTo::TypeId();
    s_tidPlayAnim    = PlayAnimation::TypeId();
    s_tidPerformWork = PerformWork::TypeId();
    s_tidAnimNR      = AnimateNaturalResource::TypeId();
}

// The character's class and visibility for this visit (char_prof.h Classify).
void CharProfClassify(CharacterComponent* ch, const ComponentEntity* ent) {
    CharVisit& v = s_charVisit;
    v.ent = ent; v.node = g_elProfTreeNodes;
    CharProf::Chain chain;
    uint32_t decider = 0;
    const ActionStack& st = ch->actionStack;
    const Action* head = st.current >= 0 ? st.actions[st.current].Get() : nullptr;
    if (head) {
        chain.empty = false;
        decider = head->Type();
        for (const Action* n = head; n; n = n->_next.Get()) {
            const uint32_t t = n->Type();
            if (t == s_tidMoveTo && !chain.hasMoveTo) {
                chain.hasMoveTo = true;
                chain.moveToPathIndex = static_cast<const MoveTo*>(n)->pathIndex;
                decider = t;
            } else if (!chain.hasMoveTo && !chain.hasTimed &&
                       (t == s_tidPlayAnim || t == s_tidPerformWork || t == s_tidAnimNR)) {
                chain.hasTimed = true;
                decider = t;
            }
        }
    }
    v.cls = CharProf::Classify(chain);
    v.nodeType = decider;
    const ModelComponent* model = ch->modelComponent.Get();
    v.vis = (model && !Ext::RenderCulling::DiagPointVisible(&model->transform.translation.x))
                ? CharProf::kOff : CharProf::kOn;
}

// ── home-loss watch ──────────────────────────────────────────────────────────
// The 10x town loses ~8% of its housed citizens per meal wave and the homeless
// then never eat and starve (hunger climbs linearly to the cap). Nothing in the
// port writes citizen+0x238 by name, so this catches the writer in the act: the
// component Update that ran when the home went null, plus the citizen's state.
uint32_t s_tidCitizen = 0, s_tidCitizenAI = 0;
bool     s_tidResolved = false;
void*    s_curComp = nullptr;        // component whose Update is running (hot loop)
uint32_t s_curTid  = 0;
CitizenComponent* s_curCitizen = nullptr;   // citizen watched during that Update
ComponentEntity*  s_curHome = nullptr;
// [HomeLoss] watch is opt-in since 2026-09-04 (BANISHED_STRESS_HOMEWATCH=1): the population
// collapse it was built for is closed, and its per-Update bookkeeping sat on every bucket tick.
bool s_homeWatch = false;
double TscMs(long long tsc);
void HomeWatchInit() {
    s_homeWatch = Opt().stressHomeWatch;
    TscMs(0);   // stamp the TSC/QPC calibration origin
}
int      s_homeLossLogged = 0;
std::vector<std::pair<CitizenComponent*, ComponentEntity*>> s_homeShadow;   // (citizen, home) last frame

void LogHomeLoss(CitizenComponent* cz, const char* where) {
    if (++s_homeLossLogged > 400) return;
    CharacterComponent* ch = cz->character.Get();                                // +0x278
    const int depth = Readable(ch) ? ch->actionStack.current : -99;              // +0x60
    CitizenDescription* desc = cz->description;                            // +0x288
    const float adultAge = Readable(desc) ? desc->adultAge : 0.f;          // +0x44
    const char* name = cz->name._data;                                     // +0x28 String's character data
    const unsigned long long prof =
        reinterpret_cast<uintptr_t>(cz->profession);  // CAST-OK: ptr_int
    std::fprintf(stderr, "[HomeLoss] frame=%d %s citizen=%p name=%s age=%.2f(adult>=%.1f) hunger=%.1f "
                         "state+0xb0=%d flags+0x54=0x%x +0x88=0x%x prof=0x%llx work248=%p parent250=%p "
                         "partner258=%p ring260=%p elder208=%p target228=%p depth=%d\n",
                 s_worldFrame, where, (void*)cz, Readable(name) ? name : "?",
                 cz->age, adultAge, cz->hunger,
                 (int32_t)cz->field_0xB0, cz->statusFlags,
                 cz->happinessFlags,
                 prof,
                 (void*)cz->workplace.Get(), (void*)cz->spouse.Get(),
                 (void*)cz->_parent.Get(), (void*)cz->_firstChild.Get(),
                 (void*)cz->school.Get(), (void*)cz->careBuilding.Get(), depth);
    std::fflush(stderr);
}

// Per-frame shadow compare of every registered citizen's home slot. Catches a clear
// that happened OUTSIDE a Citizen/CitizenAI Update (message handler, residence tick,
// entity removal) — the component hook cannot see those.
void HomeShadowScan(GameUpdateLoop* gw) {
    if (!Readable(gw)) return;
    CitizenBehavior* town = gw->m_citizenList;            // +0xF0
    if (!Readable(town)) return;
    Object::InternalPtr<CitizenComponent>* arr = town->citizens._data;              // +0xA0
    const int n = town->citizens._size;                     // +0xA8
    if (!arr || n <= 0 || n > 100000) return;
    std::vector<std::pair<CitizenComponent*, ComponentEntity*>> now;
    now.reserve(n);
    for (int i = 0; i < n; ++i) {
        CitizenComponent* cz = arr[i].Get();
        if (!Readable(cz)) continue;
        ComponentEntity* const home = cz->home.Get();           // +0x238
        now.emplace_back(cz, home);
        if (home) continue;
        for (const auto& pr : s_homeShadow)
            if (pr.first == cz) { if (pr.second) LogHomeLoss(cz, "OUTSIDE-UPDATE(or unattributed)"); break; }
    }
    s_homeShadow.swap(now);
}

// OFFSCREEN: the /char rows (char_prof.h; docs/extensions/handins/OFFSCREEN.md §4 reads
// them). Printed only when the window counted a character tick (COMPPROF=2). Must run
// before the window's EntityList accumulators are reset.
void CharProfReport(FILE* o) {
    const CharProf::Window& w = s_charWin;
    const long long chars = w.Chars(CharProf::kAllClasses, CharProf::kBothVis);
    if (chars == 0) return;
    const double n = (double)s_interval;
    // EntityList and the tree walk as the phase rows above print them (QPC).
    const double elMs   = QpcMs(g_elProfDrainTicks + g_elProfPreTicks + g_elProfTreeTicks + g_elProfBucketTicks) / n;
    const double treeMs = QpcMs(g_elProfTreeTicks) / n;
    // The walk's own cost per entity visit: the tree phase minus every /tree tick, over the
    // visits. Includes the profiler's own Begin/End work (ELPROF: ~0.3 ms/frame at 1600
    // citizens), so it overstates the walk.
    long long treeCompTicks = 0;
    for (int i = 0; i < kCompSlots; ++i) if (s_comp[i].vtbl && s_comp[i].kind == 1) treeCompTicks += s_comp[i].ticks;
    const double nodes = (double)g_elProfTreeNodes;
    double walkMsPerVisit = nodes > 0 ? (QpcMs(g_elProfTreeTicks) - TscMs(treeCompTicks)) / nodes : 0.0;
    if (walkMsPerVisit < 0) walkMsPerVisit = 0;
    const char* visName[2] = { "on ", "off" };
    std::fprintf(o, "[COMPPROF]   /char  CharacterComponent slot-8 ticks by visibility x class (start of tick); "
                    "EntityList %.3f ms/frame, treeWalk %.3f, walk est. %.1f ns/visit (incl. profiler)\n",
                 elMs, treeMs, walkMsPerVisit * 1e6);
    for (int v = 0; v < CharProf::kVisCount; ++v)
        for (int c = 0; c < CharProf::kClassCount; ++c) {
            const CharProf::Cell& e = w.cell[v][c];
            if (!e.chars) continue;
            const double cn = (double)e.chars;
            std::fprintf(o, "[COMPPROF]   /char %s %-8s chars/frame=%8.1f  char %.3f ms/frame %5.1f ns/call "
                            "(last-iter %5.1f)  model %.3f ms/frame  moved %5.1f%%  still %5.1f%% (last-iter %5.1f%%)  trans/frame %7.1f\n",
                         visName[v], CharProf::ClassName(c), cn / n,
                         TscMs(e.ticks) / n, 1e6 * TscMs(e.ticks) / cn,
                         e.lastCalls ? 1e6 * TscMs(e.lastTicks) / (double)e.lastCalls : 0.0,
                         TscMs(e.modelTicks) / n,
                         100.0 * (double)e.moved / cn, 100.0 * (double)e.still / cn,
                         e.lastCalls ? 100.0 * (double)e.lastStill / (double)e.lastCalls : 0.0,
                         (double)e.transitions / n);
        }
    // The bounds: character tick + its model tick + the walk estimate per visit, as ms/frame
    // and as a share of EntityList. "as if every character were off-screen" = both rows.
    struct B { const char* name; unsigned classes; };
    const B bounds[] = {
        { "walk",           1u << CharProf::kWalk },       // OFFSCREEN D1's ceiling (teleport)
        { "pathwait",       1u << CharProf::kPathWait },
        { "timed",          CharProf::kTimedClasses },     // E1's ceiling with idle (still fast path)
        { "idle",           1u << CharProf::kIdle },
        { "walk+timed+idle", (1u << CharProf::kWalk) | CharProf::kTimedClasses | (1u << CharProf::kIdle) },
        { "every class",    CharProf::kAllClasses },
    };
    // BoundTicks' walk term is 0 here: the walk estimate is in ms (QPC), added after.
    for (const B& b : bounds) {
        const double offMs = TscMs((long long)CharProf::BoundTicks(w, b.classes, CharProf::kOffOnly, 0.0)) / n +
                             walkMsPerVisit * (double)w.Chars(b.classes, CharProf::kOffOnly) / n;
        const double allMs = TscMs((long long)CharProf::BoundTicks(w, b.classes, CharProf::kBothVis, 0.0)) / n +
                             walkMsPerVisit * (double)w.Chars(b.classes, CharProf::kBothVis) / n;
        std::fprintf(o, "[COMPPROF]   /char bound %-20s off-screen %.3f ms/frame = %5.1f%% of EntityList; "
                        "if every character were off-screen %.3f = %5.1f%%\n",
                     b.name, offMs, elMs > 0 ? 100.0 * offMs / elMs : 0.0,
                     allMs, elMs > 0 ? 100.0 * allMs / elMs : 0.0);
    }
    // D2's ceiling (walk batch at speed N): the walkers' character ticks on the intermediate
    // iterations, every visibility (only the last iteration is drawn). Their model tick is
    // ext.anim_batch's and the walk visit stays, so neither is added.
    {
        long long t = 0, c = 0;
        for (int v = 0; v < CharProf::kVisCount; ++v) {
            const CharProf::Cell& e = w.cell[v][CharProf::kWalk];
            t += e.ticks - e.lastTicks; c += e.chars - e.lastCalls;
        }
        const double ms = TscMs(t) / n;
        std::fprintf(o, "[COMPPROF]   /char bound walk intermediate (D2) every visibility %.3f ms/frame = %5.1f%% of EntityList "
                        "(%.1f ticks/frame)\n", ms, elMs > 0 ? 100.0 * ms / elMs : 0.0, (double)c / n);
    }
    // E1's ceiling: the time of the ticks that left the character bit-for-bit where it was
    // (any class, any visibility). The model tick and the walk stay, so neither is added.
    {
        const double stillMs = TscMs(w.StillTicks(CharProf::kAllClasses, CharProf::kBothVis)) / n;
        const double stillTimedIdle = TscMs(w.StillTicks(CharProf::kTimedClasses | (1u << CharProf::kIdle),
                                                         CharProf::kBothVis)) / n;
        std::fprintf(o, "[COMPPROF]   /char bound still (E1)          every visibility %.3f ms/frame = %5.1f%% of EntityList "
                        "(timed+idle %.3f = %5.1f%%)\n",
                     stillMs, elMs > 0 ? 100.0 * stillMs / elMs : 0.0,
                     stillTimedIdle, elMs > 0 ? 100.0 * stillTimedIdle / elMs : 0.0);
    }
    // STILLPATH (still_path.h; docs/extensions/handins/STILLPATH.md section 5): the ticks the
    // exact still fast path took, what they cost, and the saving measured against the still
    // intermediate ticks that ran the full tail in the same window.
    {
        const CharProf::StillPathEstimate e = CharProf::EstimateStillPath(w);
        const long long stillAll = w.Sum(&CharProf::Cell::still);
        const long long inter = chars - w.Sum(&CharProf::Cell::lastCalls);
        const double savedMs = TscMs((long long)e.savedTicks) / n;
        std::fprintf(o, "[COMPPROF]   /char stillpath (E1) fast ticks/frame=%8.1f = %5.1f%% of intermediate char ticks, %5.1f%% of still ticks; "
                        "%.3f ms/frame %5.1f ns/call; full-tail still intermediate %8.1f/frame %5.1f ns/call; "
                        "est. saved %.3f ms/frame = %5.1f%% of EntityList\n",
                     (double)e.fast / n, inter > 0 ? 100.0 * (double)e.fast / (double)inter : 0.0,
                     stillAll > 0 ? 100.0 * (double)e.fast / (double)stillAll : 0.0,
                     TscMs(e.fastTicks) / n, e.fast ? 1e6 * TscMs(e.fastTicks) / (double)e.fast : 0.0,
                     (double)e.fullInter / n, e.fullInter ? 1e6 * TscMs(e.fullInterTicks) / (double)e.fullInter : 0.0,
                     savedMs, elMs > 0 ? 100.0 * savedMs / elMs : 0.0);
        std::fprintf(o, "[COMPPROF]   /char stillpath by class (fast ticks/frame, %% of the class's intermediate ticks):");
        for (int c = 0; c < CharProf::kClassCount; ++c) {
            long long f = 0, ci = 0;
            for (int v = 0; v < CharProf::kVisCount; ++v) {
                f += w.cell[v][c].fast; ci += w.cell[v][c].chars - w.cell[v][c].lastCalls;
            }
            std::fprintf(o, "  %s %.1f (%.1f%%)", CharProf::ClassName(c), (double)f / n,
                         ci > 0 ? 100.0 * (double)f / (double)ci : 0.0);
        }
        std::fprintf(o, "\n");
    }
    // WALKBATCH (ext.walk_batch, src/ext/walk_batch/walk_batch.h; docs/extensions/handins/
    // WALKBATCH.md section 5): the intermediate walker ticks the batched walk took, what they
    // charged, and against them the walk-class intermediate ticks that ran the full tick in the
    // same window (declined, or the extension off: then the batched count is 0).
    {
        long long interCalls = 0, interTicks = 0;
        for (int v = 0; v < CharProf::kVisCount; ++v) {
            const CharProf::Cell& e = w.cell[v][CharProf::kWalk];
            interCalls += e.chars - e.lastCalls; interTicks += e.ticks - e.lastTicks;
        }
        const long long bCalls = Ext::WalkBatch::g_profCalls, bTicks = Ext::WalkBatch::g_profTicks;
        const long long fullCalls = interCalls - bCalls, fullTicks = interTicks - bTicks;
        const double bNs = bCalls ? 1e6 * TscMs(bTicks) / (double)bCalls : 0.0;
        const double fullNs = fullCalls > 0 ? 1e6 * TscMs(fullTicks) / (double)fullCalls : 0.0;
        const double savedMs = (bCalls && fullCalls > 0) ? (fullNs - bNs) * (double)bCalls / 1e6 / n : 0.0;
        std::fprintf(o, "[COMPPROF]   /char walkbatch (D2) batched ticks/frame=%8.1f = %5.1f%% of the walk class's intermediate ticks; "
                        "%.3f ms/frame %5.1f ns/call; full walk intermediate %8.1f/frame %5.1f ns/call; "
                        "est. saved %.3f ms/frame = %5.1f%% of EntityList\n",
                     (double)bCalls / n, interCalls > 0 ? 100.0 * (double)bCalls / (double)interCalls : 0.0,
                     TscMs(bTicks) / n, bNs, (double)(fullCalls > 0 ? fullCalls : 0) / n, fullNs,
                     savedMs, elMs > 0 ? 100.0 * savedMs / elMs : 0.0);
    }
    // The node types that decided the class, by time (top 14).
    int idx[CharProf::kMaxNodes];
    for (int i = 0; i < w.nNodes; ++i) idx[i] = i;
    std::sort(idx, idx + w.nNodes, [&w](int a, int b) { return w.nodes[a].ticks > w.nodes[b].ticks; });
    for (int k = 0; k < w.nNodes && k < 14; ++k) {
        const CharProf::NodeRow& r = w.nodes[idx[k]];
        const char* nm = r.typeId ? ::Object::GetTypeNameById(r.typeId) : "(empty stack)";
        std::fprintf(o, "[COMPPROF]   /charnode %-26s chars/frame=%8.1f  %.3f ms/frame %5.1f ns/call  off-screen %5.1f%% of its chars\n",
                     nm && nm[0] ? nm : "?", (double)r.chars / n, TscMs(r.ticks) / n,
                     1e6 * TscMs(r.ticks) / (double)r.chars,
                     100.0 * (double)r.offChars / (double)r.chars);
    }
    if (w.nodeOverflow)
        std::fprintf(o, "[COMPPROF]   /charnode (%lld character ticks had a node type past the table)\n", w.nodeOverflow);
}

// SMMEMO (Component/sm_memo.h; docs/extensions/handins/SMMEMO.md section 5): the
// StateMachineComponent slot-8 ticks the memo skipped, what they still cost (the dispatch
// and the compare, profiler included), what the full ones cost, and the indicative saving.
void SmMemoReport(FILE* o) {
    const SmMemo::ProfWindow& w = s_smWin;
    if (w.calls == 0) return;
    const double n = (double)s_interval;
    const double elMs = QpcMs(g_elProfDrainTicks + g_elProfPreTicks + g_elProfTreeTicks + g_elProfBucketTicks) / n;
    const long long full = w.Full();
    const double savedMs = TscMs((long long)w.SavedTicks()) / n;
    std::fprintf(o, "[COMPPROF]   /sm slot-8 memo (SMMEMO) skipped/frame=%8.1f of %8.1f StateMachine slot-8 ticks (%5.1f%%); "
                    "skipped %.3f ms/frame %5.1f ns/call; full %8.1f/frame %.3f ms/frame %5.1f ns/call; "
                    "est. saved %.3f ms/frame = %5.1f%% of EntityList (EntityList %.3f ms/frame)\n",
                 (double)w.skips / n, (double)w.calls / n, 100.0 * (double)w.skips / (double)w.calls,
                 TscMs(w.skipTicks) / n, w.skips ? 1e6 * TscMs(w.skipTicks) / (double)w.skips : 0.0,
                 (double)full / n, TscMs(w.FullTicks()) / n, full ? 1e6 * TscMs(w.FullTicks()) / (double)full : 0.0,
                 savedMs, elMs > 0 ? 100.0 * savedMs / elMs : 0.0, elMs);
}

// STILLTERRAIN (Component/still_terrain.h; docs/extensions/handins/STILLTERRAIN.md section 5):
// the still characters' last-iteration terrain queries the memo answered, what those ticks
// cost against the ones that ran the query, the floor-cell declines and the epoch bumps.
void StillTerrainReport(FILE* o) {
    const StillTerrain::ProfWindow& w = s_tzWin;
    if (w.calls == 0) return;
    const double n = (double)s_interval;
    const double elMs = QpcMs(g_elProfDrainTicks + g_elProfPreTicks + g_elProfTreeTicks + g_elProfBucketTicks) / n;
    const long long full = w.Full();
    const double savedMs = TscMs((long long)w.SavedTicks()) / n;
    std::fprintf(o, "[COMPPROF]   /still-z terrain memo (STILLTERRAIN) hits/frame=%8.1f of %8.1f still last-iteration queries (%5.1f%%); "
                    "hit ticks %.3f ms/frame %5.1f ns/call; full %8.1f/frame %5.1f ns/call (floor cells %.1f/frame); "
                    "epoch bumps/frame %.2f; est. saved %.3f ms/frame = %5.1f%% of EntityList\n",
                 (double)w.hits / n, (double)w.calls / n, 100.0 * (double)w.hits / (double)w.calls,
                 TscMs(w.hitTicks) / n, w.hits ? 1e6 * TscMs(w.hitTicks) / (double)w.hits : 0.0,
                 (double)full / n, full ? 1e6 * TscMs(w.FullTicks()) / (double)full : 0.0,
                 (double)(StillTerrain::g_floor - s_tzFloor0) / n,
                 (double)(StillTerrain::g_epoch - s_tzEpoch0) / n,
                 savedMs, elMs > 0 ? 100.0 * savedMs / elMs : 0.0);
}
// PARTREPEAT Step 0: the /part row (first-of-frame vs repeat particle ticks, and the early-exit
// share of the repeats, which an exact repeat skip could remove).
void PartReport(FILE* o) {
    const PartWindow& w = s_partWin;
    const long long calls = w.firstCalls + w.repCalls;
    if (calls == 0) return;
    const double n = (double)s_interval;
    const double elMs = QpcMs(g_elProfDrainTicks + g_elProfPreTicks + g_elProfTreeTicks + g_elProfBucketTicks) / n;
    auto ms = [n](long long t) { return TscMs(t) / n; };
    auto ns = [](long long t, long long c) { return c ? 1e6 * TscMs(t) / (double)c : 0.0; };
    std::fprintf(o, "[COMPPROF]   /part ParticleComponent slot-8 (PARTREPEAT step 0) ticks/frame=%8.1f: "
                    "first %8.1f/frame %.3f ms/frame %5.1f ns/call; repeat %8.1f/frame %.3f ms/frame %5.1f ns/call "
                    "(= %5.2f%% of EntityList); early-exit repeat %8.1f/frame (%5.1f%% of repeats) %.3f ms/frame %5.1f ns/call "
                    "%.2f live emitters/tick (= %5.2f%% of EntityList; EntityList %.3f ms/frame); "
                    "UNSTAMP stamp hits %8.1f/frame first %8.1f/frame none %6.1f/frame, lowerings %.2f/frame\n",
                 (double)calls / n,
                 (double)w.firstCalls / n, ms(w.firstTicks), ns(w.firstTicks, w.firstCalls),
                 (double)w.repCalls / n, ms(w.repTicks), ns(w.repTicks, w.repCalls),
                 elMs > 0 ? 100.0 * ms(w.repTicks) / elMs : 0.0,
                 (double)w.quietCalls / n, w.repCalls ? 100.0 * (double)w.quietCalls / (double)w.repCalls : 0.0,
                 ms(w.quietTicks), ns(w.quietTicks, w.quietCalls),
                 w.quietCalls ? (double)w.quietLive / (double)w.quietCalls : 0.0,
                 elMs > 0 ? 100.0 * ms(w.quietTicks) / elMs : 0.0, elMs,
                 (double)w.stampHits / n, (double)w.stampFirst / n, (double)w.stampNone / n,
                 (double)w.lowered / n);
}

// SKINMEMO (graphics/skin_memo.h; docs/extensions/handins/SKINMEMO.md section 5): the skinned
// bone requests inside Renderer::Draw, how many the memo answered, the distinct keys (unique
// instances vs shared instanced states), evictions, and the indicative saving.
void SkinMemoReport(FILE* o) {
    using namespace SkinMemo;
    if (g_calls == 0 && g_declines == 0) return;
    const double n = (double)s_interval;
    const uint64_t uniqCalls = g_calls - g_callsInstanced;
    const uint64_t uniqKeys = g_misses - g_missesInstanced;
    const double missMs = TscMs((long long)g_missTicks) / n;
    const double hitMs = TscMs((long long)g_hitTicks) / n;
    // What the hits would have cost at the misses' per-bone price, less what they cost.
    const double perBoneMs = g_bonesComputed ? missMs / (double)g_bonesComputed : 0.0;
    const double savedMs = perBoneMs * (double)g_bonesSaved - hitMs;
    std::fprintf(o, "[COMPPROF]   /skin bone memo (SKINMEMO) requests/frame=%8.1f hits=%8.1f (%5.1f%%) scopes/frame=%.2f declines/frame=%.1f; "
                    "unique: %8.1f req %8.1f keys (%.2f req/key, all-still %.1f keys/frame); "
                    "instanced: %8.1f req %8.1f keys (%.2f req/key); evictions/frame=%.1f; "
                    "bones computed/frame=%.0f saved/frame=%.0f; miss %.3f ms/frame %5.1f ns/bone; "
                    "hit %.3f ms/frame %5.1f ns/call; est. saved %.3f ms/frame\n",
                 (double)g_calls / n, (double)g_hits / n, g_calls ? 100.0 * (double)g_hits / (double)g_calls : 0.0,
                 (double)g_scopes / n, (double)g_declines / n,
                 (double)uniqCalls / n, (double)uniqKeys / n, uniqKeys ? (double)uniqCalls / (double)uniqKeys : 0.0,
                 (double)g_stillUnique / n,
                 (double)g_callsInstanced / n, (double)g_missesInstanced / n,
                 g_missesInstanced ? (double)g_callsInstanced / (double)g_missesInstanced : 0.0,
                 (double)g_evictions / n, (double)g_bonesComputed / n, (double)g_bonesSaved / n,
                 missMs, g_bonesComputed ? 1e6 * TscMs((long long)g_missTicks) / (double)g_bonesComputed : 0.0,
                 hitMs, g_hits ? 1e6 * TscMs((long long)g_hitTicks) / (double)g_hits : 0.0,
                 savedMs);
}
void SkinMemoReset() {
    using namespace SkinMemo;
    g_calls = g_callsInstanced = g_hits = g_hitsInstanced = g_misses = g_missesInstanced = 0;
    g_declines = g_evictions = g_bonesComputed = g_bonesSaved = g_scopes = g_stillUnique = 0;
    g_missTicks = g_hitTicks = 0;
}

// DRAWSORT target 1: SetSizeCore's element-loop iterations on the main thread, per
// frame, by spec. `empty` = iterations of retail's loop that do nothing (no fill, no
// init, i >= oldSize): what SetSizeFast::kSkipEmptyTail skips. Taken once per window
// (the take resets the engine table; both outputs print the same rows).
rtcoll::SetSizeProfRow s_ssRows[6];
int      s_ssN = 0;
uint64_t s_ssCalls = 0, s_ssIters = 0, s_ssEmpty = 0;
void SetSizeTake() { s_ssN = rtcoll::SetSizeProf_Take(s_ssRows, 6, &s_ssCalls, &s_ssIters, &s_ssEmpty); }
void SetSizeReport(FILE* o) {
    const double n = s_interval > 0 ? (double)s_interval : 1.0;
    std::fprintf(o, "[COMPPROF]   setsize: calls/frame=%.0f  loop iters/frame=%.0f  empty iters/frame=%.0f (%.1f%%)\n",
                 (double)s_ssCalls / n, (double)s_ssIters / n, (double)s_ssEmpty / n,
                 s_ssIters ? 100.0 * (double)s_ssEmpty / (double)s_ssIters : 0.0);
    for (int i = 0; i < s_ssN; ++i) {
        const rtcoll::SetSizeProfRow& r = s_ssRows[i];
        uint64_t c = 0, it = 0;
        for (int k = 0; k < rtcoll::kSetSizeClasses; ++k) { c += r.calls[k]; it += r.iters[k]; }
        std::fprintf(o, "[COMPPROF]     setsize %-36s calls/f=%8.1f iters/f=%9.0f empty/f=%9.0f realloc/f=%6.1f maxN=%d"
                        "  iters/f by class append=%.0f clear=%.0f grow0=%.0f grow=%.0f shrink=%.0f\n",
                     rtcoll::SetSizeProf_SpecName(r.spec), (double)c / n, (double)it / n,
                     (double)r.emptyIters / n, (double)r.reallocs / n, r.maxNewSize,
                     (double)r.iters[rtcoll::kSetSizeAppend] / n, (double)r.iters[rtcoll::kSetSizeClear] / n,
                     (double)r.iters[rtcoll::kSetSizeGrow0] / n, (double)r.iters[rtcoll::kSetSizeGrow] / n,
                     (double)r.iters[rtcoll::kSetSizeShrink] / n);
    }
}

// DRAWSORT target 2: the per-pass draw-item sorts of the window, per frame. `keyed` =
// lists that took the packed-key path; `inline` = the rest (short, NaN key, pointer
// >= 2^48), sorted by retail's skeleton with the comparator inlined. `ties` = adjacent
// sorted items that compare equal; `ties arg8!=` = those whose _arg8 differs (the
// case where the swap sequence decides what DrawItemList sees).
DrawSort::Counters s_dsWin = {};
void DrawSortReport(FILE* o) {
    const DrawSort::Counters& c = s_dsWin;
    const double n = s_interval > 0 ? (double)s_interval : 1.0;
    std::fprintf(o, "[COMPPROF]   draw sort: sorts/frame=%.1f items/frame=%.0f (max list %llu) compares/frame=%.0f "
                    "(%.1f/item)  keyed=%.1f inline: small=%.1f nan=%.1f ptr=%.1f /frame  ties/frame=%.1f  ties arg8!=/frame=%.2f\n",
                 (double)c.sorts / n, (double)c.items / n, (unsigned long long)c.maxItems, (double)c.compares / n,
                 c.items ? (double)c.compares / (double)c.items : 0.0,
                 (double)c.keyed / n, (double)c.smallLists / n, (double)c.nanLists / n, (double)c.ptrLists / n,
                 (double)c.ties / n, (double)c.tiesArg8 / n);
    // SORTCMP's census: `plain` = keyed lists that took the plain sort, `fallback` = of
    // those, lists with a differing-_arg8 tie (keyed skeleton rerun); `groups/list` =
    // distinct 240-bit prefixes per plain list; `runs/list` = ascending runs of the key
    // array before sorting (keyable lists, memo hits included); `same-as-last` = keyable lists whose key array
    // hashes equal to the same buffer's previous sort; `max tie` = longest tie group;
    // `arg8>1` = items that can open an instancing batch; `shadow byte-only` =
    // COMPPROF=4 lists consumer-equal to retail but not byte-equal (a harmless tie
    // permutation).
    std::fprintf(o, "[COMPPROF]   draw sort/cmp: plain=%.1f fallback=%.2f /frame  groups/list=%.0f  runs/list=%.0f  "
                    "same-as-last=%.1f%% (%.1f checked/frame)  max tie=%llu  arg8>1 items/frame=%.0f  "
                    "shadow byte-only=%.2f /frame\n",
                 (double)c.plainLists / n, (double)c.fallbacks / n,
                 c.plainLists ? (double)c.groups / (double)c.plainLists : 0.0,
                 c.keyable ? (double)c.runs / (double)c.keyable : 0.0,
                 c.repeatChecks ? 100.0 * (double)c.repeatHits / (double)c.repeatChecks : 0.0,
                 (double)c.repeatChecks / n, (unsigned long long)c.maxTie, (double)c.instItems / n,
                 (double)c.shadowBytes / n);
    // SORTMEMO. The census (whatever kSortMemo says; over the same `checked` lists as
    // `same-as-last`): `raw` = lists whose 56 bytes per item all hash equal to the same
    // buffer's previous sort, `masked` = equal outside the two pad dwords (the memo's
    // ceiling: a raw repeat is a memcpy hit, a masked-only one a permutation hit). The
    // memo (kSortMemo, lists >= 32): `hit` = byte-equal input, stored output copied;
    // `pad-hit` = equal but for the pads, stored permutation applied; `miss` = an entry
    // was there and the input differed; `new` = no entry for the buffer; `stores` =
    // entries written after a sort; `full` = misses not stored (table full); `cmp` = MB of
    // fresh input compared per frame (each side); `stored` = MB written into entries per
    // frame; `entries` / `held` = the table at the end of the window. All 0 with the
    // switch off except the census.
    std::fprintf(o, "[COMPPROF]   draw sort/memo: census raw=%.1f%% masked=%.1f%%  memo hit=%.1f pad-hit=%.1f "
                    "miss=%.1f new=%.2f stores=%.1f full=%.2f /frame  cmp=%.2f MB/frame  stored=%.2f MB/frame  "
                    "entries=%llu held=%.1f MB\n",
                 c.repeatChecks ? 100.0 * (double)c.repeatRaw / (double)c.repeatChecks : 0.0,
                 c.repeatChecks ? 100.0 * (double)c.repeatMasked / (double)c.repeatChecks : 0.0,
                 (double)c.memoHits / n, (double)c.memoPadHits / n, (double)c.memoMisses / n,
                 (double)c.memoNew / n, (double)c.memoStores / n, (double)c.memoFull / n,
                 (double)c.memoCompared / n / 1048576.0, (double)c.memoStored / n / 1048576.0,
                 (unsigned long long)c.memoEntries, (double)c.memoHeld / 1048576.0);
}

// GATHERPF: per DRAW frame of the window. `visible` = slot-3 dispatches (the visible list),
// `draw items` = Renderer::AddDrawItem appends (every caller; all of them are reached from
// the gather's dispatch).
void GatherReport(FILE* o) {
    const double n = s_interval > 0 ? (double)s_interval : 1.0;
    std::fprintf(o, "[COMPPROF]   gather (faithful walk): visible/frame=%.0f  draw items appended/frame=%.0f "
                    "(%.2f/visible)  passes/frame=%.2f\n",
                 (double)g_gatherVisible / n,
                 (double)g_gatherDrawItems / n,
                 g_gatherVisible ? (double)g_gatherDrawItems / (double)g_gatherVisible : 0.0,
                 (double)g_frustumCullPasses / n);
}

void CompProfReport(int frame) {
    struct Row { const CompSlot* s; };
    std::vector<const CompSlot*> rows;
    long long total = 0;
    for (int i = 0; i < kCompSlots; ++i) if (s_comp[i].vtbl && s_comp[i].calls) {
        rows.push_back(&s_comp[i]); total += s_comp[i].ticks;
    }
    if (rows.empty()) return;
    std::sort(rows.begin(), rows.end(),
              [](const CompSlot* a, const CompSlot* b) { return a->ticks > b->ticks; });
    SetSizeTake();
    s_dsWin = DrawSort::TakeCounters();
    if (!s_compOut) s_compOut = std::fopen("standalone_stress_compprof.txt", "w");
    FILE* outs[2] = { stderr, s_compOut };
    for (FILE* o : outs) {
        if (!o) continue;
        {
            std::lock_guard<std::mutex> lock(s_verifyLock);   // RENDERPIPE-1: the render thread reports too
            for (int i = 0; i < s_verifyN; ++i)   // ELPROF verification shadows (level 4): cumulative
                std::fprintf(o, "[VERIFY] %s checks=%lld mismatches=%lld\n",
                             s_verify[i].what, s_verify[i].checks, s_verify[i].mismatches);
        }
        std::fprintf(o, "[COMPPROF] world#%d frame=%d window=%d frames  component Update total %.2f ms/frame\n",
                     s_worldNo, frame, s_interval, TscMs(total) / s_interval);
        std::fprintf(o, "[COMPPROF]   EntityList_Update phases ms/frame: drain %.3f  preUpdate %.3f  "
                        "treeWalk %.3f (%.0f nodes/frame)  buckets %.3f (%.0f visits, %.0f ticked /frame)\n",
                     QpcMs(g_elProfDrainTicks) / s_interval, QpcMs(g_elProfPreTicks) / s_interval,
                     QpcMs(g_elProfTreeTicks) / s_interval, (double)g_elProfTreeNodes / s_interval,
                     QpcMs(g_elProfBucketTicks) / s_interval, (double)g_elProfBucketVisits / s_interval,
                     (double)g_elProfBucketTicked / s_interval);
        // FRUSTUMCULL: the scene cull's box tests (AddCulledDrawItems adds its count once
        // per call, so the counter costs nothing per box).
        std::fprintf(o, "[COMPPROF]   frustum cull: boxes tested/frame=%.0f  cull passes/frame=%.2f  (%.0f boxes/pass)\n",
                     (double)g_frustumCullBoxes / s_interval, (double)g_frustumCullPasses / s_interval,
                     g_frustumCullPasses ? (double)g_frustumCullBoxes / (double)g_frustumCullPasses : 0.0);
        // CULLROUTE: every routed caller of the frustum test, boxes/frame (calls/frame).
        // Each site adds its count once per call. `scene` equals the row above.
        {
            static const char* const kSiteNames[kFrustumCullSiteCount] = {
                "scene", "terrain chunks", "terrain decals", "terrain roads", "weather tiles", "ext.draw_gather"};
            uint64_t all = 0;
            for (int s = 0; s < kFrustumCullSiteCount; ++s) all += g_frustumCullSiteBoxes[s];
            std::fprintf(o, "[COMPPROF]   frustum cull sites: all %.0f boxes/frame;", (double)all / s_interval);
            for (int s = 0; s < kFrustumCullSiteCount; ++s)
                std::fprintf(o, "  %s %.0f (%.2f calls)", kSiteNames[s],
                             (double)g_frustumCullSiteBoxes[s] / s_interval,
                             (double)g_frustumCullSiteCalls[s] / s_interval);
            std::fprintf(o, "\n");
        }
        // GATHERPF: the faithful gather's per-phase object counts (AddCulledDrawItems adds
        // its counts once per call). No shadow: the GATHERPF arms are prefetch hints only.
        // With ext.draw_gather ON the faithful walk is not reached and the first three are 0.
        GatherReport(o);
        SetSizeReport(o);
        DrawSortReport(o);
        int shown = 0;
        for (const CompSlot* s : rows) {
            if (++shown > 30) break;
            const char* name = ::Object::GetTypeNameById(s->typeId);
            char nm[64];
            std::snprintf(nm, sizeof(nm), "%s%s", name && name[0] ? name : "?", s->kind ? "/tree" : "");
            std::fprintf(o, "[COMPPROF]   %-34s %8.3f ms/frame %6.2f%%  calls/frame=%8.1f  %.4f us/call\n",
                         nm, TscMs(s->ticks) / s_interval,
                         total ? 100.0 * (double)s->ticks / (double)total : 0.0,
                         (double)s->calls / s_interval,
                         1000.0 * TscMs(s->ticks) / (double)s->calls);
        }
        CharProfReport(o);
        SmMemoReport(o);
        StillTerrainReport(o);
        PartReport(o);
        SkinMemoReport(o);
        std::fflush(o);
    }
    s_charWin.Reset();
    s_smWin.Reset();
    s_tzWin.Reset(); s_tzEpoch0 = StillTerrain::g_epoch; s_tzFloor0 = StillTerrain::g_floor;
    s_partWin.Reset();
    SkinMemoReset();
    Ext::WalkBatch::g_profTicks = 0; Ext::WalkBatch::g_profCalls = 0;   // WALKBATCH: the /char walkbatch row's window
    for (int i = 0; i < kCompSlots; ++i) { s_comp[i].ticks = 0; s_comp[i].calls = 0; }
    g_elProfDrainTicks = g_elProfPreTicks = g_elProfTreeTicks = g_elProfBucketTicks = 0;
    g_elProfTreeNodes = g_elProfBucketVisits = g_elProfBucketTicked = 0;
    g_frustumCullBoxes = g_frustumCullPasses = 0;
    for (int s = 0; s < kFrustumCullSiteCount; ++s) g_frustumCullSiteBoxes[s] = g_frustumCullSiteCalls[s] = 0;   // CULLROUTE
    g_gatherVisible = g_gatherDrawItems = 0;   // GATHERPF
}

// ── staffing pass ────────────────────────────────────────────────────────────
constexpr int kStaffFirst  = 600;
constexpr int kStaffRetry  = 300;
constexpr int kStaffDense  = 6000;   // retry every kStaffRetry until here, then every 1800
int s_staffPasses = 0;

void StaffPass(GameUpdateLoop* gw, int worldFrame) {
    CitizenBehavior* townMgr = gw->m_citizenList;     // +0xF0
    EntityList* entityList   = gw->m_entityList;      // +0xE8
    if (!townMgr || !entityList) return;
    Object::InternalPtr<ComponentEntity>* arr = entityList->masterArray._data;   // +0x08
    const int n = entityList->masterArray._size;            // +0x10
    if (!arr || n <= 0) return;

    // 0x140002e10 = FindComponent_WorkPlace, 0x140003050 = FindComponent_Work (both
    // Ghidra-misnamed; see reference_misnamed_component_finders): the two
    // ComponentEntity::FindComponent<T> instances, called by name.

    struct Need { Profession* prof; int want; int have; int buildings; };
    std::vector<Need> needs;
    int workplaces = 0, notAccepting = 0;
    for (int i = 0; i < n; ++i) {
        ComponentEntity* ent = arr[i].Get();
        if (!ent) continue;
        if (ent->_entityFlags & 0x400) continue;   // removed (+0x40)
        WorkPlaceComponent* wpc = ent->FindComponent<WorkPlaceComponent>();   // 0x140002e10
        if (!wpc) continue;
        Profession* const prof = wpc->profession;   // +0x48
        if (!prof) continue;
        ++workplaces;
        if (wpc->isAccepting == 0) { ++notAccepting; continue; }            // +0x50
        const int have = wpc->workers._size;   // live roster (+0x60)
        int want = 1;
        WorkComponent* wc = ent->FindComponent<WorkComponent>();             // 0x140003050
        if (wc) {
            // Component::_description (+0x18) downcast inside the Description hierarchy.
            const Description* baseDesc = wc->_description;
            const WorkDescription* wd = static_cast<const WorkDescription*>(baseDesc);
            if (wd) {
                const int dflt = wd->_i24;   // +0x24
                const int mx   = wd->_i2c;   // +0x2c
                want = dflt > 0 ? dflt : 1;
                if (mx > 0 && want > mx) want = mx;
            }
            // Effective capacity as the panel enforces it (workComp+0x30), when computed.
            const int cap = wc->workState;
            if (cap > 0 && want > cap) want = cap;
        }
        Need* nd = nullptr;
        for (Need& x : needs) if (x.prof == prof) { nd = &x; break; }
        if (!nd) { needs.push_back({prof, 0, 0, 0}); nd = &needs.back(); }
        nd->want += want; nd->have += have; ++nd->buildings;
    }

    int seatedTotal = 0, wanted = 0;
    for (Need& x : needs) {
        const int missing = x.want - x.have;
        if (missing <= 0) continue;
        wanted += missing;
        const int got = CitizenEmploy_SeatWorkers(townMgr, x.prof, missing);
        seatedTotal += got;
        if (got || s_staffPasses == 0)
            std::fprintf(stderr, "[Stress::staff]   prof=0x%llx buildings=%d want=%d have=%d -> seated %d/%d\n",
                         reinterpret_cast<unsigned long long>(x.prof),   // CAST-OK: ptr_int (the key logged as an integer)
                         x.buildings, x.want, x.have, got, missing);
    }
    std::fprintf(stderr, "[Stress::staff] pass#%d worldFrame=%d workplaces=%d (notAccepting=%d) "
                         "professions=%zu missing=%d seated=%d\n",
                 s_staffPasses, worldFrame, workplaces, notAccepting, needs.size(), wanted, seatedTotal);
    std::fflush(stderr);
    ++s_staffPasses;
}

// ── food census ──────────────────────────────────────────────────────────────
// One [Stress::food] line per CSV row: the edible units (RawMaterialDescription _flags bit 5)
// held by the registered storage locations (townMgr+0x00), by houses (ResidenceComponent),
// and by every other StorageComponent (workplace output piles, markets), plus the storage
// locations' used / total volume. Read-only.
const RawMaterialDescription* RawMatOf(const ComponentDescription* tmpl) {
    if (!tmpl) return nullptr;
    const uint32_t tid = RawMaterialDescription::TypeId();
    for (int32_t i = 0; i < tmpl->_subDescCount; ++i) {
        Description* d = tmpl->_subDescs[i];
        if (d && d->Type() == tid) return static_cast<const RawMaterialDescription*>(d);
    }
    return nullptr;
}
int EdibleUnits(const StorageComponent* sc) {
    int n = 0;
    for (int i = 0; i < sc->storageSlots._size; ++i) {
        const StorageComponent::Item& it = sc->storageSlots._data[i];
        const RawMaterialDescription* rm = RawMatOf(it._resource);
        if (rm && (rm->_flags & 0x20)) n += it._count;
    }
    return n;
}
// The town's storage locations (townMgr+0x00), each entity once: retail appends without
// a dedup and the founding barn can sit in the list twice.
void UniqueStorageLocations(CitizenBehavior* townMgr, std::vector<StorageComponent*>& out) {
    out.clear();
    std::vector<ComponentEntity*> seen;
    const int nLoc = townMgr->field_0x00._size;
    for (int i = 0; i < nLoc; ++i) {
        ComponentEntity* e = townMgr->field_0x00._data[i].Get();
        if (!e || (e->_entityFlags & 0x400)) continue;
        if (std::find(seen.begin(), seen.end(), e) != seen.end()) continue;
        seen.push_back(e);
        StorageComponent* sc = e->FindComponent<StorageComponent>();
        if (sc) out.push_back(sc);
    }
}

long long s_foodAdded = 0, s_foodAddedWin = 0;   // FOOD: units the top-up pass placed
int s_foodPasses = 0, s_foodShort = 0;          // passes that added / that could not place the whole deficit

void FoodCensus(GameUpdateLoop* gw, int worldFrame) {
    CitizenBehavior* townMgr = gw->m_citizenList;
    EntityList* el = gw->m_entityList;
    if (!townMgr || !el) return;
    std::vector<StorageComponent*> locs;
    UniqueStorageLocations(townMgr, locs);
    int locFood = 0; long long locUsed = 0, locCap = 0;
    char detail[1024]; int dl = 0; detail[0] = 0;
    for (StorageComponent* sc : locs) {
        const int f = EdibleUnits(sc);
        const int used = sc->QueryCount();
        locFood += f; locUsed += used; locCap += sc->capacity;
        if ((sc->rawMaterialFlags & 0x20) && dl < (int)sizeof(detail) - 40)
            dl += std::snprintf(detail + dl, sizeof(detail) - dl, " %d/%d/%d", f, used, sc->capacity);
    }
    int houseN = 0, houseFood = 0, houseEmpty = 0, allFood = 0;
    Object::InternalPtr<ComponentEntity>* arr = el->masterArray._data;
    const int n = el->masterArray._size;
    for (int i = 0; i < n; ++i) {
        ComponentEntity* e = arr[i].Get();
        if (!e || (e->_entityFlags & 0x400)) continue;
        StorageComponent* sc = e->FindComponent<StorageComponent>();
        if (!sc) continue;
        const int f = EdibleUnits(sc);
        allFood += f;
        if (e->FindComponent<ResidenceComponent>()) {
            ++houseN; houseFood += f; if (f == 0) ++houseEmpty;
        }
    }
    const int otherFood = allFood - houseFood - locFood;
    std::fprintf(stderr, "[Stress::food] worldFrame=%d storageLocs=%zu locFood=%d locVolume=%lld/%lld "
                         "houses=%d houseFood=%d housesEmpty=%d otherFood=%d allFood=%d "
                         "topup=%lld (total %lld, passes %d, short %d) | foodLocs food/used/cap:%s\n",
                 worldFrame, locs.size(), locFood, locUsed, locCap, houseN, houseFood, houseEmpty,
                 otherFood, allFood, s_foodAddedWin, s_foodAdded, s_foodPasses, s_foodShort, detail);
    std::fflush(stderr);
    s_foodAddedWin = 0;
}

// ── food top-up pass ─────────────────────────────────────────────────────────
// The stress town cannot feed itself: its food is the seeded start stock, which the
// barns' volume caps (~23k edible units in four 6000-volume barns, whatever
// BANISHED_STRESS_FOOD_MULT asks for), and ~950 citizens eat it in ~100k sim ticks while
// three hunter/gatherer sets, one field, one orchard and one dock produce next to nothing.
// So, like StaffPass, the harness keeps the larder stocked: every kFoodPeriod world frames,
// when the food in the town's storage locations is below kFoodPerCitizen units per living
// citizen, it adds the deficit straight into the food-accepting storage locations (the same
// StorageComponent::AddItem the start-stock seeding uses, CanAdd-checked, round-robin over the
// town's edible raw-material templates so the diet keeps its variety). Citizens still fetch
// it from the barns and carry it home themselves; nothing else is touched.
constexpr int kFoodFirst      = 300;   // world frame of the first pass
constexpr int kFoodPeriod     = 60;    // world frames between passes
constexpr int kFoodPerCitizen = 15;    // storage floor, edible units per living citizen

void FoodPass(GameUpdateLoop* gw) {
    CitizenBehavior* townMgr = gw->m_citizenList;
    if (!townMgr) return;
    const int citizens = townMgr->citizens._size;
    if (citizens <= 0) return;
    std::vector<StorageComponent*> locs;
    UniqueStorageLocations(townMgr, locs);
    int stock = 0;
    for (StorageComponent* sc : locs) stock += EdibleUnits(sc);
    int deficit = citizens * kFoodPerCitizen - stock;
    if (deficit <= 0) return;

    std::vector<ComponentDescription*> foods;
    for (int t = 0; t < townMgr->field_0x110._size; ++t) {
        ComponentDescription* tmpl = townMgr->field_0x110._data[t];
        const RawMaterialDescription* rm = RawMatOf(tmpl);
        if (rm && (rm->_flags & 0x20)) foods.push_back(tmpl);
    }
    if (foods.empty()) return;

    // Spread the deficit: a share per (location, food) pair, rounds until it is met so a full barn's
    // share moves on to the barns with room.
    long long added = 0;
    for (int round = 0; round < 16 && deficit > 0; ++round) {
        const int pairs = (int)(locs.size() * foods.size());
        const int share = std::max(1, deficit / std::max(1, pairs));
        bool progress = false;
        for (StorageComponent* sc : locs) {
            for (ComponentDescription* f : foods) {
                if (deficit <= 0) break;
                if (!sc->AcceptsMaterial(f)) continue;
                const int w = std::max(1, (int)RawMatOf(f)->_weight);
                const int room = (sc->capacity - sc->QueryCount()) / w;
                const int k = std::min(std::min(share, deficit), room);
                if (k <= 0 || !sc->CanAdd(f, k)) continue;
                sc->AddItem(f, k);   // 0x14012d000, as the start-stock seeding
                deficit -= k; added += k; progress = true;
            }
        }
        if (!progress) break;
    }
    if (added > 0) { ++s_foodPasses; s_foodAdded += added; s_foodAddedWin += added; }
    if (deficit > 0) ++s_foodShort;
}

// ── CSV row ──────────────────────────────────────────────────────────────────
void OpenCsv() {
    if (s_csv) return;
    s_csv = std::fopen(s_csvPath, "w");
    if (!s_csv) { std::fprintf(stderr, "[Stress] cannot open %s\n", s_csvPath); return; }
    Platform::LocalTime st; Platform::LocalTimeNow(&st);   // GetLocalTime
    std::fprintf(s_csv, "# banished stress run %04d-%02d-%02d %02d:%02d:%02d build=%s %s\n",
                 st.year, st.month, st.day, st.hour, st.minute, st.second, __DATE__, __TIME__);
    std::fprintf(s_csv, "# citizens=%d sets=%d layout=%s speed=%d frames=%d interval=%d seed=%s map=%s opt=%s\n",
                 s_citizens, s_sets < 0 ? -s_sets : s_sets, s_sets < 0 ? "spread" : "compact",
                 s_speed, s_frames, s_interval,
                 Opt().seed ? Opt().seed : "?",
                 Opt().mapSize ? Opt().mapSize : "?",
#ifdef __OPTIMIZE__
                 "on"
#else
                 "off"
#endif
    );
    {
        // CPULIMIT: the leg's CPU mask (sys.cpu_limit) and core counts; stress_report.py prints
        // every '#' line in its header, so each A/B leg proves its mask.
        char cpu[192];
        Cpu::Describe(Cpu::Get(), cpu, sizeof(cpu));
        std::fprintf(s_csv, "# cpu %s\n", cpu);
    }
    std::fprintf(s_csv, "wall_s,world,frame,speed,sim_iters,fps,frame_ms_mean,frame_ms_p95,"
                        "update_ms_mean,update_ms_p95,update_ms_per_iter,draw_ms_mean,"
                        "ph_paircache_ms,ph_animation_ms,ph_entitylist_ms,ph_weather_ms,"
                        "ph_citizenlist_ms,ph_postsim_ms,"
                        "entities,citizens,home238,work248,busy,age_mean,hunger_mean,ws_mb,private_mb,"
                        // PFBENCH (docs/extensions/handins/PFBENCH.md): appended, so every
                        // older reader of the columns above is unaffected.
                        "pf_requests,pf_req_per_sim_s,pf_search_us_p50,pf_search_us_p95,pf_search_us_max,"
                        "pf_expanded_mean,pf_expanded_p95,pf_wait_us_p50,pf_wait_us_p95,pf_queue_max,"
                        "pf_fail,pf_worker_busy_pct,pf_len_mean,pf_e2e_us_p50,pf_e2e_us_p95,"
                        "pf_ready_us_p50,pf_ready_us_p95,pf_poll_us_p50,pf_poll_us_p95,"
                        "pf_frames0_pct,pf_frames1_pct,pf_frames2_pct,pf_frames3p_pct,"
                        "pf_shadow_n,pf_shadow_equal_cost,pf_shadow_same_cells,pf_shadow_worse,pf_shadow_speedup,"
                        "pf_sync_wait_ms,ph_pathdistance_ms,ph_fillzones_ms,"
                        // RENDERPIPE (docs/extensions/handins/RENDERPIPE0.md): appended.
                        "draw_main_ms,draw_packet_ms,draw_emit_ms,"
                        // RENDERPIPE-1 (docs/extensions/handins/RENDERPIPE1.md): appended.
                        "pipe_wait_us,pipe_latency_us,"
                        // HITCH (perf/README.md "Hitches"): appended. The window's longest
                        // Draw-to-Draw period and its hitch count / worst / summed excess.
                        "frame_ms_max,hitch_n,hitch_excess_max_ms,hitch_excess_sum_ms\n");
    std::fflush(s_csv);
}

// [Census] (2026-10-12, the entity-count gap between stress profiles): once per CSV row at
// COMPPROF >= 1, the master array's entities bucketed by the type names of their first two
// components, the 24 largest kinds to stderr. Diagnostic only; nothing in the sim reads it.
static void EntityCensus(GameUpdateLoop* gw, int frame) {
    if (!Readable(gw)) return;
    EntityList* el = gw->m_entityList;
    if (!Readable(el)) return;
    const int n = el->masterArray._size;
    if (n <= 0 || n > 10000000) return;
    struct Kind { const char* a; const char* b; int count; };
    std::vector<Kind> kinds;
    int nullSlots = 0, empty = 0;
    for (int i = 0; i < n; ++i) {
        ComponentEntity* e = el->masterArray._data[i].Get();
        if (!Readable(e)) { ++nullSlots; continue; }
        const int cc = e->_components._size;
        const char* a = "-"; const char* b = "-";
        if (cc <= 0) { ++empty; }
        else {
            Component* c0 = e->_components[0].Get();
            if (Readable(c0)) a = ::Object::GetTypeNameById(c0->Type());
            if (cc > 1) { Component* c1 = e->_components[1].Get(); if (Readable(c1)) b = ::Object::GetTypeNameById(c1->Type()); }
        }
        bool found = false;
        for (Kind& k : kinds) if (k.a == a && k.b == b) { ++k.count; found = true; break; }
        if (!found) kinds.push_back(Kind{a, b, 1});
    }
    std::sort(kinds.begin(), kinds.end(), [](const Kind& x, const Kind& y) { return x.count > y.count; });
    std::fprintf(stderr, "[Census] frame=%d masterArray=%d nullSlots=%d noComponents=%d kinds=%d\n",
                 frame, n, nullSlots, empty, (int)kinds.size());
    for (size_t i = 0; i < kinds.size() && i < 24; ++i)
        std::fprintf(stderr, "[Census]   %6d  %s + %s\n", kinds[i].count, kinds[i].a ? kinds[i].a : "?", kinds[i].b ? kinds[i].b : "?");
}

void EmitRow(GameUpdateLoop* gul, int frame) {
    s_hCur.markers |= kHmRow;   // HITCH: the harness's own row work lands in this period
    if (g_compProfOn >= 1) EntityCensus(GameUpdateLoop::_singleton, frame);
    OpenCsv();
    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    const long long now = QpcNow();
    const double wall = QpcMs(now - s_runT0) / 1000.0;
    const double windowS = QpcMs(now - (s_lastRow ? s_lastRow : s_runT0)) / 1000.0;
    s_lastRow = now;

    int speed = 0;
    {
        PlayerToolManager* eui = gul->m_toolManager;                    // +0x120
        if (eui && eui->paused == 0) speed = eui->field_0x34;            // +0x30 / +0x34
    }
    const double fps = windowS > 0 ? s_drawN / windowS : 0.0;
    const double updMean = s_updN ? s_updMsSum / s_updN : 0.0;
    // RENDERPIPE: fold the emitting thread's window. The packets' GameUpdateLoop::Draw
    // share joins draw_ms_mean (the same span as the faithful column).
    const double pipePacketSum = QpcMs(s_pipePacketTicks.exchange(0, std::memory_order_relaxed));
    const int    pipePacketN   = s_pipePacketN.exchange(0, std::memory_order_relaxed);
    const double pipeEmitSum   = QpcMs(s_pipeEmitTicks.exchange(0, std::memory_order_relaxed));
    const int    pipeEmitN     = s_pipeEmitN.exchange(0, std::memory_order_relaxed);
    s_drawMsSum += pipePacketSum;
    const double drawMean = s_drawN ? s_drawMsSum / s_drawN : 0.0;
    const double perIter = s_simIters ? s_updMsSum / (double)s_simIters : 0.0;
    // RENDERPIPE: per pipelined frame / per packet / per emit (0 when none in the window).
    const double pipeMain   = s_pipeMainN ? s_pipeMainSum / s_pipeMainN : 0.0;
    const double pipePacket = pipePacketN ? pipePacketSum / pipePacketN : 0.0;
    const double pipeEmit   = pipeEmitN   ? pipeEmitSum   / pipeEmitN   : 0.0;
    // RENDERPIPE-1: the hand-off waits (us per draw frame of the window) and the packet
    // latency (hand-off -> Present done, mean us); 0 in the sequential split.
    const Ext::RenderPipe::Counters rp = Ext::RenderPipe::TakeWindow();
    const double pipeWaitUs    = s_drawN ? static_cast<double>(rp.waitUs) / s_drawN : 0.0;
    const double pipeLatencyUs = rp.latencyN ? static_cast<double>(rp.latencyUs) / rp.latencyN : 0.0;
    double frameMean = 0;
    for (float f : s_frameSamples) frameMean += f;
    if (!s_frameSamples.empty()) frameMean /= s_frameSamples.size();
    const float updP95 = Pct(s_updSamples, 0.95), frameP95 = Pct(s_frameSamples, 0.95);

    double ph[8] = {0};
    // HITCH: the phases are drained per Update (HitchDrainPhases); this is the row's sum.
    const int phN = 8;
    for (int i = 0; i < 8; ++i) { ph[i] = s_phWindow[i]; s_phWindow[i] = 0; }
    // phases: 0 PairCache 1 Animation 2 EntityList 3 Weather 4 CitizenList 5 PostSim 6 Total
    for (int i = 0; i < phN; ++i) ph[i] /= (s_updN ? s_updN : 1);

    Census c = TakeCensus(gw);
    double ws = 0, priv = 0; MemoryMB(&ws, &priv);

    // PFBENCH: take the window (one lock), fold in the main-thread brackets, summarise.
    PfStats::Window pfWin;
    PfStats::MainWindow pfMain;
    {
        std::lock_guard<std::mutex> g(s_pfLock);
        std::swap(pfWin, s_pfWin);
        std::swap(pfMain, s_pfMain);
    }
    pfMain.pathDistanceTicks = s_pfMainTicks[PF_MAIN_PATHDISTANCE].exchange(0, std::memory_order_relaxed);
    pfMain.fillZonesTicks    = s_pfMainTicks[PF_MAIN_FILLZONES].exchange(0, std::memory_order_relaxed);
    pfMain.syncWaitTicks     = s_pfMainTicks[PF_MAIN_SYNCWAIT].exchange(0, std::memory_order_relaxed);
    const double usPerTick = TscMs(1000000000LL) / 1000000.0;   // ms per 1e9 ticks -> us per tick
    const double simS = (double)s_simTicks / kTimeTicksPerSecond;
    s_simTicks = 0;
    const PfStats::Summary pf = PfStats::Summarize(pfWin, pfMain, usPerTick, windowS, simS, s_updN);

    if (s_csv) {
        std::fprintf(s_csv, "%.1f,%d,%d,%d,%lld,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                            "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                            "%d,%d,%d,%d,%d,%.2f,%.1f,%.1f,%.1f,",
                     wall, s_worldNo, s_worldFrame, speed, s_simIters, fps, frameMean, frameP95,
                     updMean, updP95, perIter, drawMean,
                     ph[0], ph[1], ph[2], ph[3], ph[4], ph[5],
                     c.entities, c.citizens, c.home238, c.work248, c.busy, c.ageMean, c.hungerMean,
                     ws, priv);
        std::fprintf(s_csv, "%lld,%.2f,%.1f,%.1f,%.1f,%.1f,%.0f,%.1f,%.1f,%d,"
                            "%lld,%.2f,%.1f,%.1f,%.1f,"
                            "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,"
                            "%lld,%.2f,%.2f,%lld,%.3f,"
                            "%.4f,%.4f,%.4f,"
                            "%.4f,%.4f,%.4f,"
                            "%.1f,%.1f,"
                            "%.3f,%d,%.3f,%.3f\n",
                     pf.requests, pf.reqPerSimS, pf.searchUsP50, pf.searchUsP95, pf.searchUsMax,
                     pf.expandedMean, pf.expandedP95, pf.waitUsP50, pf.waitUsP95, pf.queueMax,
                     pf.fail, pf.workerBusyPct, pf.lenMean, pf.e2eUsP50, pf.e2eUsP95,
                     pf.readyUsP50, pf.readyUsP95, pf.pollUsP50, pf.pollUsP95,
                     pf.frames0Pct, pf.frames1Pct, pf.frames2Pct, pf.frames3pPct,
                     pf.shadowN, pf.shadowEqualCostPct, pf.shadowSameCellsPct, pf.shadowWorse, pf.shadowSpeedup,
                     pf.syncWaitMs, pf.pathDistanceMs, pf.fillZonesMs,
                     pipeMain, pipePacket, pipeEmit,
                     pipeWaitUs, pipeLatencyUs,
                     s_hWinMax, s_hWinN, s_hWinExcessMax, s_hWinExcessSum);
        std::fflush(s_csv);
    }
    if (HitchOn()) {
        std::fprintf(stderr, "[Stress::hitch] thr=%.2fms frames=%d max=%.2fms hitches=%d (excess max %.2f sum %.1f) "
                             "hist[t,2t,4t,8t,16t,32t,64t,+]=%d,%d,%d,%d,%d,%d,%d,%d total=%lld\n",
                     Opt().stressHitchMs, s_hWinFrames, s_hWinMax, s_hWinN, s_hWinExcessMax, s_hWinExcessSum,
                     s_hWinHist[0], s_hWinHist[1], s_hWinHist[2], s_hWinHist[3], s_hWinHist[4], s_hWinHist[5],
                     s_hWinHist[6], s_hWinHist[7], s_hTotal);
    }
    s_hWinMax = s_hWinExcessMax = s_hWinExcessSum = 0; s_hWinN = s_hWinFrames = 0;
    for (int& h : s_hWinHist) h = 0;
    std::fprintf(stderr, "[Stress::pf] requests=%lld (%.1f/sim s) busy=%.1f%% search us p50=%.0f p95=%.0f max=%.0f "
                         "expanded mean=%.0f p95=%.0f wait us p50=%.0f p95=%.0f queueMax=%d fail=%lld len=%.1f "
                         "| main ms/frame PathDistance=%.3f FillZones=%.3f syncSpin=%.3f\n",
                 pf.requests, pf.reqPerSimS, pf.workerBusyPct, pf.searchUsP50, pf.searchUsP95, pf.searchUsMax,
                 pf.expandedMean, pf.expandedP95, pf.waitUsP50, pf.waitUsP95, pf.queueMax, pf.fail, pf.lenMean,
                 pf.pathDistanceMs, pf.fillZonesMs, pf.syncWaitMs);
    std::fprintf(stderr, "[Stress] t=%.0fs world#%d frame=%d speed=%d fps=%.1f frame=%.2fms(p95 %.2f) "
                         "update=%.2fms(p95 %.2f, %.3f/iter) draw=%.2fms | entities=%d citizens=%d "
                         "home=%d work=%d busy=%d hunger=%.0f | ws=%.0fMB priv=%.0fMB\n",
                 wall, s_worldNo, s_worldFrame, speed, fps, frameMean, frameP95,
                 updMean, updP95, perIter, drawMean, c.entities, c.citizens, c.home238, c.work248,
                 c.busy, c.hungerMean, ws, priv);
    std::fflush(stderr);
    // DRAWGATHER (ext.draw_gather, docs/extensions/handins/DRAWGATHER.md): per draw frame of
    // the window. Printed only while the extension is on or counted something.
    {
        const Ext::DrawGather::Counters g = Ext::DrawGather::TakeWindow();
        if (Ext::DrawGather::Installed() || g.gathers != 0) {
            const double df = s_drawN > 0 ? static_cast<double>(s_drawN) : 1.0;
            std::fprintf(stderr, "[Stress::gather] draw_gather=%d per frame: gathers=%.1f nodes=%.0f visible=%.0f "
                                 "geoTouched=%.0f (%.1f%% of nodes) refreshes=%.1f cleans=%.1f | window: logged=%llu rebuilds=%llu\n",
                         Ext::DrawGather::Installed() ? 1 : 0, g.gathers / df, g.nodesTested / df, g.visible / df,
                         g.geoTouched / df, g.nodesTested ? 100.0 * g.geoTouched / g.nodesTested : 0.0,
                         g.refreshes / df, g.cleans / df,
                         static_cast<unsigned long long>(g.logged), static_cast<unsigned long long>(g.rebuilds));
        }
        // CULLPAR (ext.gather_threads, docs/extensions/handins/CULLPAR.md): the parallel walk's
        // tasks, the helpers' share of them and the main thread's join wait (summed per pass).
        if (Ext::DrawGather::ParallelHelpers() > 0 || g.parPasses != 0) {
            const double df = s_drawN > 0 ? static_cast<double>(s_drawN) : 1.0;
            const double pp = g.parPasses ? static_cast<double>(g.parPasses) : 1.0;
            const double waitUs = 1000.0 * QpcMs(static_cast<long long>(g.parWaitTicks));
            std::fprintf(stderr, "[Stress::gather] gather_threads helpers=%d per frame: wakes=%.2f passes=%.1f "
                                 "tasks=%.1f (%.1f/pass) helperShare=%.1f%% joinWait=%.1fus (%.2fus/pass) "
                                 "| window: sideGrew=%llu\n",
                         Ext::DrawGather::ParallelHelpers(), g.parWakes / df, g.parPasses / df, g.parTasks / df,
                         g.parTasks / pp, g.parTasks ? 100.0 * g.parHelper / g.parTasks : 0.0,
                         waitUs / df, waitUs / pp, static_cast<unsigned long long>(g.parSideGrew));
        }
    }
    // RENDERPIPE (ext.render_pipe, docs/extensions/handins/RENDERPIPE0.md): printed only
    // while the extension is installed or counted something.
    {
        if (Ext::RenderPipe::Installed() || rp.pipelined != 0 || rp.emits != 0) {
            std::fprintf(stderr, "[Stress::pipe] render_pipe=%d thread=%d draw main=%.3fms packet=%.3fms emit=%.3fms "
                                 "(main share %.1f%%) wait=%.1fus/frame latency=%.1fus | window: pipelined=%llu "
                                 "sequential=%llu emits=%llu waits=%llu (fence %llu) recoveries=%llu "
                                 "drains step=%llu other=%llu discarded=%llu idle writes=%llu skips=%llu "
                                 "| overlap: steps=%llu snapshots=%llu refused=%llu copy=%.3fms %lluKB/packet "
                                 "governor=%s removable=%lldus idle=%lldus (without the copies %lldus)\n",
                         Ext::RenderPipe::Installed() ? 1 : 0, Ext::RenderPipe::Threaded() ? 1 : 0,
                         pipeMain, pipePacket, pipeEmit,
                         pipeMain + pipePacket > 0 ? 100.0 * pipeMain / (pipeMain + pipePacket) : 0.0,
                         pipeWaitUs, pipeLatencyUs,
                         static_cast<unsigned long long>(rp.pipelined), static_cast<unsigned long long>(rp.sequential),
                         static_cast<unsigned long long>(rp.emits), static_cast<unsigned long long>(rp.waits),
                         static_cast<unsigned long long>(rp.waitsFence), static_cast<unsigned long long>(rp.recoveries),
                         static_cast<unsigned long long>(rp.drainStep),
                         static_cast<unsigned long long>(rp.drainOther), static_cast<unsigned long long>(rp.discarded),
                         static_cast<unsigned long long>(rp.idleWrites), static_cast<unsigned long long>(rp.idleSkips),
                         static_cast<unsigned long long>(rp.overlapped), static_cast<unsigned long long>(rp.snapshots),
                         static_cast<unsigned long long>(rp.refused),
                         rp.snapshots > 0 ? static_cast<double>(rp.snapshotUs) / 1000.0 / static_cast<double>(rp.snapshots) : 0.0,
                         static_cast<unsigned long long>(rp.snapshots > 0 ? rp.snapshotKB / rp.snapshots : 0),
                         rp.governorOn ? "on" : "off", static_cast<long long>(rp.governorRemovableUs),
                         static_cast<long long>(rp.governorIdleUs), static_cast<long long>(rp.governorIdleWithoutUs));
        }
    }
    (void)frame;

    if (g_compProfOn) CompProfReport(s_worldFrame);

    s_updMsSum = s_drawMsSum = 0; s_updN = s_drawN = 0; s_simIters = 0;
    PipeWindowReset();
    s_updSamples.clear(); s_frameSamples.clear();
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
extern "C" void StressHarness_Init() {
    HomeWatchInit();
    Resolve();
    if (!s_enabled) return;
    // The stress-town default block (AUTOSTART/MAP_SIZE/SEED/DIFFICULTY/CLIMATE/
    // FPS_CAP/FORCE_SPEED/SIMPROF/PAUSEMENU*/CAMERA_PIN/BUILD_DEMO_*/
    // CROPFIELD_FORCE_START/ORCHARD_FORCE_START — "set only what the caller left
    // unset") now runs in src/app/options.cpp's ApplyStressDefaults(), called
    // from OptionsInit() before anything else reads the environment.
    std::fprintf(stderr, "[Stress] ENABLED citizens=%d sets=%d speed=%d frames=%d interval=%d "
                         "foodMult=%d staff=%d compprof=%d csv=%s\n",
                 s_citizens, s_sets, s_speed, s_frames, s_interval, s_foodMult, s_staff,
                 g_compProfOn, s_csvPath);
    std::fflush(stderr);
}

extern "C" bool StressHarness_Enabled()        { Resolve(); return s_enabled == 1; }
extern "C" int  StressHarness_TargetCitizens() { Resolve(); return s_enabled == 1 ? s_citizens : 0; }
// A NEGATIVE BANISHED_STRESS_SETS selects the spread layout (PFBENCH): |sets| sets.
extern "C" int  StressHarness_Sets()           { Resolve(); return s_enabled == 1 ? (s_sets < 0 ? -s_sets : s_sets) : 0; }
extern "C" bool StressHarness_Spread()         { Resolve(); return s_enabled == 1 && s_sets < 0; }
extern "C" int  StressHarness_FoodMult()       { Resolve(); return s_enabled == 1 ? s_foodMult : 1; }

extern "C" void StressHarness_UpdateBegin(int frameNum) {
    if (!StressHarness_Enabled()) return;
    (void)frameNum;
    ++s_pfFrame;   // E2E SPLIT
    s_updT0 = QpcNow();
    if (!s_runT0) s_runT0 = s_updT0;
}

extern "C" void StressHarness_UpdateEnd(GameUpdateLoop* gul, int frameNum) {
    if (!StressHarness_Enabled() || !s_updT0) return;
    const long long now = QpcNow();
    const double ms = QpcMs(now - s_updT0);
    s_updMsSum += ms; ++s_updN;
    if (s_updSamples.size() < 8192) s_updSamples.push_back((float)ms);
    ++s_hCur.updates; s_hCur.updMs += ms;   // HITCH
    HitchDrainPhases();

    PlayerToolManager* eui = gul->m_toolManager;                        // +0x120
    int frameSpeed = 0;
    if (eui && eui->paused == 0) {                                       // +0x30
        int spd = eui->field_0x34;                                       // +0x34
        if (spd > 0) { s_simIters += spd; frameSpeed = spd; }
    }
    s_hCur.speed = frameSpeed;   // HITCH
    // PFBENCH: simulated seconds = this frame's m_gameTime step x its speed (Update adds
    // the frame's Time once and runs `speed` iterations of it).
    {
        const long long gt = gul->m_gameTime;                            // +0x38
        if (s_lastGameTime >= 0 && gt > s_lastGameTime && frameSpeed > 0)
            s_simTicks += (gt - s_lastGameTime) * frameSpeed;
        s_lastGameTime = gt;
    }

    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    if (gw != s_world) {
        s_world = gw; s_worldSeenAt = frameNum; ++s_worldNo;
        s_runT0 = now; s_lastRow = 0; s_staffPasses = 0;
        s_updMsSum = s_drawMsSum = 0; s_updN = s_drawN = 0; s_simIters = 0;
        PipeWindowReset();
        s_updSamples.clear(); s_frameSamples.clear();
        s_simTicks = 0; s_lastGameTime = gul->m_gameTime;   // PFBENCH
        PfResetWindow();
        HitchReset(); s_hCur.markers |= kHmNewWorld;        // HITCH
        std::fprintf(stderr, "[Stress] world#%d %p first ticked at process frame %d\n",
                     s_worldNo, (void*)gw, frameNum);
    }
    s_worldFrame = frameNum - s_worldSeenAt + 1;
    s_hCur.frame = s_worldFrame;                               // HITCH
    if (s_worldFrame % 1800 == 0) s_hCur.markers |= kHmFrame1800;

    const long long harnessT0 = QpcNow();   // HITCH: the harness's own work below is not the game's
    if (gw && s_homeWatch) HomeShadowScan(gw);
    if (s_worldFrame % s_interval == 0) EmitRow(gul, frameNum);
    if (gw && s_worldFrame % s_interval == 0) FoodCensus(gw, s_worldFrame);
    if (s_worldFrame % s_interval == 0 && Ext::SpreadWarmth::g_on) {   // HITCH_PFBURST: ext.spread_warmth, cumulative
        const Ext::SpreadWarmth::Stats& w = Ext::SpreadWarmth::GetStats();
        std::fprintf(stderr, "[Stress::warmth] frame=%d scans=%lld candidates=%lld searches=%lld maxFrameSearches=%d deferrals=%lld framesDeferring=%d maxStreak=%d budget=%d/%d\n",
                     s_worldFrame, w.scans, w.charged, w.searches, w.maxFrameSearches, w.deferrals, w.framesDeferring, w.maxStreak,
                     Ext::SpreadWarmth::kMissBudget, Ext::SpreadWarmth::kCandidateBudget);
    }
    if (s_worldFrame % s_interval == 0 && Ext::DrainSplit::g_on) {   // HITCH_DRAINSPLIT: ext.drain_split, cumulative
        const Ext::DrainSplit::Stats& d = Ext::DrainSplit::GetStats();
        std::fprintf(stderr, "[Stress::drainsplit] frame=%d splits=%lld runs=%lld forced=%lld dropped=%lld overlaps=%lld "
                     "gapTicks=%lld maxGapTicks=%d citizensAtDrain=%lld gone=%lld born=%lld changed=%lld "
                     "home=%lld work=%lld prof=%lld idle=%lld\n",
                     s_worldFrame, d.splits, d.runs, d.runsForced, d.dropped, d.overlaps, d.gapTicks, d.maxGapTicks,
                     d.citizensAtDrain, d.gone, d.born, d.anyChanged, d.homeChanged, d.workChanged, d.profChanged,
                     d.idleChanged);
    }
    if (gw && s_worldFrame >= kFoodFirst && (s_worldFrame - kFoodFirst) % kFoodPeriod == 0) FoodPass(gw);

    if (s_staff && gw && s_worldFrame >= kStaffFirst) {
        const int since = s_worldFrame - kStaffFirst;
        const int period = s_worldFrame < kStaffDense ? kStaffRetry : 1800;
        if (since % period == 0) { s_hCur.markers |= kHmStaff; StaffPass(gw, s_worldFrame); }   // HITCH marker
    }
    s_hCur.harnessMs += QpcMs(QpcNow() - harnessT0);
}

extern "C" void StressHarness_DrawBegin() {
    if (!StressHarness_Enabled()) return;
    static bool s_markedDrawThread = false;   // DRAWSORT: the SetSizeCore counter's thread
    if (!s_markedDrawThread) { s_markedDrawThread = true; rtcoll::SetSizeProf_MarkThisThread(); }
    const long long now = QpcNow();
    if (s_lastDrawEntry) {
        const double period = QpcMs(now - s_lastDrawEntry);
        if (s_frameSamples.size() < 8192) s_frameSamples.push_back((float)period);
        if (HitchOn()) HitchPeriod(s_lastDrawEntry, now);   // HITCH: close the period
    }
    s_lastDrawEntry = now;
    s_drawT0 = now;
}

extern "C" void StressHarness_DrawEnd() {
    if (!StressHarness_Enabled() || !s_drawT0) return;
    const double ms = QpcMs(QpcNow() - s_drawT0);
    s_drawMsSum += ms; ++s_drawN;
    s_hCur.drawMs += ms;   // HITCH
}

// RENDERPIPE: a pipelined frame's Draw bracket ends with its main half (prelude +
// pass 1); its packet's share is added to draw_ms_mean when the packet is emitted,
// so draw_ms_mean stays the same span as the faithful column (GameUpdateLoop::Draw).
extern "C" void StressHarness_PipeMainEnd() {
    if (!StressHarness_Enabled() || !s_drawT0) return;
    const double ms = QpcMs(QpcNow() - s_drawT0);
    s_drawMsSum += ms; ++s_drawN;
    s_pipeMainSum += ms; ++s_pipeMainN;
    s_hCur.drawMs += ms;   // HITCH (the main half; the packet runs on the emitting thread)
}
extern "C" void StressHarness_PipePacketBegin() {
    if (!StressHarness_Enabled()) return;
    s_pipePacketT0 = QpcNow();
}
extern "C" void StressHarness_PipePacketEnd() {
    if (!StressHarness_Enabled() || !s_pipePacketT0) return;
    s_pipePacketTicks.fetch_add(QpcNow() - s_pipePacketT0, std::memory_order_relaxed);   // folded into draw_ms at EmitRow
    s_pipePacketN.fetch_add(1, std::memory_order_relaxed);
    s_pipePacketT0 = 0;
}
extern "C" void StressHarness_PipeEmitBegin() {
    if (!StressHarness_Enabled()) return;
    s_pipeEmitT0 = QpcNow();
}
extern "C" void StressHarness_PipeEmitEnd() {
    if (!StressHarness_Enabled() || !s_pipeEmitT0) return;
    s_pipeEmitTicks.fetch_add(QpcNow() - s_pipeEmitT0, std::memory_order_relaxed);
    s_pipeEmitN.fetch_add(1, std::memory_order_relaxed);
    s_pipeEmitT0 = 0;
}

static void CompProf_WatchBegin(Object::Base* comp, uint32_t tid) {
    if (!s_tidResolved) {
        s_tidResolved = true;
        s_tidCitizen   = CitizenComponent::TypeId();
        s_tidCitizenAI = CitizenAIComponent::TypeId();
    }
    s_curComp = comp; s_curTid = tid; s_curCitizen = nullptr; s_curHome = nullptr;
    // The type id just matched names the class (B10 downcast).
    CitizenComponent* cz = nullptr;
    if (tid == s_tidCitizen) cz = static_cast<CitizenComponent*>(comp);
    else if (tid == s_tidCitizenAI) cz = static_cast<CitizenAIComponent*>(comp)->citizen.Get();   // +0x38
    if (cz && Readable(cz)) {
        s_curCitizen = cz;
        s_curHome = cz->home.Get();                                            // +0x238
    }
}

extern "C" int CompProf_BeginKind(Object::Base* comp, int kind) {
    void* vtbl = *reinterpret_cast<void**>(comp);   // vtable probe (the profiler keys its table on the vtable word)  // CAST-OK: crash_walk
    uintptr_t h = ((reinterpret_cast<uintptr_t>(vtbl) >> 4) + (uintptr_t)kind * 0x9E37u) & (kCompSlots - 1);  // CAST-OK: hash
    for (int probe = 0; probe < kCompSlots; ++probe) {
        const int idx = (int)((h + probe) & (kCompSlots - 1));
        CompSlot& s = s_comp[idx];
        if (s.vtbl == vtbl && s.kind == kind) { if (!kind && s_homeWatch) CompProf_WatchBegin(comp, s.typeId); return idx; }
        if (!s.vtbl) {
            s.vtbl = vtbl; s.kind = kind;
            s.typeId = comp->Type();
            s.ticks = 0; s.calls = 0;
            if (!kind && s_homeWatch) CompProf_WatchBegin(comp, s.typeId);
            return idx;
        }
    }
    return -1;
}

extern "C" int CompProf_Begin(Object::Base* comp) { return CompProf_BeginKind(comp, 0); }

extern "C" void CompProf_End(int slot, long long qpcTicks) {
    if (s_homeWatch && s_curCitizen && s_curHome && Readable(s_curCitizen) &&
        s_curCitizen->home == nullptr) {                                  // +0x238
        const char* tn = ::Object::GetTypeNameById(s_curTid);
        char where[96];
        std::snprintf(where, sizeof(where), "IN-UPDATE(%s comp=%p)", tn ? tn : "?", s_curComp);
        LogHomeLoss(s_curCitizen, where);
        // Mark the shadow so the per-frame scan does not report it a second time.
        for (auto& pr : s_homeShadow) if (pr.first == s_curCitizen) { pr.second = nullptr; break; }
    }
    s_curComp = nullptr; s_curCitizen = nullptr; s_curHome = nullptr;
    if (slot < 0) return;
    s_comp[slot].ticks += qpcTicks;
    ++s_comp[slot].calls;
}

// OFFSCREEN (char_prof.h). Token 0 = not counted, 1 = a CharacterComponent tick, 2 = the
// ModelComponent tick of an entity that has a CharacterComponent, 3 = a StateMachineComponent
// tick (SMMEMO's /sm row).
extern "C" int CharProf_Begin(Object::Base* comp, int slot) {
    if (slot < 0) return 0;
    CharProfResolveTids();
    const uint32_t tid = s_comp[slot].typeId;
    if (tid == s_tidChar) {
        CharacterComponent* ch = static_cast<CharacterComponent*>(comp);   // the slot's type id named the class
        const ComponentEntity* ent = ch->_entity.Get();
        if (s_charVisit.ent != ent || s_charVisit.node != g_elProfTreeNodes) CharProfClassify(ch, ent);
        CharPending& p = s_charPend;
        p.ch = ch;
        p.model = ch->modelComponent.Get();
        if (p.model) { p.rot = p.model->transform.rotation; p.pos = p.model->transform.translation; p.yaw = p.model->yaw; }
        p.current = ch->actionStack.current;
        p.head = p.current >= 0 ? ch->actionStack.actions[p.current].Get() : nullptr;
        p.last = !Ext::RenderCulling::RootOnlyNow();
        p.fast0 = StillPath::g_fastTicks;
        p.tzQ0 = StillTerrain::g_queries; p.tzH0 = StillTerrain::g_hits;
        return 1;
    }
    if (tid == s_tidModel) {
        const ComponentEntity* ent = static_cast<ModelComponent*>(comp)->_entity.Get();
        if (!ent) return 0;
        if (s_charVisit.ent == ent && s_charVisit.node == g_elProfTreeNodes) return 2;
        CharacterComponent* ch = const_cast<ComponentEntity*>(ent)->FindComponent<CharacterComponent>();  // CAST-OK: const_api (a diagnostic read through the non-const lookup)
        if (!ch) return 0;
        CharProfClassify(ch, ent);
        return 2;
    }
    if (tid == s_tidSM) {   // SMMEMO: was this slot-8 tick skipped?
        s_smSkip0 = SmMemo::g_skipped;
        return 3;
    }
    if (tid == s_tidPart) {   // PARTREPEAT step 0: first or repeat tick of this render frame?
        const ParticleComponent* pc = static_cast<const ParticleComponent*>(comp);   // the slot's type id named the class
        const int frame = GameUpdateLoop_CurrentFrame();
        const uint32_t h = (uint32_t)((std::hash<const void*>{}(pc) >> 4) * 2654435761u) & (kPartSeenCap - 1);
        bool repeat = false;
        for (int probe = 0; probe < kPartSeenCap; ++probe) {
            PartSeen& sl = s_partSeen[(h + probe) & (kPartSeenCap - 1)];
            if (sl.frame != frame) { sl.frame = frame; sl.key = pc; break; }
            if (sl.key == pc) { repeat = true; break; }
        }
        s_partRepeat = repeat;
        s_partPc = pc; s_partStamp0 = pc->_pad24; s_partFrame = frame;
        s_partLowered0 = PartUnstamp::g_lowered;
        s_partQuiet = false;
        s_partLive = 0;
        if (repeat) {
            bool quiet = true;
            const int fi = ::Graphics::ParticleInstance::_frameIndex;
            const ParticleEmitterSlot* arr = pc->emitters._data;
            for (int i = 0; arr && i < pc->emitters._size; ++i) {
                const ParticleEmitterSlot& e = arr[i];
                if (!e.instance) continue;
                if (e.nodeIndex >= 0) { quiet = false; break; }
                if (e.sceneId < 0) continue;
                ++s_partLive;
                const ::Graphics::ParticleInstance* g = e.instance;
                if (e.fadeRate != 0.0f || !(g->_opacity > 0.0f) || !g->_resource ||
                    g->_resource->_active == 0 || g->_index < 0 ||
                    g->_resource->_records._data[g->_index]->frameTag != fi) { quiet = false; break; }
            }
            s_partQuiet = quiet;
        }
        return 4;
    }
    return 0;
}

static inline bool SameBits(const void* a, const void* b, size_t n) { return std::memcmp(a, b, n) == 0; }

extern "C" void CharProf_End(int token, long long tscTicks) {
    if (token == 2) { s_charWin.AddModel(s_charVisit.vis, s_charVisit.cls, tscTicks); return; }
    if (token == 3) { s_smWin.Add(tscTicks, SmMemo::g_skipped != s_smSkip0); return; }
    if (token == 4) {   // PARTREPEAT step 0
        PartWindow& w = s_partWin;
        const uint32_t token = static_cast<uint32_t>(s_partFrame);
        if (s_partFrame != 0 && s_partStamp0 == token) ++w.stampHits;
        else if (s_partPc->_pad24 == token) ++w.stampFirst;
        else ++w.stampNone;
        w.lowered += PartUnstamp::g_lowered - s_partLowered0;
        if (!s_partRepeat) { ++w.firstCalls; w.firstTicks += tscTicks; return; }
        ++w.repCalls; w.repTicks += tscTicks;
        if (s_partQuiet) { ++w.quietCalls; w.quietTicks += tscTicks; w.quietLive += s_partLive; }
        return;
    }
    if (token != 1) return;
    const CharPending& p = s_charPend;
    bool moved = false, still = false;
    if (p.model) {
        const Math::Transform& xf = p.model->transform;
        moved = !SameBits(&xf.translation, &p.pos, sizeof(p.pos));
        still = !moved && SameBits(&xf.rotation, &p.rot, sizeof(p.rot)) &&
                SameBits(&p.model->yaw, &p.yaw, sizeof(p.yaw));
    }
    const ActionStack& st = p.ch->actionStack;   // the character is alive: removal is deferred to the drain
    const Action* head = st.current >= 0 ? st.actions[st.current].Get() : nullptr;
    const bool transition = st.current != p.current || head != p.head;
    const bool fast = StillPath::g_fastTicks != p.fast0;
    if (StillTerrain::g_queries != p.tzQ0) s_tzWin.Add(tscTicks, StillTerrain::g_hits != p.tzH0);
    s_charWin.AddChar(s_charVisit.vis, s_charVisit.cls, tscTicks, p.last, moved, still, transition, fast);
    s_charWin.AddNode(s_charVisit.nodeType, s_charVisit.vis, tscTicks);
}

// ── PFBENCH hooks (stress_harness.h). Each runs only when g_pfProfOn is set. ──────
extern "C" void PfProf_Enqueue(int slot, int queueDepth) {
    if (slot >= 0 && slot < kPfSlots) {
        s_pfEnqTsc[slot] = CompProf_Now();
        s_pfReadyTsc[slot] = 0;
        s_pfEnqFrame[slot] = s_pfFrame;
    }
    std::lock_guard<std::mutex> g(s_pfLock);
    s_pfMain.Enqueued(queueDepth);
}

// The worker's publish (ready = 1 in ApplyCommand case 5; the pool and ext.fast_paths
// share that site). Unlocked per-slot stamp, read by PfProf_Taken on the main thread
// after it has seen the ready byte.
extern "C" void PfProf_Ready(int slot) {
    if (slot >= 0 && slot < kPfSlots) s_pfReadyTsc[slot] = CompProf_Now();
}

extern "C" void PfProf_Search(int slot, long long t0, long long t1, int cells, int expanded) {
    PfStats::Request r;
    r.searchTicks = t1 - t0;
    r.waitTicks   = -1;
    if (slot >= 0 && slot < kPfSlots && s_pfEnqTsc[slot] != 0 && t0 >= s_pfEnqTsc[slot])
        r.waitTicks = t0 - s_pfEnqTsc[slot];
    r.expanded = expanded;
    r.cells    = cells;
    std::lock_guard<std::mutex> g(s_pfLock);
    s_pfWin.Add(r);
}

extern "C" void PfProf_Taken(int slot) {
    if (slot < 0 || slot >= kPfSlots || s_pfEnqTsc[slot] == 0) return;
    const long long now = CompProf_Now();
    const long long e2e = now - s_pfEnqTsc[slot];
    const long long ready = s_pfReadyTsc[slot];
    const long long toReady = (ready > 0 && ready >= s_pfEnqTsc[slot]) ? ready - s_pfEnqTsc[slot] : -1;
    const long long toTaken = (ready > 0 && now >= ready) ? now - ready : -1;
    const int framesCrossed = s_pfFrame - s_pfEnqFrame[slot];
    s_pfEnqTsc[slot] = 0;   // taken: the slot is free until its next enqueue stamps it
    s_pfReadyTsc[slot] = 0;
    std::lock_guard<std::mutex> g(s_pfLock);
    s_pfMain.Taken(e2e);
    s_pfMain.TakenSplit(toReady, toTaken, framesCrossed);
}

extern "C" void PfProf_Main(int which, long long ticks) {
    if (which < 0 || which > PF_MAIN_SYNCWAIT || ticks <= 0) return;
    s_pfMainTicks[which].fetch_add(ticks, std::memory_order_relaxed);
    s_hCur.pf[which] += ticks;   // HITCH: main thread only (the three brackets are main-thread)
}

PathGraphCostCounts g_pathGraphCost = { 0, 0, 0, 0, -1 };

extern "C" void StressHarness_PathGraphDrain(int pairs, long long reassignTicks, long long assignTicks,
                                             int costCalls, int cacheHits, int cacheMisses, int direct,
                                             int differ) {
    if (!g_pfProfOn) return;
    std::fprintf(stderr, "[Stress::pathgraph] worldFrame=%d pairs=%d reassign=%.3fms assign=%.3fms "
                 "costCalls=%d hits=%d misses=%d direct=%d differ=%d\n",
                 s_worldFrame, pairs, TscMs(reassignTicks), TscMs(assignTicks),
                 costCalls, cacheHits, cacheMisses, direct, differ);
}

extern "C" void StressHarness_PathShadow(long long retailTicks, long long extTicks,
                                         int retailExpanded, int extExpanded,
                                         long long retailCost, long long extCost, int sameCells) {
    if (!g_pfProfOn) return;
    PfStats::Shadow s;
    s.retailTicks = retailTicks; s.extTicks = extTicks;
    s.retailExpanded = retailExpanded; s.extExpanded = extExpanded;
    s.retailCost = retailCost; s.extCost = extCost; s.sameCells = sameCells;
    std::lock_guard<std::mutex> g(s_pfLock);
    s_pfWin.AddShadow(s);
}

// For the heap watchdog's report: what the sim was doing when corruption was seen.
extern "C" void StressHarness_Context(char* buf, int n) {
    if (!buf || n <= 0) return;
    const char* tn = s_curComp ? ::Object::GetTypeNameById(s_curTid) : nullptr;
    std::snprintf(buf, (size_t)n, "world#%d frame=%d inUpdate=%s comp=%p citizen=%p",
                  s_worldNo, s_worldFrame, tn ? tn : (s_curComp ? "?" : "-"), s_curComp,
                  (void*)s_curCitizen);
}
