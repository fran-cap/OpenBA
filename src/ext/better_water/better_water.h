// ============================================================================
// src/ext/better_water/better_water.h -- ext.better_water (class A, default ON)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERWATER.md.
//
// A shore edge on the game's water: a shallow tint next to the bank and a thin, broken foam line
// just below the waterline. Retail's water draw is untouched; each water range is drawn a second
// time over it with our own pixel program (src/render/common/better_water/shore_overlay.hlsl) in
// place of the water's, keeping the water material's vertex layout, vertex program and render state
// (the user's installed ones). The shore depth is the frame copy's alpha, which retail's terrain and
// in-water models already write, so nothing new is rendered or bound for it.
//
// Plain is this pass alone and runs on every backend (Android's choice for power). Fancy, later,
// adds what needs a readable depth or the terrain shader (contact foam for models that write no
// frame alpha, a wet line on the bank); it will be a valued key of this row.
//
// THE SEAM is WorldGeometry's contour-range draw (geometry_draw.cpp, modes 0x6000 / 0x8000): right
// after retail's DrawIndexed, `if (Enabled()) DrawShore(...)`. A few water ranges per frame; the
// faithful path pays one load per range. Range A (0x6000) holds the flow bodies (rivers, the coast)
// drawn with the river material, range B (0x8000) the shore rings round lakes and ponds drawn with the
// lake material (water_contour_mesh.cpp); the two scroll their uvs differently, so each gets its own
// shore program (Kind).
// ============================================================================
#pragma once

#include <cstdint>

namespace Video { class Context; }

namespace Ext {
namespace BetterWater {

// The row (ext.better_water), as the Extensions dialog and the Enhanced tab leave it. Read per
// water range on the drawing thread: a plain bool load.
bool Enabled();

// Which water the range is: picks the shore program whose still-coordinate blend matches the
// material's uv scroll (shore_overlay.hlsl MeshCoord). A mod that swaps the two materials gets a
// slowly drifting pattern, nothing worse.
enum class Kind : int32_t { River = 0, Lake = 1 };

// Draws the range retail just drew once more with the shore program, then rebinds the water's own
// program, so the next draw item finds the context as retail left it. a..d are retail's
// Context::DrawIndexed arguments for the range (primitive type 0, instance count 1). Does nothing
// (and logs once) when the program could not be made on this device.
void DrawShore(Video::Context& context, Kind kind, int32_t a, int32_t b, int32_t c, int32_t d);

}  // namespace BetterWater
}  // namespace Ext
