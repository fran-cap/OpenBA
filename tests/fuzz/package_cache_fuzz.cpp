// ============================================================================
// tests/fuzz/package_cache_fuzz.cpp -- Ext::PackageCache::Decode over arbitrary payloads
// ============================================================================
// ext.package_cache restores FileSystem::_packfileIndex from a derived-cache payload (a file in the
// user's Documents folder: a local cache; its XXH64 hashes are not a signature). P0_MAP §8.
// The packages the payload must fit are made from the input too.
//
// Input, byte 0 = mode:
//   bits 0-1  package count - 1
//   bit 2     CHAIN: a synthetic payload of N nodes, each the right child of the one before
//             (N = the next 3 bytes, little-endian, at most 400000); byte 4 bit 0 = keys
//             increasing (the decode succeeds) or all the same key (the decode refuses after
//             building the chain). One package of N synthetic names.
//   bit 3     after a successful decode, insert one new name per entry (rtavl::FilenameSetInsert,
//             the loop's own insert: its AVL retrace reads the decoded balance / height).
//   bit 4     the target writes the 12-byte header (magic, node count = rest / 21, package count)
//             in front of the rest, so the fuzzer does not have to match the exact size.
// Otherwise: u16 names length, the names ('\n'-separated, a line "|" starts the next package),
// then the payload.
// ============================================================================
#include "ext/package_cache/index_codec.h"
#include "collection/avl_trees.h"
#include "rt_crt.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Ext::PackageCache;

namespace {

void* SeatAlloc(uint64_t size, int) {
    return std::malloc(static_cast<size_t>(size));
}
void SeatFree(void* p) {
    std::free(p);
}

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    const size_t at = out.size();
    out.resize(at + 4);
    std::memcpy(out.data() + at, &v, 4);
}

void Free(RtIO::PackfileIndexTree& tree) {
    rtavl::SubtreeFree(&tree, tree.root);
    tree.root = nullptr;
    tree.count = 0;
}

// filesystem_packages.cpp's IndexFind (the third loop's descent; ext_package_cache_test.cpp).
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

constexpr uint32_t kMagic = 0x58494B50u;   // "PKIX" (index_codec.cpp)

}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    System::g_memoryAllocatorInstrument.memAlloc = &SeatAlloc;
    System::g_memoryAllocatorInstrument.memFree = &SeatFree;
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1 || size > 0x40000)
        return 0;
    const uint8_t mode = data[0];
    std::vector<std::vector<RtIO::PackageFileEntry>> packs;
    std::vector<uint8_t> payload;

    if (mode & 4) {
        if (size < 5)
            return 0;
        uint32_t n = data[1] | (data[2] << 8) | (static_cast<uint32_t>(data[3]) << 16);
        if (n == 0 || n > 400000)
            return 0;
        // MASK (finding: a decoded chain recurses SubtreeFree once per node, a stack overflow at
        // ~10^5 nodes under ASan; and IndexFind over a chain is O(N) per lookup, so N names time
        // out): proven with N = 400000 on a 5-byte input, capped here so the fuzzer reaches the rest.
        if (n > 2000)
            n = 2000;
        const bool increasing = (data[4] & 1) != 0;
        packs.resize(1);
        char name[32];
        for (uint32_t i = 0; i < (increasing ? n : 1u); ++i) {
            std::snprintf(name, sizeof(name), "n%08u", i);
            RtIO::PackageFileEntry e{};
            e.name = System::Filename(name);
            packs[0].push_back(e);
        }
        PutU32(payload, kMagic);
        PutU32(payload, n);
        PutU32(payload, 1);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t file = increasing ? i : 0;
            PutU32(payload, 0);
            PutU32(payload, file);
            PutU32(payload, 0);
            PutU32(payload, file);
            const int16_t bal = 0;
            const int16_t height = 1;
            payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&bal),
                           reinterpret_cast<const uint8_t*>(&bal) + 2);
            payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&height),
                           reinterpret_cast<const uint8_t*>(&height) + 2);
            payload.push_back(i + 1 < n ? 2 : 0);   // a right child, the last one a leaf
        }
    } else {
        if (size < 3)
            return 0;
        const size_t namesLen = data[1] | (data[2] << 8);
        if (3 + namesLen > size)
            return 0;
        const uint32_t maxPacks = 1 + (mode & 3);
        packs.resize(1);
        std::string line;
        auto flush = [&]() {
            if (line == "|") {
                if (packs.size() < maxPacks)
                    packs.emplace_back();
            } else {
                RtIO::PackageFileEntry e{};
                e.name = System::Filename(line.c_str());
                packs.back().push_back(e);
            }
            line.clear();
        };
        // MASK (finding: Filename(const char*)'s AdjustDelimiters loops forever on some "\.."
        // names, e.g. 5c 2e 2e 5c 5c 2e 2e 08 2e; package_cache2_fuzz.cpp isolates it): names with
        // ".." are not made here.
        for (size_t i = 0; i + 1 < namesLen; ++i) {
            if (data[3 + i] == '.' && data[4 + i] == '.')
                return 0;
        }
        for (size_t i = 0; i < namesLen; ++i) {
            const char c = static_cast<char>(data[3 + i]);
            if (c == '\n')
                flush();
            else if (c != '\0' && !(c == '.' && !line.empty() && line.back() == '.'))
                line.push_back(c);   // the ".." MASK above, also across a dropped NUL
        }
        if (!line.empty())
            flush();
        const uint8_t* rest = data + 3 + namesLen;
        const size_t restLen = size - 3 - namesLen;
        if (mode & 16) {
            PutU32(payload, kMagic);
            PutU32(payload, static_cast<uint32_t>(restLen / 21));
            PutU32(payload, static_cast<uint32_t>(packs.size()));
            payload.insert(payload.end(), rest, rest + (restLen / 21) * 21);
        } else {
            payload.assign(rest, rest + restLen);
        }
    }

    std::vector<PackView> views;
    for (const auto& p : packs) {
        PackView v;
        v.entries = p.data();
        v.count = static_cast<int32_t>(p.size());
        v.overridable = true;
        views.push_back(v);
    }
    RtIO::PackfileIndexTree tree{ nullptr, 0, 0 };
    if (Decode(payload, views, tree, nullptr)) {
        for (const auto& p : packs) {
            for (const RtIO::PackageFileEntry& e : p)
                IndexFind(tree.root, e.name._data);
        }
        if (mode & 8) {
            uint32_t pi = 0;
            for (const auto& p : packs) {
                for (uint32_t fi = 0; fi < p.size(); ++fi) {
                    const std::string s = std::string(p[fi].name._data) + "~";
                    System::Filename key(s.c_str());
                    const RtIO::PackfileIndexValue value{ pi, fi };
                    RtIO::PackfileIndexNode* out = nullptr;
                    rtavl::FilenameSetInsert(&tree, &out, &key, &value);
                }
                ++pi;
            }
        }
        Free(tree);
    }
    return 0;
}

// error.cpp (in the archive) reaches the platform's message box; the unit suite's
// error_display_test.cpp defines it, so the archive does not. A local stub (never called here).
namespace Platform {
namespace Window {
void ShowErrorMessage(const uint16_t*, const uint16_t*) {
}
}  // namespace Window
}  // namespace Platform
