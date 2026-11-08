// ============================================================================
// src/ext/menu_entry/menu_entry.cpp -- NOT RETAIL. ext.menu_entry
// ============================================================================
// Class A (docs/extensions/handins/MENUENTRY.md); menu_entry.h has the contract, fingerprint.h the
// argument for the remount skip.
// ============================================================================

#include "ext/menu_entry/menu_entry.h"

#include "ext/menu_entry/fingerprint.h"
#include "ext/package_cache/index_codec.h"   // KeyBytes / Encode: the shadow's package comparison
#include "ext/registry.h"
#include "collection/fold_index.h"           // OPENBA_PATHINDEX_CHECK: the debug exe's checks
#include "io/FileSystem.h"
#include "io/Modification.h"
#include "io/PackageFile.h"
#include "io/packfile_fold_index.h"          // PackIndexFind: the fold index a skip keeps
#include "platform_file.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "system/WideString.h"

#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Ext {
namespace MenuEntry {

namespace {

double MsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

std::string Chars(const System::String& s) {
    return s._data != nullptr ? std::string(s._data) : std::string();
}

// Name, size and last write time of one file, read through its own handle.
FileStamp Stat(const std::string& path) {
    FileStamp s;
    s.name = path;
    const System::String str(path.c_str());
    const System::WideString ws(str);
    const Platform::FileHandle h = Platform::FileOpenRead(ws._data);
    if (h == nullptr || h == Platform::kInvalidFileHandle)
        return s;
    s.size = Platform::FileSize64(h);
    uint64_t t = 0;
    if (Platform::FileLastWrite(h, &t) != 0)
        s.writeTime = t;
    Platform::FileClose(h);
    return s;
}

FileStamp StatFor(void*, const std::string& path) {
    return Stat(path);
}

// ============================================================================
// (a) The mod scan
// ============================================================================

// One .pkm's record as loop 1 built it before the settings part (enabled 0, order 0, no `files`),
// or a file retail discarded.
struct Record {
    FileStamp                  stamp;
    bool                       discarded = false;
    std::unique_ptr<RtIO::Mod> mod;
};

struct Scan {
    std::map<std::string, Record>    records;    // by the .pkm path's characters
    std::map<std::string, FileStamp> pending;    // stamped before a read, for the store
    std::set<std::string>            seen;       // the paths this scan asked about
    OverrideAnswer                   overrides;
    bool                             overridesValid = false;
    std::vector<FileStamp>           pendingKey;
    int32_t                          hits = 0;
    int32_t                          reads = 0;
    double                           fillMs = 0.0;   // copying kept records out, this scan
    // The debug exe's shadow: a kept record is not served; the file is read and the read's record
    // compared with it, and loop 2 runs and is compared with its kept answer.
    std::map<std::string, Record>    shadowExpect;
    int32_t                          shadowSame = 0;
    int32_t                          shadowDiffer = 0;
    bool                             shadowOverrides = false;
    std::vector<std::vector<int32_t>> shadowCollides;
};
// Never destroyed: the records hold game-allocator memory (strings, arrays, path sets), and the
// allocator is gone before static destructors run at exit.
Scan& ScanState() {
    static Scan* const s = new Scan;
    return *s;
}
Scan& g_scan = ScanState();

void ForgetScan() {
    ScanState() = Scan();
}

bool SameChars(const char* a, const char* b) {
    if (a == nullptr || b == nullptr)
        return a == b;
    return std::strcmp(a, b) == 0;
}

// Two path sets with the same shape, keys (by characters), balance and height.
bool SameSet(const RtIO::ModFilenameSet& a, const RtIO::ModFilenameSet& b) {
    if (a.GetCount() != b.GetCount())
        return false;
    using Node = RtIO::ModFilenameSet::Node;
    std::vector<std::pair<const Node*, const Node*>> stack;
    if ((a._root == nullptr) != (b._root == nullptr))
        return false;
    if (a._root != nullptr)
        stack.emplace_back(a._root, b._root);
    while (!stack.empty()) {
        const Node* x = stack.back().first;
        const Node* y = stack.back().second;
        stack.pop_back();
        if (!SameChars(x->element._data, y->element._data) || x->balance != y->balance ||
            x->height != y->height)
            return false;
        if ((x->left == nullptr) != (y->left == nullptr) || (x->right == nullptr) != (y->right == nullptr))
            return false;
        if (x->left != nullptr)
            stack.emplace_back(x->left, y->left);
        if (x->right != nullptr)
            stack.emplace_back(x->right, y->right);
    }
    return true;
}

template <class T>
bool SameArray(const Collection::Array<T>& a, const Collection::Array<T>& b) {
    if (a._size != b._size)
        return false;
    for (int32_t i = 0; i < a._size; ++i) {
        if (!(a._data[i] == b._data[i]))
            return false;
    }
    return true;
}

// The first member two loop-1 records differ in, or null.
const char* RecordDifference(const RtIO::Mod& a, const RtIO::Mod& b) {
    if (!SameChars(a.name._data, b.name._data) || !SameChars(a.author._data, b.author._data) ||
        !SameChars(a.description._data, b.description._data))
        return "strings";
    if (a.version != b.version || a.userVersion != b.userVersion || a.order != b.order || a.iconWidth != b.iconWidth || a.iconHeight != b.iconHeight ||
        a.crc != b.crc || a.enabled != b.enabled)
        return "ints";
    if (!SameArray(a.ints, b.ints))
        return "icon";
    if (!SameArray(a.bytes, b.bytes))
        return "preview";
    if (!SameChars(a.file._data, b.file._data))
        return "file";
    if (!SameSet(a.set, b.set))
        return "path set";
    if (a.files._size != 0 || b.files._size != 0)
        return "files";
    return nullptr;
}

int ModCached(const System::Filename& file, RtIO::Mod& out) {
    if (!Ext().menu_entry) {
        if (!g_scan.records.empty())
            ForgetScan();
        return IO::Modification::kModRead;
    }
    const FileStamp s = Stat(Chars(file));
    g_scan.seen.insert(s.name);
    auto it = g_scan.records.find(s.name);
    if (s.size >= 0 && it != g_scan.records.end() && it->second.stamp == s) {
        ++g_scan.hits;
        if (OPENBA_PATHINDEX_CHECK) {
            g_scan.shadowExpect[s.name] = std::move(it->second);
            g_scan.records.erase(it);
            g_scan.pending[s.name] = s;
            ++g_scan.reads;
            return IO::Modification::kModRead;
        }
        if (it->second.discarded)
            return IO::Modification::kModSkipped;
        // Everything the read would have built; `file` stays the one loop 1 set from FindFiles.
        const auto t0 = std::chrono::steady_clock::now();
        const System::Filename keep(out.file);
        out = *it->second.mod;   // Mod::operator= 0x180026ef0: strings shared, arrays and set copied
        out.file = keep;
        g_scan.fillMs += MsSince(t0);
        return IO::Modification::kModFilled;
    }
    ++g_scan.reads;
    g_scan.pending[s.name] = s;
    return IO::Modification::kModRead;
}

void ModStored(const System::Filename& file, const RtIO::Mod* record) {
    if (!Ext().menu_entry)
        return;
    const std::string name = Chars(file);
    Record r;
    auto p = g_scan.pending.find(name);
    r.stamp = p != g_scan.pending.end() ? p->second : Stat(name);
    if (p != g_scan.pending.end())
        g_scan.pending.erase(p);
    if (r.stamp.size < 0)
        return;   // not stat-able: never served from here
    r.discarded = record == nullptr;
    if (record != nullptr)
        r.mod.reset(new RtIO::Mod(*record));   // Mod's copy ctor 0x1800101e0
    auto shadow = g_scan.shadowExpect.find(name);
    if (shadow != g_scan.shadowExpect.end()) {
        const char* diff = nullptr;
        if (shadow->second.discarded != r.discarded)
            diff = "discarded";
        else if (record != nullptr)
            diff = RecordDifference(*shadow->second.mod, *record);
        if (diff == nullptr) {
            ++g_scan.shadowSame;
        } else {
            ++g_scan.shadowDiffer;
            std::fprintf(stderr, "[MenuEntry] shadow MISMATCH: kept record of %s differs in %s\n",
                         name.c_str(), diff);
        }
        g_scan.shadowExpect.erase(shadow);
    }
    g_scan.records[name] = std::move(r);
}

// The stamps of the found mods in their order, from this scan's records.
bool ScanKey(const IO::Modification& mod, std::vector<FileStamp>& key) {
    key.clear();
    for (int32_t i = 0; i < mod._mods._size; ++i) {
        auto it = g_scan.records.find(Chars(mod._mods._data[i].file));
        if (it == g_scan.records.end() || it->second.discarded)
            return false;
        key.push_back(it->second.stamp);
    }
    return true;
}

// A record of a .pkm this scan did not find is dropped (the file went away or was renamed).
void PruneUnseen() {
    for (auto it = g_scan.records.begin(); it != g_scan.records.end();) {
        if (g_scan.seen.count(it->first) == 0)
            it = g_scan.records.erase(it);
        else
            ++it;
    }
    g_scan.seen.clear();
    g_scan.pending.clear();
}

void LogScan(const IO::Modification& mod, const char* overrides) {
    std::fprintf(stderr,
                 "[MenuEntry] mod scan: %d mods, %d .pkm from the last scan (records copied in %.1f ms), "
                 "%d read; collisions %s\n",
                 mod._mods._size, g_scan.hits, g_scan.fillMs, g_scan.reads, overrides);
    if (OPENBA_PATHINDEX_CHECK && g_scan.shadowSame + g_scan.shadowDiffer > 0) {
        std::fprintf(stderr, "[MenuEntry] shadow %s: %d kept records read again, %d same, %d differ\n",
                     g_scan.shadowDiffer == 0 ? "SAME" : "MISMATCH", g_scan.shadowSame + g_scan.shadowDiffer,
                     g_scan.shadowSame, g_scan.shadowDiffer);
    }
    g_scan.shadowSame = 0;
    g_scan.shadowDiffer = 0;
    g_scan.shadowExpect.clear();
    g_scan.hits = 0;
    g_scan.fillMs = 0.0;
    g_scan.reads = 0;
}

bool OverridesRestore(IO::Modification& mod) {
    if (!Ext().menu_entry)
        return false;
    PruneUnseen();
    std::vector<FileStamp> key;
    const bool keyed = ScanKey(mod, key);
    g_scan.pendingKey = keyed ? key : std::vector<FileStamp>();
    if (!keyed || !g_scan.overridesValid || key != g_scan.overrides.key ||
        g_scan.overrides.collides.size() != static_cast<size_t>(mod._mods._size)) {
        return false;
    }
    if (OPENBA_PATHINDEX_CHECK) {
        g_scan.shadowOverrides = true;
        g_scan.shadowCollides = g_scan.overrides.collides;
        return false;
    }
    // Loop 2's appends, in its order: for each mod i, mods[j].file for every j it collides with.
    RtIO::Mod* const mods = mod._mods._data;
    for (int32_t i = 0; i < mod._mods._size; ++i) {
        for (const int32_t j : g_scan.overrides.collides[static_cast<size_t>(i)]) {
            RtIO::RtColl_ArraySetSizeFill_180026880(&mods[i].files, mods[i].files._size + 1, &mods[j].file);
        }
    }
    LogScan(mod, "from the last scan");
    return true;
}

void OverridesBuilt(const IO::Modification& mod) {
    if (!Ext().menu_entry)
        return;
    g_scan.overridesValid = false;
    if (g_scan.pendingKey.size() != static_cast<size_t>(mod._mods._size)) {
        LogScan(mod, "computed (not kept: a mod has no record)");
        return;
    }
    OverrideAnswer a;
    a.key = g_scan.pendingKey;
    const RtIO::Mod* const mods = mod._mods._data;
    for (int32_t i = 0; i < mod._mods._size; ++i) {
        std::vector<int32_t> js;
        int32_t from = 0;
        for (int32_t e = 0; e < mods[i].files._size; ++e) {
            const char* f = mods[i].files._data[e]._data;
            int32_t found = -1;
            // The loop appends in ascending j, a copy of mods[j].file (the same buffer).
            for (int32_t j = from; j < mod._mods._size; ++j) {
                if (j != i && mods[j].file._data == f) {
                    found = j;
                    break;
                }
            }
            if (found < 0) {
                LogScan(mod, "computed (not kept: an entry is not a mod's file)");
                return;
            }
            js.push_back(found);
            from = found + 1;
        }
        a.collides.push_back(std::move(js));
    }
    const bool shadow = g_scan.shadowOverrides;
    const bool same = a.collides == g_scan.shadowCollides;
    g_scan.shadowOverrides = false;
    g_scan.overrides = std::move(a);
    g_scan.overridesValid = true;
    if (shadow)
        LogScan(mod, same ? "computed; shadow SAME as the kept answer"
                          : "computed; shadow MISMATCH with the kept answer");
    else
        LogScan(mod, "computed");
}

// The boot's first scan (Engine::Startup, engine.mods) runs before the registry exists, so its
// reads are not stored. Its records are still in the mod list: loop 1's record is the mod as it
// stands with the settings part undone (enabled and order back to the 0 loop 1 leaves them at)
// and loop 2's `files` freed (~Array<Filename>'s body, 0x1800098d0: the header ends zeroed, as a
// record born in loop 1 has it). Each is stamped now; the boot's second scan (the main menu)
// then reads only a .pkm that changed in between. Files the first scan discarded have no mod and
// are read again.
void PrimeFromBootScan() {
    const IO::Modification* mod = IO::Modification::_singleton;
    if (!Ext().menu_entry || mod == nullptr)
        return;
    int32_t primed = 0;
    for (int32_t i = 0; i < mod->_mods._size; ++i) {
        const RtIO::Mod& m = mod->_mods._data[i];
        const FileStamp s = Stat(Chars(m.file));
        if (s.size < 0)
            continue;
        Record r;
        r.stamp = s;
        r.mod.reset(new RtIO::Mod(m));   // Mod's copy ctor 0x1800101e0
        r.mod->enabled = 0;
        r.mod->order = 0;
        r.mod->files.DestroyAndFree();   // ~Array<Filename>'s body: elements, buffer, zeroed header
        g_scan.records[s.name] = std::move(r);
        ++primed;
    }
    std::fprintf(stderr, "[MenuEntry] %d mod records kept from the boot's first scan\n", primed);
}

// ============================================================================
// (b) The remount
// ============================================================================

struct Remount {
    bool        stored = false;
    Fingerprint last;          // taken at the end of the last completed remount
    bool        inRemount = false;
    int32_t     skips = 0;
    // The debug exe's shadow: the package state before the remount it would have skipped.
    bool                 shadow = false;
    std::vector<uint8_t> shadowKey;
    std::vector<uint8_t> shadowTree;
    std::vector<std::string> shadowPacks;
    std::vector<std::string> shadowUser;
    std::chrono::steady_clock::time_point shadowStart;
};
Remount g_remount;

std::vector<std::string> BasePackages(const IO::FileSystem& fs) {
    RtIO::FilenameArray found = { nullptr, 0, 0 };
    {
        const System::String pattern("*.pkg");   // LoadPackages' pattern, the same FindFiles
        const System::Filename packDir(fs.GetPackDirectory());
        fs.FindFiles(packDir.AsString(), pattern, false, &found);
    }
    std::vector<std::string> names;
    for (int32_t i = 0; i < found.size; ++i) {
        names.push_back(Chars(found.data[i]));
        found.data[i].~Filename();
    }
    System::MemoryAllocator::MemFree(found.data);
    return names;
}

std::vector<MountedPack> Mounted(const IO::FileSystem& fs) {
    std::vector<MountedPack> out;
    for (int32_t pi = 0; pi < fs._packfiles._size; ++pi) {
        const IO::PackageFile* pkg = fs._packfiles[pi];
        MountedPack m;
        m.object = reinterpret_cast<uintptr_t>(pkg);   // CAST-OK: identity only, never dereferenced
        m.handle = (static_cast<uint64_t>(static_cast<uint32_t>(pkg->_file.handle)) << 32) ^
                   reinterpret_cast<uintptr_t>(pkg->_file.winHandle);   // CAST-OK: identity only
        m.name = Chars(pkg->_name);
        m.entries = pkg->_entries._size;
        m.priority = pkg->_priority;
        m.crc = pkg->_crc;
        m.overridable = pkg->_overridable;
        out.push_back(std::move(m));
    }
    return out;
}

// What a remount would mount now (`fromMods`: act 2's list from the mods), or what the one that
// just ended mounted (the user pack list it built).
Fingerprint Current(const IO::Modification& mod, const IO::FileSystem& fs, bool fromMods) {
    Fingerprint f;
    f.base = Stamps(BasePackages(fs), &StatFor, nullptr);
    std::vector<std::string> user;
    if (fromMods) {
        std::vector<ModView> views;
        for (int32_t i = 0; i < mod._mods._size; ++i)
            views.push_back(ModView{ Chars(mod._mods._data[i].file), mod._mods._data[i].enabled != 0 });
        user = UserPackOrder(views);
    } else {
        for (int32_t i = 0; i < fs._userPackFilenames.size; ++i)
            user.push_back(Chars(fs._userPackFilenames.data[i]));
    }
    f.user = Stamps(user, &StatFor, nullptr);
    f.mounted = Mounted(fs);
    f.existsInPackfileGate = fs._existsInPackfileGate;
    f.platformExistsGate = fs._platformExistsGate;
    f.largePackages = Fix().large_packages;
    f.packageCache = Ext().package_cache;
    return f;
}

std::vector<PackageCache::PackView> Packs(const IO::FileSystem& fs) {
    std::vector<PackageCache::PackView> packs;
    for (int32_t pi = 0; pi < fs._packfiles._size; ++pi) {
        const IO::PackageFile* pkg = fs._packfiles[pi];
        PackageCache::PackView v;
        v.entries = pkg->_entries._data;
        v.count = pkg->_entries._size;
        v.overridable = pkg->_overridable != 0;
        packs.push_back(v);
    }
    return packs;
}

std::vector<std::string> PackSummary(const IO::FileSystem& fs) {
    std::vector<std::string> out;
    for (int32_t pi = 0; pi < fs._packfiles._size; ++pi) {
        const IO::PackageFile* pkg = fs._packfiles[pi];
        char buf[64];
        std::snprintf(buf, sizeof(buf), "|%d|%d|%u|%d|%d", pkg->_entries._size, pkg->_priority, pkg->_crc,
                      pkg->_version, pkg->_overridable ? 1 : 0);
        out.push_back(Chars(pkg->_name) + buf);
    }
    return out;
}

// The fold index a skip leaves in place must still answer for the live tree: every node's key
// finds that node.
int64_t FoldIndexMisses(const IO::FileSystem& fs, int64_t* answered) {
    int64_t misses = 0;
    *answered = 0;
    std::vector<const RtIO::PackfileIndexNode*> stack;
    if (fs._packfileIndex.root != nullptr)
        stack.push_back(fs._packfileIndex.root);
    while (!stack.empty()) {
        const RtIO::PackfileIndexNode* n = stack.back();
        stack.pop_back();
        RtIO::PackfileIndexNode* out = nullptr;
        if (IO::PackIndexFind(&fs._packfileIndex, n->element.key._data, &out)) {
            ++*answered;
            if (out != n)
                ++misses;
        }
        if (n->left != nullptr)
            stack.push_back(n->left);
        if (n->right != nullptr)
            stack.push_back(n->right);
    }
    return misses;
}

void ShadowCapture(const IO::FileSystem& fs) {
    g_remount.shadow = true;
    g_remount.shadowStart = std::chrono::steady_clock::now();
    const std::vector<PackageCache::PackView> packs = Packs(fs);
    if (!PackageCache::KeyBytes(packs, g_remount.shadowKey))
        g_remount.shadowKey.clear();
    if (!PackageCache::Encode(fs._packfileIndex, packs, g_remount.shadowTree))
        g_remount.shadowTree.clear();
    g_remount.shadowPacks = PackSummary(fs);
    g_remount.shadowUser.clear();
    for (int32_t i = 0; i < fs._userPackFilenames.size; ++i)
        g_remount.shadowUser.push_back(Chars(fs._userPackFilenames.data[i]));
    int64_t answered = 0;
    const int64_t misses = FoldIndexMisses(fs, &answered);
    std::fprintf(stderr, "[MenuEntry] shadow: the remount would be skipped; running it (fold index %lld of %d "
                 "answered, %lld wrong)\n", static_cast<long long>(answered), fs._packfileIndex.count,
                 static_cast<long long>(misses));
}

void ShadowCompare(const IO::FileSystem& fs, int32_t reloads, int32_t unloads) {
    g_remount.shadow = false;
    const std::vector<PackageCache::PackView> packs = Packs(fs);
    std::vector<uint8_t> key;
    std::vector<uint8_t> tree;
    const bool keyOk = PackageCache::KeyBytes(packs, key);
    const bool treeOk = PackageCache::Encode(fs._packfileIndex, packs, tree);
    std::vector<std::string> user;
    for (int32_t i = 0; i < fs._userPackFilenames.size; ++i)
        user.push_back(Chars(fs._userPackFilenames.data[i]));
    const bool sameKey = keyOk && !g_remount.shadowKey.empty() && key == g_remount.shadowKey;
    const bool sameTree = treeOk && !g_remount.shadowTree.empty() && tree == g_remount.shadowTree;
    const bool samePacks = PackSummary(fs) == g_remount.shadowPacks;
    const bool sameUser = user == g_remount.shadowUser;
    const bool ok = sameKey && sameTree && samePacks && sameUser && reloads == 0 && unloads == 0;
    std::fprintf(stderr, "[MenuEntry] shadow %s: %d packages, %d index nodes, entries %s, tree %s, "
                 "_packfiles %s, user packs %s, reloads %d, unloads %d (%.1f ms)\n",
                 ok ? "SAME" : "MISMATCH", fs._packfiles._size, fs._packfileIndex.count,
                 sameKey ? "same" : "DIFFER", sameTree ? "same" : "DIFFER", samePacks ? "same" : "DIFFER",
                 sameUser ? "same" : "DIFFER", reloads, unloads, MsSince(g_remount.shadowStart));
}

bool RemountSkip(IO::Modification& mod) {
    g_remount.inRemount = true;
    if (!Ext().menu_entry) {
        g_remount.stored = false;
        return false;
    }
    if (!g_remount.stored)
        return false;   // the first remount of the run, or the packages were closed elsewhere
    const IO::FileSystem& fs = *IO::FileSystem::_singleton;
    const auto t0 = std::chrono::steady_clock::now();
    const Fingerprint now = Current(mod, fs, true);
    const char* diff = FirstDifference(g_remount.last, now);
    if (diff != nullptr) {
        std::fprintf(stderr, "[MenuEntry] remount runs: %s changed (%.1f ms)\n", diff, MsSince(t0));
        return false;
    }
    if (OPENBA_PATHINDEX_CHECK) {
        ShadowCapture(fs);
        return false;
    }
    g_remount.inRemount = false;
    ++g_remount.skips;
    std::fprintf(stderr, "[MenuEntry] remount skipped: %zu packages unchanged (%zu .pkg, %zu mods), "
                 "fingerprint %.1f ms, skip %d\n", now.mounted.size(), now.base.size(), now.user.size(),
                 MsSince(t0), g_remount.skips);
    return true;
}

void RemountDone(IO::Modification& mod, int32_t reloads, int32_t unloads) {
    g_remount.inRemount = false;
    const IO::FileSystem& fs = *IO::FileSystem::_singleton;
    if (g_remount.shadow)
        ShadowCompare(fs, reloads, unloads);
    if (!Ext().menu_entry) {
        g_remount.stored = false;
        return;
    }
    g_remount.last = Current(mod, fs, false);
    g_remount.stored = true;
}

void PackagesClosed(IO::FileSystem&) {
    if (g_remount.inRemount || !g_remount.stored)
        return;
    g_remount.stored = false;
    std::fprintf(stderr, "[MenuEntry] packages closed outside a remount: fingerprint dropped\n");
}

}  // namespace

void Sync() {
    const bool faithful = CurrentProfile() == Profile::Faithful;
    IO::Modification::_extModCached = faithful ? nullptr : &ModCached;
    IO::Modification::_extModStored = faithful ? nullptr : &ModStored;
    IO::Modification::_extOverridesRestore = faithful ? nullptr : &OverridesRestore;
    IO::Modification::_extOverridesBuilt = faithful ? nullptr : &OverridesBuilt;
    IO::Modification::_extRemountSkip = faithful ? nullptr : &RemountSkip;
    IO::Modification::_extRemountDone = faithful ? nullptr : &RemountDone;
    IO::FileSystem::_extPackagesClosed = faithful ? nullptr : &PackagesClosed;
    if (!faithful)
        PrimeFromBootScan();
}

}  // namespace MenuEntry
}  // namespace Ext
