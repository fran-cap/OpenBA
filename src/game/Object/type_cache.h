#pragma once
// ============================================================================
// src/game/Object/type_cache.h -- Object::Base::Type() through a vptr-keyed cache (perf, identical answers)
//
// The sim's component and description walks (ComponentEntity::FindComponent<T>, the storage gate, the
// status icon's model lookup, StorageComponent::QueryCount, ...) ask every element its Type() (vtable
// slot 2) until one matches. Type() is `return _typeInfo.typeId` of the object's dynamic class
// (DECLARE_TYPE / DECLARE_EXE_TYPE; Base::Type answers 0): a pure function of the object's vptr. Its
// cost is the indirect call itself, which mispredicts along a walk over mixed classes (6% of the
// 50x stress step's main-thread samples sat on the first instruction of some Type() body).
//
// TypeOf(obj) answers the same id from a small direct-mapped table keyed by the vptr:
//  * The id a class answers is fixed once Factory::Startup has written it (~CRC32 of the name; the
//    ctor's 0xFFFFFFFF sentinel before). An entry is stored only for an id other than the sentinel,
//    so no entry ever goes stale, and a miss asks Type() itself.
//  * An entry is ONE 64-bit word, key in the high half, id in the low half, read and written with
//    relaxed atomics: a reader on any thread sees a whole entry or an older whole entry, never a
//    key from one class and an id from another.
//  * The key is the vptr's low 32 bits with bit 0 set (vtables are 8-aligned, so the key is unique
//    among the vtables whose high 32 bits equal the table's own; an empty entry, 0, never matches).
//    A vptr outside that 4 GB window (the image straddling a 4 GB line) is never cached: it asks
//    Type() every time.
// Nothing is written but the table, and Type() has no side effect, so every walk visits the same
// elements and returns the same pointer as retail's.
//
// The shadow (BANISHED_STRESS_COMPPROF=4): the stress harness sets g_verify and g_report, and every
// cache hit also asks Type() and reports the comparison ("TypeCache"; expect 0 mismatches). The hook
// is a function pointer so this header needs nothing from the harness (unit tests include it).
// ============================================================================

#include <atomic>
#include <cstdint>
#include "object/TypeInfo.h"   // Object::Base::Type

namespace TypeCache {

constexpr int kBits = 9;
constexpr uint32_t kEntries = 1u << kBits;
constexpr uint32_t kInvalidId = 0xFFFFFFFFu;   // BaseTypeInfo's id before Factory::Startup

// The table: zero-initialised (constant initialisation), so it is valid before any constructor runs.
inline std::atomic<uint64_t> g_table[kEntries];

// The COMPPROF=4 shadow (stress_harness.cpp sets both): every hit is checked against the slow answer.
inline int g_verify = 0;
inline void (*g_report)(bool ok, long long detail) = nullptr;

inline uint32_t SlotOf(uint32_t key) {
    return (key * 0x9E3779B1u) >> (32 - kBits);
}

// The core, over a vptr value and the slow answer (unit-tested on its own): `window` = the high
// 32 bits every cacheable vptr shares; `slow()` = the object's Type().
template <class Slow>
inline uint32_t Lookup(std::atomic<uint64_t>* table, uint64_t vptr, uint64_t window, Slow&& slow) {
    if ((vptr >> 32) != window) {
        return slow();
    }
    const uint64_t key = (vptr & 0xFFFFFFFFull) | 1u;
    std::atomic<uint64_t>& entry = table[SlotOf(static_cast<uint32_t>(key))];
    const uint64_t held = entry.load(std::memory_order_relaxed);
    if ((held >> 32) == key) {
        const uint32_t id = static_cast<uint32_t>(held);
        if (g_verify != 0) {
            const uint32_t real = slow();
            if (g_report != nullptr) g_report(real == id, static_cast<long long>(id) ^ real);
            return real;
        }
        return id;
    }
    const uint32_t id = slow();
    if (id != kInvalidId) {
        entry.store((key << 32) | id, std::memory_order_relaxed);
    }
    return id;
}

// The high 32 bits of this image's addresses (the table lives in the same image as every vtable).
inline uint64_t Window() {
    return reinterpret_cast<uint64_t>(&g_table[0]) >> 32;   // CAST-OK: ptr_int (the table's address window)
}

// obj->Type(), the same answer. No null check: Type() has none either (a null object faults on the
// vptr load, as the virtual call would).
inline uint32_t TypeOf(const Object::Base* obj) {
    const void* const vptr = *reinterpret_cast<const void* const*>(obj);   // CAST-OK: vslot (the object's vptr, the cache key)
    return Lookup(g_table, reinterpret_cast<uint64_t>(vptr), Window(),   // CAST-OK: ptr_int (the vptr's bits as the key)
                  [obj]() { return obj->Type(); });
}

// obj->IsDerivedFrom(id) for an object whose Type() is `id`: always true. Base::IsDerivedFrom is
// Factory::IsDerivedFrom(Type(), id), whose first test is `typeId == baseTypeId -> true`
// (src/engine/object/type_reflection.cpp), so after a Type() match it reads nothing more. The
// FindComponent walks narrow their match this way; the shadow asks the real call.
inline bool IsDerivedFromOwnType(const Object::Base* obj, uint32_t id) {
    if (g_verify != 0) {
        const bool real = obj->IsDerivedFrom(id);
        if (g_report != nullptr) g_report(real, static_cast<long long>(id));
        return real;
    }
    return true;
}

}  // namespace TypeCache
