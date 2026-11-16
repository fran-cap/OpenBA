#pragma once
// ============================================================================
// src/engine/graphics/skin_memo.h -- SKINMEMO: bone matrices computed once per
// key per Renderer::Draw
// ============================================================================
//
// NOT RETAIL. Faithful perf work (docs/extensions/handins/SKINMEMO.md). The
// same `SetBones` calls in the same order with the same bytes, bit for bit.
//
// `MeshInstance::GetBones` (retail 0x180100070) is a pure function of:
//   * the pose array it picks (`interp`: the instance's `_nodes` or, when
//     `_animation._instanced`, the shared AnimationManager state the last
//     layer's handle names) and the first `count` records in it,
//   * the mesh's node array (`count` = its size, and each node's `_bind`),
//   * `_animation._instanced` (which picks an IDENTITY root and ignores the
//     caller's root), the caller's root Transform when not instanced, and `t`.
// It writes only `out[0 .. count)`. Inside one `Renderer::Draw`, `t` is
// `Renderer::_alphaT` for every draw, and nothing writes a pose array, a mesh's
// node array or the AnimationManager (census: hand-in section 1.3). Yet the
// renderer asks for the same bones several times: once per triangle list,
// once per pass (the sun's shadow map, then DrawLight per light and list), and
// once per instanced batch of trees that share one baked state.
//
// The memo: a fixed slot table keyed on every input above BY IDENTITY AND BITS
// (pointers to the two arrays, count, instanced, root bits, t bits), each slot
// holding its key's `count` matrices. It is valid only inside one scope
// (`BeginScope` / `EndScope` around `Renderer::Draw`): both bump the epoch, so
// no entry outlives the draw it was computed in. Outside a scope every call is
// retail's path. A key never stands for different bytes inside a scope because
// no input is written there; that claim is the census, and the COMPPROF=4
// shadow (`SkinBones`) recomputes every hit with the faithful body.
//
// The table is header-only and free of game types, so the unit test
// (tests/unit/skin_memo_test.cpp) drives them with a fake producer.
// ============================================================================

#include <cstdint>
#include <cstring>
#include <functional>   // std::hash<const void*>
#include <vector>

#include "math/Math.h"

namespace Graphics { class MeshInstance; }

namespace SkinMemo {

// The kill switch. false = every skinned draw runs retail's path (the zeroed
// stack buffer, GetBones, SetBones).
inline constexpr bool kEnabled = true;

// Everything GetBones reads, by identity and bits. 56 bytes, no padding.
struct Key {
    const void* interp;      // the pose array GetBones picks
    const void* binds;       // mesh->GetNodes()._data
    int32_t     count;       // mesh->GetNodes()._size
    uint32_t    instanced;   // _animation._instanced (0 / 1)
    uint32_t    t;           // the float's bits
    uint32_t    root[7];     // the root Transform's bits; all 0 when instanced (ignored there)
};
static_assert(sizeof(Key) == 56, "SkinMemo::Key has no padding");

inline uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

inline bool SameKey(const Key& a, const Key& b) { return std::memcmp(&a, &b, sizeof(Key)) == 0; }

inline uint64_t HashKey(const Key& k) {
    const std::hash<const void*> ph;   // the pointer's value, with no cast
    uint64_t h = uint64_t{ ph(k.interp) } * 0x9E3779B97F4A7C15ull;
    h ^= uint64_t{ ph(k.binds) } + 0x7F4A7C159E3779B9ull + (h << 6) + (h >> 2);
    h ^= uint64_t{ k.t } * 0xBF58476D1CE4E5B9ull;
    for (int i = 0; i < 7; ++i) h = (h ^ k.root[i]) * 0x94D049BB133111EBull;
    return h ^ (h >> 29);
}

// A fixed, direct-mapped slot table: slot = hash & (kSlots - 1), each slot one
// stored key, its epoch and a fixed region of kMaxBones matrices. A slot is
// live only when its epoch is the current one, so a scope change empties the
// table with no clear. A different key hashing to a live slot EVICTS it (the
// next request for the evicted key recomputes; counted). Bounded: kSlots x
// (kMaxBones x 64 + 64) bytes, allocated on first use; only `count` matrices
// of a slot are ever written.
inline constexpr int32_t kSlots    = 4096;   // power of two
inline constexpr int32_t kMaxBones = 48;     // the larger caller buffer (Draw / EdgeMeshInstance::Draw)

class Table {
public:
    struct Slot { Key key; uint64_t epoch; };

    // The slot for hash `h`; Live() says whether it holds `k` in `epoch`.
    int32_t SlotOf(uint64_t h) const { return (int32_t)(h & (uint64_t)(kSlots - 1)); }
    bool Live(int32_t s, const Key& k, uint64_t epoch) const {
        return !_slots.empty() && _slots[(size_t)s].epoch == epoch && SameKey(_slots[(size_t)s].key, k);
    }
    // True when filling slot `s` in `epoch` evicts another live key.
    bool Occupied(int32_t s, uint64_t epoch) const {
        return !_slots.empty() && _slots[(size_t)s].epoch == epoch;
    }
    // Claim slot `s` for `k`; returns its matrix region (kMaxBones matrices).
    Math::Matrix* Claim(int32_t s, const Key& k, uint64_t epoch) {
        if (_slots.empty()) {
            _slots.assign((size_t)kSlots, Slot{ Key{}, 0 });
            _bones.resize((size_t)kSlots * (size_t)kMaxBones);
        }
        _slots[(size_t)s] = Slot{ k, epoch };
        return Region(s);
    }
    Math::Matrix* Region(int32_t s) { return _bones.data() + (size_t)s * (size_t)kMaxBones; }

private:
    std::vector<Slot> _slots;
    std::vector<Math::Matrix> _bones;
};

// The scope state (main thread only: every Renderer::Draw runs on the thread
// that owns the Video::Context).
inline uint64_t g_epoch = 1;   // bumped at every BeginScope and EndScope; 0 marks an empty slot
inline int      g_depth = 0;   // > 0 inside a Renderer::Draw
inline uint64_t g_scopes = 0;  // Renderer::Draw scopes opened
inline Table    g_table;

inline void BeginScope() { ++g_depth; ++g_epoch; ++g_scopes; }
inline void EndScope()   { --g_depth; ++g_epoch; }

// The RAII form Renderer::Draw holds for its whole body.
struct Scope {
    Scope()  { if constexpr (kEnabled) BeginScope(); }
    ~Scope() { if constexpr (kEnabled) EndScope(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// ---- counters (main thread; always counted, cheap) and the profile ---------
inline uint64_t g_calls = 0;           // skinned bone requests inside a scope
inline uint64_t g_callsInstanced = 0;
inline uint64_t g_hits = 0;
inline uint64_t g_hitsInstanced = 0;
inline uint64_t g_misses = 0;          // = distinct keys per scope, summed
inline uint64_t g_missesInstanced = 0; // distinct shared (instanced) keys
inline uint64_t g_declines = 0;        // outside a scope, count <= 0 or above the caller's buffer
inline uint64_t g_evictions = 0;       // a miss that overwrote another live key's slot
inline uint64_t g_bonesComputed = 0;
inline uint64_t g_bonesSaved = 0;
// Candidate 2's census (only under g_prof, unique misses): pose arrays whose every
// node has `_previous` bit-identical to `_current` (NOT exploitable: section 2.2).
inline uint64_t g_stillUnique = 0;
// RDTSC ticks, only under g_prof.
inline uint64_t g_missTicks = 0;
inline uint64_t g_hitTicks = 0;

// Set by the stress harness (src/diag/stress_harness.cpp): g_prof at
// BANISHED_STRESS_COMPPROF >= 1, g_verify + g_report at 4. Never otherwise; the
// engine does not include the harness.
inline int  g_prof = 0;
inline int  g_verify = 0;
inline void (*g_report)(bool ok, long long detail) = nullptr;

// The memo's entry. Returns the matrices to hand to SetBones and their count, or
// nullptr to decline (the caller then runs retail's arm). `capacity` is the
// caller's stack buffer: a count above it overflows that buffer in retail, so
// it declines and keeps retail's behaviour.
const Math::Matrix* Bones(const Graphics::MeshInstance& src, float t,
                          const Math::Transform& root, int32_t capacity, int32_t* countOut);

}  // namespace SkinMemo
