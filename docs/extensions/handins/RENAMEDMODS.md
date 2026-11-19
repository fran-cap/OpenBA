# RENAMEDMODS hand-in (2026-11-17)

Branch `ext-renamed-mods`, on `main` 0809be4c. The task (the user's): a mod whose `.pkm` file was renamed adds no new content to the game, and nothing says why; make an extension that loads it anyway. **Result: `ext.renamed_mods`, class B, off by default.**

## 1. Why
The game finds a mod's content list by the name of the mod's `.pkm` file. `Engine::UpdateLoop::LoadResources` (retail `0x1800dd7d0`) walks the enabled mods and, for each, takes the file's name without its extension and appends `Resources.rsc:resource`: `MegaMod.pkm` asks for `MegaModResources.rsc:resource`. The package holds that list as its root-level entry `MegaModResources_resource.crs`, named after the `PackageFile` it was compiled from. Rename the file and the name the loader asks for no longer matches the entry. The list is never loaded, so none of the mod's NEW content (its templates, toolbar entries, ...) is registered. The loader skips it without a message (`g_modsFound` is the only trace). The mod's files that REPLACE the game's own still apply: `Modification::SetupFileSystem` mounts every enabled package on the packfile stack whatever its name, and a resource the game loads by its own path (the main menu, the loading screen, the menu music) is read from the highest package that holds that path. Seen by the user on a renamed mod with the row off (2026-11-17): the replaced menu, loading screen and music, and no new content. The enabled flag and the load order are kept per file name as well (`ModEnabled_<stem>`, `ModIndex_<stem>`), so a rename also resets those.

## 2. What the packages look like (measured)
60 packages sampled from the user's collection (read-only): every one has **exactly one** root-level `...Resources_resource.crs`, none has none, none has two. 56 are named after their file. The other 4 are files a player renamed (`ChooChoo_NotMineVariant.pkm` holds `ChooChooResources_resource.crs`; `RenewableResourcesOld.pkm`, `OldTailorHouse_OldModsFromKidVariant.pkm`, `EBSVVanillaMineOverride.pkm` likewise): exactly the case this row is for. 57 of 60 also hold `<name>.index`, the package's own name.

## 3. What the extension does
With the row on AT BOOT (`RenamedMods::Sync()` reads it once and keeps that value for the whole run; the hook never reads the row again), in `LoadResources`, for an enabled mod whose list is not in its package (the engine's own existence test fails), a hook is asked. It reads the package's entry names from the mod's path set (`Mod::set`, filled by the mod scan) and:

* **one root-level list**: loads it (`<prefix>Resources.rsc:resource`) and logs `[RenamedMods] <file>.pkm: no <stem>Resources.rsc in the package; loading its own list ...` once per mod;
* **several**: the package's `.index` entry names its own list; if that picks exactly one, that one. Otherwise nothing is loaded and the log says so;
* **none**: nothing is loaded and the log says so (`... nothing to load (a mod of replacement files has none)`). A mod that only replaces existing files has no list of its own, so this line is normal for it and is not an error; it appears only with the row on.

A mod whose list the file name does find is untouched, and so is every package with the row off at boot (the seam is not installed, one null test per mod whose list is missing). Names are compared without regard to case, as the game's paths are.

| file | role |
|---|---|
| `src/ext/renamed_mods/renamed_mods_math.h` | the choice of the root list from the entry names (pure, unit tested) |
| `src/ext/renamed_mods/renamed_mods.h/.cpp` | the hook: walks the mod's path set, logs, names the list |
| `tests/unit/ext_renamed_mods_test.cpp` | the choice, on entry lists shaped like the sampled packages |

Seam: `Engine::UpdateLoop::_extModRoot` (null by default, `UpdateLoop.h`, called in `updateloop_loadresources.cpp` only when the list is missing), installed by `RenamedMods::Sync()` from `registry_boot.cpp`, null under the faithful profile. Registration: `registry.cpp/.h`, the row's icon (`tools/ext_icons/renamedmods.py`, `ExtensionsSprites.rsc`, the baked sheet).

## 4. Class
B: nothing is saved, but a town can hold buildings that exist only because the mod loaded, so the row is locked for the game and the save records the extension. The mods load once at start, so a change takes effect at the next start: the row is read once at boot, and a toggle in the Extensions dialog, or the lock a loaded save takes, changes nothing until then. Off by default.

**The row on at boot, turned off at the menu, then a new game.** The hook stays installed for the session, so the renamed mod's content is in the new town, but the game lock records the row as the dialog has it (off), so the save does not carry `ext:renamed_mods` and a later load shows no notice. Read from the code (`Ext::LockForGame` copies the request), not run. The record follows the dialog, not the boot value; a save's load does not depend on the record (next paragraph).

**A save made with the row on, loaded in a session that started with it off.** The save carries the extension in its manifest. The load shows the `TurnedOff` notice (`EXTNOTICE.md`); on Yes the manifest locks the row on for that game. The boot value is not changed by it, so the hook stays off for the session. Run in a game (2026-11-18, the author, `OpenBA_renamed3.exe`, two mods, each with the row on at boot, a modded building built, saved, then the row off, restart, load): the notice appeared, Yes loaded the save, and the mod's building and toolbar entries were present in both cases (Colonial Charter Renamed; a renamed copy of EB Village Apiary, `EBVillageApiaryTest.pkm`). The log of those runs has `ext.renamed_mods=0` at boot, no `[RenamedMods]` line, and `modsWithResources=0`, so the content did not come through the hook or through a list found by the file name. Why a loaded save still has it is NOT established; the likely reading is that the save's own references load the templates and entries from the mounted package by path, which a new game does not do. A NEW game started with the row off at boot with the renamed mod had none of the mod's content (the author, same build), as with the earlier build; so the extension is what supplies it for a new game, and a loaded save brings it with it either way.

## 5. Not done / not looked at
* **Run in a game.** 2026-11-17, an earlier build that read the row on every call, one renamed installed mod: row off, its files that replace the game's own applied (menu, loading screen, music) and no new content; row on, its new content appeared ("the renamed mod does work"). 2026-11-18, the boot-latched build: with the row on at boot, Colonial Charter Renamed loaded and a modded building could be built and saved; the save-load case is in section 4. A new game with the row off at boot, same build: no modded content. Not run: the `[RenamedMods]` log lines of the new wording, Vulkan and the platforms below.
* A package renamed to the name of ANOTHER installed mod's list collides as any two mods with the same list do (the mods' priority decides); the extension does not look for that.
* Only the root list is found. A mod whose own content refers to its file name in some other way is not looked at (none seen).
* The Windows gate and the resave check (the maintainer's); Vulkan, Metal, Linux, Android, macOS.
