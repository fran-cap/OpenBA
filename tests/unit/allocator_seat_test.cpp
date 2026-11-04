// ============================================================================
// tests/unit/allocator_seat_test.cpp — the allocator's instrument seat, reached
//   through `System::MemoryAllocator::MemAlloc` / `MemFree`
// ============================================================================
//
// The diagnostic instruments (BANISHED_REDZONE's shim, BANISHED_ALLOC_STATS) sit
// on ONE seat, `System::g_memoryAllocatorInstrument`
// (src/engine/system/MemoryAllocator.h), consulted at the entry of the two
// statics. The property these tests pin — "an instrument armed AFTER a caller
// first allocated still sees every later allocation of that caller, and alloc
// and free move together" — is a property of the seat:
//
//   1. a call reaches whichever hook is in the seat AT THE TIME OF THE CALL:
//      arming, re-arming and disarming take effect on the next call, also for a
//      caller that holds a pointer to the static taken before the arming;
//   2. the armed hook receives the arguments unchanged and is called once.
//
// (B3R W2-RES: until then these two tests reached the statics through
// `rtcoll::DoorAlloc` / `DoorFree`, src/engine/collection/alloc_door.cpp, a
// function-pointer door with no product caller; it is deleted and the tests call
// the statics directly.)
//
// The unit build links src/engine/system/allocator.cpp (tests/unit/sources.cmake)
// so the real statics run; with a hook in the seat they never touch
// `_singleton` (null here), which is the seat's contract. Each test clears the
// seat before it ends. No other test arms it.
// ============================================================================

#include "unit.h"

#include <cstdint>

#include "system/MemoryAllocator.h"

namespace {

int      g_allocCallsA = 0, g_allocCallsB = 0;
uint64_t g_lastSize = 0;
int      g_lastAlign = 0;
char     g_blockA[16], g_blockB[16];

void* HookAllocA(uint64_t size, int align) {
    ++g_allocCallsA; g_lastSize = size; g_lastAlign = align; return g_blockA;
}
void* HookAllocB(uint64_t size, int align) {
    ++g_allocCallsB; g_lastSize = size; g_lastAlign = align; return g_blockB;
}

int   g_freeCallsA = 0, g_freeCallsB = 0;
void* g_lastFreed = nullptr;

void HookFreeA(void* p) { ++g_freeCallsA; g_lastFreed = p; }
void HookFreeB(void* p) { ++g_freeCallsB; g_lastFreed = p; }

}  // namespace

UNIT_TEST(memalloc_reads_the_instrument_seat_per_call) {
    System::MemoryAllocatorInstrument& seat = System::g_memoryAllocatorInstrument;
    // The ASan build arms the seat at startup (BANISHED_ASAN_HEAP): restored at the end.
    const System::MemoryAllocatorInstrument saved = seat;
    CHECK(BANISHED_ASAN_HEAP || seat.memAlloc == nullptr);

    // A pointer to the static, taken before anything is armed (a caller that
    // cached it keeps it).
    void* (*const memAlloc)(uint64_t, int) = &System::MemoryAllocator::MemAlloc;

    // (1) Armed AFTER the pointer was taken: the next call is instrumented.
    g_allocCallsA = g_allocCallsB = 0;
    seat.memAlloc = &HookAllocA;
    CHECK(memAlloc(0x30, 8) == g_blockA);
    CHECK_EQ(g_allocCallsA, 1);                        // (2) once
    CHECK_EQ(static_cast<int>(g_lastSize), 0x30);      // (2) unchanged
    CHECK_EQ(g_lastAlign, 8);

    // (1) Re-armed: the static, through the pointer or by name, reaches the new hook.
    seat.memAlloc = &HookAllocB;
    CHECK(System::MemoryAllocator::MemAlloc(0x10, 4) == g_blockB);
    CHECK_EQ(g_allocCallsA, 1);
    CHECK_EQ(g_allocCallsB, 1);
    CHECK_EQ(g_lastAlign, 4);

    seat = saved;
}

UNIT_TEST(memfree_reads_the_instrument_seat_per_call) {
    System::MemoryAllocatorInstrument& seat = System::g_memoryAllocatorInstrument;
    // The ASan build arms the seat at startup (BANISHED_ASAN_HEAP): restored at the end.
    const System::MemoryAllocatorInstrument saved = seat;
    CHECK(BANISHED_ASAN_HEAP || seat.memFree == nullptr);

    void (*const memFree)(void*) = &System::MemoryAllocator::MemFree;

    g_freeCallsA = g_freeCallsB = 0;
    seat.memFree = &HookFreeA;
    memFree(g_blockA);
    CHECK_EQ(g_freeCallsA, 1);
    CHECK(g_lastFreed == g_blockA);

    // The alloc and the free must move TOGETHER — a block allocated through the
    // shim and freed around it leaves a stale live-block entry and the canary
    // scan then reports a phantom overflow (redzone_alloc.cpp's
    // `ShimAllocStatic` banner). One seat for both makes that structural.
    seat.memFree = &HookFreeB;
    System::MemoryAllocator::MemFree(g_blockB);
    CHECK_EQ(g_freeCallsA, 1);
    CHECK_EQ(g_freeCallsB, 1);
    CHECK(g_lastFreed == g_blockB);

    // A null reaches the hook too: the seat is consulted before retail's own
    // null test, and every armed hook handles null (ShimFree, SwapFree).
    memFree(nullptr);
    CHECK_EQ(g_freeCallsB, 2);
    CHECK(g_lastFreed == nullptr);

    seat = saved;
}
