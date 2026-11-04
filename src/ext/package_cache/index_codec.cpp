// ============================================================================
// src/ext/package_cache/index_codec.cpp -- NOT RETAIL. ext.package_cache's
//   tree <-> bytes (index_codec.h has the contract)
// ============================================================================

#include "ext/package_cache/index_codec.h"

#include "collection/avl_trees.h"   // rtavl::FilenameNodeAlloc / SubtreeFree: the loop's own node
#include "rt_crt.h"                 // rtcrt::StrICmp: the loop's comparator

#include <cstring>
#include <unordered_map>

namespace Ext {
namespace PackageCache {

namespace {

using Node = RtIO::PackfileIndexNode;

constexpr uint32_t kMagic = 0x58494B50u;   // "PKIX"
constexpr size_t kHeaderBytes = 12;        // magic, node count, package count
constexpr size_t kRecordBytes = 21;        // key pack, key file, value pack, value file, balance, height, children
constexpr uint8_t kHasLeft = 1;
constexpr uint8_t kHasRight = 2;
constexpr uint32_t kMaxDepth = 64;         // Decode: deeper than any AVL tree of 2^31 nodes

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    const size_t at = out.size();
    out.resize(at + 4);
    std::memcpy(out.data() + at, &v, 4);
}

void PutI16(std::vector<uint8_t>& out, int16_t v) {
    const size_t at = out.size();
    out.resize(at + 2);
    std::memcpy(out.data() + at, &v, 2);
}

uint32_t GetU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

int16_t GetI16(const uint8_t* p) {
    int16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

bool ValidEntry(const std::vector<PackView>& packs, uint32_t pack, uint32_t file) {
    return pack < packs.size() && file < static_cast<uint32_t>(packs[pack].count) &&
           packs[pack].entries[file].name._data != nullptr;
}

// Strictly increasing `_stricmp` order in order: the tree is a search tree under the loop's
// comparator and holds no two equal keys.
bool InOrderStrict(const Node* root) {
    std::vector<const Node*> stack;
    const char* prev = nullptr;
    const Node* n = root;
    while (n != nullptr || !stack.empty()) {
        while (n != nullptr) {
            stack.push_back(n);
            n = n->left;
        }
        n = stack.back();
        stack.pop_back();
        const char* key = n->element.key._data;
        if (prev != nullptr && rtcrt::StrICmp(prev, key) >= 0)
            return false;
        prev = key;
        n = n->right;
    }
    return true;
}

void FreeAll(RtIO::PackfileIndexTree& tree) {
    rtavl::SubtreeFree(&tree, tree.root);
    tree.root = nullptr;
    tree.count = 0;
}

}  // namespace

bool KeyBytes(const std::vector<PackView>& packs, std::vector<uint8_t>& out) {
    out.clear();
    PutU32(out, static_cast<uint32_t>(packs.size()));
    for (const PackView& p : packs) {
        PutU32(out, static_cast<uint32_t>(p.count));
        out.push_back(p.overridable ? 1 : 0);
        for (int32_t i = 0; i < p.count; ++i) {
            const char* name = p.entries[i].name._data;
            if (name == nullptr)
                return false;
            const size_t len = std::strlen(name);
            PutU32(out, static_cast<uint32_t>(len));
            const size_t at = out.size();
            out.resize(at + len);
            std::memcpy(out.data() + at, name, len);
        }
    }
    return true;
}

bool Encode(const RtIO::PackfileIndexTree& tree, const std::vector<PackView>& packs, std::vector<uint8_t>& out) {
    // Which entry's name buffer each node's key is.
    std::unordered_map<const char*, uint64_t> owner;
    size_t total = 0;
    for (const PackView& p : packs)
        total += static_cast<size_t>(p.count);
    owner.reserve(total);
    for (uint32_t pi = 0; pi < packs.size(); ++pi) {
        for (int32_t fi = 0; fi < packs[pi].count; ++fi)
            owner.emplace(packs[pi].entries[fi].name._data, (static_cast<uint64_t>(pi) << 32) | static_cast<uint32_t>(fi));
    }

    out.clear();
    out.reserve(kHeaderBytes + static_cast<size_t>(tree.count > 0 ? tree.count : 0) * kRecordBytes);
    PutU32(out, kMagic);
    PutU32(out, static_cast<uint32_t>(tree.count));
    PutU32(out, static_cast<uint32_t>(packs.size()));

    uint32_t written = 0;
    std::vector<const Node*> stack;
    if (tree.root != nullptr)
        stack.push_back(tree.root);
    while (!stack.empty()) {
        const Node* n = stack.back();
        stack.pop_back();
        auto it = owner.find(n->element.key._data);
        if (it == owner.end())
            return false;
        PutU32(out, static_cast<uint32_t>(it->second >> 32));
        PutU32(out, static_cast<uint32_t>(it->second));
        PutU32(out, n->element.value.packIndex);
        PutU32(out, n->element.value.fileIndex);
        PutI16(out, n->balance);
        PutI16(out, n->height);
        out.push_back(static_cast<uint8_t>((n->left != nullptr ? kHasLeft : 0) | (n->right != nullptr ? kHasRight : 0)));
        ++written;
        // Pre-order: the left subtree comes out first, so it goes on the stack last.
        if (n->right != nullptr)
            stack.push_back(n->right);
        if (n->left != nullptr)
            stack.push_back(n->left);
    }
    return written == static_cast<uint32_t>(tree.count);
}

bool Decode(const std::vector<uint8_t>& payload, const std::vector<PackView>& packs, RtIO::PackfileIndexTree& tree,
            void (*onNode)(const RtIO::PackfileIndexTree& tree, const RtIO::PackfileIndexNode* node)) {
    if (tree.root != nullptr || tree.count != 0)
        return false;
    if (payload.size() < kHeaderBytes || GetU32(payload.data()) != kMagic)
        return false;
    const uint32_t nodes = GetU32(payload.data() + 4);
    if (GetU32(payload.data() + 8) != packs.size() || nodes > 0x7FFFFFFFu ||
        payload.size() != kHeaderBytes + static_cast<size_t>(nodes) * kRecordBytes)
        return false;

    struct Open {
        Node* node;
        uint32_t depth;    // the node's level, the root's 1
        uint8_t pending;   // kHasLeft / kHasRight still to attach
    };
    std::vector<Open> stack;
    const uint8_t* p = payload.data() + kHeaderBytes;
    for (uint32_t i = 0; i < nodes; ++i, p += kRecordBytes) {
        const uint32_t keyPack = GetU32(p);
        const uint32_t keyFile = GetU32(p + 4);
        const uint32_t valuePack = GetU32(p + 8);
        const uint32_t valueFile = GetU32(p + 12);
        const uint8_t children = p[20];
        if (!ValidEntry(packs, keyPack, keyFile) || !ValidEntry(packs, valuePack, valueFile) || children > 3 ||
            (i == 0) != stack.empty()) {
            FreeAll(tree);
            return false;
        }
        // The value is an entry of the key's own path (review F-241): Encode writes it so, and a
        // value naming another entry would resolve the key to that other file.
        const char* const keyName = packs[keyPack].entries[keyFile].name._data;
        const char* const valueName = packs[valuePack].entries[valueFile].name._data;
        if (rtcrt::StrICmp(keyName, valueName) != 0) {
            FreeAll(tree);
            return false;
        }

        Node* parent = nullptr;
        bool asLeft = false;
        uint32_t depth = 1;
        if (i != 0) {
            Open& top = stack.back();
            parent = top.node;
            depth = top.depth + 1;
            asLeft = (top.pending & kHasLeft) != 0;
            top.pending &= static_cast<uint8_t>(asLeft ? ~kHasLeft : ~kHasRight);
        }
        // The shape is the payload's: a chain of any length would pass every other check, and the
        // recursive SubtreeFree / the IndexFind descent go once per level. An AVL tree of 2^31 nodes
        // is under 46 levels high, so a deeper one is refused (FreeAll is still shallow here).
        if (depth > kMaxDepth) {
            FreeAll(tree);
            return false;
        }

        const RtIO::PackfileIndexPair pair{ packs[keyPack].entries[keyFile].name, { valuePack, valueFile } };
        Node* node = rtavl::FilenameNodeAlloc(nullptr, &pair, parent);
        if (node == nullptr) {
            FreeAll(tree);
            return false;
        }
        if (parent == nullptr)
            tree.root = node;
        else if (asLeft)
            parent->left = node;
        else
            parent->right = node;
        node->balance = GetI16(p + 16);
        node->height = GetI16(p + 18);
        tree.count += 1;

        if (!stack.empty() && stack.back().pending == 0)
            stack.pop_back();
        if (children != 0)
            stack.push_back(Open{ node, depth, children });
    }
    if (!stack.empty() || !InOrderStrict(tree.root)) {
        FreeAll(tree);
        return false;
    }

    if (onNode != nullptr) {
        std::vector<const Node*> walk;
        if (tree.root != nullptr)
            walk.push_back(tree.root);
        while (!walk.empty()) {
            const Node* n = walk.back();
            walk.pop_back();
            onNode(tree, n);
            if (n->right != nullptr)
                walk.push_back(n->right);
            if (n->left != nullptr)
                walk.push_back(n->left);
        }
    }
    return true;
}

bool SameTree(const RtIO::PackfileIndexTree& a, const RtIO::PackfileIndexTree& b, std::string* why) {
    auto fail = [why](const char* what, const Node* n) {
        if (why != nullptr) {
            *why = what;
            if (n != nullptr && n->element.key._data != nullptr)
                *why += std::string(" at ") + n->element.key._data;
        }
        return false;
    };
    if (a.count != b.count)
        return fail("count", nullptr);
    std::vector<std::pair<const Node*, const Node*>> stack;
    if ((a.root == nullptr) != (b.root == nullptr))
        return fail("root", nullptr);
    if (a.root != nullptr)
        stack.emplace_back(a.root, b.root);
    while (!stack.empty()) {
        const Node* x = stack.back().first;
        const Node* y = stack.back().second;
        stack.pop_back();
        if (x->element.key._data != y->element.key._data)
            return fail("key buffer", x);
        if (x->element.value.packIndex != y->element.value.packIndex ||
            x->element.value.fileIndex != y->element.value.fileIndex)
            return fail("value", x);
        if (x->balance != y->balance || x->height != y->height)
            return fail("balance/height", x);
        if ((x->left == nullptr) != (y->left == nullptr) || (x->right == nullptr) != (y->right == nullptr))
            return fail("shape", x);
        if ((x->parent == nullptr) != (y->parent == nullptr))
            return fail("parent", x);
        if (x->left != nullptr)
            stack.emplace_back(x->left, y->left);
        if (x->right != nullptr)
            stack.emplace_back(x->right, y->right);
    }
    return true;
}

}  // namespace PackageCache
}  // namespace Ext
