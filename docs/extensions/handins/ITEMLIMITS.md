# ITEMLIMITS hand-in

`ext.item_limits`: a limit for one item beside its category's, set in the panel of a workshop that makes it. One class C toggle (locked for the game, per-town state recorded in the save), off by default, `formatVersion` 1. It follows the shape `ext.restricted_foods` and `ext.item_priority` set: the save-state codec, the template-path key, the flat array the seams scan.

## 1. What it does

Retail limits production by category only (the Town Hall's Limits: Tools, Clothing, Food, Alcohol, ...). A mod's Steel tools and Iron tools share the Tools limit, so retail cannot say "50 steel tools, then stop". With this row, a workshop also stops making an item once the town holds as many of it as the item's own limit, even while the category is under its limit.

**One limit per item, town-wide** (the user, 2026-11-03). The limit belongs to the item, not to a building. Every workshop that makes the item reads the same number, and setting it in one workshop's panel sets it for all of them; every open panel showing the item follows the change at its next layout.

| place | retail | what the extension does |
|---|---|---|
| `ConsumeProduce::Execute` (`consumeproduce.cpp`, the workshop work) | a recipe is "depleted" when its category's stock reaches the category limit (`CitizenBehavior::professionResources[recipe.materialType]`): the active recipe, or any recipe in the random mode (`consumeResourceId < 0`) | when retail's test passed, a recipe whose product has reached its item limit counts as depleted too, in the same shape (the active recipe; any recipe in the random mode). What follows (the status icon, the idle branch) is retail's own |
| `CitizenBehavior::Update`, right after `CountResources` | the per-category stock, every tick | `CountTown` walks the same two storage lists and tallies only the limited items |

The only reader of a recipe's limit in the game is `Execute` (the recipe's `materialType` has no other reader), so that is the one sim seam. Other producers (fields, orchards, hunters, fishers, foresters, gatherers, pastures, mines, quarries) are not workshops and keep their category limits only.

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
| where | right under the Product row (the parent of the panel's `comboProduct`), at the panel's foot: a vertical column (`ItemLimitsPanel.rsc:ribbonItemLimitsColumn`) takes that row's slot in its holder and holds the Product row, then ours, so the row gets its own line and the panel grows by its height. Under the category limit's row it overlapped other controls: that row sits in the tab strip beside the Inventory tab, the page below does not follow its height, and a sawmill's second category row has its own fixed place. Without a Product row the row goes at the end of the category limit row's holder (logged). A panel with no `editLimit` gets no row (logged, `[ItemLimits]`) |
| the row | `Dialog/ItemLimitsPanel.rsc:ribbonItemLimitsRow` (our own: the check box and the text), plus a copy of the category spinner made from its own description, so it looks like the one above it. Range 0..999,999, or ext.high_limits' range when that row is on |
| follows | the product the panel's combo selects (the recipe name, as the combo shows it); the item's limit when it has one, else the category's current limit as a starting value |
| check box | checked = the item has its own limit (the spinner's value); unchecking removes it |
| spinner | a change sets the item's limit and checks the box |
| seams | `consumeproduceui.cpp`: `OnCreated` (adds the row), `OnUpdateLayout` (follows the combo and the other panels), `OnElementEvent` (the row's own ids, 0x7E10 / 0x7E11); each a test of the row's locked value |

No toolbar page: the limits are set where the item is made. A list of every limit in the extension window is not built.

## 5. Where

| file | change |
|---|---|
| `src/ext/item_limits/item_limits.{h,cpp}` (new) | the list, the resolve, `g_on`, `g_items`, `BeginCount` / `Tally` / `AtLimit`. Standard C++ |
| `src/ext/item_limits/item_limits_town.cpp` (new) | the factory walk, `TemplatePath`, `BeginFrame`, `CountTownSlow`, `RecipeAtLimitSlow` |
| `src/ext/item_limits/panel_row.cpp` (new) | the panel row |
| `src/ext/item_limits/save_state.cpp` (new), `src/ext/state_codecs.cpp` | the codec and its registration |
| `src/ext/registry.{h,cpp}` | the row |
| `src/game/Work/consumeproduce.cpp`, `src/game/Entity/citizenbehavior.cpp`, `src/game/Game/gameupdateloop.cpp`, `src/game/Dialog/consumeproduceui.cpp` | the seams |
| `src/ext/ui/Dialog/ItemLimitsPanel.rsc` (new), `ExtensionsStringTable.rsc`, `ExtensionsSprites.rsc`, `CMakeLists.txt`, `src/ext/resources/resources.cpp` | the row's resource, its texts (`ItemLimits*`), the embed list and the provider prefix |
| `tools/ext_icons/itemlimits.py`, `Entry_item_limits.png`, `ext_sprites_data.h` | the row icon |
| `tests/unit/ext_item_limits_test.cpp` (new), `ext_rsc_text_test.cpp`, `tests/unit/sources.cmake` | the tests |

## 6. Behaviour notes

* **Off is retail.** Under the faithful profile the row is forced off; with the row off every seam is a not-taken branch and the panel has no row.
* **Random mode.** A workshop whose product choice is random stops when any of its products is at its item limit, as retail stops it when any product's category is at its limit.
* **With ext.recipe_skip.** In the random mode recipe_skip skips a recipe whose category is at its limit and stops the workshop only when every recipe is. A recipe whose product is at its item limit is treated the same way there (`RecipeSkip::Read` marks it at its limit), and `Execute` does not add the item-limit stop after recipe_skip's answer, so the two together skip limited recipes and stop only when every recipe is at one limit or the other.
* **The row cannot be switched on in a running town** (DESIGN.md: the lock is on activation).

## 7. Verified and not verified

* **Built and tested on Windows only** (MinGW GCC 16.2): `tools/build.sh unit` 2303 tests, 16 skipped, one failure outside this row (`sysmisc_failed_datetime_conversion_formats_empty`, a date-format test of files this change does not touch); the debug and release exes build; `tools/acceptance/acceptance.sh` 0 lines off target.
* **Unit tests:** the registry row; the list (one limit per item, case, clamping, clear); the codec round trip with awkward names, the empty record, every kind of damaged payload refused with the old list kept, an unknown version; the state travels in the tail; the faithful profile writes no record; the reset follows the lock owner; the resolve, the count, the limit test, two templates of one path counted together, the count kept across a re-resolve, the reset forgetting the panel rows, a value change that needs no walk, a zero limit, a missing path; `g_on` and the panel flag follow the row and the list; the embedded `.rsc` parses.
* **Run in a game** (Windows, release exe, a MegaMod X town, checked by hand): the panel row sits under the Product row with no overlap on the workshop panels where the earlier placement overlapped; a limit is set from the spinner.
* **Not run:** the gate, the resave check, the stress run, Linux and macOS; ext.recipe_skip and this row on together in a game (unit-level only: the `Read` change and the `Execute` branch).
* To try it: turn **Item limits** on in the Extensions menu, start a town, open a workshop (a blacksmith), check the box under the Product row and set a number. The log lines to read are `[ItemLimits]` and `[ExtSave]` on stderr.
