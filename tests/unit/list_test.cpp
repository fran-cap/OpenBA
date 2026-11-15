// ============================================================================
// tests/unit/list_test.cpp — ENDGAME B.3.2 step 1, the LIST family
// ============================================================================
//
// Two subjects, both of them code this batch turned into a template and
// neither of them coverable from the game run:
//
//   1. `Collection::LinkedList<T, Allocator>` (src/engine/collection/
//      Collection.h) — Add / Remove / Clear / GetCount / the iterator, driven
//      against the counting test allocator in src/engine/collection/
//      linkedlist.cpp. Retail ships ONE instantiation of this template and its
//      three bodies have NO oracle (linkedlist.cpp's header block: a Remove
//      replay unlinks an already-unlinked node and frees it twice), so these
//      assertions are the only mechanical check the family has.
//
//   2. The ported introsorts — `rtsort::SortByKey70` (retail 0x1800264a0, the
//      packfile sort, hand-inlined small-range pass) and the shared MSVC sort
//      skeleton `rtsort::Introsort` / `rtsort::SelectionSortMaxToHigh`
//      (src/engine/collection/introsort_msvc.h, retail 0x180062d60 /
//      0x180028760 / 0x180063b90 / 0x180028fc0).
//
//      THE POINT OF THE SORT TESTS IS THE TIE BEHAVIOUR, NOT THE SORTEDNESS.
//      COLLECTION_INVENTORY.md §5.7: "the packfile introsort is retail-
//      observable via tie behavior and must NOT be replaced by std::sort /
//      std::stable_sort" — on a tie-heavy population the exact swap sequence
//      decides which package owns a colliding path. "It came out sorted" is
//      true of a bubble sort, a std::sort and a broken transcription alike, so
//      the tie tests below compare the whole PERMUTATION against a golden table
//      recorded from the body as it stood at main e519898c, BEFORE this batch
//      touched anything, and only the large random test settles for ordering.
//
// REGENERATING THE GOLDEN TABLES. They are a recording of the current body, so
// a change that alters them is either a bug or a deliberate re-transcription
// that must be argued for in its own right. To re-record after such an
// argument, build this file with -DUNIT_DUMP_GOLDEN and paste what it prints:
//
//   g++ -std=c++17 -DBANISHED_UNIT_TEST -DUNIT_DUMP_GOLDEN -I. -Iruntime/types
//       -Iruntime/funcs -Itests/unit tests/unit/list_test.cpp
//       src/engine/collection/introsort.cpp
//       src/engine/collection/linkedlist.cpp -o dump.exe && ./dump.exe
// ============================================================================

#include "unit.h"

#include <cstdint>
#include <algorithm>
#include <vector>

#include "collection/Collection.h"   // rtsort::SortByKey70 (0x1800264a0)
#include "io/PackageFile.h"          // the sorted element
#include "collection/introsort_msvc.h"

// ---------------------------------------------------------------------------
// The two seams this file reaches into.
// ---------------------------------------------------------------------------
namespace Collection {
namespace detail {
// The test allocator's counters (src/engine/collection/linkedlist.cpp, only
// defined under BANISHED_UNIT_TEST). `g_listNodeDoubleFrees` is what makes
// "frees every node exactly once" an assertion rather than a hope: the test
// allocator stamps each block LIVE/DEAD and counts a second free instead of
// tolerating it.
extern uint64_t g_listNodeAllocs;
extern uint64_t g_listNodeFrees;
extern uint64_t g_listNodeDoubleFrees;
}  // namespace detail
}  // namespace Collection

namespace {

using List  = Collection::LinkedList<void*>;
using Elem  = void*;

Elem E(int n) { return reinterpret_cast<void*>(static_cast<uintptr_t>(n)); }
int  V(Elem e) { return static_cast<int>(reinterpret_cast<uintptr_t>(e)); }

// A zero-initialised list, the way retail's three embedders arrive: their own
// constructors write head/tail/count as explicit stores. `LinkedList` has no
// user-provided default ctor precisely so that `List l{};` still zeroes.
struct Fixture {
    List     list{};
    uint64_t allocs0, frees0, dbl0;
    Fixture()
        : allocs0(Collection::detail::g_listNodeAllocs),
          frees0(Collection::detail::g_listNodeFrees),
          dbl0(Collection::detail::g_listNodeDoubleFrees) {}
    uint64_t Allocs() const { return Collection::detail::g_listNodeAllocs - allocs0; }
    uint64_t Frees()  const { return Collection::detail::g_listNodeFrees - frees0; }
    uint64_t Doubles() const {
        return Collection::detail::g_listNodeDoubleFrees - dbl0;
    }
};

// head -> tail as a vector of element values.
std::vector<int> Forward(const List& l) {
    std::vector<int> out;
    for (List::Iterator it = l.GetFirst(); it; ++it) out.push_back(V(*it));
    return out;
}

// tail -> head through `prev`, which is what proves the back links were
// maintained (retail's CleanRemovedDialogs walks them).
std::vector<int> Backward(const List& l) {
    std::vector<int> out;
    for (List::Node* n = l._tail; n; n = n->prev) out.push_back(V(n->element));
    return out;
}

}  // namespace

// ============================================================================
// Collection::LinkedList
// ============================================================================

UNIT_TEST(LinkedListEmptyListEdges) {
    Fixture f;
    CHECK_EQ(f.list.GetCount(), 0);
    CHECK(f.list.GetFirst().node == nullptr);
    CHECK(f.list.GetLast().node == nullptr);
    CHECK(!f.list.GetFirst());

    // A miss on an empty list is SILENT: retail's `MOV R8,[RCX] / TEST / JZ ret`
    // means no free, no count change (linkedlist.cpp, Remove note 3).
    f.list.Remove(E(7));
    CHECK_EQ(f.list.GetCount(), 0);
    CHECK_EQ(f.Allocs(), 0u);
    CHECK_EQ(f.Frees(), 0u);

    // The three Clear stores are UNCONDITIONAL — they run on an already-empty
    // list too — and `_pad14` is NOT written.
    f.list._pad14 = 0x5a5a5a5a;
    f.list.Clear();
    CHECK_EQ(f.list.GetCount(), 0);
    CHECK(f.list._head == nullptr);
    CHECK(f.list._tail == nullptr);
    CHECK_EQ(f.list._pad14, static_cast<int32_t>(0x5a5a5a5a));
    CHECK_EQ(f.Frees(), 0u);

    f.list.Clear();
}

UNIT_TEST(LinkedListAddKeepsInsertionOrderBothWays) {
    Fixture f;
    for (int i = 1; i <= 5; ++i) f.list.Add(E(i));

    CHECK_EQ(f.list.GetCount(), 5);
    CHECK_EQ(f.Allocs(), 5u);

    const std::vector<int> fwd = Forward(f.list);
    const std::vector<int> bwd = Backward(f.list);
    CHECK_EQ(fwd.size(), 5u);
    CHECK_EQ(bwd.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(fwd[static_cast<size_t>(i)], i + 1);       // Add appends at the tail
        CHECK_EQ(bwd[static_cast<size_t>(i)], 5 - i);       // prev links agree
    }
    // The first Add on an empty list takes the `tail == null` arm and must set
    // BOTH head and tail (retail 0x180120490).
    CHECK_EQ(V(f.list._head->element), 1);
    CHECK_EQ(V(f.list._tail->element), 5);
    CHECK(f.list._head->prev == nullptr);
    CHECK(f.list._tail->next == nullptr);

    f.list.Clear();
}

UNIT_TEST(LinkedListRemoveByValueFirstMatchOnly) {
    // Retail's search loads the value ONCE and stops on the first equal
    // element, then returns after the single free without resuming
    // (0x18007a01e / the free at 0x18007a010's tail JMP). A list holding the
    // same value twice therefore KEEPS THE SECOND.
    Fixture f;
    f.list.Add(E(1));
    f.list.Add(E(9));
    f.list.Add(E(2));
    f.list.Add(E(9));
    f.list.Add(E(3));

    f.list.Remove(E(9));

    CHECK_EQ(f.list.GetCount(), 4);
    CHECK_EQ(f.Frees(), 1u);
    const std::vector<int> fwd = Forward(f.list);
    const int want[] = {1, 2, 9, 3};
    CHECK_EQ(fwd.size(), 4u);
    for (size_t i = 0; i < fwd.size() && i < 4; ++i) CHECK_EQ(fwd[i], want[i]);
    // The back links survive an interior unlink.
    const std::vector<int> bwd = Backward(f.list);
    const int wantB[] = {3, 9, 2, 1};
    CHECK_EQ(bwd.size(), 4u);
    for (size_t i = 0; i < bwd.size() && i < 4; ++i) CHECK_EQ(bwd[i], wantB[i]);

    f.list.Clear();
    CHECK_EQ(f.Doubles(), 0u);
}

UNIT_TEST(LinkedListRemoveHeadTailAndOnly) {
    {   // head
        Fixture f;
        f.list.Add(E(1)); f.list.Add(E(2)); f.list.Add(E(3));
        f.list.Remove(E(1));
        CHECK_EQ(f.list.GetCount(), 2);
        CHECK_EQ(V(f.list._head->element), 2);
        CHECK(f.list._head->prev == nullptr);
        CHECK_EQ(V(f.list._tail->element), 3);
        f.list.Clear();
    }
    {   // tail
        Fixture f;
        f.list.Add(E(1)); f.list.Add(E(2)); f.list.Add(E(3));
        f.list.Remove(E(3));
        CHECK_EQ(f.list.GetCount(), 2);
        CHECK_EQ(V(f.list._tail->element), 2);
        CHECK(f.list._tail->next == nullptr);
        f.list.Clear();
    }
    {   // the only element: head and tail both go null
        Fixture f;
        f.list.Add(E(4));
        f.list.Remove(E(4));
        CHECK_EQ(f.list.GetCount(), 0);
        CHECK(f.list._head == nullptr);
        CHECK(f.list._tail == nullptr);
        CHECK_EQ(f.Allocs(), 1u);
        CHECK_EQ(f.Frees(), 1u);
        // and the list is usable again afterwards
        f.list.Add(E(5));
        CHECK_EQ(f.list.GetCount(), 1);
        CHECK_EQ(V(f.list._head->element), 5);
        CHECK_EQ(V(f.list._tail->element), 5);
        f.list.Clear();
    }
}

UNIT_TEST(LinkedListRemoveMissIsSilent) {
    Fixture f;
    f.list.Add(E(1)); f.list.Add(E(2));
    f.list.Remove(E(99));                 // walks off the end
    CHECK_EQ(f.list.GetCount(), 2);
    CHECK_EQ(f.Frees(), 0u);
    CHECK_EQ(Forward(f.list).size(), 2u);
    f.list.Clear();
}

UNIT_TEST(LinkedListClearFreesEveryNodeOnce) {
    // The Clear body has no oracle (linkedlist.cpp: a replay walks a chain
    // whose every node has already been freed). This is the mechanical stand-in
    // for the missing one: the test allocator stamps LIVE/DEAD, so a node freed
    // twice is COUNTED, and a node not freed shows as frees < allocs.
    Fixture f;
    for (int i = 0; i < 64; ++i) f.list.Add(E(i));
    CHECK_EQ(f.list.GetCount(), 64);
    CHECK_EQ(f.Allocs(), 64u);

    f.list.Clear();

    CHECK_EQ(f.Frees(), 64u);
    CHECK_EQ(f.Doubles(), 0u);
    CHECK_EQ(f.list.GetCount(), 0);
    CHECK(f.list._head == nullptr);
    CHECK(f.list._tail == nullptr);

    // Clear() retains nothing: a second Clear is a no-op, not a double free.
    f.list.Clear();
    CHECK_EQ(f.Frees(), 64u);
    CHECK_EQ(f.Doubles(), 0u);
}

UNIT_TEST(LinkedListLayoutIsRetails) {
    // The layout the whole port rests on, restated where a failure names this
    // family rather than a header nobody reads.
    static_assert(sizeof(List) == 0x18, "LinkedList is 0x18 bytes");
    static_assert(sizeof(List::Node) == 0x18, "Node(T=8) is 0x18 bytes");
    static_assert(sizeof(List::Iterator) == 0x08, "Iterator is one pointer");
    static_assert(offsetof(List, _head) == 0x00, "_head @ +0x00");
    static_assert(offsetof(List, _tail) == 0x08, "_tail @ +0x08");
    static_assert(offsetof(List, _count) == 0x10, "_count @ +0x10");
    static_assert(offsetof(List::Node, prev) == 0x08, "Node::prev @ +0x08");
    static_assert(offsetof(List::Node, next) == 0x10, "Node::next @ +0x10");
    // Value-init must still zero: adding a user-provided default ctor would
    // silently break every `Foo f = {};` in the three embedders (B31_RULES
    // class rule 5).
    List l{};
    CHECK(l._head == nullptr);
    CHECK(l._tail == nullptr);
    CHECK_EQ(l._count, 0);
}

// ============================================================================
// The introsorts — tie behaviour by permutation replay
// ============================================================================

namespace {

// `rtsort::SortByKey70` sorts an array of `IO::PackageFile*`, DESCENDING,
// comparing the signed int32 `_priority` (+0x70) and nothing else. The
// elements are real PackageFiles (their inline ctor; ~PackageFile is the host
// stub). `_version`, which the sort never reads, carries the TAG: where each
// element started, so the resulting PERMUTATION can be compared, not just the
// key sequence.
using Pkg = IO::PackageFile;
static int32_t& Tag(Pkg& p) { return p._version; }
static int32_t& Key70(Pkg& p) { return p._priority; }

// One deterministic LCG so the corpus is identical on every machine and in the
// golden dump. Not std::mt19937: the goldens must not move with the library.
struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t Next() { s = s * 1664525u + 1013904223u; return s >> 8; }
};

// Sort `n` packages whose keys are `distinct` distinct values (1 => all ties)
// and return the resulting permutation of the original indices.
std::vector<int> SortByKey70Permutation(int n, int distinct, uint32_t seed) {
    std::vector<Pkg>   pkgs(static_cast<size_t>(n));
    std::vector<Pkg*>  ptrs(static_cast<size_t>(n));
    Lcg rng(seed);
    for (int i = 0; i < n; ++i) {
        Tag(pkgs[static_cast<size_t>(i)]) = i;
        Key70(pkgs[static_cast<size_t>(i)]) =
            static_cast<int32_t>(rng.Next() % static_cast<uint32_t>(distinct));
        ptrs[static_cast<size_t>(i)] = &pkgs[static_cast<size_t>(i)];
    }
    rtsort::SortByKey70(ptrs.data(), n);
    std::vector<int> out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        out[static_cast<size_t>(i)] = Tag(*ptrs[static_cast<size_t>(i)]);
    return out;
}

// The same idea for the shared MSVC skeleton, which takes its comparator as a
// template parameter. Elements are 8 bytes {key, tag} so the swap is a plain
// struct assignment, as it is at the RendererDrawItem instantiation.
struct Item { int32_t key; int32_t tag; };
bool ItemLess(const Item& a, const Item& b) { return a.key < b.key; }
void ItemSwap(Item* a, Item* b) { Item t = *a; *a = *b; *b = t; }
void ItemSmallSort(Item* lo, Item* hi) {
    rtsort::SelectionSortMaxToHigh(lo, hi, ItemLess, ItemSwap);
}

std::vector<int> IntrosortPermutation(int n, int distinct, uint32_t seed) {
    std::vector<Item> v(static_cast<size_t>(n));
    Lcg rng(seed);
    for (int i = 0; i < n; ++i) {
        v[static_cast<size_t>(i)].key =
            static_cast<int32_t>(rng.Next() % static_cast<uint32_t>(distinct));
        v[static_cast<size_t>(i)].tag = i;
    }
    rtsort::Introsort(v.data(), n, ItemLess, ItemSwap, ItemSmallSort);
    std::vector<int> out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) out[static_cast<size_t>(i)] = v[static_cast<size_t>(i)].tag;
    return out;
}

// Compare against the golden table, or print a fresh one under
// -DUNIT_DUMP_GOLDEN. Deliberately NOT a "is it sorted" check: sortedness is
// what a broken transcription also passes.
void CheckPermutation(const char* name, const std::vector<int>& got,
                      const int* want, size_t wantN) {
#ifdef UNIT_DUMP_GOLDEN
    (void)want; (void)wantN;
    std::printf("static const int %s[%zu] = {", name, got.size());
    for (size_t i = 0; i < got.size(); ++i)
        std::printf("%s%d", i ? "," : "", got[i]);
    std::printf("};\n");
#else
    (void)name;
    CHECK_EQ(got.size(), wantN);
    if (got.size() != wantN) return;
    for (size_t i = 0; i < wantN; ++i) CHECK_EQ(got[i], want[i]);
#endif
}

// ---- the golden tables -----------------------------------------------------
// Recorded 2026-09-13 from the bodies as they stood at main e519898c, BEFORE
// ENDGAME B.3.2 touched this directory. See the file header for how to
// re-record and why doing so needs an argument.
static const int kSort70_n4_ties1[4] = {1,2,3,0};
static const int kSort70_n8_ties2[8] = {7,5,4,0,3,6,2,1};
static const int kSort70_n9_ties2[9] = {0,1,8,3,5,6,7,4,2};
static const int kSort70_n12_ties3[12] = {11,1,2,9,3,5,7,8,6,4,10,0};
static const int kSort70_n25_ties3[25] = {16,13,14,20,4,5,22,2,19,18,10,11,12,23,7,17,0,9,8,6,21,3,1,24,15};
static const int kSort70_n40_ties4[40] = {0,31,29,28,4,22,20,19,17,16,11,10,38,13,35,34,32,30,2,26,25,21,5,24,9,14,15,33,1,37,8,3,23,27,7,6,36,18,12,39};
static const int kSort70_n64_ties2[64] = {0,1,57,56,4,5,6,51,49,9,43,11,36,13,14,34,32,30,29,19,20,21,26,24,23,25,22,27,28,18,17,31,16,33,15,35,12,37,38,39,40,41,42,10,44,45,46,47,48,8,50,7,52,53,54,55,3,2,58,59,60,61,62,63};
static const int kIntro_n4_ties1[4] = {1,2,3,0};
static const int kIntro_n9_ties2[9] = {3,6,2,4,5,1,0,7,8};
static const int kIntro_n25_ties3[25] = {24,16,15,23,4,5,14,20,18,21,10,19,0,13,6,2,1,8,11,9,7,22,3,12,17};
static const int kIntro_n64_ties4[64] = {0,41,48,53,30,5,29,22,42,9,10,20,12,13,14,59,16,40,39,19,11,38,7,56,24,52,3,28,21,60,4,62,54,26,25,35,61,44,8,58,17,1,55,43,37,45,46,47,36,49,50,51,34,33,32,27,23,57,18,15,6,2,31,63};

}  // namespace

UNIT_TEST(SortByKey70TieReplay) {
    // Seven tie-heavy corpora, the smallest of them entirely ties (which is what
    // the boot's four-element packfile population actually is). A green result
    // here means the exact swap sequence is unchanged, which is the "which
    // package owns a colliding path" question lane PKG could not answer.
    CheckPermutation("kSort70_n4_ties1",
                     SortByKey70Permutation(4, 1, 1u), kSort70_n4_ties1, 4);
    CheckPermutation("kSort70_n8_ties2",
                     SortByKey70Permutation(8, 2, 2u), kSort70_n8_ties2, 8);
    CheckPermutation("kSort70_n9_ties2",
                     SortByKey70Permutation(9, 2, 3u), kSort70_n9_ties2, 9);
    CheckPermutation("kSort70_n12_ties3",
                     SortByKey70Permutation(12, 3, 4u), kSort70_n12_ties3, 12);
    CheckPermutation("kSort70_n25_ties3",
                     SortByKey70Permutation(25, 3, 5u), kSort70_n25_ties3, 25);
    CheckPermutation("kSort70_n40_ties4",
                     SortByKey70Permutation(40, 4, 6u), kSort70_n40_ties4, 40);
    CheckPermutation("kSort70_n64_ties2",
                     SortByKey70Permutation(64, 2, 7u), kSort70_n64_ties2, 64);
}

UNIT_TEST(IntrosortSkeletonTieReplay) {
    // The same replay for the shared skeleton in introsort_msvc.h, which is the
    // one that already carries the comparator as a template parameter and that
    // Graphics::RendererDrawItem and IO::Mod instantiate.
    CheckPermutation("kIntro_n4_ties1",
                     IntrosortPermutation(4, 1, 11u), kIntro_n4_ties1, 4);
    CheckPermutation("kIntro_n9_ties2",
                     IntrosortPermutation(9, 2, 12u), kIntro_n9_ties2, 9);
    CheckPermutation("kIntro_n25_ties3",
                     IntrosortPermutation(25, 3, 13u), kIntro_n25_ties3, 25);
    CheckPermutation("kIntro_n64_ties4",
                     IntrosortPermutation(64, 4, 14u), kIntro_n64_ties4, 64);
}

UNIT_TEST(SortByKey70OrdersDescendingLikeStdSort) {
    // The ordering half, on a large distinct-key population where no tie rule
    // is in play, so std::sort IS a valid reference for the KEY SEQUENCE (not
    // for the permutation — that is the test above).
    const int n = 2000;
    std::vector<Pkg>   pkgs(static_cast<size_t>(n));
    std::vector<Pkg*>  ptrs(static_cast<size_t>(n));
    std::vector<int32_t> keys(static_cast<size_t>(n));
    Lcg rng(0xC0FFEEu);
    for (int i = 0; i < n; ++i) {
        Tag(pkgs[static_cast<size_t>(i)]) = i;
        Key70(pkgs[static_cast<size_t>(i)]) = static_cast<int32_t>(rng.Next() % 1000000u);
        keys[static_cast<size_t>(i)] = Key70(pkgs[static_cast<size_t>(i)]);
        ptrs[static_cast<size_t>(i)] = &pkgs[static_cast<size_t>(i)];
    }
    rtsort::SortByKey70(ptrs.data(), n);
    std::sort(keys.begin(), keys.end(), [](int32_t a, int32_t b) { return a > b; });

    bool ok = true;
    for (int i = 0; i < n; ++i) {
        if (Key70(*ptrs[static_cast<size_t>(i)]) !=
            keys[static_cast<size_t>(i)]) { ok = false; break; }
    }
    CHECK(ok);

    // Every element is still present exactly once — a sort is a permutation.
    std::vector<int> seen(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) ++seen[static_cast<size_t>(
        Tag(*ptrs[static_cast<size_t>(i)]))];
    bool once = true;
    for (int i = 0; i < n; ++i) if (seen[static_cast<size_t>(i)] != 1) once = false;
    CHECK(once);
}

UNIT_TEST(IntrosortSkeletonOrdersAscendingLikeStdSort) {
    // `ItemLess` is `a.key < b.key`, and the skeleton's selection pass moves the
    // MAXIMUM to the high end, so the result is ASCENDING by key.
    const int n = 2000;
    std::vector<Item> v(static_cast<size_t>(n));
    std::vector<int32_t> keys(static_cast<size_t>(n));
    Lcg rng(0xBADF00Du);
    for (int i = 0; i < n; ++i) {
        v[static_cast<size_t>(i)].key = static_cast<int32_t>(rng.Next() % 1000000u);
        v[static_cast<size_t>(i)].tag = i;
        keys[static_cast<size_t>(i)] = v[static_cast<size_t>(i)].key;
    }
    rtsort::Introsort(v.data(), n, ItemLess, ItemSwap, ItemSmallSort);
    std::sort(keys.begin(), keys.end());

    bool ok = true;
    for (int i = 0; i < n; ++i)
        if (v[static_cast<size_t>(i)].key != keys[static_cast<size_t>(i)]) { ok = false; break; }
    CHECK(ok);

    std::vector<int> seen(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) ++seen[static_cast<size_t>(v[static_cast<size_t>(i)].tag)];
    bool once = true;
    for (int i = 0; i < n; ++i) if (seen[static_cast<size_t>(i)] != 1) once = false;
    CHECK(once);
}

UNIT_TEST(SortByKey70SmallRangeEdges) {
    // n == 0 and n == 1 pop immediately (`CMP R10,R11 / JBE`), and n == 2 is the
    // shortest path through the selection pass.
    Pkg* none[1] = {nullptr};
    rtsort::SortByKey70(none, 0);            // must not touch, must not fault

    Pkg one;
    Tag(one) = 0; Key70(one) = 5;
    Pkg* p1[1] = {&one};
    rtsort::SortByKey70(p1, 1);
    CHECK(p1[0] == &one);

    Pkg a, b;
    Tag(a) = 0; Key70(a) = 1;
    Tag(b) = 1; Key70(b) = 2;
    Pkg* p2[2] = {&a, &b};
    rtsort::SortByKey70(p2, 2);
    CHECK(p2[0] == &b);                      // descending: the larger key first
    CHECK(p2[1] == &a);
}
