// ============================================================================
// src/engine/system/thread.cpp — System::CriticalSection / System::Thread
// ============================================================================
// Phase D.4 THREADS E.2: portable engine code. Until D.4 this was
// src/platform/win/thread_win.cpp and called Win32 directly; every call is now the
// matching Platform:: primitive (src/platform/common/platform_{sync,thread}.h), whose
// Windows body is that same Win32 call with the same arguments
// (src/platform/win/{sync,thread}_win.cpp). The type layouts live in
// src/engine/system/Thread.h.
//
// SCOPE: System::CriticalSection (ctor / dtor / Lock / Unlock) and System::Thread
// (InitFields / ReleaseFields, the ctor / dtor bodies; Start, WaitForFinish,
// OnCompleted, ThreadFunc). Retail addresses at each body.
// ============================================================================

#include "system/Thread.h"

#include "platform_sync.h"
#include "platform_thread.h"
#include "diag_threads.h"   // DiagThreads::RegisterNamed (NOT RETAIL; block LOADSTAMP)
#include <typeinfo>

// ---- System::CriticalSection -----------------------------------------------

// 0x18000c5c0  ??0CriticalSection@System@@QEAA@XZ
// B6 row 50: the real constructor (the two old doors' bodies, unchanged; their
// one caller, PathfinderThread, now constructs its +0x30 member).
System::CriticalSection::CriticalSection() {
    // InitializeCriticalSection(this); SetCriticalSectionSpinCount(this, 5000)
    Platform::MutexInit(&_storage, 5000);
}

// 0x18000c5b0  ??1CriticalSection@System@@QEAA@XZ
System::CriticalSection::~CriticalSection() {
    Platform::MutexDestroy(&_storage);   // DeleteCriticalSection
}

// ---- System::Thread ---------------------------------------------------------

// ENDGAME B.3.9 VS (SYS W3, row 140): the minted `??_7Thread@System@@6B@` is
// RETIRED, and with it the three things that read it here -- the by-name
// `RtSystemThreadVftable()` lookup and the two ctor/dtor DOORS
// (`??0Thread@System@@QEAA@I@Z` 0x1800ca4e0, `??1Thread@System@@QEAA@XZ`
// 0x1800ca590) that stamped it before running the shared body. Every Thread in
// the build is now a C++ subclass built by `: System::Thread(stackSize)` and
// torn down by its compiler-chained `~Thread()` (Engine::Loader, Audio::Stream,
// IO::PersistenceWorker, the exe PathfinderThread), wearing compiler tables;
// no caller of either door was left (none by name, none by address, no exe
// import slot registered). Retail's `ThreadFunc` handoff (`LEA R8,[0x1800ca440]`)
// is `Thread::ThreadFunc` named directly in `InitFields`, with no first-use
// cache (B9-10).

// ============================================================================
// unsigned int System::Thread::ThreadFunc(void*)      retail 0x1800ca440
// ?ThreadFunc@Thread@System@@CAIPEAX@Z
//
// Wave 9, lane P (tier 0). The worker body every System::Thread runs — the last
// unported member of the class, so System::Thread is now COMPLETE (§4.1
// decision 5).
//
// ENDGAME B.3.4b: THE BODY MOVED onto `System::Thread::ThreadFunc` at the
// bottom of this file and what follows is the delegating door. This banner
// stays here, with the door that carries the registered address, because it
// documents the RETAIL bytes; the member body carries the note about why its
// two slot calls stay raw.
//
// Its only callees are Win32: WaitForMultipleObjects through the DLL's IAT slot
// 0x18014b358 and SetEvent through 0x18014b368, spelled Platform::EventWaitAny /
// Platform::EventSet here (Phase D.4), whose Windows bodies are those two calls.
//
// STRUCTURE, from the bytes:
//
//   handles[2] = { self->hStart, self->hStop };   // filled ONCE, before the
//                                                 // loop, and REUSED by the
//                                                 // second wait (RSP+0x20 is
//                                                 // never rewritten)
//   r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
//   while (r == 0) {                              // TEST EAX,EAX / JNZ
//       self->runFlags |= 1;                      // OR [RBX+8],1
//       self->vtable[0](self);                    // the work call
//       self->vtable[1](self);                    // OnCompleted — NOTE the
//                                                 // vtable is RELOADED at
//                                                 // 0x1800ca48c, so a work
//                                                 // call that re-stamps the
//                                                 // vtable is honoured
//       h = self->hFinished;                      // loaded BEFORE the AND
//       self->runFlags &= ~1;
//       SetEvent(h);
//       r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
//   }
//   return (r == 1) ? 0 : 0xFFFFFFFF;             // CMP EAX,1 / CMOVZ
//
// The return value distinguishes an orderly stop (index 1 == hStop, the
// destructor's signal) from anything else — abandoned, failed or timed out —
// which all report -1. Nothing in the DLL reads it; it lands in the thread's
// exit code.
//
// Retail RELOADS the vtable between slot 0 and slot 1 (0x1800ca48c); that
// reload is reproduced faithfully below, because a port that cached the vtable
// would call the wrong OnCompleted if it ever changed.
// ============================================================================

// ============================================================================
// System::Thread::OnCompleted()   retail 0x18000c590   (Wave 9 lane B3)
// ?OnCompleted@Thread@System@@UEAAXXZ
// ----------------------------------------------------------------------------

// ============================================================================
// ENDGAME B.3.4b — the member BODIES.
// ============================================================================
// They live in `namespace System` because C++ requires a member definition to
// appear in a namespace ENCLOSING the class, and `RtSystem` is a SIBLING of
// `System`, not a parent (B34_BRIEFS §A.2). Every body below is the one that
// used to sit in the door directly above it: same control flow, same callees,
// same order.
//
// ⚠ GREP HAZARD (B34_RULES §7.2): inside this block a bare `Thread` or
// `CriticalSection` binds the CLASS. The PODs must be spelled `RtSystem::`.
// ============================================================================

namespace System {

// 0x18000c700  ?Lock@CriticalSection@System@@QEAAXXZ
// (Not analysed: the capability is the object, the primitive takes its _storage bytes.)
void CriticalSection::Lock() NO_THREAD_SAFETY_ANALYSIS {
    Platform::MutexLock(&_storage);      // EnterCriticalSection
}

// 0x18000c6f0  ?Unlock@CriticalSection@System@@QEAAXXZ
void CriticalSection::Unlock() NO_THREAD_SAFETY_ANALYSIS {
    Platform::MutexUnlock(&_storage);    // LeaveCriticalSection
}

// 0x1800ca4e0  ??0Thread@System@@QEAA@I@Z -- the body MINUS the vptr store
// (the compiler's, in `Thread(unsigned)`; the minted one, in the door).
void Thread::InitFields(unsigned int stackSize) {
    _runFlags = 0;

    // Retail: XOR ECX,ECX (security=NULL); [RSP+0x20]=4 (CREATE_SUSPENDED) is
    // the LAST arg written before the call, so RDX (stackSize) passes through
    // unmodified. Retail called its own statically-linked CRT copy of
    // _beginthreadex (0x18007fbfc); ours is the identical routine.
    // CREATE_SUSPENDED matters: ThreadFunc's first act is to wait on {hStart,
    // hStop}, and those two handles (plus hFinished) are not created until
    // AFTER this call returns. Starting running immediately would have
    // ThreadFunc wait on uninitialized handles.
    // _beginthreadex(NULL, stackSize, ThreadFunc, this, CREATE_SUSPENDED, &id)
    _hThread = Platform::ThreadCreateSuspended(&Thread::ThreadFunc, this, stackSize);
    Platform::ThreadSetNormalPriority(_hThread);   // SetThreadPriority(NORMAL)

    _hStart    = Platform::EventCreate(false, false);   // auto-reset, unsignalled
    _hStop     = Platform::EventCreate(false, false);   // auto-reset, unsignalled
    _hFinished = Platform::EventCreate(true,  true);    // MANUAL-reset, signalled

    Platform::ThreadResume(_hThread);
}

// 0x1800ca590  ??1Thread@System@@QEAA@XZ -- the body MINUS the vptr store.
void Thread::ReleaseFields() {
    Platform::EventSet(_hStop);
    Platform::ThreadJoin(_hThread);        // WaitForSingleObject(hThread, INFINITE)
    Platform::ThreadClose(_hThread);       // CloseHandle x4
    Platform::EventClose(_hStart);
    Platform::EventClose(_hStop);
    Platform::EventClose(_hFinished);
}

// 0x1800ca610  ?Start@Thread@System@@QEAAXXZ
void Thread::Start() {
    Platform::EventReset(_hFinished);
    Platform::EventSet(_hStart);
}

// 0x1800ca5f0  ?WaitForFinish@Thread@System@@QEAAXXZ
void Thread::WaitForFinish() {
    Platform::EventWait(_hFinished, Platform::kInfinite);
}

// 0x18000c590  ?OnCompleted@Thread@System@@UEAAXXZ  — slot 1
void Thread::OnCompleted() {
    // Retail's body, in full: the shipped 0x18000c590 is a bare RET.
}

// 0x1800ca440  ?ThreadFunc@Thread@System@@CAIPEAX@Z
//
// `param` is the Thread-derived object `Start` handed to _beginthreadex — every
// one in the tree is a real C++ subclass wearing its compiler table
// (`Engine::Loader`, `IO::PersistenceWorker`, `Audio::Stream`, the exe's
// PathfinderThread), so slot 0 is `Run` and slot 1 `OnCompleted`, called by
// name. Retail's loop body is exactly two virtual calls: `MOV RAX,[RBX]; OR
// [RBX+8],1; CALL [RAX]` then `MOV RAX,[RBX]; CALL [RAX+8]` — the vptr is
// re-read for the second call, which a C++ virtual call after an opaque call
// does as well.
unsigned int Thread::ThreadFunc(void* param) {
    Thread* self = static_cast<Thread*>(param);  // CAST-OK: win32

    Platform::EventHandle handles[2] = { self->_hStart,
                                         self->_hStop };

    unsigned int r = Platform::EventWaitAny(2, handles, Platform::kInfinite);
    while (r == 0) {
        // NOT RETAIL (block LOADSTAMP): name this thread for [LoadTime] and the sampler.
        // Once per thread (a thread_local test after the first); the object is fully
        // constructed here, so its dynamic type names the role. No effect on the thread.
        DiagThreads::RegisterNamed(typeid(*self).name());
        // NOT RETAIL (review F-150): retail's `OR [RBX+8],1` / `AND [RBX+8],~1` are
        // plain RMWs on a word other threads poll; atomic here (IsWorking, Thread.h).
        __atomic_fetch_or(&self->_runFlags, 1u, __ATOMIC_RELAXED);
        // NOT RETAIL (block LOADSTAMP, review S-034): the sampler's busy bit, kept in
        // the DiagThreads roster rather than read from this object, which the owner
        // may free while the sampler still looks at the thread.
        DiagThreads::SetBusy(true);
        self->Run();           // slot 0 — the work
        self->OnCompleted();   // slot 1

        Platform::EventHandle finished = self->_hFinished;
        // NOT RETAIL (review F-103): retail clears bit 0 BEFORE SetEvent(hFinished)
        // (1800ca499 AND, 1800ca49d SetEvent). A caller that saw the bit clear in
        // between and called Start() (ResetEvent(hFinished), SetEvent(hStart)) had
        // its reset undone by this old job's SetEvent, so its WaitForFinish returned
        // while the NEW job still ran (PathfinderThread::DrainQueue, then Map::~Map
        // freeing the pathfinder under a live A* job). Signal first, then clear the
        // bit with a release, so any Start() that follows the clear also follows
        // the SetEvent.
        Platform::EventSet(finished);
        __atomic_fetch_and(&self->_runFlags, ~1u, __ATOMIC_RELEASE);
        DiagThreads::SetBusy(false);   // NOT RETAIL (block LOADSTAMP)

        r = Platform::EventWaitAny(2, handles, Platform::kInfinite);
    }
    return (r == 1) ? 0u : 0xFFFFFFFFu;
}

}  // namespace System

// ---- Registration ------------------------------------------------------------
