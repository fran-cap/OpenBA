// ============================================================================
// src/ext/storage_metrics/storage_math.h -- ext.storage_metrics: the sums and the texts of the Storage tab
// ============================================================================
// NOT RETAIL. docs/extensions/handins/STORAGEMETRICS.md. Standard C++ only (tests/unit/
// ext_storage_metrics_test.cpp includes this header alone): which kind a storage building is, the used
// and total space of each kind and of each building type, the lines the tab shows, and their texts
// ("12,400 / 15,000", "82.6%").
//
// Space is the game's own measure: StorageComponent::capacity (the description's limit, times the
// footprint for an area-based store) against StorageComponent::QueryCount (every stored item's count
// times its weight), which is what StorageComponent::CanAdd compares when a citizen brings goods.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Ext {
namespace StorageMetrics {

// The three kinds the tab totals, in the order it shows them.
enum class Kind : uint8_t { Stockpile, Barn, TradingPost, Count };
constexpr int kKindCount = static_cast<int>(Kind::Count);

// A building is recognised by its components, never by its name, so a mod's storage lands in the same
// kind as the game's: a trade component makes a trading post; a store whose limit grows with its footprint
// (StorageDescription's area-based flag: the stockpile the player drags out) is a stockpile; every other
// storage location is a barn.
constexpr Kind Classify(bool hasTrade, bool areaBased) {
    if (hasTrade) {
        return Kind::TradingPost;
    }
    return areaBased ? Kind::Stockpile : Kind::Barn;
}

// Whether a building in state `state` (the game's BuildingState byte, StateMachine::currentInput) stands
// finished, so its space is the town's to use. Not while its site is cleared, stocked and built (0, 1, 8)
// and not once it is being taken down or lies in rubble (3 .. 7). Every other state is a finished
// building at work: Built (2), and the states the finished building's own components run (a workplace
// producing or paused, a trading post with a merchant inbound or docked).
constexpr bool Standing(int state) {
    return !(state == 0 || state == 1 || state == 8 || (state >= 3 && state <= 7));
}

// Used and total space over some buildings.
struct Space {
    int64_t used = 0;
    int64_t capacity = 0;
    int32_t buildings = 0;

    void Add(int32_t buildingUsed, int32_t buildingCapacity) {
        used += std::max(buildingUsed, 0);
        capacity += std::max(buildingCapacity, 0);
        ++buildings;
    }
    bool operator==(const Space& o) const {
        return used == o.used && capacity == o.capacity && buildings == o.buildings;
    }
};

// One building type (a template: the game's Storage Barn, a mod's special store) of the town.
struct TypeSpace {
    const void* key = nullptr;   // the template, compared by address
    std::string name;            // its title, as the toolbar shows it
    Kind kind = Kind::Barn;
    Space space;
};

struct Snapshot {
    Space totals[kKindCount];
    std::vector<TypeSpace> types;

    // Folds one finished building in.
    void Add(const void* key, const std::string& name, Kind kind, int32_t used, int32_t capacity) {
        totals[static_cast<int>(kind)].Add(used, capacity);
        for (TypeSpace& t : types) {
            if (t.key == key) {
                t.space.Add(used, capacity);
                return;
            }
        }
        TypeSpace t;
        t.key = key;
        t.name = name;
        t.kind = kind;
        t.space.Add(used, capacity);
        types.push_back(t);
    }
};

// One line of the tab.
struct Line {
    bool total = false;   // a kind's total (its name is the kind's); else one building type
    Kind kind = Kind::Barn;
    std::string name;     // a type line's name; empty on a total line
    Space space;

    bool SameRow(const Line& o) const {
        return total == o.total && kind == o.kind && name == o.name;
    }
};

// The lines, top to bottom: each kind's total (always, an empty town shows three lines of nothing), and
// under it its building types by name when the kind has more than one (with one type the total says it
// all: the game without mods shows three lines).
inline std::vector<Line> Lines(const Snapshot& s) {
    std::vector<Line> out;
    for (int k = 0; k < kKindCount; ++k) {
        Line total;
        total.total = true;
        total.kind = static_cast<Kind>(k);
        total.space = s.totals[k];
        out.push_back(total);

        std::vector<const TypeSpace*> types;
        for (const TypeSpace& t : s.types) {
            if (static_cast<int>(t.kind) == k) {
                types.push_back(&t);
            }
        }
        std::stable_sort(types.begin(), types.end(),
                         [](const TypeSpace* a, const TypeSpace* b) { return a->name < b->name; });
        // Two templates under one name (a mod's second "Storage Barn") are one line: the player cannot
        // tell them apart by what the tab shows.
        std::vector<Line> named;
        for (const TypeSpace* t : types) {
            if (!named.empty() && named.back().name == t->name) {
                named.back().space.used += t->space.used;
                named.back().space.capacity += t->space.capacity;
                named.back().space.buildings += t->space.buildings;
                continue;
            }
            Line line;
            line.kind = t->kind;
            line.name = t->name;
            line.space = t->space;
            named.push_back(line);
        }
        if (named.size() < 2) {
            continue;
        }
        out.insert(out.end(), named.begin(), named.end());
    }
    return out;
}

// 12400 -> "12,400".
inline std::string Grouped(int64_t value) {
    const bool negative = value < 0;
    std::string digits = std::to_string(negative ? -value : value);
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) {
        digits.insert(static_cast<size_t>(at), ",");
    }
    return negative ? "-" + digits : digits;
}

// "12,400 / 15,000".
inline std::string SpaceText(const Space& s) {
    return Grouped(s.used) + " / " + Grouped(s.capacity);
}

// How full, in tenths of a percent, rounded to nearest; -1 when there is no capacity to be a share of.
// A store can hold more than its limit (a merchant unloads into a full trading post): that reads above 100.
inline int32_t FullTenths(const Space& s) {
    if (s.capacity <= 0) {
        return -1;
    }
    return static_cast<int32_t>((s.used * 1000 + s.capacity / 2) / s.capacity);
}

// "82.6%", or "-" with no capacity.
inline std::string FullText(const Space& s) {
    const int32_t tenths = FullTenths(s);
    if (tenths < 0) {
        return "-";
    }
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "%";
}

}  // namespace StorageMetrics
}  // namespace Ext
