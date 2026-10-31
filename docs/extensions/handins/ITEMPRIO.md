# ITEMPRIO hand-in

`ext.item_priority`: the town ranks the items its citizens fetch from a storage (tools, fuel, clothing, health), and a citizen takes the best-ranked item the storages hold. One class C toggle (locked for the game, per-town state recorded in the save), off by default, `formatVersion` 2 (v2 added the Never list, section 9), with a Priority page in the shared in-game extension window.

Built to the rulings in DESIGN.md: the lock is on activation, a live window editing class C state is fine, and per-town state travels in the tail record. The save-state codec and the toolbar host are the ones `ext.restricted_foods` introduced.

## 1. What it does

A town with several items of one kind in its storages (a mod's Steel, Iron and Bronze tools in one barn, Coal and Firewood in one stockpile) cannot say which a citizen takes first: retail takes the item flagged `_preferred` (`RawMaterialDescription` +0x58), else a random one of those the citizen may take. With the row on the player ranks the items per category, and the citizen takes the best-ranked item. An item the list does not name is ranked after every named one; items that tie keep retail's choice among them. With no list for a category every item ties, so nothing changes.

| category | finder / `ef40` number | `_flags` bit | who takes it |
|---|---|---|---|
| Fuel | 1 | 6 | homes (the fuel fetch) |
| Tool | 2 | 9 | workers |
| Health | 3 | 13 | citizens seeking care (the health fetch) |
| Clothing | 4 | 14 | citizens dressing for the cold |

(Category 0, eating, is `ext.restricted_foods`'s.)

## 2. Where the item is chosen, and the seams

Every pick of an item of these categories is a two-pass choice over a storage's rows: count the rows whose category bit is set and that have stock, remember the last `_preferred` one, else roll a random one, then walk to it. There are three copies of that loop, and each gets one extra step before the choice: of the rows it would consider, only those of the best rank stay eligible.

| copy | file | note |
|---|---|---|
| `CitizenComp_Entity_impl_ef40`, categories 1..4 (retail 0x14008ef40) | `citizencomponent.cpp` | the storage item picker. Tools and clothing are taken through it (the finder's returned item). The finder also asks it "does this storage hold one" for every category while it looks for the nearest storage |
| `SelectAndTransfer`, Fuel | `citizenai_gather.cpp` | the fuel fetch runs its OWN inline copy of the pick after the finder named the storage, so the picker alone never decides which fuel is carried |
| the Health fetch's pick | `citizenai_growup.cpp` | the same, inline |

**Across storages.** The finder (`CitizenBehavior::FindBestTarget`) picks the NEAREST storage that holds any item of the category, so a better item in a farther storage would lose. The four call sites of the finder for these categories (fuel in `citizenai_gather.cpp`, health in `citizenai_growup.cpp`, tools and clothing in `citizenai_handlers2.cpp`) therefore run it once per tier through `FindTiered`, best first: with a rank ceiling that makes `ef40` skip every item ranked worse than the tier, taking the first tier that finds a storage; the last tier has no ceiling.

`Ext::ItemPriority::BeginFrame()` runs once per frame in `GameUpdateLoop::Update`, beside the other extensions'.

## 3. The hot path

`BeginFrame` resolves the saved paths against the loaded game when a list or the lock epoch changes: it walks the `RawMaterialDescription` objects (`ExternalFactory::FindNext`) and keeps, per category, the pointers whose path is in the list, in list order, in `g_ranked[category - 1]`. A material's rank is its index there.

Each seam tests `Active(category)` once, above its loops (`g_on` and a non-empty array for that category), and inside the loops scans the array only for a row that already passed retail's own flag test. No map, no string, no allocation per row. With the row off, or a category with no list resolved, each seam pays one load and a not-taken branch.

## 4. Class C: the state, and where it lives

The state is the town's four ranked lists. It is per-town, so the test in DESIGN.md makes the row class C, and the lists never live in `settings.cfg`.

| | |
|---|---|
| row | `item_priority`, `Tier::Extension`, `Class::C`, default OFF, `formatVersion` 2 |
| record | the row's own record in the tail chunk; written whenever the row is active, even with empty lists; zero bytes for a faithful game |
| codec | `Ext::ItemPriority::Codec()` (`src/ext/item_priority/save_state.cpp`), registered in `src/ext/state_codecs.cpp` |
| key | the item's **template path**, `External::GetPath` of its `RawMaterialDescription`, compared without case. Not `_name`, which is a sprite / string key that mods reuse. Never an index: mods change the load order between sessions. A path the game no longer has stays in the list and ranks nothing |
| wire format v1 (v2 adds the Never kind, section 9) | little-endian: `u32 categories` (always 4: Fuel, Tool, Health, Clothing, the finder's numbers 1..4), then per category `u32 count` and `count` x { `u8 kind` (1 = a ranked item; 2 = a Never item, v2), `u32 length`, `u8 path[length]` }, best first. Documented at the top of `save_state.cpp` |
| decode | accepts exactly what encode writes; refuses a different category count, a count the payload cannot hold or above 4096, an unknown kind, an empty, over-long (more than 1024 bytes) or control-character path, a path twice in one list (without case), bytes left over, or a cut-short payload. A refused payload changes nothing: the old lists stay |
| load | `ExtSave_ReadTail`: `SaveTail::ApplyState` after the whole lock block, on both routes; a payload that does not decode is fatal through the same box as a damaged manifest, naming `ext:item_priority`, with nothing applied |
| reset | `SaveTail::ResetAllState()` after `LockForGame`, and inside `Ext::UnlockForGame` for the lock's owner only |

## 5. The page

The Priority tab of the shared extension window (RESTRICTEDFOODS.md section 4 describes the window and its one toolbar button, and the element id spans). A tab strip (the engine's tab control, declared in the page's `.rsc` like the window's own tabs) chooses the category (Tools, Fuel, Clothing, Health), so the tab that is showing is the category that is showing; each tab holds that category's list, one cell each: a **check box** (ranked or not), the item's icon and name, its **rank number** (best first), and **Top / Up / Down**. Checking an item ranks it at the bottom, unchecking removes it, Up / Down swap a ranked item with its neighbour, Top moves it to rank 1. Ranked items come first, then the rest by name. Every change applies at once (live: no Apply).

**Show all items.** A check box under the list. Unchecked (the default) shows the items the town has made or stored (`CitizenBehavior::field_0x110`, the town's known-material list) and the ranked ones; checked shows every item of the category. The category and this choice belong to the current town: a new game, a load or a reload starts them at Tools and unchecked.

A click on a cell's check box or Top / Up / Down is handled while the dialog is still dispatching that very click, so it never destroys a cell: it edits the list and re-fills the cells that exist. Cells are created only from the category tabs and the check box, which sit outside the table; the tables of the tabs that are not showing are emptied.

Element ids are local to the page (the host gives it an `idBase`): the category tab buttons 10 to 13 (children 3 + 2i of the strip's tab control), the check box 20, a cell 100 + 3 x its position (+ 0 check box, + 1 Up, + 2 Down). The page's objects are `Dialog/ItemPriorityPage.rsc:pageItemPriority`, controller `src/ext/item_priority/toolbar_page.cpp`.

## 6. Where

| file | change |
|---|---|
| `src/ext/item_priority/item_priority.{h,cpp}` (new) | the lists (`Lists`, `Rank`, `Unrank`, `Move`, `RankOf`), `Resolve` / `NeedsResolve` / `SetRowActive`, `g_on`, `g_ranked`, `Active`, `RankOfMat`, the rank ceiling and `FindTiered`. Standard C++ |
| `src/ext/item_priority/item_priority_town.cpp` (new) | the factory walk, `TemplatePath`, `BeginFrame` |
| `src/ext/item_priority/save_state.cpp` (new), `src/ext/state_codecs.cpp` | the codec and its registration (section 4) |
| `src/ext/item_priority/toolbar_page.cpp` (new), `src/ext/toolbar/toolbar.{h,cpp}` | the Priority page and its entry in the page list |
| `src/game/Component/citizencomponent.cpp` | the `ef40` seam: a pre-pass for the best rank, then the filter in both passes |
| `src/game/Component/citizenai_gather.cpp`, `citizenai_growup.cpp`, `citizenai_handlers2.cpp` | the fuel and health picks' filters; the four finder call sites through `FindTiered` |
| `src/game/Game/gameupdateloop.cpp` | `BeginFrame()` |
| `src/ext/registry.{h,cpp}` | the row (class C, version 2, OFF) |
| `src/ext/ui/Dialog/ItemPriorityPage.rsc` (new), `ExtensionsStringTable.rsc`, `ExtensionsSprites.rsc`, `CMakeLists.txt`, `src/ext/resources/resources.cpp` | the page resource, its texts, the row icon's entry, the embed list and the provider prefix |
| `tools/ext_icons/itemprio.py`, `Entry_item_priority.png`, `ext_sprites_data.h` | the row icon (programmer art, class C red), regenerated with `tools/ext_sprites.py` and `tools/ext_sprites_bake.py` |
| `tests/unit/ext_item_priority_test.cpp` (new), `ext_toolbar_test.cpp`, `ext_rsc_text_test.cpp`, `tests/unit/sources.cmake` | the tests (section 8) |

No engine change and no new setting keys beyond `ext.item_priority` (the row's own toggle).

## 7. Behaviour notes

* **Off is retail.** With the row off, or on with every list empty or unresolved, `g_on` is false and the three loops and the four finder calls are retail's own. Under the faithful profile the row is forced off.
* **Ties keep retail's choice.** Among the items of the best rank the `_preferred` flag and the random roll decide, as in retail; an unlisted item ties with every other unlisted one.
* **Two materials with one name** are two entries (two paths).
* **The ceiling** is a global set only while `FindTiered` runs and restored when it returns; the game's finder calls are on the main thread.
* **A change applies to the NEXT decision.** The item is chosen when a citizen sets out for the storage (the finder names the storage and the item then, as in retail), and the pickup action carries that choice; a ranking changed meanwhile does not retarget a trip already under way.
* **Not in this version:** an item discovery log and a hotkey. A category's ranking applies to every storage of the town; there is no per-storage setting.
* **Considered, not built: a town-wide scan.** The finder scans a primary list, then a fallback list, and a primary hit inside the "B" radius band returns at once, so a better-ranked item that sits only in a fallback-list storage can lose. A variant where the citizen scans every storage for the best-ranked item would remove that and could follow a ranking change mid-walk, at the cost of more long trips and a change to a finder every caller shares. It would be its own opt-in extension.

## 8. Verified and not verified

* **No game content ships (hook rule 7).** The icon is generated by `tools/ext_icons/`; the `.rsc` files are the project's own text and reference retail objects only as bases.
* **Built and tested** on Windows (MinGW), debug unit build and the game build, on `main` 0c9c3c2 (with PRs 28, 31 and 34 merged): unit 1424 / 0 failures / 8 skipped (the kit skips); `tools/abi_audit.py` OK; `tools/acceptance/acceptance.sh` 0 lines off target (R14 to R16 need a new-game log directory and were not run); `OpenBA.exe` builds. (The stress table below was measured on an earlier revision, before the tab strip.)
* **Unit tests:** the registry row; the lists (rank, unrank, move, by path without case); the resolution into the flat arrays (the list's order is the rank, a path the game lacks ranks nothing and does not shift the others); `g_on` and `Active` following the row and the lists; the tier search (best first, stops at the first hit, retail's single search with no list or the row off); the codec's round trip with awkward paths, an empty list is still a valid record, every kind of damaged payload is refused and leaves the old lists alone; `ApplyState` with a damaged payload returns false and names `item_priority`; the faithful profile writes no record; the state travels in the tail and goes with the game; the reset follows the lock owner; the page list and the embedded `.rsc`.
* **Stress run (hook rule 6).** `tools/stress_run.sh --no-build` on a perf-flavour build of this branch (MinGW, Windows), the fed stress town (800 citizens, speed 10, 6000 frames, seed 31337, map medium), one run per leg, machine load CLEAN in every leg, the deployed exe checksum unchanged across each run. Update is the harness's `update ms/frame`:

  | leg | profile | update ms/frame (p95) | sim ms / iteration | fps |
  |---|---|---|---|---|
  | A | `/profile:faithful` | 4.34 (5.92) | 0.43 | 60.0 |
  | B0 | per-entry, every extension off (control for the profile mode) | 8.63 (13.4) | 0.86 | 59.9 |
  | B | per-entry, only `ext.item_priority` on, empty lists | 8.99 (13.3) | 0.90 | 60.0 |
  | C | as B with a ranked list in every category (a temporary, uncommitted change to `BeginFrame` that ranks every material the town has in each category: 2 fuel, 2 tool, 1 health, 3 clothing; so every pick runs the pre-pass and the rank scan) | 8.85 (13.3) | 0.89 | 59.7 |
  | D | `/profile:all`, every fix and extension on | 8.35 (14.8) | 0.83 | 55.3 |

  The row costs nothing the run can resolve: B and C against B0 differ by +0.36 and +0.22 ms per frame, and D, which adds every other extension, is below B, so the spread between legs is the run-to-run noise. Leg A is faster than the per-entry legs by about 4 ms per frame, and B0, with every extension off, shows the same gap, so it comes from the profile mode and not from this row. Not measured: a faithful leg from before this change on the same machine, and a town with long lists (the stress town has few tools, fuels, clothes and medicines, so the lists in C are short; the scan per row is linear in the list length).
* **Looked at in a running game by the author, on an earlier revision of this extension (Windows):** the Priority tab builds and switches; the category buttons switch the list; a check box ranks and unranks an item; Up / Down move a ranked item; a ranking survives a save, quit and reload together with a food restriction in the same town; the tool ranking decides which tool new workers take (a town with Iron Tools and Steel Tools in one barn: vanilla hands out Steel, which carries `_preferred`; with Iron ranked first they took Iron, and Steel first they took Steel).
* **Looked at in a running game by the author, on this revision (Windows):** the page's layout only: the Foods-style row spacing, the tab strip for the categories, the small Top / Up / Down buttons in one column and the rank numbers right-aligned in their column. The tab switching, ranking, saving and worker behavior above were not re-run on this revision.
* **After the rebase onto 244a393 (Windows):** the author ran the rebuilt exe; the Priority page and the ranking still worked (not every category was repeated).
* **In a running game (this revision before the rebase, Windows):** the Priority page and its check box work; a citizen takes the best-ranked TOOL, FUEL and CLOTHING item; across storages, with iron tools ranked first, citizens who spawned beside a barn holding only steel walked about 40 blocks to the barn holding iron. The categories were each run on their own, not from one save.
* **Class C resave (DESIGN.md "Gates"), Windows, per-entry profile with `ext.item_priority` on:** a town save made with the row on and ranked lists (fuel, tools) was loaded and resaved unattended twice (`tools/resave_gate/resave_gate.sh`, roundtrip mode, an isolated game directory). The two resaved images are byte-identical: 54,848,761 bytes raw, the same compressed hash (`fc458e07822ea265`), the tail 80 bytes, no crash log. The in-process loaded-vs-resaved compare is DIFF, as the gate's README says it is by design (resource numbering depends on the writing process), so the first resave is the golden.
* **Not verified:** a citizen taking the best-ranked HEALTH item; a faithful-profile resave leg (with MegaMod installed both `main` 244a393 and this branch crash at resource load under `/profile:faithful`, before any extension code runs, so that leg could not be made on this machine; the off path is covered by the unit tests); Linux and macOS.
* To try it: build, turn **Item priority** on in the Extensions menu, start a town, press the extension button in the Reports group of the toolbar, open the **Priority** tab, rank two tools (or Coal above Firewood), and watch which new workers take. The log lines to read are `[ItemPriority]` and `[ExtSave]` on stderr.

## 9. Never (2026-10-27)

An item can be set to **Never**: citizens do not take it at all, as `ext.restricted_foods` does for a food, even when it is the only one the storages hold. Each category keeps a second list, the town's Never items, next to its ranking; an item is on the ranking or on the Never list, never both (ranking a Never item takes it off the list; setting an item to Never takes it off the ranking).

* **The seams.** A Never item has the rank `kNever` (0x7fffffff), worse than any ceiling the tier search sets, so the picker (`ef40`) never counts it; the fuel and health picks, which run their own copy of the loop, test the same rule through `Eligible(category, material, bestRank)`, which replaces the rank comparison of the three loops: a row passes when its rank is the best rank of the rows AND it is not Never. (A storage holding only Never items has the "nothing eligible" best rank 0x7fffffff, which is also `kNever`, so the rank alone could not tell them apart.) `Active(category)` is true when the category has a ranked or a Never list that resolved, so a Never list alone narrows the pick; `FindTiered` runs its single unconstrained search then. The finder's "does this storage hold one" goes through the same picker, so a storage that holds only Never items of the category is not chosen.
* **The page.** Each row has a **Never** button (after Down). It marks the item Never (the rank column reads "Never" and the row's border is tinted red) and, pressed again, allows it (the item is then unranked). Checking the item's box ranks it, which also allows it.
* **Wire format v2.** The entry kind 2 means a Never item, written after the ranked entries of its category in the same list (the count covers both); no path may appear twice in a category, ranked and Never together. v1 payloads (ranked entries only) load unchanged; a kind 2 inside a v1 payload is refused. The registry row's `formatVersion` is 2, and the record is written at that version whenever the extension is on, whether or not a Never entry exists (the bytes are the same as v1 then). A build that only knows v1 refuses the whole save, as for any extension record in the lock set, and names `ext:item_priority` and the saved version.
* **What it does not do.** It stops a citizen TAKING the item from a storage. Items already carried or already in a home stay where they are; a profession whose only tool is Never gets no tool (the work waits), and a home whose only fuel is Never gets no fuel: that is what Never means. Markets, trade and the town's own counts are not touched.
* **Tests.** `tests/unit/ext_item_priority_test.cpp`: the lists keep ranked and Never exclusive; `Eligible` and `kNever` including the all-Never storage; a Never list alone makes the category active; the codec round trip at v2, a v1 payload loading, kind 2 refused under v1, a path both ranked and Never refused. Checked in a running game (2026-10-27): the Never button, the red border and "Never" in the rank column, a save and reload keeping it, and the Never behaviour itself: with iron tools and steel tools in the storages and steel set to Never, citizens took the iron tools, and once those ran out they went without a tool and did not take the steel ones.
