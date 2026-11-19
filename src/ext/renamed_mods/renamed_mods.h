#pragma once
// ============================================================================
// src/ext/renamed_mods/renamed_mods.h -- ext.renamed_mods (class B, from the next start)
// ============================================================================
// NOT RETAIL (docs/extensions/handins/RENAMEDMODS.md). The game finds a mod's content list by the
// name of its .pkm file (`<file name>Resources.rsc:resource`, Engine::UpdateLoop::LoadResources),
// so a renamed file's new content never appears, and nothing says why. (Its files that replace the
// game's own still apply: every enabled package is mounted.) With the row on, a mod whose list is not found
// by that name is loaded by the list its package holds (renamed_mods_math.h picks it from the
// package's root-level entries). Never under the faithful profile: the seam stays null.

namespace Ext {
namespace RenamedMods {

// Boot only (registry_boot.cpp): reads the row ONCE and installs Engine::UpdateLoop::_extModRoot only when
// it is on (null otherwise, and under the faithful profile). The hook uses that boot value for the whole run.
void Sync();

}  // namespace RenamedMods
}  // namespace Ext
