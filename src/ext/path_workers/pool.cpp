// ============================================================================
// src/ext/path_workers/pool.cpp -- ext.path_workers: the worker pool core
// ============================================================================
// NOT RETAIL. pool.h has the model and the contract.
// ============================================================================
#include "ext/path_workers/pool.h"

namespace Ext {
namespace PathWorkers {

Pool::Pool(Runner& runner, int workers)
    : runner_(runner), workers_(workers < 1 ? 1 : (workers > kMaxWorkers ? kMaxWorkers : workers)) {}

void Pool::Submit(const Job& job) {
    {
        Sys::MutexLock lk(mutex_);
        if (stop_) return;
        queue_.push_back(job);
        ++inFlight_;
    }
    work_.notify_one();
}

void Pool::Barrier() {
    Sys::MutexLock lk(mutex_);
    idle_.wait(lk.Native(), [this]() REQUIRES(mutex_) { return inFlight_ == 0; });
}

int Pool::InFlight() {
    Sys::MutexLock lk(mutex_);
    return inFlight_;
}

void Pool::WorkerMain(int worker) {
    for (;;) {
        Job job;
        {
            Sys::MutexLock lk(mutex_);
            work_.wait(lk.Native(), [this]() REQUIRES(mutex_) { return stop_ || !queue_.empty(); });
            if (queue_.empty()) return;   // stopped and drained
            job = queue_.front();
            queue_.pop_front();
        }
        runner_.RunJob(worker, job);
        bool idle;
        {
            Sys::MutexLock lk(mutex_);
            ++completed_;
            idle = --inFlight_ == 0;
        }
        if (idle) idle_.notify_all();
    }
}

void Pool::Stop() {
    {
        Sys::MutexLock lk(mutex_);
        stop_ = true;
    }
    work_.notify_all();
}

long long Pool::Completed() {
    Sys::MutexLock lk(mutex_);
    return completed_;
}

}  // namespace PathWorkers
}  // namespace Ext
