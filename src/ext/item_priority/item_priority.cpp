// src/ext/item_priority/item_priority.cpp -- NOT RETAIL. ext.item_priority (class C): the lists, the resolved
// ranks the seams scan, and the tier search. See the header and docs/extensions/handins/ITEMPRIO.md. Standard
// C++ only (the game side is item_priority_town.cpp).
#include "ext/item_priority/item_priority.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace Ext {
namespace ItemPriority {

bool g_on = false;
std::vector<const void*> g_ranked[kCategories];
std::vector<const void*> g_never[kCategories];

namespace {

Lists s_lists;
int s_ceiling = kUnranked;
uint32_t s_version = 1;          // advances whenever the lists change
uint32_t s_resolvedVersion = 0;  // the list version g_ranked was built from
uint32_t s_resolvedEpoch = ~0u;  // the lock epoch g_ranked was built in

std::string Fold(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// The index of `path` in `list`, or -1 (compared without case).
int IndexOf(const std::vector<std::string>& list, const std::string& path) {
    const std::string key = Fold(path);
    for (size_t i = 0; i < list.size(); ++i) {
        if (Fold(list[i]) == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

int FlagBit(int category) {
    switch (category) {
        case kFuel:     return 6;
        case kTool:     return 9;
        case kHealth:   return 13;
        case kClothing: return 14;
        default:        return -1;
    }
}

bool IsCategory(int category) {
    return category >= 1 && category <= kCategories;
}

bool Lists::Empty() const {
    for (const std::vector<std::string>& list : paths) {
        if (!list.empty()) {
            return false;
        }
    }
    for (const std::vector<std::string>& list : never) {
        if (!list.empty()) {
            return false;
        }
    }
    return true;
}

int RankOf(const Lists& lists, int category, const std::string& path) {
    if (!IsCategory(category) || path.empty()) {
        return kUnranked;
    }
    const int at = IndexOf(lists.Of(category), path);
    return at >= 0 ? at : kUnranked;
}

bool IsNever(const Lists& lists, int category, const std::string& path) {
    return IsCategory(category) && !path.empty() && IndexOf(lists.NeverOf(category), path) >= 0;
}

bool ClearNever(Lists& lists, int category, const std::string& path) {
    if (!IsCategory(category)) {
        return false;
    }
    std::vector<std::string>& list = lists.NeverOf(category);
    const int at = IndexOf(list, path);
    if (at < 0) {
        return false;
    }
    list.erase(list.begin() + at);
    return true;
}

bool SetNever(Lists& lists, int category, const std::string& path) {
    if (!IsCategory(category) || path.empty() || IsNever(lists, category, path)) {
        return false;
    }
    Unrank(lists, category, path);   // an item is ranked or Never, never both
    lists.NeverOf(category).push_back(path);
    return true;
}

bool Rank(Lists& lists, int category, const std::string& path) {
    if (!IsCategory(category) || path.empty()) {
        return false;
    }
    const bool wasNever = ClearNever(lists, category, path);   // ranking an item takes it off the Never list
    if (IndexOf(lists.Of(category), path) >= 0) {
        return wasNever;
    }
    lists.Of(category).push_back(path);
    return true;
}

bool Unrank(Lists& lists, int category, const std::string& path) {
    if (!IsCategory(category)) {
        return false;
    }
    std::vector<std::string>& list = lists.Of(category);
    const int at = IndexOf(list, path);
    if (at < 0) {
        return false;
    }
    list.erase(list.begin() + at);
    return true;
}

bool Move(Lists& lists, int category, const std::string& path, int direction) {
    if (!IsCategory(category) || (direction != -1 && direction != 1)) {
        return false;
    }
    std::vector<std::string>& list = lists.Of(category);
    const int at = IndexOf(list, path);
    if (at < 0) {
        return false;
    }
    const int to = at + direction;
    if (to < 0 || to >= static_cast<int>(list.size())) {
        return false;
    }
    std::swap(list[static_cast<size_t>(at)], list[static_cast<size_t>(to)]);
    return true;
}

const Lists& Current() {
    return s_lists;
}

void Apply(const Lists& lists) {
    s_lists = lists;
    ++s_version;
}

void ResetState() {
    s_lists = Lists();
    ++s_version;
    for (std::vector<const void*>& ranked : g_ranked) {
        ranked.clear();
    }
    for (std::vector<const void*>& never : g_never) {
        never.clear();
    }
    g_on = false;
    s_ceiling = kUnranked;
}

bool NeedsResolve(uint32_t epoch) {
    return s_resolvedVersion != s_version || s_resolvedEpoch != epoch;
}

void Resolve(uint32_t epoch, const std::vector<Candidate>& materials) {
    for (std::vector<const void*>& ranked : g_ranked) {
        ranked.clear();
    }
    for (std::vector<const void*>& never : g_never) {
        never.clear();
    }
    if (!s_lists.Empty()) {
        std::unordered_map<std::string, const void*> byPath;
        for (const Candidate& c : materials) {
            byPath.emplace(Fold(c.path), c.material);   // the first of two equal paths wins
        }
        for (int category = 1; category <= kCategories; ++category) {
            for (const std::string& path : s_lists.Of(category)) {
                const auto found = byPath.find(Fold(path));
                if (found != byPath.end()) {
                    g_ranked[category - 1].push_back(found->second);
                }
            }
            for (const std::string& path : s_lists.NeverOf(category)) {
                const auto found = byPath.find(Fold(path));
                if (found != byPath.end()) {
                    g_never[category - 1].push_back(found->second);
                }
            }
        }
    }
    s_resolvedVersion = s_version;
    s_resolvedEpoch = epoch;
}

void SetRowActive(bool rowOn) {
    bool any = false;
    for (const std::vector<const void*>& ranked : g_ranked) {
        any = any || !ranked.empty();
    }
    for (const std::vector<const void*>& never : g_never) {
        any = any || !never.empty();
    }
    g_on = rowOn && any;
}

int Ceiling() {
    return s_ceiling;
}

bool FindTiered(int category, bool (*once)(void* context), void* context) {
    if (!Active(category)) {
        return once(context);   // retail's single search
    }
    struct Restore {
        int prev;
        ~Restore() { s_ceiling = prev; }
    } restore{s_ceiling};
    const int tiers = static_cast<int>(g_ranked[category - 1].size());
    for (int r = 0; r < tiers; ++r) {
        s_ceiling = r;   // only items ranked 0..r are eligible in this search
        if (once(context)) {
            return true;
        }
    }
    s_ceiling = kUnranked;   // the last tier: every item, ranked or not
    return once(context);
}

}  // namespace ItemPriority
}  // namespace Ext
