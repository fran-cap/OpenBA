// ============================================================================
// src/ext/sharp_ui/sharp_scaled.h -- ext.sharp_ui's magnified sprite sheets,
// the engine half (NOT RETAIL; docs/extensions/handins/SHARP_SPRITES.md)
// ============================================================================
// sharp_sheet.h has the arithmetic and sharp_encode.h the BC3 encode; this file
// reads a sheet back from the GPU, builds the scaled atlas with our art over
// it, and makes its texture and material. Two sheets have art:
//   Dialog            the game's Dialog/SpriteSheet.rsc (the player's own sheet
//                     upscaled, our menu chrome and icons over it);
//   Extensions        our Dialog/ExtensionsSprites.rsc (built in the exe; the
//                     main-menu gear, the toolbar cog and the phone menu's
//                     Game files floppy drawn at the factor, the rest upscaled).
// Factors 2 and 4 are baked (sharp_sprites_data_2x.h / _4x.h). The 2x copy is
// one BGRA8 level, as the sheet itself; the 4x copy is BC3 with a full mip
// chain (64 MB as BGRA8, ~21 MB so), encoded once and kept in the derived-data
// cache (ext/derived_cache), keyed by the built atlas's bytes. A GPU without BC
// (Android) gets it as ETC2 through the Vulkan backend's transcode of BC3.
// THE BUILD IS OFF THE FRAME. The first selection that wants a copy reads the
// sheet back and starts a worker thread on copies of it (the upscale, our art,
// the BC3 chain or its cache read) and draws retail's sheet meanwhile; the
// frame poll (ScaledSheetJobFinished) asks for a selection again once a worker
// is done, and that selection makes the texture and the material on the main
// thread and installs the copy.
// Exe only (the video device and the factory).
// ============================================================================
#pragma once

namespace UI { class SpriteSheet; }
namespace Graphics { class MaterialInstance; }

namespace Ext {
namespace SharpUI {

enum class SheetKind {
    None,
    Dialog,       // Dialog/SpriteSheet.rsc
    Extensions,   // Dialog/ExtensionsSprites.rsc
};

// The sheet a resource path names ("<path>:<object>" accepted), or None.
SheetKind KindOfSheet(const char* path);

// The factor of the copy to draw `pxPerUnit` device pixels per layout unit
// from (PickSheetFactor over the baked factors), or 0: retail's sheet.
int PickSheetScale(float pxPerUnitX, float pxPerUnitY);

// The material that draws `sheet` from its copy `factor` times larger, built on
// first use and kept for the sheet's texture's life; null when it cannot be
// built (a texture that is not plain BGRA8 -- a mod's -- or a failed read back,
// logged once as `[SharpUI]`).
// Null also while its build is still on the worker (retail's sheet is drawn).
Graphics::MaterialInstance* ScaledSheetMaterial(UI::SpriteSheet* sheet, SheetKind kind, int factor);

// The boot prewarm (sharp_ui.h Prewarm): for each loaded sheet we have art for, start (or finish) its
// copy at `factor` once; a sheet already asked for at that factor is skipped without a read back.
void PrewarmScaledSheets(int factor);

// Per frame (cheap: one atomic per build in flight): true once for every build
// that has finished since the last call; the caller selects again.
bool ScaledSheetJobFinished();

// Load time only (the menu's Startup): waits for every build in flight, at most `capMs`.
void WaitForScaledSheets(int capMs);

// After a selection: builds the poll saw finished but that selection did not
// claim (a factor or a sheet no longer wanted) are dropped. A build that
// finished during the selection is kept for the next poll.
void DropSeenJobs();

// The video device is going away (Engine::MainLoop::OnVideoError: a renderer or display change in the
// Options dialog, a lost device). Every copy built so far is deleted while its device still lives, and
// the failures are forgotten (they named sheets the restart reloads): a copy is a texture made at run
// time, which the engine's restart cannot reload by path, and the content key alone would hand the dead
// copy back for the reloaded sheet. Builds still on a worker are kept (CPU data; finished on the new
// device). Main thread.
void DropScaledSheets();

}  // namespace SharpUI
}  // namespace Ext
