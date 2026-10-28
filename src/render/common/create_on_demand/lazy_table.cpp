// ============================================================================
// src/render/common/create_on_demand/lazy_table.cpp -- ext.create_on_demand's table of deferred
// GPU creates (docs/extensions/handins/CREATEONDEMAND.md). The contract is in lazy_table.h.
// ============================================================================
// NOT RETAIL.
// ============================================================================

#include "create_on_demand/lazy_table.h"

namespace CreateOnDemand {

namespace {

uint64_t NowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

}  // namespace

LazyTable::LazyTable(int workers) : _workers(workers < 0 ? 0 : workers) {}

LazyTable::~LazyTable() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
    }
    _workCv.notify_all();
    for (std::thread& t : _threads) {
        t.join();
    }
}

void LazyTable::Untrack(Entry& e, const void* obj) {
    if (e.tracked) _stamps.Remove(obj);
    e.tracked = false;
    _made.erase(obj);
}

void LazyTable::Add(const void* obj, std::unique_ptr<Record> record) {
    std::unique_lock<std::mutex> lock(_mutex);
    // A running record is never replaced under its thread (the caller's Forget already waited).
    _doneCv.wait(lock, [&] {
        auto it = _entries.find(obj);
        return it == _entries.end() || it->second.state != State::Running;
    });
    const uint64_t bytes = record->Bytes();
    Entry& e = _entries[obj];
    if (e.record != nullptr) {
        // The caller Forgot obj first; a second record for a live one replaces it.
        if (e.state != State::Made) {
            _stats.pendingBytes -= e.record->Bytes();
            --_stats.pending;
            ++_stats.forgotten;
        }
        Untrack(e, obj);
    } else {
        _count.fetch_add(1, std::memory_order_release);
    }
    e.record   = std::move(record);
    e.state    = State::Idle;
    e.madeOnce = false;
    e.kept     = false;
    ++_stats.recorded;
    _stats.recordedBytes += bytes;
    ++_stats.pending;
    _stats.pendingBytes += bytes;
}

void LazyTable::RunClaimed(std::unique_lock<std::mutex>& lock, const void* obj, bool onUse) {
    Record* const record = _entries.find(obj)->second.record.get();
    ++_running;
    lock.unlock();
    const uint64_t t0 = NowNs();
    const Outcome outcome = record->Materialize();
    const uint64_t dt = NowNs() - t0;
    lock.lock();
    --_running;
    _stats.materialNs += dt;
    Entry& e = _entries.find(obj)->second;
    if (outcome == Outcome::Retry) {
        // Kept, unpublished: the next use (or prefetch) after the device is back makes it.
        ++_stats.retried;
        e.state = State::Idle;
        _doneCv.notify_all();
        return;
    }
    const bool ok = outcome == Outcome::Made;
    record->Publish();
    const uint64_t bytes = record->Bytes();
    --_stats.pending;
    _stats.pendingBytes -= bytes;
    if (onUse) {
        ++_stats.onUse;
        _stats.onUseNs += dt;
        if (dt > _stats.worstOnUseNs) _stats.worstOnUseNs = dt;
    } else {
        ++_stats.prefetched;
    }
    if (ok) {
        // Kept, made: its Locator stays for a re-materialization after an eviction.
        _stats.materialBytes += bytes;
        if (e.madeOnce) ++_stats.remade;
        e.madeOnce = true;
        e.state    = State::Made;
        e.tracked  = !e.kept && _stamps.Insert(obj, _epoch.load(std::memory_order_relaxed));
        if (e.tracked) _made.insert(obj);
    } else {
        ++_stats.failed;
        Untrack(e, obj);
        _entries.erase(obj);
        _count.fetch_sub(1, std::memory_order_release);
    }
    _doneCv.notify_all();
}

void LazyTable::EnsureSlow(const void* obj) {
    std::unique_lock<std::mutex> lock(_mutex);
    bool waited = false;
    for (;;) {
        auto it = _entries.find(obj);
        if (it == _entries.end() || it->second.state == State::Made) return;
        if (it->second.state == State::Running) {
            if (!waited) ++_stats.waited;
            waited = true;
            _doneCv.wait(lock);
            continue;
        }
        if (_paused) return;   // no device: the use goes without, as on a failed create
        it->second.state = State::Running;
        RunClaimed(lock, obj, true);
        return;
    }
}

void LazyTable::ForgetSlow(const void* obj) {
    std::unique_lock<std::mutex> lock(_mutex);
    bool waited = false;
    for (;;) {
        auto it = _entries.find(obj);
        if (it == _entries.end()) return;
        if (it->second.state == State::Running) {
            if (!waited) ++_stats.waited;
            waited = true;
            _doneCv.wait(lock);
            continue;
        }
        if (it->second.state != State::Made) {
            _stats.pendingBytes -= it->second.record->Bytes();
            --_stats.pending;
            ++_stats.forgotten;
        }
        Untrack(it->second, obj);
        _entries.erase(it);   // a queued one is skipped by the worker that pops it
        _count.fetch_sub(1, std::memory_order_release);
        return;
    }
}

void LazyTable::StartWorkers() {
    for (int i = 0; i < _workers; ++i) {
        _threads.emplace_back([this, i] { WorkerMain(i); });
    }
}

void LazyTable::Prefetch(const void* obj) {
    if (_count.load(std::memory_order_acquire) == 0 || _workers == 0) return;
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _entries.find(obj);
    if (it == _entries.end()) return;
    QueueLocked(it->second, obj);
}

void LazyTable::Keep(const void* obj) {
    if (_count.load(std::memory_order_acquire) == 0) return;
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _entries.find(obj);
    if (it == _entries.end() || it->second.kept) return;
    Entry& e = it->second;
    e.kept = true;
    ++_stats.kept;
    if (e.state == State::Made) Untrack(e, obj);   // out of the sweep's set
    if (_workers > 0) QueueLocked(e, obj);
}

void LazyTable::QueueLocked(Entry& e, const void* obj) {
    if (e.state != State::Idle || _stop) return;
    e.state = State::Queued;
    _queue.push_back(obj);
    if (_threads.empty()) StartWorkers();
    _workCv.notify_one();
}

void LazyTable::SetIdleLimit(uint32_t epochs, std::chrono::milliseconds epochLength) {
    std::lock_guard<std::mutex> lock(_mutex);
    _idleEpochs  = epochs;
    _epochLength = epochLength;
    if (_threads.empty() && _workers > 0 && epochs != 0 && epochLength.count() > 0) StartWorkers();
    _workCv.notify_all();
}

int64_t LazyTable::Sweep() {
    std::lock_guard<std::mutex> one(_sweepMutex);
    const uint32_t now = _epoch.fetch_add(1, std::memory_order_relaxed) + 1;
    uint32_t idle = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ++_stats.sweeps;
        idle = _paused ? 0 : _idleEpochs;
    }
    if (idle == 0) return 0;
    // The candidates first, from the made objects only, under the table's mutex alone (no use waits on the
    // scan): what was not used for more than `idle` whole epochs (a use late in the last
    // epoch is not idle yet).
    auto idleNow = [&](const void* obj) {
        uint32_t last = 0;
        return _stamps.LastUse(obj, &last) && now - last > idle;
    };
    std::vector<const void*> candidates;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const void* obj : _made) {
            if (idleNow(obj)) candidates.push_back(obj);
        }
    }
    if (candidates.empty()) return 0;
    // No use may be reading a handle while it is given back: close the gate (no lock held), then take
    // the table's mutex (no materialization publishes meanwhile) and give back each candidate still
    // made and still idle.
    const uint64_t t0 = NowNs();
    _gate.Close();
    int64_t evicted = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const void* obj : candidates) {
            auto it = _entries.find(obj);
            if (it == _entries.end()) continue;
            Entry& e = it->second;
            if (e.state != State::Made || !e.tracked || !idleNow(obj)) continue;
            e.record->Evict();
            Untrack(e, obj);
            e.state = State::Idle;
            const uint64_t bytes = e.record->Bytes();
            ++_stats.pending;
            _stats.pendingBytes += bytes;
            ++_stats.evicted;
            _stats.evictedBytes += bytes;
            ++evicted;
        }
        _stats.evictNs += NowNs() - t0;
    }
    _gate.Open();
    if (evicted != 0 && _sweepHook != nullptr) _sweepHook(evicted);
    return evicted;
}

void LazyTable::WorkerMain(int index) {
    std::unique_lock<std::mutex> lock(_mutex);
    auto nextSweep = std::chrono::steady_clock::now() + _epochLength;
    for (;;) {
        auto ready = [this] { return _stop || (!_paused && !_queue.empty()); };
        // Worker 0 also runs the sweep, once per epoch, while an idle limit is set.
        if (index == 0 && _idleEpochs != 0 && _epochLength.count() > 0) {
            if (!_workCv.wait_until(lock, nextSweep, ready)) {
                lock.unlock();
                Sweep();
                lock.lock();
                nextSweep = std::chrono::steady_clock::now() + _epochLength;
                continue;
            }
        } else {
            _workCv.wait(lock, ready);
            nextSweep = std::chrono::steady_clock::now() + _epochLength;
        }
        if (_stop) return;
        const void* const obj = _queue.front();
        _queue.pop_front();
        auto it = _entries.find(obj);
        // Gone (forgotten, or materialized by a use), or a later record at a reused address.
        if (it == _entries.end() || it->second.state != State::Queued) {
            if (_queue.empty()) _doneCv.notify_all();
            continue;
        }
        it->second.state = State::Running;
        RunClaimed(lock, obj, false);
    }
}

void LazyTable::Pause() {
    std::unique_lock<std::mutex> lock(_mutex);
    _paused = true;
    _doneCv.notify_all();   // DrainQueueUnlessPaused returns
    _doneCv.wait(lock, [this] { return _running == 0; });
}

void LazyTable::Resume() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _paused = false;
    }
    _workCv.notify_all();
    _doneCv.notify_all();
}

void LazyTable::DrainQueue() {
    std::unique_lock<std::mutex> lock(_mutex);
    _doneCv.wait(lock, [this] { return _queue.empty() && _running == 0; });
}

void LazyTable::DrainQueueUnlessPaused() {
    std::unique_lock<std::mutex> lock(_mutex);
    _doneCv.wait(lock, [this] { return _paused || (_queue.empty() && _running == 0); });
}

LazyTable::Stats LazyTable::GetStats() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _stats;
}

}  // namespace CreateOnDemand
