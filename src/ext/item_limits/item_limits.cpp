// src/ext/item_limits/item_limits.cpp -- NOT RETAIL. ext.item_limits (class C): the town's list of item
// limits and the flat array the seams scan. See the header and docs/extensions/handins/ITEMLIMITS.md.
// Standard C++ only (the game side is item_limits_town.cpp and panel_row.cpp).
#include "ext/item_limits/item_limits.h"

#include <algorithm>
#include <cctype>

namespace Ext {
namespace ItemLimits {

bool g_on = false;
bool g_rowOn = false;
std::vector<Resolved> g_items;
std::vector<int32_t> g_counts;

namespace {

std::vector<Limit> s_limits;        // the list, in the order the limits were first set
uint32_t s_version = 1;             // advances whenever the list changes
uint32_t s_pathsVersion = 1;        // advances when a path joins or leaves the list (the walk's input)
uint32_t s_resolvedVersion = 0;     // the list version g_items was built from
uint32_t s_resolvedPathsVersion = 0;// the paths version of the last walk
uint32_t s_resolvedEpoch = ~0u;     // the lock epoch g_items was built in

// Equal without case, without a copy: the panel asks every frame it is open.
bool SamePath(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// The index of `path` in the list, or -1.
int IndexOf(const std::string& path) {
    for (size_t i = 0; i < s_limits.size(); ++i) {
        if (SamePath(s_limits[i].path, path)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int32_t Clamp(int32_t limit) {
    return std::max<int32_t>(0, std::min<int32_t>(limit, kMaxLimit));
}

}  // namespace

const std::vector<Limit>& Limits() {
    return s_limits;
}

bool LimitOf(const std::string& path, int32_t& out) {
    const int at = IndexOf(path);
    if (at < 0) {
        return false;
    }
    out = s_limits[static_cast<size_t>(at)].limit;
    return true;
}

bool SetLimit(const std::string& path, int32_t limit) {
    if (path.empty()) {
        return false;
    }
    const int32_t value = Clamp(limit);
    const int at = IndexOf(path);
    if (at >= 0) {
        Limit& l = s_limits[static_cast<size_t>(at)];
        if (l.limit == value) {
            return false;
        }
        l.limit = value;
    } else {
        Limit l;
        l.path = path;
        l.limit = value;
        s_limits.push_back(l);
        ++s_pathsVersion;
    }
    ++s_version;
    return true;
}

bool ClearLimit(const std::string& path) {
    const int at = IndexOf(path);
    if (at < 0) {
        return false;
    }
    s_limits.erase(s_limits.begin() + at);
    ++s_version;
    ++s_pathsVersion;
    return true;
}

void ReplaceLimits(const std::vector<Limit>& limits) {
    s_limits = limits;
    for (Limit& l : s_limits) {
        l.limit = Clamp(l.limit);
    }
    ++s_version;
    ++s_pathsVersion;
}

void ResetState() {
    s_limits.clear();
    g_items.clear();
    g_counts.clear();
    g_on = false;
    g_rowOn = false;
    ++s_version;
    ++s_pathsVersion;
    s_resolvedVersion = 0;
    s_resolvedPathsVersion = 0;
    s_resolvedEpoch = ~0u;
    PanelsForget();   // the game ends (UnlockForGame) or another starts: none of its panels is left
}

bool NeedsResolve(uint32_t epoch) {
    return s_resolvedVersion != s_version || s_resolvedEpoch != epoch;
}

bool NeedsWalk(uint32_t epoch) {
    return s_resolvedPathsVersion != s_pathsVersion || s_resolvedEpoch != epoch;
}

void RefreshLimits() {
    for (Resolved& r : g_items) {
        if (r.slot >= 0 && static_cast<size_t>(r.slot) < s_limits.size()) {
            r.limit = s_limits[static_cast<size_t>(r.slot)].limit;
        }
    }
    s_resolvedVersion = s_version;
}

void Resolve(uint32_t epoch, const std::vector<Candidate>& items) {
    std::vector<Resolved> next;
    std::vector<int32_t> counts(s_limits.size(), 0);
    for (const Candidate& c : items) {
        if (c.item == nullptr) {
            continue;
        }
        const int slot = IndexOf(c.path);
        if (slot < 0) {
            continue;
        }
        bool seen = false;
        for (const Resolved& r : next) {
            if (r.item == c.item) {
                seen = true;
                break;
            }
        }
        if (seen) {
            continue;
        }
        Resolved r;
        r.item = c.item;
        r.limit = s_limits[static_cast<size_t>(slot)].limit;
        r.slot = slot;
        // Keep the last count of a path that stays limited: the count is refreshed every tick, and a limit
        // edited between the count and a workshop's test must not read an empty town. Every template of a
        // path carried its path's count, so any one of them gives it.
        for (const Resolved& old : g_items) {
            if (old.item == c.item && old.slot >= 0 && static_cast<size_t>(old.slot) < g_counts.size()) {
                counts[static_cast<size_t>(slot)] = g_counts[static_cast<size_t>(old.slot)];
                break;
            }
        }
        next.push_back(r);
    }
    g_items.swap(next);
    g_counts.swap(counts);
    s_resolvedVersion = s_version;
    s_resolvedPathsVersion = s_pathsVersion;
    s_resolvedEpoch = epoch;
}

void SetRowActive(bool rowOn) {
    g_rowOn = rowOn;
    g_on = rowOn && !g_items.empty();
}

}  // namespace ItemLimits
}  // namespace Ext
