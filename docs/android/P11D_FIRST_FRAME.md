# Android P11d: a dialog's first drawn frame is its final one (2026-10-26)

NOT RETAIL. Session log of the owner's ruling in `docs/runtime_port/NEXT_SESSION.md` §2.12 ("Options, and the
Save and Load dialogs, RESIZE themselves 1-2 ticks after opening"), lane `p11dframe`, branch based on
`android/p11c` (e7b958e46). Lasting facts are in the header `src/ext/phone_menu/touch_fit.h` ((f) and
`TouchFirstPlace`). Phone: Galaxy S25, UI 42 (982 x 453 layout units), phone_menu, touch_mode and touch_pointer
on, real `adb shell input tap` touches.

## 1. How it was measured

A temporary trace (removed before the commit) printed, for every dialog with a description, its root's laid-out
rectangle and its own scale k after `UserInterface::Update`'s ComputeLayout (`LAYOUT`) and at `UserInterface::
Draw`'s per-dialog scale point (`DRAW`, what is drawn), each time either changed, with a frame counter; plus, in
`touch_fit.cpp`, the change counts of the per-frame walks and of `TouchFirstPlace`, and (for the Load dialog) a
dump of the tree's rectangles whenever the root's changed.

## 2. Before (the P11c code, layout units, root {left, top, right, bottom})

| dialog | frame 1 | later | what changed |
|---|---|---|---|
| Options (front end) | 255 15 728 438, k 1 | frame 2: 243 15 740 438 | 24 units wider |
| Load (front end) | 243 19 831 477, k 0.913 | frame 14 (~230 ms): 225 19 849 477 | 36 units wider (own units), recentred |

(Logs: session scratchpad `p11dframe/run1_error.log`, `run4_error.log`.)

## 3. Causes

1. **Options: the P9 scroll-bar rule ran a frame late on the frames P11b adds.** `TouchFirstPlace` (P11b) runs
   the touch-size walk at a dialog's first placement, and that walk frames the Video and Audio pages in a
   scrolling list (`touch_scroll.cpp`, `Dialog/PhoneScroll.rsc`). The new frames' scroll bars are made
   thicker (touch_fit.h (a): the container grows by what the bar gains, 24 units) only by the P9 walk, which
   runs once a frame at the HEAD dialog's layout: the next frame. Trace: `frame-walk p9=3` at the head (the
   dialog's own bars), `first-place size=139`, then on frame 2 `frame-walk p9=2` and the 24-unit growth.
   The scale and list cap were already final on frame 1 (k 1.000, cap 305, 423 of 423 high, unchanged).
2. **Save / Load: a widened list in a group shown later.** `Dialog/SaveLoad.rsc`'s page shows a "Please wait"
   group first and the save list (`groupSelect`: the list and its bar) once the save files are read, about 14
   frames later (retail's controller). A hidden group adds nothing to its parent's size (GroupElement::
   ComputeSize 0x180117f00 skips hidden children). Retail's page is exactly as wide as the list it shows
   (572: list 540, bar 12, spacing and pads), so retail does not resize; the bar made 36 units thicker by
   (a) made the page 608 when the list appeared, and the whole dialog widened and recentred. Tree dump:
   `mainPage 572 -> 608`, `groupSelect hid=1 -> 592 wide`, `verticalScrollbar 48`.
   The first fix tried (the root's minimum width) kept the width but not the place: DialogInstance::
   ComputeLayout 0x180113d90 centres a root by its laid-out width, which the root's own minimum does not
   enter (the root was drawn 18 units right of centre, then moved). The hold is therefore on the page.

The brief's suspects: `TouchFirstPlace` does run for centred dialogs (at their first `kExtPlaceLayout`, P11b's
`fresh` test covers both points); the fit (`DecideScale`) and the list cap were final on the first frame for
every dialog traced; the Options controller filling its pages after the first layout did not move the size.

## 4. The change (`touch_fit.cpp`, `touch_fit.h`)

- `TouchFirstPlace` also runs the P9 walk ((a) scroll bars, (b) close button) after the touch-size walk, and
  runs the touch-size walk again when that changed a bar (the cap search measures the window as it is), then
  (f) below, then decides the fit. A dialog's bars are their final thickness from its first layout.
- (f) `HoldHiddenListWidth`: at the first placement, every list (a) widened inside a group hidden at that
  moment (the outermost hidden element; not a tab's page, which TabElement already sizes by) is measured with
  that group shown; the group's parent (the page) gets that width as its INSTANCE minimum width, so the dialog
  is laid out at that width, centred, from its first frame. Width only (the bars add no height). Records
  are dropped when the dialog closes, and with the row off the description's minimum is put back
  (`ReleaseHeld`, once a frame in `TouchFrame`).
- Row off (every desktop by default) or the faithful profile: `TouchFirstPlace` returns at once, nothing is
  held, `ReleaseHeld` has no records: retail's sizes.

## 5. After (S25, debug apk of this branch, trace build; then the trace-free build)

Every dialog: one LAYOUT / DRAW rectangle and scale from the frame it opens to the end of the run.

| dialog | drawn from frame 1 | log |
|---|---|---|
| Options, front end | 243 15 740 438, k 1 (cap 305, 423 of 423) | run5 / run6 |
| Load, front end | 225 19 849 477, k 0.913 (`mainPage held at 608`) | run6, final_load |
| Mods, front end | 287 20 951 550, k 0.792 | run7 |
| Fixes and Extensions, front end | 159 15 823 438, k 1 (cap 243) | run8 |
| entity panel (house, top-clamped) | 405 15 809 235 (laid out at 472 140, moved by the tool's SetUpperLeft before the draw, as in P11b) | run9 |
| pause menu | 228 161 751 369 | run10 |
| Options, pause menu | 243 15 740 438, k 1 | run11 |
| Save, pause menu | 225 19 849 477, k 0.913 | run11 |

Screenshots and logs: session scratchpad `p11dframe/` (`run*.png`, `run*_error.log`). Not in the tree.

Windows: debug build and `tools/build.sh unit` pass (1551 tests, 0 failures, 1 skipped: the kit cross-check, no
kit output in this worktree). No Windows game run (the coordinator's gate); with the row off the new code
does nothing. Linux / macOS not built here: portable C++ in `src/ext/phone_menu/`, no platform branch. No unit
test: the change walks a live element tree (no pure arithmetic was added).

## 6. Rulings owed

1. **A held page does not shrink back.** With (f), a page whose hidden group holds a widened list keeps that
   width while the dialog is open, also when another group is shown in its place. On the Save / Load dialogs
   this is the width retail has too (the list page is the page); an entity panel's `pages` element was held at
   388 (`pages held at 388 units wide`), the width its list page lays it out at anyway (the panel is 404 wide
   as before). A panel whose other pages are narrower than its list page now keeps the list page's width
   (retail would narrow it). Taken as the least surprising (no resize); flag if a panel looks too wide.
2. **Mods dialog scale** (k 0.792, its list not capped): steady from the first frame, but drawn small; the
   cause (a hidden Browse page's list read for its uninitialised `_layoutHeight` in the cap search) is the
   p11dbuttons lane's (`touch_scroll.cpp`), reported to it.
