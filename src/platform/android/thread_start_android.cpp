// src/platform/android/thread_start_android.cpp -- see thread_start_android.h.
#include "thread_start_android.h"

#include <atomic>
#include <pthread.h>
#include <sched.h>

extern "C" int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*),
                                     void* arg);

namespace {

std::atomic<uint64_t> g_startMask{0};

struct Start {
    void* (*start)(void*);
    void*    arg;
    uint64_t mask;
};

void* Trampoline(void* p) {
    const Start s = *static_cast<Start*>(p);
    delete static_cast<Start*>(p);
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int c = 0; c < 64; ++c) {
        if ((s.mask >> c) & 1u) CPU_SET(c, &set);
    }
    sched_setaffinity(0, sizeof(set), &set);
    return s.start(s.arg);
}

}  // namespace

// The link renames every pthread_create call of this library to this function.
extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*),
                                     void* arg) {
    const uint64_t mask = g_startMask.load(std::memory_order_relaxed);
    if (mask == 0) return __real_pthread_create(thread, attr, start, arg);
    Start* s = new Start{ start, arg, mask };
    const int rc = __real_pthread_create(thread, attr, &Trampoline, s);
    if (rc != 0) delete s;
    return rc;
}

namespace Platform {
namespace Android {

void SetThreadStartMask(uint64_t mask) {
    g_startMask.store(mask, std::memory_order_relaxed);
}

}  // namespace Android
}  // namespace Platform
