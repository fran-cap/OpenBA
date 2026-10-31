// ============================================================================
// src/platform/android/perf_hint_android.cpp -- ADPF sessions and the thermal log
// ============================================================================
// NOT RETAIL. Replaces src/platform/posix/perf_hint_posix.cpp (platform_perf_hint.h has the contract).
//
// APerformanceHint_* is API 31 (setPreferPowerEfficiency API 35) and AThermal_* is API 30; minSdk is
// 29, so libandroid.so is asked for every function once at run time (dlsym, as window_android.cpp
// does for ANativeWindow_setFrameRate). An absent function means an absent feature: no session, no
// thermal log. A session is one thread's promise ("a frame's work fits `target` ns") followed by each
// frame's actual work, from which the system's governor picks that thread's core and clock.
//
// Threads: a role's session is made, retargeted, reported to and closed only by the role's thread
// (AttachThread / WorkEnd / DetachThread), so no session is ever shared; the only cross-thread datum
// is the target (an atomic, written by the frame limiter on the main thread). The work time is the
// thread's CPU time (CLOCK_THREAD_CPUTIME_ID) over the WorkBegin / WorkEnd bracket: blocking waits in
// the bracket (vkAcquireNextImageKHR / present, the render pipe's hand-off, a fence) cost no CPU time
// and are not work; the frame limiter's own wait is outside the bracket.
//
// The fastest cores: the two roles' threads are also the frame's critical path, and the scheduler
// keeps them off a heterogeneous SoC's biggest cores (the S25 stress town at the 60 cap: the main
// thread at 92% of a 3.53 GHz core, 0 of 120 samples on the 4.47 GHz pair). AttachThread pins each to
// the CPUs of the largest cpu_capacity when there are at least two of them (one core per thread;
// pinning the main thread alone left the render thread on a cluster whose shared clock then fell,
// and the frame gained nothing). Measured at 803 citizens: update 6.3 -> 4.6 ms, draw 6.3 -> 5.0 ms.
// With a single CPU of the largest capacity (the S10's lone prime) the pin is the next tier with two
// or more (its gold cluster): the two threads then share one clock domain, which neither leaves for
// the lone core (core_ctl wakes it for one thread and the other's cluster falls to its floor clock:
// docs/android/P13_THERMAL.md). A device whose largest tier has two (the S25) is pinned as before.
// The threads either of them creates start on the process's mask again (thread_start_android.h).
// ============================================================================

#include "platform_perf_hint.h"
#include "platform_cpu.h"            // Platform::CpuAffinity / CpuCoreCapacity
#include "thread_start_android.h"    // Platform::Android::SetThreadStartMask

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <dlfcn.h>
#include <mutex>
#include <sched.h>
#include <time.h>
#include <unistd.h>

namespace Platform {
namespace PerfHint {

namespace {

using GetManagerFn = void* (*)();
using CreateSessionFn = void* (*)(void* manager, const int32_t* threadIds, size_t size, int64_t targetNs);
using UpdateTargetFn = int (*)(void* session, int64_t targetNs);
using ReportActualFn = int (*)(void* session, int64_t actualNs);
using CloseSessionFn = void (*)(void* session);
using PreferEfficiencyFn = int (*)(void* session, bool enabled);   // API 35
using ThermalCallback = void (*)(void* data, int status);
using ThermalAcquireFn = void* (*)();
using ThermalRegisterFn = int (*)(void* manager, ThermalCallback callback, void* data);
using ThermalCurrentFn = int (*)(void* manager);

CreateSessionFn    g_createSession    = nullptr;
UpdateTargetFn     g_updateTarget     = nullptr;
ReportActualFn     g_reportActual     = nullptr;
CloseSessionFn     g_closeSession     = nullptr;
PreferEfficiencyFn g_preferEfficiency = nullptr;
void*              g_hintManager      = nullptr;   // non-null: sessions can be made (set once, in Resolve)

std::once_flag       g_resolveOnce;
std::atomic<int64_t> g_targetNs{0};    // SetTarget's budget (0 = not paced)
std::atomic<int>     g_thermalStatus{-1};

// One role's session. Touched only by the role's thread (see the file comment).
struct Slot {
    const char* name          = "";
    int32_t     tid           = 0;         // AttachThread's caller
    void*       session       = nullptr;
    int64_t     appliedTarget = 0;         // the target the session was last given
    int64_t     cpuBegin      = -1;        // WorkBegin's thread CPU time, ns (-1 = no open bracket)
    bool        refused       = false;     // createSession failed for this tid: not asked again
};
Slot g_slot[2] = { { "main" }, { "render" } };   // by Role

Slot& SlotOf(Role role) {
    return g_slot[static_cast<int>(role)];
}

int64_t ThreadCpuNs() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

// The thermal service's callback, on a binder thread: the status is stored and a change logged.
void ThermalChanged(void* data, int status) {
    (void)data;
    const int before = g_thermalStatus.exchange(status);
    if (before != status) {
        std::fprintf(stderr, "[Thermal] status %d\n", status);
        std::fflush(stderr);
    }
}

void StartThermalLog(void* lib) {
    const auto acquire = reinterpret_cast<ThermalAcquireFn>(dlsym(lib, "AThermal_acquireManager"));  // CAST-OK: dlsym
    const auto listen = reinterpret_cast<ThermalRegisterFn>(dlsym(lib, "AThermal_registerThermalStatusListener"));  // CAST-OK: dlsym
    const auto current = reinterpret_cast<ThermalCurrentFn>(dlsym(lib, "AThermal_getCurrentThermalStatus"));  // CAST-OK: dlsym
    void* manager = acquire != nullptr && listen != nullptr && current != nullptr ? acquire() : nullptr;
    if (manager == nullptr) {
        std::fprintf(stderr, "[Thermal] not available (AThermal_* absent: API < 30)\n");
        std::fflush(stderr);
        return;
    }
    const int status = current(manager);
    g_thermalStatus.store(status);
    std::fprintf(stderr, "[Thermal] status %d (at start)\n", status);
    // The manager is kept for the process's life (the listener belongs to it).
    if (listen(manager, &ThermalChanged, nullptr) != 0) {
        std::fprintf(stderr, "[Thermal] the listener was refused: no change will be logged\n");
    }
    std::fflush(stderr);
}

void Resolve() {
    void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_NOLOAD);
    if (lib == nullptr) lib = dlopen("libandroid.so", RTLD_NOW);
    if (lib == nullptr) {
        std::fprintf(stderr, "[ADPF] not available (libandroid.so not loaded): no session\n");
        std::fflush(stderr);
        return;
    }
    const auto getManager = reinterpret_cast<GetManagerFn>(dlsym(lib, "APerformanceHint_getManager"));  // CAST-OK: dlsym
    g_createSession = reinterpret_cast<CreateSessionFn>(dlsym(lib, "APerformanceHint_createSession"));  // CAST-OK: dlsym
    g_updateTarget = reinterpret_cast<UpdateTargetFn>(dlsym(lib, "APerformanceHint_updateTargetWorkDuration"));  // CAST-OK: dlsym
    g_reportActual = reinterpret_cast<ReportActualFn>(dlsym(lib, "APerformanceHint_reportActualWorkDuration"));  // CAST-OK: dlsym
    g_closeSession = reinterpret_cast<CloseSessionFn>(dlsym(lib, "APerformanceHint_closeSession"));  // CAST-OK: dlsym
    g_preferEfficiency = reinterpret_cast<PreferEfficiencyFn>(dlsym(lib, "APerformanceHint_setPreferPowerEfficiency"));  // CAST-OK: dlsym
    const bool all = getManager != nullptr && g_createSession != nullptr && g_updateTarget != nullptr &&
                     g_reportActual != nullptr && g_closeSession != nullptr;
    if (all) g_hintManager = getManager();
    if (g_hintManager == nullptr) {
        std::fprintf(stderr, "[ADPF] not available (%s): no session\n",
                     all ? "APerformanceHint_getManager returned none" : "APerformanceHint_* absent: API < 31");
        std::fflush(stderr);
    }
    StartThermalLog(lib);
}

void Close(Slot& s) {
    if (s.session == nullptr) return;
    g_closeSession(s.session);
    std::fprintf(stderr, "[ADPF] %s session closed (tid %d)\n", s.name, static_cast<int>(s.tid));
    std::fflush(stderr);
    s.session = nullptr;
}

// The session of `s`'s thread, made with `targetNs`. One [ADPF] line either way.
void Create(Slot& s, int64_t targetNs) {
    s.session = g_createSession(g_hintManager, &s.tid, 1, targetNs);
    if (s.session == nullptr) {
        s.refused = true;
        std::fprintf(stderr, "[ADPF] %s session refused (tid %d, target %lld ns)\n", s.name,
                     static_cast<int>(s.tid), static_cast<long long>(targetNs));
        std::fflush(stderr);
        return;
    }
    s.appliedTarget = targetNs;
    // With the target met, the system may run the thread on efficient cores (API 35).
    const char* efficiency = "absent (API < 35)";
    if (g_preferEfficiency != nullptr) {
        efficiency = g_preferEfficiency(s.session, true) == 0 ? "on" : "refused";
    }
    std::fprintf(stderr, "[ADPF] %s session: APerformanceHint resolved, tid %d, target %lld ns, "
                         "preferPowerEfficiency %s\n",
                 s.name, static_cast<int>(s.tid), static_cast<long long>(targetNs), efficiency);
    std::fflush(stderr);
}

// The fastest cores (the file comment): found once, from the first attaching thread's mask (the main
// thread's, before any pin: the process's).
std::once_flag g_fastOnce;
uint64_t       g_processMask = 0;
uint64_t       g_fastMask    = 0;   // 0 = no pin on this device

void FindFastCores() {
    uint64_t system = 0;
    if (!CpuAffinity(&g_processMask, &system) || g_processMask == 0) {
        std::fprintf(stderr, "[Pin] not pinned: the affinity mask is unknown\n");
        return;
    }
    int capacity[64] = {};
    for (int c = 0; c < 64; ++c) {
        if (((g_processMask >> c) & 1u) == 0 || !CpuCoreCapacity(c, &capacity[c])) capacity[c] = 0;
    }
    // The tiers from the largest capacity down; the first with two CPUs or more is the pin.
    int tier = 0;
    int above = 0;   // a larger tier skipped for having one CPU (0 = none)
    uint64_t fast = 0;
    for (int ceiling = INT32_MAX;;) {
        tier = 0;
        for (int c = 0; c < 64; ++c) {
            if (capacity[c] > tier && capacity[c] < ceiling) tier = capacity[c];
        }
        if (tier == 0) break;
        fast = 0;
        for (int c = 0; c < 64; ++c) {
            if (capacity[c] == tier) fast |= uint64_t{1} << c;
        }
        if (__builtin_popcountll(fast) >= 2) break;
        if (above == 0) above = tier;
        ceiling = tier;
    }
    const int count = tier > 0 ? __builtin_popcountll(fast) : 0;
    if (count < 2 || fast == g_processMask) {
        std::fprintf(stderr, "[Pin] not pinned: no tier of two or more CPUs below the whole mask 0x%llx\n",
                     static_cast<unsigned long long>(g_processMask));
        return;
    }
    if (above > 0) {
        std::fprintf(stderr, "[Pin] the largest capacity %d has one CPU: the tier of capacity %d (%d CPUs)\n",
                     above, tier, count);
    }
    g_fastMask = fast;
    Android::SetThreadStartMask(g_processMask);
}

// The calling thread onto the fastest cores, when the device has them.
void PinToFastCores(const Slot& s) {
    std::call_once(g_fastOnce, FindFastCores);
    if (g_fastMask == 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int c = 0; c < 64; ++c) {
        if ((g_fastMask >> c) & 1u) CPU_SET(c, &set);
    }
    const int rc = sched_setaffinity(0, sizeof(set), &set);
    std::fprintf(stderr, "[Pin] %s thread tid %d -> CPUs 0x%llx (of 0x%llx): %s\n", s.name, static_cast<int>(s.tid),
                 static_cast<unsigned long long>(g_fastMask), static_cast<unsigned long long>(g_processMask),
                 rc == 0 ? "pinned" : "refused");
    std::fflush(stderr);
}

}  // namespace

void SetTarget(int64_t targetNs) {
    g_targetNs.store(targetNs > 0 ? targetNs : 0, std::memory_order_relaxed);
}

void AttachThread(Role role) {
    std::call_once(g_resolveOnce, Resolve);
    Slot& s = SlotOf(role);
    const int32_t tid = static_cast<int32_t>(gettid());
    if (s.tid == tid) return;
    // A new thread for the role: a session the old thread left open names a tid that is gone.
    if (g_hintManager != nullptr) Close(s);
    s.tid      = tid;
    s.refused  = false;
    s.cpuBegin = -1;
    PinToFastCores(s);
}

void DetachThread(Role role) {
    Slot& s = SlotOf(role);
    if (g_hintManager != nullptr) Close(s);
    s.tid      = 0;
    s.cpuBegin = -1;
}

void WorkBegin(Role role) {
    if (g_hintManager == nullptr) return;
    SlotOf(role).cpuBegin = ThreadCpuNs();
}

void WorkEnd(Role role) {
    if (g_hintManager == nullptr) return;
    Slot& s = SlotOf(role);
    if (s.cpuBegin < 0 || s.tid == 0) return;
    const int64_t workNs = ThreadCpuNs() - s.cpuBegin;
    s.cpuBegin = -1;
    const int64_t targetNs = g_targetNs.load(std::memory_order_relaxed);
    if (targetNs <= 0 || s.refused) return;
    if (s.session == nullptr) {
        Create(s, targetNs);
        if (s.session == nullptr) return;
    } else if (s.appliedTarget != targetNs) {
        g_updateTarget(s.session, targetNs);
        s.appliedTarget = targetNs;
        std::fprintf(stderr, "[ADPF] %s session target %lld ns\n", s.name, static_cast<long long>(targetNs));
        std::fflush(stderr);
    }
    if (workNs > 0) g_reportActual(s.session, workNs);
}

}  // namespace PerfHint
}  // namespace Platform
