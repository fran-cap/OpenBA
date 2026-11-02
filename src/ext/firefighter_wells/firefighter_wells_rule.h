// ============================================================================
// src/ext/firefighter_wells/firefighter_wells_rule.h -- fix.firefighter_wells: the pure rule
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §20. Standard C++ only, so
// tests/unit/ext_firefighter_wells_test.cpp links it without the world.
//
// The "Firefighter" achievement reads "Build 20 wells in a single town." (the string table's
// FirefighterDesc), but its one goal in the game data is a StatGoal comparing stat 0x19 (finished,
// standing buildings of a template, 0x1400d81b0) for Template\Well.rsc with ">=" against the
// constant 40. The data is read from the player's install and is not changed; with the row on, the
// comparison uses 20 for exactly that goal (stat 0x19, constant 40, the Well template), so the
// achievement unlocks at the count its description gives. Any other constant (a mod's) is kept.
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>

namespace Ext {
namespace FirefighterWells {

constexpr uint32_t kStatFinishedOfTemplate = 0x19;   // StatGoal stat type, 0x1400d81b0
constexpr int32_t  kDataThreshold          = 40;     // the constant in the game data
constexpr int32_t  kDescribedThreshold     = 20;     // FirefighterDesc: "Build 20 wells"
constexpr const char* kWellTemplate        = "Template\\Well.rsc";

// The constant a StatGoal compares against: `fixedValue` unless the row is on and the goal is
// the Firefighter wells goal. `templateName` may be null.
inline int32_t Threshold(bool fixOn, uint32_t statType, int32_t fixedValue, const char* templateName) {
    if (!fixOn) return fixedValue;
    if (statType != kStatFinishedOfTemplate || fixedValue != kDataThreshold) return fixedValue;
    if (templateName == nullptr || std::strcmp(templateName, kWellTemplate) != 0) return fixedValue;
    return kDescribedThreshold;
}

}  // namespace FirefighterWells
}  // namespace Ext
