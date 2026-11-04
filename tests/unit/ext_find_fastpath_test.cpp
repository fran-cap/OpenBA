// ============================================================================
// tests/unit/ext_find_fastpath_test.cpp -- ext.find_fastpath's request table
//   (src/ext/find_fastpath/request_table.h, docs/extensions/handins/VISITREPLAY.md)
// ============================================================================
// The table against a std::map under random paths (shared prefixes, every
// length 0..96, case variants that must stay distinct), replaced values and
// generation changes; then Clear. The game's own answers are checked live by
// the debug exe (`[FindFastpath] MISMATCH`).
// ============================================================================
#include "unit.h"
#include "ext/find_fastpath/request_table.h"

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <utility>

using Ext::FindFastpath::RequestTable;

UNIT_TEST(ext_find_fastpath_request_table_matches_a_map) {
    RequestTable t;
    std::map<std::string, std::pair<uintptr_t, uint64_t>> oracle;
    std::mt19937 rng(20261024);
    const char* const stems[] = {"Build/Models/", "build/models/", "Dialog/", "Terrain/Tex/", ""};
    uint64_t gen = 1;
    auto make = [&]() {
        std::string s = stems[rng() % 5];
        const size_t n = rng() % 97;
        for (size_t i = 0; i < n; ++i)
            s.push_back(static_cast<char>('a' + rng() % 6));
        if (rng() % 3 == 0)
            s += ":resource";
        return s;
    };
    for (int step = 0; step < 60000; ++step) {
        const std::string k = make();
        const int op = static_cast<int>(rng() % 10);
        if (op < 5) {
            const uintptr_t v = (static_cast<uintptr_t>(rng()) << 4) | 8;
            t.Put(k.data(), k.size(), reinterpret_cast<void*>(v), gen);   // CAST-OK: test value
            oracle[k] = {v, gen};
        } else if (op == 5 && rng() % 50 == 0) {
            ++gen;
        }
        const std::string q = (op & 1) ? k : make();
        void* got = t.Find(q.data(), q.size(), gen);
        auto it = oracle.find(q);
        const void* want = (it != oracle.end() && it->second.second == gen)
                               ? reinterpret_cast<const void*>(it->second.first)   // CAST-OK: test value
                               : nullptr;
        CHECK(got == want);
    }
    CHECK(t.Size() == oracle.size());
    t.Clear();
    CHECK(t.Size() == 0u);
    CHECK(t.Find("Dialog/x", 8, gen) == nullptr);
    t.Put("Dialog/x", 8, reinterpret_cast<void*>(uintptr_t(16)), gen);   // CAST-OK: test value
    CHECK(t.Find("Dialog/x", 8, gen) == reinterpret_cast<void*>(uintptr_t(16)));   // CAST-OK: test value
    CHECK(t.Find("dialog/x", 8, gen) == nullptr);
    CHECK(t.Find("Dialog/x", 8, gen + 1) == nullptr);
}

// Review F-250 / F-253: StoreRef sized its id-indexed table by a reference id read from the save,
// bounded only at 2^24 (about 400 MB of 24-byte entries for one crafted id). A save's ids are
// 1..N, N its reference table's count (SerializeReferenceTable's write arm); nothing past N is
// cached, so the table never outgrows the save that filled it.
UNIT_TEST(ext_find_fastpath_ref_table_is_bounded_by_the_reference_count) {
    using Ext::FindFastpath::RefIdCacheable;
    CHECK(RefIdCacheable(1, 3));
    CHECK(RefIdCacheable(3, 3));
    CHECK(!RefIdCacheable(4, 3));
    CHECK(!RefIdCacheable(1u << 24, 3));
    CHECK(!RefIdCacheable(1u << 24, 1 << 20));
    CHECK(!RefIdCacheable(0xFFFFFFFEu, 0x7FFFFFFF));
    CHECK(!RefIdCacheable(1, 0));
    CHECK(!RefIdCacheable(1, -5));
}
