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
namespace Graphics { class Camera; }

namespace Ext {
namespace ShowGrid {

// Called from the ground draw (src/ext/ground_draw.h: inside Renderer::Draw, after the opaque list of the main
// scene) while the row is on and the box checked, with that scene's camera set on the context and passed as
// `camera`. The tool and the UI draw later, over the grid.
void Draw(Video::Context* context, const Graphics::Camera& camera);

// Whether the grid is drawn: the Grid page's check box. Runtime only (no value key, nothing in settings.cfg):
// the row on does not show it, the box does.
bool Shown();
void SetShown(bool shown);

// A new game or a load starts with the grid hidden, whatever the last game left and whether or not the row
// was on at the start. The look settings (opacity, hue, ...) are kept.
void OnGameStart();

}  // namespace ShowGrid
}  // namespace Ext
