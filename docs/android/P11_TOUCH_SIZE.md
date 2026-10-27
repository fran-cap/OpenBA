# Android P11: every control the size of a finger (2026-10-26)

Session log of `docs/runtime_port/NEXT_SESSION.md` §2.9 item 2 ("touch-size pass on every control"),
under `ext.phone_menu` (same row, same switch, same walk as P9's 4x scroll bars and 2x close buttons,
`docs/android/P9_DIALOGS.md`). The facts that outlive the session are in
`docs/extensions/handins/PHONEMENU.md` §2 ("Every control the size of the close button") and §3 (the
slider seam). Code: `src/ext/phone_menu/touch_size.{h,cpp}`, `size_math.{h,cpp}`, one call in
`touch_fit.cpp` (`TouchFrame`'s per-dialog loop), three lines in `phone_menu.cpp` (`Place`, `Sync`), the
engine seam `UI::SliderElement::_extSpriteScale` (`src/engine/ui/SliderElement.h`, `sliderelement.cpp`).
Unit tests: `tests/unit/ext_phone_size_test.cpp`.

## 1. The rule

The reference is the P9 close button: StandardDialog.rsc's 16-unit cross at twice its size, padded 2 on
each side = **36 layout units** (`kTouchSide`), 85 device pixels on the S25 at the owner's UI 42 (measured,
the house panel's [x]). Every clickable control is brought to that height (icon buttons and check boxes to
that square), with the row on only.

How (read off the engine): a `ButtonElement` (and `CheckElement` / `ComboElement`, its subclasses) is the
union of its content's padded size and its state background's, floored by its `_minWidth` /
`_minHeight`; its content is laid out over its rectangle by the content's OWN alignment, TopLeft for most
of the game's labels and icons. Raising the button's minimum would leave the label in the top-left corner,
so the walk grows the CONTENT's padding instead (half above, half below; for icon buttons also left /
right), and an icon of 16 units or less is first drawn at twice its sprite (`_width` / `_height`, the P9
close button's rule: a whole multiple). The padding is the description's plus the extra; with the row off
the description's again (`Element::_description`).

## 2. What was touched, in the owner's order (one commit per control type)

| # | Control | What the walk does | Commit |
|---|---|---|---|
| 1 | Title-bar buttons (pin, centre, the event log's notify checks; anything inside `StandardDialog.rsc:titleBar`) | icon 2x, padded to 36 x 36 (a check's tick the same) | `ext.phone_menu: touch size walk; title-bar and icon buttons ...` |
| 2 | `>` on each citizen row (house, work place, town hall; `buttonGoto`), the event log's row and scroll buttons, the house panel's residence button | icon 2x, padded to 36 x 36 | same commit (one control type: icon buttons) |
| 3 | Ok / Accept / Cancel / Apply / Yes / No / Follow, the key-binding buttons, the Extensions dialog's buttons | label padded to 36 high, retail width | `... text buttons` |
| 4 | Options sliders (and every other slider) | thumb and track sprites at 2x through the engine seam `SliderElement::_extSpriteScale` (no size field exists on the instance) | `... sliders` |
| 4 | Options buttons (Cancel / Apply / Ok, the tab row) | as 3 and 7 | |
| 5 | Check boxes | unticked icon and tick 2x, padded to 36 x 36 | `... check boxes` |
| 5 | Drop-downs and their list rows | combo label padded to 36 high; each row of the list's ribbon padded to 36 high (the list hit-tests the ribbon's extents, so a padded row is picked and highlighted whole) | `... drop-downs and their rows` |
| 5 | Tab buttons | label padded to 36 high | `... tab buttons` |
| 5 | Spinner up / down | arrows 2x, each button 36 wide and 18 high, the pair 36 | `... spinner buttons` |

**Left as retail, and why:**
* Buttons already 36 or more in their description's minimum: the in-game toolbar (40-unit buttons), the
  main / pause menu squares (phone_menu's own rows).
* A button whose minimum is not its description's (sized at run time by its owner).
* Every dialog ext.touch_mode places (its toolbar, sub-toolbars, event notices and the camera cluster):
  that row sizes them for a finger itself. `TouchSeen` (called from `phone_menu.cpp` `Place`) records which
  dialogs the place hook saw and which touch_mode claimed; a dialog is walked from its second frame only,
  so a newly opened touch_mode dialog is never touched. (An intermediate build that walked them doubled
  the cluster's + / - glyphs: `after_game.png` of the third build; fixed.)
* **Controls on a tab's page that does not scroll** (the Options Video page; the Audio page's table).
  A tab element is as tall as its tallest page, shown or not (`TabElement::ComputeSize`). Measured on the
  S25 at UI 42 with the Video page's combos and checks padded: the Options dialog went from the fitted
  UI scale 44 % to **57 %** (`[PhoneMenu] UI ...: laid out at UI scale 0.57`), so the whole dialog was
  drawn smaller and the padded combos ended up no larger on screen than before, everything else smaller
  (`after_options_game.png`, `after_options_video.png`: the Game page also showed empty space under its
  318-unit list). So controls on a non-scrolling tab page keep retail's size; controls in a scrolling list
  on any page grow (the Game, Input and Enhanced pages are scrolling lists). Sliders grow everywhere (the
  Audio page's four sliders stay well under the Video page's height).
* Edit fields, number fields, the scroll bars (P9's 4x rule), the toolbar tooltips: not buttons.
* Combo drop-down lists themselves (ComboDialog) are not scaled; only their rows are padded.

## 3. Measured on the S25 (UI 42, render scale Full, 2340 x 1080; device pixels, about +-3)

Before = this branch's code with the walk disabled (`build/p11_before.apk`, identical to P9 for every
dialog except the camera cluster, see §5); after = the final apk. Seeded new game (`BANISHED_SEED=31337`).

| Target | Before | After | Note |
|---|---|---|---|
| Close [x] (the reference) | 85 | 85 | unchanged (P9) |
| Pin / centre (house panel title bar) | 43 x 45 | 85 x 85 | |
| Residence button (house panel, top right) | 48 | 82 | |
| `>` on a citizen row (house panel) | 45 x 47 | 86 x 85 | rows 50 -> 86 high, the list scrolls after 3 rows |
| Occupants / Inventory tabs | 50 | 60 | already near 36 units |
| Options Ok / Apply / Cancel | 46 | 66 | Options shown at 45 % in game (its own fit) |
| Options check box (Game page) | 57 | 90 | |
| Options slider thumb (Game page) | 57 x 28 | 115 x 60 | track 32 -> 70 high |
| Autosave drop-down (Game page) | 54 | 70 | its list rows about 84 |
| Options Video page combos | 66 | 66 | kept retail (§2) |
| Professions spinner arrows | 22 each | 42 each (pair 84) | |
| Professions `>` | 40 | 80 | |
| Event log notify checks (title bar) | 44 | 82 | |

Screenshots (session scratchpad `p11size/`, not in the tree):
* before: `before_options.png`, `before_options_video.png`, `before_options_audio.png` (front-end Options),
  `before_game.png`, `before_house.png`, `before_eventlog.png`, `before_professions.png`;
* after (final apk): `final_game.png`, `final_house.png`, `final_options.png` (pause-menu Options);
* after (earlier builds of this branch, same rules for the dialogs shown): `after_house.png`,
  `after_eventlog.png`, `after_professions.png`, `after2_options_game.png`, `after2_options_sliders.png`,
  `after2_slider_drag.png` (drag to the end: clamps at 200), `after2_slider_offband.png` (a drag that left
  the 32-unit band keeps its value, 66: fix.slider_drag_clamp on), `after2_combo_list.png` (drop-down rows),
  `after2_extensions.png` (Fixes and Extensions), `after3_tool1.png` (town statistics);
* the rejected Video-page enlargement: `after_options_game.png`, `after_options_video.png` (fit 57 %);
* crops with a pixel ruler: `m_before_house*.png`, `m_after_house*.png`, `m_before_opt.png`,
  `m_after_opt.png`, `m_before_chk.png`, `m_after_chk.png`;
* Windows, row off (the faithful desktop look, `tools/build.sh run-menu` + `BANISHED_AUTOMENU_BUTTON=4` on
  the functional game dir, Enhanced tab): `win_rowoff_options.png`: retail's slider, buttons and bars.

## 4. The tall-dialog fit (for the coordinator; the scale-down code was not changed)

From `standalone_error.log` of the final run: the pause-menu Options dialog is fitted at **0.44, then
0.45** (the tab row and the Ok row are taller: about +1 % against P9's 0.44 at the same UI 42). The
house panel, the event log and the Professions dialog fit without a scale-down. The New Game dialog (the
autostart opens it for a moment) logged a fit at **0.47**; its value before this pass was not captured.
While the fit is in force ext.touch_mode re-derives its cluster from the fitted layout size (`[TouchMode]
1029x475: cluster cell 52` then `1053x486: cell 53` while Options was open): the cluster changes size behind
a tall dialog. That is the P9 per-interface scale-down (the other lane), not this pass.

## 5. Rulings owed

1. **Options Video / Audio pages kept at retail size** (§2). The alternative (enlarge them) costs the whole
   dialog: 44 % -> 57 % fit at UI 42. A scrolling Video page would need new elements in retail's layout.
2. **The stretched slider art**: the 2x thumb is the game's 16-unit sprite drawn at 32 (a whole multiple,
   crisp but blocky), and its pointed tip reaches below the track (`after2_options_sliders.png`). Not
   redrawn (no new art).
3. **Table rows with a 36-unit `>`**: the other cells of the row (name, gender, age, profession) are laid
   out at the row's height, so their light backgrounds are 36 units tall too; a house list shows 3 rows
   instead of 5 before scrolling (`final_house.png`).
4. **Spinner arrows at 18 units each** (half the touch side, the pair 36): a full 36 each would make every
   Professions row 72 units and the dialog taller than the screen.
5. **Before screenshots of the camera cluster are not P9's**: the "before" apk walked every dialog with
   the row off, which reset the cluster's glyph sizes (its + / - drawn smaller than on main). Only the
   cluster is affected; the panels and Options in the before shots are P9's.

## 6. Verified

* Windows: `tools/build.sh build` (debug) clean, no new warnings; `tools/build.sh unit`: 1525 tests,
  0 failures, 1 skipped (the kit cross-check, as on main). Row off: `win_rowoff_options.png` (faithful look).
* Android: `tools/android.sh build debug`, installed and run on the S25 (serial RFCY807LZLY) under the
  phone lock. The phone was left stopped, with this branch's debug apk, its `settings.cfg` as found
  (`GameUIScale=42`; every Options change in the runs was cancelled).
* Not built: Linux, macOS (CI). The engine change is portable C++ in `src/engine/ui/sliderelement.cpp`.
