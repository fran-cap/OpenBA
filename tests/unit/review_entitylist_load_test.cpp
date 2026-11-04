// tests/unit/review_entitylist_load_test.cpp
// review S-007 and S-038: values EntityList::Serialize reads back from a save
// (src/game/Entity/entitylist_bucket.h). EntityList::Serialize / Update / RemoveEntity need a
// live world; they call these. All platforms.

#include "unit.h"
#include "Entity/entitylist_bucket.h"
#include <cstdint>
#include <vector>

using EntityListBucket::AddTicks;
using EntityListBucket::PlacementsConsistent;
using EntityListBucket::TicksSince;

namespace {

// S-038: a signed overflow is not a constant expression, so the old `a + b` / `now - then`
// bodies fail these at compile time.
static_assert(AddTicks(INT64_MAX, 1) == INT64_MIN, "accumulatedTime wraps as retail's ADD");
static_assert(TicksSince(INT64_MAX, -1) == INT64_MIN, "a saved -1 last-update time wraps as retail's SUB");
static_assert(TicksSince(INT64_MIN, 1) == INT64_MAX, "SUB wraps the other way");
static_assert(TicksSince(1000, 400) == 600, "an in-range delta is unchanged");
static_assert(AddTicks(1000, -400) == 600, "an in-range sum is unchanged");

struct Ent {
    int32_t _entityId = 0;
    uint32_t _handle = 0;
};

uint32_t Handle(int32_t bucketId, int32_t idx) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(bucketId)) << 24) | (static_cast<uint32_t>(idx) & 0xFFFFFF);
}

struct World {
    std::vector<Ent*> master;
    std::vector<Ent*> buckets[5];

    bool Consistent() const {
        auto at = [this](int32_t i) { return master[i]; };
        auto size = [this](int32_t b) { return static_cast<int32_t>(buckets[b].size()); };
        auto entry = [this](int32_t b, int32_t i) { return buckets[b][i]; };
        return PlacementsConsistent<Ent>(static_cast<int32_t>(master.size()), at, size, entry);
    }
};

// Three entities as RegisterEntity leaves them: a in bucket 0 entry 0, b in bucket 3 entry 0,
// c in no bucket (category 6, index 0xFFFFFF).
struct Fixture {
    Ent a, b, c;
    World w;
    Fixture() {
        a._entityId = 0;
        a._handle = Handle(0, 0);
        b._entityId = 1;
        b._handle = Handle(3, 0);
        c._entityId = 2;
        c._handle = Handle(6, 0xFFFFFF);
        w.master = {&a, &b, &c};
        w.buckets[0] = {&a};
        w.buckets[3] = {&b};
    }
};

}  // namespace

UNIT_TEST(review_entitylist_ticks_wrap) {
    // Run-time copies of the static_asserts above (the -Og build evaluates these at run time).
    volatile int64_t big = INT64_MAX;
    volatile int64_t minus1 = -1;
    CHECK_EQ(AddTicks(big, 1), INT64_MIN);
    CHECK_EQ(TicksSince(big, minus1), INT64_MIN);
    CHECK(TicksSince(big, minus1) <= 0);   // Update's `delta > 0` skips the tick, as retail
}

UNIT_TEST(review_entitylist_placements_valid) {
    Fixture f;
    CHECK(f.w.Consistent());
    World empty;
    CHECK(empty.Consistent());
}

UNIT_TEST(review_entitylist_placements_bad_bucket_id) {
    // A bucket id other than 0..4 / 6: RemoveEntity would take &buckets[id] past the five.
    for (int32_t id : {5, 7, 127, -1, -128}) {
        Fixture f;
        f.c._handle = Handle(id, 0);
        CHECK(!f.w.Consistent());
    }
}

UNIT_TEST(review_entitylist_placements_bad_bucket_index) {
    Fixture f;
    f.a._handle = Handle(0, 1);   // past bucket 0's one entry
    CHECK(!f.w.Consistent());

    Fixture g;
    g.b._handle = Handle(3, 0x7FFFFF);
    CHECK(!g.w.Consistent());

    Fixture h;
    h.a._handle = Handle(0, 0xFFFFFF);   // sign-extends to -1
    CHECK(!h.w.Consistent());

    // The entry it names holds another entity.
    Fixture k;
    k.b._handle = Handle(0, 0);
    CHECK(!k.w.Consistent());
}

UNIT_TEST(review_entitylist_placements_bad_master_index) {
    Fixture f;
    f.b._entityId = 2;   // names c's slot
    CHECK(!f.w.Consistent());

    Fixture g;
    g.c._entityId = 3;   // past the master array
    CHECK(!g.w.Consistent());

    Fixture h;
    h.a._entityId = -1;
    CHECK(!h.w.Consistent());
}

UNIT_TEST(review_entitylist_placements_null_and_strays) {
    // A null master slot (the F-143 type filter clears a slot that is not a ComponentEntity).
    Fixture f;
    f.w.master[1] = nullptr;
    CHECK(!f.w.Consistent());

    // A bucket entry whose entity says category 6: RemoveEntity would leave it in the bucket.
    Fixture g;
    g.w.buckets[0].push_back(&g.c);
    CHECK(!g.w.Consistent());

    // A bucket entry no master slot holds.
    Fixture h;
    Ent stray;
    stray._entityId = 0;
    stray._handle = Handle(0, 1);
    h.w.buckets[0].push_back(&stray);
    CHECK(!h.w.Consistent());

    // A null bucket entry.
    Fixture k;
    k.w.buckets[4].push_back(nullptr);
    CHECK(!k.w.Consistent());

    // One entity in two master slots.
    Fixture m;
    m.w.master.push_back(&m.a);
    CHECK(!m.w.Consistent());
}
