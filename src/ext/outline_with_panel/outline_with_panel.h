#pragma once
// ============================================================================
// src/ext/outline_with_panel/outline_with_panel.h -- fix.outline_with_panel (fix tier, default ON)
// ============================================================================
// NOT RETAIL (docs/retail_bugs.md §16, docs/extensions/handins/OUTLINEWITHPANEL.md). Retail's
// GameUpdateLoop::Update runs the UI's Update (step 9, which takes the dialogs added since the last one
// into its list and lays them out) BEFORE the tool manager's (step 10, where a click selects an entity:
// its outline goes into the scene and its info panel is added to the UI). That frame draws the outline
// and not the panel, which the UI takes in only at the next frame's step 9: the outline shows for one
// frame where the panel is about to appear. Invisible at a desktop's frame rate, plain at 60 fps on a
// phone (measured on the S25: the stockpile's outline 16.7 ms before its panel).
//
// With the row on, a frame whose tool update left a dialog waiting to be taken in by the UI holds back
// the edge-mesh outlines the previous frame did not draw: they are skipped at the gather
// (Graphics::EdgeMeshInstance::AddDrawItems, through the engine seam _extHoldDraw) for that one frame
// and drawn from the next, with the panel. Outlines already on screen are not touched; nothing changes
// on a frame that opened no dialog. Render only: the scene, the sim and the save are retail's.
//
// Threads: AfterToolUpdate runs on the main thread (GameUpdateLoop::Update); HoldDraw runs in the
// frame's gather, which is on the main thread or, with ext.gather_threads, on its helpers in parallel
// (a lock), never at the same time as AfterToolUpdate. ext.render_pipe's emit reads the gathered lists,
// not this record.
// ============================================================================

namespace Graphics {
class EdgeMeshInstance;
}

namespace Ext {
namespace OutlineWithPanel {

// GameUpdateLoop::Update, after step 10 (the tool manager's Update), every frame: starts this frame's
// record; `dialogPending` = the UI has dialogs added and not yet taken in
// (UI::UserInterface::GetAddDialogCount() > 0). The frame holds when the row is on and one is.
void AfterToolUpdate(bool dialogPending);

// The engine seam Graphics::EdgeMeshInstance::_extHoldDraw: true = skip `outline` in this gather.
bool HoldDraw(const Graphics::EdgeMeshInstance* outline);

// Boot (registry_boot.cpp): installs the seam, null under the faithful profile (the profile is fixed
// for the run; the hook reads the row's live bool each frame).
void Sync();

}  // namespace OutlineWithPanel
}  // namespace Ext
