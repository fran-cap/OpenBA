// ============================================================================
// tests/unit/ext_terrain_late_test.cpp -- ext.terrain_late's pure parts
// ============================================================================
// src/ext/terrain_late/terrain_late_rules.h: the move of the terrain items behind the other opaque
// items (a stable partition, checked against std::stable_partition on random lists), the late depth
// test (less-or-equal becomes less) and which terrain pass states may be moved at all.
// ============================================================================
#include "unit.h"
#include "ext/terrain_late/terrain_late_rules.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace {

using namespace Ext::TerrainLate;

// An item: `terrain` decides the match, `id` is its original position (so order is checkable).
struct Item {
    bool    terrain;
    int32_t id;
};

std::vector<Item> Make(const char* pattern) {
    std::vector<Item> v;
    for (int32_t i = 0; pattern[i] != '\0'; ++i) v.push_back({ pattern[i] == 'T', i });
    return v;
}

std::vector<Item> Reference(std::vector<Item> v) {
    std::stable_partition(v.begin(), v.end(), [](const Item& it) { return !it.terrain; });
    return v;
}

bool Same(const std::vector<Item>& a, const std::vector<Item>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id || a[i].terrain != b[i].terrain) return false;
    }
    return true;
}

int32_t Move(std::vector<Item>& v) {
    const int32_t n = static_cast<int32_t>(v.size());
    return MoveMatchingToBack(v.data(), n, [](const Item& it) { return it.terrain; });
}

}  // namespace

// The list retail's sort makes: the sky first, then the terrain's run, then the objects. The terrain
// run goes to the end in its own order; the sky and the objects keep theirs.
UNIT_TEST(ext_terrain_late_moves_the_terrain_run_last) {
    std::vector<Item> v = Make("STTTTOOOOO");
    CHECK(Move(v) == 4);
    const int32_t expect[] = { 0, 5, 6, 7, 8, 9, 1, 2, 3, 4 };
    for (int i = 0; i < 10; ++i) CHECK(v[static_cast<size_t>(i)].id == expect[i]);
}

// Nothing to move, everything to move, the empty list and a list already in order are left as they are.
UNIT_TEST(ext_terrain_late_leaves_lists_without_a_move) {
    std::vector<Item> none = Make("SOOO");
    CHECK(Move(none) == 0);
    CHECK(Same(none, Make("SOOO")));
    std::vector<Item> all = Make("TTT");
    CHECK(Move(all) == 3);
    CHECK(Same(all, Make("TTT")));
    std::vector<Item> empty;
    CHECK(Move(empty) == 0);
    std::vector<Item> done = Make("SOOTT");
    CHECK(Move(done) == 2);
    CHECK(Same(done, Make("SOOTT")));
}

// Several runs (two terrains in one scene, or a run split by an item of the same key): the result is
// std::stable_partition's, on fixed and on random lists.
UNIT_TEST(ext_terrain_late_is_a_stable_partition) {
    const char* const fixed[] = { "TOTOT", "OTTOOTO", "TTOTTOTT", "OTOTOTOTO", "T", "O" };
    for (const char* p : fixed) {
        std::vector<Item> v = Make(p);
        const std::vector<Item> want = Reference(v);
        Move(v);
        CHECK(Same(v, want));
    }
    std::mt19937 rng(20261114u);
    for (int trial = 0; trial < 2000; ++trial) {
        const int32_t n = static_cast<int32_t>(rng() % 64u);
        std::vector<Item> v;
        for (int32_t i = 0; i < n; ++i) v.push_back({ (rng() % 3u) == 0u, i });
        const std::vector<Item> want = Reference(v);
        int32_t terrain = 0;
        for (const Item& it : v) terrain += it.terrain ? 1 : 0;
        CHECK(Move(v) == terrain);
        CHECK(Same(v, want));
    }
}

// Less-or-equal becomes less (a depth tie stays the earlier object's); every other test is kept.
UNIT_TEST(ext_terrain_late_depth_test) {
    CHECK(LateDepthFunc(kFuncLessEqual) == kFuncLess);
    CHECK(LateDepthFunc(kFuncLess) == kFuncLess);
    for (uint32_t f = 0; f < 8; ++f) {
        if (f != kFuncLessEqual) CHECK(LateDepthFunc(f) == f);
    }
}

// Only a pass that tests and writes depth with less / less-or-equal and does not blend can move.
UNIT_TEST(ext_terrain_late_safe_states) {
    PassState retail;
    retail.depthTest  = true;
    retail.depthWrite = true;
    retail.depthFunc  = kFuncLessEqual;
    retail.blend      = false;
    CHECK(LateSafe(retail));
    PassState less = retail;
    less.depthFunc = kFuncLess;
    CHECK(LateSafe(less));
    PassState noTest = retail;
    noTest.depthTest = false;
    CHECK(!LateSafe(noTest));
    PassState noWrite = retail;
    noWrite.depthWrite = false;
    CHECK(!LateSafe(noWrite));
    PassState blended = retail;
    blended.blend = true;
    CHECK(!LateSafe(blended));
    for (uint32_t f = 0; f < 8; ++f) {
        PassState s = retail;
        s.depthFunc = f;
        CHECK(LateSafe(s) == (f == kFuncLess || f == kFuncLessEqual));
    }
}
