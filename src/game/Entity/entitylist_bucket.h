#pragma once
// ============================================================================
// EntityList's five update buckets: the round robin of EntityList::Update (0x1400c4090) and
// the bucket index packed in ComponentEntity::_handle. Pure helpers, so the unit tests reach
// them (tests/unit/entitylist_bucket_test.cpp).
// ============================================================================
#include <cstdint>

namespace EntityListBucket {

// The 24-bit index of an entity within its bucket: the low 24 bits of _handle (+0x1c),
// sign-extended as retail does (MOV ECX,[RAX+0x1c] / SHL ECX,8 / SAR ECX,8 at 0x1400c462c in
// Serialize, SHL EBP,8 / SAR EBP,8 at 0x1400c3934 in RemoveEntity). A category-6 entity's
// 0xFFFFFF is -1.
inline int32_t IndexOf(uint32_t handle) {
    return static_cast<int32_t>(handle << 8) >> 8;
}

// One bucket of the slot-9 round robin (0x1400c4170..0x1400c4331). `size()` reads the
// bucket's entry count, `visit(idx)` ticks entry idx. Returns the cursor to store back.
// The count is read ONCE (0x1400c4170 MOVSXD RAX,[R13+0x8], kept in RDI / [RSP+0x20]) and the
// wrap compares against it (0x1400c430e CMP RSI,RDI): an entry a tick appends during the pass
// waits for a later frame.
template <class Size, class Visit>
inline int32_t RoundRobin(Size size, int32_t cursor, int32_t toProcess, Visit visit) {
    const int32_t count = size();
    // 0x1400c4185 CMP R14D,EAX / CMOVGE: a cursor at or past the count restarts at 0.
    // NOT RETAIL: the `cursor < 0` test. The cursor is read from the save, and retail
    // sign-extends a negative one (0x1400c419b MOVSXD RSI,R14D) and reads before the array
    // (out-of-bounds read, undefined behaviour). A valid save never holds one.
    if (cursor < 0 || cursor >= count) cursor = 0;

    const int32_t startIdx = cursor;
    int32_t idx = startIdx;
    int32_t processed = 0;
    if (toProcess > 0) {
        do {
            visit(static_cast<int64_t>(idx));
            idx++;
            processed++;
            if (idx >= count) idx = 0;
        } while (idx != startIdx && processed < toProcess);
    }
    return idx;
}

// The category byte (+0x1F) of an entity that sits in no bucket (RegisterEntity's
// category 6, index 0xFFFFFF).
constexpr int32_t kNoBucket = 6;
constexpr int32_t kBucketCount = 5;

// The time sums of EntityList::Update: `accumulatedTime += time` (0x1400c40b6) and the
// per-entity `delta = currentTime - _lastUpdateTime` (0x1400c4227). Retail's ADD / SUB wrap.
// NOT RETAIL (review S-038): both operands come from the save (System::Time::Serialize, a raw
// int64), and a signed overflow is undefined behaviour in C++; the sums go through uint64_t,
// which wraps as retail's instructions do. Every result is the same bits.
constexpr int64_t AddTicks(int64_t a, int64_t b) {
    return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}
constexpr int64_t TicksSince(int64_t now, int64_t then) {
    return static_cast<int64_t>(static_cast<uint64_t>(now) - static_cast<uint64_t>(then));
}

// NOT RETAIL (review S-007): EntityList::Serialize stores each entity's saved master index
// (+0x18), bucket index (+0x1C low 24 bits) and bucket id (+0x1F) unchecked (retail
// 0x1400c46b9..0x1400c46dd), and RemoveEntity (0x1400c38e0) then writes buckets[id]._data[idx]
// and masterArray._data[_entityId] with them. A loaded list is accepted only when every
// master slot holds an entity whose fields name that slot and the bucket entry that holds it,
// and every bucket entry is an entity whose fields name that entry: the state RegisterEntity
// and RemoveEntity keep. `master(i)` and `bucket(b, i)` return the entity (null for none);
// `bucketSize(b)` the entry count of bucket b. Entity has _entityId and _handle.
template <class Entity, class Master, class BucketSize, class Bucket>
inline bool PlacementsConsistent(int32_t masterCount, Master master, BucketSize bucketSize, Bucket bucket) {
    for (int32_t i = 0; i < masterCount; ++i) {
        const Entity* e = master(i);
        if (e == nullptr || e->_entityId != i) {
            return false;
        }
        const int32_t id = static_cast<int8_t>(e->_handle >> 24);
        if (id == kNoBucket) {
            continue;
        }
        if (id < 0 || id >= kBucketCount) {
            return false;
        }
        const int32_t idx = IndexOf(e->_handle);
        if (idx < 0 || idx >= bucketSize(id) || bucket(id, idx) != e) {
            return false;
        }
    }
    // Every bucket entry is a listed entity that names this entry (none in category 6, none
    // twice, none outside the master array).
    for (int32_t b = 0; b < kBucketCount; ++b) {
        const int32_t n = bucketSize(b);
        for (int32_t i = 0; i < n; ++i) {
            const Entity* e = bucket(b, i);
            if (e == nullptr) {
                return false;
            }
            if (static_cast<int8_t>(e->_handle >> 24) != b || IndexOf(e->_handle) != i) {
                return false;
            }
            if (e->_entityId < 0 || e->_entityId >= masterCount || master(e->_entityId) != e) {
                return false;
            }
        }
    }
    return true;
}

} // namespace EntityListBucket
