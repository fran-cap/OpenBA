# TRADERREQUEST hand-in

`ext.trader_request`: a trading post can ask for one trader instead of taking whoever turns up; a requested trader's goods cost double. One class C toggle (locked for the game, per-town state recorded in the save), on by default, `formatVersion` 1. It follows the shape `ext.item_limits` set: the pure list, the save-state codec, a row added to a retail panel.

## 1. What it does

Retail rolls which trader docks, uniformly over the `TradeDescription`'s merchants, when the boat arrives (`TradeComponent::BuildMerchantManifest`, 0x14005b580). The player can order goods from a trader (the Orders tab, at 1.25x) but cannot choose who comes.

With this row, each trading post can hold one **request**: a trader and a mode.

| | |
|---|---|
| trader | one of the post's `TradeDescription` merchants |
| mode | **Next visit** (the arrival uses the request up) or **Every visit** (until the player sets the post back to "Any trader") |
| cost | every line of a requested visit's manifest is priced at 2x what retail would ask for that visit (`kPriceFactor`), after retail's own 1.25x for an ordered good. Retail's floor of 1 holds |
| not changed | what the trader pays for the town's goods (`TradeComponent::GetResourcePrice`). Trade is barter: doubling both sides would cancel |
| not changed | when a boat comes (the tier's delay), how many lines the manifest has, the stock per line |

A post with no request gets retail's roll and retail's prices. Retail's roll is still made at a post with a request (the random stream advances as in retail) and its result replaced.

## 2. The seams

| place | retail | what the extension does |
|---|---|---|
| `BuildMerchantManifest` (`tradecomponent_dock.cpp`), after the roll | `tradeId` = the rolled merchant | `Arriving` replaces the rolled index with the requested trader's when the post has a request whose trader the description lists; a "next visit" request is removed |
| the same function, per manifest line | base price, x1.25 for an ordered good, at least 1 | `RequestedPrice` (x2, saturating) when the visit was requested; the flag is read once, outside the roll loop |
| `TradeComponent::Teardown` (`tradecomponent_states.cpp`, the post is demolished) | | the post's request is removed |
| `TradeUI::OnCreated` / `OnUpdateLayout` / `OnElementEvent` (`tradeui.cpp`) | | the Orders tab's row (section 4) |

Each seam tests the row's locked value (`Ext::Ext().trader_request`) once. The doubled price is written into the manifest lines retail already serializes (`TradeManifestLine::price`), so a requested visit in progress survives a save and a load with no state of ours, and auto-trade and the Trade tab read it as they read any price.

## 3. Class C: the state, and where it lives

| | |
|---|---|
| row | `trader_request`, `Tier::Extension`, `Class::C`, default ON (the user, 2026-11-11), `formatVersion` 1 (`registry.cpp`) |
| record | the row's own record in the tail chunk; written whenever the row is active for the game, even with an empty list; zero bytes for a faithful game |
| codec | `Ext::TraderRequest::Codec()` (`src/ext/trader_request/save_state.cpp`), registered in `src/ext/state_codecs.cpp` |
| key | the post's entity id (`ComponentEntity::_entityId`, stable across a save and a load, as `ext.tool_names` names a citizen) and the trader's name key (`TradeMerchant::name`). Never the merchant index: mods change the list between sessions. A trader the game no longer has is never arriving: the post gets retail's roll, the request stays, and the row shows "Any trader" |
| wire format v1 | little-endian: `u32 count`, then `count` x { `i32 post`, `u32 mode` (1 next visit, 2 every visit), `u32 length`, `u8 trader[length]` } |
| decode | accepts exactly what encode writes; refuses a count the payload cannot hold or above 4096, a negative post, a post twice, an unknown mode, an empty, over-long (more than 255 bytes) or control-character trader, bytes left over, or a cut-short payload. A refused payload changes nothing |
| reset | `SaveTail::ResetAllState()` (new game, load) and `Ext::UnlockForGame` for the lock's owner; it also forgets every open window's row (`PanelsForget`) |

## 4. The Orders tab row

| | |
|---|---|
| where | the last line of the trading post window's Orders tab. A vertical column (`TraderRequestPanel.rsc:ribbonTraderRequestColumn`) takes the slot of the order combo's parent and holds that parent, then our row. In the retail window that parent is the tab's page (the order combo and the goods table), so the row sits under the goods table and the window grows by its height. A window with no `comboOrder` gets no row (logged, `[TraderRequest]`) |
| the row | `Dialog/TraderRequestPanel.rsc:ribbonTraderRequestRow` (our own: the text "Request trader (price mult)", with a tooltip; left pad 8, and the column's bottom pad 6, so it lines up with the goods table and clears the page's border), plus two copies of the order combo made from its own description, so they look like it in the retail window or a mod's |
| trader combo | "Any trader (1x)", then the description's traders as `ComboLabel` writes them: the display name without a trailing "Merchant" / "Trader", then the multiple ("Food (2x)"). Picking a trader makes the request with the mode the second combo shows; "Any trader" removes it |
| mode combo | "Next visit" / "Every visit". Changing it changes a request that exists; with "Any trader" it only sets what the next pick will use |
| follows | the post's request at every layout, so a "next visit" request an arrival used up reads "Any trader" again while the window is open |
| enabled | always: unlike retail's order combo it does not need a docked trader. A request made while a trader is docked applies from the next boat |
| ids | 0x7E20 (trader), 0x7E21 (mode), `SelectionChanged` |

## 5. Where

| file | change |
|---|---|
| `src/ext/trader_request/trader_request.{h,cpp}` (new) | the list, `TakeArrival`, `RequestedPrice`. Standard C++ |
| `src/ext/trader_request/trader_request_town.cpp` (new) | `ArrivingSlow`, `PostRemovedSlow` |
| `src/ext/trader_request/panel_row.cpp` (new) | the Orders tab row |
| `src/ext/trader_request/save_state.cpp` (new), `src/ext/state_codecs.cpp` | the codec and its registration |
| `src/ext/registry.{h,cpp}` | the row |
| `src/game/Component/tradecomponent_dock.cpp`, `tradecomponent_states.cpp`, `src/game/Dialog/tradeui.cpp` | the seams |
| `src/ext/ui/Dialog/TraderRequestPanel.rsc` (new), `ExtensionsStringTable.rsc`, `ExtensionsSprites.rsc`, `CMakeLists.txt`, `src/ext/resources/resources.cpp` | the row's resource, its texts (`TraderRequest*`), the embed list and the provider prefix |
| `tools/ext_icons/traderrequest.py`, `Entry_trader_request.png`, `ext_sprites_data.h` | the row icon |
| `tests/unit/ext_trader_request_test.cpp` (new), `ext_rsc_text_test.cpp`, `ext_registry_test.cpp`, `tests/unit/sources.cmake` | the tests |

## 6. Behaviour notes

* **Off is retail.** Under the faithful profile the row is forced off; with the row off every seam is a not-taken branch and the window has no row.
* **With an order.** A requested trader that also carries a custom order prices the ordered goods at 1.25 x 2 of the base value.
* **Auto-trade** buys from a requested visit at the doubled prices, as it reads the manifest.
* **A trader docked or on its way** when the request is made is not replaced: the request applies to the next arrival's roll.
* **The row cannot be switched on in a running town** (DESIGN.md: the lock is on activation).

## 7. Verified and not verified

* **Built and tested on Windows only** (MinGW GCC 16.2): `tools/build.sh unit` 2362 tests, 0 failures; the debug exe builds; `tools/acceptance/acceptance.sh` 0 lines off target; the gate (`tools/build.sh run`, faithful profile, fixes and extensions off) reaches a running game with an empty crash log; `tools/resave_gate/resave_gate.sh Gate` PASS (resave == golden).
* **Unit tests:** the registry row; one request per post; the arrival (the requested trader, "every visit" kept, "next visit" used up, another post's request ignored, an unknown trader leaving retail's roll and the request); the doubled price with its floor and its saturation; the codec round trip, the empty record, every kind of damaged payload refused with the old list kept, an unknown version; the state travels in the tail; the faithful profile writes no record; the reset forgets the rows; the embedded `.rsc` parses.
* **Run in a game** (Windows, debug exe, per-entry profile with the row at its default ON, a new town with the demo trading post, `BANISHED_PANEL_HOLD=TradeComponent BANISHED_PANEL_HOLD_TAB=3`): the row is built on the Orders tab with no `[TraderRequest]` warning, the crash log is empty, and a UI screenshot shows the text and the two combos ("Any trader", "Next visit") under the goods table.
* **Hand test** (the user, 2026-11-11, Windows debug exe, a larger loaded save): a trader picked in the row arrives as requested and the feature works; the wording and the row's layout (aligned with the goods table, clear of the page's lower border) accepted.
* **Not run:** a modded trader list, the perf, release and Vulkan builds, Linux and macOS. No resave golden of its own (`Gate<Id>.resaved.md5`).
* To try it: start a town, build a trading post, open its Orders tab, pick a trader in "Request trader (price mult)". The log lines to read are `[TraderRequest]` and `[ExtSave]` on stderr.
