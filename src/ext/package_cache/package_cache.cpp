// ============================================================================
// src/ext/package_cache/package_cache.cpp -- NOT RETAIL. ext.package_cache
// ============================================================================
// Class A (docs/extensions/handins/PKGCACHE.md); package_cache.h has the contract,
// index_codec.h the identity argument.
// ============================================================================

#include "ext/package_cache/package_cache.h"

#include "ext/derived_cache/derived_cache.h"
#include "ext/derived_cache/hash64.h"
#include "ext/package_cache/index_codec.h"
#include "ext/registry.h"
#include "io/FileSystem.h"
#include "io/PackageFile.h"
#include "io/packfile_fold_index.h"   // block PATHINDEX: the fold index beside the tree

#include <chrono>
#include <cstdio>
#include <utility>
#include <vector>

namespace Ext { namespace PackageCache {

namespace {

constexpr uint32_t kProducer = DerivedCache::MakeProducer('P', 'K', 'I', 'X');
constexpr uint32_t kVersion = 1;

// The key of the mount in progress, made by Restore and used by Built (both on the thread that
// runs LoadPackages, one after the other).
struct Pending {
    bool valid = false;
    DerivedCache::Key key;
};
Pending g_pending;

double MsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

std::vector<PackView> Packs(const IO::FileSystem& fs) {
    std::vector<PackView> packs;
    packs.reserve(static_cast<size_t>(fs._packfiles._size > 0 ? fs._packfiles._size : 0));
    for (int32_t pi = 0; pi < fs._packfiles._size; ++pi) {
        const IO::PackageFile* pkg = fs._packfiles[pi];
        PackView v;
        v.entries = pkg->_entries._data;
        v.count = pkg->_entries._size;
        v.overridable = pkg->_overridable != 0;
        packs.push_back(v);
    }
    return packs;
}

bool MakeKey(const std::vector<PackView>& packs, DerivedCache::Key& key) {
    std::vector<uint8_t> bytes;
    if (!KeyBytes(packs, bytes))
        return false;
    uint64_t entries = 0;
    for (const PackView& p : packs)
        entries += static_cast<uint64_t>(p.count);
    key.producer = kProducer;
    key.version = kVersion;
    key.shape[0] = static_cast<uint32_t>(packs.size());
    key.shape[1] = static_cast<uint32_t>(entries);
    key.shape[2] = 0;
    key.shape[3] = 0;
    key.sourceSize = bytes.size();
    key.sourceHash = DerivedCache::Hash64(bytes.data(), bytes.size());
    return true;
}

void AddToFoldIndex(const RtIO::PackfileIndexTree& tree, const RtIO::PackfileIndexNode* node) {
    ::IO::PackIndexInsert(&tree, node);
}

bool Restore(IO::FileSystem& fs) {
    g_pending.valid = false;
    if (!Ext().package_cache || fs._packfileIndex.root != nullptr)
        return false;
    DerivedCache::Store* store = DerivedCache::OpenShared();
    if (store == nullptr)
        return false;

    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<PackView> packs = Packs(fs);
    DerivedCache::Key key;
    if (!MakeKey(packs, key))
        return false;
    g_pending.valid = true;
    g_pending.key = key;

    std::vector<uint8_t> payload;
    if (!store->Find(key, payload))
        return false;
    if (!Decode(payload, packs, fs._packfileIndex, &AddToFoldIndex)) {
        std::fprintf(stderr, "[PackageCache] the cached directory did not fit the packages: built by the loop\n");
        // Its bytes match its record, so the store would keep it and refuse Built's Put of the
        // same key: dropped here, so that Put replaces it (review F-241).
        store->Forget(key);
        return false;
    }
    g_pending.valid = false;
    std::fprintf(stderr, "[PackageCache] directory of %d packages, %d entries restored from the cache (%.1f ms)\n",
                 fs._packfiles._size, fs._packfileIndex.count, MsSince(t0));
    return true;
}

void Built(IO::FileSystem& fs) {
    if (!g_pending.valid || !Ext().package_cache)
        return;
    g_pending.valid = false;
    DerivedCache::Store* store = DerivedCache::Shared();
    if (store == nullptr)
        return;
    std::vector<uint8_t> payload;
    if (!Encode(fs._packfileIndex, Packs(fs), payload))
        return;
    std::fprintf(stderr, "[PackageCache] directory of %d packages, %d entries stored (%zu bytes)\n",
                 fs._packfiles._size, fs._packfileIndex.count, payload.size());
    store->Put(g_pending.key, std::move(payload));
}

}  // namespace

void Sync() {
    const bool faithful = CurrentProfile() == Profile::Faithful;
    IO::FileSystem::_extIndexRestore = faithful ? nullptr : &Restore;
    IO::FileSystem::_extIndexBuilt = faithful ? nullptr : &Built;
}

}}  // namespace Ext::PackageCache
