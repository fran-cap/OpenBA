// ============================================================================
// tests/unit/system_abi_stubs.cpp — the out-of-line members the REAL
// `System::Thread` / `System::CriticalSection` own, defined for the host link.
// ENDGAME B.3.4b SYSTEM (2).
// ============================================================================
//
// WHY THIS FILE EXISTS, AND WHY IT IS NOT A MIRROR
// ------------------------------------------------
// `tests/unit/system_abi_test.cpp` pins the two-slot vtable order by
// constructing a concrete probe subclass of `System::Thread`. Constructing one
// runs the BASE's constructor, which stores the base's vptr — so the link needs
// `vtable for System::Thread`.
//
// GCC emits a class's vtable in the translation unit that defines its KEY
// FUNCTION (the first non-inline, non-pure virtual), and for `System::Thread`
// that is `OnCompleted` — whose real body lives in
// `src/engine/system/thread.cpp` (Phase D.4: portable, over the Platform::
// primitives). It is not linked here because its `InitFields` starts a real OS
// thread, and these tests pin WHEN that body runs, not what it does
// (tests/unit/README.md, B31_RULES rule 6).
//
// THE CLASS IS STILL THE REAL ONE. What is defined here is only the BODY of
// each member; the declarations, their ORDER and their signatures come from
// `src/engine/system/Thread.h`, which is exactly what the test is pinning.
// This file re-declares nothing, so it is the opposite of a mirror — a mirror
// would restate the slots and then test its own copy of the order.
//
// THREE KINDS OF BODY BELOW, and the difference is deliberate:
//
//   * `Thread::OnCompleted` is OBSERVABLE (it bumps a counter). It has to be,
//     because it is the key function AND it is slot 1, and slot 1 is half of
//     what the test measures. Retail's real body is a bare RET (0x18000c590),
//     so the counter is a test double for an empty function and cannot mask a
//     behavioural difference — there is no behaviour to mask.
//   * `InitFields` / `ReleaseFields` RECORD each call (see below).
//   * everything else ABORTS. `Start`, `WaitForFinish` and `ThreadFunc` need
//     live Platform:: thread and event handles and `CriticalSection::Lock`/
//     `Unlock` need an initialised Platform::LockStorage; none can run on the
//     host. Aborting means a test that accidentally reaches one fails loudly
//     instead of quietly passing through a stub.
// ============================================================================

#include <cstdlib>

#include "system/Thread.h"

#include "system_abi_stubs.h"

int g_systemStub_OnCompletedCalls = 0;
SystemStubThreadCall g_systemStub_InitFields    = {};
SystemStubThreadCall g_systemStub_ReleaseFields = {};

namespace System {

// 0x18000c590  ?OnCompleted@Thread@System@@UEAAXXZ — slot 1, and the KEY
// FUNCTION that pulls `vtable for System::Thread` into this TU.
void Thread::OnCompleted() {
    ++g_systemStub_OnCompletedCalls;
}

// ENDGAME B.3.9 SYS W1 — the shared ctor / dtor bodies (retail 0x1800ca4e0 /
// 0x1800ca590 minus their vptr stores). The real ones start an OS thread and
// create / close four HANDLEs, which no host test does; these RECORD instead,
// so the tests can pin that the real `Thread(unsigned)` / `~Thread()` run them
// exactly once, with the argument, while the object wears the BASE's table.
void Thread::InitFields(unsigned int stackSize) {
    ++g_systemStub_InitFields.calls;
    g_systemStub_InitFields.self       = this;
    g_systemStub_InitFields.vptrAtCall = *reinterpret_cast<void* const*>(this);
    g_systemStub_InitFields.stackSize  = stackSize;
}

void Thread::ReleaseFields() {
    ++g_systemStub_ReleaseFields.calls;
    g_systemStub_ReleaseFields.self       = this;
    g_systemStub_ReleaseFields.vptrAtCall = *reinterpret_cast<void* const*>(this);
}

// The three that cannot run on the host. Retail addresses kept so the reader
// can find the real bodies: 0x1800ca610 / 0x1800ca5f0 / 0x1800ca440.
void Thread::Start()         { std::abort(); }
void Thread::WaitForFinish() { std::abort(); }

unsigned int Thread::ThreadFunc(void* /*param*/) {
    std::abort();
}

// 0x18000c700 / 0x18000c6f0 — an uninitialised CRITICAL_SECTION on the host.
void CriticalSection::Lock()   { std::abort(); }
void CriticalSection::Unlock() { std::abort(); }

}  // namespace System
