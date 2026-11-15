# ITEMLIMITS hand-in

`ext.item_limits`: a limit for one item beside its category's, set in the panel of a workshop, a pasture or a gatherer that makes it. One class C toggle (locked for the game, per-town state recorded in the save), off by default, `formatVersion` 1. It follows the shape `ext.restricted_foods` and `ext.item_priority` set: the save-state codec, the template-path key, the flat array the seams scan.

## 1. What it does

Retail limits production by category only (the Town Hall's Limits: Tools, Clothing, Food, Alcohol, ...). A mod's Steel tools and Iron tools share the Tools limit, so retail cannot say "50 steel tools, then stop". With this row, a workshop also stops making an item once the town holds as many of it as the item's own limit, even while the category is under its limit.

**One limit per item, town-wide** (the user, 2026-11-03). The limit belongs to the item, not to a building. Every workshop that makes the item reads the same number, and setting it in one workshop's panel sets it for all of them; every open panel showing the item follows the change at its next layout.

| place | retail | what the extension does |
|---|---|---|
| `ConsumeProduce::Execute` (`consumeproduce.cpp`, the workshop work) | a recipe is "depleted" when its category's stock reaches the category limit (`CitizenBehavior::professionResources[recipe.materialType]`): the active recipe, or any recipe in the random mode (`consumeResourceId < 0`) | when retail's test passed, a recipe whose product has reached its item limit counts as depleted too, in the same shape (the active recipe; any recipe in the random mode). What follows (the status icon, the idle branch) is retail's own |
| `PastureWork::Execute` (`pasturework.cpp`, the herder's work) | the herder slaughters an animal over the pasture's capacity only while the pasture's category (`PastureDescription::_type`, Food) is under its limit; at the limit it does nothing and the status icon shows the limit | when retail's test passed and every item the slaughter would yield (`AnimalDescription::_rawMaterials` of the animal branch A would take: `SlaughterCandidate` repeats retail's `FindOldestLivestock` pick and reads that animal's own description, the one `RemoveAnimalActions` gets; after a livestock switch the old herd is slaughtered first, so the pasture's selected `livestockType` is not it) is at its item limit, the slaughter is held: the herder still hauls the pasture's piles and grazes, and the status icon shows the limit |
| `RemoveAnimalActions` (`action_pf_roots_drop.cpp`, the slaughter's actions) | one `CreateRawMaterial` per item the animal yields, for a herder and a hunter alike | for a herder whose workplace is a pasture, an item at its item limit gets no `CreateRawMaterial`: a leather limit leaves the meat, and the leather is not made. A hunter's kill yields everything |
| `PastureProduce` (`pasturecomponent_states.cpp`, the herd's byproduct) | each production credit drops a pile of the byproduct (`LivestockDescription::_byproduct`: wool, milk, eggs, a mod's fertilizer ...) unless the byproduct's category is at its limit, when the credit is spent with no pile | the byproduct at its item limit counts as "full" too, in the same shape |
| `HerbalistWork::Execute` (`herbalistwork.cpp`, the gatherer's work) | the gatherer gathers only while its category (`HerbalistDescription::_resourceLimit`) is under its limit; it searches its radius for a grown plant of its list (`_herbs`, natural resource templates) | when every item of the list (each plant's `NaturalResourceDescription::_building`, the template the harvest creates) is at its item limit it gathers nothing, as at the category limit, and the status icon shows the limit; otherwise a plant whose item is at its limit is passed by and the search goes on |
| `CitizenBehavior::Update`, right after `CountResources` | the per-category stock, every tick | `CountTown` walks the same two storage lists and tallies only the limited items |

The only reader of a recipe's limit in the game is `ConsumeProduce::Execute` (the recipe's `materialType` has no other reader); a pasture's are `PastureWork::Execute` and `RemoveAnimalActions` (slaughter) and `PastureProduce` (byproduct); a gatherer's is `HerbalistWork::Execute`. Other producers (fields, orchards, hunters, fishers, foresters, mines, quarries) keep their category limits only.

## 2. Class C: the state, and where it lives

| | |
|---|---|
| row | `item_limits`, `Tier::Extension`, `Class::C`, default OFF, `formatVersion` 1 (`registry.cpp`) |
| record | the row's own record in the tail chunk; written whenever the row is active for the game, even with an empty list; zero bytes for a faithful game |
| codec | `Ext::ItemLimits::Codec()` (`src/ext/item_limits/save_state.cpp`), registered in `src/ext/state_codecs.cpp` |
| key | the item's `RawMaterialDescription` template path (`External::GetPath`), compared without case: the key the sibling rows use. Never an index. A path the game no longer has stays in the list and limits nothing |
| wire format v1 | little-endian: `u32 count`, then `count` x { `u32 length`, `u8 path[length]`, `i32 limit` } |
| decode | accepts exactly what encode writes; refuses a count the payload cannot hold, an empty, over-long (more than 1024 bytes) or control-character path, an item twice (without case), a limit outside 0..999,999,999, bytes left over, or a cut-short payload. A refused payload changes nothing |
| reset | `SaveTail::ResetAllState()` (new game, load) and `Ext::UnlockForGame` for the lock's owner; it also forgets every open panel's row (`PanelsForget`) |

## 3. The seams: no work per row while off

`BeginFrame` (once per frame, beside `ItemPriority::BeginFrame`) resolves the list when a path joins or leaves it or the lock epoch changes: it walks the loaded `ComponentDescription` objects (`ExternalFactory::FindNext`), keeps those whose `RawMaterialDescription` path is listed, and builds `g_items`, a short flat array of { item template, limit, entry in the list }, and `g_counts`, one count per listed path. Several templates can carry one `RawMaterialDescription` (a mod's variant of an item): they are counted together, into their path's count, and each stops when that total reaches the limit, so the town never holds more than the number the panel shows. A path that stays limited keeps its last count across a re-resolve, so a limit edited between the count and a workshop's test never reads an empty town. When only a value changed (the spinner), `RefreshLimits` copies the new values into `g_items` without the walk: the walk visits every loaded resource, and a held spinner changes the value every few frames.

`g_on` is the row's locked value AND a non-empty `g_items`. `CountTown` and the `Execute` test each test `g_on` once; with the row off, or no limit set, each pays one load and a not-taken branch. `Execute` consults the extension only when retail's own test said "not depleted".

## 4. The panel row

A workshop's panel (`ConsumeProduceUI`) gets one more row, under its Product row: a check box, "`<product>` limit" and a spinner.

| | |
|---|---|
| where | right under the Product row (the parent of the panel's `comboProduct`), at the panel's foot: a vertical column (`ItemLimitsPanel.rsc:ribbonItemLimitsColumn`) takes that row's slot in its holder and holds the Product row, then ours, so the row gets its own line and the panel grows by its height. Under the category limit's row it overlapped other controls: that row sits in the tab strip beside the Inventory tab, the page below does not follow its height, and a sawmill's second category row has its own fixed place. A panel without a Product row gets one row per product, below. When neither place is found the rows go at the end of the category limit row's holder (logged). A panel with no `editLimit` gets no row (logged, `[ItemLimits]`) |
| the row | `Dialog/ItemLimitsPanel.rsc:ribbonItemLimitsRow` (our own: the check box and the text), plus a copy of the category spinner made from its own description, so it looks like the one above it. Range 0..999,999, or ext.high_limits' range when that row is on |
| follows | the product the panel's combo selects (the recipe name, as the combo shows it); the item's limit when it has one, else the category's current limit as a starting value |
| check box | checked = the item has its own limit (the spinner's value); unchecking removes it |
| spinner | a change sets the item's limit and checks the box |
| seams | `consumeproduceui.cpp`: `OnCreated` (adds the row), `OnUpdateLayout` (follows the combo and the other panels), `OnElementEvent` (the row's own ids, 0x7E10 / 0x7E11); each a test of the row's locked value |

**A workshop panel without a Product combo** gets one row per product its recipes make (the first recipe of each product, at most 16; ids 0x7E10 + 2i and the next), named by the item's own name. Without a combo the workshop runs every recipe (retail's random mode), so a single row following recipe 0 left the other products unlimitable. Two shapes, both seen in MegaMod X:

| panel | example | who builds the rows |
|---|---|---|
| `ConsumeProduceUI` with no `comboProduct` (it has `progressRemaining`) | a clay or sand pit (`Template\TinyClayPit.rsc`, ...: one recipe, no inputs; the quarry shape) | `ConsumeProduceUI::OnCreated`, as above |
| no `ConsumeProduceUI` at all: `WorkPlaceUI` + `ResourceLimitUI` | a mod's butcher (`Template\Butcher.rsc`: beef cuts, sausages, tallow) | the panel's first `ResourceLimitUI`, at its first `OnUpdateLayout` |

The rows sit in a column under the panel's button bar (`buttonBar`, the strip in the page that holds the category limit): a vertical column takes the bar's slot in its page's column (`pageMain`) and holds the bar, then the rows. The category limit's own group is no place for them: on a pit it is shared with the remaining-stock bar, and the old single row overlapped both. The `ResourceLimitUI` path decides at the first layout because `UI::DialogInstance::AddController` runs each controller's `OnCreated` in turn, so at `ResourceLimitUI::OnCreated` a later `ConsumeProduceUI` is not there yet. It builds rows only when the panel has no `ConsumeProduceUI`, the controller is the panel's first `ResourceLimitUI` (a sawmill has two) and the entity has a `ConsumeProduceComponent` (not the Town Hall's Limits tab, not a pasture). Seams in `resourcelimitui.cpp`: `OnCreated` (forgets the controller's rows), `OnUpdateLayout`, `OnElementEvent`.

The sim side needs nothing new: these are ConsumeProduce workshops, so `ConsumeProduce::Execute`'s test covers them. In the random mode a workshop stops when any of its products is at its item limit, as retail does at a category limit; with ext.recipe_skip on it skips the limited recipe instead (section 6), which is what a butcher limited on tallow alone wants.

**The gatherer panel** (`HerbalistComponent`: the gatherer's hut, a mod's florist or mushroom shelter) has no `ConsumeProduceUI` either and takes the same path as a workshop panel without a combo: its first `ResourceLimitUI` builds one row per item the plants of its list give (`HerbItem`: each plant's `NaturalResourceDescription::_building`, once per item), named by the item, under the button bar. A list with more items than rows (16; a MegaMod X gatherer's 147 plants give 87 items) gets an item picker instead: a combo (`ItemLimitsPanel.rsc:comboItemLimitsItem`, made from the options window's combo) listing every item by name, above one row that follows the item it selects (the combo's id 0x7E30, its `SelectionChanged` re-points the row).

**The pasture panel** (`PastureUI`, `pasture_rows.cpp`) gets one such row per item its livestock gives: the slaughter yield, then the byproduct (at most 8 rows). The rows sit in a column under the animal-count slider: a vertical column takes the slider's slot and holds the slider, then the rows' column, so the panel grows by their height. When the combo picks another livestock, a new column of rows replaces the old one (an empty column when it gives nothing). Seams in `pastureui.cpp`: `OnCreated`, `OnUpdateLayout`, `OnElementEvent` (ids 0x7E40..0x7E4F: row i's check box 0x7E40 + 2i, its spinner the next). The category limit spinner (`editLimit`, from the pasture template's own panel) is not found yet at `PastureUI::OnCreated`, so the rows are built at the panel's first layout, as the workshop path does; a pasture panel with no category limit spinner even then gets no rows (logged). The rows' shared parts with the workshop row are `panel_common.{h,cpp}`.

No toolbar page: the limits are set where the item is made. A list of every limit in the extension window is not built.

## 5. Where

| file | change |
|---|---|
| `src/ext/item_limits/item_limits.{h,cpp}` (new) | the list, the resolve, `g_on`, `g_items`, `BeginCount` / `Tally` / `AtLimit`. Standard C++ |
| `src/ext/item_limits/item_limits_town.cpp` (new) | the factory walk, `TemplatePath`, `BeginFrame`, `CountTownSlow`, `RecipeAtLimitSlow`, `SlaughterCandidate`, `SlaughterAtLimitSlow`, `SlaughterSkipsYieldSlow`, `HerbItem`, `HerbAtLimitSlow`, `HerbsAtLimitSlow` |
| `src/ext/item_limits/panel_row.cpp`, `pasture_rows.cpp`, `panel_common.{h,cpp}` (new) | the workshop and gatherer panels' rows and item picker, the pasture panel's rows, their shared parts |
| `src/ext/item_limits/save_state.cpp` (new), `src/ext/state_codecs.cpp` | the codec and its registration |
| `src/ext/registry.{h,cpp}` | the row |
| `src/game/Work/consumeproduce.cpp`, `src/game/Entity/citizenbehavior.cpp`, `src/game/Game/gameupdateloop.cpp`, `src/game/Dialog/consumeproduceui.cpp`, `src/game/Dialog/resourcelimitui.cpp`, `src/game/Work/pasturework.cpp`, `src/game/Work/herbalistwork.cpp`, `src/game/Action/action_pf_roots_drop.cpp`, `src/game/Component/pasturecomponent_states.cpp`, `src/game/Dialog/pastureui.cpp` | the seams |
| `src/ext/ui/Dialog/ItemLimitsPanel.rsc` (new), `ExtensionsStringTable.rsc`, `ExtensionsSprites.rsc`, `CMakeLists.txt`, `src/ext/resources/resources.cpp` | the row's resource, its texts (`ItemLimits*`), the embed list and the provider prefix |
| `tools/ext_icons/itemlimits.py`, `Entry_item_limits.png`, `ext_sprites_data.h` | the row icon |
| `tests/unit/ext_item_limits_test.cpp` (new), `ext_rsc_text_test.cpp`, `tests/unit/sources.cmake` | the tests |

## 6. Behaviour notes

* **Off is retail.** Under the faithful profile the row is forced off; with the row off every seam is a not-taken branch and the panel has no row.
* **Random mode.** A workshop whose product choice is random stops when any of its products is at its item limit, as retail stops it when any product's category is at its limit.
* **With ext.recipe_skip.** In the random mode recipe_skip skips a recipe whose category is at its limit and stops the workshop only when every recipe is. A recipe whose product is at its item limit is treated the same way there (`RecipeSkip::Read` marks it at its limit), and `Execute` does not add the item-limit stop after recipe_skip's answer, so the two together skip limited recipes and stop only when every recipe is at one limit or the other.
* **The row cannot be switched on in a running town** (DESIGN.md: the lock is on activation).

## 7. Verified and not verified

* **Built and tested on Windows only** (MinGW GCC 16.2): `tools/build.sh unit` 2408 tests, 16 skipped, one failure outside this row (`sysmisc_failed_datetime_conversion_formats_empty`, a date-format test of files this change does not touch); the debug and release exes build; `tools/acceptance/acceptance.sh` 0 lines off target.
* **Unit tests:** the registry row; the list (one limit per item, case, clamping, clear); the codec round trip with awkward names, the empty record, every kind of damaged payload refused with the old list kept, an unknown version; the state travels in the tail; the faithful profile writes no record; the reset follows the lock owner; the resolve, the count, the limit test, two templates of one path counted together, the count kept across a re-resolve, the reset forgetting the panel rows, a value change that needs no walk, a zero limit, a missing path; `g_on` and the panel flag follow the row and the list; the embedded `.rsc` parses. The producers' tests the seams call, pure in `item_limits.h` / `item_limits.cpp`: `AllAtLimit` (a slaughter held only when every yield is at its limit, not with one under, an unlimited or null yield, or no yield; a gatherer stopped only when every plant's item is, a plant with no item keeping it going), `SkipsYield` (only a herder's limited yield, never a hunter's) and `FirstOfEach` (each item once, null items dropped, at most the row count).
* **Run in a game** (Windows, release exe, a MegaMod X town, checked by hand): the panel row sits under the Product row with no overlap on the workshop panels where the earlier placement overlapped; a limit is set from the spinner.
* **Run with ext.recipe_skip** (Windows, debug exe, a MegaMod X town, a `Template\Butcher.rsc` placed by the harness with temporary diagnostics, staffed by `BANISHED_AUTOSTAFF`, its input refilled): with the tallow limit 0 the butcher stayed in the random mode and picked beef cuts 35 times and sausages 30 times, tallow never, and the town's stock of both grew; without the limit it picked tallow 15 times in 60 and made 12. So an item limit skips its own recipe and does not stop the others.
* **Workshop panels without a combo** (same setup): a Tiny Clay Pit shows "Clay limit" under the button bar; a Cow Butcher shows Beef Cuts, Sausages and Tallow rows.
* **Pasture panel and slaughter** (Windows, debug exe, a MegaMod X town, the harness's demo pasture typed as `LivestockRKDuck` and filled by temporary diagnostics, staffed by `BANISHED_AUTOSTAFF`): the panel shows Duck Meat, Feathers, Fertilizer and Eggs rows (before the first-layout build it showed none: the spinner lookup at `OnCreated` failed). With the feather limit 0 and the capacity cut to 1, each of three slaughters created duck meat and fertilizer and skipped the feathers.
* **Gatherer panel and gathering** (Windows, debug exe, a MegaMod X town, the harness's gatherer hut staffed by `BANISHED_AUTOSTAFF`, temporary diagnostics): with 16 rows the panel showed Onions .. Pear (Flowers among them) and the 87-item list was cut, so the picker replaced the rows: the panel shows the combo and one row, and selecting Flowers re-pointed the row to "Flowers limit". With the flowers limit 0 the gatherer passed by a flower plant and went on gathering onions, flax, oats, maize, roots and others. The combo's drop-down list with 87 items was not opened by hand.
* **Livestock switch** (same setup, temporary diagnostics): a duck pasture filled to 9 ducks was switched to `LivestockBLRKCow` with the capacity at 1. With the duck yields (duck meat, feathers, fertilizer) at limit 0 the slaughter was held while ducks remained (the candidate was the ducks' own description). With those cleared and the cow yields (beef, leather, fertilizer) at 0 instead, a duck was slaughtered: duck meat and feathers made, fertilizer (shared with the cow) skipped. The pasture panel shows the selected livestock's items only, so an old herd's other items are limited from another panel until it is gone.
* **Gate** (`tools/build.sh run`, debug, `/profile:faithful`, a game dir with only the retail packages): autostart fired, no crash or hang log.
* **Not run:** the resave check (no `Gate.sav` fixture on the author's machine), the stress run, Linux and macOS.
* To try it: turn **Item limits** on in the Extensions menu, start a town, open a workshop (a blacksmith), check the box under the Product row and set a number. The log lines to read are `[ItemLimits]` and `[ExtSave]` on stderr.
