// ============================================================================
// tests/unit/set_test.cpp — Collection::Set (ENDGAME B.3.2 step 1, SET core)
// ============================================================================
//
// WHAT IS UNDER TEST, AND WHAT IS NOT
// -----------------------------------
// `Collection::Set<T, Compare, Allocator, Key>` is a WRAPPER: at runtime the
// rotations and the retrace are the ported bodies at
// 0x18000adc0/b040/b170/aba0 (element 0x10), 0x180023170/23570/236a0 (element
// 0x18) and 0x180030540/307b0/308e0 (element 0x30). Under `BANISHED_UNIT_TEST`
// the header compiles its GENERIC core for every width instead
// (Collection.h's banner says so in as many words). The generic core is those
// same bodies with `Node*` in place of the baked-in offsets, so what this file
// verifies is the algorithm, the layouts, the duplicate-key rule and the
// ownership — and the real run verifies the ported bodies, which is why
// B32_RULES asks for one. (Since 2026-10-17 the ported bodies ARE driven on the
// host as well: tests/unit/set_ported_test.cpp links the four cores and runs
// them against this same invariant walker and against the generic core.)
//
// Everything under test is in src/engine/collection/Collection.h, and the
// allocator is the counting policy below — which is also how "Clear releases
// every node exactly once" is measured.
// ============================================================================

#include "unit.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "collection/Collection.h"

namespace {

// ---------------------------------------------------------------------------
// The counting allocator policy. `Collection::detail::AllocTraits` routes any
// non-DefaultAllocator policy straight at these two statics; DefaultAllocator
// is retail's 0x1800c7a50 / 0x180009c30 pair and is not reachable here.
// ---------------------------------------------------------------------------
struct CountingAllocator {
    static int  live;
    static int  allocs;
    static int  frees;
    static void Reset() { live = 0; allocs = 0; frees = 0; }
    static void* Allocate(uint64_t bytes, int align) {
        CHECK_EQ(align, 8);                      // Set nodes are alignment 8
        ++allocs; ++live;
        return ::operator new(static_cast<size_t>(bytes));
    }
    static void Free(void* p) {
        if (p) { ++frees; --live; }
        ::operator delete(p);
    }
};
int CountingAllocator::live   = 0;
int CountingAllocator::allocs = 0;
int CountingAllocator::frees  = 0;

// ---------------------------------------------------------------------------
// One element type per node width. The widths are the three the DLL evidence
// covers (Collection.h's LayoutProbe): 8 -> node 0x28, 0x10 -> node 0x30,
// 0x18 -> node 0x38.
// ---------------------------------------------------------------------------
struct E8 {                                  // exe EntityList tree element
    uint64_t k;
    bool operator<(const E8& o) const { return k < o.k; }
};
struct Pair16 {                              // Map_Insert's {key, value} pair
    uint64_t key;
    uint64_t value;
    bool operator<(const Pair16& o) const { return key < o.key; }
};
struct E24 {                                 // Input::KeyMap's key-set element
    uint64_t k;
    uint64_t b;
    uint64_t c;
    bool operator<(const E24& o) const { return k < o.k; }
};

// The key of a Pair element is its LEADING SUB-OBJECT, which is retail's rule
// (Collection.h: "Key == T: the key is the element or its leading
// sub-object"). Spelling the Key parameter proves the template honours it.
struct U64Key {
    uint64_t k;
    bool operator<(const U64Key& o) const { return k < o.k; }
};
struct Pair16Keyed {
    U64Key   key;
    uint64_t value;
};

template <class T, class Key = T>
using TestSet = Collection::Set<T, Collection::AVLCompare, CountingAllocator, Key>;

// ---------------------------------------------------------------------------
// The test-only AVL walker. It reads the PUBLIC node layout only (element,
// parent, left, right, balance, height) and checks every invariant the retrace
// is supposed to maintain:
//
//   * BST order (in-order keys strictly increasing)
//   * parent linkage on every edge, and a null parent at the root
//   * height  == max(childHeight + 1), a leaf being 0 (set_avl.cpp's Recompute)
//   * balance == rightHeight - leftHeight   *** NOTE THE SIGN ***
//   * |balance| <= 1 everywhere — the AVL invariant itself
// ---------------------------------------------------------------------------
struct WalkResult {
    int  nodes      = 0;
    bool ordered    = true;
    bool linked     = true;
    bool heights    = true;
    bool balanced   = true;
};

template <class SetT, class KeyOf>
int CheckSubtree(typename SetT::Node* n, typename SetT::Node* parent,
                 WalkResult& r, KeyOf keyOf,
                 std::vector<uint64_t>& inorder) {
    if (!n) return -1;                       // a null child's "height + 1" is 0
    if (n->parent != parent) r.linked = false;
    ++r.nodes;

    const int lh = CheckSubtree<SetT>(n->left, n, r, keyOf, inorder);
    inorder.push_back(keyOf(n->element));
    const int rh = CheckSubtree<SetT>(n->right, n, r, keyOf, inorder);

    const int height  = (lh > rh ? lh : rh) + 1;
    const int balance = rh - lh;
    if (n->height  != height)  r.heights  = false;
    if (n->balance != balance) r.heights  = false;
    if (balance < -1 || balance > 1) r.balanced = false;
    return height;
}

template <class SetT, class KeyOf>
WalkResult Walk(const SetT& s, KeyOf keyOf) {
    WalkResult r;
    std::vector<uint64_t> inorder;
    if (s._root && s._root->parent != nullptr) r.linked = false;
    CheckSubtree<SetT>(s._root, nullptr, r, keyOf, inorder);
    for (size_t i = 1; i < inorder.size(); ++i)
        if (!(inorder[i - 1] < inorder[i])) r.ordered = false;
    return r;
}

auto KeyOfE8     = [](const E8& e)     { return e.k; };
auto KeyOfPair16 = [](const Pair16& e) { return e.key; };
auto KeyOfE24    = [](const E24& e)    { return e.k; };

// A deterministic shuffle, so a failure is reproducible. (xorshift64*)
uint64_t NextRandom(uint64_t& state) {
    state ^= state >> 12; state ^= state << 25; state ^= state >> 27;
    return state * 2685821657736338717ull;
}

}  // namespace

// ============================================================================
// Layout — the same three widths Collection.h's LayoutProbe pins, checked
// through the CLASS this time (the class must not have moved one byte).
// ============================================================================
UNIT_TEST(set_layout_is_unchanged) {
    using S8  = TestSet<E8>;
    using S16 = TestSet<Pair16>;
    using S24 = TestSet<E24>;

    static_assert(sizeof(S8) == 0x10, "Set is {root, count} + 4 bytes of slack");
    static_assert(offsetof(S8, _root)  == 0x00, "Set::_root @ +0x00");
    static_assert(offsetof(S8, _count) == 0x08, "Set::_count @ +0x08");

    static_assert(sizeof(S8::Node)  == 0x28, "Set::Node(T=8) is 0x28");
    static_assert(sizeof(S16::Node) == 0x30, "Set::Node(T=0x10) is 0x30");
    static_assert(sizeof(S24::Node) == 0x38, "Set::Node(T=0x18) is 0x38");

    static_assert(offsetof(S8::Node,  parent) == 0x08, "T=8 parent @ +0x08");
    static_assert(offsetof(S16::Node, parent) == 0x10, "T=0x10 parent @ +0x10");
    static_assert(offsetof(S24::Node, parent) == 0x18, "T=0x18 parent @ +0x18");
    static_assert(offsetof(S16::Node, left)    == 0x18, "T=0x10 left @ +0x18");
    static_assert(offsetof(S16::Node, right)   == 0x20, "T=0x10 right @ +0x20");
    static_assert(offsetof(S16::Node, balance) == 0x28, "T=0x10 balance @ +0x28");
    static_assert(offsetof(S16::Node, height)  == 0x2a, "T=0x10 height @ +0x2a");

    CHECK(sizeof(typename S8::Iterator) == 8);   // one node pointer, nothing else
}

// ============================================================================
// Insert / Find / Remove, once per node width.
// ============================================================================
UNIT_TEST(set_insert_find_remove_width8) {
    CountingAllocator::Reset();
    {
        TestSet<E8> s;
        for (uint64_t i = 0; i < 64; ++i) CHECK(s.Insert(E8{i}) != nullptr);
        CHECK_EQ(s.GetCount(), 64);
        CHECK_EQ(CountingAllocator::live, 64);

        for (uint64_t i = 0; i < 64; ++i) {
            typename TestSet<E8>::Node* n = s.Find(E8{i});
            CHECK(n != nullptr);
            if (n) CHECK_EQ(n->element.k, i);
        }
        CHECK(s.Find(E8{64}) == nullptr);

        CHECK(s.Remove(E8{31}));
        CHECK_EQ(s.GetCount(), 63);
        CHECK(s.Find(E8{31}) == nullptr);
        CHECK(!s.Remove(E8{31}));            // already gone
        WalkResult r = Walk(s, KeyOfE8);
        CHECK_EQ(r.nodes, 63);
        CHECK(r.ordered && r.linked && r.heights && r.balanced);
    }
    CHECK_EQ(CountingAllocator::live, 0);
}

UNIT_TEST(set_insert_find_remove_width16_pair) {
    CountingAllocator::Reset();
    {
        TestSet<Pair16> s;
        for (uint64_t i = 0; i < 64; ++i) s.Insert(Pair16{i, i * 10});
        CHECK_EQ(s.GetCount(), 64);
        for (uint64_t i = 0; i < 64; ++i) {
            typename TestSet<Pair16>::Node* n = s.Find(Pair16{i, 0});
            CHECK(n != nullptr);
            if (n) CHECK_EQ(n->element.value, i * 10);   // the VALUE came along
        }
        // Erase every third key and re-check the invariants.
        for (uint64_t i = 0; i < 64; i += 3) CHECK(s.Remove(Pair16{i, 0}));
        WalkResult r = Walk(s, KeyOfPair16);
        CHECK_EQ(r.nodes, s.GetCount());
        CHECK(r.ordered && r.linked && r.heights && r.balanced);
    }
    CHECK_EQ(CountingAllocator::live, 0);
}

UNIT_TEST(set_insert_find_remove_width24) {
    CountingAllocator::Reset();
    {
        TestSet<E24> s;
        for (uint64_t i = 0; i < 64; ++i) s.Insert(E24{i, i + 1, i + 2});
        CHECK_EQ(s.GetCount(), 64);
        for (uint64_t i = 0; i < 64; ++i) {
            typename TestSet<E24>::Node* n = s.Find(E24{i, 0, 0});
            CHECK(n != nullptr);
            if (n) CHECK_EQ(n->element.c, i + 2);
        }
        for (uint64_t i = 1; i < 64; i += 2) CHECK(s.Remove(E24{i, 0, 0}));
        WalkResult r = Walk(s, KeyOfE24);
        CHECK_EQ(r.nodes, 32);
        CHECK(r.ordered && r.linked && r.heights && r.balanced);
    }
    CHECK_EQ(CountingAllocator::live, 0);
}

// ============================================================================
// The duplicate-key rule — retail's equal arm (0x180030418 -> 0x180030443):
// *** the EXISTING node comes back, nothing is inserted, the count does not
// move, and no node is allocated. ***
// ============================================================================
UNIT_TEST(set_duplicate_key_returns_the_existing_node) {
    CountingAllocator::Reset();
    {
        TestSet<Pair16> s;
        typename TestSet<Pair16>::Node* first = s.Insert(Pair16{7, 700});
        CHECK(first != nullptr);
        CHECK_EQ(s.GetCount(), 1);
        const int allocsAfterFirst = CountingAllocator::allocs;

        typename TestSet<Pair16>::Node* again = s.Insert(Pair16{7, 999});
        CHECK(again == first);                          // the SAME node
        CHECK_EQ(s.GetCount(), 1);                      // count did not move
        CHECK_EQ(CountingAllocator::allocs, allocsAfterFirst);   // nothing allocated
        CHECK_EQ(again->element.value, 700u);           // the value is NOT updated

        // ... and it holds against a root insert too (the empty-tree arm is a
        // different branch of the core).
        for (uint64_t i = 0; i < 32; ++i) s.Insert(Pair16{i, i});
        const int32_t count = s.GetCount();
        const int allocs = CountingAllocator::allocs;
        for (uint64_t i = 0; i < 32; ++i) {
            typename TestSet<Pair16>::Node* n = s.Insert(Pair16{i, 12345});
            CHECK(n == s.Find(Pair16{i, 0}));
        }
        CHECK_EQ(s.GetCount(), count);
        CHECK_EQ(CountingAllocator::allocs, allocs);
    }
    CHECK_EQ(CountingAllocator::live, 0);
}

// ============================================================================
// A Pair whose Key is its LEADING SUB-OBJECT, spelled as the Key parameter —
// retail's `Set<Pair<K,V>, …, K, Compare>` shape, which is the only "Map" this
// engine has.
// ============================================================================
UNIT_TEST(set_pair_keyed_by_leading_subobject) {
    CountingAllocator::Reset();
    {
        TestSet<Pair16Keyed, U64Key> s;
        for (uint64_t i = 0; i < 32; ++i) s.Insert(Pair16Keyed{U64Key{i}, i * 3});
        CHECK_EQ(s.GetCount(), 32);
        for (uint64_t i = 0; i < 32; ++i) {
            typename TestSet<Pair16Keyed, U64Key>::Node* n = s.Find(U64Key{i});
            CHECK(n != nullptr);
            if (n) CHECK_EQ(n->element.value, i * 3);
        }
        CHECK(s.Find(U64Key{99}) == nullptr);
    }
    CHECK_EQ(CountingAllocator::live, 0);
}

// ============================================================================
// In-order iteration, through the Iterator (the parent-pointer successor walk
// inlined into 0x1800a76c0).
// ============================================================================
UNIT_TEST(set_iterates_in_key_order) {
    CountingAllocator::Reset();
    {
        TestSet<E8> s;
        CHECK(s.Begin() == s.End());                    // empty
        uint64_t state = 0x9e3779b97f4a7c15ull;
        for (int i = 0; i < 200; ++i) s.Insert(E8{NextRandom(state) % 1000});

        int seen = 0;
        uint64_t last = 0;
        for (typename TestSet<E8>::Iterator it = s.Begin(); it != s.End(); ++it) {
            if (seen) CHECK(last < (*it).k);            // strictly increasing
            last = (*it).k;
            ++seen;
        }
        CHECK_EQ(seen, s.GetCount());
    }
    CHECK_EQ(CountingAllocator::live, 0);
}

// ============================================================================
// Clear releases every node EXACTLY ONCE, and it runs the element destructor
// exactly once per element (which is what the string-map instantiation's
// `~Pair<String,String>` rides on — Collection.h's Clear, retail 0x18000f7d0).
// ============================================================================
namespace {
int g_dtorCalls = 0;
struct Counted {
    uint64_t k;
    uint64_t pad;                                   // element width 0x10
    Counted() : k(0), pad(0) {}
    explicit Counted(uint64_t v) : k(v), pad(0) {}
    Counted(const Counted& o) : k(o.k), pad(o.pad) {}
    ~Counted() { ++g_dtorCalls; }
    bool operator<(const Counted& o) const { return k < o.k; }
};
}  // namespace

UNIT_TEST(set_clear_releases_every_node_once) {
    CountingAllocator::Reset();
    g_dtorCalls = 0;
    {
        Collection::Set<Counted, Collection::AVLCompare, CountingAllocator> s;
        for (uint64_t i = 0; i < 100; ++i) s.Insert(Counted(i));
        CHECK_EQ(s.GetCount(), 100);
        CHECK_EQ(CountingAllocator::live, 100);
        const int dtorsFromTemporaries = g_dtorCalls;   // the `Counted(i)` args

        s.Clear();
        CHECK_EQ(s.GetCount(), 0);
        CHECK(s.GetRoot() == nullptr);
        CHECK_EQ(CountingAllocator::live, 0);
        CHECK_EQ(CountingAllocator::frees, 100);        // exactly once each
        CHECK_EQ(g_dtorCalls - dtorsFromTemporaries, 100);

        // Retail's Clear writes the two zeroes even on an already-empty Set
        // (it branches to a second copy of them at 0x18000f824 rather than
        // skipping); a second Clear must therefore be a no-op, not a crash.
        s.Clear();
        CHECK_EQ(s.GetCount(), 0);
        CHECK_EQ(CountingAllocator::frees, 100);

        s.Insert(Counted(1));                           // usable again after Clear
        CHECK_EQ(s.GetCount(), 1);
    }
    CHECK_EQ(CountingAllocator::live, 0);               // the destructor clears too
    CHECK_EQ(CountingAllocator::frees, 101);
}

// ============================================================================
// Balance after ascending, descending and random inserts — the retrace is the
// only thing standing between an AVL tree and a linked list, and AVL DEFECTS
// CANCEL (set_avl.cpp's ORACLE banner), so the walker checks shape, height,
// balance and parent linkage at every node rather than just the key sequence.
// ============================================================================
UNIT_TEST(set_balances_ascending_inserts) {
    CountingAllocator::Reset();
    {
        TestSet<E8> s;
        for (uint64_t i = 0; i < 1000; ++i) s.Insert(E8{i});
        WalkResult r = Walk(s, KeyOfE8);
        CHECK_EQ(r.nodes, 1000);
        CHECK(r.ordered && r.linked && r.heights && r.balanced);
        // An AVL tree of 1000 nodes is at most 1.44*log2(1001) ~= 14 deep; a
        // retrace that never ran would leave a 1000-deep spine.
        CHECK(s.GetRoot()->height <= 14);
    }
    CHECK_EQ(CountingAllocator::live, 0);
}

UNIT_TEST(set_balances_descending_inserts) {
    CountingAllocator::Reset();
    {
        TestSet<Pair16> s;
        for (uint64_t i = 1000; i > 0; --i) s.Insert(Pair16{i, i});
        WalkResult r = Walk(s, KeyOfPair16);
        CHECK_EQ(r.nodes, 1000);
        CHECK(r.ordered && r.linked && r.heights && r.balanced);
        CHECK(s.GetRoot()->height <= 14);
    }
    CHECK_EQ(CountingAllocator::live, 0);
}

UNIT_TEST(set_balances_random_inserts_and_removes) {
    CountingAllocator::Reset();
    {
        TestSet<E24> s;
        uint64_t state = 12345;
        for (int i = 0; i < 2000; ++i) s.Insert(E24{NextRandom(state) % 5000, 0, 0});
        WalkResult r = Walk(s, KeyOfE24);
        CHECK_EQ(r.nodes, s.GetCount());
        CHECK(r.ordered && r.linked && r.heights && r.balanced);

        // Erase half of the key space, re-checking the invariants throughout —
        // the erase's case 3 is a PREDECESSOR splice and a successor splice
        // would leave a valid tree with the WRONG SHAPE, which only a
        // shape-checking walk catches.
        for (uint64_t k = 0; k < 5000; k += 2) s.Remove(E24{k, 0, 0});
        r = Walk(s, KeyOfE24);
        CHECK_EQ(r.nodes, s.GetCount());
        CHECK(r.ordered && r.linked && r.heights && r.balanced);
        for (uint64_t k = 0; k < 5000; k += 2) CHECK(s.Find(E24{k, 0, 0}) == nullptr);
    }
    CHECK_EQ(CountingAllocator::live, 0);
}

// ============================================================================
// A POINTER KEY: null orders as less than everything, and it does so in the
// DESCENT, not in the comparator — retail's two null arms are asymmetric (a
// null SEARCH key goes LEFT, a null NODE key goes RIGHT: avl_trees.cpp's
// group-B banner) and a "tidied" comparator that collapsed them would build a
// differently-shaped tree on any input containing a null key.
//
// `Collection::AVLCompare` on a `char*` key resolves to
// `detail::AvlStrCmpSigned` (out of line in set_core.cpp); the test supplies
// its own comparator policy. What it
// exercises is `KeyTraits<char*>` and the two null arms, which are the header's.
// ============================================================================
namespace {
struct StrCompare {
    static bool Less(char* const& a, char* const& b) {
        return std::strcmp(a, b) < 0;      // both non-null: the null arms are the
    }                                      // descent's job, not the comparator's
};
struct Row { char* key; uint64_t value; };
using RowSet = Collection::Set<Row, StrCompare, CountingAllocator, char*>;
}  // namespace

UNIT_TEST(set_pointer_key_null_orders_first) {
    CountingAllocator::Reset();
    {
        RowSet s;
        char alpha[] = "alpha", beta[] = "beta", gamma[] = "gamma";
        s.Insert(Row{beta,  2});
        s.Insert(Row{alpha, 1});
        s.Insert(Row{gamma, 3});
        s.Insert(Row{nullptr, 99});                 // the null key
        CHECK_EQ(s.GetCount(), 4);

        // In-order: the null key first, then the three strings in order.
        RowSet::Iterator it = s.Begin();
        CHECK((*it).key == nullptr);
        ++it; CHECK_STREQ((*it).key, "alpha");
        ++it; CHECK_STREQ((*it).key, "beta");
        ++it; CHECK_STREQ((*it).key, "gamma");
        ++it; CHECK(it == s.End());

        CHECK(s.Find(beta) != nullptr);
        CHECK(s.Find(nullptr) != nullptr);
        char absent[] = "delta";
        CHECK(s.Find(absent) == nullptr);

        // A duplicate null key is still a lookup, not an insert.
        CHECK(s.Insert(Row{nullptr, 1234}) == s.Find(nullptr));
        CHECK_EQ(s.GetCount(), 4);
    }
    CHECK_EQ(CountingAllocator::live, 0);
}

// ============================================================================
// ENDGAME B.3.2 closing pass, item 2 — ELEMENT WIDTH 8, the `Set<Filename>`
// shape
// ============================================================================
//
// `AvlOps<Node, 0x8>` is new: the template used to carry 0x10, 0x18 and 0x30
// and fell through to `AvlGeneric` for 8, so the one width-8 tree in the DLL —
// the `Set<System::Filename>` path set every `IO::Modification::Mod` carries —
// could not be spelled as a `Collection::Set` at all. Its ported core is
// src/engine/io/modification_avl8.cpp's 0x1800274c0 / 0x180027e20 /
// 0x180027f50; the template calls the retrace by name.
//
// ON THE HOST THIS TEST DRIVES `AvlGeneric`, not those three bodies: the
// specialisation is behind `#if !defined(BANISHED_UNIT_TEST) ||
// defined(BANISHED_UNIT_PORTED_AVL)` like the other three widths, and this file
// does not define BANISHED_UNIT_PORTED_AVL (tests/unit/set_ported_test.cpp
// drives the ported bodies). What it therefore verifies is the
// SHAPE the width-8 tree needs — node 0x28, a key with a null state, a
// case-insensitive comparator, the duplicate rule and erase — and the real run
// verifies the bodies. The two halves together are what B32_RULES asks for.
//
// The element IS the key and it is ONE POINTER, which is what makes this width
// 8 (`System::Filename` is `{ char* _data; }`). `FilenameLike` is that, with the
// case-insensitive compare `IO::Modification`'s tree uses — NOT the signed
// `strcmp` the pathid tree at width 0x10 uses, which is the one field
// set_avl_pathid.cpp's banner says separates the two families.
namespace {

struct FilenameLike { char* data; };

int AsciiStrICmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        const int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca == 0) return 0;
    }
}

struct FilenameICompare {
    static bool Less(const FilenameLike& a, const FilenameLike& b) {
        return AsciiStrICmp(a.data, b.data) < 0;
    }
};

using PathSet = Collection::Set<FilenameLike, FilenameICompare, CountingAllocator>;

}  // namespace

namespace Collection {
namespace detail {
// The null arm, exactly as Modification.h specialises it for the real
// `System::Filename`.
template <> struct KeyTraits<FilenameLike> {
    static bool IsNull(const FilenameLike& k) { return k.data == nullptr; }
};
}  // namespace detail
}  // namespace Collection

UNIT_TEST(set_width8_node_is_0x28_and_the_element_is_the_key) {
    CHECK_EQ(sizeof(PathSet), static_cast<size_t>(0x10));
    CHECK_EQ(sizeof(PathSet::Node), static_cast<size_t>(0x28));
    CHECK_EQ(offsetof(PathSet::Node, parent),  static_cast<size_t>(0x08));
    CHECK_EQ(offsetof(PathSet::Node, left),    static_cast<size_t>(0x10));
    CHECK_EQ(offsetof(PathSet::Node, right),   static_cast<size_t>(0x18));
    CHECK_EQ(offsetof(PathSet::Node, balance), static_cast<size_t>(0x20));
    CHECK_EQ(offsetof(PathSet::Node, height),  static_cast<size_t>(0x22));
}

UNIT_TEST(set_width8_insert_find_erase_and_duplicates) {
    CountingAllocator::Reset();
    {
        PathSet s;
        char a[] = "Data/Models/House.rsc";
        char b[] = "data/models/barn.rsc";
        char c[] = "data/Terrain/grass.rsc";
        char aDifferentCase[] = "DATA/MODELS/HOUSE.RSC";

        CHECK(s.Insert(FilenameLike{b}) != nullptr);
        CHECK(s.Insert(FilenameLike{a}) != nullptr);
        CHECK(s.Insert(FilenameLike{c}) != nullptr);
        CHECK_EQ(s.GetCount(), 3);
        CHECK_EQ(CountingAllocator::allocs, 3);

        // Case-INSENSITIVE: a differently-cased path is the SAME key, so the
        // insert is a lookup that returns the existing node and does not count.
        PathSet::Node* existing = s.Insert(FilenameLike{a});
        CHECK(s.Insert(FilenameLike{aDifferentCase}) == existing);
        CHECK_EQ(s.GetCount(), 3);
        CHECK_EQ(CountingAllocator::allocs, 3);       // nothing was allocated

        CHECK(s.Find(FilenameLike{aDifferentCase}) == existing);
        char absent[] = "data/models/mill.rsc";
        CHECK(s.Find(FilenameLike{absent}) == nullptr);

        // A null key: the left arm descends left on it, the right arm reports
        // EQUAL — so the FIRST null inserts and every later one is a lookup.
        PathSet::Node* nullNode = s.Insert(FilenameLike{nullptr});
        CHECK(nullNode != nullptr);
        CHECK_EQ(s.GetCount(), 4);
        CHECK(s.Insert(FilenameLike{nullptr}) == nullNode);
        CHECK_EQ(s.GetCount(), 4);

        // ... and it orders first.
        CHECK(s.Begin().GetNode() == nullNode);

        // Erase. `AvlOps<Node, 0x8>` has no ported erase — retail never emitted
        // one at this width (`Mod::~Mod` tears the whole tree down post-order),
        // so this is the generic body, which is the same algorithm.
        s.Remove(existing);
        CHECK_EQ(s.GetCount(), 3);
        CHECK(s.Find(FilenameLike{a}) == nullptr);
        CHECK(s.Find(FilenameLike{b}) != nullptr);
        CHECK(s.Find(FilenameLike{c}) != nullptr);
        CHECK_EQ(CountingAllocator::live, 3);
    }
    CHECK_EQ(CountingAllocator::live, 0);             // ~Set cleared the rest
}

UNIT_TEST(set_width8_stays_balanced_and_linked) {
    CountingAllocator::Reset();
    {
        PathSet s;
        // Unique, zero-padded names, so lexicographic order IS numeric order
        // and the walker's key can be the number parsed back out.
        std::vector<std::vector<char>> names(400);
        for (int i = 0; i < 400; ++i) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Path/%04d.rsc", i);
            names[i].assign(buf, buf + std::strlen(buf) + 1);
        }
        uint64_t state = 0x9e3779b97f4a7c15ull;
        for (int i = 399; i > 0; --i)                  // deterministic shuffle
            std::swap(names[i], names[NextRandom(state) % (i + 1)]);
        for (auto& n : names) s.Insert(FilenameLike{n.data()});
        CHECK_EQ(s.GetCount(), 400);

        WalkResult r = Walk(s, [](const FilenameLike& e) {
            return static_cast<uint64_t>(std::strtoul(e.data + 5, nullptr, 10));
        });
        CHECK_EQ(r.nodes, s.GetCount());
        CHECK(r.ordered && r.linked && r.heights && r.balanced);

        // And the order is the COMPARATOR's, checked directly: an in-order walk
        // is strictly increasing under `_stricmp`.
        const char* prev = nullptr;
        for (PathSet::Iterator it = s.Begin(); it != s.End(); ++it) {
            const char* cur = (*it).data;
            if (prev) CHECK(AsciiStrICmp(prev, cur) < 0);
            prev = cur;
        }
    }
    CHECK_EQ(CountingAllocator::live, 0);
}

// ============================================================================
// ENDGAME B.3.2 closing pass, item 4 — `InsertInPlace` / `Emplace`
// ============================================================================
//
// The element is CONSTRUCTED IN THE NODE instead of copy-constructed from a `T`
// the caller built first. Same descent, same retrace, same duplicate rule.
// Collection.h's banner says when that is observably identical to `Insert` and
// when it is not; these tests assert the mechanism.
namespace {

struct LifeEvent { enum Kind { kCtor, kCopy, kDtor } kind; int value; };
std::vector<LifeEvent>* g_life = nullptr;

// The KEY is the leading sub-object and it is TRIVIAL — retail's rule, and the
// one that makes an emplace worth having: the descent needs a key before
// anything is allocated, so a key that costs a construction of its own would
// hand the temporary straight back.
struct TKey {
    int k;
    bool operator<(const TKey& o) const { return k < o.k; }
};

struct Tracked {
    TKey key;          // +0x00 — the leading sub-object
    int  payload;
    Tracked(int k, int p) : key{k}, payload(p) {
        if (g_life) g_life->push_back({LifeEvent::kCtor, k});
    }
    Tracked(const Tracked& o) : key(o.key), payload(o.payload) {
        if (g_life) g_life->push_back({LifeEvent::kCopy, o.key.k});
    }
    Tracked& operator=(const Tracked&) = default;
    ~Tracked() { if (g_life) g_life->push_back({LifeEvent::kDtor, key.k}); }
};

using TrackedSet =
    Collection::Set<Tracked, Collection::AVLCompare, CountingAllocator, TKey>;

}  // namespace

UNIT_TEST(set_emplace_constructs_once_in_the_node) {
    CountingAllocator::Reset();
    std::vector<LifeEvent> log;
    {
        TrackedSet s;

        // The `Insert` path, for the contrast: the caller builds a `T`, the
        // node copy-constructs from it, and the caller's temporary dies.
        g_life = &log;
        {
            Tracked tmp(1, 100);
            s.Insert(tmp);
        }
        g_life = nullptr;
        CHECK_EQ(static_cast<int>(log.size()), 3);
        if (log.size() == 3) {
            CHECK(log[0].kind == LifeEvent::kCtor);
            CHECK(log[1].kind == LifeEvent::kCopy);
            CHECK(log[2].kind == LifeEvent::kDtor);
        }

        // The `Emplace` path: ONE construction, in the node. No copy, and no
        // temporary to destroy.
        log.clear();
        g_life = &log;
        TrackedSet::Node* n = s.Emplace(TKey{2}, 2, 200);
        g_life = nullptr;
        CHECK(n != nullptr);
        CHECK_EQ(static_cast<int>(log.size()), 1);
        if (log.size() == 1) {
            CHECK(log[0].kind == LifeEvent::kCtor);
            CHECK_EQ(log[0].value, 2);
        }
        CHECK_EQ(n->element.key.k, 2);
        CHECK_EQ(n->element.payload, 200);
        CHECK_EQ(s.GetCount(), 2);

        log.clear();
    }
    CHECK_EQ(CountingAllocator::live, 0);
    g_life = nullptr;
}

UNIT_TEST(set_emplace_on_a_duplicate_key_constructs_nothing) {
    CountingAllocator::Reset();
    std::vector<LifeEvent> log;
    {
        TrackedSet s;
        TrackedSet::Node* first = s.Emplace(TKey{7}, 7, 700);
        CHECK_EQ(s.GetCount(), 1);
        const int allocsAfterFirst = CountingAllocator::allocs;

        // The descent finds the key and returns the EXISTING node before it
        // ever reaches the allocation — so `T(args…)` never runs, the count
        // does not move, and the stored payload is the FIRST one.
        g_life = &log;
        TrackedSet::Node* again = s.Emplace(TKey{7}, 7, 999);
        g_life = nullptr;
        CHECK(again == first);
        CHECK_EQ(s.GetCount(), 1);
        CHECK_EQ(CountingAllocator::allocs, allocsAfterFirst);
        CHECK_EQ(static_cast<int>(log.size()), 0);
        CHECK_EQ(first->element.payload, 700);
    }
    CHECK_EQ(CountingAllocator::live, 0);
    g_life = nullptr;
}

UNIT_TEST(set_emplace_builds_the_same_tree_as_insert) {
    CountingAllocator::Reset();
    {
        // Two trees, the same keys in the same order, one filled by `Insert`
        // and one by `Emplace`. The shapes must be identical: `InsertInPlace`
        // differs from `Insert` only in what runs in the node, never in the
        // descent or the retrace.
        TestSet<E8> byInsert;
        Collection::Set<E8, Collection::AVLCompare, CountingAllocator> byEmplace;

        uint64_t state = 0x243f6a8885a308d3ull;
        std::vector<uint64_t> keys;
        for (int i = 0; i < 500; ++i) keys.push_back(NextRandom(state) % 900u);

        for (uint64_t k : keys) {
            byInsert.Insert(E8{k});
            byEmplace.Emplace(E8{k}, E8{k});
        }
        CHECK_EQ(byInsert.GetCount(), byEmplace.GetCount());

        auto a = byInsert.Begin();
        auto b = byEmplace.Begin();
        int walked = 0;
        while (a != byInsert.End() && b != byEmplace.End()) {
            CHECK_EQ((*a).k, (*b).k);
            CHECK_EQ(a.GetNode()->height,  b.GetNode()->height);
            CHECK_EQ(a.GetNode()->balance, b.GetNode()->balance);
            ++a; ++b; ++walked;
        }
        CHECK_EQ(walked, byInsert.GetCount());
        CHECK(a == byInsert.End());
        CHECK(b == byEmplace.End());
    }
    CHECK_EQ(CountingAllocator::live, 0);
}
