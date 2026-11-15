// ============================================================================
// tests/unit/type_cache_test.cpp -- TYPECACHE: Object::Base::Type() through the vptr-keyed cache
// (src/game/Object/type_cache.h) answers what Type() answers
// ============================================================================
// The core TypeCache::Lookup runs on a private table with synthetic vptr values, so every case is
// reachable: a miss asks the slow answer and stores it; a hit answers the stored id without asking;
// the sentinel 0xFFFFFFFF (an id before Factory::Startup) is never stored; a vptr outside the
// table's 4 GB window is never cached; two vptrs on one slot replace each other and never answer
// each other's id; a vptr whose low 32 bits are 0 never matches an empty entry. A randomized run
// over many synthetic classes compares every answer with the slow one. TypeOf itself runs on two
// real Object::Base subclasses (vptr read, the shared table). Platform-independent; no heap
// address enters an expectation (the synthetic vptrs are constants, the real objects are compared
// only with their own Type()).
// ============================================================================
#include "unit.h"
#include "Object/type_cache.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace {

struct Table {
    std::atomic<uint64_t> e[TypeCache::kEntries];
    Table() {
        for (auto& x : e) x.store(0, std::memory_order_relaxed);
    }
};

constexpr uint64_t kWindow = 0x1ull;   // vptrs 0x1'xxxxxxxx

struct Slow {
    uint32_t id;
    int* calls;
    uint32_t operator()() const {
        ++*calls;
        return id;
    }
};

class TypeA : public Object::Base {
public:
    const char* TypeName() const override { return "TypeA"; }
    uint32_t Type() const override { return 0x1234u; }
};

class TypeB : public Object::Base {
public:
    const char* TypeName() const override { return "TypeB"; }
    uint32_t Type() const override { return 0xBEEFu; }
};

}  // namespace

UNIT_TEST(type_cache_miss_then_hit) {
    Table t;
    int calls = 0;
    const uint64_t vp = 0x140161810ull;
    CHECK_EQ(TypeCache::Lookup(t.e, vp, kWindow, Slow{0x77u, &calls}), 0x77u);
    CHECK_EQ(calls, 1);
    // a hit answers the stored id and does not ask (the slow answer here would differ)
    CHECK_EQ(TypeCache::Lookup(t.e, vp, kWindow, Slow{0x99u, &calls}), 0x77u);
    CHECK_EQ(calls, 1);
}

UNIT_TEST(type_cache_sentinel_never_stored) {
    Table t;
    int calls = 0;
    const uint64_t vp = 0x140161900ull;
    CHECK_EQ(TypeCache::Lookup(t.e, vp, kWindow, Slow{TypeCache::kInvalidId, &calls}), TypeCache::kInvalidId);
    CHECK_EQ(TypeCache::Lookup(t.e, vp, kWindow, Slow{0x55u, &calls}), 0x55u);   // asked again: nothing stored
    CHECK_EQ(calls, 2);
    CHECK_EQ(TypeCache::Lookup(t.e, vp, kWindow, Slow{0x66u, &calls}), 0x55u);
    CHECK_EQ(calls, 2);
}

UNIT_TEST(type_cache_outside_window_not_cached) {
    Table t;
    int calls = 0;
    const uint64_t vp = 0x240161810ull;   // high half 2, the window is 1
    CHECK_EQ(TypeCache::Lookup(t.e, vp, kWindow, Slow{0x10u, &calls}), 0x10u);
    CHECK_EQ(TypeCache::Lookup(t.e, vp, kWindow, Slow{0x11u, &calls}), 0x11u);
    CHECK_EQ(calls, 2);
    // the same low half inside the window is a different key
    CHECK_EQ(TypeCache::Lookup(t.e, 0x140161810ull, kWindow, Slow{0x12u, &calls}), 0x12u);
    CHECK_EQ(calls, 3);
}

UNIT_TEST(type_cache_zero_low_half_never_matches_empty) {
    Table t;
    int calls = 0;
    const uint64_t vp = 0x100000000ull;   // low 32 bits 0: the key is 1, an empty entry is 0
    CHECK_EQ(TypeCache::Lookup(t.e, vp, kWindow, Slow{0x0u, &calls}), 0x0u);
    CHECK_EQ(calls, 1);
    CHECK_EQ(TypeCache::Lookup(t.e, vp, kWindow, Slow{0x5u, &calls}), 0x0u);   // now a hit on its own entry
    CHECK_EQ(calls, 1);
}

UNIT_TEST(type_cache_slot_collisions_replace_never_alias) {
    // find two keys on one slot
    const uint64_t a = 0x140200000ull;
    uint64_t b = a + 8;
    const uint32_t slotA = TypeCache::SlotOf(static_cast<uint32_t>(a | 1u));
    while (TypeCache::SlotOf(static_cast<uint32_t>(b | 1u)) != slotA) b += 8;
    Table t;
    int calls = 0;
    CHECK_EQ(TypeCache::Lookup(t.e, a, kWindow, Slow{0xAu, &calls}), 0xAu);
    CHECK_EQ(TypeCache::Lookup(t.e, b, kWindow, Slow{0xBu, &calls}), 0xBu);   // evicts a
    CHECK_EQ(TypeCache::Lookup(t.e, a, kWindow, Slow{0xAu, &calls}), 0xAu);   // miss again, evicts b
    CHECK_EQ(TypeCache::Lookup(t.e, b, kWindow, Slow{0xBu, &calls}), 0xBu);
    CHECK_EQ(calls, 4);
}

UNIT_TEST(type_cache_random_classes_identical_to_slow) {
    // 2000 synthetic classes (8-aligned vptrs inside the window) with ids, 200k lookups in a
    // skewed order: every answer equals the class's own id.
    std::vector<uint64_t> vptrs;
    std::vector<uint32_t> ids;
    uint64_t s = 0x9E3779B97F4A7C15ull;
    auto next = [&s]() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    };
    for (int i = 0; i < 2000; ++i) {
        vptrs.push_back(0x140000000ull + (next() & 0x00FFFFF8ull));
        ids.push_back(static_cast<uint32_t>(next()) & 0x7FFFFFFFu);
    }
    // duplicate vptrs must carry one id: keep the first id per vptr
    for (size_t i = 0; i < vptrs.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (vptrs[j] == vptrs[i]) {
                ids[i] = ids[j];
                break;
            }
        }
    }
    Table t;
    int calls = 0;
    long long wrong = 0;
    for (int n = 0; n < 200000; ++n) {
        const uint64_t r = next();
        const size_t k = (r & 1) ? static_cast<size_t>((r >> 8) % 40) : static_cast<size_t>((r >> 8) % vptrs.size());
        if (TypeCache::Lookup(t.e, vptrs[k], kWindow, Slow{ids[k], &calls}) != ids[k]) ++wrong;
    }
    CHECK_EQ(wrong, 0LL);
    CHECK(calls < 200000);   // the hot 40 hit
}

UNIT_TEST(type_cache_typeof_real_objects) {
    TypeA a1;
    TypeA a2;
    TypeB b1;
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(TypeCache::TypeOf(&a1), a1.Type());
        CHECK_EQ(TypeCache::TypeOf(&a2), 0x1234u);
        CHECK_EQ(TypeCache::TypeOf(&b1), 0xBEEFu);
    }
    CHECK(TypeCache::IsDerivedFromOwnType(&b1, 0xBEEFu));
}
