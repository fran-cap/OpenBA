# Android P11d: the Mods and Save / Load buttons at the touch size (2026-10-26)

Session log of `docs/runtime_port/NEXT_SESSION.md` §2.12 ("RULED 2026-10-26 (owner, real-finger test of the
P11c apk)": the Mods menu's buttons and the Save / Load / Cancel buttons are not touch-sized). Lane
`p11dbuttons`, based on `android/p11c`. Facts that outlive the session are in
`docs/extensions/handins/PHONEMENU.md` §2 (the list cap paragraph) and `src/ext/phone_menu/touch_scroll.h` (b).

## 1. Why the walk "missed" them: it did not

A temporary per-button log in `touch_size.cpp` (not committed) showed the size walk reaching every button of
both dialogs, on Windows with the row on and on the S25: Mods `buttonOk` / `buttonCancel` / the tab's
`buttonMod`, SaveLoad `buttonSave` / `buttonLoad` / `buttonCancel` / `buttonDelete`, all padded from 26 to
**36 units**. None of the four suspects in the task was the cause (not skipped as menus, not resized by their
owner, not built late, not an unhandled control type). The buttons were drawn small because the WINDOW was
shown at the fitted, smaller UI scale (touch_fit.h (c)): the scrolling-list cap that should have made it fit
(touch_scroll.h (b), P11b) was dropped as "took nothing off", for a different reason in each dialog:

* **Mods (`Dialog/Mod.rsc`)**: the dialog holds two vertical lists, `tableList` (installed mods, 350 units)
  and `containerResult` on the hidden Browse page (348). A hidden page is never laid out, so the second
  list's `_layoutHeight` is unwritten memory (1952543827 in a Windows dump). The cap step took the tallest
  list from `_layoutHeight`, so the cap came out larger than any list, changed nothing, and was dropped.
  S25 log: `larger than the safe area: shown at UI scale 0.51 (k 0.824)` then `0.53 (k 0.792)`.
* **Save / Load (`Dialog/SaveLoad.rsc`)**: `mainGroup` has its own minimum height, 360 units, around the
  344-unit save list (`groupSelect`, 8 + 8 padding). Capping the list shortened the list but not the window,
  so the cap was dropped. S25 log: `shown at UI scale 0.45 (k 0.933)` then `0.46 (k 0.913)`.

## 2. What changed (both in `touch_scroll.cpp`, one commit per dialog)

The other P11d agent (p11d-firstframe) agreed in session that these go in this branch; its branch touches
only `touch_fit.cpp`.

| Commit | Dialog | Change |
|---|---|---|
| `ext.phone_menu: Mods dialog list cap reads each list's requested height` | Mods | the cap step's tallest list is its `_requestedHeight` (cap or description), its content's when clamped; `_layoutHeight` only for a list with no requested height |
| `ext.phone_menu: Save / Load dialog frame follows its list's cap` | Save, Load | `FitFrames`: when a list's cap changes, each ancestor with a description minimum height keeps that minimum less what the cap took off (`size_math` `FrameMinHeight`, unit test `ext_phone_size_list_frame_min`). Set with the cap, not from the laid-out list, so the "Please wait" page shown before the save list already has the capped height (no height change when the list appears). Row off: `ApplyCap(0)` puts retail's minimum back. A minimum above the description's (an owner's) is left alone |

## 3. Measured on the S25 (UI 42, 2340 x 1080; outer edge of the button frame, device pixels, about +-2)

| Target | Before | After | Window scale after |
|---|---|---|---|
| Mods Ok / Cancel | 66 | 85 | player's (`lists capped at 243 units ... 423 of 423`) |
| Load Load / Cancel / Delete | 78 | 85 | player's (`capped at 309 units`) |
| Save (in game) Save / Cancel / Delete | 78 (same dialog) | 85 | player's (`capped at 309 units`) |
| Yes / No modal (`SaveLoad.rsc:confirmDelete`, nested UserInterface) | - | 85 | already right (walked; fits) |
| New Game Ok / Cancel, the five combos | 78 / ~75 | unchanged | k 0.933: no scrolling list to cap (§5) |

Before = `android/p11c`'s `touch_scroll.cpp` in this branch's apk (Load), and the diag-only apk (Mods). The
reference [x] is 85 px. Screenshots and ruler crops, session scratchpad `p11d_buttons/`:
`s25_mods_before.png`, `s25_mods_final.png`, `m_mods_before.png`, `m_mods_final.png`, `s25_load_before.png`,
`s25_load_final.png`, `m_load_before.png`, `m_load_final.png`, `s25_save_after.png`, `s25_modal.png`,
`m_modal_yesno.png`, `s25_newgame.png`, `m_newgame_ok.png`, `m_newgame_combo.png`.

The modal was shown on the phone by a temporary, uncommitted hack (`SaveLoadDialog::OnUpdate` calling
`DoDelete` once, the unattended answer bypassed); no driver reaches a modal otherwise.

## 4. The front-end sweep

* **Already right (36 units, 85 px):** the Yes / No confirm modals (`SaveLoad.rsc:confirm*`: walked on the
  nested UserInterface `ShowDialog` builds; small enough to fit), Options (P11c), Fixes and Extensions
  (P11b). Front-end Exit has no confirmation modal in retail (`MainMenuDialog::OnCommand` kButton_Quit quits).
* **Fixed here:** Mods, Load, Save (§2).
* **Not fixed, ruling owed:** New Game (§5).
* **Mod rows** (only with mods installed; the S25 has none): on Windows with the row on and two mods
  (`DebugMenu.pkm`, `betterFields.pkm` copied in for the run and removed after), the Enable check, `Top`,
  `...` (Details) and the up / down arrows are all 36 units high (`win_rowon_mods_2mods.png`). The `...`
  button stays narrow (retail's width): a trial rule padding every text button to at least
  36 wide pushed the up / down arrows out of the row's fixed Load Order column
  (`win_rowon_mods_2mods_after.png`) and was not kept.

## 5. Rulings owed

1. **New Game at 0.933 (78 px)**: its form has no scrolling list, so nothing can give height back and the
   tall-dialog fit shrinks it. Options: accept; or frame its page in a scrolling list (touch_scroll.h (a),
   as Options' Video page), which would put the last rows / the difficulty text below the fold.
2. **Mods `...` (Details) button width**: 36 high but retail's narrow width. Widening it needs the row's
   Load Order column wider (retail's layout) or the four row buttons re-flowed.
3. **Frame rule scope**: any frame with a description minimum height above a capped list follows the cap
   (only SaveLoad's `mainGroup` is known to have one). A dialog whose frame minimum is larger than its list
   would also shrink by the cap's amount, never below its content.

## 6. Also seen (for p11d-firstframe, not changed here)

Both dialogs still log `larger than the safe area: shown at UI scale ...` on their first placement, then the
cap, then `back at the interface's scale`: the first frame is drawn fitted, the second at the player's
scale. That is the first-frame resize the other P11d lane owns (touch_fit.cpp TouchFirstPlace).

## 7. Verified

* Windows: `tools/build.sh build` (debug) clean; `tools/build.sh unit`: 1552 tests, 0 failures, 1 skipped
  (the kit cross-check, as on main). Row off: `run-menu`-equivalent runs on the functional dir
  (`BANISHED_AUTOMENU_BUTTON=7` / `2`): `win_rowoff_mods.png`, `win_rowoff_load.png`, retail's look.
* Android: `tools/android.sh build debug`, installed and run on the S25 under the phone lock; the phone was
  left stopped with this branch's debug apk.
* Not built: Linux, macOS (CI). The change is portable C++ in `src/ext/phone_menu/`.
