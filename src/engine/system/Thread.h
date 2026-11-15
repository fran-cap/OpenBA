#pragma once
// ============================================================================
// src/engine/system/Thread.h — System::CriticalSection / System::Thread
// ============================================================================
// Binary-compatible layouts recovered from Runtime-x64.dll (retail addresses
// in the comments). Platform-neutral by design (PLAN.md §4.1 decision 1): this
// header must never include <windows.h>. Since Phase D.4 (THREADS E.2) the OS
// objects are spelled with the Platform:: types: the lock is a
// `Platform::LockStorage` (the 0x28 CRITICAL_SECTION bytes, built in place) and
// the four handles are `Platform::ThreadHandle` / `Platform::EventHandle` (8-byte
// pointers to incomplete types; on Windows the value is the Win32 HANDLE). The
// bodies are src/engine/system/thread.cpp, over src/platform/common/platform_*.h.
//

#include <cstdint>
#include <cstddef>
#include <type_traits>   // B.3.4b: the "CriticalSection has no vptr" pin

#include "platform_sync.h"     // Platform::LockStorage, Platform::EventHandle
#include "platform_thread.h"   // Platform::ThreadHandle
#include "system/thread_annotations.h"   // CAPABILITY on CriticalSection


// ----------------------------------------------------------------------------
// System::CriticalSection — the retail layout
// ----------------------------------------------------------------------------
// Retail (0x18000c5c0, ??0CriticalSection@System@@QEAA@XZ):
//   InitializeCriticalSection(this);
//   SetCriticalSectionSpinCount(this, 5000);
// i.e. the object IS a Win32 CRITICAL_SECTION, spin count 5000. Represented
// here as opaque storage sized/aligned exactly like CRITICAL_SECTION on x64
// (40 bytes: DebugInfo ptr, LockCount, RecursionCount, OwningThread,
// LockSemaphore, SpinCount — all 8-byte-aligned). A _posix twin would replace
// this storage with e.g. a pthread_mutex_t of matching size/alignment; the
// class has no vtable, so there is no ABI concern beyond size and alignment.

// ----------------------------------------------------------------------------
// System::Thread — the retail layout
// ----------------------------------------------------------------------------
// Retail is ALWAYS subclassed: the base vtable's slot 0 is _purecall
// (0x18007ec28) and only slot 1 (OnCompleted, 0x18000c590) has a real base
// body (a bare RET). A derived class supplies its own vtable and stamps it
// over this one right after the base ctor runs (see src/game/Game/
// pathfinder_seed.cpp and keystone_a_gamedtor.cpp for two such subclasses).
//
// Layout, from 0x1800ca4e0 (ctor), 0x1800ca590 (dtor), 0x1800ca610 (Start),
// 0x1800ca5f0 (WaitForFinish), 0x1800ca440 (ThreadFunc):
//   +0x00 vtable        System::Thread's own vtable (??_7Thread@System@@6B@,
//                        a real PE export at 0x180155f20) until a subclass
//                        ctor overwrites it.
//   +0x08 runFlags      uint32; bit0 set for the duration of the work call
//                        inside ThreadFunc (cleared again right after).
//   +0x0c (padding)     implicit — the next field is 8-byte aligned.
//   +0x10 hThread       HANDLE from _beginthreadex.
//   +0x18 hStart        HANDLE, auto-reset, CreateEventW(NULL,FALSE,FALSE,NULL)
//                        — Start() SetEvents this to wake the worker.
//   +0x20 hStop         HANDLE, auto-reset, CreateEventW(NULL,FALSE,FALSE,NULL)
//                        — the dtor SetEvents this to wake ThreadFunc for
//                        shutdown (ThreadFunc waits on {hStart, hStop} and the
//                        hStop slot, index 1, is the exit branch).
//   +0x28 hFinished     HANDLE, MANUAL-reset, initially SIGNALED
//                        (CreateEventW(NULL,TRUE,TRUE,NULL)) — WaitForFinish()
//                        blocks on this; Start() ResetEvents it before waking
//                        hStart, so a fresh WaitForFinish() blocks until the
//                        worker's next pass sets it again.
// Total base size 0x30. A derived class's own fields start at +0x30 (e.g. the
// worker-queue Thread in keystone_a_gamedtor.cpp embeds a CriticalSection
// there; the pathfinder's Thread subclass is 0x1f8 bytes total).
//
// The class spells no `vtable` field: the compiler's own vptr occupies +0x00.


// ============================================================================
// ENDGAME B.3.4b — the REAL `System::CriticalSection` and `System::Thread`.
// ============================================================================
//
// Design: B34B_RULES.md rules B4b-1 (the name collision), B4b-2 (no real
// ctor/dtor for either) and B4b-3 (Thread stays ABSTRACT and the POD mirror
// holds the POD); B34_RULES.md §4.3 (the ctor/dtor door question) and §1.5
// rule 3 (the POD stays); ENDGAME §B.1 decision 2 (no virtual destructor).
//
// *** NEITHER CLASS GETS A REAL CONSTRUCTOR OR DESTRUCTOR IN B.3.4b. ***
// ----------------------------------------------------------------------------
// `System::Thread`: B34_RULES §4.3's test — "would the member form change which
// vtable a live object wears?" — answers YES at all four constructing sites,
// and two of them are outside this step's file set entirely:
//
//   src/game/Game/pathfinder_seed.cpp:142-148    stamps exe 0x14015ef18
//   src/engine/engine/singleton_ctors.cpp:448-450 builds IO::Persistence's
//                                                  embedded Thread at +0x10 and
//                                                  stamps 0x180155860 over it
//   src/engine/engine/loops.cpp                    defines Engine::Loader's
//                                                  static, LoaderLoop::_loader
//   src/engine/audio/audio_stream.cpp:303-341 builds Audio::Stream
//
// Nothing in the tree reads the base table 0x180155f20 by name or address: a
// `System::Thread` base wears the compiler's table (see the B.3.9 notes below).
//
// `System::CriticalSection` has no vtable at all, so §4.3 would PERMIT a real
// ctor/dtor — and it still does not get one, for a different reason: two of its
// three known embedders (`IO::Persistence`, `Audio::Stream`) call raw
// `InitializeCriticalSection` / `SetCriticalSectionSpinCount(5000)` and never
// reach the ctor door, so a real constructor would leave those two objects
// constructed a second, different way. Retail's own scoped-lock shape is a
// SEPARATE class, `System::StackCriticalSection` (??0 0x180034fa0 /
// ??1 0x180034f90) — unported, unreferenced, and recorded in B34B_RULES §1 so
// that nobody "modernises" this class into a lock_guard and calls it faithful.
//
// *** `System::Thread` IS ABSTRACT, AND THAT IS RULE B4b-3. ***
// ----------------------------------------------------------------------------
// `src/engine/engine/UpdateLoop.h` derives `Engine::Loader` from this class.
// Retail's slot 0 here is `_purecall`, there is no base body to port, and
// inventing one is forbidden — so this class stays abstract.
// Nothing constructs a `System::Thread` as a C++ object; `sizeof` works on an
// abstract class, so every layout pin below holds.
// ============================================================================
//
// *** ENDGAME B.3.9 SYS W1 — `System::Thread` HAS ITS REAL CTOR AND DTOR. ***
// ----------------------------------------------------------------------------
// The B.3.4b ban above is lifted by B39_RULES D1 / RULE B8-1: the class gets a
// PROTECTED `Thread(unsigned int stackSize)` (retail 0x1800ca4e0 minus its
// vptr store) and a protected NON-virtual `~Thread()` (retail 0x1800ca590
// minus its vptr store), both sharing ONE body with the two registered doors
// through `InitFields` / `ReleaseFields`. The doors stay (their callers --
// `Engine::Loader`'s mint construction, `Audio::Stream`, the exe pathfinder,
// `IO::Persistence`'s worker -- convert in W2) and stamp the MINTED table
// before calling the shared body, exactly as before. The class stays ABSTRACT
// (retail's slot 0 is `_purecall`) and has NO `DeletingDtor`: its table has
// two slots, `Run` and `OnCompleted`, and no deleting-dtor slot at all, like
// both subclasses' (UpdateLoop.h / Stream.h "NO deleting dtor").
//
// *** ENDGAME B.3.9 VS (SYS W3) — THE DOORS ARE GONE, THE MINTED ROW RETIRED. ***
// ----------------------------------------------------------------------------
// Every subclass is a real class on this ctor now (Engine::Loader,
// Audio::Stream, IO::PersistenceWorker, the exe PathfinderThread), so the
// `??0Thread` / `??1Thread` doors lost their last callers and are deleted with
// the minted `??_7Thread@System@@6B@` (row 140). A `System::Thread` base wears
// the compiler's table while its ctor / dtor body runs, exactly where retail's
// wears 0x180155f20.
// ============================================================================

namespace System {

// ----------------------------------------------------------------------------
// System::CriticalSection — 0x28 bytes, NO vtable.
// ----------------------------------------------------------------------------
// A capability to clang's thread-safety analysis (system/thread_annotations.h): no layout change.
class CAPABILITY("mutex") CriticalSection {
public:
    // B6 row 50: the REAL ctor and dtor. Its one holder, the exe's
    // PathfinderThread (+0x30), constructs and destroys it as a member, as
    // retail's 0x1400a67f0 / 0x1400a6720 do; IO::Persistence and Audio::Stream
    // hold their CRITICAL_SECTION as opaque storage and never reach this class.
    // ??0CriticalSection@System@@QEAA@XZ  retail 0x18000c5c0:
    //   InitializeCriticalSection(this); SetCriticalSectionSpinCount(this, 5000)
    CriticalSection();
    // ??1CriticalSection@System@@QEAA@XZ  retail 0x18000c5b0: DeleteCriticalSection(this)
    ~CriticalSection();
    CriticalSection(const CriticalSection&)            = delete;   // retail never copies one
    CriticalSection& operator=(const CriticalSection&) = delete;

    // ?Lock@CriticalSection@System@@QEAAXXZ     retail 0x18000c700
    // ?Unlock@CriticalSection@System@@QEAAXXZ   retail 0x18000c6f0
    // One Platform:: call each (EnterCriticalSection / LeaveCriticalSection on
    // Windows); bodies in src/engine/system/thread.cpp.
    void Lock() ACQUIRE();
    void Unlock() RELEASE();

    // ---- the 0x28 bytes ----------------------------------------------------
    // PUBLIC for the same reason the (deleted) `IO::Registry`'s fields were: `offsetof` on
    // it has to be well formed. The Platform::Mutex* primitives build the native
    // lock in these bytes (a CRITICAL_SECTION on Windows).
    Platform::LockStorage _storage;
};

static_assert(sizeof(CriticalSection) == 0x28,
              "System::CriticalSection is sizeof(CRITICAL_SECTION) on x64");
static_assert(offsetof(CriticalSection, _storage) == 0x0, "CriticalSection::_storage");
// Stated as assertions so a future `virtual` fails the build rather than the
// boot: a vptr would make this 0x30 bytes and push the storage eight bytes
// down.
static_assert(!std::is_polymorphic<CriticalSection>::value,
              "System::CriticalSection must never acquire a vtable: retail has none");
static_assert(std::is_standard_layout<CriticalSection>::value,
              "the lock bytes sit at +0x00 of a standard-layout class");
// B6 row 50: the teardown IS the member destructor now (the ??1 door is gone,
// so there is exactly one DeleteCriticalSection per object).
static_assert(!std::is_trivially_destructible<CriticalSection>::value,
              "B6 row 50: ~CriticalSection is retail's 0x18000c5b0");

// ----------------------------------------------------------------------------
// System::Thread — vtable ??_7Thread@System@@6B@ 0x180155f20, 2 slots, 0x30.
// ----------------------------------------------------------------------------
// B.3.9: a subclass builds its base with `: System::Thread(stackSize)` and wears
// the compiler's tables (no door and no minted row remain). The class stays
// ABSTRACT, so `System::Thread t;` is still a hard compile error.
//
// The two slots of retail's 0x180155f20:
//   [0] +0x00  0x18007ec28 `_purecall`  -> pure here, see below
//   [1] +0x08  0x18000c590  ?OnCompleted@Thread@System@@UEAAXXZ
class Thread {
public:
    // slot 0  +0x00 — PURE, because retail's own base table really is
    // `_purecall` in this slot: there is no base body to port and none is
    // invented. THE NAME IS EVIDENCE, NOT A GUESS: the one derived table this
    // tree holds is `??_7Loader@Engine@@6B@`, whose slot-0 override is exported
    // as `?Run@Loader@Engine@@UEAAXXZ` @ 0x1800dbfc0
    // in retail's export table, and src/engine/engine/
    // UpdateLoop.h:40 records that table as [0] Run 0x1800dbfc0, [1] 0x18000c590.
    virtual void Run() = 0;

    // slot 1  +0x08  0x18000c590  ?OnCompleted@Thread@System@@UEAAXXZ
    // The shipped body is one instruction: `RET`. The base class's do-nothing
    // hook, called from ThreadFunc's slot 1 once the work function returns.
    virtual void OnCompleted();

    // ?Start@Thread@System@@QEAAXXZ          retail 0x1800ca610
    // ?WaitForFinish@Thread@System@@QEAAXXZ  retail 0x1800ca5f0
    // NON-virtual (there are only two slots in the table above and neither is
    // one of these).
    void Start();
    void WaitForFinish();

    // NOT RETAIL (review F-150, F-157, F-147): bit 0 of `_runFlags` ("mid-job"),
    // read as an ACQUIRE load. Retail reads the word with a plain
    // `TEST byte ptr [RCX+0x8],0x1` at every poll site, while the worker sets and
    // clears it outside any lock (ThreadFunc): a data race under the C++ model, and
    // on ARM64 seeing the bit clear did not order the job's stores (the save list,
    // `_status`, the load buffers) before the reads that follow. ThreadFunc clears
    // the bit with a RELEASE RMW, so a caller that sees it clear sees the job's
    // results. On x86 the acquire load is the same plain MOV retail has.
    bool IsWorking() const { return (__atomic_load_n(&_runFlags, __ATOMIC_ACQUIRE) & 1u) != 0; }

    // ?ThreadFunc@Thread@System@@CAIPEAX@Z   retail 0x1800ca440
    // The worker entry point handed to _beginthreadex. Retail's is `CA`, i.e.
    // PRIVATE static; it is public here, and access control is not ABI. It
    // carries no `__stdcall`: that is what
    // _beginthreadex's signature spells, a no-op on x64 (one calling convention)
    // and not a keyword off Windows (Phase D.4 POSIX; Platform::ThreadEntry).
    //
    // ITS BODY CALLS THE TWO SLOTS BY NAME: `Run()` (slot 0) then `OnCompleted()`
    // (slot 1), on objects that are real subclasses wearing their compiler tables:
    // `Engine::Loader`, `IO::PersistenceWorker`, `Audio::Stream`, the pathfinder
    // (thread.cpp).
    static unsigned int ThreadFunc(void* param);

    // ---- the 0x30 bytes, at retail's offsets -------------------------------
    // +0x00 is the COMPILER'S vptr. PUBLIC: `offsetof` on them has to be well
    // formed, and code outside the class reads the event handles
    // (src/app/engine_loader_gate.cpp, src/engine/save/saveload_update.cpp).
    uint32_t _runFlags;    // +0x08
    uint32_t _pad0c;       // +0x0c
    Platform::ThreadHandle _hThread;     // +0x10  HANDLE (_beginthreadex)
    Platform::EventHandle  _hStart;      // +0x18  HANDLE, auto-reset event
    Platform::EventHandle  _hStop;       // +0x20  HANDLE, auto-reset event
    Platform::EventHandle  _hFinished;   // +0x28  HANDLE, MANUAL-reset event

protected:
    // ??0Thread@System@@QEAA@I@Z  retail 0x1800ca4e0 — the ONE retail ctor.
    // The compiler stamps the vptr (retail's `LEA RAX,[0x180155f20]; MOV
    // [RCX],RAX`); the body is InitFields. Protected: the class is abstract.
    explicit Thread(unsigned int stackSize) { InitFields(stackSize); }

    // ??1Thread@System@@QEAA@XZ  retail 0x1800ca590. ENDGAME decision 2's base
    // shape: NON-virtual (Itanium spends two slots on a virtual destructor,
    // which would push `OnCompleted` off slot 1 and silently mis-aim every
    // reader of the minted table) and protected, because nothing deletes a
    // Thread through this type. The compiler re-stamps this class's vptr on
    // entry, as retail does; the body is ReleaseFields.
    ~Thread() { ReleaseFields(); }

private:
    // Retail's ctor / dtor bodies MINUS their vptr stores. Defined in
    // src/engine/system/thread.cpp; kept out of line (not folded into the
    // ctor / dtor) because the unit tests link RECORDING stand-ins for them
    // (tests/unit/system_abi_stubs.cpp) so no OS thread starts in a test.
    void InitFields(unsigned int stackSize);
    void ReleaseFields();
};

static_assert(sizeof(Thread) == 0x30, "System::Thread is 0x30 bytes");
static_assert(std::is_polymorphic<Thread>::value,
              "System::Thread MUST be polymorphic: +0x00 is the vptr the four "
              "constructing sites stamp");
static_assert(std::is_abstract<Thread>::value,
              "B4b-3: abstract is what makes `System::Thread t;` a compile error, "
              "and retail's slot 0 really is _purecall");
static_assert(!std::is_destructible<Thread>::value,
              "B.3.9 D1.3: the destructor is protected");
static_assert(!std::has_virtual_destructor<Thread>::value,
              "B.3.9 D1.3: non-virtual until the normal-virtual cutover");
static_assert(offsetof(Thread, _runFlags)  == 0x08, "Thread::runFlags @ +0x08");
static_assert(offsetof(Thread, _pad0c)     == 0x0c, "the gap the ctor never writes");
static_assert(offsetof(Thread, _hThread)   == 0x10, "Thread::hThread @ +0x10");
static_assert(offsetof(Thread, _hStart)    == 0x18, "Thread::hStart @ +0x18");
static_assert(offsetof(Thread, _hStop)     == 0x20, "Thread::hStop @ +0x20");
static_assert(offsetof(Thread, _hFinished) == 0x28, "Thread::hFinished @ +0x28");

}  // namespace System
