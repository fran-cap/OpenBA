# DIALOGPOSITIONDRIFT: a panel at the screen's edge no longer jumps for a frame (`fix.dialog_position_drift`, 2026-10-26)

> **Built: `fix.dialog_position_drift` (tier 1, default ON, off under `/profile:faithful`), UI only.**
> A dialog moved after its layout (an entity's info panel, placed above its building every frame) is
> moved from where the layout actually put it. Retail moves it from a stored position that the layout's
> pull-back at the screen's far edge left stale, so a panel whose layout crossed the bottom edge is drawn
> a few units off for one frame, then snaps back.

Retail entry: `docs/retail_bugs.md` §13. Inventory row F13. Found on the Galaxy S25 in P11b
(`docs/android/P11B_DIALOG_SNAP.md` cause 3); the owner ruled a fix row for every platform
(`docs/runtime_port/NEXT_SESSION.md` §2.11, "drift fix good -> a `fix.*` row for desktop YES").

## 1. What changes, and where

| Site | Retail | With the row on |
|---|---|---|
| `DialogInstance::SetUpperLeft` (0x180114a60, `src/engine/ui/dialog.cpp`), before its delta | `dx = x - _upperLeftX`, `dy = y - _upperLeftY` from the stored upper-left | `_upperLeftX` / `_upperLeftY` first set to the root's laid-out left / top less its pads, on the axes the root's alignment anchors left / top (`drift_math.h` `LaidOutUpperLeft`); the rest is retail's |

No engine seam of its own. The call point is `UI::UserInterface::_extDialogPlace` at `kExtPlaceUpperLeft`
(added by ext.phone_menu in P11b, called by SetUpperLeft before it sizes the root). That pointer is
ext.phone_menu's `Place` (`src/ext/phone_menu/phone_menu.cpp`), installed under every profile except
faithful whether that row is on or off; `Place` hands the call to `Ext::DialogPositionDrift::OnSetUpperLeft`
before its own row's test, so the fix is independent of ext.phone_menu (DESIGN.md §3 Names table, "a fix on a
seam another row owns"). Not for a dialog shown at its own scale (k != 1: ext.touch_mode's toolbar, a
window ext.phone_menu fits), whose position is in its own units and which those rows place themselves.

ext.phone_menu (P11b) needs the same sync for the dialogs it keeps in its safe area. With the fix on the
sync runs once, from the fix; with the fix off, phone_menu's own arm calls `SyncUpperLeft` for its dialogs.
The sync is a store of a value read from the root, so a second call is a no-op
(`ext_dialog_position_drift_sync_is_idempotent`).

Code: `src/ext/dialog_position_drift/dialog_position_drift.{h,cpp}` (the call point), `drift_math.{h,cpp}`
(the arithmetic, moved here from `phone_math.h`, where P11b had it).

## 2. When retail's drift shows

ComputeLayout lays the root out at `_upperLeft` and pulls a dialog that crosses the far edge back onto the
screen; SetUpperLeft clamps with the size it measures itself. The two disagree when the size at
SetUpperLeft is not the size the layout then uses: a panel placed before its controller filled it (the
phone, P11b: measured 336 x 88, laid out 404 x 220), or a panel that grows while it is held against the
bottom edge (a row added, a page switched). On a desktop the harness panels measured their final size at
their first SetUpperLeft (the house panel 368 x 196 and a citizen's 370 x 218 at UI 83 on 2528 x 1345,
`BANISHED_PANEL_HOLD`), so the drift did not show there; it needs a panel that grows against the edge.

## 3. Save, sim and perf

- Save / sim: none. Only where a dialog is drawn changes, and only for one frame.
- Perf: one bool test per SetUpperLeft with the row off; four stores with it on.

## 4. Tests and gates

- `tests/unit/ext_dialog_position_drift_test.cpp`: the S25 trace's numbers (placed 216, laid out 213 against
  the bottom margin), pads, the centred / right-anchored axes left alone, a root not laid out yet, an
  alignment out of range, and the sync's idempotence.
- `tests/unit/ext_registry_test.cpp` `ext_registry_table_shape`: the row is a fix, no class, default ON.
- Row icon `Entry_dialog_position_drift.png` (`tools/ext_icons/fixes.py` `draw_drift`, baked by
  `tools/ext_sprites_bake.py`).
- The gates run `/profile:faithful`, which forces the row off and leaves the seam null: retail's path.
