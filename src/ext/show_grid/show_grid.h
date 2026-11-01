#pragma once
// ============================================================================
// src/ext/show_grid/show_grid.h -- ext.show_grid (class A, off by default)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/SHOWGRID.md. Draws a grid of tile outlines over the map, so a player
// planning with the game paused can see where the squares are. A picture-only change: nothing is saved and
// nothing in the simulation reads it. It uses the game's own tile overlay renderer (the one that tints the
// tiles under a building ghost), so the grid lies on the terrain like those tiles do.
// ============================================================================

namespace Video { class Context; }

namespace Ext {
namespace ShowGrid {

// Called from the two tool-draw seams (GameUpdateLoop::Draw, ext.render_pipe's ToolDraw) while the row is on,
// with the world camera set, before the tool and the UI are drawn.
void Draw(Video::Context* context);

// Whether the grid is drawn: the Grid page's check box. Runtime only (no value key, nothing in settings.cfg):
// the row on does not show it, the box does.
bool Shown();
void SetShown(bool shown);

// A new game or a load starts with the grid hidden, whatever the last game left and whether or not the row
// was on at the start. The look settings (opacity, hue, ...) are kept.
void OnGameStart();

}  // namespace ShowGrid
}  // namespace Ext
