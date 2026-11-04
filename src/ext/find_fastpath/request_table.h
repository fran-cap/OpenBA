#pragma once
// ============================================================================
// src/ext/find_fastpath/request_table.h -- ext.find_fastpath's request table
//   (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md)
// ============================================================================
// Request characters -> a value (the row FindOrLoad's hit arm landed on) and
// the generation it was stored in. A flat open-addressing table (linear
// probing) whose keys are copied once into an append-only arena: no allocation
// per entry, one hash of the characters per lookup. Entries are never removed
// one by one; `Clear` empties the table and the arena.
//
// Standard C++ only (the unit tests link it on its own). Not thread-safe: the
// caller holds the resource-load lock.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace Ext {
namespace FindFastpath {

class RequestTable {
public:
    RequestTable() = default;
    ~RequestTable() { delete[] _slots; }
    RequestTable(const RequestTable&) = delete;
    RequestTable& operator=(const RequestTable&) = delete;

    // The value stored for chars[0..len) in generation `gen`, or null.
    void* Find(const char* chars, size_t len, uint64_t gen) const {
        if (_used == 0)
            return nullptr;
        const uint64_t h = Hash(chars, len);
        for (size_t i = static_cast<size_t>(h) & (_cap - 1); _slots[i].chars != nullptr; i = (i + 1) & (_cap - 1)) {
            const Slot& s = _slots[i];
            if (s.hash == h && s.len == len && std::memcmp(s.chars, chars, len) == 0)
                return s.gen == gen ? s.value : nullptr;
        }
        return nullptr;
    }

    // Stores `value` for chars[0..len) in generation `gen` (replacing an older one).
    void Put(const char* chars, size_t len, void* value, uint64_t gen) {
        if ((_used + 1) * 2 > _cap)
            Grow();
        const uint64_t h = Hash(chars, len);
        size_t i = static_cast<size_t>(h) & (_cap - 1);
        while (_slots[i].chars != nullptr) {
            Slot& s = _slots[i];
            if (s.hash == h && s.len == len && std::memcmp(s.chars, chars, len) == 0) {
                s.value = value;
                s.gen = gen;
                return;
            }
            i = (i + 1) & (_cap - 1);
        }
        Slot& s = _slots[i];
        s.chars = Copy(chars, len);
        s.len = len;
        s.hash = h;
        s.value = value;
        s.gen = gen;
        ++_used;
    }

    void Clear() {
        for (size_t i = 0; i < _cap; ++i)
            _slots[i] = Slot{};
        _used = 0;
        _blocks.clear();
        _blockUsed = kBlock;
    }

    size_t Size() const { return _used; }

    static uint64_t Hash(const char* p, size_t len) {
        uint64_t h = 0x9e3779b97f4a7c15ull ^ (len * 0xff51afd7ed558ccdull);
        size_t i = 0;
        for (; i + 8 <= len; i += 8) {
            uint64_t w;
            std::memcpy(&w, p + i, 8);
            h = (h ^ w) * 0xc4ceb9fe1a85ec53ull;
            h ^= h >> 29;
        }
        uint64_t tail = 0;
        std::memcpy(&tail, p + i, len - i);
        h = (h ^ tail) * 0xff51afd7ed558ccdull;
        h ^= h >> 32;
        h *= 0xc4ceb9fe1a85ec53ull;
        return h ^ (h >> 29);
    }

private:
    struct Slot {
        const char* chars = nullptr;
        size_t len = 0;
        uint64_t hash = 0;
        void* value = nullptr;
        uint64_t gen = 0;
    };
    static constexpr size_t kBlock = 1u << 20;

    const char* Copy(const char* chars, size_t len) {
        if (len > kBlock) {
            // its own block, at the front: the last block stays the one small keys fill
            _blocks.emplace(_blocks.begin(), new char[len]);
            std::memcpy(_blocks.front().get(), chars, len);
            return _blocks.front().get();
        }
        if (_blockUsed + len > kBlock) {
            _blocks.emplace_back(new char[kBlock]);
            _blockUsed = 0;
        }
        char* dst = _blocks.back().get() + _blockUsed;
        std::memcpy(dst, chars, len);
        _blockUsed += len;
        return dst;
    }

    void Grow() {
        const size_t oldCap = _cap;
        Slot* old = _slots;
        _cap = oldCap == 0 ? 4096 : oldCap * 2;
        _slots = new Slot[_cap];
        for (size_t i = 0; i < oldCap; ++i) {
            if (old[i].chars == nullptr)
                continue;
            size_t j = static_cast<size_t>(old[i].hash) & (_cap - 1);
            while (_slots[j].chars != nullptr)
                j = (j + 1) & (_cap - 1);
            _slots[j] = old[i];
        }
        delete[] old;
    }

    Slot* _slots = nullptr;
    size_t _cap = 0;
    size_t _used = 0;
    std::vector<std::unique_ptr<char[]>> _blocks;
    size_t _blockUsed = kBlock;   // the last block is full: the next small key opens one
};

// StoreRef's test (find_fastpath.cpp): does reference id `refId` get a slot in the dense id -> row
// table, whose size is the largest id stored? `refCount` is the factory's reference table count.
// A save's ids are 1..N, N its reference table's count (SerializeReferenceTable's write arm numbers
// them so); an id past the count, which only a crafted save holds, is not cached and its lookups take
// the faithful path. Review F-250, F-253: the old bound, 2^24, let one id size the table at about
// 400 MB.
inline bool RefIdCacheable(uint32_t refId, int32_t refCount) {
    return refCount > 0 && refId <= static_cast<uint32_t>(refCount);
}

}  // namespace FindFastpath
}  // namespace Ext
