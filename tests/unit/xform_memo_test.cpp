// ============================================================================
// tests/unit/xform_memo_test.cpp -- XFORMMEMO: the per-Renderer::Draw instance
// transform memo (src/engine/graphics/xform_memo.h, issue #253)
// ============================================================================
// The slot table is checked directly, then two copies of one scripted renderer
// run side by side, as skin_memo_test.cpp does. The "instances" live in a fixed
// pool, so a freed instance is reused AT THE SAME ADDRESS with new contents
// (the pointer-key hazard across frames). World A always runs the producer (a
// stand-in for MeshInstance::GetInstanceTransform: a pure function of the
// instance bytes, the packed level|list and t). World B runs the memo exactly
// as XformMemo::Transform does (SlotOf, Find, Occupied, Claim) with one epoch
// per scope. Writes happen only between scopes; every request's 64 bytes must
// match bit for bit. A broken variant that writes an instance inside a scope
// must diverge (the test can tell).
// ============================================================================
#include "unit.h"
#include "graphics/xform_memo.h"
#include "math/Math.h"

#include <cstdint>
#include <cstring>

namespace {

uint64_t Mix(uint64_t x) {   // splitmix64
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

constexpr int kPool = 64;
constexpr int kFloats = 18;   // two Transforms (14 floats) and a colour (4)

struct FakeInstance { float data[kFloats]; };

// The producer: every output float depends on every input bit.
void Produce(const FakeInstance& g, int32_t packed, uint32_t t, Math::Matrix* out) {
    uint64_t h = Mix(static_cast<uint64_t>(static_cast<uint32_t>(packed)) * 131u + t);
    for (int j = 0; j < kFloats; ++j) {
        uint32_t u;
        std::memcpy(&u, &g.data[j], 4);
        h = Mix(h ^ u);
    }
    for (int c = 0; c < 16; ++c) {
        h = Mix(h + static_cast<uint64_t>(c));
        out->m[c] = static_cast<float>(h >> 40) * 0.001f;
    }
}

struct Stats { long long requests = 0, hits = 0, mismatches = 0, evictions = 0; };

Stats Run(uint64_t seed, bool writeInsideScope, int frames) {
    Stats st;
    static FakeInstance pool[kPool];   // a fixed address per instance
    auto fill = [&](int i, uint64_t s) {
        for (int j = 0; j < kFloats; ++j) pool[i].data[j] = static_cast<float>(Mix(s + j) % 2000) * 0.01f - 10.0f;
    };
    for (int i = 0; i < kPool; ++i) fill(i, seed * 977u + static_cast<uint64_t>(i));

    XformMemo::Table table;
    uint64_t epoch = 1;
    uint64_t rng = seed;
    for (int f = 0; f < frames; ++f) {
        // Between scopes: the sim moves some instances, frees and reuses others in place.
        for (int k = 0; k < 8; ++k) {
            rng = Mix(rng);
            fill(static_cast<int>(rng % kPool), rng);
        }
        ++epoch;   // BeginScope
        const float tf = static_cast<float>(f % 7) * 0.125f;
        const uint32_t tb = XformMemo::Bits(tf);
        // Four "passes" (shadow, main, a light, reflection) over the same items.
        for (int pass = 0; pass < 4; ++pass) {
            for (int i = 0; i < kPool; ++i) {
                const int32_t packed = (pass == 3 ? ((i & 1) << 12) : 0) | (i % 3);
                if (writeInsideScope && pass == 2 && i == 5) fill(5, rng + 12345u);
                Math::Matrix ref;
                Produce(pool[i], packed, tb, &ref);

                const int32_t s = table.SlotOf(XformMemo::HashKey(&pool[i], packed, tb));
                const Math::Matrix* got = table.Find(s, &pool[i], packed, tb, epoch);
                if (got != nullptr) {
                    ++st.hits;
                } else {
                    if (table.Occupied(s, epoch)) ++st.evictions;
                    Math::Matrix* out = table.Claim(s, &pool[i], packed, tb, epoch);
                    Produce(pool[i], packed, tb, out);
                    got = out;
                }
                ++st.requests;
                if (std::memcmp(got, &ref, sizeof(Math::Matrix)) != 0) ++st.mismatches;
            }
        }
        ++epoch;   // EndScope
    }
    return st;
}

}  // namespace

UNIT_TEST(xform_memo_table_slots_and_epochs) {
    XformMemo::Table table;
    int a = 0;
    int b = 0;
    const uint32_t t = XformMemo::Bits(0.5f);
    const int32_t s = table.SlotOf(XformMemo::HashKey(&a, 3, t));
    CHECK(s >= 0 && s < XformMemo::kSlots);
    CHECK(table.Find(s, &a, 3, t, 1) == nullptr);   // empty before first use
    CHECK(!table.Occupied(s, 1));
    Math::Matrix* m = table.Claim(s, &a, 3, t, 1);
    m->m[0] = 42.0f;
    CHECK(table.Find(s, &a, 3, t, 1) == m);
    CHECK(table.Occupied(s, 1));
    CHECK(table.Find(s, &a, 3, t, 2) == nullptr);   // a new epoch empties the table
    CHECK(!table.Occupied(s, 2));
    CHECK(table.Find(s, &b, 3, t, 1) == nullptr);   // every key field is compared
    CHECK(table.Find(s, &a, 4, t, 1) == nullptr);
    CHECK(table.Find(s, &a, 3, XformMemo::Bits(0.25f), 1) == nullptr);
    CHECK(XformMemo::Bits(-0.0f) != XformMemo::Bits(0.0f));   // keyed by bits, not by value
}

UNIT_TEST(xform_memo_two_renderers_bit_identical) {
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        const Stats st = Run(seed, false, 40);
        CHECK(st.mismatches == 0);
        CHECK(st.hits > st.requests / 2);   // passes 1 and 2 (and most of 3) hit
    }
}

UNIT_TEST(xform_memo_write_inside_scope_diverges) {
    const Stats st = Run(3, true, 10);
    CHECK(st.mismatches > 0);   // the census claim is what the memo rests on
}
