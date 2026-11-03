// ============================================================================
// tests/unit/skin_memo_test.cpp -- SKINMEMO: the per-Renderer::Draw bone memo
// (src/engine/graphics/skin_memo.h, skin_memo.cpp)
// ============================================================================
// The slot table is checked directly, then two copies of one scripted renderer
// run side by side. Both hold pose arrays (the instances' `_nodes` and shared
// instanced states) and bind arrays in a fixed pool of buffers, so a freed array
// is reallocated AT THE SAME ADDRESS with new contents (the pointer-key hazard).
// World A always runs the producer (a stand-in for GetBones: a pure function of
// the pose bytes, the bind bytes, count, instanced, root and t). World B runs
// the memo exactly as SkinMemo::Bones does (key, slot, Live / Claim). Writes,
// frees and reallocations happen only between scopes (the census claim); every
// request's `count` matrices must match bit for bit. A broken variant that
// writes a pose inside a scope must diverge (the test can tell).
// Candidate 2 (a still pose memo across frames) is checked against the real
// Quaternion::Lerp: with both halves equal its result still depends on t.
// ============================================================================
#include "unit.h"
#include "graphics/skin_memo.h"
#include "math/Math.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using SkinMemo::Key;

uint64_t Mix(uint64_t x) {   // splitmix64
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

constexpr int kPool = 24;       // buffers (pose and bind arrays share the pool)
constexpr int kMaxCount = 40;   // floats per "node" record = 14 (two Transforms)

struct Buffer { std::vector<float> data; bool live = false; };

// One cheap bijective step (multiply by an odd constant, xorshift): the producer runs ~1.4M times
// in this file; in the -O0 unit build splitmix64 per input and output float was most of its time.
uint64_t Step(uint64_t h, uint32_t u) {
    h = (h ^ u) * 0x100000001B3ull;
    return h ^ (h >> 29);
}

// The producer: every output float depends on every input bit (each input goes through a bijective
// step of hk, and every output is drawn from hk after all of them).
void Produce(const float* pose, const float* binds, int count, bool instanced,
             const uint32_t root[7], uint32_t t, Math::Matrix* out) {
    uint64_t h = Mix((uint64_t)count * 31u + (instanced ? 7u : 0u) + t);
    if (!instanced) for (int i = 0; i < 7; ++i) h = Mix(h ^ root[i]);
    for (int k = 0; k < count; ++k) {
        uint64_t hk = h;
        for (int j = 0; j < 14; ++j) { uint32_t u; std::memcpy(&u, &pose[k * 14 + j], 4); hk = Step(hk, u); }
        for (int j = 0; j < 7; ++j)  { uint32_t u; std::memcpy(&u, &binds[k * 7 + j], 4); hk = Step(hk, u); }
        hk = Mix(hk);
        for (int c = 0; c < 16; ++c) {
            hk = Step(hk, (uint32_t)c);
            out[k].m[c] = (float)(hk >> 40) * 0.001f;
        }
    }
}

struct Instance { int pose; int binds; int count; bool instanced; };

struct Stats { long long requests = 0, hits = 0, mismatches = 0, evictions = 0, reuses = 0; };

// One run. `writeInsideScope` breaks the census claim on purpose.
Stats Run(uint64_t seed, bool writeInsideScope, int frames) {
    Stats st;
    Buffer pool[kPool];
    for (int i = 0; i < kPool; ++i) {
        pool[i].data.assign((size_t)kMaxCount * 14, 0.0f);   // never reallocated: a fixed address
        pool[i].live = false;
    }
    auto fill = [&](int b, uint64_t s) {
        for (size_t i = 0; i < pool[b].data.size(); ++i) pool[b].data[i] = (float)(Mix(s + i) % 2000) * 0.01f - 10.0f;
        pool[b].live = true;
    };
    for (int b = 0; b < kPool; ++b) fill(b, Mix(seed ^ (uint64_t)b));

    std::vector<Instance> inst;
    for (int i = 0; i < 30; ++i) {
        const uint64_t r = Mix(seed * 977 + (uint64_t)i);
        inst.push_back(Instance{ (int)(r % 12), 12 + (int)((r >> 8) % 6), 1 + (int)((r >> 16) % kMaxCount),
                                 ((r >> 24) & 3) == 0 });
    }
    SkinMemo::Table table;
    uint64_t epoch = 1;
    std::vector<Math::Matrix> ref((size_t)kMaxCount);

    for (int f = 0; f < frames; ++f) {
        // ---- between scopes: the sim writes poses, frees and reallocates arrays
        for (int w = 0; w < 4; ++w) {
            const uint64_t r = Mix(seed ^ ((uint64_t)f << 20) ^ (uint64_t)w);
            const int b = (int)(r % kPool);
            fill(b, r);                       // same address, new contents (a realloc reuse)
            ++st.reuses;
        }
        if (Mix(seed + (uint64_t)f) % 5 == 0) {   // an instance re-pointed (SetMesh / PlayAnimation)
            const uint64_t r = Mix(seed * 3 + (uint64_t)f);
            Instance& in = inst[r % inst.size()];
            in.pose = (int)((r >> 8) % 12);
            in.instanced = !in.instanced;
        }

        // ---- one Renderer::Draw
        ++epoch;                                         // BeginScope
        float tf = (float)(Mix(seed + (uint64_t)f * 13) % 1000) / 1000.0f;
        const uint32_t t = SkinMemo::Bits(tf);
        for (int q = 0; q < 300; ++q) {
            const uint64_t r = Mix(seed ^ ((uint64_t)f << 32) ^ (uint64_t)q * 0x51ED27u);
            const Instance& in = inst[r % inst.size()];
            // the root: the instance's own transform, an identity (SetSharedInstanceData)
            // or an edge mesh's transform: three variants per instance
            uint32_t root[7];
            for (int i = 0; i < 7; ++i) root[i] = (uint32_t)Mix((uint64_t)(&in - inst.data()) * 7 + (r >> 40) % 3 + (uint64_t)i);

            if (writeInsideScope && q == 150) fill(in.pose, r);   // BROKEN: a pose write inside the draw

            Key key;
            key.interp = pool[in.pose].data.data();
            key.binds = pool[in.binds].data.data();
            key.count = in.count;
            key.instanced = in.instanced ? 1u : 0u;
            key.t = t;
            if (in.instanced) for (uint32_t& x : key.root) x = 0;
            else std::memcpy(key.root, root, sizeof(root));

            Produce(pool[in.pose].data.data(), pool[in.binds].data.data(), in.count, in.instanced, root, t, ref.data());

            const uint64_t h = SkinMemo::HashKey(key);
            const int32_t s = table.SlotOf(h & 63);   // a tiny effective table: force evictions
            const Math::Matrix* got;
            if (table.Live(s, key, epoch)) { got = table.Region(s); ++st.hits; }
            else {
                if (table.Occupied(s, epoch)) ++st.evictions;
                Math::Matrix* out = table.Claim(s, key, epoch);
                Produce(pool[in.pose].data.data(), pool[in.binds].data.data(), in.count, in.instanced, root, t, out);
                got = out;
            }
            ++st.requests;
            if (std::memcmp(got, ref.data(), sizeof(Math::Matrix) * (size_t)in.count) != 0) ++st.mismatches;
        }
        ++epoch;                                         // EndScope
    }
    return st;
}

}  // namespace

UNIT_TEST(skin_memo_table_slots_and_epochs) {
    SkinMemo::Table table;
    Key a{}; a.interp = &a; a.count = 3; a.t = SkinMemo::Bits(0.5f);
    Key b = a; b.root[2] = SkinMemo::Bits(-0.0f);     // differs only in a sign bit
    CHECK(!SkinMemo::SameKey(a, b));
    const int32_t s = table.SlotOf(SkinMemo::HashKey(a));
    CHECK(!table.Live(s, a, 5));
    CHECK(!table.Occupied(s, 5));
    Math::Matrix* r = table.Claim(s, a, 5);
    r[0].m[0] = 1.0f;
    CHECK(table.Live(s, a, 5));
    CHECK(!table.Live(s, b, 5));                      // a stored key compare, not the hash
    CHECK(!table.Live(s, a, 6));                      // a new scope: empty, no clear
    CHECK(!table.Occupied(s, 6));
    CHECK(table.Occupied(s, 5));
    table.Claim(s, b, 5);                             // a collision evicts
    CHECK(!table.Live(s, a, 5));
    CHECK(table.Live(s, b, 5));
    CHECK(table.SlotOf(~0ull) == SkinMemo::kSlots - 1);
    CHECK(SkinMemo::Bits(0.0f) != SkinMemo::Bits(-0.0f));
}

UNIT_TEST(skin_memo_two_renderers_bit_identical) {
    long long req = 0, hits = 0, ev = 0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        const Stats st = Run(seed, false, 200);   // 360k requests, 800 same-address refills per seed
        CHECK_EQ(st.mismatches, 0LL);
        req += st.requests; hits += st.hits; ev += st.evictions;
    }
    std::printf("  skin_memo: %lld requests, %lld memo hits, %lld evictions\n", req, hits, ev);
    CHECK(hits > req / 4);
    CHECK(ev > 0);
}

UNIT_TEST(skin_memo_write_inside_scope_diverges) {
    long long mism = 0;
    for (uint64_t seed = 1; seed <= 6 && mism == 0; ++seed) mism += Run(seed, true, 200).mismatches;   // one is enough
    CHECK(mism > 0);
}

// Candidate 2 closed: a pose whose two halves are bit-identical does NOT give a
// t-independent bone, because Quaternion::Lerp(a, a, t) normalises
// a*(1-t) + a*t, whose rounding depends on t.
UNIT_TEST(skin_memo_still_lerp_depends_on_t) {
    int found = 0;
    for (uint64_t i = 0; i < 2000 && !found; ++i) {
        const uint64_t r = Mix(i + 12345);
        Math::Quaternion a((float)(r % 1000) / 997.0f - 0.5f, (float)((r >> 10) % 1000) / 991.0f - 0.5f,
                           (float)((r >> 20) % 1000) / 983.0f - 0.5f, (float)((r >> 30) % 1000) / 977.0f + 0.1f);
        const Math::Quaternion q1 = a.Lerp(a, Math::Scalar(0.0f));
        for (int k = 1; k < 64; ++k) {
            const Math::Quaternion q2 = a.Lerp(a, Math::Scalar((float)k / 64.0f + 0.003f));
            if (std::memcmp(&q1, &q2, sizeof(q1)) != 0) { ++found; break; }
        }
    }
    CHECK(found > 0);
}
