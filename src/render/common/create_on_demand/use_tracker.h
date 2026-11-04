#pragma once
// ============================================================================
// src/render/common/create_on_demand/use_tracker.h -- ext.create_on_demand: when a made object was
// last used, and a gate that keeps an eviction away from every use (docs/extensions/handins/
// CREATEONDEMAND.md section 7, issue #13)
// ============================================================================
// NOT RETAIL. Backend-neutral (unit-tested: tests/unit/ext_create_on_demand_test.cpp).
//
// UseStamps: a fixed open-addressing table, object -> the epoch (minute) of its last use. Only the
// objects the extension made from a Locator are in it (Insert / Remove under the deferred-create
// table's mutex); Touch, on every use, probes it with no lock: a miss (an object never deferred)
// stops at the first empty slot, a hit stores the epoch only when it changed (no store per use).
// A probe never runs further than kMaxProbe slots: an object that does not fit is simply never
// tracked, so never given back.
//
// UseGate: every slot that reads a companion's handles (Bind, Lock, SetData, Update, Read, the save
// arm) runs inside Enter / Leave; Close waits until no use is in progress and holds new ones back
// until Open (or gives up after a bound, reopened: a use can wait inside for a lock). An eviction (Release + null of an object's handles) runs only between the two, so no
// use can read a handle that is being given back. Enter is two atomic operations on one line when
// open; it is not re-entrant (a slot never calls another slot of a companion).
// ============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

namespace CreateOnDemand {

class UseStamps {
public:
    static constexpr uint32_t kSlots    = 1u << 18;
    static constexpr uint32_t kMaxProbe = 64;

    UseStamps();

    // Under the owner's mutex. Insert: false when the object did not fit (it is then never tracked).
    bool Insert(const void* obj, uint32_t epoch);
    void Remove(const void* obj);
    // The epoch of obj's last use; false when obj is not tracked. Under the owner's mutex.
    bool LastUse(const void* obj, uint32_t* epoch) const;

    // Any thread, no lock.
    void Touch(const void* obj, uint32_t epoch) {
        const uintptr_t key = reinterpret_cast<uintptr_t>(obj);   // CAST-OK: a pointer used as a hash key
        uint32_t i = Home(key);
        for (uint32_t n = 0; n < kMaxProbe; ++n) {
            const uintptr_t k = _slots[i].key.load(std::memory_order_acquire);
            if (k == key) {
                if (_slots[i].epoch.load(std::memory_order_relaxed) != epoch)
                    _slots[i].epoch.store(epoch, std::memory_order_relaxed);
                return;
            }
            if (k == kEmpty) return;
            i = (i + 1) & (kSlots - 1);
        }
    }

private:
    static constexpr uintptr_t kEmpty = 0;
    static constexpr uintptr_t kTomb  = 1;
    struct Slot {
        std::atomic<uintptr_t> key{kEmpty};
        std::atomic<uint32_t>  epoch{0};
    };
    static uint32_t Home(uintptr_t key) {
        return static_cast<uint32_t>(((key >> 4) * 0x9E3779B97F4A7C15ull) >> (64 - 18));
    }
    std::unique_ptr<Slot[]> _slots;
};

class UseGate {
public:
    void Enter() {
        for (;;) {
            _users.fetch_add(1, std::memory_order_seq_cst);
            if (!_closed.load(std::memory_order_seq_cst)) return;
            _users.fetch_sub(1, std::memory_order_seq_cst);
            while (_closed.load(std::memory_order_seq_cst)) std::this_thread::yield();
        }
    }
    void Leave() {
        _users.fetch_sub(1, std::memory_order_seq_cst);
    }
    // One closer at a time (the sweep). True once no use is in progress (the gate stays closed until
    // Open). A use may wait inside the gate for a lock whose holder is itself waiting to Enter (the
    // loader's SetData for Video::Device::Lock, held by the frame thread binding): so the closer gives
    // up after `limit`, reopens the gate and returns false (review F-133: a permanent deadlock before).
    bool Close(std::chrono::nanoseconds limit) {
        _closed.store(true, std::memory_order_seq_cst);
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (_users.load(std::memory_order_seq_cst) != 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                Open();
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }
    void Open() {
        _closed.store(false, std::memory_order_seq_cst);
    }

private:
    std::atomic<int>  _users{0};
    std::atomic<bool> _closed{false};
};

}  // namespace CreateOnDemand
