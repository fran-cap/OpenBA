// ============================================================================
// src/ext/render_pipe/resource_pin.h -- RENDERPIPE: a step's free of a resource
// the in-flight snapshot still draws waits until the emit's pass 2 is over
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/android/P13_OVERLAP.md section 2.8).
//
// The snapshot (snapshot.h) copies geometry but SHARES its resources raw: an
// Object::ExternalPtr is one pointer with no refcount (object/External.h), so a
// reference per packet does not exist to take. Factory resources are freed only at
// a loop change, which the extension joins first (P13_OVERLAP 2.4). One resource a
// copy shares is not the factory's: HighlightComponent's outline, a heap
// Graphics::EdgeMesh the steps delete (Repaint, its rebuild, ~HighlightComponent)
// while an overlapped emit's pass 2 may still draw it (EdgeMeshInstance::Draw reads
// its list arrays and buffers; ~EdgeMesh zeroes the lists first, with no lock).
//
// THE PIN. Each slot's snapshot records the EdgeMeshes its copies share (PinSet).
// Before the publish (render thread idle) the packet's set goes in flight
// (PinGate::Publish); the emit says when its reads of them are over (ReadsOver: at
// its gate, before it parks or once it passed, and at the emit's end for an emit
// that never got there). A free of a pinned mesh before that WAITS for it
// (BeforeFree, from ~EdgeMesh's seam): the free itself then runs exactly as
// retail's -- the same calls in the same order on the same thread, so the heap
// sees what it sees without the pin. Nothing is deferred. Off the in-flight set,
// or once the reads are over, BeforeFree returns at once.
//
// THE BOUND. The wait wakes every millisecond to run the sent messages
// (Platform::DispatchSentMessages, handoff.h's SENT MESSAGES rule) and gives up
// after `boundUs`: the free then runs as it did before the pin (the race it closes),
// counted in PinStats::timeouts. The deadlock review (P13_OVERLAP 2.8) found no path
// on which pass 2 waits for what a freeing step holds; the bound only keeps a missed
// one from freezing the game.
//
// Engine-free (standard C++ and platform_thread.h): the unit test drives it with a
// std::thread as the render thread.
// ============================================================================
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

#include "platform_thread.h"   // Platform::DispatchSentMessages (review F-224)

namespace Ext {
namespace RenderPipe {

// One packet's pinned resources (main thread only: written by its snapshot, read by
// BeforeFree). A handful a packet: a linear scan.
class PinSet {
public:
    void Clear() { _items.clear(); }
    void Add(const void* p) {
        if (p == nullptr || Holds(p)) return;
        _items.push_back(p);
    }
    bool Holds(const void* p) const {
        for (const void* q : _items) {
            if (q == p) return true;
        }
        return false;
    }
    size_t Size() const { return _items.size(); }

private:
    std::vector<const void*> _items;
};

// What the pin waited for (main thread; per CSV window, render_pipe.h Counters).
struct PinStats {
    uint64_t checks   = 0;   // frees the seam saw (every ~EdgeMesh while installed)
    uint64_t waits    = 0;   //   of which were pinned by the packet in flight, its reads not over: waited
    uint64_t waitUs   = 0;   //   the time they waited, us
    uint64_t maxUs    = 0;   //   the longest
    uint64_t timeouts = 0;   //   of which gave up at the bound (the free ran beside the reads)
};

class PinGate {
public:
    // Main thread, render thread idle (Host::BeforePublish): the packet about to go holds
    // `set` (null: no copies, nothing pinned). Its reads are not over until it says so.
    void Publish(const PinSet* set) {
        std::lock_guard<std::mutex> lk(_m);
        _set  = set;
        _over = set == nullptr;
    }

    // The emitting thread: the packet's reads of its pins are over. Idempotent.
    void ReadsOver() {
        {
            std::lock_guard<std::mutex> lk(_m);
            if (_over) return;
            _over = true;
        }
        _cv.notify_all();
    }

    // Main thread, before freeing `p` (a step). Waits while `p` is pinned by the packet in
    // flight and its reads are not over, at most `boundUs`.
    void BeforeFree(const void* p, int64_t boundUs) {
        std::unique_lock<std::mutex> lk(_m);
        ++_stats.checks;
        if (_over || _set == nullptr || !_set->Holds(p)) return;
        ++_stats.waits;
        _waiting = true;
        const auto t0 = std::chrono::steady_clock::now();
        const auto limit = t0 + std::chrono::microseconds(boundUs);
        bool over = false;
        for (;;) {
            if (_cv.wait_for(lk, std::chrono::milliseconds(1), [this]() { return _over; })) {
                over = true;
                break;
            }
            if (std::chrono::steady_clock::now() >= limit) break;
            lk.unlock();
            Platform::DispatchSentMessages();
            lk.lock();
        }
        const int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - t0).count();
        const uint64_t waited = us > 0 ? static_cast<uint64_t>(us) : 0;
        _stats.waitUs += waited;
        if (waited > _stats.maxUs) _stats.maxUs = waited;
        if (!over) ++_stats.timeouts;
        _waiting = false;
    }

    // The emitting thread (SMOOTHREPLAY): a step's free is waiting for the reads to be over. The
    // replayed frames, which read the pins again, stop when they see it.
    bool FreeWaiting() {
        std::lock_guard<std::mutex> lk(_m);
        return _waiting;
    }

    // Main thread: the window's counts, reset.
    PinStats TakeStats() {
        std::lock_guard<std::mutex> lk(_m);
        const PinStats s = _stats;
        _stats = PinStats{};
        return s;
    }

    // Tests: whether a free of `p` would wait now.
    bool Pinned(const void* p) {
        std::lock_guard<std::mutex> lk(_m);
        return !_over && _set != nullptr && _set->Holds(p);
    }

private:
    std::mutex              _m;
    std::condition_variable _cv;
    const PinSet*           _set  = nullptr;   // the in-flight packet's (main thread writes it, render idle)
    bool                    _over = true;      // its reads are over (or nothing is pinned)
    bool                    _waiting = false;  // a BeforeFree is waiting for them
    PinStats                _stats;
};

// ---- the engine glue (resource_pin.cpp) ------------------------------------
// Install / remove Graphics::EdgeMesh::_extBeforeDestroy (Sync / Uninstall, render idle).
void InstallPin();
void RemovePin();
// Host::BeforePublish: the pins of the packet about to go (`overlap` false: none).
void PublishPins(bool overlap, int32_t slot);
// The emitting thread: the packet's reads of its pins are over.
void PinReadsOver();
// The emitting thread (SMOOTHREPLAY): a step's free waits for the reads; stop replaying.
bool PinFreeWaiting();
// The snapshot (main thread): slot `slot`'s set, cleared and refilled by each build.
PinSet& SlotPins(int32_t slot);
PinStats TakePinStats();

}  // namespace RenderPipe
}  // namespace Ext
