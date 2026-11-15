#pragma once
// ============================================================================
// src/app/res_load_lock.h — the resource-loader serialize lock
//                              (Wave 9 lane FNHOOK)
// ============================================================================
//
// WHAT IT PROTECTS
//
// `Object::ExternalFactory::FindOrLoad` (Runtime 0x1800bf320) and
// `Object::External::Load` (0x1800bc620) are NOT thread-safe, and the game
// calls them from two threads at once during the menu load (two distinct tids,
// measured). `FindOrLoad` mutates the factory's resource AVL tree; `External::
// Load` walks the same tree and does refcounted `System::String`/`Filename`
// traffic while building paths. The race corrupts the data-directory String's
// refcount (the bucket-12 free-list "+0x100000000 bit-32 flip" crash) AND the
// factory cache (a racy Add makes `FindOrLoad` return NULL for a resource that
// exists — the texture-null and dialog-null derefs). Six sessions chased those
// two faces separately; they are one race. Serializing both entry points took
// the full-speed menu crash from ~33% of runs to 0 of 24.
//
// ONE RECURSIVE CRITICAL SECTION, taken at BOTH entry points. Recursive is
// load-bearing: `FindOrLoad` calls `External::Load`, and resource dependencies
// recurse, so a non-recursive lock self-deadlocks on the first nesting. Only
// cross-thread calls actually serialize.
//
// WHERE IT IS TAKEN
//
// The lock lives here and initializes itself on first use, so it needs no install
// site. `ExpandRelativePath` (src/engine/io/external_path.cpp), ext.find_fastpath and
// ext.visit_replay take it; the ported `FindOrLoad` (src/engine/object/
// external_compiled.cpp) and `External::Load` (src/engine/object/external_load.cpp)
// bodies do not take it themselves. The form the rule below asks of them:
//
//     const External* RtObject_ExternalFactory_FindOrLoad(ExternalFactory* self,
//                                                         const Filename* name) {
//         ResLoadGuard guard;          // <-- NOT optional; see below
//         …byte-faithful body…
//     }
//
// TAKING IT IN THE PORTED BODY IS MANDATORY, AND A CALL-SITE LOCK IS NOT A
// SUBSTITUTE. Two independent reasons:
//
//   1. `DetourRuntimeFunc` resolves its patch target RAW, so a registered port
//      at 0x1800bf320 and the DLL detour can coexist while both binaries are
//      loaded — but at cutover the detour is gone and the port IS the only
//      body. If the lock is only in the wrapper, the serialization silently
//      ceases to exist on exactly the build that has no fallback.
//   2. Our ~40 `ExternalFactory::FindOrLoad` call sites in `src/game/` are
//      not the only callers. Ported Runtime bodies (`UpdateLoop::LoadResources`
//      calls it three times) and the loader thread reach it too. A lock spread
//      over call sites is a lock with a hole in it the first time somebody adds
//      a caller.
//
// Recursion makes "both" safe: a body that takes the lock under a caller that
// holds it takes the same CS twice on one thread and proceeds.
//
// KILL SWITCH. `BANISHED_NO_FOL_LOCK=1` disables the serialization — read ONCE,
// at first use, and cached, so the A/B is process-wide and cannot drift
// mid-run. With it set, lock and unlock are no-ops, which is the pre-fix
// behaviour (~33% menu crash) and exists only for A/B measurement.
//
// PORTABILITY. This header is deliberately `<windows.h>`-free so that
// `src/engine/` may include it (PLAN.md §4.1 decision 1). The implementation
// lives in `src/app/res_load_lock.cpp`.
// ============================================================================

#include "system/thread_annotations.h"

// THE CAPABILITY. `ResLoad` names the lock to clang's thread-safety analysis
// (system/thread_annotations.h): code that needs it held says REQUIRES(ResLoad),
// and a caller that does not hold it fails the BANISHED_THREAD_SAFETY build. It
// is a token, never read or written: the lock itself is the Platform lock in
// res_load_lock.cpp. The analysis does not model recursion; nothing re-takes
// the lock within one analysed function.
struct CAPABILITY("mutex") ResLoadCapability {};
extern ResLoadCapability ResLoad;

// Acquire / release the resource-loader lock. Recursive on one thread.
// Both are no-ops when BANISHED_NO_FOL_LOCK is set.
void ResLoadLockAcquire() ACQUIRE(ResLoad);
void ResLoadLockRelease() RELEASE(ResLoad);

// True when the lock is armed (i.e. BANISHED_NO_FOL_LOCK is NOT set). For
// diagnostics only — never branch behaviour on it.
bool ResLoadLockArmed();

// RAII form. Use this in ported bodies; the manual pair is for a walk that lets go
// of the lock and takes it again mid-scope (src/ext/visit_replay/visit_replay.cpp).
struct SCOPED_CAPABILITY ResLoadGuard {
    ResLoadGuard() ACQUIRE(ResLoad) { ResLoadLockAcquire(); }
    ~ResLoadGuard() RELEASE() { ResLoadLockRelease(); }
    ResLoadGuard(const ResLoadGuard&) = delete;
    ResLoadGuard& operator=(const ResLoadGuard&) = delete;
};
