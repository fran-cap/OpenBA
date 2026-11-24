# STORAGEFILTER hand-in

`ext.storage_filter`: a storage building can refuse items its flags would allow, or take them only up to a number of units, from an Accepts tab of its window. One class C toggle (locked for the game, per-town state recorded in the save), off by default.

## 1. What it does

Retail decides what a storage building takes with the material's flags against the building's accept mask (`StorageComponent::AcceptsMaterial`, 0x14012c560) and its capacity (`CanAdd`, 0x14012cd80). Drop-offs, the choice of the storage a carrier walks to, the absorbing of a cleared site's piles and the pickup gate all go through `CanAdd`. The player cannot choose what a building takes.

With this row, each storage building (barn, yard, cart, a mod's storages) can hold a set of **refused items** and a **cap** per item. A building counts as a storage building when it has a `StorageLocationComponent`, the component deliveries are routed to: a house, a workshop or the Town Hall has a `StorageComponent` for its own stock and gets no tab.

| | |
|---|---|
| flags still rule | the set only takes away from what the flags allow. A barn without the Logs flag takes no logs, listed or not. An item the flags do not allow is not listed in the tab at all: the tab shows only what the building can store |
| a refused item | `AcceptsMaterial` answers no, so `CanAdd` and everything behind it follow: no carrier brings it, no pile is absorbed into the building, the pickup gate does not count it as storage |
| a cap | the most units of one item the building takes. `CanAdd` answers no when the units already inside plus the units offered would pass it. 0 means no limit. Units already on their way are not counted, so several carriers arriving together can overshoot by a delivery |
| stock already inside | stays, and citizens take it as before. Only new deliveries stop (a refused item, or an item at its cap) |
| default | nothing refused. A building with no entry, and a town with none, gets retail's answer |
| not changed | capacity, the flag test itself, trade, any other building |

A refused item, or one at its cap, with no other storage that takes it stays on the ground, as an item with no storage does in retail: a tool a blacksmith made waits at the work site until a barn takes it again.

## 2. The seams

| place | retail | what the extension does |
|---|---|---|
| `StorageComponent::AcceptsMaterial` (`action_pf_ctors.cpp`), before the flag test | the flag test | `Refuses` answers no when the building's entry names the material |
| `StorageComponent::CanAdd` (`action_pf_roots_route.cpp`), after the weight is worked out | the capacity test | `OverCap` answers no when the building's cap on the item would be passed |
| `GameUpdateLoop::Update` (`gameupdateloop.cpp`) | | `BeginFrame`: resolves the saved paths to `RawMaterialDescription` pointers when the list or the lock epoch changed, and drops the entry of a tile that no longer holds a storage building |
| `StorageUI::OnCreated` / `~StorageUI` / `OnElementEvent` (`storageui.cpp`) | | the Accepts tab (section 4). `OnElementEvent` is a new override that calls the base |

The seam tests one flag (`g_active`: the row is on and some building has a refusal or a cap) and, only for a building that has an entry, scans a short flat array of pointers (DESIGN.md hook rule 4). With the row off, or no refusal and no cap anywhere, it costs one load and a not-taken branch.

## 3. Class C: the state, and where it lives

| | |
|---|---|
| row | `storage_filter`, `Tier::Extension`, `Class::C`, default OFF, `formatVersion` 1 (`registry.cpp`) |
| record | the row's own record in the tail chunk |
| codec | `Ext::StorageFilter::Codec()` (`src/ext/storage_filter/save_state.cpp`), registered in `src/ext/state_codecs.cpp` |
| key | the building's footprint corner tile (`MapComponent` tileX0 / tileY0), and the item's template path (the `RawMaterialDescription`'s `External::GetPath`), never an index. The entity id is not used: it is the entity's index in the master array, and removing any entity renumbers the last one. A tile is fixed for as long as the building stands. An entry applies only to a building with a `StorageLocationComponent` (the kind the tab is for) on its tile, so a house built on a demolished barn's corner does not inherit it: `BeginFrame`'s resolve removes an entry whose tile holds no such building, and until it has run the lookup (`EntryFor`) applies it only to a storage building on that tile |
| wire format v1 | little-endian: `u32 count`, then `count` x { `i32 tileX`, `i32 tileY` (both >= 0), `u32 items`, `items` x { `u32 length`, `u8 path[length]` }, `u32 caps`, `caps` x { `u32 length`, `u8 path[length]`, `i32 limit` (1..999999) } } |
| decode | accepts exactly what encode writes; refuses a count the payload cannot hold, a negative or repeated building tile, an entry with neither an item nor a cap, a cap path twice or a cap outside 1..999999, an empty, over-long or control-character path, a path twice in one building, bytes left over; a refused payload leaves the old list alone |
| limits | 65,536 buildings, 4,096 items per building, 1,024 bytes per path |

## 4. The tab

`StorageUI::OnCreated` adds an **Accepts** tab to the building window's tab control (`pageTab`) with `UI::TabElement::AddTab`, after the game's own tabs (`panel_tab.cpp`, `src/ext/ui/Dialog/StorageFilterTab.rsc`). A window without a tab layout is left as the game made it. The list is a table of cells like ext.restricted_foods' page: a check box, the item's icon and its name, one cell per item the town has made or held that the building's flags allow, plus what it already refuses by name when its flags allow it. A checked item is delivered here. An item the building's flags do not allow is left out of the list. Each cell also has the game's small number spinner (made at run time from the game's `Dialog/TownHall.rsc:spinnerCount`, loaded by its fixed path: the rsc text reader has no spinner type; if it does not load, the failure is logged once and the rows have no cap field) for the cap, 0 = no limit, up to 999999; it is greyed while the item is unchecked. The tab's button has its own glyph (`ExtensionsGlyph_storage_filter_tab`, a crate with a check mark). A change applies at once; the next frame's `BeginFrame` resolves it.

## 5. Tests

`tests/unit/ext_storage_filter_test.cpp` (the list, case-folded paths, the limits, the revision counter, the codec round trip, every kind of damaged payload, the lock-owner reset) and `ext_rsc_text_test.cpp` (the tab's rsc and string counts). The seam, the entity lookup and the tab are the exe's: tested in the game.

## 6. Not done

An all / none pair of buttons; a cap that counts units on their way; a blank field for "no limit" (the spinner always shows a number, so 0 means no limit); a refusal for market and trade stock (those use the trade components, which this does not touch).
