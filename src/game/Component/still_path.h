// src/game/Component/still_path.h -- STILLPATH (OFFSCREEN design E1): the exact "standing
// still" fast path of the character move tick (CharacterComponent_MoveTick_1094c0,
// citizenai_executor.cpp). docs/extensions/handins/STILLPATH.md is the proof.
//
// Faithful perf work, not an extension: no registry row, no settings key, no flag. The
// move tick's TAIL (the transform update after the action-stack executor) is skipped on an
// intermediate speed iteration (MeshInstance::_rootOnlyUpdates set) when, AT THE POINT THE
// TAIL WOULD WRITE, every value it is about to write is bit-identical to what is already
// there:
//   translation  speed (|root offset| x +0x38, computed by the tail itself) is not > 0, and
//                the z the tail writes (desc +0x40 when desc +0x3c is set, else the z it
//                read) has the bits of the live z;
//   yaw          fVar14 + desc +0x24 (the tail's own heading result) has the bits of the
//                live yaw;
//   rotation     the live rotation is (+0, +0, -sin, cos) of that yaw as the tail's trig
//                computes it -- read from a MEMO of the trig, never recomputed: the memo
//                maps yaw bits to the trig's result bits, so any entry is a true fact
//                whoever wrote it and whenever (a pure function memo, no invalidation).
// The executor always runs in full before this (its pops, prechecks and flips are
// retail's); the two remaining effects of the tail, the Geometry push (when
// ext.render_culling is off) and CharacterGrid::Place, are performed by the fast path
// with the same arguments. Header-only and free of game types so
// tests/unit/still_path_test.cpp checks the same code the tick runs.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace StillPath {

// The kill switch (like kTreeSnapshot): false = the tail always runs in full, the memo is
// never written, and the counter stays 0. Shipped value = shipped code.
static constexpr bool kEnabled = true;

// Fast-path ticks taken (or, under BANISHED_STRESS_COMPPROF=4, the ticks the shadow
// checked instead). Main thread only. Defined in citizenai_executor.cpp; read by the
// [COMPPROF] /char rows (stress_harness.cpp).
extern long long g_fastTicks;

inline uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof b);
    return b;
}

// One trig result: the tail builds rotation (+0, +0, -sin(a), cos(a)) with
// a = yaw * pi/180 * 0.5 from MsvcLibm::Sinf / Cosf, a pure function of the yaw.
struct Memo {
    uint32_t yaw = 0;      // the yaw the tail wrote (its trig input before the constants)
    uint32_t negSin = 0;   // rotation.z it wrote
    uint32_t cos = 0;      // rotation.w it wrote
    uint32_t valid = 0;    // 0 = never written (a zero entry is NOT R(+0): cos(0) = 1)
};

// One entry per CharacterGrid index (self+0x40): dense and walk-local. Entries are never
// invalidated: an index reused by another character, a stale entry, a teleported model --
// each can only make the check fail (decline), never pass wrongly, because the check
// compares the LIVE yaw and rotation against an entry that is (y, R(y)) for its own y.
class MemoTable {
public:
    const Memo* Find(int idx) const {
        return static_cast<size_t>(static_cast<unsigned>(idx)) < m_.size() ? &m_[static_cast<size_t>(idx)] : nullptr;
    }
    void Put(int idx, float yaw, float negSin, float cos) {
        if (idx < 0) return;
        const size_t i = static_cast<size_t>(idx);
        if (i >= m_.size()) m_.resize(i + 256);
        Memo& e = m_[i];
        e.yaw = Bits(yaw); e.negSin = Bits(negSin); e.cos = Bits(cos); e.valid = 1;
    }
    size_t Size() const { return m_.size(); }
private:
    std::vector<Memo> m_;
};

// The z the tail writes on an intermediate iteration with no step: the terrain block and
// the 0x0b test run only on the last iteration, so pos.z is desc +0x40 when desc +0x3c is
// set, else the z it read.
inline float ZOut(bool descB3c, float descF40, float z) { return descB3c ? descF40 : z; }

// The decision, on values the tail has already computed (yawOut = fVar14 + desc +0x24,
// zOut = ZOut(...)) and the live model state (rotation x y z w, translation z, yaw).
// Translation x / y are not passed: with no step the tail writes back the x / y it read.
inline bool IsFixedPoint(float rx, float ry, float rz, float rw, float z, float yaw,
                         float yawOut, float zOut, const Memo* m) {
    if (m == nullptr || m->valid == 0) return false;
    const uint32_t y = Bits(yawOut);
    return y == Bits(yaw) && Bits(zOut) == Bits(z) && m->yaw == y &&
           Bits(rx) == 0u && Bits(ry) == 0u && Bits(rz) == m->negSin && Bits(rw) == m->cos;
}

// The shadow's comparison (BANISHED_STRESS_COMPPROF=4): what the full tail left vs what
// the fast path claimed (the state before the tail). Bit 0 translation, 1 rotation, 2 yaw;
// 0 = identical.
inline long long ShadowDiff(const uint32_t before[8], const uint32_t after[8]) {
    long long d = 0;
    for (int i = 0; i < 4; ++i) if (before[i] != after[i]) d |= 2;   // rotation x y z w
    for (int i = 4; i < 7; ++i) if (before[i] != after[i]) d |= 1;   // translation x y z
    if (before[7] != after[7]) d |= 4;                                // yaw
    return d;
}

}  // namespace StillPath
