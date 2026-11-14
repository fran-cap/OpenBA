// ============================================================================
// src/ext/terrain_late/terrain_late.h -- ext.terrain_late (class A): the terrain after the opaque objects
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TERRAINLATE.md; the pure rules are terrain_late_rules.h.
//
// The terrain's pixel program is costly per pixel (several texture reads under the player's anisotropic
// filter and a filtered shadow lookup, over the whole screen), and retail draws it FIRST in the Opaque list, so
// every terrain pixel a building, a tree, a rock or a citizen covers later is shaded and then
// overwritten. Drawn LAST, the hardware's early depth test rejects those pixels before the program runs.
// On the stress town fully zoomed out 42% of the terrain's pixels are covered that way (handin section 3).
//
// TWO SEAMS, both null by default and under the faithful profile (set once by Sync):
//   Graphics::Renderer::_extOpaqueOrder   Renderer::Draw's pass 1 (and the ext.render_pipe copy) hands
//       it the main entry's sorted Opaque list (shader Normal, list 0). OrderOpaque moves the terrain's
//       items behind the other items (terrain_late_rules.h MoveMatchingToBack). The shadow and the
//       reflection entries, and the Blended and third lists, keep retail's order.
//   WorldGeometry::_extTerrainLate        the row's live bool. WorldGeometry::Draw's terrain chunk / run
//       draw in the Normal shader calls BindLateState after the material's Setup: the terrain's own
//       render state with its less-or-equal test made less, so a depth tie stays the object's
//       (terrain_late_rules.h section 2).
//
// Threads: OrderOpaque runs on the thread that runs pass 1 (the main thread), BindLateState on the
// drawing thread (ext.render_pipe's render thread when it is on). A toggle between a frame's pass 1 and
// its pass 2 (ext.render_pipe) can pair the order of one setting with the state of the other for that
// frame; either pairing differs from retail only at depth ties.
// ============================================================================
#pragma once

#include <cstdint>

class WorldGeometry;
namespace Graphics { class RendererDrawItem; }
namespace Video { class Context; }

namespace Ext {
namespace TerrainLate {

// Boot (InitFromSettings): installs or clears both seams. Under the faithful profile both stay null.
void Sync();

// The _extOpaqueOrder body: while the row is on, the terrain items of `items` move to its end.
void OrderOpaque(Graphics::RendererDrawItem* items, int32_t count);

// WorldGeometry::Draw, a terrain chunk or run item of the Normal shader, after the material's Setup:
// while the row is on, the bound terrain state is replaced by its less-test copy.
void BindLateState(Video::Context& context, const WorldGeometry& geo);

}  // namespace TerrainLate
}  // namespace Ext
