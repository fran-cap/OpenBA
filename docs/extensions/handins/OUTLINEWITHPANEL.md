# OUTLINEWITHPANEL: a selection outline appears with its panel (`fix.outline_with_panel`, 2026-10-31)

> **Built: `fix.outline_with_panel` (tier 1, default ON, off under `/profile:faithful`), render only.**
> On the frame a click selects an entity, retail draws the entity's new outline but not its info panel,
> which appears one frame later. With the row on, the new outline is held back for that one frame and
> appears with the panel.

Retail entry: `docs/retail_bugs.md` §16. Inventory row F15. Found on the Galaxy S25 (60 fps, a screen
recording of a tap on a stockpile: the yellow outline alone for one frame, 16.7 ms, then the panel over it).

## 1. What changes, and where

| Site | Retail | With the row on |
|---|---|---|
| `GameUpdateLoop::Update`, after step 10 (the tool manager's Update, retail 0x1400965a0-0x1400965ed) | nothing | `Ext::OutlineWithPanel::AfterToolUpdate(GetAddDialogCount() > 0)`: starts the frame's record and holds when the tool left a dialog waiting for the UI |
| `Graphics::EdgeMeshInstance::AddDrawItems` (0x1800a2fd0, `src/engine/graphics/mesh.cpp`) | the triangle and edge lists added to the renderer | engine seam `_extHoldDraw` asked first: on a holding frame an outline the previous frame did not draw adds no items |

Outlines already on screen are never held; a frame that opened no dialog holds nothing. The record
(`src/ext/outline_with_panel/hold_set.h`, 64 outlines; a full record holds nothing) is written by the
gather, on the main thread or on ext.gather_threads' helpers (a lock). ext.render_pipe's emit reads the
gathered lists, not the record, so the hold is decided on the main thread for each packet.

With ext.render_pipe's overlap (P13), the UI half of a packet is drawn after the next Frame's steps, so a
panel can already show on the packet whose scene holds the outline back: the panel then leads the outline
by a frame, which is the order a UI-over-scene frame shows anyway.

Code: `src/ext/outline_with_panel/outline_with_panel.{h,cpp}` (the call points), `hold_set.h` (the record).

## 2. Save, sim and perf

- Save / sim: none. Only which frame first draws an outline changes.
- Perf: with the row on, one uncontended lock per outline per gather; off (the faithful profile) the seam
  is null.

## 3. Tests and gates

- `tests/unit/ext_outline_with_panel_test.cpp`: a new outline held on the click frame and drawn the next,
  nothing held without a pending dialog, two holding frames in a row, a full record.
- `tests/unit/ext_registry_test.cpp` `ext_registry_table_shape`: the row is a fix, no class, default ON.
- Row icon `Entry_outline_with_panel.png` (`tools/ext_icons/fixes.py` `draw_outline_panel`, baked by
  `tools/ext_sprites_bake.py`).
- On the S25 (debug apk, Auto save): the stockpile's outline and panel first drawn in the same frame.
- The gates run `/profile:faithful`, which forces the row off and leaves the seam null: retail's path.
