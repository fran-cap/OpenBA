#pragma once
// ============================================================================
// src/game/Game/pathfinder_queue_count.h -- NOT RETAIL (UB fix): every access to
// PathfinderThread::queueCount (+0x70) goes through these two.
//
// Retail's worker loop 0x1400a6360 reads the count outside the queue lock (0x1400a6366
// `CMP dword ptr [RCX+0x70],0`, 0x1400a6400 `CMP [RBX+0x70],ESI`) while the main thread's
// enqueuers write it under the lock (0x1400a6fe0 and its siblings): a data race in C++. The
// field stays a plain int32_t (no layout change, PathfinderThread.h's static_asserts hold);
// every read is an acquire load and every write a release store, which on x86-64 are the plain
// MOV / CMP retail runs. A read-modify-write stays a load then a store, as retail's: the
// writers are serialised by the lock, so no update is lost. A pointer, not a reference, so
// clang -Wthread-safety does not ask for the lock on the worker's unlocked reads. Header-only
// so the host unit tests use the same two (tests/unit/pathfinder_queue_count_test.cpp).
// ============================================================================
#include <cstdint>

inline int32_t LoadQueueCount(const int32_t* count) {
    return __atomic_load_n(count, __ATOMIC_ACQUIRE);
}

inline void StoreQueueCount(int32_t* count, int32_t value) {
    __atomic_store_n(count, value, __ATOMIC_RELEASE);
}
