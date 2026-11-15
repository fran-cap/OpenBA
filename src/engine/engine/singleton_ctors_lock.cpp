// ============================================================================
// src/engine/engine/singleton_ctors_lock.cpp — Wave 9 lane X, platform half
//   (was src/platform/win/singleton_ctors_win.cpp; OS-free since Phase D.4 THREADS E.3,
//   moved here unchanged by SDL-5: the lock init is Platform::MutexInit)
// ============================================================================
// PLAN.md §4.1 decision 1 kept every non-`_win` file under src/engine/ free
// of <windows.h>, which is why this half was a `_win` file until it went portable. Two of this lane's fourteen constructors
// (System::MemoryAllocator — already owned by allocator.cpp — and
// Video::Device) initialise a CRITICAL_SECTION inline, so the lane needs the
// same two Win32 calls the retail bodies make and nothing else:
//
//   ??0Device@Video@@QEAA@W4DeviceType@01@@Z  0x1800f7160
//       ADD  RCX,0x60
//       CALL qword ptr [0x18014b380]        ; InitializeCriticalSection
//       MOV  EDX,0x1388                     ; 5000
//       LEA  RCX,[RSI+0x60]
//       CALL qword ptr [0x18014b208]        ; SetCriticalSectionSpinCount
//
// The spin count is the literal 0x1388 out of the disassembly, not a guess, and
// it is the same value MemoryAllocator's ctor passes (allocator_win.cpp).
// ============================================================================

#include "system/MemoryAllocator.h"

#include "platform_sync.h"   // Platform::MutexInit (RtLaneXPlatformLockInit)

// ---------------------------------------------------------------------------
// The allocator platform seam.
//
// `rtalloc::Construct` (src/engine/system/allocator.cpp) opens with
// `g_plat.lockInit(&self->lock)`. `g_plat` is a table of function pointers that
// `RtSystemAllocatorInstallPlatform()` fills; `RtSystemAllocatorArm()` installs
// it at boot (src/app/runtime_init.cpp). The function below installs it when
// `lockInit` is null and reports whether `lockInit` is set; nothing calls it.
//
// `g_plat` is extern-declared here the same way src/platform/win/
// allocator_win.cpp declares it.
// ---------------------------------------------------------------------------
extern rt::system::RtSysMemPlatform g_plat;

bool RtLaneXEnsureAllocatorPlatform() {
    if (!g_plat.lockInit) RtSystemAllocatorInstallPlatform();
    // Report the seam the caller is about to USE, not a separate `installed`
    // flag: the whole failure being fixed here was a null lockInit.
    return g_plat.lockInit != nullptr;
}

// Defined here, declared in singleton_ctors.cpp. Not a general seam: this lane
// is the only caller and the two calls are one retail body's, verbatim.
void RtLaneXPlatformLockInit(Platform::LockStorage* cs) {
    // InitializeCriticalSection(cs); SetCriticalSectionSpinCount(cs, 0x1388)
    Platform::MutexInit(cs, Platform::kLockSpinCount);
}

