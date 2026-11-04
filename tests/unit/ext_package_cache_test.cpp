// ============================================================================
// tests/unit/ext_package_cache_test.cpp -- PKGCACHE: the package directory
//   tree written to bytes and built back is the tree LoadPackages built
// ============================================================================
// `BuildLikeLoadPackages` is LoadPackages' third loop over test packages (the
// IndexFind descent with retail's `_stricmp`, the incumbent's override byte,
// rtavl::FilenameSetInsert). The tree it builds is encoded, decoded into a
// second tree, and compared node for node (SameTree: shape, key buffer
// addresses, values, balance, height), and every entry name is looked up in
// both. Over random package sets with overrides and case variants, and over
// the seven MegaMod parts when installed (401,167 entries). Then the refusals:
// a payload that does not fit the packages leaves the tree empty.
// ============================================================================
#include "unit.h"
#include "ext/package_cache/index_codec.h"
#include "collection/avl_trees.h"
#include "rt_crt.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "pkm_names.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace Ext::PackageCache;

namespace {

// The host has no seated MemoryAllocator: the tree nodes allocate through the
// instrument seat, armed with the CRT heap for each test (ext_sheet_builder_test.cpp
// does the same). Declared first in each test, so it outlives every Filename the test makes.
void* SeatAlloc(uint64_t size, int) {
    return std::malloc(static_cast<size_t>(size));
}
void SeatFree(void* p) {
    std::free(p);
}
struct HeapSeat {
    HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = &SeatAlloc;
        System::g_memoryAllocatorInstrument.memFree = &SeatFree;
    }
    ~HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = nullptr;
        System::g_memoryAllocatorInstrument.memFree = nullptr;
    }
};

struct TestPack {
    std::vector<RtIO::PackageFileEntry> entries;
    bool overridable = true;
};

std::vector<PackView> Views(const std::vector<TestPack>& packs) {
    std::vector<PackView> v;
    for (const TestPack& p : packs) {
        PackView pv;
        pv.entries = p.entries.data();
        pv.count = static_cast<int32_t>(p.entries.size());
        pv.overridable = p.overridable;
        v.push_back(pv);
    }
    return v;
}

// filesystem_packages.cpp's IndexFind (the third loop's descent).
RtIO::PackfileIndexNode* IndexFind(RtIO::PackfileIndexNode* node, const char* key) {
    while (node != nullptr) {
        if (node->element.key._data != nullptr) {
            if (key == nullptr) {
                node = node->left;
                continue;
            }
            if (rtcrt::StrICmp(key, node->element.key._data) < 0) {
                node = node->left;
                continue;
            }
        }
        if (key == nullptr)
            return node;
        if (node->element.key._data != nullptr && rtcrt::StrICmp(node->element.key._data, key) >= 0)
            return node;
        node = node->right;
    }
    return nullptr;
}

// LoadPackages' third loop.
void BuildLikeLoadPackages(const std::vector<TestPack>& packs, RtIO::PackfileIndexTree& tree) {
    for (uint32_t pi = 0; pi < packs.size(); ++pi) {
        for (uint32_t fi = 0; fi < packs[pi].entries.size(); ++fi) {
            System::Filename key(packs[pi].entries[fi].name);
            RtIO::PackfileIndexNode* node = IndexFind(tree.root, key._data);
            if (node != nullptr) {
                if (packs[node->element.value.packIndex].overridable)
                    node->element.value = RtIO::PackfileIndexValue{ pi, fi };
            } else {
                const RtIO::PackfileIndexValue value{ pi, fi };
                RtIO::PackfileIndexNode* out = nullptr;
                rtavl::FilenameSetInsert(&tree, &out, &key, &value);
            }
        }
    }
}

void Free(RtIO::PackfileIndexTree& tree) {
    rtavl::SubtreeFree(&tree, tree.root);
    tree.root = nullptr;
    tree.count = 0;
}

int g_nodesSeen = 0;
void CountNode(const RtIO::PackfileIndexTree&, const RtIO::PackfileIndexNode*) {
    ++g_nodesSeen;
}

// Encode, decode, compare; every name looked up in both trees.
bool RoundTrip(const std::vector<TestPack>& packs, std::string* why) {
    const std::vector<PackView> views = Views(packs);
    RtIO::PackfileIndexTree built{ nullptr, 0, 0 };
    BuildLikeLoadPackages(packs, built);
    std::vector<uint8_t> bytes;
    bool ok = Encode(built, views, bytes);
    RtIO::PackfileIndexTree restored{ nullptr, 0, 0 };
    g_nodesSeen = 0;
    ok = ok && Decode(bytes, views, restored, &CountNode);
    ok = ok && g_nodesSeen == restored.count;
    ok = ok && SameTree(built, restored, why);
    for (const TestPack& p : packs) {
        for (const RtIO::PackageFileEntry& e : p.entries) {
            const RtIO::PackfileIndexNode* a = IndexFind(built.root, e.name._data);
            const RtIO::PackfileIndexNode* b = IndexFind(restored.root, e.name._data);
            if (a == nullptr || b == nullptr || a->element.key._data != b->element.key._data ||
                a->element.value.packIndex != b->element.value.packIndex ||
                a->element.value.fileIndex != b->element.value.fileIndex) {
                ok = false;
                if (why != nullptr)
                    *why = std::string("lookup ") + e.name._data;
            }
        }
    }
    Free(restored);
    Free(built);
    return ok;
}

std::string RandomName(std::mt19937& rng) {
    static const char* const parts[] = {"Models", "models", "Template", "Textures", "Audio", "Tree", "TREE",
                                        "tree", "Field", "Crop", "a", "B", "_x", "[z]", "Wheat"};
    std::string s;
    const int n = 1 + static_cast<int>(rng() % 4);
    for (int i = 0; i < n; ++i) {
        if (i > 0)
            s += (rng() % 2) ? "\\" : "/";
        s += parts[rng() % (sizeof(parts) / sizeof(parts[0]))];
    }
    s += (rng() % 2) ? ".crs" : "_resource.crs";
    return s;
}

}  // namespace

UNIT_TEST(package_cache_round_trip_random_packages) {
    HeapSeat seat;
    std::mt19937 rng(2026);
    for (int round = 0; round < 40; ++round) {
        std::vector<TestPack> packs(1 + rng() % 6);
        for (TestPack& p : packs) {
            p.overridable = (rng() % 3) != 0;
            const int n = static_cast<int>(rng() % 400);
            for (int i = 0; i < n; ++i) {
                RtIO::PackageFileEntry e{};
                e.name = System::Filename(RandomName(rng).c_str());
                p.entries.push_back(e);
            }
        }
        std::string why;
        CHECK(RoundTrip(packs, &why));
        if (!why.empty())
            std::printf("  [package_cache] round %d: %s\n", round, why.c_str());
    }
}

UNIT_TEST(package_cache_key_bytes_follow_every_input) {
    std::vector<TestPack> packs(2);
    for (int i = 0; i < 3; ++i) {
        RtIO::PackageFileEntry e{};
        e.name = System::Filename(("Models\\M" + std::to_string(i) + ".crs").c_str());
        packs[0].entries.push_back(e);
        packs[1].entries.push_back(e);
    }
    std::vector<uint8_t> a, b;
    CHECK(KeyBytes(Views(packs), a));
    packs[1].overridable = false;
    CHECK(KeyBytes(Views(packs), b));
    CHECK(a != b);                                   // the override byte
    packs[1].overridable = true;
    std::swap(packs[0].entries[0], packs[0].entries[1]);
    CHECK(KeyBytes(Views(packs), b));
    CHECK(a != b);                                   // the entry order
    std::swap(packs[0].entries[0], packs[0].entries[1]);
    packs[0].entries[2].name = System::Filename("models\\M2.crs");
    CHECK(KeyBytes(Views(packs), b));
    CHECK(a != b);                                   // a case change
    std::swap(packs[0], packs[1]);
    packs[1].entries[2].name = System::Filename("Models\\M2.crs");
    CHECK(KeyBytes(Views(packs), b));
    CHECK(a == b);                                   // equal inputs, equal bytes
    RtIO::PackageFileEntry nullName{};
    packs[0].entries.push_back(nullName);
    CHECK(!KeyBytes(Views(packs), b));               // a null name: no key
}

UNIT_TEST(package_cache_refuses_a_payload_that_does_not_fit) {
    HeapSeat seat;
    std::mt19937 rng(7);
    std::vector<TestPack> packs(3);
    for (TestPack& p : packs) {
        for (int i = 0; i < 200; ++i) {
            RtIO::PackageFileEntry e{};
            e.name = System::Filename(RandomName(rng).c_str());
            p.entries.push_back(e);
        }
    }
    const std::vector<PackView> views = Views(packs);
    RtIO::PackfileIndexTree built{ nullptr, 0, 0 };
    BuildLikeLoadPackages(packs, built);
    std::vector<uint8_t> bytes;
    CHECK(Encode(built, views, bytes));

    auto refused = [&](const std::vector<uint8_t>& payload, const std::vector<PackView>& v) {
        RtIO::PackfileIndexTree t{ nullptr, 0, 0 };
        const bool ok = Decode(payload, v, t);
        const bool empty = t.root == nullptr && t.count == 0;
        if (ok)
            Free(t);
        return !ok && empty;
    };
    std::vector<uint8_t> bad = bytes;
    bad.push_back(0);
    CHECK(refused(bad, views));                                        // a trailing byte
    bad = bytes;
    bad.resize(bad.size() - 21);
    CHECK(refused(bad, views));                                        // a record short
    std::vector<PackView> fewer(views.begin(), views.end() - 1);
    CHECK(refused(bytes, fewer));                                      // another package set
    bad = bytes;
    bad[12 + 4] = 0xFF;                                                // the root's key: a file out of range
    bad[12 + 5] = 0xFF;
    CHECK(refused(bad, views));
    bad = bytes;
    std::swap_ranges(bad.begin() + 12, bad.begin() + 20, bad.begin() + 12 + 21);   // two keys swapped
    CHECK(refused(bad, views));
    bad = bytes;
    bad[12 + 20] = 0;                                                  // the root claims no children
    CHECK(refused(bad, views));
    // Review F-241: the root's value names the next node's entry, another path than its key.
    bad = bytes;
    std::copy(bad.begin() + 12 + 21, bad.begin() + 12 + 29, bad.begin() + 12 + 8);
    CHECK(refused(bad, views));

    // A key that is not an entry's buffer is not encoded.
    System::Filename stray("Models\\Stray.crs");
    built.root->element.key = stray;
    CHECK(!Encode(built, views, bytes));
    Free(built);
}

UNIT_TEST(package_cache_round_trip_megamod) {
    HeapSeat seat;
    const std::vector<std::vector<std::string>> parts = pkm_names::MegaModParts();
    if (parts.empty())
        UNIT_SKIP("the MegaMod packages are not installed");
    std::vector<TestPack> packs(parts.size());
    size_t total = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        for (const std::string& n : parts[i]) {
            RtIO::PackageFileEntry e{};
            e.name = System::Filename(n.c_str());
            packs[i].entries.push_back(e);
        }
        total += parts[i].size();
    }
    std::string why;
    CHECK(RoundTrip(packs, &why));
    if (!why.empty())
        std::printf("  [package_cache] MegaMod: %s\n", why.c_str());
    std::printf("  [package_cache] MegaMod: %zu entries round-tripped\n", total);
}

namespace {

// One package of n names, increasing under _stricmp, and a payload that hangs them all off one
// another as right children: a chain n levels deep.
std::vector<uint8_t> ChainPayload(uint32_t n, std::vector<TestPack>& packs) {
    packs.assign(1, TestPack());
    char name[32];
    for (uint32_t i = 0; i < n; ++i) {
        std::snprintf(name, sizeof(name), "f%06u.crs", i);
        RtIO::PackageFileEntry e{};
        e.name = System::Filename(name);
        packs[0].entries.push_back(e);
    }
    // "PKIX", the node count (set below), one package.
    std::vector<uint8_t> bytes = { 0x50, 0x4B, 0x49, 0x58, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 };
    std::memcpy(bytes.data() + 4, &n, 4);
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t lo = static_cast<uint8_t>(i);
        const uint8_t hi = static_cast<uint8_t>(i >> 8);
        const uint8_t record[21] = {
            0x00, 0x00, 0x00, 0x00, lo, hi, 0x00, 0x00,   // key: pack 0, file i
            0x00, 0x00, 0x00, 0x00, lo, hi, 0x00, 0x00,   // value: pack 0, file i
            0x00, 0x00, 0x00, 0x00,                       // balance, height
            static_cast<uint8_t>(i + 1 < n ? 0x02 : 0x00),  // children: a right one, none on the last
        };
        bytes.insert(bytes.end(), record, record + sizeof(record));
    }
    return bytes;
}

}  // namespace

// Review F-037: keys, values, child bits, size and in-order key order are all valid, so Decode
// took a chain of any length; the recursive SubtreeFree then overflows the stack on a deep enough
// one (400,000 levels in the fuzz input) and every IndexFind walks it linearly. An AVL tree of 2^31
// nodes is under 46 levels high, so a depth past 64 is refused.
UNIT_TEST(package_cache_refuses_a_degenerate_chain) {
    HeapSeat seat;
    std::vector<TestPack> packs;
    std::vector<uint8_t> bytes = ChainPayload(64, packs);
    RtIO::PackfileIndexTree t{ nullptr, 0, 0 };
    CHECK(Decode(bytes, Views(packs), t));   // 64 levels: taken
    CHECK_EQ(t.count, 64);
    Free(t);
    bytes = ChainPayload(200, packs);
    const bool ok = Decode(bytes, Views(packs), t);
    CHECK(!ok);                              // 200 levels: refused, the tree left empty
    CHECK(t.root == nullptr);
    CHECK_EQ(t.count, 0);
    if (ok)
        Free(t);
}
