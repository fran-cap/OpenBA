#pragma once
// ============================================================================
// src/ext/custom_maps/mod_check.h -- ext.custom_maps: is a mod the game has enabled (NOT RETAIL)
// ============================================================================
// The New Game row asks which of the mods a map lists are missing. The answer comes from the game's own mod
// list (IO::Modification, the list SaveLoad::CheckMod matches a save's mods against: each mod's display name
// and its enabled byte), not from looking a template up with the resource loader: a lookup from the menu
// loads the template outside a loading screen's tracking, and quitting the game afterwards crashed the next
// loader pass on a resource the cleanup had freed (OpenBA_maps10.exe, 2026-11-13). Its own file because the
// engine's mod header must come before <windows.h>.
// ============================================================================

#include "ext/custom_maps/bmap.h"

namespace Ext {
namespace CustomMaps {

// True when the game has an enabled mod whose name is `mod.name` (case does not matter). True when the game's
// mod list cannot be read (nothing is said about a mod the game cannot be asked about).
bool ModIsEnabled(const ModRef& mod);

}  // namespace CustomMaps
}  // namespace Ext
