#pragma once
// ============================================================================
// src/engine/graphics/xform_memo.h -- XFORMMEMO: instance transforms computed
// once per key per Renderer::Draw (issue #253)
// ============================================================================
//
// NOT RETAIL. Faithful perf work, SkinMemo's pattern (skin_memo.h) applied to
// Geometry slot 6. The same `SetTransform` calls in the same order with the
// same bytes, bit for bit.
//
// `MeshInstance::GetInstanceTransform` (retail 0x180101280, mesh.cpp) is a
// pure function of the instance (its transform pair, its `_nodes` or shared
// instanced pose, its mesh's triangle-list node index and its `_color`), the
// packed (level, list) argument and `t`. Inside one `Renderer::Draw`, `t` is
// `Renderer::_alphaT` for every draw and nothing writes those inputs (SkinMemo's
// census; under ext.render_pipe the packet's snapshot freezes the copies the
// render thread draws). Yet the instancing loop of DrawItemList (and its
// ext.draw_batch copy) asks for the same transform once per pass: the sun's
// shadow map, the main lists, each light, and the reflection when its level
// matches.
//
// The memo: a slot table (home slot plus a short linear probe) keyed on (Geometry*, packed, t bits),
// each slot holding the 64-byte matrix the call returned. It shares SkinMemo's
// scope and epoch (`SkinMemo::Scope` around `Renderer::Draw`, or around pass 2
// under ext.render_pipe), so no entry outlives the draw it was computed in.
// Only an exact `MeshInstance` is memoised: EdgeMeshInstance, particles and
// every other Geometry keep their own virtual call. The COMPPROF=4 shadow
// (`XformMemo`) recomputes every hit with the virtual call and compares.
//
// The table is header-only and free of game types, so the unit test
// (tests/unit/xform_memo_test.cpp) drives it with a fake producer.
// ============================================================================

#include <cstdint>
#include <cstring>
#include <functional>   // std::hash<const void*>
#include <vector>

#include "math/Math.h"

namespace Graphics { class Geometry; }

namespace XformMemo {

// The kill switch. false = every instance transform is the virtual call.
inline constexpr bool kEnabled = true;

inline uint32_t Bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

inline uint64_t HashKey(const void* geometry, int32_t packed, uint32_t t) {
    const std::hash<const void*> ph;   // the pointer's value, with no cast
    uint64_t h = uint64_t{ ph(geometry) } * 0x9E3779B97F4A7C15ull;
    h ^= uint64_t{ static_cast<uint32_t>(packed) } * 0xBF58476D1CE4E5B9ull;
    h ^= uint64_t{ t } * 0x94D049BB133111EBull;
    h ^= h >> 31;
    return h * 0x9E3779B97F4A7C15ull;
}

// A fixed slot table with a short linear probe: a key's HOME slot is the
// hash's top kSlotBits bits and it lives in one of the kProbe slots from there
// (wrapping), each slot one key, its epoch and its matrix. A slot is live only
// when its epoch is the current one, so a scope change empties the table with
// no clear. Nothing is removed inside an epoch, so a live key always sits ahead
// of the first non-live slot of its window and a lookup stops there. A miss
// fills that first non-live slot; only when all kProbe are live does it
// EVICT the home slot's key (the next request for the evicted key recomputes;
// counted). kSlots x 88 bytes, allocated on first use.
//
// Sized to hold a far-zoom forest's distinct keys per Renderer::Draw with few
// evictions (the measurements: docs/android/I253_RENDER.md).
inline constexpr int32_t kSlotBits = 15;
inline constexpr int32_t kSlots    = 1 << kSlotBits;
inline constexpr int32_t kProbe    = 8;

class Table {
public:
    struct Slot {
        Math::Matrix m;
        const void*  geometry;
        int32_t      packed;
        uint32_t     t;
        uint64_t     epoch;   // 0 = never filled
    };

    int32_t SlotOf(uint64_t h) const { return static_cast<int32_t>(h >> (64 - kSlotBits)); }

    // The stored matrix when the key is live in `epoch` in the probe window of
    // `home`, else nullptr with `*place` = the slot a miss claims (the window's
    // first slot not live in `epoch`, or `home` when all kProbe are live) and
    // `*evicts` = whether claiming it drops another live key.
    const Math::Matrix* Find(int32_t home, const void* geometry, int32_t packed, uint32_t t,
                             uint64_t epoch, int32_t* place, bool* evicts) const {
        *place = home;
        *evicts = false;
        if (_slots.empty()) return nullptr;
        for (int32_t k = 0; k < kProbe; ++k) {
            const int32_t s = (home + k) & (kSlots - 1);
            const Slot& slot = _slots[static_cast<size_t>(s)];
            if (slot.epoch != epoch) {
                *place = s;
                return nullptr;
            }
            if (slot.geometry == geometry && slot.packed == packed && slot.t == t) return &slot.m;
        }
        *evicts = true;
        return nullptr;
    }
    // Claim slot `s` for the key; returns its matrix for the producer to fill.
    Math::Matrix* Claim(int32_t s, const void* geometry, int32_t packed, uint32_t t, uint64_t epoch) {
        if (_slots.empty()) {
            _slots.assign(static_cast<size_t>(kSlots), Slot{});
        }
        Slot& slot = _slots[static_cast<size_t>(s)];
        slot.geometry = geometry;
        slot.packed = packed;
        slot.t = t;
        slot.epoch = epoch;
        return &slot.m;
    }

private:
    std::vector<Slot> _slots;
};

inline Table g_table;

// ---- counters (the drawing thread; always counted, cheap) -------------------
inline uint64_t g_calls = 0;       // MeshInstance transform requests inside a scope
inline uint64_t g_hits = 0;
inline uint64_t g_misses = 0;      // = distinct keys per scope, summed (less evictions' repeats)
inline uint64_t g_evictions = 0;   // a miss that overwrote another live key's slot
inline uint64_t g_declines = 0;    // outside a scope, or not an exact MeshInstance

// Set by the stress harness (src/diag/stress_harness.cpp) at
// BANISHED_STRESS_COMPPROF=4. Never otherwise; the engine does not include the harness.
inline int  g_verify = 0;
inline void (*g_report)(bool ok, long long detail) = nullptr;

// The memo's entry, in place of `geometry.GetInstanceTransform(scratch, packed, t)`.
// Returns the matrix to hand to SetTransform: a slot's on a hit or a miss, or
// `scratch` (filled by the virtual call) when it declines or verifies. The
// pointer is valid until the next call.
const Math::Matrix* Transform(const Graphics::Geometry& geometry, int32_t packed, float t,
                              Math::Matrix* scratch);

}  // namespace XformMemo
