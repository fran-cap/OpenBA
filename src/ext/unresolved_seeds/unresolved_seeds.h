#pragma once
// ============================================================================
// src/ext/unresolved_seeds/unresolved_seeds.h -- fix.unresolved_seeds (fix tier, default ON)
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §19.
//
// The town's available-agriculture list (CitizenBehavior +0x120: the crops, orchard trees and
// livestock its fields, orchards and pastures may grow) can hold a null entry in retail:
//   * the start town (0x1400a3300) resolves each picked start-condition entry by name with the
//     find-only resolver 0x14001b1d0 and appends the result with 0x140091a20, neither testing
//     it, so a start condition naming a resource that does not exist (a mod's "Fig" where the
//     package has "Fig Tree") appends null;
//   * a load (0x1400049c0, the ExternalPtr<ComponentDescription> array body) stores null for an
//     entry whose resource no longer loads (a save made with a mod, loaded without it, or a
//     world restored after an in-game Mods change that disabled the mod).
// The list's readers dereference every entry with no test: SelectResourceUI::PopulateCombo
// 0x140078830 reads entry+0x30 (its sub-description count), so selecting any crop field,
// orchard or pasture faults at address 0x30.
//
// With the row on, the two writers keep the null out: the start town skips an entry that did
// not resolve (after drawing its random pick, so the world RNG advances as in retail), and a
// load drops the null entries from the list it read. The panels are untouched.
// ============================================================================

#include <cstdint>

namespace Ext {
namespace UnresolvedSeeds {

// Removes the null entries of `data[0..size)` in place, keeping the order of the rest, and
// returns the new count. Entries past the new count are left as they were.
template <class T>
int32_t DropNulls(T** data, int32_t size) {
    int32_t kept = 0;
    for (int32_t i = 0; i < size; ++i) {
        if (data[i] != nullptr) {
            data[kept] = data[i];
            ++kept;
        }
    }
    return kept;
}

}  // namespace UnresolvedSeeds
}  // namespace Ext
