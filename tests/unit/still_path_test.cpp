// ============================================================================
// tests/unit/still_path_test.cpp -- STILLPATH: the exact still fast path of the move tick
// ============================================================================
// src/game/Component/still_path.h decides, at the point the move tick's tail would write,
// whether every value it is about to write already has the same bits. These tests check
// the decision and the trig memo directly, then run two copies of one scripted world: one
// always runs the tail in full (a literal copy of the tail's intermediate-iteration
// arithmetic, citizenai_executor.cpp; the production text between its [COPY-PINNED]
// markers is hashed by copy_drift_test.cpp, so a change there fails until this copy is
// re-synced), the other takes the fast path exactly as the tick
// does (same order: heading and speed computed, decision, memo written on a decline). The
// states must be bit-identical after every iteration, whatever the script does between
// iterations (turns, steps, b3c heights, external transform writes, index reuse).
// ============================================================================
#include "unit.h"
#include "Component/still_path.h"
#include "char_prof.h"
#include "Game/exe_constants.h"
#include "crt/msvc_libm.h"   // the tail's trig, the ported CRT sinf / cosf

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using StillPath::Bits;

float FromBits(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }

// --- a literal copy of the tail on an intermediate iteration (rootOnly set) ---------------
struct Model { float rot[4]; float pos[3]; float yaw; };          // model +0x148 / +0x158 / +0x168
struct Desc  { float f24, slowSpeed; bool b3c; float f40; };      // CharacterDescription
struct Char  { int index; float f3C, f38; float dir[3]; float root[3]; Desc* desc; Model m;
               long long pushes = 0, places = 0; };                 // the two calls the fast path keeps


float Heading(const Char& ch) {   // fVar14, the tail's heading recurrence
    float fVar14 = ch.f3C;
    const float fVar10 = ch.m.yaw - ch.desc->f24;
    float fVar13 = fVar14 - fVar10;
    const float c180 = kRdata_140172220, c360 = FromBits(kRdata_140172238);
    const float c01 = FromBits(kRdata_1401720e4), cm180 = FromBits(kRdata_1401722a0);
    const float uv = kRdata_14017214c;
    const float a13 = fVar13 < 0.0f ? -fVar13 : fVar13;
    if (c180 <= a13) {
        if (fVar13 <= 0.0f) fVar13 = fVar13 + c360;
        else fVar13 = -(c360 - fVar13);
    }
    const float b13 = fVar13 < 0.0f ? -fVar13 : fVar13;
    if (c01 < b13) {
        const float t = ch.desc->slowSpeed;
        fVar14 = (fVar13 + fVar10) * (uv - t) + t * fVar10;
        if (fVar14 <= c180) { if (fVar14 < cm180) fVar14 = fVar14 + c360; }
        else fVar14 = fVar14 - c360;
    }
    return fVar14;
}

float Speed(const Char& ch) {
    const float* r = ch.root;
    const float dot = r[1] * r[1] + r[0] * r[0] + r[2] * r[2];
    return std::sqrt(dot) * ch.f38;
}

// The tail from the speed test on. `fastPath` = the tick's STILLPATH block in front of it.
void Tail(Char& ch, bool fastPath, StillPath::MemoTable& memo, long long* fast) {
    const float fx0 = ch.m.pos[0], fy0 = ch.m.pos[1], fz = ch.m.pos[2];
    float pos[3] = { fx0, fy0, fz };
    float fVar14 = Heading(ch);
    const float speed = Speed(ch);
    float fx = fx0, fy = fy0;
    if (0.0f < speed) {
        pos[0] = fx + speed * ch.dir[0]; pos[1] = fy + speed * ch.dir[1]; pos[2] = fz + speed * ch.dir[2];
        fx = pos[0]; fy = pos[1];
    }
    bool candidate = false;
    if (fastPath && !(0.0f < speed)) {
        const float yawOut = fVar14 + ch.desc->f24;
        const float zOut = StillPath::ZOut(ch.desc->b3c, ch.desc->f40, fz);
        if (StillPath::IsFixedPoint(ch.m.rot[0], ch.m.rot[1], ch.m.rot[2], ch.m.rot[3], fz, ch.m.yaw,
                                    yawOut, zOut, memo.Find(ch.index))) {
            ++*fast;
            ++ch.pushes; ++ch.places;
            return;
        }
        candidate = true;
    }
    if (ch.desc->b3c) { pos[2] = ch.desc->f40; pos[0] = fx; pos[1] = fy; }
    fVar14 = fVar14 + ch.desc->f24;
    const float ang = fVar14 * FromBits(kRdata_1401720cc) * kRdata_140172114;
    float s = 0, c = 0;
    s = MsvcLibm::Sinf(ang);
    c = MsvcLibm::Cosf(ang);
    const float negSin = -s;
    if (candidate) memo.Put(ch.index, fVar14, negSin, c);
    ch.m.rot[0] = 0.0f; ch.m.rot[1] = 0.0f; ch.m.rot[2] = negSin; ch.m.rot[3] = c;
    ch.m.pos[0] = pos[0]; ch.m.pos[1] = pos[1]; ch.m.pos[2] = pos[2];
    ch.m.yaw = fVar14;
    ++ch.pushes; ++ch.places;
}

bool SameModel(const Model& a, const Model& b) { return std::memcmp(&a, &b, sizeof(Model)) == 0; }

// A small deterministic generator (no <random>: the sequence must be the same everywhere).
struct Lcg {
    uint64_t s;
    uint32_t Next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<uint32_t>(s >> 33); }
    float Unit() { return static_cast<float>(Next() & 0xffffff) / 16777216.0f; }
    bool Chance(int outOf, int n) { return static_cast<int>(Next() % static_cast<uint32_t>(outOf)) < n; }
};

}  // namespace

UNIT_TEST(still_path_memo_table) {
    StillPath::MemoTable t;
    CHECK(t.Find(0) == nullptr);
    CHECK(t.Find(-1) == nullptr);
    t.Put(5, 30.0f, -0.25f, 0.96f);
    CHECK(t.Size() >= 6u);
    const StillPath::Memo* m = t.Find(5);
    CHECK(m != nullptr);
    CHECK_EQ(m->valid, 1u);
    CHECK_EQ(m->yaw, Bits(30.0f));
    CHECK_EQ(m->negSin, Bits(-0.25f));
    CHECK_EQ(m->cos, Bits(0.96f));
    // Untouched entries of a grown table are not valid (a zero entry is not R(+0)).
    CHECK(t.Find(4) != nullptr);
    CHECK_EQ(t.Find(4)->valid, 0u);
    t.Put(-3, 1.0f, 1.0f, 1.0f);   // ignored
    CHECK(t.Find(-3) == nullptr);
}

UNIT_TEST(still_path_decision) {
    using StillPath::IsFixedPoint;
    StillPath::Memo m;
    m.yaw = Bits(90.0f); m.negSin = Bits(-0.7071f); m.cos = Bits(0.7071f); m.valid = 1;
    // The fixed point: rotation (+0, +0, memo), yaw and z unchanged.
    CHECK(IsFixedPoint(0.0f, 0.0f, -0.7071f, 0.7071f, 3.5f, 90.0f, 90.0f, 3.5f, &m));
    // No memo / invalid memo.
    CHECK(!IsFixedPoint(0.0f, 0.0f, -0.7071f, 0.7071f, 3.5f, 90.0f, 90.0f, 3.5f, nullptr));
    StillPath::Memo inv = m; inv.valid = 0;
    CHECK(!IsFixedPoint(0.0f, 0.0f, -0.7071f, 0.7071f, 3.5f, 90.0f, 90.0f, 3.5f, &inv));
    // The yaw the tail would write differs (a turn still converging).
    CHECK(!IsFixedPoint(0.0f, 0.0f, -0.7071f, 0.7071f, 3.5f, 90.0f, 90.0001f, 3.5f, &m));
    // The memo is for another yaw.
    CHECK(!IsFixedPoint(0.0f, 0.0f, -0.7071f, 0.7071f, 3.5f, 45.0f, 45.0f, 3.5f, &m));
    // The z the tail would write differs (desc +0x3c set, a different +0x40).
    CHECK(!IsFixedPoint(0.0f, 0.0f, -0.7071f, 0.7071f, 3.5f, 90.0f, 90.0f, 0.0f, &m));
    // Bits, not values: -0.0 is not what the tail writes into rotation x / y, nor is it the z.
    CHECK(!IsFixedPoint(-0.0f, 0.0f, -0.7071f, 0.7071f, 3.5f, 90.0f, 90.0f, 3.5f, &m));
    CHECK(!IsFixedPoint(0.0f, -0.0f, -0.7071f, 0.7071f, 3.5f, 90.0f, 90.0f, 3.5f, &m));
    CHECK(!IsFixedPoint(0.0f, 0.0f, -0.7071f, 0.7071f, 0.0f, 90.0f, 90.0f, -0.0f, &m));
    // The live rotation is not the memo's (an external write).
    CHECK(!IsFixedPoint(0.0f, 0.0f, -0.7071f, 0.70711f, 3.5f, 90.0f, 90.0f, 3.5f, &m));
    CHECK(!IsFixedPoint(0.1f, 0.0f, -0.7071f, 0.7071f, 3.5f, 90.0f, 90.0f, 3.5f, &m));
    // A NaN yaw that is a fixed point (same bits in and out) is decided like any other.
    const float qnan = FromBits(0x7fc00001u);
    StillPath::Memo mn; mn.yaw = Bits(qnan); mn.negSin = Bits(qnan); mn.cos = Bits(qnan); mn.valid = 1;
    CHECK(IsFixedPoint(0.0f, 0.0f, qnan, qnan, 1.0f, qnan, qnan, 1.0f, &mn));
    // ZOut: desc +0x3c selects +0x40, else the z read.
    CHECK_EQ(Bits(StillPath::ZOut(true, 7.0f, 3.0f)), Bits(7.0f));
    CHECK_EQ(Bits(StillPath::ZOut(false, 7.0f, 3.0f)), Bits(3.0f));
}

UNIT_TEST(still_path_shadow_diff) {
    uint32_t a[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint32_t b[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK_EQ(StillPath::ShadowDiff(a, b), 0LL);
    b[2] = 9;  CHECK_EQ(StillPath::ShadowDiff(a, b), 2LL);
    b[5] = 9;  CHECK_EQ(StillPath::ShadowDiff(a, b), 3LL);
    b[7] = 9;  CHECK_EQ(StillPath::ShadowDiff(a, b), 7LL);
}

// The two worlds: every iteration both copies tick; between iterations the script changes
// inputs (and, for the fast world, never anything the full world does not also see).
UNIT_TEST(still_path_two_worlds_bit_identical) {
    Desc descs[3] = { { 0.0f, 0.85f, false, 0.0f }, { 90.0f, 0.85f, false, 0.0f }, { 0.0f, 0.85f, true, 2.5f } };
    constexpr int kChars = 24;
    std::vector<Char> full(kChars), fast(kChars);
    Lcg rng{ 12345 };
    for (int i = 0; i < kChars; ++i) {
        Char c{};
        c.index = i % 20;   // indices 0..3 are shared by two characters (a reused grid index)
        c.desc = &descs[i % 3];
        c.f3C = rng.Unit() * 360.0f - 180.0f;
        c.f38 = 1.0f;
        c.dir[0] = 1.0f; c.dir[1] = 0.0f; c.dir[2] = 0.0f;
        c.m.rot[0] = 0.0f; c.m.rot[1] = 0.0f; c.m.rot[2] = 0.0f; c.m.rot[3] = 1.0f;
        c.m.pos[0] = rng.Unit() * 100.0f; c.m.pos[1] = rng.Unit() * 100.0f; c.m.pos[2] = rng.Unit();
        c.m.yaw = rng.Unit() * 360.0f - 180.0f;
        full[i] = c; fast[i] = c;
    }
    StillPath::MemoTable memoFull, memoFast;   // the full world never reads its memo
    long long fastTicks = 0, unused = 0, ticks = 0;
    for (int it = 0; it < 4000; ++it) {
        // The script (applied to both worlds alike).
        for (int i = 0; i < kChars; ++i) {
            Char& a = full[i]; Char& b = fast[i];
            if (rng.Chance(1000, 3)) { const float t = rng.Unit() * 360.0f - 180.0f; a.f3C = t; b.f3C = t; }  // new facing
            if (rng.Chance(1000, 5)) {                                                                    // start / stop a step
                const float r = rng.Chance(100, 50) ? 0.0f : 0.01f * rng.Unit();
                a.root[0] = r; b.root[0] = r;
            }
            if (rng.Chance(1000, 2)) {                                                                    // speed 0 with root motion
                const float s = rng.Chance(100, 50) ? 0.0f : 1.0f; a.f38 = s; b.f38 = s;
            }
            if (rng.Chance(1000, 2)) {                                                                    // an external transform write
                const float y = rng.Unit() * 360.0f; a.m.yaw = y; b.m.yaw = y;
                const float x = rng.Unit() * 50.0f;  a.m.pos[0] = x; b.m.pos[0] = x;
            }
            if (rng.Chance(1000, 3)) {                                                                    // a yaw-only nudge inside the 0.1 deg turn epsilon
                const float y = a.m.yaw + 0.05f; a.m.yaw = y; b.m.yaw = y;                                 // (the tail snaps it back to f3C + f24)
            }
            if (rng.Chance(1000, 2)) {                                                                    // a z-only write (the tail keeps it, or b3c resets it)
                const float z = rng.Unit(); a.m.pos[2] = z; b.m.pos[2] = z;
            }
            if (rng.Chance(1000, 2)) { a.m.rot[0] = -0.0f; b.m.rot[0] = -0.0f; }                               // a -0 the tail rewrites
            if (rng.Chance(1000, 2)) { a.m.rot[3] = 0.5f; b.m.rot[3] = 0.5f; }                                 // a rotation tamper
        }
        for (int i = 0; i < kChars; ++i) {
            Tail(full[i], false, memoFull, &unused);
            Tail(fast[i], true, memoFast, &fastTicks);
            ++ticks;
            CHECK(SameModel(full[i].m, fast[i].m));
            CHECK_EQ(full[i].pushes, fast[i].pushes);
            CHECK_EQ(full[i].places, fast[i].places);
        }
    }
    CHECK_EQ(unused, 0LL);
    // The fast path must actually fire (most characters stand still between the script's events).
    std::printf("  two worlds: %lld ticks, %lld fast\n", ticks, fastTicks);
    CHECK(fastTicks > ticks / 4);
    CHECK(fastTicks < ticks);
}

// A converged character's second tick is fast; a turn is never fast until it converges.
UNIT_TEST(still_path_converges_then_fast) {
    Desc d{ 0.0f, 0.85f, false, 0.0f };
    Char c{};
    c.index = 0; c.desc = &d; c.f3C = 120.0f; c.f38 = 1.0f;
    c.m.rot[3] = 1.0f; c.m.yaw = 0.0f;
    StillPath::MemoTable memo;
    long long fast = 0;
    int firstFast = -1;
    float lastYaw = c.m.yaw;
    for (int it = 0; it < 400 && firstFast < 0; ++it) {
        Tail(c, true, memo, &fast);
        if (fast) firstFast = it;
        else lastYaw = c.m.yaw;
    }
    CHECK(firstFast > 1);
    // The tick before the first fast one wrote the converged yaw (= f3C + f24) and the memo.
    CHECK_EQ(Bits(lastYaw), Bits(120.0f));
    CHECK_EQ(Bits(c.m.yaw), Bits(120.0f));
    // A step: never fast while moving.
    c.root[0] = 0.02f;
    const long long before = fast;
    for (int it = 0; it < 10; ++it) Tail(c, true, memo, &fast);
    CHECK_EQ(fast, before);
}

UNIT_TEST(still_path_char_prof_estimate) {
    CharProf::Window w;
    // 10 fast ticks at 20, 4 full-tail still intermediate ticks at 50, 2 still last-iteration
    // ticks at 90 (excluded), 3 walking ticks.
    for (int i = 0; i < 10; ++i) w.AddChar(CharProf::kOn, CharProf::kTimed, 20, false, false, true, false, true);
    for (int i = 0; i < 4; ++i)  w.AddChar(CharProf::kOff, CharProf::kIdle, 50, false, false, true, false);
    for (int i = 0; i < 2; ++i)  w.AddChar(CharProf::kOn, CharProf::kTimed, 90, true, false, true, false);
    for (int i = 0; i < 3; ++i)  w.AddChar(CharProf::kOn, CharProf::kWalk, 70, false, true, false, false);
    const CharProf::StillPathEstimate e = CharProf::EstimateStillPath(w);
    CHECK_EQ(e.fast, 10LL);
    CHECK_EQ(e.fastTicks, 200LL);
    CHECK_EQ(e.fullInter, 4LL);
    CHECK_EQ(e.fullInterTicks, 200LL);
    CHECK(std::fabs(e.savedTicks - 300.0) < 1e-9);   // (50 - 20) x 10
    CHECK_EQ(w.Sum(&CharProf::Cell::fast), 10LL);
    CHECK_EQ(w.cell[CharProf::kOn][CharProf::kTimed].lastStillTicks, 180LL);
    // No full-tail population: no estimate.
    CharProf::Window w2;
    w2.AddChar(CharProf::kOn, CharProf::kTimed, 20, false, false, true, false, true);
    CHECK(CharProf::EstimateStillPath(w2).savedTicks == 0.0);
}
