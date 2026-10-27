# Android P11c: Options reopened, the safe margin, the drift fix (2026-10-26)

Session log of three of the owner's rulings in `docs/runtime_port/NEXT_SESSION.md` §2.11 ("RULED 2026-10-26,
real-finger test of the P11b apk"), lane `p11cdialogs`, branch based on `android/p11b` (43e65f064). Lasting
facts: `docs/extensions/handins/PHONEMENU.md` (§2 safe area, §3 the kExtPlaceUpperLeft arm),
`docs/extensions/handins/DIALOGPOSITIONDRIFT.md`, `docs/retail_bugs.md` §13, the headers
(`touch_size.h` `TouchForgetClosed`, `touch_scroll.h` `TouchScrollForgetClosed`, `phone_math.h`
`kSafeMarginFraction`, `src/ext/dialog_position_drift/`). Phone: Galaxy S25, UI 42 (982 x 453 layout units),
phone_menu, touch_mode and touch_pointer on, real `adb shell input tap` touches.

## 1. BUG: Options opened several times in a row loses its fit (commit `ext.phone_menu: a dialog opened again ...`)

**Reproduced** on the P11b code (this branch before the change): front-end Options opened, Cancel, opened
again, six times. `standalone_error.log` (`[PhoneMenu]` lines, session scratchpad `p11cdialogs/b_error.log`):

| opening | dialog | log |
|---|---|---|
| 1 | 0x72477399e8 | fit 0.44 (k 0.955), `capped at 295 units`, `back at the interface's scale` |
| 2 | 0x7247803830 | the same three lines |
| 3 .. 6 | 0x7247803830 (the same address) | `shown at UI scale 0.58 (k 0.724)` and nothing else: no cap |

Screenshots `b_open1.png` .. `b_open6.png`: from the third opening the dialog is drawn at 0.72 of its size,
its Video page uncapped (the dialog as tall as the screen), the Game list at retail's 318 units.

**Root cause.** Every per-window record of the fit is keyed by the DialogInstance's address: `touch_size.cpp`
`g_seen` (TouchSeen / TouchKnown, with the description pointer) and `touch_scroll.cpp` `g_windows` (the list
cap: `cap`, `settled`, `room`). Nothing forgot a record when its dialog closed. A dialog closed and opened again
is allocated at the closed one's address (seen above, and for Fixes and Extensions and Professions below), with
the same description, so it matched the old records:
* `TouchScrollCap` found `cap = 295`, `settled = 413` for a window whose new lists were at retail's height. The
  search starts from the cap in force: `ShrinkCap` returned the same 295, `cap == w.cap` broke the loop before
  `ApplyCap`, so the lists were never capped, the window stayed taller than the safe area, and `DecideScale`
  scaled the whole dialog down. The record then settled at that height, so nothing tried again while it was open.
* `TouchKnown` said "seen", so the P11b first-placement walk (`TouchFirstPlace`) did not run either.
The second opening worked only because it landed at a new address.

**Fix (state cleared on close).** `TouchFrame`, once a frame per UserInterface at its head dialog's layout,
first calls `TouchForgetClosed(ui)`: every `g_seen` record and every `g_windows` record (now carrying their
UserInterface) whose dialog is no longer in that UI's open list is dropped. A dialog opened again is a new one:
first-placement walk, fresh cap search. Belt and braces: the cap search applies the cap in force to the lists
before searching from it (a no-op for the window it was decided on). The P9 `g_dialogFits` table was already
pruned the same way by `DecideScale`.

**Verified on the S25** (debug apk of this branch, `a_error.log`, screenshots `a_*.png`):
* front-end Options, 7 openings in a row (6 at the same address 0x7247a40190): every one logs the cap
  (295 units, 413 of 413 high) and the dialog back at the player's scale; `a_open1.png` .. `a_open7.png` identical;
* Fixes and Extensions, 6 openings, all at 0x72478f7cd8: cap 233 every time;
* pause-menu Options in a new game (seed 31337), 6 openings (5 at 0x7248fccee8): cap 295 every time;
* Professions, 6 openings (addresses reused twice): cap 353 every time, `a_prof1.png` .. `a_prof6.png`.
* Windows: the code is behind the row; desktop with the row off walks nothing but the forget pass (records
  are only made with the row on).

## 2. The safe margin a quarter smaller (commit `ext.phone_menu: the safe margin ...`)

The margin is not the device's cut-out insets: it is a fixed fraction of the layout height
(`phone_math.h` `kSafeMarginFraction`, P7's 4.5 %, the S25's 3.5 % corner radius and 3 % cut-out with air). It is
now `kSafeMarginP7Fraction * 0.75` = 3.375 %: at UI 42 15 units (was 20), at 1080 pixels 36 (was 49). It still
clears the cut-out's depth; the corner radius is cleared by each dialog's own frame and padding. ext.touch_mode
(its toolbar and camera cluster margin) and ext.touch_pointer read the same `SafeMargin` and follow. The UI-scale
slider floor at render scale 50 drops from 70 to 65 (35 at full scale, unchanged). Unit tests updated
(`ext_phone_menu_test.cpp`, `ext_phone_touch_test.cpp`: every number derived from the margin, one new boundary
case). Before / after on the S25: before `menu.png`, `b_open1.png`, `a_ext1.png`, `game.png`, `a_prof1.png`;
after `m_menu.png`, `m_options.png`, `m_ext.png`, `m_game.png`, `m_house_low.png` (session scratchpad
`p11cdialogs/`). The dialogs' top edge moves from about 56 to about 44 device pixels.

## 3. fix.dialog_position_drift (commit `fix.dialog_position_drift: ...`)

Built as ruled: fix tier (the registry's fixes carry no class; "class A" in the ruling read as "live, no save
effect"), default ON, off under faithful, its own directory, settings key `fix.dialog_position_drift`, a row on
the Extensions dialog's Default tab ("Steady panels at the edge", icon `Entry_dialog_position_drift.png`). Details
in the hand-in. It rides the existing `_extDialogPlace` seam at `kExtPlaceUpperLeft`; ext.phone_menu's Place calls
it before its own row's test, and phone_menu's own sync now runs only with the fix off, so with both on the sync
runs once (and is idempotent anyway).

**Measured** (a temporary per-call trace in `DialogInstance::SetUpperLeft`, removed before the commit: requested
upper-left, the stored one before and after the sync, the root's laid-out rectangle):
* S25, phone_menu + fix on, the house at tap (1123,760): first SetUpperLeft before any layout (stored 0,0, measured
  88 high); the second call finds the stored y 221 but the root laid out at 218 (pulled back 3 units to the
  bottom margin 438): the sync sets 218, the panel lands at the requested 42 (retail's delta would have drawn it
  at 39 for that frame), and every later frame stored = laid out (`drift_phone_error.log`).
* S25, the same house, fix OFF and phone_menu on (toggled in the Extensions dialog): phone_menu's own sync takes
  the same 221 -> 218, drawn at 42. The P11b behaviour is kept with the fix off.
* S25, phone_menu OFF (retail layout, 196-unit panel), a house panned to just above the toolbar so its first
  layout crosses the screen's bottom (453), the same camera for both runs (`drift_abc_error.log`):

  | fix | stored y before the move | root laid out at | requested | drawn that frame |
  |---|---|---|---|---|
  | off | 262 | 257 (pulled back 5) | 106 | **101** (retail's one-frame jump) |
  | on | 262 -> 257 (synced) | 257 | 106 | **106** |

  That is retail's mechanism on its own, reproduced and fixed without ext.phone_menu. Both rows were put back on
  at the end of the session (`[Ext] ext.phone_menu requested=1`, `fix.dialog_position_drift requested=1`).
* Windows (debug, functional dir, seed 31337, `BANISHED_CAMERA_PIN`, `BANISHED_PANEL_HOLD` house and citizen):
  the panels measure their final size at their first SetUpperLeft (368 x 196, 370 x 218 at UI 83), so stored and
  laid-out agree on every frame with the fix on; retail's drift needs a panel that grows against the bottom edge,
  which the harness cannot produce. No desktop before / after of the jump itself.

## 4. Rulings owed

1. **The margin vs the rounded corners.** At 3.375 % the margin is just under the S25's corner radius (3.5 %); a
   dialog laid out in the very corner keeps its content off the curve only through its own frame. Not seen to clip
   on the S25 screenshots; flag if a corner control is hard to hit.
2. **Fix row name and wording** ("Steady panels at the edge"), and its icon (a panel between two edge bars).
3. **"Class A" for a fix**: the registry's fixes have no class (DESIGN.md §2); built as a fix, default ON, live.
4. **The desktop trigger is rare** (a panel growing while held against the bottom edge); the row is on everywhere
   by default as ruled.

## 5. Not done / follow-ups

* The S25 locked behind its secure keyguard once during a build (the owner unlocked it); `svc power stayon usb` was
  set so it stays awake on USB. The phone was left with a trace-free debug apk of this branch, stopped.
* Linux and macOS not built here; the changes are portable C++ in `src/ext/` with no platform branch.
