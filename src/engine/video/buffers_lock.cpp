// ============================================================================
// src/engine/video/buffers_lock.cpp — Video::Device's recursive render lock,
// used by the four Platform{Construct,Destruct} bodies in buffers.cpp (Wave 9
// lane W). Was src/platform/win/buffers_win.cpp; OS-free since Phase D.4
// THREADS E.3 (Platform::LockStorage / MutexLock / CurrentThreadId), moved here
// unchanged by SDL-5.
// ============================================================================
//
// WHY THIS FILE EXISTS AT ALL. `Video::ImageBuffer::PlatformConstruct` and its
// three siblings are not "platform" in any interesting sense — they create the
// dx11 companion object and splice `this` into a class-static intrusive list.
// The one OS-specific thing they do is take `Video::Device`'s lock, and they do
// it by INLINE COPY of `?Lock@Device@Video@@QEAAXXZ` (0x18000ca90) /
// `?Unlock@Device@Video@@QEAAXXZ` (0x18000cac0) rather than by calling them —
// four EnterCriticalSection / LeaveCriticalSection pairs sitting directly in
// the bodies.
//
// PLAN.md §4.1 decision 1 kept `src/engine/` free of <windows.h> outside a
// `_win` twin, so the lock lived in src/platform/win and buffers.cpp stayed
// portable. Since THREADS E.3 the OS calls are the Platform:: sync / thread
// primitives (sync_win.cpp, sync_posix.cpp), so the file is portable as it
// stands and needs no twin.
//
// THE LOCK IS RECURSIVE-BY-HAND, AND THAT IS NOT DECORATION. Retail keeps an
// owner thread id at Device+0x88 and a recursion depth at Device+0x8C alongside
// the CRITICAL_SECTION at +0x60, and `?VerifyLock@Device@Video@@QEAAXXZ`
// (0x18000cae0) reads them. src/render/dx11/ reads Device+0x100 at exactly this offset
// and calls Lock/Unlock by name, so the two fields have live readers on BOTH
// sides of the port: writing the depth but not the owner (or vice versa) would
// leave a lock that works and a VerifyLock that lies.
//
// Retail's exact sequence, from the four decompiles (they agree byte for byte):
//
//   lock:    d = *_singleton;
//            EnterCriticalSection(d + 0x60);
//            d->ownerTid = GetCurrentThreadId();
//            d->depth   += 1;
//
//   unlock:  d = *_singleton;              // RE-READ, not the cached `d`
//            d->depth -= 1;
//            if (d->depth == 0) d->ownerTid = 0xFFFFFFFF;
//            LeaveCriticalSection(d + 0x60);
//
// The unlock's re-read of the singleton is reproduced deliberately. It is
// observable if anything swaps `Device::_singleton` while the lock is held
// (device reset does exactly that in the retail teardown path), and "the
// compiler would have cached it" is not a licence to cache it for retail.
//
// NOTE ON THE NULL SINGLETON. `*_singleton` is dereferenced unguarded here,
// exactly as retail does. That is deliberate per CLAUDE.md's "don't fix a
// faithful crash with a null check": every caller of these four functions runs
// after `Video::Device` construction, and a null here means an upstream
// ordering defect that a guard would hide rather than fix.
// ============================================================================

#include <cstdint>

// Phase D.4 (THREADS E.3): no <windows.h> here any more. The lock is the
// Platform::LockStorage Device.h declares (RULE B5-6: still the opaque 0x28
// CRITICAL_SECTION bytes on Windows), taken through Platform::MutexLock /
// MutexUnlock, and the owner id is Platform::CurrentThreadId (GetCurrentThreadId).
#include "video/Device.h"
#include "platform_sync.h"
#include "platform_thread.h"

// Device's layout puts the owner tid at +0x88, which requires a 40-byte lock at
// +0x60: Device.h asserts sizeof(lock) == 0x28, sync_win.cpp that a
// CRITICAL_SECTION fills a Platform::LockStorage.

// `deviceSingleton` is the ADDRESS OF the exported `?_singleton@Device@Video@@`
// slot, not the Device itself — the re-read above is the whole reason it is
// passed as a slot pointer and not as an already-loaded `Device*`.
// The four bodies are the lock's primitives: Device.h declares what each acquires / releases, and
// clang's thread-safety analysis takes them on trust (it cannot equate the re-read `d` with the slot).
void RtVideoDeviceLock(Video::Device** deviceSingleton) NO_THREAD_SAFETY_ANALYSIS {
    Video::Device* d = *deviceSingleton;
    Platform::MutexLock(&d->lock);                    // EnterCriticalSection
    d->lockOwnerThreadId = Platform::CurrentThreadId();   // GetCurrentThreadId
    d->lockRecursionCount += 1;
}

void RtVideoDeviceUnlock(Video::Device** deviceSingleton) NO_THREAD_SAFETY_ANALYSIS {
    Video::Device* d = *deviceSingleton;   // retail re-reads
    d->lockRecursionCount -= 1;
    if (d->lockRecursionCount == 0)
        d->lockOwnerThreadId = 0xFFFFFFFFu;
    Platform::MutexUnlock(&d->lock);                  // LeaveCriticalSection
}

// ============================================================================
// The EXPORTED Video::Device::Lock / ::Unlock  (Wave 9 lane VID)
//
// Retail 0x18000ca90 / 0x18000cac0. These are NOT the two functions above with
// a different spelling, and the difference is worth stating because getting it
// wrong is invisible:
//
//   * the two above are the INLINE COPIES the PlatformConstruct/PlatformDestruct
//     bodies carry, and they read `Device::_singleton` themselves;
//   * these two are the exported members, and they operate on the `this` the
//     caller passed. src/render/dx11/ calls them by name on a Device it already holds.
//
// The store ORDER also differs, and is preserved rather than normalised:
//
//   0x18000ca99  ADD RCX,0x60
//   0x18000ca9d  CALL EnterCriticalSection
//   0x18000caa3  CALL GetCurrentThreadId
//   0x18000caa9  INC dword ptr [RBX+0x8c]        <-- depth FIRST
//   0x18000caaf  MOV dword ptr [RBX+0x88],EAX    <-- then the owner id
//
// while the inline copy stores the owner first. Neither order is observable
// from a single thread; both are transcribed as written, because "the compiler
// would have reordered it anyway" is not a licence to reorder it for retail.
//
// Unlock takes no singleton re-read at all — RCX IS the object:
//
//   0x18000cac0  DEC dword ptr [RCX+0x8c]
//   0x18000cac6  JNZ .leave
//   0x18000cac8  MOV dword ptr [RCX+0x88],0xffffffff
//   0x18000cad2  ADD RCX,0x60
//   0x18000cad6  JMP LeaveCriticalSection
//
// Note the predicate: retail tests the ZF from the DEC (depth != 0), which is
// the same as comparing the decremented value with zero. Reproduced as such.
// ============================================================================
void RtVideoDeviceLockThis(Video::Device* d) NO_THREAD_SAFETY_ANALYSIS {
    Platform::MutexLock(&d->lock);                    // EnterCriticalSection
    uint32_t tid = Platform::CurrentThreadId();       // GetCurrentThreadId
    d->lockRecursionCount += 1;
    d->lockOwnerThreadId = tid;
}

void RtVideoDeviceUnlockThis(Video::Device* d) NO_THREAD_SAFETY_ANALYSIS {
    d->lockRecursionCount -= 1;
    if (d->lockRecursionCount == 0)
        d->lockOwnerThreadId = 0xFFFFFFFFu;
    Platform::MutexUnlock(&d->lock);                  // LeaveCriticalSection
}
