# Android P11b: a dialog opened on a building snaps a few frames later (2026-10-26)

NOT RETAIL. Session log of `docs/runtime_port/NEXT_SESSION.md` §2.10 ("a dialog opened on a building appears
ABOVE its place and snaps down a few frames later"), lane `p11bsnap`, branch based on `android/p11` (c3093d522).
Lasting facts are in `docs/extensions/handins/PHONEMENU.md` §3 (the `kExtPlaceUpperLeft` call point) and the
headers (`phone_math.h` `SafeBounds` / `LaidOutUpperLeft`, `touch_fit.h` `TouchFirstPlace`). Phone: Galaxy S25,
UI 42 (982 x 453 layout units, safe margin 20), phone_menu, touch_mode and touch_pointer on, real `adb shell input
tap` touches.

## 1. How it was measured

Screenshots (`adb exec-out screencap`) take about half a second each on the S25: too slow to catch a one-frame
snap. A temporary trace (removed before the commit) printed, per frame, each new dialog's `_upperLeft` and its
root's laid-out rectangle before and after `UserInterface::Update`'s layout (`_extDialogPlace` at
`kExtPlaceLayout` / `kExtPlaceAfterLayout`) and after `EditTool::Update`'s `SetUpperLeft` (the per-frame
placement of an unpinned entity panel: its bottom on the entity's screen top, `edittool.cpp`). The frame order is
`UserInterface::Update` (layout), then the tools (`SetUpperLeft`), then the draw, so the rectangle after the
tool's call is what is drawn that frame.

## 2. Before: what is drawn, frame by frame (layout units, the root's {left, top, right, bottom})

A house whose panel is clamped at the top of the screen (tap at 1440,573):

| frame | drawn root | note |
|---|---|---|
| 1 | 405 0 809 208 | placed by the tool at retail's clamp (top 0), the panel at retail size (208 high) |
| 2.. | 405 20 809 240 | the safe area's floor (20) and the touch-size walk (208 -> 220): down 20, bottom down 32 |

A house lower on the screen (tap at 1123,760), panel first laid out against the bottom margin:

| frame | drawn root | note |
|---|---|---|
| 1 | 271 53 675 261 | retail size (208) |
| 2.. | 271 42 675 262 | the walk grows it to 220: top up 11 |

(A second run of the same house: frame 1 at top 33, then 44: down 11.)

## 3. Causes (all three in the placement path; retail's own pieces are faithful)

1. **The touch-size walk ran a frame late** (suspect 1). `TouchSizeWalk` (P11) walked only dialogs the place hook
   had already seen, and it runs at the HEAD dialog's layout, before a new dialog is reached in the list: a new
   dialog was laid out once at retail's size and grew the next frame. The panel's height changed between its
   first and second drawn frame (208 -> 220), and the tool, which keeps its bottom on the building, moved it.
   Combo drop-downs (rows padded by the walk) had the same one-frame growth.
2. **SetUpperLeft clamps to the whole screen, the layout to the safe area.** `phone_menu.cpp` lays every
   non-menu dialog out in the safe area (the near edges a floor: a dialog at y = 0 is laid out at 20). The entity
   panel is placed AFTER the layout, by `DialogInstance::SetUpperLeft`, which clamps into `{0, 0, w, h}` and moves
   the laid-out tree: the first drawn frame sits at retail's clamp (0), the next layout puts it at the margin (20).
   This is the reported "above its place, then snaps down".
3. **SetUpperLeft moves the tree by the delta from `_upperLeft`, not to it.** When the layout did not put the
   root at `_upperLeft` (ComputeLayout pulls a dialog crossing the far edge back; the safe floor raises one in the
   margin), the move lands the root off by that difference for one frame. The panel's first `SetUpperLeft` comes
   before its controller has filled it (it is measured 336 x 88, then laid out 404 x 220), so its first layout
   crosses the bottom margin and is pulled back (by 3 to 11 units in the runs above); the tool's next move kept
   that offset until the next layout. This one is RETAIL's mechanism (SetUpperLeft 0x180114a60 and ComputeLayout
   0x180113d90 are faithful): with the row off it shows whenever a panel's first layout crosses the screen's
   bottom edge (rare at a desktop UI scale, where the first size rarely reaches the edge). It is only changed with
   ext.phone_menu on (below); a desktop fix would be a `fix.*` row (ruling owed, §6).

Suspects 2 (the per-dialog fit deciding a frame late) and 3 (`Place` anchoring at `kExtPlaceLayout` with a size
that changes at `kExtPlaceAfterLayout`) were not the cause for these dialogs: k stayed 1.000 on every traced frame
and no AfterLayout move touched them. Suspect 2 had the same one-frame shape as cause 1 for a tall dialog (the fit
judged its retail size on its first frame); it is closed by the same change (the fit is decided again right after
the first walk).

## 4. The change (root cause: the first frame's layout and placement are the final ones)

- `touch_size.cpp` / `.h` (the touch-size agent's file this round; minimal, in its own commit): `TouchKnown`,
  whether the place hook has seen a dialog. No behaviour change there.
- `phone_menu.cpp` `Place`: a dialog the hook has not seen yet is at its first placement (`kExtPlaceUpperLeft`) or
  first layout; once it is known not to be ext.touch_mode's, `TouchFirstPlace` (`touch_fit.cpp`) runs its touch-size
  walk and decides the fit again, BEFORE it is first sized. `TouchSeen` is now called on the main thread's points
  only (it was also called from the draw's backdrop points).
- Engine seam `UserInterface::kExtPlaceUpperLeft` in `DialogInstance::SetUpperLeft`: the clamp rectangle, retail's
  `{0, 0, w, h}` (same four compares in the same order) unless the hook answers. `Place` answers the safe area the
  layout uses (`SafeBounds`), after setting `_upperLeft` to where the root was actually laid out
  (`LaidOutUpperLeft`, on the axes the root's alignment anchors top / left). Not for the menus, the version line, a
  screen-sized root, a dialog at its own fitted scale, nor a dialog ext.touch_mode places (it answers first).
- Row off (every desktop by default) or the faithful profile: the hook returns before any of this (null under
  faithful), so SetUpperLeft is retail's.

## 5. After: measured on the S25 (same taps, debug apk of this branch)

| dialog | drawn frames after the tap | snaps |
|---|---|---|
| house panel, top-clamped (1440,573) | 407 20 811 240 from the first drawn frame on | no |
| house panel, lower (1123,760) | 267 40 671 260 from the first drawn frame on (first layout pulled back to 213, the move lands on 40) | no |
| citizen panel (the `>` of an occupant row) | 290 20 690 262 from the first drawn frame on (first measured 326 x 52, laid out 400 x 242) | no |
| sub-toolbar (Housing) | 235 174 381 232 from its first layout | no (ext.touch_mode's; unchanged) |
| touch_pointer action bar (Place / Rotate / Flip / Cancel) | 660 175 962 253 from its first layout | no (unchanged) |
| the tool's CreateBar (placing a Wooden House) | 293 x 40 from its first layout; it eases toward the cursor (retail's smoothing), no size change | no |
| event notices | not triggered in the runs; placed by ext.touch_mode every AfterLayout (its slide), not walked: not touched by this change | not measured |
| tooltips | a finger never shows one now (P11 ruling, hidden on lift); a mouse tooltip is placed by SetUpperLeft at the cursor and goes through the new clamp | not measured with a finger |

Screenshots: the session scratchpad's `p11bsnap/` (`j_3.png` the top-clamped panel, `final_3.png` the trace-
free build, `m_3.png` the sub-toolbar); traces `trace_a.txt`, `trace_b.txt`, `trace_d.txt` (before),
`trace_c.txt`, `trace_g.txt`, `trace_j.txt`, `trace_k.txt`, `trace_n.txt` (after). Not in the tree.

Windows: debug build and `tools/build.sh unit` pass (1539 tests, 0 failures, 1 skipped: the kit cross-check, no
kit output in this worktree). No Windows game run (the coordinator's gate); with the row off the seam answers
nothing. Linux / macOS not built here: the change is in shared engine / ext code with no platform branch.

## 6. Rulings owed

- Cause 3 is retail's: a panel whose first layout crosses the screen's bottom edge is drawn one frame off on a
  desktop too. Left retail with the row off. A `fix.*` row (class A) could carry `LaidOutUpperLeft` for every
  profile but faithful; not built.
- With the row on, a dragged panel (`ToolUI`'s drag, SetUpperLeft) now stops at the safe margin instead of
  entering it and being pushed back by the layout (the old "dead zone"). Taken as the least surprising; flag if
  a panel must still be draggable into the margin.
