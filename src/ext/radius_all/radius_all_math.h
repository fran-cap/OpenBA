#pragma once
// ============================================================================
// src/ext/radius_all/radius_all_math.h -- ext.radius_all's rules (pure, unit tested)
// ============================================================================
// NOT RETAIL (docs/extensions/handins/RADIUSALL.md). Which kinds of building show their circles, the centre
// cell a circle is drawn round, when the list of circles has to be rebuilt, what to drop and what to add, and
// what counts as a long press. Standard C++ only: tests/unit/ext_radius_all_test.cpp includes this alone.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Ext {
namespace RadiusAll {

// ---- the kinds shown ------------------------------------------------------------------------------------

// A Shift+click on a building of `kind` (its template, compared by address): shown now -> hidden, hidden ->
// shown. Returns true when the kind is shown after the call.
inline bool ToggleKind(std::vector<const void*>& kinds, const void* kind) {
    for (size_t i = 0; i < kinds.size(); ++i) {
        if (kinds[i] == kind) {
            kinds.erase(kinds.begin() + static_cast<std::ptrdiff_t>(i));
            return false;
        }
    }
    kinds.push_back(kind);
    return true;
}

// ---- the circle's centre --------------------------------------------------------------------------------

// The cell RadiusUI::OnOpened (0x140077650) hands RadiusDescription::AddHighlight for a footprint whose
// corner is (x0, y0) and whose extent is (w, h): the footprint's centre, row * size + column. -1 for a
// footprint that is not on the map (a MapComponent not placed yet keeps -1 in all four).
inline int32_t CentreCell(int32_t x0, int32_t y0, int32_t w, int32_t h, int32_t size) {
    if (size <= 0 || x0 < 0 || y0 < 0 || w < 0 || h < 0) {
        return -1;
    }
    const int32_t cx = x0 + (w >> 1);
    const int32_t cy = y0 + (h >> 1);
    if (cx >= size || cy >= size) {
        return -1;
    }
    return cy * size + cx;
}

// ---- when to rebuild ------------------------------------------------------------------------------------

// A running digest of the buildings the shown kinds have: a kind's template, then its entities in the
// town's order. Two frames with the same digest have the same buildings, so the circles are left alone.
constexpr uint64_t kDigestSeed = 0xcbf29ce484222325ull;

inline uint64_t DigestAdd(uint64_t digest, uint64_t value) {
    digest ^= value;
    digest *= 0x100000001b3ull;
    digest ^= digest >> 29;
    return digest;
}

// ---- what to drop and what to add -----------------------------------------------------------------------

struct Circle {
    const void* entity = nullptr;   // the building
    const void* kind = nullptr;     // its template
    int32_t cell = -1;              // the centre the circle was drawn round
    int32_t decal = -1;             // the terrain decal's slot
};

struct Wanted {
    const void* entity = nullptr;
    const void* kind = nullptr;
    int32_t cell = -1;
};

// A circle stays when the same building of the same kind still stands on the same centre. `drop`: indices
// into `have` whose building is gone (or was replaced by another at its address). `add`: indices into `want`
// with no circle yet. Both ascending.
inline void Diff(const std::vector<Circle>& have, const std::vector<Wanted>& want, std::vector<size_t>& drop,
                 std::vector<size_t>& add) {
    drop.clear();
    add.clear();
    std::vector<bool> kept(want.size(), false);
    for (size_t i = 0; i < have.size(); ++i) {
        bool found = false;
        for (size_t j = 0; j < want.size(); ++j) {
            if (!kept[j] && want[j].entity == have[i].entity && want[j].kind == have[i].kind &&
                want[j].cell == have[i].cell) {
                kept[j] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            drop.push_back(i);
        }
    }
    for (size_t j = 0; j < want.size(); ++j) {
        if (!kept[j]) {
            add.push_back(j);
        }
    }
}

// ---- the long press -------------------------------------------------------------------------------------

constexpr float kLongPressSeconds = 0.5f;        // a finger held this long without moving
constexpr float kLongPressSlopFraction = 0.03f;  // "without moving": less than this fraction of the short edge
// How long a long press stays armed for the click it sends (the click reaches the tool a few polls later).
constexpr double kArmedSeconds = 0.5;

inline bool IsLongPress(float seconds, float travel, float shortEdge) {
    return seconds >= kLongPressSeconds && travel < kLongPressSlopFraction * shortEdge;
}

}  // namespace RadiusAll
}  // namespace Ext
