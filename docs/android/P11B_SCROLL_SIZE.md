# Android P11b: tall dialogs scroll, buttons at the [x]'s height (2026-10-26)

Session log of the owner's touch-size rulings in `docs/runtime_port/NEXT_SESSION.md` §2.10 ("RULED
2026-10-26"), under `ext.phone_menu` (same row, same walk as P9 / P11). Facts that outlive the session:
`docs/extensions/handins/PHONEMENU.md` §2 ("A tall window scrolls instead of shrinking"). Code:
`src/ext/phone_menu/touch_scroll.{h,cpp}`, `size_math.{h,cpp}` (`ShrinkCap`, `GrowCap`, `kListFloor`), three
lines in `touch_size.cpp` (`TouchSizeWalk`), our frame `src/ext/ui/Dialog/PhoneScroll.rsc`. Unit tests:
`tests/unit/ext_phone_size_test.cpp` (cap arithmetic), `ext_rsc_text_test.cpp` (the frame parses). Not
touched: `TouchFrame`, `DecideScale`, the Place functions, `src/ext/enhanced_tab/`.

## 1. Why the buttons stopped short of the [x] (ruling 1)

Round 1 did pad the Cancel / Ok and tab buttons to 36 layout units. What kept them small on screen was
the P9 tall-dialog fit (`touch_fit.h` (c)): a dialog taller than the safe area is drawn at a smaller own
scale, and every control in it with it. Logged on the S25 at UI 42 (`standalone_error.log`):

| Dialog | Fit before (P11 code) | After |
|---|---|---|
| Fixes and Extensions | UI 0.52, then **0.54 (k 0.778)** | lists capped, `back at the interface's scale` (k 1) |
| Options (front end) | UI 0.44 (k 0.955), then **0.45 (k 0.933)** | lists capped at 295 units, `413 of 413 high`, k 1 |

36 units at k 0.778 is 66 device pixels: the 66 round 1 measured. The Options tab buttons were never
short: their labels (icon over text) make them 42 units (logged), 93 px at k 0.933.

**The fix (one rule, not per dialog):** a top-level window taller than the safe area at the player's UI
scale first gives the height back from its scrolling lists. Every vertical list of the window is capped at
one height (the tallest list's less the overflow, at least `kListFloor` = 3 x 36 units); the window then
fits and `DecideScale` leaves it at the player's scale. The fallback scale-down stays for a window that
still does not fit with its lists at the floor. A cap that takes nothing off is dropped (round 2 of the
code: the Options Video page, before it scrolled, held the height and the Game list was capped for
nothing). The decision is re-taken only when the window's height or the safe area changes.

Fixes and Extensions is our own `.rsc`, so its buttons could have been sized there instead; that would
not have helped (the fit shrinks whatever size they have) and the Mods dialog (retail's, the same list)
would have stayed small.

## 2. Options pages scroll like the Enhanced tab (ruling 2): the route, and why

Retail's Options pages: Game and Input are scrolling containers (`Options.rsc:gameOptions`,
`inputOptions`, 318 high); Video (`graphicsOptions`, a vertical ribbon of two tables, 327) and Audio
(`soundOptions`, a table) are not. A tab element is as tall as its tallest page, so Video held the dialog.

The two routes the task named:
* **an ext-owned replacement page description** (a copy of the Video page's layout in our `.rsc`, or a
  swap of the tab's content description at load): a derivative of retail's layout data in the tree, and a
  change to retail's loaded description that every platform would see. Rejected.
* **an ext seam that wraps the page at run time (chosen).** On the phone, with the row on, the page's
  ELEMENT is moved into a scrolling frame of ours (`Dialog/PhoneScroll.rsc:phoneScroll`, which derives
  from retail's `Options.rsc:gameOptions` by reference: its padding and scroll bar, size left to the
  content). Retail's descriptions are untouched; nothing runs with the row off (desktop: the walk never
  starts, `win_rowoff_options.png` is retail's look). The elements are moved, not rebuilt, so the
  OptionsDialog controller's pointers (found at open) stay valid and Apply / Cancel work unchanged. The
  frame is built the way `TabElement::AddTab` builds the Enhanced page. Row off puts the page back.

With every page a scrolling list, round 1's "controls on a non-scrolling tab page keep retail's size" rule
no longer applies to Options (it is kept in the code for other dialogs' tab pages): the Video page's
combos and checks take the touch size. The Enhanced tab's page and rows are not changed (its container is
capped like the others).

## 3. Professions list scrolls (ruling 3)

`Profession.rsc:professionTable` is moved into the same frame (retail's own comment beside it: "could put
parent object(content) in a Container with scroll bar"). The spinner scale is unchanged. With the stock
professions the dialog is capped at 353 units: 9 rows of two shown, the rest reached by the bar
(`a2_prof.png`, scrolled `a2_prof_scrolled.png`). Not checked with a mod adding professions: the two mods in
`C:/GOG Games/Banished/_mods_off/` (`betterFields.pkm`, `DebugMenu.pkm`) add none; no mod was mounted.

## 4. Measured on the S25 (UI 42, render scale Full, 2340 x 1080; device pixels, about +-3)

Before = `android/p11` code (this branch with the two new calls disabled, `before.apk`); after = this
branch. A text button is measured over its dark frame, a tab over its bevel.

| Target | Before | After |
|---|---|---|
| Close [x] (reference) | 85 | 85 |
| Fixes and Extensions Cancel / Ok | 57 | 84 |
| Fixes and Extensions Optional / Default tabs | about 58 | about 80 (36 units) |
| Options Cancel / Apply / Ok | about 79 | 84 |
| Options Game / Video / Audio / Input / Enhanced tabs | 93 (42 units at k 0.933) | 100 (42 units) |
| Options Video page combos | 64 | about 82 |
| Options Video page check boxes | about 53 | about 95 (36 units with the frame) |

Screenshots (session scratchpad `p11bsize/`, not in the tree): before `b_options_game.png`,
`b_options_video.png`, `b_options_audio.png`, `b_ext.png`, `b_prof.png`; after `a2_options_game.png`
(opened on Enhanced), `a2_options_video.png`, `a2_options_audio.png`, `a2_options_gamepage.png`,
`a2_options_input.png`, `a2_ext.png`, `a2_prof.png`, `a2_prof_scrolled.png`; ruler crops `m_b_*.png`,
`m_a2_*.png`; Windows row off `win_rowoff_options.png`.

## 5. Rulings owed

1. **The Audio page's empty scroll track.** The frame is the Game page's container, whose bar is always
   drawn; the Audio page fits, so its bar has nothing to scroll (`a2_options_audio.png`). Kept (the Game
   and Input pages show theirs the same way).
2. **A capped list shows a part of its next row** at the bottom (Options Game / Video, Professions). Kept:
   the cap is a height, not a row count.
3. **One frame at the fitted scale.** The walk sizes a dialog from its second frame (`TouchSeen`), so a
   tall dialog is drawn fitted for one frame and then at the player's scale (`larger than the safe area`
   then `back at the interface's scale` in the log).
4. **The Options tab buttons** were already above the [x]'s height (42 units). If the owner still finds
   them hard to hit, the lever is their width (about 40 units, the label's) or the 4-unit gap between them.
5. **`kListFloor` = 3 touch rows** (108 units): below that the scale-down takes over again.
6. **Every tall window gets the cap**, not only the three named (the Mods dialog's list too). The New Game
   dialog still logs a fit (0.47): what holds its height does not scroll.

## 6. Verified

* Windows: `tools/build.sh build` (debug) clean, no new warnings; `tools/build.sh unit`: 1540 tests, 0
  failures, 1 skipped (the kit cross-check, as on main). `run-menu` + `BANISHED_AUTOMENU_BUTTON=4` on the
  functional dir (row off): Options in retail's look, `win_rowoff_options.png`.
* Android: `tools/android.sh build debug`, run on the S25 under the phone lock; front-end Options (every
  page), Fixes and Extensions, Professions in a seeded new game (`BANISHED_SEED=31337`). The phone was
  left stopped with a debug apk of this branch plus one temporary diagnostic log line (since removed); every Options change was cancelled.
* Not built: Linux, macOS (CI). The new code is portable C++ in `src/ext/phone_menu/`.
