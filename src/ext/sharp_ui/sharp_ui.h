// ============================================================================
// src/ext/sharp_ui/sharp_ui.h -- ext.sharp_ui (class A, default OFF)
// ============================================================================
// NOT RETAIL. "Sharp UI scaling": crisp text at any UI scale. The Options
// slider means what it always meant (_uiScale = slider / 100 on every path,
// retail's line); layout is retail's. While the extension is on, for the
// duration of retail's UserInterface::Draw:
//   1. text is drawn from FONT VARIANTS: the game's four fonts (Font,
//      FontSmall, FontMedium, FontLarge) compiled by the mod kit at the pixel
//      height the screen needs (src/ext/ui/Font/SharpFont*.rsc, sharp_math.h:
//      the set and the pick; the font itself when the pick is native). Each
//      glyph is drawn one texel per device pixel on the base glyph's baseline,
//      the glyphs of a word stepping by the variant's own advances from the
//      word's snapped base pen (ui/SharpDraw.h, the word-run banner;
//      SHARP-SPACING.md); the base font still lays out every string, so
//      wrapping, widths and hit-testing are unchanged;
//   2. a sprite sheet `X.rsc` with a deployed `X2x.rsc` twin is drawn from
//      the twin while the UI is magnified (none ships: SHARPUI.md §12); the
//      game's dialog sheet (Dialog/SpriteSheet.rsc) and our Extensions sheet
//      are otherwise drawn from a copy 2 or 4 times their size BUILT AT LOAD
//      from the sheet itself, with our art over it (sharp_scaled.h,
//      sharp_sheet.h; docs/extensions/handins/SHARP_SPRITES.md);
//   3. every axis-aligned UI quad's corners land on whole device pixels;
//   4. at an integer ratio >= 2 (unreachable from the 75-150% slider) pixel-
//      exact batches are point-sampled.
// The engine end is UI::SpriteContext::_extSharp (ui/SharpDraw.h), null
// outside this window and always in the faithful profile.
// Class A: no serialized state, no sim effect, live.
//
// The seams (each `if (Ext::Ext().sharp_ui) <ours> else <retail's line>`):
//   GameUpdateLoop::Draw step 12 / MenuDialogLoopT::Draw: `Draw(ui, ctx)`
//     around retail's UserInterface::Draw, which runs unchanged inside it
// TRACE (stderr, standalone_error.log), one line when the extension engages
// (the first frame it draws after a frame it did not) and when _uiScale or
// the back buffer changes:
//   [SharpUI] on scale=<_uiScale> px/unit=<r> fonts: Font-><px> FontSmall-><px>
//             FontMedium-><px> FontLarge-><px> 2x=<twins in force>
//             sheet=<the factor of the built copies drawn, 0 = retail's>
//   (<px> is the variant drawn; "native" = the retail font at ratio 1,
//    "retail(<why>)" = the retail font because no variant could be used)
//   and once per built copy of a sheet:
//   [SharpUI] <f>x <dialog|extensions> sheet built: <w> x <h> <BGRA8 | BC3 + mips, cached
//             | BC3 + mips, encoded>, <n> of <count> sprites from our art, <ms> ms
// ============================================================================
#pragma once

namespace UI { class UserInterface; }
namespace Video { class Context; }

namespace Ext {
namespace SharpUI {

// Once per frame, in place of `ui.Draw(context)`: (re)selects the font
// variants and 2x twins when the scale, the back buffer or the resource
// factory's generation changed, sets the engine-side pointer, runs retail's
// UserInterface::Draw, clears it and the point-sampling flag (forcing the
// texture slots to rebind). Call only when Ext::Ext().sharp_ui.
void Draw(UI::UserInterface& ui, Video::Context& context);

// The boot prewarm (the loading splash's Update, every frame while it shows): at "Text + icons", start
// the workers of the sheet copies the menu will want (sharp_scaled.h), for the UI scale in the settings,
// as soon as each sheet exists, so the menu's first frame finds them built. Cheap once started.
void Prewarm();

// The menu's Startup, after its dialogs loaded (our Extensions sheet is provided then, after the splash):
// Prewarm once more and wait for the builds (at most 2 s), so the menu's first frame draws every copy.
void PrewarmAndWait();

// Engine::MainLoop::_extVideoLost (set at boot, src/ext/registry_boot.cpp): the video device is going
// away. The sheet copies are dropped (sharp_scaled.h DropScaledSheets) and the selection is made again
// at the next Draw, which starts their rebuild on the new device (retail's sheet drawn meanwhile).
void OnVideoLost();

}  // namespace SharpUI
}  // namespace Ext
