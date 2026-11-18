# RENAMEDMODS hand-in (2026-11-17)

Branch `ext-renamed-mods`, on `main` 0809be4c. The task (the user's): a mod whose `.pkm` file was renamed adds no new content to the game, and nothing says why; make an extension that loads it anyway. **Result: `ext.renamed_mods`, class B, off by default.**

## 1. Why
The game finds a mod's content list by the name of the mod's `.pkm` file. `Engine::UpdateLoop::LoadResources` (retail `0x1800dd7d0`) walks the enabled mods and, for each, takes the file's name without its extension and appends `Resources.rsc:resource`: `MegaMod.pkm` asks for `MegaModResources.rsc:resource`. The package holds that list as its root-level entry `MegaModResources_resource.crs`, named after the `PackageFile` it was compiled from. Rename the file and the name the loader asks for no longer matches the entry. The list is never loaded, so none of the mod's NEW content (its templates, toolbar entries, ...) is registered. The loader skips it without a message (`g_modsFound` is the only trace). The mod's files that REPLACE the game's own still apply: `Modification::SetupFileSystem` mounts every enabled package on the packfile stack whatever its name, and a resource the game loads by its own path (the main menu, the loading screen, the menu music) is read from the highest package that holds that path. Seen by the user on a renamed mod with the row off (2026-11-17): the replaced menu, loading screen and music, and no new content. The enabled flag and the load order are kept per file name as well (`ModEnabled_<stem>`, `ModIndex_<stem>`), so a rename also resets those.

## 2. What the packages look like (measured)
60 packages sampled from the user's collection (read-only): every one has **exactly one** root-level `...Resources_resource.crs`, none has none, none has two. 56 are named after their file. The other 4 are files a player renamed (`ChooChoo_NotMineVariant.pkm` holds `ChooChooResources_resource.crs`; `RenewableResourcesOld.pkm`, `OldTailorHouse_OldModsFromKidVariant.pkm`, `EBSVVanillaMineOverride.pkm` likewise): exactly the case this row is for. 57 of 60 also hold `<name>.index`, the package's own name.

## 3. What the extension does
With the row on, in `LoadResources`, for an enabled mod whose list is not in its package (the engine's own existence test fails), a hook is asked. It reads the package's entry names from the mod's path set (`Mod::set`, filled by the mod scan) and:

* **one root-level list**: loads it (`<prefix>Resources.rsc:resource`) and logs `[RenamedMods] <file>.pkm: no <stem>Resources.rsc in the package; loading its own list ...` once per mod;
* **several**: the package's `.index` entry names its own list; if that picks exactly one, that one. Otherwise nothing is loaded and the log says so;
* **none**: nothing is loaded and the log says so (`...its new content is not loaded`). This is the only message the game gives for a mod whose list is not loaded, and it appears only with the row on.

A mod whose list the file name does find is untouched, and so is every package with the row off (the seam is one null test per mod whose list is missing). Names are compared without regard to case, as the game's paths are.

| file | role |
|---|---|
| `src/ext/renamed_mods/renamed_mods_math.h` | the choice of the root list from the entry names (pure, unit tested) |
| `src/ext/renamed_mods/renamed_mods.h/.cpp` | the hook: walks the mod's path set, logs, names the list |
| `tests/unit/ext_renamed_mods_test.cpp` | the choice, on entry lists shaped like the sampled packages |

Seam: `Engine::UpdateLoop::_extModRoot` (null by default, `UpdateLoop.h`, called in `updateloop_loadresources.cpp` only when the list is missing), installed by `RenamedMods::Sync()` from `registry_boot.cpp`, null under the faithful profile. Registration: `registry.cpp/.h`, the row's icon (`tools/ext_icons/renamedmods.py`, `ExtensionsSprites.rsc`, the baked sheet).

## 4. Class
B: nothing is saved, but a town can hold buildings that exist only because the mod loaded, so the row is locked for the game and the save records the extension. The mods load once at start, so a change takes effect at the next start. Off by default.

## 5. Not done / not looked at
* **Not run in a game.** Checked by the unit tests, by reading the loader, and by the 60-package measurement above. The first thing to look at: rename an installed mod's `.pkm`, enable the row, restart, and see the mod's content and the `[RenamedMods]` line in the log.
* A package renamed to the name of ANOTHER installed mod's list collides as any two mods with the same list do (the mods' priority decides); the extension does not look for that.
* Only the root list is found. A mod whose own content refers to its file name in some other way is not looked at (none seen).
* The Windows gate and the resave check (the maintainer's); Vulkan, Metal, Linux, Android, macOS.
