#pragma once
// ============================================================================
// src/engine/system/annotated_mutex.h -- std::mutex with clang thread-safety annotations
// ============================================================================
// NOT RETAIL; port-only code only (never a retail-ABI field: a std::mutex is not a pinned type).
// libstdc++'s std::mutex carries no capability attribute, so GUARDED_BY(a std::mutex) cannot be
// checked on the Linux clang build. `Sys::Mutex` is the same std::mutex with the attributes on, and
// `Sys::MutexLock` is std::unique_lock over it, annotated as a scoped capability. Neither adds a
// byte or an instruction: every member is an inline forward to the standard type.
//
// A condition variable waits on `lock.Native()` (the std::unique_lock). The wait releases and
// re-takes the mutex inside the standard library, which the analysis does not see; it treats the
// lock as held across the wait, which is what the caller observes. A wait predicate that reads
// guarded fields is a lambda annotated REQUIRES(the mutex).
// ============================================================================
#include <mutex>

#include "thread_annotations.h"

namespace Sys {

class CAPABILITY("mutex") Mutex {
public:
    Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock() ACQUIRE() { _m.lock(); }
    void unlock() RELEASE() { _m.unlock(); }
    bool try_lock() TRY_ACQUIRE(true) { return _m.try_lock(); }

    // The standard mutex, for a std::unique_lock a condition variable needs.
    std::mutex& Native() { return _m; }

private:
    std::mutex _m;
};

class SCOPED_CAPABILITY MutexLock {
public:
    explicit MutexLock(Mutex& m) ACQUIRE(m) : _lk(m.Native()) {}
    ~MutexLock() RELEASE() {}
    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;

    // Early release / re-take within the scope (std::unique_lock::unlock / lock).
    void Unlock() RELEASE() { _lk.unlock(); }
    void Lock() ACQUIRE() { _lk.lock(); }

    // For std::condition_variable::wait / wait_for / wait_until.
    std::unique_lock<std::mutex>& Native() { return _lk; }

private:
    std::unique_lock<std::mutex> _lk;
};

}  // namespace Sys
