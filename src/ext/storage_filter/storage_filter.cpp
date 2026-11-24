// src/ext/storage_filter/storage_filter.cpp -- NOT RETAIL. ext.storage_filter (class C): the town's list of
// refused items per storage building. Standard C++ only. See storage_filter.h and
// docs/extensions/handins/STORAGEFILTER.md.
#include "ext/storage_filter/storage_filter.h"

#include <cctype>

namespace Ext {
namespace StorageFilter {

namespace {

std::vector<Entry> g_entries;
uint32_t g_revision = 0;

std::string Fold(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

int IndexOf(int32_t tileX, int32_t tileY) {
    for (std::size_t i = 0; i < g_entries.size(); ++i) {
        if (g_entries[i].tileX == tileX && g_entries[i].tileY == tileY) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int PathIndex(const Entry& e, const std::string& path) {
    const std::string key = Fold(path);
    for (std::size_t i = 0; i < e.refused.size(); ++i) {
        if (Fold(e.refused[i]) == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int CapIndex(const Entry& e, const std::string& path) {
    const std::string key = Fold(path);
    for (std::size_t i = 0; i < e.caps.size(); ++i) {
        if (Fold(e.caps[i].path) == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

const std::vector<Entry>& Entries() {
    return g_entries;
}

bool AnyRefusals() {
    return !g_entries.empty();
}

uint32_t Revision() {
    return g_revision;
}

bool PathIsValid(const std::string& path) {
    if (path.empty() || path.size() > kMaxPathBytes) {
        return false;
    }
    for (char c : path) {
        if (static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    return true;
}

bool IsRefused(int32_t tileX, int32_t tileY, const std::string& path) {
    const int i = IndexOf(tileX, tileY);
    return i >= 0 && PathIndex(g_entries[static_cast<std::size_t>(i)], path) >= 0;
}

std::vector<std::string> RefusedOf(int32_t tileX, int32_t tileY) {
    const int i = IndexOf(tileX, tileY);
    return i >= 0 ? g_entries[static_cast<std::size_t>(i)].refused : std::vector<std::string>();
}

bool SetRefused(int32_t tileX, int32_t tileY, const std::string& path, bool refuse) {
    if (tileX < 0 || tileY < 0 || !PathIsValid(path)) {
        return false;
    }
    const int i = IndexOf(tileX, tileY);
    if (!refuse) {
        if (i < 0) {
            return false;
        }
        Entry& e = g_entries[static_cast<std::size_t>(i)];
        const int p = PathIndex(e, path);
        if (p < 0) {
            return false;
        }
        e.refused.erase(e.refused.begin() + p);
        if (e.refused.empty() && e.caps.empty()) {
            g_entries.erase(g_entries.begin() + i);
        }
        ++g_revision;
        return true;
    }
    if (i >= 0) {
        Entry& e = g_entries[static_cast<std::size_t>(i)];
        if (PathIndex(e, path) >= 0 || e.refused.size() >= kMaxItemsPerBuilding) {
            return false;
        }
        e.refused.push_back(path);
        ++g_revision;
        return true;
    }
    if (g_entries.size() >= kMaxBuildings) {
        return false;
    }
    Entry e;
    e.tileX = tileX;
    e.tileY = tileY;
    e.refused.push_back(path);
    g_entries.push_back(e);
    ++g_revision;
    return true;
}

int32_t CapOf(int32_t tileX, int32_t tileY, const std::string& path) {
    const int i = IndexOf(tileX, tileY);
    if (i < 0) {
        return 0;
    }
    const Entry& e = g_entries[static_cast<std::size_t>(i)];
    const int c = CapIndex(e, path);
    return c >= 0 ? e.caps[static_cast<std::size_t>(c)].limit : 0;
}

bool SetCap(int32_t tileX, int32_t tileY, const std::string& path, int32_t limit) {
    if (tileX < 0 || tileY < 0 || !PathIsValid(path)) {
        return false;
    }
    if (limit > kMaxCap) {
        limit = kMaxCap;
    }
    const int i = IndexOf(tileX, tileY);
    if (limit <= 0) {
        if (i < 0) {
            return false;
        }
        Entry& e = g_entries[static_cast<std::size_t>(i)];
        const int c = CapIndex(e, path);
        if (c < 0) {
            return false;
        }
        e.caps.erase(e.caps.begin() + c);
        if (e.refused.empty() && e.caps.empty()) {
            g_entries.erase(g_entries.begin() + i);
        }
        ++g_revision;
        return true;
    }
    if (i >= 0) {
        Entry& e = g_entries[static_cast<std::size_t>(i)];
        const int c = CapIndex(e, path);
        if (c >= 0) {
            if (e.caps[static_cast<std::size_t>(c)].limit == limit) {
                return false;
            }
            e.caps[static_cast<std::size_t>(c)].limit = limit;
            ++g_revision;
            return true;
        }
        if (e.caps.size() >= kMaxItemsPerBuilding) {
            return false;
        }
        Entry::Cap cap;
        cap.path = path;
        cap.limit = limit;
        e.caps.push_back(cap);
        ++g_revision;
        return true;
    }
    if (g_entries.size() >= kMaxBuildings) {
        return false;
    }
    Entry e;
    e.tileX = tileX;
    e.tileY = tileY;
    Entry::Cap cap;
    cap.path = path;
    cap.limit = limit;
    e.caps.push_back(cap);
    g_entries.push_back(e);
    ++g_revision;
    return true;
}

bool ClearBuilding(int32_t tileX, int32_t tileY) {
    const int i = IndexOf(tileX, tileY);
    if (i < 0) {
        return false;
    }
    g_entries.erase(g_entries.begin() + i);
    ++g_revision;
    return true;
}

void ReplaceEntries(const std::vector<Entry>& entries) {
    g_entries = entries;
    ++g_revision;
}

void ResetState() {
    g_entries.clear();
    ++g_revision;
}

}  // namespace StorageFilter
}  // namespace Ext
