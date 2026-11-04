// ============================================================================
// src/ext/sharp_ui/sharp_scaled.h -- ext.sharp_ui's magnified dialog sheet,
// the engine half (NOT RETAIL; docs/extensions/handins/SHARP_SPRITES.md)
// ============================================================================
// sharp_sheet.h has the arithmetic; this file reads the player's own sheet back
// from the GPU, builds the scaled atlas with our art over it and makes its
// texture and material. Exe only (the video device and the factory).
// ============================================================================
#pragma once

namespace UI { class SpriteSheet; }
namespace Graphics { class MaterialInstance; }

namespace Ext {
namespace SharpUI {

// The sheet our art is drawn for: the game's dialog sprites.
bool IsDialogSheet(const char* path);

// The factor of the art set to draw `pxPerUnit` device pixels per layout unit
// from, or 0 (no art set fits: retail's sheet). Only 2 ships; a 4x set is a
// second sharp_sprites_data header and one more arm here (sharp_sheet.h).
int PickSheetScale(float pxPerUnitX, float pxPerUnitY);

// The material that draws `sheet` from its copy `factor` times larger, built on
// first use and kept for the sheet's texture's life; null when it cannot be
// built (a texture that is not plain BGRA8 -- a mod's -- or a failed read back,
// logged once as `[SharpUI]`).
Graphics::MaterialInstance* ScaledSheetMaterial(UI::SpriteSheet* sheet, int factor);

}  // namespace SharpUI
}  // namespace Ext
