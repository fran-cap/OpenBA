// src/ext/dynamic_professions/professions_math.h -- ext.dynamic_professions' list rules (NOT RETAIL)
//
// Standard C++ with no game types, so the unit tests build it (tests/unit/ext_dynamic_professions_test.cpp).
// docs/extensions/handins/DYNAMICPROFESSIONS.md.
//
// Which professions the window lists, and in what order. The window gives this one row per profession of the
// game's list, in the list's order (retail's), with the laborer and the builder first: those two have no
// workplace of their own to find (the builder appears with a construction site), so they are always shown.
// Any other profession is shown while the town has a workplace for it OR any citizen holds it; once neither
// is true it stays for kLingerFrames more (a row that vanished under the cursor the moment its spinner
// reached zero would move the rows below it), then it goes.
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Ext {
namespace DynamicProfessions {

constexpr int kLingerFrames = 120;   // about two seconds

// What the window knows of one profession of the game's list (index = its place in that list).
struct Usage {
    int pin = 2;          // 0 the laborer, 1 the builder, 2 any other
    int workplaces = 0;   // built workplaces whose profession it is
    int holders = 0;      // citizens (adult, not a student) who hold it
};

// The indexes to show, in the order to show them. `linger` has one counter per profession (resized here); it
// is the frames a profession that no longer qualifies still stays. Call once per frame (or per check).
inline std::vector<int> VisibleProfessions(const std::vector<Usage>& probes, std::vector<int>& linger,
                                           int framesPassed = 1) {
    linger.resize(probes.size(), 0);
    std::vector<int> shown;
    for (size_t i = 0; i < probes.size(); ++i) {
        const Usage& p = probes[i];
        const bool qualifies = p.pin < 2 || p.workplaces > 0 || p.holders > 0;
        if (qualifies) {
            linger[i] = kLingerFrames;
            shown.push_back(static_cast<int>(i));
        } else if (linger[i] > 0) {
            linger[i] = std::max(0, linger[i] - framesPassed);
            shown.push_back(static_cast<int>(i));
        }
    }
    // the laborer, then the builder, then the rest in the game's order
    std::stable_sort(shown.begin(), shown.end(), [&probes](int a, int b) {
        return probes[static_cast<size_t>(a)].pin < probes[static_cast<size_t>(b)].pin;
    });
    return shown;
}

// The citizens the window can still move out of a profession into the laborers, and the most the spinner of
// a profession may read: its holders plus the laborers (retail's min/max: 0 .. count + laborers).
inline int SpinnerMax(int holders, int laborers) { return holders + laborers; }

}  // namespace DynamicProfessions
}  // namespace Ext
