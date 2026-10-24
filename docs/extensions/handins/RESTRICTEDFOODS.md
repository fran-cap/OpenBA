# RESTRICTEDFOODS hand-in

`ext.restricted_foods`: the player chooses foods that citizens do not eat, from a Foods page in the shared in-game extension window. One class C toggle (locked for the game, per-town state recorded in the save), off by default, `formatVersion` 1. It is the first class C extension, so it also builds the shared pieces the next one copies: the save-state codec and the toolbar host.

Built to the rulings in DESIGN.md of 2026-10-20: the lock is on activation, a live window editing class C state is fine, and per-town state travels in the tail record.

## 1. What it does

Chosen Edible materials stop being EATEN by citizens while staying Edible for everything else (storage by category, markets, recipes, trade). The motivating case: raw Wheat is directly edible, so citizens eat it from the barn instead of the Bread a mod's bakery makes from it.

Retail decides "may a citizen eat this" in exactly two places, both a test of `RawMaterialDescription::_flags` bit 5:

| place | retail | what the extension does |
|---|---|---|
| `CitizenComp_Entity_impl_ef40`, category 0 (`citizencomponent.cpp`) | the storage item picker; two passes over the same bit test | a blocked material's bit counts as clear in BOTH passes, so it is never eligible and never picked |
| `ResidenceComponent::GetFoodCount` (`citizenaicomponent.cpp`, 0x14012a9d0) | the Edible count of a home's storage; gates "does my home have food", the starving icon, the home's food check | a blocked material is not counted |

Nothing else changes: the material keeps its flag, so it is still stored, sold, traded and cooked. With the row off, or on with nothing checked, both functions are retail's own, in retail's order.

**Carried home, eaten, and already in a house.** The picker is the one place a food leaves a storage to be eaten or carried, and its category-0 callers are every path that moves food toward a mouth: `EatFrom` (eating out of the target storage), the target finders (`citizenai_findhelpers.cpp`, `citizenai_findtarget.cpp`), and `GatherTransferLoop` (carrying food from a storage to a home). A blocked food is never selected by any of them, so it is not eaten where it lies and not carried home. Blocked food that is already in a house stays in the house: `GetFoodCount` does not count it, the picker never offers it, and nothing removes or moves it. It is eaten again when the player unchecks it.

## 2. Class C: the state, and where it lives

The state is the town's list of blocked foods. It is per-town, so the test in DESIGN.md ("must a reload restore it for the town to play on as saved?") makes the row class C, and the list never lives in `settings.cfg`.

| | |
|---|---|
| row | `restricted_foods`, `Tier::Extension`, `Class::C`, default OFF, `formatVersion` 1 (`registry.cpp`) |
| record | the row's own record in the tail chunk (`savetail.h`); written whenever the row is active for the game, **even with an empty list** (the lock set must not depend on content); zero bytes for a faithful game |
| codec | `Ext::RestrictedFoods::Codec()` (`src/ext/restricted_foods/save_state.cpp`), registered in `src/ext/state_codecs.cpp` (`kCodecs`, one line) |
| key | the food's **template path**, `External::GetPath` of its `RawMaterialDescription` (for example `Template\RawMaterialWheat.rsc:rawmaterial`), compared without case. Not `_name`, which is a sprite / string key that mods reuse. Never an index: mods change the load order between sessions. A path the game no longer has stays in the list and matches nothing |
| wire format v1 | little-endian: `u32 count`, then `count` x { `u8 kind` (1 = a blocked food), `u32 length`, `u8 path[length]` }. Documented at the top of `save_state.cpp` |
| decode | accepts exactly what encode writes; refuses an unknown kind, an empty, over-long (more than 1024 bytes) or control-character path, a path listed twice (without case), a count the payload cannot hold, bytes left over, or a cut-short payload. A refused payload changes nothing: the old list stays |
| load | `ExtSave_ReadTail`: after the lock took the saved set, on both routes (the manifest's own set, and an in-game B / C change that keeps the request), `SaveTail::ApplyState` hands each record's payload to its codec; a payload that does not decode is fatal through the same box as a damaged manifest, naming `ext:restricted_foods`, with nothing applied |
| reset | `SaveTail::ResetAllState()` runs after `LockForGame` (a new game or a load starts empty) and inside `Ext::UnlockForGame`, only for the lock's owner: a reload whose new loop locked first keeps the state it decoded |
| row off for the town | the codec is never called; the next save carries no record |

Because the save names `ext:restricted_foods`, retail (and a build without the extension) shows the missing-mod notice instead of playing the town under different rules; with the extension turned off our loader turns it back on for that town and restores the list (DESIGN.md section 2).

## 3. The seams: no work per storage row

`BeginFrame` (once per frame, beside `SpreadWarmth::BeginFrame`) resolves the saved paths against the loaded game when the list or the lock epoch changes: it walks the Edible `RawMaterialDescription` objects (`ExternalFactory::FindNext`) and keeps the pointers whose path is in the list in `g_blocked`, a short flat array. `g_on` is the row's locked value AND a non-empty `g_blocked`.

Both seams test `g_on` once, above their loops (`param_4 == 0 && g_on` in the picker, above both passes), and inside the loop do a linear scan of the array only for a row that already passed retail's own bit test. No map, no string, no allocation per row. With the row off, or nothing resolved, each site pays one load and a not-taken branch.

## 4. The page (the shared extension window)

Per DESIGN.md "Surfaces", the in-game controls are not a button per feature: the main toolbar gets ONE extension button, which opens one window with a tab per active extension page. This is the first extension that needs it, so this hand-in also builds the host (`src/ext/toolbar/`, `toolbar.h` has the contract):

| | |
|---|---|
| button | one button in the Reports group (Status Bar, Limits, Town Hall, Help), sort priority 950, between Town Hall and Help; built from the toolbar's utility-button template (template 3). The toolbar keeps a **null** entry in its item list for it, and every reader of that list skips it. The nesting depth for the shortcut keys is read from the group, the parent of every item, so an empty Reports group, or a mod item sorted ahead of the button, does not put the null first. Not added when no page is offered (no active contributor), outside a running game, or under the gate's command-line faithful profile |
| window | `Dialog/ExtensionsWindow.rsc`: a utility-style window like Limits and Status Bar (not modal, does not pause the game, the game's own title bar, remembers its place, closes with its close button or the toolbar button). Its body is ONE tab control; `UI::TabElement::AddTab` adds one tab per active page when it opens, as the Options dialog's Enhanced tab is added. Pages beyond eight are logged and left out. `~Window` closes every open page, so a page never keeps pointers into a freed window |
| page | `Contribution { id, caption, page, open, changed, close }`: offered only while its row is active for the game. The controls are **live**: `changed` writes the state at once (no OK / Apply). The tab's label is the text `caption` |
| element ids | each open page owns the span `[idBase, idBase + kPageIdSpan)`; `open` receives `idBase`, and `changed` is called only for ids in the page's own span, with the local id. Two pages cannot answer each other's clicks |
| shared helpers | `OurString`, `SetTableText`, `Lower` are exported by the host; pages use `Ext::FindElement<T>` |
| this page | `Dialog/RestrictedFoodsPage.rsc:pageRestrictedFoods`, controller `src/ext/restricted_foods/toolbar_page.cpp` |

**The Foods page.** One cell per edible food of the running game (every loaded `RawMaterialDescription` with the Edible bit, once per template path, sorted by the name the game shows), laid out like the Town Hall's Inventory tab: a table of cells, as many columns as fit the list (four in the game's) and filling its width (`Ext::ListWidth`, `src/ext/ui/list_width.h`), each a check box plus the Town Hall's compact item label (20 px icon, small font), so a modded game's hundreds of foods are a short scroll. Checking a food applies it at once; the cell tints red while the food is not eaten.

**Show all foods.** A check box under the list. Unchecked (the default) shows the foods the town has made or stored (`CitizenBehavior::field_0x110`, the town's known-material list: `RawMaterialComponent::Startup` appends a template the first time an entity of it is created, nothing removes it, so it is the Town Hall inventory's "ever made" list); checked shows every edible food, needed to block a food before the town makes it. Checked foods never disappear, and a change rebuilds the cells but keeps every check. A town with no known foods opens on all. The choice belongs to the current town: a new game, a load or a reload starts it unchecked.

## 5. Where

| file | change |
|---|---|
| `src/ext/restricted_foods/restricted_foods.{h,cpp}` (new) | the list (`BlockedPaths`, `IsPathBlocked`, `SetPathBlocked`, `ReplaceBlockedPaths`, `ResetState`), `Resolve` / `NeedsResolve` / `SetRowActive`, `g_on`, `g_blocked`, `IsBlocked`. Standard C++ |
| `src/ext/restricted_foods/restricted_foods_town.cpp` (new) | the factory walk, `TemplatePath`, `BeginFrame` |
| `src/ext/restricted_foods/save_state.cpp` (new), `src/ext/state_codecs.cpp` (new) | the codec and its registration (section 2) |
| `src/ext/restricted_foods/toolbar_page.cpp` (new) | the Foods page (section 4) |
| `src/ext/savetail.{h,cpp}` | `StateCodec`, `Writer` / `Reader` (promoted for the first codec), `FindCodec`, `ResetAllState`, `ApplyState`; `ActiveRecords` calls the codec's `encode` |
| `src/ext/registry.{h,cpp}` | the row (class C, version 1, OFF); `UnlockForGame` resets the class C state for the lock's owner |
| `src/ext/toolbar/toolbar.{h,cpp}`, `toolbar_window.cpp` (new) | the shared host: the page list, `Offered`, the window, `InsertAt` / `Style` / `Toggle`, the id spans, the shared helpers |
| `src/ext/ui/find_element.h` (existing) | `Ext::FindElement<T>`, used by the page and the host |
| `src/game/Dialog/toolbardialog.cpp` | the button's seams: the null item, `InsertAt` / `Style` while the Reports group is built, `Toggle` on the click, the null guards in the three readers, the depth read from the group |
| `src/game/Component/citizencomponent.cpp`, `citizenaicomponent.cpp` | the two eating seams |
| `src/game/Game/gameupdateloop.cpp`, `gameupdateloop_load.cpp`, `gameupdateloop_serialize.cpp` | `BeginFrame()`; `ResetAllState()` after the lock in `Load`; `ApplyState` after the whole lock block in `ExtSave_ReadTail` |
| `src/ext/ui/Dialog/ExtensionsWindow.rsc`, `RestrictedFoodsPage.rsc` (new), `ExtensionsStringTable.rsc`, `ExtensionsSprites.rsc`, `CMakeLists.txt`, `src/ext/resources/resources.cpp` | the window and page resources, their texts (`ExtensionsWindow*`, `RestrictedFoods*`), the embed list and the provider prefixes |
| `tools/ext_icons/restrictedfoods.py`, `Entry_restricted_foods.png`, `Toolbar_extensions.png`, `ext_sprites_data.h` | the row icon and the toolbar button's icon, regenerated with `tools/ext_sprites.py` and `tools/ext_sprites_bake.py` |
| `tests/unit/ext_restricted_foods_test.cpp`, `ext_toolbar_test.cpp` (new), `ext_rsc_text_test.cpp`, `tests/unit/sources.cmake` | the tests (section 7) |

No engine change and no new setting keys beyond `ext.restricted_foods` (the row's own toggle).

## 6. Behaviour notes

* **Off is retail.** Under the faithful profile the row is forced off; a build with the row off runs both functions unchanged.
* **Starvation.** A list that blocks every food a town has leaves citizens with nothing they may eat; they starve as in a town with no food. That is what the rule asks for.
* **The row cannot be switched on in a running town** (DESIGN.md: the lock is on activation). Turn it on in the Extensions menu before starting the town; what the page edits is live from then on.
* **Two materials with one name.** They are two entries (two paths); the page lists both.

## 7. Verified and not verified

* **No game content ships (hook rule 7).** The icons are generated by `tools/ext_icons/` and baked by `tools/ext_sprites_bake.py`; the `.rsc` files are the project's own text and reference retail objects only as bases; no retail table, texture, model or string is copied.
* **Unit tests:** the registry row; the list (add, remove, repeat, case); the codec's round trip with names the old text grammar would have cut (`Fish, Smoked`, `Dried.Fish`, `RawMaterialHoney`, `A;B#C:D/E`, a non-ASCII byte); an empty list is still a valid record; every kind of damaged payload is refused and leaves the old list alone; `ApplyState` with a damaged payload returns false, names `restricted_foods` and leaves the old state; the faithful profile writes no record; the state travels in the tail and goes with the game; `UnlockForGame` by a non-owner keeps the state; the list resolves into the flat array; `g_on` follows the row and the list; the page list and its id spans; the embedded `.rsc` files parse and use only types `Extensions.rsc` already uses.
* **Stress run (hook rule 6).** `tools/stress_run.sh --no-build` on a perf-flavour build of this branch (MinGW, Windows), the fed stress town (800 citizens, speed 10, 6000 frames, seed 31337, map medium), one run per leg, machine load CLEAN in every leg, the deployed exe checksum unchanged across each run. Update is the harness's `update ms/frame`:

  | leg | profile | update ms/frame (p95) | sim ms / iteration | fps |
  |---|---|---|---|---|
  | A | `/profile:faithful` | 6.34 (7.87) | 0.63 | 60.0 |
  | B0 | per-entry, every extension off (control for the profile mode) | 10.6 (15.9) | 1.06 | 47.4 |
  | B | per-entry, only `ext.restricted_foods` on, empty list | 10.7 (17.1) | 1.07 | 47.3 |
  | C | as B with 20 pointers in the blocked list (a temporary, uncommitted change to `BeginFrame`; none match a food, so the town behaves the same and every Edible row scans all 20) | 10.5 (15.9) | 1.05 | 47.5 |
  | D | `/profile:all`, every fix and extension on | 7.71 (12.5) | 0.77 | 41.4 |

  The row costs nothing measurable: B and C against B0 differ by less than 0.2 ms per frame, inside the run-to-run noise (the p95 column moves more than that between B0 and B). Leg A is faster than the per-entry legs by about 4 ms per frame, and B0, with every extension off, shows the same gap. The per-entry legs run with the fixes on (the ruled default), and the fixes change the town itself (`fix.start_barn_height` alone removes about 3,700 trees), so A and B0 are different towns and the gap is not this row's. Leg D adds every extension, so its numbers are not this row's. Not measured: a faithful leg from before this change on the same machine, and a list that blocks foods the town actually stores (that changes what citizens eat, so it is a different town).
* **Looked at in a Windows run by the author, on this revision:** the toolbar button opens and closes the window; the Foods page lists the foods; the "Show all foods" check box works and the page is left-aligned; the checked foods survive a save, quit and load; a new game starts with the check box and the list empty; with wheat checked and no other food, the citizens did not eat it and starved.
* **Known limit:** the window is not reopened by a load. Vanilla utility windows are (the tool manager's pinned-window set, written in the main save and reopened by `PlayerToolManager::Startup`), but that set is keyed by a retail resource path, so this window cannot use it without writing something retail cannot read. Where shared window state should live is a question for the maintainer.
* **Not verified:** a second extension's tab (only one page exists), the resave golden for the class C row under the official fixture, Linux and macOS.
* To try it: build, turn **Restricted foods** on in the Extensions menu, start a town, press the new button in the Reports group of the toolbar, check a food. The log lines to read are `[ExtToolbar]`, `[RestrictedFoods]` and `[ExtSave]` on stderr.
