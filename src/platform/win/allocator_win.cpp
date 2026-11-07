// ============================================================================
// src/platform/win/allocator_win.cpp — the Win32 half of System::MemoryAllocator
// ============================================================================
//
// PLAN.md §4.1 requires Win32 to stay behind a platform seam, and the original
// authors already drew that seam themselves: the class has six exported
// `Platform*` members and every VirtualAlloc/GetSystemInfo/CRITICAL_SECTION call
// in the allocator is either one of them or an inlined copy of one. So this file
// is not a wrapper we invented — it is the retail `_win` translation unit,
// reconstructed. A `_posix`/`_sdl` twin implements the same entry point:
//
//     RtSystemAllocatorInstallPlatform()
//
// src/engine/system/allocator.cpp — the portable half — never sees an OS header.
//
// Retail addresses:
//   0x1800c7b30  ?PlatformConstruct@MemoryAllocator@System@@AEAAXXZ
//   0x1800c7b50  ?PlatformDestruct@MemoryAllocator@System@@AEAAXXZ
//   0x1800c7b60  ?PlatformAllocate@MemoryAllocator@System@@AEAAPEAX_KPEAX_N@Z
//   0x1800c7bb0  ?PlatformFree@MemoryAllocator@System@@AEAAXPEAX_K@Z
//   0x1800c7c40  ?PlatformStartup@MemoryAllocator@System@@AEAA_NXZ
//   0x1800c7c70  ?PlatformShutdown@MemoryAllocator@System@@AEAAXXZ
//
// PlatformDestruct and PlatformShutdown are `ret` in retail. They are not stubs
// we left unfinished; the DLL genuinely does nothing there.
// ============================================================================

#include <windows.h>
#include <cstdint>

#include "system/MemoryAllocator.h"
#include "platform_sync.h"      // Platform::MutexInit: the +0x640 lock

using rt::system::MemoryAllocator;
using rt::system::RtSysMemPlatform;

// The seam table, defined in the portable half. PlatformAllocate/PlatformFree
// go through it rather than calling VirtualAlloc twice from two files.
extern RtSysMemPlatform g_plat;

// The one Win32 fact the OBJECT LAYOUT depends on (the critical section at +0x640
// and _pageSize at +0x668, so exactly 0x28 bytes) is asserted where the lock is
// built: src/platform/win/sync_win.cpp, sizeof(CRITICAL_SECTION) == LockStorage.

namespace {

void*    Reserve(void* a, uint64_t n)       { return VirtualAlloc(a, (SIZE_T)n, MEM_RESERVE, PAGE_READWRITE); }
void*    ReserveCommit(void* a, uint64_t n) { return VirtualAlloc(a, (SIZE_T)n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); }
void*    Commit(void* a, uint64_t n)        { return VirtualAlloc(a, (SIZE_T)n, MEM_COMMIT, PAGE_READWRITE); }
void     Decommit(void* a, uint64_t n)      { VirtualFree(a, (SIZE_T)n, MEM_DECOMMIT); }
void     Release(void* a)                   { VirtualFree(a, 0, MEM_RELEASE); }

uint64_t PageSize() { SYSTEM_INFO si; GetSystemInfo(&si); return si.dwPageSize; }

// The ctor's own sequence: InitializeCriticalSection then a 5000 spin count.
// Built in place in the +0x640 bytes: the allocator's lock never allocates.
// Enter / Leave / Delete are the primitives themselves (EnterCriticalSection /
// LeaveCriticalSection / DeleteCriticalSection), one call deep as before.
void LockInit(Platform::LockStorage* cs) { Platform::MutexInit(cs, Platform::kLockSpinCount); }

const RtSysMemPlatform kWin32 = {
    &Reserve, &ReserveCommit, &Commit, &Decommit, &Release, &PageSize,
    &LockInit, &Platform::MutexLock, &Platform::MutexUnlock, &Platform::MutexDestroy
};

}  // namespace

// ---------------------------------------------------------------------------
// The six exported Platform* members, byte-faithful (B3R W0-A0: members of
// System::MemoryAllocator; they were free functions with `self` explicit).
// ---------------------------------------------------------------------------

// 0x1800c7b30 — the ctor inlines exactly this one store.
void MemoryAllocator::PlatformConstruct() { pageSize = 0; }

// 0x1800c7b50 / 0x1800c7c70 — `ret`.
void MemoryAllocator::PlatformDestruct() {}
void MemoryAllocator::PlatformShutdown() {}

// 0x1800c7c40 — Startup inlines exactly this.
bool MemoryAllocator::PlatformStartup() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    pageSize = si.dwPageSize;
    return true;
}

// 0x1800c7b60 — VirtualAlloc(at, roundUpToPage(bytes),
//                            MEM_RESERVE | (commitToo ? MEM_COMMIT : 0),
//                            PAGE_READWRITE)
// The flag word in the listing is `-(uint)commitToo & 0x1000 | 0x2000`, i.e.
// MEM_RESERVE always and MEM_COMMIT only when the bool is set.
void* MemoryAllocator::PlatformAllocate(uint64_t bytes, void* at, bool commitToo) {
    uint64_t page = pageSize;
    uint64_t n = ((bytes - 1 + page) / page) * page;
    return VirtualAlloc(at, (SIZE_T)n,
                        (commitToo ? MEM_COMMIT : 0u) | MEM_RESERVE,
                        PAGE_READWRITE);
}

// 0x1800c7bb0 — VirtualFree(p, 0, MEM_RELEASE). The size argument is ignored,
// which is correct for MEM_RELEASE and is what retail passes.
void MemoryAllocator::PlatformFree(void* p, uint64_t /*bytes*/) {
    VirtualFree(p, 0, MEM_RELEASE);
}

// ---------------------------------------------------------------------------
// Entry points the portable half calls.
// ---------------------------------------------------------------------------
extern "C" void RtSystemAllocatorInstallPlatform() {
    RtSysMemPlatformInstall(&kWin32);
}
