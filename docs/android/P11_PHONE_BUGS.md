# Android P11: the bugs seen on the phone after P9 / P10 (2026-10-26)

NOT RETAIL. Session log of `docs/runtime_port/NEXT_SESSION.md` §2.9 item 3, lane `p11bugs`, branch based on
`main` 5daa47bc6. Lasting facts are in `docs/extensions/handins/PHONEMENU.md` §2 / §3 (the fit, the loading
screen, the two seams) and `docs/extensions/handins/TOUCHMODE.md`. Screenshots are in the session scratchpad's
`p11bugs/before/` (a debug apk of plain main) and `p11bugs/after/` (this branch). They are not in the tree.
Phone: Galaxy S25, the owner's settings (UI 42, render scale Full, phone_menu and touch_mode on).

## 1. The tall-dialog fit scaled the whole interface: FIXED (one cause, three symptoms)

**What it was.** P9 showed a dialog larger than the safe area by raising `UserInterface::_uiScale` while it
was open, so everything else in that interface was laid out and drawn at the fitted scale too
(`touch_fit.cpp` `DecideScale`).
- *Version text smaller when Options opens*: the front end's interface went from 0.42 to 0.44 (log
  `[PhoneMenu] UI ...: a dialog larger than the safe area, laid out at UI scale 0.44 (the player's 0.42)`).
  With Fixes and Extensions (0.50 at UI 42) the version line visibly shrank: `before/pause_ext.png`.
- *Opening the tools and reports menu makes every dialog jump*: a different trigger of the same fit. A
  sub-toolbar is a child `ToolbarDialog` placed by its LEFT PADDING (`ToolbarDialog::OnElementEvent`:
  `SetPadding(0, xPos, ...)`), so its padded size reaches across the screen and the fit took it for a window
  wider than the safe area. On main, opening Tools and Reports logged `laid out at UI scale 0.43 (the
  player's 0.42)` and every dialog of the game interface moved and shrank (`[TouchMode] toolbar k=1.529 ...
  1006x464` after `k=1.489 ... 982x453`). The wider the sub-toolbar, the larger the jump.
- *The in-game pause menu no longer centred*: the pause menu lives in the game interface, so it was laid out
  at whatever fit was in force (`before/pause_with_tools.png`: the pause menu at 0.43 with Tools and Reports
  open behind it; smaller, and its rows decided for another layout size). On this run it stayed centred to
  the eye; the owner's off-centre view was not reproduced exactly, but its only route into the pause menu's
  layout was this fit, which no longer touches it.

**The change** (commit "ext.phone_menu: a tall dialog is fitted at its own scale"). The fit goes through the
per-dialog scale seam `UI::UserInterface::_extDialogScale` that P10 added: only a top-level window larger than
the safe area takes k = player / fitted (k < 1); what it opens follows it (its child dialogs, its tooltip, its
combo drop-downs: `FitOwner`). Child dialogs (the sub-toolbars) and dialogs ext.touch_mode scales are not
judged. `PhoneMenu::Sync` installs the seam with one function that asks `TouchMode::Scale` first (the toolbar),
then the fit; `TouchMode::Sync` is gone. The safe-area arm converts a fitted dialog's own-unit place and size
to layout units. The centring of the fitted dialog is computed in its own frame by the engine (the placed
rectangle divided by k).

**Engine seam fix found on the way.** In the pump arm the screen clip region every element's hover test reads
(`UserInterface::_clipRegion`, `IsInsideClipRegion`) stayed in global units. With k < 1 the dialog's lower part
lies beyond it in own units: Cancel / Ok of Fixes and Extensions did not answer (`after/ext_cancel2.png`, first
build). The pump arm now gives a k != 1 dialog the screen in its own units for its Update (P10's toolbar, k > 1,
gets a smaller and exact region; nothing of it lies outside the screen).

**Verified on the S25** (debug apk of this branch):
- Options in the front end: the dialog the same fitted size as before, the version line and the menu behind
  it at the player's 0.42 (`before/options.png` vs `after/options.png`; log `dialog ... larger than the safe
  area: shown at UI scale 0.44 (k 0.955), the interface keeps the player's 0.42`). Its Autosave combo
  drop-down opens under its box at the dialog's scale (`after/options_combo.png`).
- Tools and Reports in game: no fit line, the toolbar keeps `k=1.489`, nothing moves (`before/tools.png` with
  the 0.43 fit vs `after/tools.png`).
- Pause menu with Tools and Reports open behind it: at the player's scale, centred (`before/pause_with_tools.png`
  vs `after/pause_with_tools.png`).
- Fixes and Extensions from the pause menu: fitted, its scroll bar drags (`after/pause_ext_scrolled.png`), its
  Cancel closes it (`after/ext_cancel3.png`).
- Windows, row off (`tools/build.sh run-menu`, `BANISHED_AUTOMENU_BUTTON=4`, functional dir): Options at the
  player's full scale, no `[PhoneMenu]` fit line (`after/win_desktop_options.png`).

**The P9 follow-up "Apply on the UI-scale slider while Options is shown scaled down keeps the fitted size":
FIXED by the same change.** The fit is decided again from `_uiScale` every frame, so an Apply re-fits the
dialog on the next frame (`after/opt_applied.png`: the slider dragged to 89, Apply, the Options dialog at once at
the new scale; the phone's `settings.cfg` was put back afterwards).

## 2. Loading screen: a sliver of picture at the bottom: FIXED (two causes)

**What it was** (`before/load_4.png`, crop `loading_bottom_before_after.png`, top half):
- *A band of mismatched picture along the border.* P9 drew the opaque picture a second time bottom-aligned and
  clipped it with a straight line at 0.125 of its height, through the picture's own torn edge (it reaches
  from about 0.08 to 0.14 of the height on the S25). Between the edge's peaks the copy shows its own picture,
  which does not match the top-aligned picture above the line.
- *The strip sat a pixel or two too high.* The copy and its clip were anchored to the interface's layout
  height, the back buffer times the UI scale TRUNCATED (453 of 453.6 units at UI 42): the last two device rows
  (1078, 1079) showed the top-aligned picture under it, measured on `before/load_4.png`.
Neither the 19.5:9 fit nor the render-scale coupling was the cause (the scissor maps layout units to device
pixels by the same `SpriteContext::_scale`).

**The change** (commit "ext.phone_menu: the loading screen's bottom border from the menus' overlay"). P9's
clean route, the menus' see-through overlay drawn over the picture, crashed because the menu loop's
`ExternalFactory::Clean` freed the overlay. Now `MenuOpened` keeps it (`KeepMenuOverlay`): a resource clean
bit of its own, reserved once from the factory's loop bits and never handed back, ORed into the overlay
sheet's row through `ExternalFactory::Add`. The first build of this crashed in the next loader's reference
visit (`standalone_crash.log`: `System::Filename` copy from a freed `MaterialInstance` under
`SpriteSheet::Serialize`): the visit marks what a resource references only while `_loadEnabled` is set, so the
material was freed under the kept sheet. Setting the flag around the `Add` (the loader bracket in miniature)
marks the material and its textures; no crash since, through several new games.
The overlay is drawn bottom-aligned at the picture's width and clipped at 0.30 of its height, inside its
transparent middle (the line never shows), so its torn edge lies over a continuous picture. The bottom-aligned
draw and its clip end on the real bottom edge (`ScreenBottomUnits`, rounded up). The ruling stands: the
picture's top border is the one cut on 19.5:9 (the picture stays top-aligned), both torn borders drawn, the text
at the UI scale. Before any menu has opened (no overlay kept yet: ext.load_progress's boot screen), P9's lower
copy is the fallback, now on the real bottom edge.

Engine seam: `_extBackdropOver` takes the sprite to draw (sheet and index, in / out).

**Verified on the S25**: two new games (`after/load_6.png`, `after/load2_6.png`, a different picture); crop
`loading_bottom_before_after.png` (top: main, a band of picture between the edge's peaks and a line along the
bottom row; bottom: this branch). Rows 1074..1079 are black on the four loading frames captured on this branch (sampled
across the width); on main rows 1078 / 1079 were picture.

## 3. Toolbar tooltip over the button above the one tapped: NOT REPRODUCED, nothing changed

On main and on this branch, at render scale Full and Half, a held 120-200 ms touch on a bar button, on a
sub-toolbar button, and on a bar button with another sub-toolbar open shows the tooltip with its bottom-left
exactly at the touch point (retail's rule: the tooltip above the pointer, `BeginToolTip` / the MOVE arm
`SetUpperLeft(x, y - h)`), mapped correctly out of the toolbar's own units by touch_mode's `TooltipOffset`:
`before/tip_houses.png`, `before/tip_sub.png`, `before/tools.png`, `after/tipseq_4.png`, `after/half_tip.png`.
The tooltip's anchor is the pointer, not a button rectangle, and neither the unscaled button rectangle nor a
lift offset (touch_pointer is not built on main) enters it.

What a finger does differently from a mouse: the pointer stays where the finger lifted, so the tooltip stays
up, sitting above the touch point; with a sub-toolbar open above the bar, it covers the sub-toolbar's lower
edge, and over a sub-toolbar button it covers the tapped button's upper half. Whether that is what the owner
saw is a ruling (below); the tutorial glow ring is not the same transform (it is placed from a toolbar
element's rectangle, not from the pointer), so it was left as it was (`TOUCHMODE.md`).

## 4. Rulings owed

1. **Tooltip under a finger** (§3): which tap showed the tooltip "over the button above", and what is wanted
   for touch: keep retail's tooltip-above-the-pointer, or for a touch on the (sub-)toolbar put the tooltip
   above the whole toolbar stack (clear of the sub-toolbar), or hide a toolbar tooltip once the finger lifts.
   The touch_pointer agent's finger input (P11 item 1) is the natural owner of the last two.
2. **Text of a fitted dialog**: a dialog drawn at its own scale is drawn with retail's filtered quads (the P10
   seam switches ext.sharp_ui's snap off for it), so Options / Fixes and Extensions text is no longer
   rasterised at the fitted size as it was when the whole interface was fitted. It reads clean on the S25
   (`after/options.png`); a ruling if the owner sees it soft.

## 5. Unit tests and builds

`tools/build.sh unit`: 1525 tests, 0 failures, 1 skipped (the kit cross-check: no `build/ext_resources` in
this worktree). New cases: `ext_phone_touch_fit_is_the_dialogs_own_scale`,
`ext_phone_loading_screen_bottom_rounds_up`, `ext_phone_loading_overlay_bottom_border`. Built: Windows DX11
debug, the Android debug apk (Vulkan). Not built here: Linux, macOS (the change touches no platform seam; the
engine change is in `src/engine/ui`, compiled on all three by CI). Not run: the DX11 gate and the resave check
(the coordinator's), the faithful profile (every seam involved is null there by `Sync`).

When this lane released the phone, the app was stopped and its `settings.cfg` was byte-identical to the one
found (UI 42, render scale 100), written back into the app-owned file with `cat` after the slider and render
scale runs. Later runs on the shared phone belong to the other P11 lanes (a later check found their
`ext.touch_pointer=1` seed line appended).
