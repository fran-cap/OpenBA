// ============================================================================
// tests/unit/path_index_test.cpp -- block PATHINDEX: the case-folded hash index
//   (src/engine/collection/fold_index.h) answers exactly as the tree descent
// ============================================================================
// The oracle is an ordered tree under retail's comparator: a std::map whose
// less-than is `_stricmp` < 0 in the "C" locale (unsigned bytes, 'A'..'Z'
// lowered), filled the way the two game trees are filled (an insert that meets
// a fold-equal key inserts nothing) and searched with `find` (the same
// `!(a<b) && !(b<a)` descent). Every lookup compares the index's node with the
// tree's node, by address:
//   * the fold itself against `_stricmp` on random byte strings (bytes >= 0x80,
//     the six codes between 'Z' and 'a', case variants), and hash agreement;
//   * random path sets with mixed '/' and '\', dots, trailing separators and
//     case variants, under random inserts, erases and probes;
//   * the game's resource names (data/resource_names.inc) and every entry name
//     of the seven MegaMod packages (read-only, when installed), normalised by
//     the real System::Filename as the trees hold them, probed as stored, case
//     flipped, with '/' and with one character changed;
//   * the decline rules: null keys, a second tree, a count that differs.
// The game's own trees are checked live by the debug exe (`[PathIndex] MISMATCH`).
// ============================================================================
#include "unit.h"
#include "collection/fold_index.h"
#include "io/Stream.h"
#include "system/Filename.h"
#include "system/String.h"
#include "pkm_names.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

using Collection::FoldIndex;

namespace {

// Retail's `_stricmp` (0x18007eec0, __ascii_stricmp), the comparator of both trees.
int RetailStrICmp(const char* a, const char* b) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(a);  // CAST-OK: bytes
    const unsigned char* q = reinterpret_cast<const unsigned char*>(b);  // CAST-OK: bytes
    for (;;) {
        uint32_t c1 = *p++;
        if (c1 - 0x41u <= 0x19u)
            c1 += 0x20u;
        uint32_t c2 = *q++;
        if (c2 - 0x41u <= 0x19u)
            c2 += 0x20u;
        if (c1 == 0 || c1 != c2)
            return static_cast<int>(c1) - static_cast<int>(c2);
    }
}

struct RetailLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return RetailStrICmp(a.c_str(), b.c_str()) < 0;
    }
};

using Tree = std::map<std::string, int, RetailLess>;

const char* MapNodeKey(const void* node) {
    return static_cast<const Tree::value_type*>(node)->first.c_str();
}

// The tree and its index, kept in step the way the game's mutators keep them.
struct Indexed {
    Tree tree;
    FoldIndex index{&MapNodeKey};
    int64_t lookups = 0;
    int64_t found = 0;
    int differ = 0;

    void Insert(const std::string& key) {
        auto r = tree.emplace(key, 0);
        if (r.second)
            index.Insert(&tree, &*r.first);
    }
    void Erase(const std::string& key) {
        auto it = tree.find(key);
        if (it == tree.end())
            return;
        index.Erase(&tree, &*it);
        tree.erase(it);
    }
    void Probe(const std::string& key) {
        const void* fromIndex = nullptr;
        const bool answered = index.Find(&tree, static_cast<int64_t>(tree.size()), key.c_str(), &fromIndex);
        auto it = tree.find(key);
        const void* fromTree = it == tree.end() ? nullptr : &*it;
        ++lookups;
        if (fromTree != nullptr)
            ++found;
        if (!answered || fromIndex != fromTree) {
            if (differ < 5)
                std::printf("  [path_index] differ: key=\"%s\" answered=%d\n", key.c_str(), answered ? 1 : 0);
            ++differ;
        }
    }
};

std::string FlipCase(std::string s, std::mt19937& rng) {
    for (char& c : s) {
        if ((rng() & 1) == 0)
            continue;
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 32);
        else if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }
    return s;
}

// The trees hold keys as System::Filename normalises them.
std::string Normalised(const std::string& raw) {
    System::Filename f(raw.c_str());
    return f._data != nullptr ? std::string(f._data) : std::string();
}

std::string RandomPath(std::mt19937& rng) {
    // No "." / ".." segments: some arrangements of a delimiter before ".." never
    // leave AdjustDelimiters' splice loop (retail's body and ours alike).
    static const char* const parts[] = {"Models", "models", "Textures", "Template", "Audio", "a.b", "x.",
                                        "Tree", "TREE", "tree_01", "Field", "a", "B", "_x", "[z]",
                                        "Crops", "Wheat.rsc", "wheat.RSC", "Stone"};
    static const char* const seps[] = {"/", "\\", "//", "\\\\", "/\\"};
    std::string s;
    const int n = 1 + static_cast<int>(rng() % 5);
    for (int i = 0; i < n; ++i) {
        if (i > 0 || rng() % 8 == 0)
            s += seps[rng() % 5];
        s += parts[rng() % (sizeof(parts) / sizeof(parts[0]))];
    }
    if (rng() % 6 == 0)
        s += seps[rng() % 5];
    if (rng() % 3 == 0)
        s += ":resource";
    return s;
}

}  // namespace

UNIT_TEST(path_index_fold_matches_stricmp) {
    std::mt19937 rng(1234);
    static const unsigned char alphabet[] = {'a', 'A', 'z', 'Z', 'm', 'M', '[', '\\', ']', '^', '_', '`',
                                             '@', '{', '/', '.', 0x80, 0xC0, 0xDE, 0xE0, 0xFF, '0'};
    int bad = 0;
    for (int i = 0; i < 150000; ++i) {   // 150k random pairs (300k before)
        std::string a, b;
        const int la = static_cast<int>(rng() % 21);
        for (int k = 0; k < la; ++k)
            a.push_back(static_cast<char>(alphabet[rng() % sizeof(alphabet)]));
        if (rng() % 2 == 0) {
            std::mt19937 r2(i);
            b = FlipCase(a, r2);
        } else {
            const int lb = static_cast<int>(rng() % 6);
            for (int k = 0; k < lb; ++k)
                b.push_back(static_cast<char>(alphabet[rng() % sizeof(alphabet)]));
        }
        const bool eq = RetailStrICmp(a.c_str(), b.c_str()) == 0;
        if (FoldIndex::FoldEqual(a.c_str(), b.c_str()) != eq)
            ++bad;
        if (eq && FoldIndex::Hash(a.c_str()) != FoldIndex::Hash(b.c_str()))
            ++bad;
    }
    CHECK_EQ(bad, 0);
}

UNIT_TEST(path_index_fold_word_is_fold_per_byte) {
    std::mt19937_64 rng(42);
    int bad = 0;
    for (int i = 0; i < 2000000; ++i) {
        uint64_t w = rng();
        if (i < 256 * 8)
            w = static_cast<uint64_t>(i / 8) << (8 * (i % 8));   // every byte value in every lane
        const uint64_t got = FoldIndex::FoldWord(w);
        for (int b = 0; b < 8; ++b) {
            const unsigned char c = static_cast<unsigned char>(w >> (8 * b));
            if (static_cast<unsigned char>(got >> (8 * b)) != FoldIndex::Fold(c))
                ++bad;
        }
    }
    CHECK_EQ(bad, 0);
}

UNIT_TEST(path_index_random_paths_under_insert_erase) {
    std::mt19937 rng(77);
    Indexed t;
    std::vector<std::string> pool;
    for (int i = 0; i < 4000; ++i)
        pool.push_back(Normalised(RandomPath(rng)));
    for (int round = 0; round < 60000; ++round) {
        const std::string& k = pool[rng() % pool.size()];
        switch (rng() % 4) {
        case 0:
            t.Insert(k);
            break;
        case 1:
            t.Erase(FlipCase(k, rng));
            break;
        default:
            t.Probe(rng() % 2 ? FlipCase(k, rng) : Normalised(RandomPath(rng)));
            break;
        }
    }
    CHECK_EQ(t.differ, 0);
    CHECK(t.found > 1000);
    CHECK(t.lookups - t.found > 1000);
    CHECK(!t.index.Broken());
    CHECK_EQ(t.index.Size(), t.tree.size());
}

UNIT_TEST(path_index_resource_names) {
    static const char* const kNames[] = {
#include "data/resource_names.inc"
    };
    std::mt19937 rng(5);
    Indexed t;
    for (const char* n : kNames) {
        t.Insert(Normalised(n));
        t.Insert(Normalised(std::string(n) + ":resource"));
    }
    for (const char* n : kNames) {
        t.Probe(Normalised(n));
        t.Probe(FlipCase(Normalised(n), rng));
        t.Probe(n);   // raw '/' never matches a normalised key, in either
        t.Probe(Normalised(std::string(n) + "x"));
    }
    CHECK_EQ(t.differ, 0);
    // erase everything, probing as it empties
    for (const char* n : kNames) {
        t.Erase(Normalised(n));
        t.Probe(Normalised(n));
        t.Probe(Normalised(std::string(n) + ":resource"));
    }
    CHECK_EQ(t.differ, 0);
}

UNIT_TEST(path_index_declines) {
    Indexed t;
    t.Insert("a");
    t.Insert("b");
    const void* n = nullptr;
    CHECK(!t.index.Find(&t.tree, 2, nullptr, &n));          // a null search key
    CHECK(!t.index.Find(&t.tree, 3, "a", &n));              // a count that differs
    Tree other;
    CHECK(!t.index.Find(&other, 2, "a", &n));               // a tree it is not bound to
    CHECK(t.index.Find(&t.tree, 2, "A", &n) && n == &*t.tree.find("a"));
    // a second tree's insert breaks it for good (until Clear)
    other.emplace("c", 0);
    t.index.Insert(&other, &*other.begin());
    CHECK(!t.index.Find(&t.tree, 2, "a", &n));
    t.index.Clear(&t.tree);
    CHECK(!t.index.Broken());
    // an empty tree the index was never bound to: the answer is "none"
    Tree empty;
    FoldIndex fresh(&MapNodeKey);
    CHECK(fresh.Find(&empty, 0, "a", &n) && n == nullptr);
    // a null-keyed node: declines
    struct NullNode {
        const char* key;
    };
    static NullNode nn{nullptr};
    FoldIndex withNull([](const void* p) { return static_cast<const NullNode*>(p)->key; });
    int tree2 = 0;
    withNull.Insert(&tree2, &nn);
    CHECK(!withNull.Find(&tree2, 1, "a", &n));
    withNull.Erase(&tree2, &nn);
    CHECK(withNull.Find(&tree2, 0, "a", &n) && n == nullptr);
}

// ---- the MegaMod package directories (pkm_names.h) ---------------------------
using pkm_names::PackageNames;

UNIT_TEST(path_index_megamod_package_names) {
    const std::string dir = "C:/GOG Games/Banished/MegaMod.X.Complete.Build.261018/";
    std::vector<std::string> all;
    for (int i = 1; i <= 7; ++i) {
        const std::vector<std::string> names = PackageNames(dir + "MegaMod" + std::to_string(i) + ".pkm");
        all.insert(all.end(), names.begin(), names.end());
    }
    if (all.empty())
        UNIT_SKIP("the MegaMod packages are not installed");
    std::mt19937 rng(9);
    Indexed t;
    int64_t empties = 0;
    for (const std::string& n : all) {
        const std::string key = Normalised(n);
        if (key.empty()) {
            ++empties;
            continue;
        }
        t.Insert(key);
    }
    for (const std::string& n : all) {
        const std::string key = Normalised(n);
        if (key.empty())
            continue;
        t.Probe(key);
        t.Probe(FlipCase(key, rng));
        std::string slashed = key;
        for (char& c : slashed) {
            if (c == '\\')
                c = '/';
        }
        t.Probe(slashed);
        std::string changed = key;
        changed[rng() % changed.size()] ^= 1;
        t.Probe(changed);
    }
    std::printf("  [path_index] MegaMod: %zu names, %zu distinct keys, %lld empty, %lld lookups, %lld found\n",
                all.size(), t.tree.size(), static_cast<long long>(empties), static_cast<long long>(t.lookups),
                static_cast<long long>(t.found));
    CHECK_EQ(t.differ, 0);
    CHECK(t.tree.size() > 10000);
}

// The ExpandRelativePath memo's argument (src/engine/object/expand_memo.h, point
// 4): a null String on either side of operator+ yields the other side's characters.
UNIT_TEST(path_index_null_string_concat_is_the_other_side) {
    const System::String null;
    const System::String dir("Models\\");
    CHECK_STREQ((null + dir)._data, "Models\\");
    CHECK_STREQ((dir + null)._data, "Models\\");
    CHECK((null + null)._data == nullptr);
}
