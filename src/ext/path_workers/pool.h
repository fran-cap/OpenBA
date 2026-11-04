// ============================================================================
// src/ext/path_workers/pool.h -- ext.path_workers: the worker pool core
// ============================================================================
// NOT RETAIL. Standard C++ only (std::mutex / std::condition_variable, as
// src/platform/common/event_std.cpp already uses): the unit tests link
// pool.cpp alone and drive it with std::thread; the game drives the same
// WorkerMain from System::Thread subclasses (path_workers.cpp). This file
// creates no thread.
// docs/extensions/handins/PATHRESEARCH.md §4.2 is the spec, PATHWORKERS.md the
// hand-in.
//
// THE MODEL. Retail's single pathfinder worker (PathfinderThread::Run
// 0x1400a6360) pops a FIFO of commands and applies each (ApplyCommand
// 0x1400a6430): 0 passable mask, 1 one step cost, 4 search flags (the search
// PARAMETERS), 2 / 3 one cell's terrain / blocked bits (the GRID), 5 a path
// request. With the extension the same thread becomes a DISPATCHER
// (DispatchCommand below):
//   0 / 1 / 4  applied at once, in queue order, to the parameters record (the
//              host's: in the game, PathfinderThread::grid's own fields)
//   5          the parameters as they stand NOW are copied into a Job with the
//              request, and the job is queued for the pool
//   2 / 3      a BARRIER: wait until every queued and running job has finished,
//              then apply the edit
// So every job sees exactly the parameters the serial worker would have held
// at that queue position (its snapshot), and exactly the grid it would have
// searched (no edit can land between a request's dispatch and its finish, and
// every edit queued before the request was applied before it was dispatched).
// Only the completion TIME of a request changes; its content cannot.
// ============================================================================
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>

#include "system/annotated_mutex.h"   // Sys::Mutex / Sys::MutexLock (clang -Wthread-safety)

namespace Ext {
namespace PathWorkers {

constexpr int kMaxWorkers = 4;
constexpr int kStepCosts  = 0x20;   // GridPathSearch::stepCost[0x20]

// The worker count is Cpu::PathWorkers (src/platform/common/cpu/roster.h, block CPULIMIT):
// clamp(physical cores in the affinity mask - 2 - (render_pipe ? 1 : 0), 1, kMaxWorkers).
// (It replaced ChooseWorkers(hardware_concurrency) = clamp(threads - 2, 1, 4).)

// A worker command: PathCommandArgs' four ints (PathSearch.h), without the
// game header.
struct Command {
    int32_t type;
    int32_t arg1;
    int32_t arg2;
    int32_t arg3;
};

// The search parameters a request runs under: GridPathSearch +0x50..+0xD6
// (commands 0, 1, 4), plus what ext.fast_paths needs when it composes.
struct Params {
    int32_t  stepCost[kStepCosts];
    uint32_t passableTypes;
    uint8_t  allowDiagonal;
    uint8_t  manhattan;
    uint8_t  respectBlocked;
    uint8_t  fast;            // ext.fast_paths selected for this game
    uint32_t presentTypes;    // fast only: the terrain types the grid holds at dispatch
};

// One path request (command 5) with its parameter snapshot.
struct Job {
    Params  params;
    int32_t start;
    int32_t goal;
    int32_t slot;   // the result slot (command 5's arg3)
};

// What a worker does with a job: search on its own scratch, publish the result.
// Called on a pool thread; `worker` is the thread's index in [0, Workers()).
class Runner {
public:
    virtual void RunJob(int worker, const Job& job) = 0;
protected:
    ~Runner() = default;
};

class Pool {
public:
    Pool(Runner& runner, int workers);
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    int Workers() const { return workers_; }

    // Dispatcher side.
    void Submit(const Job& job);   // queue a job (FIFO pick-up order)
    void Barrier();                // block until no job is queued or running
    int  InFlight();               // queued + running

    // The body of pool thread `worker`: runs jobs until Stop() and the queue is
    // empty, then returns. One call per thread, each with its own index.
    void WorkerMain(int worker);

    // Every job already submitted still runs (each result is published); then
    // each WorkerMain returns. Submit after Stop is a caller bug (ignored).
    void Stop();

    // Jobs completed since construction (tests and the shutdown log).
    long long Completed();

private:
    Runner&                 runner_;
    const int               workers_;
    // GUARDED_BY: checked by clang's thread-safety analysis (system/thread_annotations.h).
    Sys::Mutex              mutex_;
    std::condition_variable work_;    // a job was queued, or Stop
    std::condition_variable idle_;    // inFlight_ reached 0
    std::deque<Job>         queue_ GUARDED_BY(mutex_);
    int                     inFlight_ GUARDED_BY(mutex_) = 0;
    long long               completed_ GUARDED_BY(mutex_) = 0;
    bool                    stop_ GUARDED_BY(mutex_) = false;
};

// The dispatcher's one decision per command (the comment at the top). `Host`
// provides:
//   void Apply(const Command&)    commands 0-4 exactly as the serial worker
//                                 applies them (the game: ApplyCommand)
//   Job  MakeJob(const Command&)  command 5: the current parameters + request
template <class Host>
inline void DispatchCommand(Pool& pool, Host& host, const Command& cmd) {
    switch (cmd.type) {
    case 5:
        pool.Submit(host.MakeJob(cmd));
        return;
    case 2:
    case 3:
        pool.Barrier();   // the grid is read by every running job
        host.Apply(cmd);
        return;
    default:              // 0, 1, 4: the parameters record; jobs hold snapshots
        host.Apply(cmd);
        return;
    }
}

}  // namespace PathWorkers
}  // namespace Ext
