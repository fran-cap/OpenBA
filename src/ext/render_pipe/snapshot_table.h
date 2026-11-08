// ============================================================================
// src/ext/render_pipe/snapshot_table.h -- RENDERPIPE: the snapshot builder's pure parts
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/android/P13_OVERLAP.md section 2.2). Split out of
// snapshot.cpp so the unit test reaches them without the engine's geometry types
// (tests/unit/ext_render_pipe_snapshot_test.cpp).
//
//   PointerTable    an open-addressing table from a live pointer to an index, cleared by
//                   generation (the builder's geometries and instanced states)
//   WorldItemCopied which WorldGeometry draw items go to the arena copy: the decal / road
//                   modes; every other mode keeps the live object (P13_OVERLAP 2.2)
//   LastEntry       the lean builder's previous-item entry (issue #253): an item at the same
//                   geometry takes it without a table probe
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>   // std::hash<const void*>: no pointer casts
#include <vector>
#include "Game/WorldDrawKey.h"   // WorldDrawKey (WorldGeometry::DrawKey; standalone: <cstdint> only)

namespace Ext {
namespace RenderPipe {

// A WorldGeometry item with this draw key (RendererDrawItem::_node) draws from the copy.
inline bool WorldItemCopied(uint32_t key) {
    const uint32_t mode = key & WorldDrawKey::kModeMask;
    return mode == WorldDrawKey::kModeDecal || mode == WorldDrawKey::kModeRoad;
}

// An open-addressing table from a live pointer to an index, cleared by generation.
// `expected` only sizes it: an insert that would take the load past one half doubles it
// first (review F-218: a full table left a missing key probing forever).
class PointerTable {
public:
    void Reset(size_t expected) {
        size_t want = 64;
        while (want < expected * 2) want *= 2;
        if (_slots.size() < want) {
            _slots.assign(want, Slot{});
            _gen = 0;
        }
        ++_gen;
        if (_gen == 0) {   // wrapped: clear once
            for (Slot& s : _slots) s.gen = 0;
            _gen = 1;
        }
        _count = 0;
    }
    // The index stored for `key`, or -1; `insert` >= 0 stores it when absent.
    int32_t Find(const void* key, int32_t insert) {
        const size_t mask = _slots.size() - 1;
        size_t i = Mix(key) & mask;
        for (;;) {
            Slot& s = _slots[i];
            if (s.gen != _gen) {
                if (insert >= 0) {
                    if ((_count + 1) * 2 > _slots.size()) {
                        Grow();
                        return Find(key, insert);
                    }
                    s.gen   = _gen;
                    s.key   = key;
                    s.index = insert;
                    ++_count;
                }
                return -1;
            }
            if (s.key == key) return s.index;
            i = (i + 1) & mask;
        }
    }
    size_t Capacity() const { return _slots.size(); }
    // A hint (issue #253): the line `key`'s probe starts on. always_inline: GCC classes an
    // out-of-line helper whose only effect is a prefetch as pure and deletes its call (cull.cpp).
    [[gnu::always_inline]] inline void Prefetch(const void* key) const {
        if (_slots.empty()) return;
        __builtin_prefetch(&_slots[Mix(key) & (_slots.size() - 1)]);
    }

private:
    // Twice the slots, this generation's keys placed again (the others dropped).
    void Grow() {
        std::vector<Slot> old;
        old.swap(_slots);
        _slots.assign(old.size() * 2, Slot{});
        const size_t mask = _slots.size() - 1;
        for (const Slot& o : old) {
            if (o.gen != _gen) continue;
            size_t i = Mix(o.key) & mask;
            while (_slots[i].gen == _gen) i = (i + 1) & mask;
            _slots[i] = o;
        }
    }
    static size_t Mix(const void* p) {
        const uint64_t h = static_cast<uint64_t>(std::hash<const void*>{}(p));
        return static_cast<size_t>((h >> 4) * 0x9E3779B97F4A7C15ull >> 16);
    }
    struct Slot {
        const void* key   = nullptr;
        int32_t     index = 0;
        uint32_t    gen   = 0;
    };
    std::vector<Slot> _slots;
    uint32_t          _gen   = 0;
    size_t            _count = 0;   // keys of this generation
};

// Issue #253: the lean builder's route for one draw item, recorded in pass A and replayed by
// pass C in the same walk order (no second table probe). kKeepLive: the item keeps its live
// geometry (a WorldGeometry mode other than decal / road).
constexpr int32_t kKeepLive = -1;

// The entry of the item walked last, when the next item may reuse it without a probe: only an
// entry pass A would take as it stands (not the WorldGeometry, whose items each mark a batch).
struct LastEntry {
    const void* key   = nullptr;
    int32_t     index = kKeepLive;

    bool Hit(const void* k) const { return k != nullptr && k == key; }
    void Set(const void* k, int32_t i) {
        key   = k;
        index = i;
    }
    void Clear() { Set(nullptr, kKeepLive); }
};

}  // namespace RenderPipe
}  // namespace Ext
