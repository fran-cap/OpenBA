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

} // namespace EntityListBucket
