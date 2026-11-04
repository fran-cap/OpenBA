// ============================================================================
// tests/unit/entitylist_bucket_test.cpp -- EntityList's bucket round robin and bucket index
// ============================================================================
// EntityList::Update (0x1400c4090) ticks `processCount[b]` entries of bucket b per frame,
// starting at the saved `cursor[b]`. Retail reads the bucket count ONCE per bucket
// (0x1400c4170 MOVSXD RAX,[R13+0x8], kept in RDI / [RSP+0x20]) and wraps against it
// (0x1400c430e CMP RSI,RDI), so an entry appended by a tick during the pass is not visited
// until a later frame (review F-185). A negative saved cursor is reset to 0 (review F-131,
// NOT RETAIL: retail reads before the array). The bucket index in ComponentEntity::_handle is
// sign-extended (SHL 8 / SAR 8 at 0x1400c462f and 0x1400c3934, review F-186).
// ============================================================================
#include "unit.h"

#include <cstdint>
#include <vector>

#include "Entity/entitylist_bucket.h"

namespace {

struct Bucket {
    std::vector<int> entries;
    std::vector<int64_t> visited;
    bool outOfRange = false;
};

int32_t Run(Bucket& b, int32_t cursor, int32_t toProcess, int appendAt) {
    auto size = [&b] { return static_cast<int32_t>(b.entries.size()); };
    auto visit = [&b, appendAt](int64_t idx) {
        if (idx < 0 || idx >= static_cast<int64_t>(b.entries.size())) {
            b.outOfRange = true;
            return;
        }
        b.visited.push_back(idx);
        if (idx == appendAt)
            b.entries.push_back(1000);   // the tick registers an entity into this bucket
    };
    return EntityListBucket::RoundRobin(size, cursor, toProcess, visit);
}

} // namespace

// The ledger's worked example: count 100, processCount 2, cursor 99, and the tick of entry 99
// appends entry 100. Retail ticks 99, wraps at the stale count, ticks 0, saves cursor 1.
UNIT_TEST(entitylist_bucket_wraps_at_count_read_once) {
    Bucket b;
    b.entries.assign(100, 0);
    const int32_t next = Run(b, 99, 2, 99);
    CHECK(!b.outOfRange);
    CHECK_EQ(b.visited.size(), static_cast<size_t>(2));
    if (b.visited.size() == 2) {
        CHECK_EQ(b.visited[0], static_cast<int64_t>(99));
        CHECK_EQ(b.visited[1], static_cast<int64_t>(0));
    }
    CHECK_EQ(next, 1);
}

// Unchanged valid behaviour: a full lap stops at the start, a cursor at or past the count
// restarts at 0, a count of zero to process stores the clamped cursor.
UNIT_TEST(entitylist_bucket_valid_cursors) {
    Bucket lap;
    lap.entries.assign(3, 0);
    CHECK_EQ(Run(lap, 1, 10, -1), 1);
    CHECK_EQ(lap.visited.size(), static_cast<size_t>(3));
    if (lap.visited.size() == 3) {
        CHECK_EQ(lap.visited[0], static_cast<int64_t>(1));
        CHECK_EQ(lap.visited[2], static_cast<int64_t>(0));
    }

    Bucket past;
    past.entries.assign(4, 0);
    CHECK_EQ(Run(past, 7, 1, -1), 1);
    CHECK(past.visited.size() == 1 && past.visited[0] == 0);

    Bucket none;
    none.entries.assign(4, 0);
    CHECK_EQ(Run(none, 9, 0, -1), 0);
    CHECK_EQ(Run(none, 2, 0, -1), 2);
    CHECK(none.visited.empty());
}

// A save holding cursor[b] = -5 (FB FF FF FF) for a non-empty bucket.
UNIT_TEST(entitylist_bucket_negative_saved_cursor) {
    const unsigned char saved[4] = { 0xFB, 0xFF, 0xFF, 0xFF };
    const int32_t cursor = static_cast<int32_t>(static_cast<uint32_t>(saved[0]) |
                                                (static_cast<uint32_t>(saved[1]) << 8) |
                                                (static_cast<uint32_t>(saved[2]) << 16) |
                                                (static_cast<uint32_t>(saved[3]) << 24));
    Bucket b;
    b.entries.assign(3, 0);
    const int32_t next = Run(b, cursor, 1, -1);
    CHECK(!b.outOfRange);
    CHECK_EQ(b.visited.size(), static_cast<size_t>(1));
    if (b.visited.size() == 1)
        CHECK_EQ(b.visited[0], static_cast<int64_t>(0));
    CHECK_EQ(next, 1);
}

// RegisterEntity with category 6 stores 0xFFFFFF in the low 24 bits; retail writes it to the
// save as the int -1 (SAR), not 16777215.
UNIT_TEST(entitylist_bucket_index_sign_extends) {
    CHECK_EQ(EntityListBucket::IndexOf(0x06FFFFFFu), -1);
    CHECK_EQ(EntityListBucket::IndexOf(0x02000005u), 5);
    CHECK_EQ(EntityListBucket::IndexOf(0x007FFFFFu), 0x7FFFFF);
    CHECK_EQ(EntityListBucket::IndexOf(0x04800000u), -0x800000);
}
