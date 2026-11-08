// ============================================================================
// tests/unit/draw_sort_test.cpp -- DRAWSORT target 2: the exact keyed draw-item sort
// ============================================================================
// src/engine/graphics/draw_sort.h promises that DrawSort::SortFastExact leaves
// the item array bit for bit as retail's sort (DrawSort::SortReference) does, for
// every input, and that SortFast (SORTCMP's plain path) leaves it equal on every
// field the consumer reads (byte-equal unless a tie group of agreeing _maxInstances was
// permuted). These tests check the two halves of the proof and the whole:
//   * the key order IS the comparator: KeyLess(key(a), key(b)) == DrawItemLess(a, b)
//     and KeyEqual <=> neither sorts first, over millions of random pairs drawn
//     from small value pools (so every term ties often and the later terms
//     decide), floats including +-0, +-inf, denormals and extremes;
//   * SortKeyBits reverses the float order and merges +0 / -0;
//   * BuildKeys refuses a NaN sort key and a pointer at or above 2^48;
//   * whole lists of 2..4000 items, including full ties that differ only in
//     _maxInstances and the never-written pads, NaN lists and high pointers: SortFastExact's
//     array memcmp-equal to SortReference's;
//   * the shadow entry point reports a match and leaves the reference order;
//   * SORTCMP: the plain path on the same random lists is ConsumerEqual to the
//     reference item by item (byte-equal whenever the output has no tie); a
//     planted differing-_maxInstances tie makes it fall back and the output is byte-equal;
//     SortPlain leaves data untouched when it refuses; NaN lists stay inline; the
//     run / tie / instancing census on crafted lists.
//   * SORTMEMO: a memo hit (byte-equal input, or equal but for the pads) leaves
//     the bytes a fresh sort leaves, on random lists with ties, fallbacks and
//     inline-path lists; a one-bit change anywhere outside the pads is a miss and
//     inside them a permutation hit; another list in the same buffer cannot hit;
//     the table stays bounded and frees what stops being sorted; the shadow
//     catches a deliberately corrupted entry.
// ============================================================================
#include "unit.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "graphics/draw_sort.h"

namespace {

using Graphics::RendererDrawItem;

struct Rng {
    uint64_t s;
    uint64_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    uint32_t U32() { return static_cast<uint32_t>(Next() >> 32); }
    int Below(int n) { return static_cast<int>(U32() % static_cast<uint32_t>(n)); }
};

float FromBits(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }

float AnyKey(Rng& r, bool allowNaN) {
    switch (r.Below(allowNaN ? 16 : 15)) {
    case 0:  return 0.0f;
    case 1:  return -0.0f;
    case 2:  return std::numeric_limits<float>::infinity();
    case 3:  return -std::numeric_limits<float>::infinity();
    case 4:  return FromBits(r.U32() & 0x807FFFFFu);                   // denormal (or +-0)
    case 5:  return std::numeric_limits<float>::max();
    case 6:  return -std::numeric_limits<float>::max();
    case 7:  return std::numeric_limits<float>::denorm_min();
    case 8:  return 1.0f;
    case 9:  return -1.0f;
    case 10: { uint32_t b = r.U32(); if ((b & 0x7F800000u) == 0x7F800000u) b &= 0xBFFFFFFFu; return FromBits(b); }
    case 15: return FromBits(0x7FC00000u | (r.U32() & 0x003FFFFFu));   // NaN (only when allowed)
    default: return static_cast<float>(r.Below(8)) * 0.5f;            // a small pool: frequent ties
    }
}

template <class T>
void SetPtr(T*& field, uint64_t v) { std::memcpy(&field, &v, 8); }

// A pointer value from a pool of `pool` values below 2^48 (or, when `high`, one
// at or above 2^48 now and then).
uint64_t PtrVal(Rng& r, int pool, bool high) {
    if (high && r.Below(64) == 0) return 0x0001000000000000ull | (r.Next() & 0xFFFFFFFFull);
    const uint64_t base = 0x00007FF000000000ull;
    const int k = r.Below(pool);
    switch (k % 4) {
    case 0:  return base + static_cast<uint64_t>(k) * 0x40;
    case 1:  return static_cast<uint64_t>(k) * 0x10;                    // low addresses
    case 2:  return 0x0000FFFFFFFFFFF0ull - static_cast<uint64_t>(k);   // the top of the 48-bit range
    default: return (r.Next() & 0x0000FFFFFFFFFFFFull);
    }
}

RendererDrawItem RandomItem(Rng& r, int pool, bool allowNaN, bool high) {
    RendererDrawItem it;
    // Random bytes everywhere first: the pads (+0x24, +0x34) are never written
    // by retail's AddDrawItem, and the sort must carry whatever is there.
    uint8_t raw[sizeof(RendererDrawItem)];
    for (size_t i = 0; i < sizeof(raw); ++i) raw[i] = static_cast<uint8_t>(r.U32());
    std::memcpy(static_cast<void*>(&it), raw, sizeof(raw));
    SetPtr(it._geometry, PtrVal(r, pool, high));
    SetPtr(it._material, PtrVal(r, pool, high));
    SetPtr(it._instance, PtrVal(r, pool, high));
    SetPtr(it._userData, PtrVal(r, pool, high));
    it._flags   = r.Below(4) == 0 ? static_cast<uint16_t>(r.U32()) : static_cast<uint16_t>(0x8000 | r.Below(pool));
    it._node    = r.Below(4) == 0 ? static_cast<uint16_t>(r.U32()) : static_cast<uint16_t>(r.Below(pool));
    it._animationInstanceId   = r.Below(4) == 0 ? r.U32() : (r.Below(2) ? 0xFFFFFFFFu : static_cast<uint32_t>(r.Below(pool)));
    it._sortKey = AnyKey(r, allowNaN);
    it._maxInstances    = r.Below(3) == 0 ? r.Below(64) : 1;
    return it;
}

bool SameBytes(const std::vector<RendererDrawItem>& a, const std::vector<RendererDrawItem>& b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(RendererDrawItem)) == 0;
}

}  // namespace

UNIT_TEST(draw_sort_key_order_is_the_comparator) {
    Rng r{0x9E3779B97F4A7C15ull};
    long long pairs = 0, less = 0, equal = 0;
    // 500k pairs (2M before): ~10% are full ties, ~49k, well above the floor checked below.
    for (int round = 0; round < 500000; ++round) {
        const int pool = 1 + r.Below(r.Below(2) ? 3 : 40);
        RendererDrawItem it[2] = { RandomItem(r, pool, false, false), RandomItem(r, pool, false, false) };
        // Often copy some leading terms across so the later ones decide.
        if (r.Below(2)) {
            const int same = r.Below(9);
            RendererDrawItem& a = it[0];
            RendererDrawItem& b = it[1];
            if (same > 0) b._flags = a._flags;
            if (same > 1) b._sortKey = r.Below(4) ? a._sortKey : (a._sortKey == 0.0f ? -a._sortKey : a._sortKey);
            if (same > 2) b._material = a._material;
            if (same > 3) b._instance = a._instance;
            if (same > 4) b._node = a._node;
            if (same > 5) b._animationInstanceId = a._animationInstanceId;
            if (same > 6) b._userData = a._userData;
            if (same > 7) b._geometry = a._geometry;
        }
        DrawSort::Key k[2];
        bool nan = true, ptr = true;
        CHECK(DrawSort::BuildKeys(it, 2, k, &nan, &ptr));
        CHECK(!nan && !ptr);
        CHECK_EQ(DrawSort::KeyIndex(k[0]), 0u);
        CHECK_EQ(DrawSort::KeyIndex(k[1]), 1u);
        const bool ab = DrawSort::DrawItemLess(it[0], it[1]);
        const bool ba = DrawSort::DrawItemLess(it[1], it[0]);
        if (DrawSort::KeyLess(k[0], k[1]) != ab) { CHECK(false); break; }
        if (DrawSort::KeyLess(k[1], k[0]) != ba) { CHECK(false); break; }
        if (DrawSort::KeyEqual(k[0], k[1]) != (!ab && !ba)) { CHECK(false); break; }
        ++pairs; less += ab; equal += (!ab && !ba);
    }
    std::printf("  pairs %lld, less %lld, equal %lld\n", pairs, less, equal);
    CHECK(equal > 10000);   // full ties are exercised
}

UNIT_TEST(draw_sort_sort_key_bits_reverse_the_float_order) {
    Rng r{12345};
    for (int i = 0; i < 2000000; ++i) {
        const float x = AnyKey(r, false), y = AnyKey(r, false);
        const uint32_t bx = DrawSort::SortKeyBits(x), by = DrawSort::SortKeyBits(y);
        if ((x > y) != (bx < by) || (y > x) != (by < bx) || ((!(x > y) && !(y > x)) != (bx == by))) {
            CHECK(false);
            break;
        }
    }
    CHECK_EQ(DrawSort::SortKeyBits(0.0f), DrawSort::SortKeyBits(-0.0f));
    CHECK(DrawSort::SortKeyBits(std::numeric_limits<float>::infinity()) <
          DrawSort::SortKeyBits(std::numeric_limits<float>::max()));
    CHECK(DrawSort::SortKeyBits(-std::numeric_limits<float>::infinity()) >
          DrawSort::SortKeyBits(-std::numeric_limits<float>::max()));
}

UNIT_TEST(draw_sort_build_keys_refuses_nan_and_high_pointers) {
    Rng r{777};
    RendererDrawItem it[3] = { RandomItem(r, 4, false, false), RandomItem(r, 4, false, false),
                               RandomItem(r, 4, false, false) };
    DrawSort::Key k[3];
    bool nan = true, ptr = true;
    CHECK(DrawSort::BuildKeys(it, 3, k, &nan, &ptr));
    it[1]._sortKey = std::numeric_limits<float>::quiet_NaN();
    CHECK(!DrawSort::BuildKeys(it, 3, k, &nan, &ptr));
    CHECK(nan && !ptr);
    it[1]._sortKey = 1.0f;
    SetPtr(it[2]._userData, 0x0001000000000000ull);
    CHECK(!DrawSort::BuildKeys(it, 3, k, &nan, &ptr));
    CHECK(!nan && ptr);
    SetPtr(it[2]._userData, 0x0000FFFFFFFFFFFFull);
    CHECK(DrawSort::BuildKeys(it, 3, k, &nan, &ptr));
}

UNIT_TEST(draw_sort_fast_equals_reference_bit_for_bit) {
    Rng r{0xD1B54A32D192ED03ull};
    int lists = 0, nanLists = 0, highLists = 0;
    long long items = 0;
    for (int round = 0; round < 1500; ++round) {
        int n;
        switch (r.Below(4)) {
        case 0:  n = 2 + r.Below(40); break;
        case 1:  n = 2 + r.Below(300); break;
        case 2:  n = 300 + r.Below(1500); break;
        default: n = 1000 + r.Below(3000); break;
        }
        const int pool = 1 + r.Below(r.Below(3) ? 6 : 200);
        const bool allowNaN = r.Below(8) == 0;
        const bool high = r.Below(8) == 0;
        std::vector<RendererDrawItem> in(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) in[static_cast<size_t>(i)] = RandomItem(r, pool, allowNaN, high);
        // Full ties that differ only in _maxInstances and the pads: copies of earlier items.
        const int dups = r.Below(n / 2 + 1);
        for (int d = 0; d < dups; ++d) {
            RendererDrawItem c = in[static_cast<size_t>(r.Below(n))];
            c._maxInstances = r.Below(64);
            c._pad24 = static_cast<int32_t>(r.U32());
            c._pad34 = static_cast<int32_t>(r.U32());
            in[static_cast<size_t>(r.Below(n))] = c;
        }
        // Sometimes a presorted or reverse-sorted input (the skeleton's worst shapes).
        if (r.Below(10) == 0) DrawSort::SortReference(in.data(), n);
        if (r.Below(10) == 0) {
            DrawSort::SortReference(in.data(), n);
            for (int i = 0; i < n / 2; ++i) std::swap(in[static_cast<size_t>(i)], in[static_cast<size_t>(n - 1 - i)]);
        }
        std::vector<RendererDrawItem> ref = in, fast = in;
        DrawSort::SortReference(ref.data(), n);
        DrawSort::SortFastExact(fast.data(), n);
        if (!SameBytes(ref, fast)) {
            std::printf("  mismatch: round %d n %d pool %d nan %d high %d\n", round, n, pool, allowNaN, high);
            CHECK(false);
            break;
        }
        ++lists; items += n; nanLists += allowNaN; highLists += high;
    }
    std::printf("  lists %d (nan %d, high pointers %d), items %lld: SortFastExact == SortReference\n",
                lists, nanLists, highLists, items);
}

UNIT_TEST(draw_sort_shadow_reports_and_counts) {
    Rng r{4242};
    static int s_ok = 0, s_bad = 0;
    static long long s_detail = -1;
    DrawSort::g_report = [](bool ok, long long detail) { if (ok) ++s_ok; else ++s_bad; s_detail = detail; };
    DrawSort::g_verify = 1;
    DrawSort::g_profOn = 1;
    DrawSort::TakeCounters();
    std::vector<RendererDrawItem> in(500);
    for (auto& it : in) it = RandomItem(r, 5, false, false);
    in[10] = in[20]; in[10]._maxInstances = 2; in[20]._maxInstances = 3;   // a full tie with differing _maxInstances
    std::vector<RendererDrawItem> ref = in;
    DrawSort::SortReference(ref.data(), 500);
    DrawSort::Sort(in.data(), 500);
    const DrawSort::Counters c = DrawSort::TakeCounters();
    DrawSort::g_verify = 0;
    DrawSort::g_profOn = 0;
    DrawSort::g_report = nullptr;
    CHECK_EQ(s_ok, 1);
    CHECK_EQ(s_bad, 0);
    CHECK_EQ(s_detail, 500ll << 32);
    CHECK(SameBytes(in, ref));
    CHECK_EQ(c.sorts, 1ull);
    CHECK_EQ(c.items, 500ull);
    CHECK_EQ(c.keyed, 1ull);
    CHECK(c.compares > 500ull);
    CHECK(c.ties >= 1ull);
    CHECK(c.tiesArg8 >= 1ull);
    // SORTCMP: the list took the plain path and fell back on the differing-_maxInstances tie.
    CHECK_EQ(c.plainLists, DrawSort::kPlainSort ? 1ull : 0ull);
    CHECK_EQ(c.fallbacks, DrawSort::kPlainSort ? 1ull : 0ull);
    CHECK_EQ(c.shadowBytes, 0ull);
}

// ============================================================================
// SORTCMP: the plain sort.
// ============================================================================
namespace {

bool ConsumerSame(const std::vector<RendererDrawItem>& a, const std::vector<RendererDrawItem>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!DrawSort::ConsumerEqual(a[i], b[i])) return false;
    return true;
}

bool HasTie(const std::vector<RendererDrawItem>& sorted) {
    for (size_t i = 1; i < sorted.size(); ++i)
        if (!DrawSort::DrawItemLess(sorted[i - 1], sorted[i]) && !DrawSort::DrawItemLess(sorted[i], sorted[i - 1]))
            return true;
    return false;
}

bool HasArg8Tie(const std::vector<RendererDrawItem>& sorted) {
    for (size_t i = 1; i < sorted.size(); ++i)
        if (!DrawSort::DrawItemLess(sorted[i - 1], sorted[i]) && !DrawSort::DrawItemLess(sorted[i], sorted[i - 1]) &&
            sorted[i - 1]._maxInstances != sorted[i]._maxInstances)
            return true;
    return false;
}

// A list shaped like a mesh gather: `meshes` prefix groups (material / instance /
// userData / arg5 shared), each item its own geometry; sort key 0, arg8 per group.
std::vector<RendererDrawItem> GatherLike(Rng& r, int n, int meshes) {
    std::vector<RendererDrawItem> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        RendererDrawItem& it = v[static_cast<size_t>(i)];
        uint8_t raw[sizeof(RendererDrawItem)];
        for (size_t b = 0; b < sizeof(raw); ++b) raw[b] = static_cast<uint8_t>(r.U32());
        std::memcpy(static_cast<void*>(&it), raw, sizeof(raw));
        const int m = r.Below(meshes);
        SetPtr(it._geometry, 0x00000200A0000000ull + static_cast<uint64_t>(i) * 0x120);
        SetPtr(it._material, 0x00000200B0000000ull + static_cast<uint64_t>(m % 7) * 0x80);
        SetPtr(it._instance, 0x00000200C0000000ull + static_cast<uint64_t>(m) * 0x60);
        SetPtr(it._userData, 0x00000200D0000000ull + static_cast<uint64_t>(m / 3) * 0x200);
        it._flags   = static_cast<uint16_t>(m % 3 == 0 ? 0x8000 : 0);
        it._node    = static_cast<uint16_t>(m % 4);
        it._animationInstanceId   = 0xFFFFFFFFu;
        it._sortKey = 0.0f;
        it._maxInstances    = (m % 2) ? 10 : 1;
    }
    return v;
}

}  // namespace

UNIT_TEST(draw_sort_plain_is_consumer_exact) {
    Rng r{0xA0761D6478BD642Full};
    int lists = 0, permuted = 0, withTies = 0;
    long long items = 0;
    for (int round = 0; round < 1500; ++round) {
        int n;
        switch (r.Below(4)) {
        case 0:  n = 2 + r.Below(40); break;
        case 1:  n = 2 + r.Below(300); break;
        case 2:  n = 300 + r.Below(1500); break;
        default: n = 1000 + r.Below(3000); break;
        }
        std::vector<RendererDrawItem> in;
        if (r.Below(3) == 0) {
            in = GatherLike(r, n, 1 + r.Below(r.Below(2) ? 20 : 400));
        } else {
            const int pool = 1 + r.Below(r.Below(3) ? 6 : 200);
            in.resize(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) in[static_cast<size_t>(i)] = RandomItem(r, pool, r.Below(16) == 0, r.Below(16) == 0);
        }
        // Planted full ties: copies of earlier items with fresh pads, and (sometimes)
        // a fresh _maxInstances or a sort key of the other zero sign.
        const int dups = r.Below(n / 3 + 1);
        for (int d = 0; d < dups; ++d) {
            RendererDrawItem c = in[static_cast<size_t>(r.Below(n))];
            if (r.Below(6) == 0) c._maxInstances = r.Below(64);
            if (c._sortKey == 0.0f && r.Below(2)) c._sortKey = -c._sortKey;
            c._pad24 = static_cast<int32_t>(r.U32());
            c._pad34 = static_cast<int32_t>(r.U32());
            in[static_cast<size_t>(r.Below(n))] = c;
        }
        if (r.Below(10) == 0) DrawSort::SortReference(in.data(), n);
        std::vector<RendererDrawItem> ref = in, fast = in;
        DrawSort::SortReference(ref.data(), n);
        DrawSort::SortFast(fast.data(), n);
        bool ok = ConsumerSame(ref, fast);
        // A list whose output has no tie is byte-equal; so is one with a
        // differing-_maxInstances tie (it fell back to the keyed skeleton).
        if (ok && (!HasTie(ref) || HasArg8Tie(ref))) ok = SameBytes(ref, fast);
        if (!ok) {
            std::printf("  mismatch: round %d n %d\n", round, n);
            CHECK(false);
            break;
        }
        withTies += HasTie(ref);
        permuted += !SameBytes(ref, fast);
        ++lists; items += n;
    }
    std::printf("  lists %d, items %lld, with ties %d, harmlessly permuted %d: SortFast ConsumerEqual SortReference\n",
                lists, items, withTies, permuted);
    if (DrawSort::kPlainSort) CHECK(permuted > 0);   // the harmless-permutation case is exercised
}

UNIT_TEST(draw_sort_plain_falls_back_on_differing_arg8) {
    Rng r{99};
    DrawSort::g_profOn = 1;
    DrawSort::TakeCounters();
    // (a) a harmless tie group: same keys and _maxInstances, different pads -> plain, no fallback.
    std::vector<RendererDrawItem> in = GatherLike(r, 600, 50);
    for (int k = 0; k < 4; ++k) {
        RendererDrawItem c = in[100];
        c._pad24 = 1000 + k;
        c._pad34 = 2000 + k;
        in[static_cast<size_t>(200 + k * 50)] = c;
    }
    std::vector<RendererDrawItem> ref = in, out = in;
    DrawSort::SortReference(ref.data(), 600);
    DrawSort::Sort(out.data(), 600);
    DrawSort::Counters c = DrawSort::TakeCounters();
    CHECK(ConsumerSame(ref, out));
    CHECK_EQ(c.keyed, 1ull);
    CHECK_EQ(c.plainLists, DrawSort::kPlainSort ? 1ull : 0ull);
    CHECK_EQ(c.fallbacks, 0ull);
    CHECK_EQ(c.ties, 4ull);          // five equal keys: four adjacent pairs
    CHECK_EQ(c.tiesArg8, 0ull);
    CHECK_EQ(c.maxTie, 5ull);
    // (b) the same list with one copy's _maxInstances changed -> fallback, byte-equal.
    in[300]._maxInstances = in[100]._maxInstances + 1;
    ref = in; out = in;
    DrawSort::SortReference(ref.data(), 600);
    DrawSort::Sort(out.data(), 600);
    c = DrawSort::TakeCounters();
    CHECK(SameBytes(ref, out));
    CHECK_EQ(c.fallbacks, DrawSort::kPlainSort ? 1ull : 0ull);
    CHECK(c.tiesArg8 >= 1ull);
    // (c) a NaN sort key: the inlined skeleton, byte-equal, never plain.
    in[5]._sortKey = std::numeric_limits<float>::quiet_NaN();
    ref = in; out = in;
    DrawSort::SortReference(ref.data(), 600);
    DrawSort::Sort(out.data(), 600);
    c = DrawSort::TakeCounters();
    CHECK(SameBytes(ref, out));
    CHECK_EQ(c.nanLists, 1ull);
    CHECK_EQ(c.plainLists, 0ull);
    DrawSort::g_profOn = 0;
}

UNIT_TEST(draw_sort_plain_refusal_leaves_data_and_keys) {
    Rng r{2024};
    std::vector<RendererDrawItem> in = GatherLike(r, 300, 30);
    in[250] = in[10];
    in[250]._maxInstances = in[10]._maxInstances + 3;
    std::vector<DrawSort::Key> keys(300), keys0;
    bool nan = true, ptr = true;
    CHECK(DrawSort::BuildKeys(in.data(), 300, keys.data(), &nan, &ptr));
    keys0 = keys;
    const std::vector<RendererDrawItem> before = in;
    uint64_t cmp = 0;
    uint32_t groups = 0;
    CHECK(!DrawSort::SortPlain(in.data(), 300, keys.data(), &cmp, &groups));
    CHECK(SameBytes(in, before));
    CHECK(std::memcmp(keys.data(), keys0.data(), keys.size() * sizeof(DrawSort::Key)) == 0);
    CHECK(groups >= 1u && groups <= 30u);
    // Without the differing tie it sorts: every adjacent pair in order.
    in[250]._maxInstances = in[10]._maxInstances;
    CHECK(DrawSort::SortPlain(in.data(), 300, keys.data(), &cmp, &groups));
    for (size_t i = 1; i < in.size(); ++i) CHECK(!DrawSort::DrawItemLess(in[i], in[i - 1]));
    std::vector<RendererDrawItem> ref = before;
    ref[250]._maxInstances = ref[10]._maxInstances;
    DrawSort::SortReference(ref.data(), 300);
    CHECK(ConsumerSame(ref, in));
}

UNIT_TEST(draw_sort_census_runs_and_instancing) {
    Rng r{31337};
    DrawSort::g_profOn = 1;
    DrawSort::TakeCounters();
    // Distinct keys (one geometry each), presorted: one ascending run.
    std::vector<RendererDrawItem> in = GatherLike(r, 400, 40);
    DrawSort::SortReference(in.data(), 400);
    std::vector<RendererDrawItem> work = in;
    DrawSort::Sort(work.data(), 400);
    DrawSort::Counters c = DrawSort::TakeCounters();
    CHECK_EQ(c.runs, 1ull);
    CHECK_EQ(c.ties, 0ull);
    uint64_t inst = 0;
    for (const RendererDrawItem& it : in) inst += it._maxInstances > 1 ? 1u : 0u;
    CHECK_EQ(c.instItems, inst);
    CHECK(inst > 0u);
    // Reversed: every key a run of its own.
    for (int i = 0; i < 200; ++i) std::swap(in[static_cast<size_t>(i)], in[static_cast<size_t>(399 - i)]);
    work = in;
    DrawSort::Sort(work.data(), 400);
    c = DrawSort::TakeCounters();
    CHECK_EQ(c.runs, 400ull);
    // The repeat census: the same buffer sorted again with the same key array is a hit
    // (the buffer `work` was last sorted holding `in`'s keys).
    work = in;
    DrawSort::Sort(work.data(), 400);
    c = DrawSort::TakeCounters();
    CHECK_EQ(c.repeatChecks, 1ull);
    CHECK_EQ(c.repeatHits, 1ull);
    // ...and a different key array in that buffer is a miss.
    std::swap(in[0], in[1]);
    work = in;
    DrawSort::Sort(work.data(), 400);
    c = DrawSort::TakeCounters();
    CHECK_EQ(c.repeatChecks, 1ull);
    CHECK_EQ(c.repeatHits, 0ull);
    DrawSort::g_profOn = 0;
}

// ============================================================================
// SORTMEMO: the memo of the previous sort of a buffer.
// ============================================================================
namespace {

// Is the list keyable (the paths that leave a permutation)?
bool Keyable(const std::vector<RendererDrawItem>& v) {
    if (v.size() < static_cast<size_t>(DrawSort::kKeyedMinCount)) return false;
    std::vector<DrawSort::Key> k(v.size());
    bool nan = false, ptr = false;
    return DrawSort::BuildKeys(v.data(), static_cast<int32_t>(v.size()), k.data(), &nan, &ptr);
}

// A fresh sort of a copy (no memo): what the memo must reproduce byte for byte.
std::vector<RendererDrawItem> Fresh(const std::vector<RendererDrawItem>& in) {
    std::vector<RendererDrawItem> out = in;
    DrawSort::SortFastNoMemo(out.data(), static_cast<int32_t>(out.size()));
    return out;
}

// Copy `src` into `buf` in place (same size: the buffer, and so the memo's key, stays).
void Refill(std::vector<RendererDrawItem>& buf, const std::vector<RendererDrawItem>& src) {
    const RendererDrawItem* const before = buf.data();
    buf = src;
    CHECK(buf.data() == before);
}

void RandomPads(Rng& r, std::vector<RendererDrawItem>& v, int changes) {
    for (int k = 0; k < changes; ++k) {
        RendererDrawItem& it = v[static_cast<size_t>(r.Below(static_cast<int>(v.size())))];
        if (r.Below(2)) it._pad24 = static_cast<int32_t>(r.U32()); else it._pad34 = static_cast<int32_t>(r.U32());
    }
}

}  // namespace

UNIT_TEST(draw_sort_memo_hit_is_a_fresh_sort) {
    if constexpr (!DrawSort::kSortMemo) UNIT_SKIP("kSortMemo off");
    Rng r{0x5EED50A7ull};
    DrawSort::MemoClear();
    DrawSort::g_profOn = 1;
    DrawSort::TakeCounters();
    int lists = 0, withTies = 0, fallbacks = 0, inlineLists = 0;
    uint64_t hits = 0, padHits = 0, padMisses = 0;
    for (int round = 0; round < 400; ++round) {
        const int n = r.Below(3) ? 32 + r.Below(600) : 600 + r.Below(3400);
        std::vector<RendererDrawItem> in;
        if (r.Below(2)) {
            in = GatherLike(r, n, 1 + r.Below(r.Below(2) ? 20 : 400));
        } else {
            const int pool = 1 + r.Below(r.Below(3) ? 6 : 200);
            in.resize(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) in[static_cast<size_t>(i)] = RandomItem(r, pool, r.Below(16) == 0, r.Below(16) == 0);
        }
        // Planted full ties with fresh pads: harmless ones (same _maxInstances) and, in a
        // quarter of the lists, one whose _maxInstances differs (SORTCMP's fallback).
        const int dups = r.Below(n / 4 + 1);
        for (int d = 0; d < dups; ++d) {
            RendererDrawItem c = in[static_cast<size_t>(r.Below(n))];
            c._pad24 = static_cast<int32_t>(r.U32());
            c._pad34 = static_cast<int32_t>(r.U32());
            in[static_cast<size_t>(r.Below(n))] = c;
        }
        if (r.Below(4) == 0) {
            RendererDrawItem c = in[0];
            c._maxInstances = in[0]._maxInstances + 1;
            in[static_cast<size_t>(1 + r.Below(n - 1))] = c;
        }
        const bool keyable = Keyable(in);
        // First sort of this buffer: a store.
        std::vector<RendererDrawItem> buf = in;
        DrawSort::Sort(buf.data(), n);
        DrawSort::Counters c = DrawSort::TakeCounters();
        bool ok = SameBytes(buf, Fresh(in)) && c.memoStores == 1u && c.memoHits == 0u;
        fallbacks += c.fallbacks != 0u;
        withTies += c.ties != 0u;
        inlineLists += !keyable;
        // The same input again: a byte-equal hit, the bytes a fresh sort leaves.
        Refill(buf, in);
        DrawSort::Sort(buf.data(), n);
        c = DrawSort::TakeCounters();
        ok = ok && c.memoHits == 1u && c.memoStores == 0u && SameBytes(buf, Fresh(in));
        hits += c.memoHits;
        // The pads changed: a permutation hit on a keyable list (an inline-path
        // list has no permutation: a miss), the bytes a fresh sort leaves, fresh pads
        // included.
        std::vector<RendererDrawItem> in2 = in;
        RandomPads(r, in2, 1 + r.Below(n));
        Refill(buf, in2);
        DrawSort::Sort(buf.data(), n);
        c = DrawSort::TakeCounters();
        ok = ok && SameBytes(buf, Fresh(in2));
        if (keyable) ok = ok && c.memoPadHits == 1u && c.memoStores == 0u;
        else         ok = ok && c.memoMisses == 1u && c.memoStores == 1u;
        padHits += c.memoPadHits;
        padMisses += c.memoMisses;
        if (!ok) {
            std::printf("  mismatch: round %d n %d keyable %d\n", round, n, keyable);
            CHECK(false);
            break;
        }
        ++lists;
    }
    DrawSort::g_profOn = 0;
    DrawSort::MemoClear();
    std::printf("  lists %d (with ties %d, fallbacks %d, inline %d): hits %llu, pad hits %llu, pad misses %llu,"
                " all byte-equal to a fresh sort\n", lists, withTies, fallbacks, inlineLists,
                (unsigned long long)hits, (unsigned long long)padHits, (unsigned long long)padMisses);
    CHECK(fallbacks > 0 || !DrawSort::kPlainSort);
    CHECK(withTies > 0);
    CHECK(inlineLists > 0);
}

UNIT_TEST(draw_sort_memo_one_byte_forces_a_miss) {
    if constexpr (!DrawSort::kSortMemo) UNIT_SKIP("kSortMemo off");
    Rng r{0xB17EB17Eull};
    DrawSort::MemoClear();
    DrawSort::g_profOn = 1;
    const int n = 300;
    int misses = 0, padHits = 0;
    for (int list = 0; list < 2; ++list) {
        std::vector<RendererDrawItem> in;
        if (list == 0) {
            in = GatherLike(r, n, 25);
        } else {
            in.resize(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) in[static_cast<size_t>(i)] = RandomItem(r, 6, false, false);
        }
        std::vector<RendererDrawItem> buf = in;
        DrawSort::Sort(buf.data(), n);
        DrawSort::TakeCounters();
        for (size_t off = 0; off < sizeof(RendererDrawItem); ++off) {
            for (int trial = 0; trial < 4; ++trial) {
                std::vector<RendererDrawItem> in2 = in;
                RendererDrawItem& victim = in2[static_cast<size_t>(r.Below(n))];
                unsigned char raw[sizeof(RendererDrawItem)];
                std::memcpy(raw, static_cast<const void*>(&victim), sizeof(raw));
                raw[off] = static_cast<unsigned char>(raw[off] ^ (1u << r.Below(8)));
                std::memcpy(static_cast<void*>(&victim), raw, sizeof(raw));
                const bool pad = (off >= 0x24 && off < 0x28) || (off >= 0x34 && off < 0x38);
                Refill(buf, in2);
                DrawSort::Sort(buf.data(), n);
                const DrawSort::Counters c = DrawSort::TakeCounters();
                CHECK(SameBytes(buf, Fresh(in2)));
                CHECK_EQ(c.memoHits, 0ull);
                if (pad) { CHECK_EQ(c.memoPadHits, 1ull); padHits += static_cast<int>(c.memoPadHits); }
                else     { CHECK_EQ(c.memoMisses, 1ull); misses += static_cast<int>(c.memoMisses); }
                // Restore the entry to `in` (a pad hit left it there already).
                Refill(buf, in);
                DrawSort::Sort(buf.data(), n);
                DrawSort::TakeCounters();
            }
        }
        // A shorter list in the same buffer (count differs): a miss.
        std::vector<RendererDrawItem> shorter(in.begin(), in.end() - 1);
        std::memcpy(static_cast<void*>(buf.data()), shorter.data(), shorter.size() * sizeof(RendererDrawItem));
        DrawSort::Sort(buf.data(), n - 1);
        const DrawSort::Counters c = DrawSort::TakeCounters();
        CHECK_EQ(c.memoMisses, 1ull);
        std::vector<RendererDrawItem> head(buf.begin(), buf.end() - 1);
        CHECK(SameBytes(head, Fresh(shorter)));
    }
    DrawSort::g_profOn = 0;
    DrawSort::MemoClear();
    std::printf("  one-bit changes: %d misses (non-pad bytes), %d pad hits (pad bytes)\n", misses, padHits);
    CHECK_EQ(misses, 2 * 48 * 4);
    CHECK_EQ(padHits, 2 * 8 * 4);
}

UNIT_TEST(draw_sort_memo_buffer_reuse_cannot_hit) {
    if constexpr (!DrawSort::kSortMemo) UNIT_SKIP("kSortMemo off");
    Rng r{0xC0FFEEull};
    DrawSort::MemoClear();
    DrawSort::g_profOn = 1;
    const int n = 500;
    // List X sorted in the buffer; then list Y (another list, same count) in the same
    // buffer, as when a freed buffer is handed to another group: a miss, sorted.
    const std::vector<RendererDrawItem> x = GatherLike(r, n, 40);
    const std::vector<RendererDrawItem> y = GatherLike(r, n, 40);
    std::vector<RendererDrawItem> buf = x;
    DrawSort::Sort(buf.data(), n);
    DrawSort::TakeCounters();
    Refill(buf, y);
    DrawSort::Sort(buf.data(), n);
    DrawSort::Counters c = DrawSort::TakeCounters();
    CHECK_EQ(c.memoHits + c.memoPadHits, 0ull);
    CHECK_EQ(c.memoMisses, 1ull);
    CHECK(SameBytes(buf, Fresh(y)));
    // Y with X's first item (equal prefix of one item): still a miss.
    std::vector<RendererDrawItem> y2 = y;
    y2[0] = x[0];
    Refill(buf, y2);
    DrawSort::Sort(buf.data(), n);
    c = DrawSort::TakeCounters();
    CHECK_EQ(c.memoHits + c.memoPadHits, 0ull);
    CHECK(SameBytes(buf, Fresh(y2)));
    // X in another buffer: a new entry, then a hit there.
    std::vector<RendererDrawItem> other = x;
    DrawSort::Sort(other.data(), n);
    c = DrawSort::TakeCounters();
    CHECK_EQ(c.memoNew, 1ull);
    Refill(other, x);
    DrawSort::Sort(other.data(), n);
    c = DrawSort::TakeCounters();
    CHECK_EQ(c.memoHits, 1ull);
    CHECK(SameBytes(other, Fresh(x)));
    // The bound: 400 distinct buffers never hold more than kMemoMaxEntries entries...
    DrawSort::MemoClear();
    std::vector<std::vector<RendererDrawItem>> many(400);
    for (auto& v : many) {
        v = GatherLike(r, 40, 5);
        DrawSort::Sort(v.data(), 40);
        CHECK(DrawSort::MemoEntries() <= DrawSort::kMemoMaxEntries);
    }
    // ...and the ones that stop being sorted are freed by the sweep.
    std::vector<RendererDrawItem> one = GatherLike(r, 40, 5);
    const std::vector<RendererDrawItem> oneIn = one;
    for (uint64_t k = 0; k < DrawSort::kMemoSweepEvery + DrawSort::kMemoMaxAge + 1; ++k) {
        Refill(one, oneIn);
        DrawSort::Sort(one.data(), 40);
    }
    CHECK_EQ(DrawSort::MemoEntries(), static_cast<size_t>(1));
    c = DrawSort::TakeCounters();
    CHECK(c.memoHits >= DrawSort::kMemoSweepEvery);
    DrawSort::g_profOn = 0;
    DrawSort::MemoClear();
}

UNIT_TEST(draw_sort_memo_shadow_catches_a_corrupt_entry) {
    if constexpr (!DrawSort::kSortMemo) {
        CHECK(!DrawSort::MemoCorruptForTest(nullptr));
        UNIT_SKIP("kSortMemo off");
    }
    Rng r{0xBADC0DEull};
    static int s_ok = 0, s_bad = 0;
    static long long s_detail = -1;
    DrawSort::MemoClear();
    DrawSort::g_report = [](bool ok, long long detail) { if (ok) ++s_ok; else ++s_bad; s_detail = detail; };
    DrawSort::g_verify = 1;
    DrawSort::g_profOn = 1;
    DrawSort::TakeCounters();
    const int n = 400;
    const std::vector<RendererDrawItem> in = GatherLike(r, n, 30);   // one geometry per item: no ties
    std::vector<RendererDrawItem> ref = in;
    DrawSort::SortReference(ref.data(), n);
    std::vector<RendererDrawItem> buf = in;
    DrawSort::Sort(buf.data(), n);                                    // store
    CHECK_EQ(s_ok, 1);
    CHECK_EQ(s_bad, 0);
    // A sound hit under the shadow is checked and matches.
    Refill(buf, in);
    DrawSort::Sort(buf.data(), n);
    CHECK_EQ(s_ok, 2);
    CHECK_EQ(s_bad, 0);
    CHECK(SameBytes(buf, ref));
    // The stored output corrupted (first and last records swapped): the hit is caught,
    // reported at item 0, and the list ends in retail's order.
    CHECK(DrawSort::MemoCorruptForTest(buf.data()));
    Refill(buf, in);
    DrawSort::Sort(buf.data(), n);
    CHECK_EQ(s_bad, 1);
    CHECK_EQ(s_detail, static_cast<long long>(n) << 32);
    CHECK(SameBytes(buf, ref));
    // The same for a permutation hit (pads changed; the stored permutation is corrupt too).
    std::vector<RendererDrawItem> in2 = in;
    RandomPads(r, in2, 50);
    std::vector<RendererDrawItem> ref2 = in2;
    DrawSort::SortReference(ref2.data(), n);
    Refill(buf, in2);
    DrawSort::Sort(buf.data(), n);
    const DrawSort::Counters c = DrawSort::TakeCounters();
    CHECK_EQ(s_bad, 2);
    CHECK_EQ(s_detail, static_cast<long long>(n) << 32);
    CHECK(SameBytes(buf, ref2));
    CHECK_EQ(c.memoHits, 2ull);
    CHECK_EQ(c.memoPadHits, 1ull);
    DrawSort::g_verify = 0;
    DrawSort::g_profOn = 0;
    DrawSort::g_report = nullptr;
    DrawSort::MemoClear();
}
