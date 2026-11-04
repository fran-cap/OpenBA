// ============================================================================
// src/diag/alloc_cutover_arm.cpp — System::MemoryAllocator: the composed
//                                    (locked) Allocate/Free entry points
// ============================================================================
//
// This is the surviving half of the old standalone/rt_alloc_swap.cpp. The other
// half — the byte-patch detour installer and the observe-only shadow driver —
// existed only to take the heap away from a loaded Runtime-x64.dll and to
// compare our arena against retail's. There is no DLL any more, so there are no
// DLL-internal callers to redirect and nothing to compare against; both were
// deleted with the rest of the raw door (ENDGAME.md A.3/A.4).
//
// WHAT IS STILL NEEDED. The RAW ported Allocate/Free members are byte-faithful
// and therefore take no lock, exactly as retail's do. The instrument seat's pair
// below composes them with the allocator critical section at self+0x640 (and the
// ALLOCSTATS record and the teardown heap check). Composing it here rather than
// inside the port keeps runtime/src free of <windows.h> and free of non-retail
// behaviour. (B3R W2-RES: the composed pair used to be handed back through the
// cutover armer for registration under ?Allocate@ / ?Free@; the registry is
// gone, and SwapAllocate, whose one use that was, with it.)
//
// System::MemoryAllocator::MemAlloc/MemFree take the same lock themselves,
// faithfully, as retail's statics do.
//
// B3R W0-A0: this file also composes the two diagnostic instruments on the
// allocator's ONE instrument seat (System::g_memoryAllocatorInstrument, see
// RtAllocInstrument_MemAlloc below).
// ============================================================================

#include "platform_sync.h"   // Platform::MutexLock / MutexUnlock on the +0x640 lock

#include <cstdint>
#include <cstdio>

#include "system/MemoryAllocator.h"   // -Iruntime/types
#include "options.h"
#include "alloc_cutover_arm.h"        // RtAllocInstrument_MemAlloc / _MemFree (B3R W2-EZ)

using rt::system::MemoryAllocator;

// allocator_lock.cpp / redzone_alloc.cpp — the diagnostics the composed stub
// must keep, in the order the lock's own wrappers ran them.
extern "C" void RedzoneAlloc_TeardownOp(const char* what);
extern "C" void AllocStats_RecordAlloc(void* p, uint64_t size, void* caller);
extern "C" void AllocStats_RecordFree(void* p);

namespace {

inline Platform::LockStorage* AllocCs(MemoryAllocator* self) {
    return &self->lock;
}

// The composed body: the teardown check, lock, the RAW member Allocate (which
// never consults the instrument seat), the stats record, unlock. The teardown
// check runs BEFORE the allocator lock (review F-287): it takes the redzone
// g_mutex and then the allocator lock, the order the redzone watchdog and the
// shim use, so taking the allocator lock first here was an ABBA deadlock.
void* ComposedAllocate(MemoryAllocator* self, uint64_t size, int align, void* caller) {
    Platform::LockStorage* cs = AllocCs(self);
    RedzoneAlloc_TeardownOp("teardown-alloc");
    Platform::MutexLock(cs);
    void* r = self->Allocate(size, align);
    AllocStats_RecordAlloc(r, size, caller);
    Platform::MutexUnlock(cs);
    return r;
}

void SwapFree(MemoryAllocator* self, void* p) {
    if (!p) return;                       // retail's TEST RDX,RDX; JZ
    Platform::LockStorage* cs = AllocCs(self);
    RedzoneAlloc_TeardownOp("teardown-free");   // before the lock: see ComposedAllocate
    Platform::MutexLock(cs);
    AllocStats_RecordFree(p);
    self->Free(p);                        // RAW member: never the instrument seat
    Platform::MutexUnlock(cs);
}

}  // namespace

// ---------------------------------------------------------------------------
// The instrument seat's composed pair (B3R W0-A0, `behaviour:`).
//
// System::g_memoryAllocatorInstrument (MemoryAllocator.h) is consulted at the
// entry of MemoryAllocator::MemAlloc / ::MemFree. These two are what it holds
// when BANISHED_ALLOC_STATS is armed, and what BANISHED_REDZONE's shim calls
// underneath itself for frees (redzone_alloc.cpp's g_realFree). They reach the
// heap through the RAW members only (ComposedAllocate / SwapFree above), never
// through MemAlloc / MemFree, so the seat cannot re-enter itself and an
// allocation is instrumented once.
//
// `_singleton` is read LIVE here, exactly as retail's MemAlloc reads it.
//
// ALLOCSTATS caller attribution on this path is the return address into the
// allocator entry (MemAlloc, or its caller when the compiler tail-calls the
// seat), not always the allocating site: a limitation of the seat, diagnostic
// only.
// ---------------------------------------------------------------------------
extern "C" void* RtAllocInstrument_MemAlloc(uint64_t size, int align) {
    return ComposedAllocate(System::MemoryAllocator::_singleton, size, align,
                            __builtin_return_address(0));
}

extern "C" void RtAllocInstrument_MemFree(void* p) {
    SwapFree(System::MemoryAllocator::_singleton, p);
}

// Handed to RtSystemAllocatorSetCutoverArmer() BEFORE RuntimeInit() (see
// alloc_cutover_arm.h); RtSystemAllocatorArm() calls it once.
//
// Nothing here installs a detour, and that is not a shortcut: a detour existed
// only to redirect the DLL's OWN internal callers, which were direct rel32 CALLs
// inside the DLL. With no DLL there are none.
//
// This is where BANISHED_ALLOC_STATS arms the instrument seat. The seat is taken
// only if nothing holds it; a redzone shim that holds it already feeds the stats
// (its g_realAlloc / g_realFree are RtAllocInstrument_MemAlloc / _MemFree).
extern "C" bool RtAllocSwapArmCutover() {
    if (Opt().allocStats && System::g_memoryAllocatorInstrument.memAlloc == nullptr) {
        System::g_memoryAllocatorInstrument.memAlloc = &RtAllocInstrument_MemAlloc;
        System::g_memoryAllocatorInstrument.memFree  = &RtAllocInstrument_MemFree;
        fprintf(stderr, "[ALLOCSTATS] instrument seat armed: MemoryAllocator::MemAlloc/"
                        "MemFree -> the composed stats pair\n");
    }
    fflush(stderr);
    return true;
}
