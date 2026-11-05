// ============================================================================
// src/app/res_load_lock.cpp — the resource-loader serialize lock
//                                (Wave 9 lane FNHOOK)
// ============================================================================
//
// Rationale, measurements and the rule that a ported FindOrLoad MUST take this
// in its own body: src/app/res_load_lock.h. Read that first.
//
// INITIALIZATION. `InitializeCriticalSection` on a recursive CS, run exactly
// once, from whichever thread gets here first. The old code initialized inside
// `InstallFindOrLoadWatch()`, which is a place that does not necessarily run —
// and at cutover definitely does not do anything useful. A ported body taking
// an uninitialized CRITICAL_SECTION is undefined behaviour that would present
// as a random hang or a corrupt wait list, i.e. exactly the failure mode the
// lock exists to remove, so the init cannot be anybody's install-time
// side effect.
//
// `InitOnceExecuteOnce` (Platform::OnceRun) is used rather than a function-local `static` because
// this may be entered from a Runtime-created loader thread, and MinGW's
// thread-safe-statics guard pulls in the GCC runtime's own once machinery on a
// thread whose stack and TLS were not set up by our CRT (PLAN.md §4.3 risk 5,
// "keep C++ exceptions and CRT-dependent machinery out of detoured code").
// The Win32 primitive has no such dependency.
// ============================================================================

#include "res_load_lock.h"
#include "options.h"

#include "platform_sync.h"
#include <cstdlib>
#include <cstdio>

namespace {

Platform::LockStorage g_resCS;               // a CRITICAL_SECTION on Windows
Platform::OnceFlag    g_resOnce = {nullptr}; // INIT_ONCE_STATIC_INIT
bool                  g_armed   = true;      // valid only after the once-init has run

void ResLoadInitOnce() {
    // Read the kill switch ONCE and cache it. Reading it per call would let a
    // mid-run _putenv_s turn the lock off between an acquire and its release —
    // a leaked lock rather than an A/B.
    g_armed = !Opt().noFolLock;
    if (g_armed) {
        // Spin count 0: contention here is a real load on another thread doing
        // file I/O, so spinning burns a core for nothing.
        Platform::MutexInit(&g_resCS, 0);   // InitializeCriticalSection, no spin count
    } else {
        fprintf(stderr, "[ResLoad] BANISHED_NO_FOL_LOCK set — resource loader "
                        "NOT serialized (A/B only; ~33%% menu-crash baseline)\n");
        fflush(stderr);
    }
}

inline void EnsureInit() {
    Platform::OnceRun(&g_resOnce, &ResLoadInitOnce);   // InitOnceExecuteOnce
}

}  // namespace

ResLoadCapability ResLoad;   // the analysis's token (res_load_lock.h); empty, never read

// Not analysed: the capability is the ResLoad token, the lock taken is g_resCS (or none, unarmed).
void ResLoadLockAcquire() NO_THREAD_SAFETY_ANALYSIS {
    EnsureInit();
    if (g_armed) Platform::MutexLock(&g_resCS);     // EnterCriticalSection
}

void ResLoadLockRelease() NO_THREAD_SAFETY_ANALYSIS {
    // No EnsureInit: a release without a matching acquire is a bug, and
    // initializing here would hide it behind a Leave on a fresh CS.
    if (g_armed) Platform::MutexUnlock(&g_resCS);   // LeaveCriticalSection
}

bool ResLoadLockArmed() {
    EnsureInit();
    return g_armed;
}
