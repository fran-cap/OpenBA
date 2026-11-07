// ============================================================================
// src/platform/darwin/allocator_darwin.cpp -- the Darwin half of System::MemoryAllocator
// ============================================================================
// The twin of src/platform/win/allocator_win.cpp (Phase D.4 THREADS E.9, plans/
// THREADS.md A.6): the same entry point, RtSystemAllocatorInstallPlatform(), and the
// six exported Platform* members, over mmap instead of VirtualAlloc.
//
//   VirtualAlloc(MEM_RESERVE)            mmap(PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE)
//   VirtualAlloc(MEM_RESERVE|MEM_COMMIT) mmap(PROT_READ|PROT_WRITE, ...)
//   VirtualAlloc(MEM_COMMIT)             mprotect(PROT_READ|PROT_WRITE): the pages keep their
//                                        contents, and a never-touched page reads zero
//   VirtualFree(MEM_DECOMMIT)            madvise(MADV_DONTNEED) + mprotect(PROT_NONE): the
//                                        next commit reads zeros again, as on Windows
//   VirtualFree(MEM_RELEASE)             munmap of the WHOLE reservation
//   GetSystemInfo().dwPageSize           sysconf(_SC_PAGESIZE)
//
// RELEASE CARRIES NO SIZE (the seam's `release(addr)` is VirtualFree(addr, 0,
// MEM_RELEASE)) and munmap needs one, so every reservation is recorded in a fixed
// table here, addr -> size, guarded by a static pthread mutex. Fixed, not a
// container: this file must not allocate (the allocator it serves is the heap).
// Releasing an address the table does not hold (null included: Shutdown releases
// every range's base) does nothing, as VirtualFree fails on it.
//
// A non-null `at` (PlatformAllocate's hint) is VirtualAlloc's exact-address
// reserve: MAP_FIXED_NOREPLACE, failing rather than moving.
//
// THE LOCK at +0x640 is Platform::MutexInit's in-place recursive pthread_mutex_t
// (sync_posix.cpp asserts it fits the 0x28 bytes); the spin count is ignored.
// ============================================================================

#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "system/MemoryAllocator.h"
#include "platform_sync.h"

using rt::system::MemoryAllocator;
using rt::system::RtSysMemPlatform;

extern RtSysMemPlatform g_plat;

namespace {

// ---- the reservation table ----------------------------------------------------
struct Reservation {
    uintptr_t base;
    uint64_t bytes;
};
constexpr int kMaxReservations = 8192;
Reservation g_reservations[kMaxReservations];
int g_reservationCount = 0;
pthread_mutex_t g_reservationLock = PTHREAD_MUTEX_INITIALIZER;

uintptr_t Addr(const void* p) { return reinterpret_cast<uintptr_t>(p); }  // CAST-OK: ptr_int
void* Ptr(uintptr_t a) { return reinterpret_cast<void*>(a); }             // CAST-OK: ptr_int

void Record(void* p, uint64_t bytes) {
    pthread_mutex_lock(&g_reservationLock);
    if (g_reservationCount == kMaxReservations) {
        pthread_mutex_unlock(&g_reservationLock);
        std::fprintf(stderr, "[Memory] allocator_posix: more than %d reservations\n", kMaxReservations);
        std::abort();
    }
    g_reservations[g_reservationCount++] = Reservation{ Addr(p), bytes };
    pthread_mutex_unlock(&g_reservationLock);
}

// Forget `p`'s reservation; its size, or 0 when the table does not hold it.
uint64_t Forget(void* p) {
    uint64_t bytes = 0;
    pthread_mutex_lock(&g_reservationLock);
    for (int i = 0; i < g_reservationCount; ++i) {
        if (g_reservations[i].base == Addr(p)) {
            bytes = g_reservations[i].bytes;
            g_reservations[i] = g_reservations[--g_reservationCount];
            break;
        }
    }
    pthread_mutex_unlock(&g_reservationLock);
    return bytes;
}

uint64_t PageSize() { return static_cast<uint64_t>(sysconf(_SC_PAGESIZE)); }

// [a, a + n) widened to whole pages, as VirtualAlloc / VirtualFree widen it.
void PageSpan(void* a, uint64_t n, uintptr_t* lo, uint64_t* len) {
    const uint64_t page = PageSize();
    const uintptr_t start = Addr(a) & ~static_cast<uintptr_t>(page - 1);
    const uintptr_t end = (Addr(a) + n + page - 1) & ~static_cast<uintptr_t>(page - 1);
    *lo = start;
    *len = end - start;
}

void* Map(void* at, uint64_t n, bool commit) {
    if (n == 0) return nullptr;   // VirtualAlloc rejects a zero size
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
    void* p = mmap(at, static_cast<size_t>(n), commit ? (PROT_READ | PROT_WRITE) : PROT_NONE, flags, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    if (at != nullptr && p != at) {   // a kernel without MAP_FIXED_NOREPLACE treats it as a hint
        munmap(p, static_cast<size_t>(n));
        return nullptr;
    }
    Record(p, n);
    return p;
}

void* Reserve(void* a, uint64_t n) { return Map(a, n, false); }
void* ReserveCommit(void* a, uint64_t n) { return Map(a, n, true); }

void* Commit(void* a, uint64_t n) {
    uintptr_t lo;
    uint64_t len;
    PageSpan(a, n, &lo, &len);
    if (mprotect(Ptr(lo), static_cast<size_t>(len), PROT_READ | PROT_WRITE) != 0) return nullptr;
    return Ptr(lo);   // VirtualAlloc(MEM_COMMIT) returns the page-rounded base
}

void Decommit(void* a, uint64_t n) {
    uintptr_t lo;
    uint64_t len;
    PageSpan(a, n, &lo, &len);
    // MAP_FIXED only replaces pages in our existing reservation; guarantees zero on recommit.
    if (mmap(Ptr(lo), static_cast<size_t>(len), PROT_NONE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) std::abort();
    mprotect(Ptr(lo), static_cast<size_t>(len), PROT_NONE);
}

void Release(void* a) {
    const uint64_t bytes = Forget(a);
    if (bytes != 0) munmap(a, static_cast<size_t>(bytes));
}

// The ctor's InitializeCriticalSection + 5000 spin, in place in the +0x640 bytes.
void LockInit(Platform::LockStorage* cs) { Platform::MutexInit(cs, Platform::kLockSpinCount); }

const RtSysMemPlatform kPosix = {
    &Reserve, &ReserveCommit, &Commit, &Decommit, &Release, &PageSize,
    &LockInit, &Platform::MutexLock, &Platform::MutexUnlock, &Platform::MutexDestroy
};

}  // namespace

// ---------------------------------------------------------------------------
// The six exported Platform* members (allocator_win.cpp has the retail notes).
// ---------------------------------------------------------------------------

// 0x1800c7b30
void MemoryAllocator::PlatformConstruct() { pageSize = 0; }

// 0x1800c7b50 / 0x1800c7c70: `ret` in retail.
void MemoryAllocator::PlatformDestruct() {}
void MemoryAllocator::PlatformShutdown() {}

// 0x1800c7c40
bool MemoryAllocator::PlatformStartup() {
    pageSize = PageSize();
    return true;
}

// 0x1800c7b60: reserve (and commit when asked) the page-rounded size at `at`.
void* MemoryAllocator::PlatformAllocate(uint64_t bytes, void* at, bool commitToo) {
    uint64_t page = pageSize;
    uint64_t n = ((bytes - 1 + page) / page) * page;
    return Map(at, n, commitToo);
}

// 0x1800c7bb0: MEM_RELEASE ignores the size; so does this (the table has it).
void MemoryAllocator::PlatformFree(void* p, uint64_t /*bytes*/) {
    Release(p);
}

extern "C" void RtSystemAllocatorInstallPlatform() {
    RtSysMemPlatformInstall(&kPosix);
}
