#pragma once
// ============================================================================
// src/game/Entity/tick_list.h -- the slot-8 component walk of
// ComponentEntity::TickComponents (0x1400c2de0), retail and cached.
//
// RETAIL (0x1400c2de0, read for B4 WORK-a; the comments below are its body):
//   if ((entity+0x40 & 0x4) && !(entity+0x40 >> 10 & 1))
//     n = entity+0x38 (read once)
//     for (ci = 0; ci < n; ++ci):
//       if (comp[ci]+0x20 >> 2 & 1)          // component flag bit 2
//         call comp[ci]->vtable[+0x40](comp, time)
//       break if entity deleted (entity+0x40 & 0x400)
// `Retail` / `RetailFrom` below are that body; TickComponents (entitylist.cpp) is
// `Retail` with the slot-8 call as `tick`.
//
// [PerfX WALKCACHE -- NON-RETAIL side table, IDENTICAL ticks]
// Most entities carry one or two components with bit 2 among five to twelve, and
// the retail loop loads every component object only to test that bit. `Entry`
// remembers which indices had bit 2 when it was built; `Cached` visits only those.
// Why that observes exactly what retail observes (docs/extensions/handins/WALKCACHE.md):
//   * every write of a component's +0x20 low word outside a constructor is
//     followed by ComponentEntity::RefreshEntityFlags (retail's `old ^ new & 0xffff`
//     idiom; every site is listed in WALKCACHE.md section 2), and RefreshEntityFlags marks the entity's entry stale
//     (EntityList_TickListInvalidate). So between a build and a visit, a component
//     that was not a candidate still has bit 2 clear: retail would read it and
//     skip it, and skipping the read changes nothing (Get() is a load, CheckPtr a RET);
//   * a candidate's bit is still read live, so a clear without a Refresh (none
//     exists) would still skip it, as retail does;
//   * no code runs between two candidates, so the deleted bit (0x400) and the
//     entry's state can only change inside a tick: both are re-read after every
//     tick. A tick that stales the entry hands the rest of the loop (ci + 1 ..
//     n - 1, the count read at entry) to RetailFrom, which is retail's loop;
//   * the component array (data, count) is part of the key: a mismatch is stale.
//     (The array changes only in the ComponentEntity ctors / Persist / dtor.)
// The entry is (re)built AFTER a retail pass, from the flags the next pass starts
// from. Build never dereferences a slot retail's loop would not have just read; a
// null slot (a standalone factory miss) makes the entry `kWide` (always retail).
//
// Templated on the entity type so the host unit tests can drive it with plain
// structs (tests/unit/tick_list_test.cpp); the game instantiates it on
// ComponentEntity. Members used: _entityFlags, _components{_data, _size, [int]},
// slot.Get(), component->_flags.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <functional>   // std::less: the pointer order
#include "Component/ComponentFlags.h"   // ComponentFlags: the entity and component bits

namespace TickList {

constexpr int kMaxIdx = 4;   // candidates per entity; more -> kWide (always retail)

enum : uint8_t {
    kStale  = 0,   // build at the next visit (after a retail pass)
    kCached = 1,   // idx[0..count) are the components that had bit 2 at build
    kWide   = 2,   // more than kMaxIdx candidates, > 255 components or a null slot
};

// One per snapshot entity, parallel to the walk snapshot (16 bytes).
struct Entry {
    const void* data;    // _components._data at build
    uint16_t    size;    // _components._size at build (<= 255 when kCached)
    uint8_t     state;
    uint8_t     count;
    uint8_t     idx[kMaxIdx];   // ascending
};
static_assert(sizeof(Entry) == 16, "TickList::Entry is 16 bytes");

// Retail's loop from component `ci` on; `compCount` is the count read at entry
// (retail never re-reads it), the slot is re-read every pass (the array data may
// move under a tick).
template <class Ent, class TickFn>
inline void RetailFrom(Ent* e, int32_t ci, int32_t compCount, TickFn& tick) {
    while (ci < compCount) {
        // Read component IBP and CheckPtr
        auto* c = e->_components[ci].Get();
        // Component flags at byte offset +0x20 from component base.
        // Verified from disassembly at 0x1400c2e42: MOV ECX,dword ptr [RAX + 0x20]
        // Then SHR ECX,0x2; TEST CL,0x1 checks bit 2.
        if (c->_flags & Bits(ComponentFlags::Tick)) {
            // CheckPtr again before vtable call
            c = e->_components[ci].Get();
            tick(c);   // component slot 8 (+0x40) Tick
        }
        ci++;
        // Break if entity was deleted during component processing
        if (e->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) break;
    }
}

// The whole retail body (0x1400c2de0).
template <class Ent, class TickFn>
inline void Retail(Ent* e, TickFn& tick) {
    const uint32_t entityFlags = e->_entityFlags;
    // Check entity has "needs tree tick" flag (bit 2) and is not deleted (bit 10)
    if ((entityFlags & Bits(ComponentFlags::Tick)) == 0) return;
    if (entityFlags & Bits(ComponentFlags::MarkedForRemoval)) return;
    const int32_t compCount = e->_components._size;
    if (compCount <= 0) return;
    RetailFrom(e, 0, compCount, tick);
}

// Record which components carry bit 2 now.
template <class Ent>
inline void Build(Entry& en, const Ent* e) {
    const int32_t n = e->_components._size;
    en.data  = e->_components._data;
    en.size  = static_cast<uint16_t>(n);
    en.count = 0;
    if (n < 0 || n > 255) { en.state = kWide; return; }
    for (int32_t i = 0; i < n; ++i) {
        const auto* c = e->_components[i].Get();
        if (c == nullptr) { en.state = kWide; return; }   // not a guard: Build adds no deref retail lacks
        if (c->_flags & Bits(ComponentFlags::Tick)) {
            if (en.count == kMaxIdx) { en.state = kWide; return; }
            en.idx[en.count++] = static_cast<uint8_t>(i);
        }
    }
    en.state = kCached;
}

inline bool KeyMatches(const Entry& en, const void* data, int32_t n) {
    return en.data == data && static_cast<int32_t>(en.size) == n;
}

// TickComponents through the entry. V = the COMPPROF=4 shadow: every component the
// cached walk skips is checked to have bit 2 clear at the moment retail would have
// read it, and the visit is reported once through `report(ok, detail)` (detail =
// the first offending component index, or -1).
template <bool V, class Ent, class TickFn, class ReportFn>
inline void Cached(Ent* e, Entry& en, TickFn& tick, ReportFn& report) {
    const uint32_t entityFlags = e->_entityFlags;
    if ((entityFlags & Bits(ComponentFlags::Tick)) == 0) return;
    if (entityFlags & Bits(ComponentFlags::MarkedForRemoval)) return;
    const int32_t compCount = e->_components._size;
    if (compCount <= 0) return;

    if (en.state != kCached || !KeyMatches(en, e->_components._data, compCount)) {
        const bool wide = en.state == kWide && KeyMatches(en, e->_components._data, compCount);
        RetailFrom(e, 0, compCount, tick);
        if (!wide && (e->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) == 0) Build(en, e);
        return;
    }

    bool ok = true;
    int64_t bad = -1;
    int32_t next = 0;   // V: the first component not yet accounted for
    auto checkSkipped = [&](int32_t from, int32_t to) {   // [from, to) skipped
        for (int32_t s = from; s < to; ++s) {
            const auto* c = e->_components[s].Get();
            if (c->_flags & Bits(ComponentFlags::Tick)) { if (ok) bad = s; ok = false; }
        }
    };

    for (int k = 0; k < en.count; ++k) {
        const int32_t ci = en.idx[k];
        if (V) checkSkipped(next, ci);
        auto* c = e->_components[ci].Get();
        if (c->_flags & Bits(ComponentFlags::Tick)) {
            c = e->_components[ci].Get();
            tick(c);
            if (e->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) { if (V) report(ok, bad); return; }
            if (en.state != kCached || !KeyMatches(en, e->_components._data, e->_components._size)) {
                // A Refresh on this entity inside the tick: retail's loop takes over.
                RetailFrom(e, ci + 1, compCount, tick);
                if ((e->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) == 0) Build(en, e);
                if (V) report(ok, bad);
                return;
            }
        }
        next = ci + 1;
    }
    if (V) { checkSkipped(next, compCount); report(ok, bad); }
}

// Index of `e` in a strictly ascending pointer array (the update tree's in-order
// walk is ordered by its key, the entity pointer), or -1. std::less is the total
// pointer order.
template <class Ent>
inline int64_t IndexOf(Ent* const* arr, size_t n, const Ent* e) {
    const std::less<const Ent*> lt;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (lt(arr[mid], e)) lo = mid + 1;
        else hi = mid;
    }
    return (lo < n && arr[lo] == e) ? static_cast<int64_t>(lo) : -1;
}

template <class Ent>
inline bool StrictlyAscending(Ent* const* arr, size_t n) {
    const std::less<const Ent*> lt;
    for (size_t i = 1; i < n; ++i)
        if (!lt(arr[i - 1], arr[i])) return false;
    return true;
}

}  // namespace TickList
