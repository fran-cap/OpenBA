# STORAGEMETRICS hand-in (Storage space)

`ext.storage_metrics`: a Storage tab in the Town Hall window with the used and total space of the town's
stockpiles, storage barns and trading posts. One class A row, **off by default**, live, no state of its own
(nothing is saved in the town, no sim effect).

## 1. What it does

With the row on, the Town Hall window (`Dialog/TownHall.rsc`) gets one more tab after the game's own, "Storage".
It lists, top to bottom:

| Line | Buildings | Used / capacity | Full |
| --- | --- | --- | --- |
| Stockpiles | how many stand finished | e.g. `12,400 / 15,000` | e.g. `82.7%` |
| Storage barns | | | |
| Trading posts | | | |

Under a kind that has more than one building type (a mod's special stores beside the game's barn), each type gets
its own line, by name, set in under the kind's total. A kind with one type shows the total only, so the game
without mods shows three lines. A kind with no building shows `0`, `0 / 0` and `-`.

**The measure is the game's own.** Capacity is `StorageComponent::capacity` (the description's limit; times the
footprint for an area-based store) and used is `StorageComponent::QueryCount` (each stored item's count times its
weight): the two numbers `StorageComponent::CanAdd` compares when a citizen brings goods. So "full" here is the
point at which the game itself refuses a delivery. A store can read above 100% (a merchant unloads into a full
trading post).

**Which building is which, with any mod.** Never by name or template path, only by components:

| Kind | Rule |
| --- | --- |
| Trading post | has a `TradeComponent` (the town's `tradePosts` list) |
| Stockpile | a storage location whose limit grows with its footprint (`StorageDescription` +0x20, the kit's `_areaBasedLimit`) |
| Storage barn | every other storage location (the town's `storageLocations` list) |

A mod's fixed-size store of logs and stone therefore counts with the barns (it does not grow when dragged), under
its own name. Left out:

- Markets and homes: not storage locations.
- A staffed workplace that is a storage location for its own goods (a `WorkPlaceComponent` with a profession;
  the game's tavern, 300 of space each; with MegaMod also the hunting cabin, the herbalist, the forester, the
  gatherer): not room the town stores goods in. A barn's workplace has no profession. A mod store that is
  staffed is left out too.
- Two templates under one name (MegaMod has a second "Storage Barn") share one line.
- A building that does not stand finished, by the state its state machine was last asked for
  (`StateMachine::currentInput`, `Standing` in `storage_math.h`): a site being cleared, stocked or built (states
  0, 1, 8) and a building being taken down or in rubble (3 to 7). A trading post is in the town's list from the
  moment it is placed, so this is what keeps a half-built one out.

The type's name is the template's toolbar title (`ToolbarDescription` `_str1`); a template without one is named
by its kind.

## 2. Where it hooks

Four `EXT SEAM` lines in `src/game/Dialog/townhallui.cpp`; each returns at once while the row is off or for a
window that has no tab:

- `TownHallUI::OnCreated`, after the game's six `Init*` calls: `TownHallCreated` adds the tab with
  `UI::TabElement::AddTab` (the call the extension window and the Enhanced tab use).
- `TownHallUI::OnUpdate`: `TownHallUpdate` re-reads the town every 30 frames (the game's own Town Hall refresh
  step) and only while the Storage tab is the one showing; it sets a text only when its figure changed, and
  rebuilds the rows only when the set of lines changed.
- `TownHallUI::OnSerializeSettings`: `SavedTab` records the Storage tab as the first tab. The window's remembered
  tab goes into the dialog's settings blob (`UI::UserInterface`'s settings pass, a retail format that outlives the
  window), and an index retail does not have must never reach it: a later window without the tab would be told to
  show a tab that is not there. The cost: a window closed on the Storage tab reopens on the first tab.
- `~TownHallUI`: `TownHallClosed` forgets the window's elements. Two Town Hall windows can be open at once, so the
  elements are kept per window.

Files: `src/ext/storage_metrics/storage_math.h` (the sums, the lines and the texts; standard C++),
`storage_town.cpp` (the walk over the town's two lists, read only), `townhall_tab.cpp` (the tab),
`src/ext/ui/Dialog/StorageMetricsTab.rsc` (our own text `.rsc`: the tab's button and page, in the Town Hall's own
looks by reference; strings `StorageMetrics*` in `ExtensionsStringTable.rsc`). The tab's icon is our own 16 px crate glyph
(`ExtensionsGlyph_storage_metrics_tab` in `ExtensionsSprites.rsc`, drawn by `tools/ext_icons/storagemetrics.py`).

**Why a Town Hall tab and not a page of the extension window.** The maintainer asked for it there: the report
belongs with the Town Hall's own Inventory and Production tabs. `docs/extensions/DESIGN.md` "Surfaces" has the new
row (a tab in one of the game's own tabbed windows). Why off by default although class A: it changes a retail
window (the same call as `fps_counter` and `building_finder`).

**Live toggle.** A Town Hall window opened after the row is switched gets (or does not get) the tab; a window that
is already open keeps what it was made with.

## 3. Tests

`tests/unit/ext_storage_metrics_test.cpp`: the kind is decided by components; the request's example reads
`12,400 / 15,000`; thousands grouping; percent rounding, no capacity, over 100%, sums past 32 bits; an empty town
shows three empty totals; one type per kind shows totals only; a kind with two types lists them by name under its
total; a row is reused while its line is the same. `ext_rsc_text_parse_storage_metrics_tab` (the names the
controller looks up), the registry's default-off rule and the row icon (`tools/ext_icons/storagemetrics.py`, baked
into `ext_sprites_data.h`) are covered by the existing suites.

## 4. Verified, and not

Debug build, Windows, DX11, save `RetailA` (a large retail town, no mods), row on:

- The tab is added as the seventh tab (`[StorageMetrics] the Storage tab is tab 6 of the Town Hall window`), its
  button shows the icon and "Storage", and the window keeps its size.
- The page showed Stockpiles 20 buildings `786,475 / 617,250` `127.4%`, Storage barns 11 `94,574 / 164,000`
  `57.7%`, Trading posts 1 `300 / 20,000` `1.5%`, and the figures moved between two reads a few seconds apart.
  The stockpiles really are over their limit in that save: its stored capacities are not the installed
  templates' (barns saved at 20,000 where the template gives 6,000, a 10 x 10 stockpile at 50,000 where 250 a
  tile gives 25,000) and single stockpiles hold up to 154,000, so the town was played under other limits. The
  tab reads the capacity the save holds, which is the one `CanAdd` uses.
- The tab is shown by the panel harness (`BANISHED_PANEL_HOLD=ImmigrationComponent BANISHED_PANEL_HOLD_TAB=-1`,
  `[PanelHold] frame 430: showing tab 6 of 7`), which makes the call a click on the tab's button makes.
- Unit 2337 / 0 failures / 3 kit skips. The gate (`tools/build.sh run`, faithful profile) boots into a new game
  with an empty crash log.
- MegaMod (7 parts, hard-linked into `WinData` for the run and removed after), new game, row on. Templates
  from `BANISHED_TEMPLATE_DUMP=StorageLocationDescription` (767 of 8,286 placeable buildings); a Town Hall, two
  game barns, `RKMCbarn`, `ECStorageBarn`, `GTALargeBarn`, `RKFirewoodStorage1x2`, `RKMCwoodpile1`,
  `RKMCstonepile1`, `RKMCbarrels1`, `CornerStockpileT1`, a hunting cabin, a tavern and a trading post placed
  finished by `BANISHED_SHOWROOM` (with a `bench 60` line). The tab read Stockpiles 1 `1,500 / 6,000`; Storage
  barns 10 `4,580 / 61,900` with the lines Barn, Barrels Storage, Firewood Storage, Old Large Barn, Stockpile,
  Stonepile Storage, Storage Barn (4, `1,430 / 30,000`); Trading posts 1 `0 / 60,000`. The limits are MegaMod's
  (barn 7,500, trading post 60,000). The cabin and the tavern are not counted. Ten of the eleven barn-kind
  buildings placed are counted: the wood pile and the stone pile were sited on one tile by the harness and one
  of them does not show.
- Reopening: the window closed on the Storage tab comes back on it (`BANISHED_PANEL_HOLD_REOPEN=1`:
  `closed the held panel`, then `reopened as <another window>, showing tab 6 of 7`); closed on Inventory it
  comes back on Inventory (`showing tab 2 of 7`, the game's own record).

Not done:

- With MegaMod only harness-placed buildings were read (below): no played MegaMod town, and no stored goods
  beyond what the placement puts in.
- Kind is decided by the area-based limit, so a mod "stockpile" with a fixed limit (MegaMod's
  `CornerStockpileT1`, 12,000) is listed under Storage barns. The drag-placed yards (`StorageYard`,
  `Yards\BasicStorageYard`) could not be placed by the showroom (no footprint) and were not seen.
- A click on the tab with the mouse, a second Town Hall window, and toggling the row with a window open were
  not tried by hand.
- Vulkan, Linux, macOS and Android were not run. The resave gate was not run (nothing serialized changed).
- The figure cells are set left under their headers; right-set figures need a text alignment the text reader
  does not take from our `.rsc` yet.

## 5. Above 100%

A reading above 100% means the buildings hold more than their limit: nothing more is accepted until they drop
below it. It is not a count of goods waiting for room (those lie on the ground or in the workplaces and are in no
storage). Every delivery path read (`DropOffRawMaterial::Deposit`, `AbsorbResourceComponent`) asks `CanAdd` first,
so play under one set of limits stays at or under 100%, give or take the merchant's unloading at a trading post.
A town reads above it when its limits went down after the goods went in: a storage mod removed or changed, or a
save from another build.

## 6. The panel harness (permanent)

`BANISHED_TEMPLATE_DUMP=<DescriptionTypeName|*>` (`src/diag/template_dump.cpp`, new) lists the placeable
buildings whose template carries a description, so a list for the showroom can name a mod's templates.

`src/diag/panel_hold_harness.cpp` gained two things this work needed and the next panel check will too:

- After the select it presses the panel's "go to" (`PlayerToolManager::CenterOnEntity`): the panel is pinned and
  the camera is on the building. Retail closes an unpinned panel whose building is off screen
  (`EditTool::Update`, 0x1400fcebc), so on a loaded save the held panel was closed again on the next frame
  whenever the building was out of view.
- `BANISHED_PANEL_HOLD_TAB=<index>` (`-1` = the last) shows that tab of the held panel's first tab control, 30
  frames after it opened. Listed in `src/app/options.h` and the `menu-driver` skill.
- `BANISHED_PANEL_HOLD_REOPEN=1` closes the held panel 90 frames after it opened (the deselect, then the close
  button's `UserInterface::CloseDialog`, since the panel is pinned), selects the building again 30 frames later
  and logs the tab the new window shows.

## 7. The tab a window reopens on

The game writes the active tab into the window's settings record, which is in the save, and reads it back into
`SetActiveTab` unchecked; `TabElement` indexes its children with it in three places. An index for the added tab
would be read out of range by retail and by a run with the row off, so the record gets tab 0 for it
(`SavedTab`). That the window closed on an added tab is kept in memory instead, per building (keyed by the
building's settings buffer), for the session: `TownHallSettings` at the end of `OnSerializeSettings` notes it on
write and shows the last added tab on read, when there is one. After a load, or with the row turned off, the
window opens on Overview.

Open for a later ruling (not decided here): where an extension keeps state in the save. Read so far: a record
with an unknown controller type id in the settings buffer is skipped by retail's reader by its length
(`DialogInstance::OnOpened`), extra bytes inside a known record are not (the reader's close reads the trailer
where it stands), and the buffer has a fixed size.
