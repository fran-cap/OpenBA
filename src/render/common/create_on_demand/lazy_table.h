#pragma once
// ============================================================================
// src/render/common/create_on_demand/lazy_table.h -- ext.create_on_demand's table of GPU objects
// whose create waits for their first use (docs/extensions/handins/CREATEONDEMAND.md, issue #13)
// ============================================================================
// NOT RETAIL. Backend-neutral (unit-tested: tests/unit/ext_create_on_demand_test.cpp).
//
// The backend records, instead of creating it, a static GPU object whose bytes it can read again
// from its package (payload.h): a Record keyed by the engine companion it belongs to (a vertex /
// index buffer's or an image's platform data). The record is MATERIALIZED by whichever comes first:
//   - Ensure(obj): a use of the object (Bind, Lock, SetData, Update, Read, a save) on any thread,
//     which runs the record there, or waits for a worker already running it;
//   - a prefetch worker, after Prefetch(obj) queued it (the extension queues the buffers and textures
//     of a mesh when a mesh instance is given it: before the object is ever drawn), or Keep(obj) did.
// Materialize does the heavy part (re-decode, create) with no lock held; Publish then writes the
// object's handles under the table's mutex and the record is kept, MADE. So a use sees either a made
// object, no record (the object was never deferred) or waits until the record is made.
// Forget(obj) (a destructor, a re-create) drops the record (waiting for one that is running), so the
// object's own release then finds its handles.
//
// EVICTION (section 7 of the hand-in). With an idle limit set (SetIdleLimit, in epochs: minutes in
// the exe), a made object not used for that long gives its handles back (Record::Evict, under the
// table's mutex and inside a closed UseGate, so no use is reading them) and its record is IDLE again:
// the next use re-materializes it from its Locator. Every use stamps the object (Use / UseStamps);
// a sweep (Sweep, from worker 0 once per epoch in the exe) advances the epoch and evicts. A KEPT object
// (Keep) is never evicted: what is drawn all at once when a window opens (the UI's sprite sheets).
//
// DEVICE. Pause() (the device going away: exit, a device-lost recovery) stops new materializations
// and waits for the running ones; Resume() (a device made) lets them go on. A record that meets a
// paused table stays recorded: the next use after Resume materializes it on the new device.
//
// Threads: one mutex, two condition variables; nothing heavy under the mutex but an eviction's
// releases. Ensure's fast path is one relaxed load when nothing is recorded (the backend tests its
// own handle first: a made object never comes here).
// ============================================================================

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "create_on_demand/use_tracker.h"

namespace CreateOnDemand {

// What a materialization came to.
enum class Outcome : uint8_t {
    Made,     // the object exists; Publish writes it
    Failed,   // a create or the re-decode failed: Publish writes what a failed create leaves
    Retry,    // no usable device (gone, or lost): nothing published, the record stays for a later use
};

// A deferred create. The backend derives one per object kind.
class Record {
public:
    virtual ~Record() = default;
    // The re-decode and the create, no lock held.
    virtual Outcome Materialize() = 0;
    // Under the table's mutex: write what Materialize made into the object's fields.
    virtual void Publish() = 0;
    // Under the table's mutex, inside a closed UseGate: give the made object's handles back (release
    // them, null the fields) so the next use materializes it again.
    virtual void Evict() = 0;
    // The payload's byte count (the counters).
    virtual uint64_t Bytes() const = 0;
};

class LazyTable {
public:
    // `workers` prefetch threads, made at the first Prefetch (or by SetIdleLimit).
    explicit LazyTable(int workers);
    ~LazyTable();

    // Record `obj`'s deferred create (replacing nothing: the caller Forgot it first).
    void Add(const void* obj, std::unique_ptr<Record> record);

    // Before a use: run obj's record here, or wait for the thread running it. No-op when obj has none.
    void Ensure(const void* obj) {
        if (_count.load(std::memory_order_acquire) != 0) EnsureSlow(obj);
    }
    // Before a destructor / re-create: drop the record (waiting for a running one).
    void Forget(const void* obj) {
        if (_count.load(std::memory_order_acquire) != 0) ForgetSlow(obj);
    }
    // Queue obj's record for a worker (no-op when it has none, or it is queued / running / made).
    void Prefetch(const void* obj);
    // Prefetch, and the object is never evicted (made already: it leaves the sweep's set).
    void Keep(const void* obj);

    // Every slot that reads a companion's handles: Enter, Use(obj), ..., Leave.
    void Enter() { _gate.Enter(); }
    void Leave() { _gate.Leave(); }
    void Use(const void* obj) { _stamps.Touch(obj, _epoch.load(std::memory_order_relaxed)); }

    // Made objects unused for more than `epochs` whole epochs are evicted by Sweep; 0 = never. `epochLength`: how
    // often worker 0 runs Sweep by itself (0 = only when called).
    void SetIdleLimit(uint32_t epochs, std::chrono::milliseconds epochLength);
    // Advance the epoch and evict what is idle. Returns the number evicted.
    int64_t Sweep();
    // Run after every sweep that evicted something (no lock held): the backend's summary line.
    void SetSweepHook(void (*hook)(int64_t evicted)) { _sweepHook = hook; }

    // The device is going away: no new materialization starts; returns when none is running.
    void Pause();
    // A device is there again.
    void Resume();
    // Wait until the prefetch queue is empty and no worker runs (tests, diagnostics).
    void DrainQueue();
    // DrainQueue that also returns once the table is paused (a device going away leaves the queue
    // unworked): the loader's wait for its prefetches (lazy_create.h FinishPrefetches).
    void DrainQueueUnlessPaused();

    struct Stats {
        int64_t  recorded       = 0;   // records added, run total
        uint64_t recordedBytes  = 0;
        int64_t  pending        = 0;   // records not made (never made, or evicted)
        uint64_t pendingBytes   = 0;
        int64_t  onUse          = 0;   // materialized by Ensure on the using thread
        int64_t  prefetched     = 0;   // materialized by a worker
        int64_t  kept           = 0;   // records marked Keep, run total
        uint64_t materialBytes  = 0;   // made, run total (a re-made object counts again)
        int64_t  failed         = 0;   // Materialize failed (published as a failed create)
        int64_t  retried        = 0;   // Materialize found no usable device (kept)
        int64_t  waited         = 0;   // Ensure / Forget calls that waited for another thread
        int64_t  forgotten      = 0;   // dropped unmade
        int64_t  evicted        = 0;   // made objects given back, run total
        uint64_t evictedBytes   = 0;
        int64_t  remade         = 0;   // of the materializations, objects made before (evicted since)
        int64_t  sweeps         = 0;
        uint64_t materialNs     = 0;   // time inside Materialize, all threads
        uint64_t onUseNs        = 0;   // of it, on the using thread
        uint64_t worstOnUseNs   = 0;   // the longest single on-use materialization
        uint64_t evictNs        = 0;   // time the gate was closed, all sweeps
    };
    Stats GetStats();

private:
    enum class State : uint8_t { Idle, Queued, Running, Made };
    struct Entry {
        std::unique_ptr<Record> record;
        State                   state   = State::Idle;
        bool                    tracked = false;   // in _stamps (made, and it fit)
        bool                    madeOnce = false;
        bool                    kept    = false;   // Keep: never evicted
    };

    void EnsureSlow(const void* obj);
    void ForgetSlow(const void* obj);
    // Runs the record of `obj` (claimed by the caller: state Running) and publishes it. Called and
    // returns with `lock` held.
    void RunClaimed(std::unique_lock<std::mutex>& lock, const void* obj, bool onUse);
    void Untrack(Entry& e, const void* obj);
    // Prefetch's body, with the table's mutex held.
    void QueueLocked(Entry& e, const void* obj);
    void StartWorkers();
    void WorkerMain(int index);

    std::mutex                                 _mutex;
    std::condition_variable                    _doneCv;   // a record finished or the table resumed
    std::condition_variable                    _workCv;   // a record was queued or the table resumed
    std::unordered_map<const void*, Entry>     _entries;
    std::unordered_set<const void*>            _made;     // the entries in state Made (the sweep's scan)
    std::deque<const void*>                    _queue;
    std::vector<std::thread>                   _threads;
    std::atomic<int64_t>                       _count{0};   // _entries.size(), for the fast path
    std::atomic<uint32_t>                      _epoch{1};
    UseStamps                                  _stamps;
    UseGate                                    _gate;
    std::mutex                                 _sweepMutex;   // one sweep at a time
    void                                     (*_sweepHook)(int64_t) = nullptr;
    uint32_t                                   _idleEpochs  = 0;
    std::chrono::milliseconds                  _epochLength{0};
    int                                        _workers;
    int                                        _running = 0;
    bool                                       _paused  = false;
    bool                                       _stop    = false;
    Stats                                      _stats;
};

}  // namespace CreateOnDemand
