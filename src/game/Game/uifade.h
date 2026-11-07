#pragma once
// src/game/Game/uifade.h
// Fullscreen-fade overlay helpers shared between standalone exe and DLL hook
// builds. The original Engine::DialogLoaderLoopT (loading screens) and
// MenuDialogLoopT (main menu) trigger UI cross-fades in their Startup paths
// via UI::UserInterface::SetFullscreenFade. Standalone bypasses those code
// paths today, so transitions hard-cut. These helpers add the fade calls
// alongside the existing Stub_Startup / Stub_Update implementations.
//
// The actual fade animation runs INSIDE Runtime-x64.dll's UI::UserInterface
// (Update advances the alpha each frame, Draw renders a fullscreen sprite
// modulated by the alpha). Standalone only needs to:
//   1) install the fade sprite once (SetFullscreenFadeSprite from KeyMap+0xA8)
//   2) trigger the fade-in/fade-out at the right moments (SetFullscreenFade)
//   3) optionally observe alpha at +0xF4 for log-based verification
//
// See decomp of UI::UserInterface::Update (Runtime 0x1801213C0) — fade math:
//   if (rate[+0xF0] != 0) {
//       alpha[+0xF4] += dtSec * rate * 4.0f;     // 1.0 reached in 0.25s
//       clamp [0..1]; when reached, rate := 0;
//   }
// And UI::UserInterface::Draw (Runtime 0x180121890) — overlay:
//   if (alpha != 0 && spriteSheet[+0xE0] && spriteIdx[+0xE8] >= 0) {
//       drawFullscreenSprite(color | (int(alpha*255) << 24));
//   }

#include <cstdint>
#include "system/Value.h"   // System::kColorBlack (the default fade colour)

namespace UI { class UserInterface; }

namespace UIFade {

// Read the current fade alpha from a UI::UserInterface ([+0xF4]).
// 0.0 = no overlay, 1.0 = fully opaque overlay. Used for log-based verification.
float GetCurrentAlpha(const UI::UserInterface* userInterface);

// Read the current fade rate from a UI::UserInterface ([+0xF0]).
// 0.0 = no animation in progress, +1.0 = fading IN (toward opaque),
// -1.0 = fading OUT (toward transparent).
float GetCurrentRate(const UI::UserInterface* userInterface);

// Install the fullscreen fade sprite onto a UI::UserInterface, copied from
// KeyMap+0xA8 (the engine-wide white-pixel sprite reference loaded by the
// resource bootstrap). Without this, SetFullscreenFade triggers do not draw.
// Idempotent: re-calling is safe but redundant.
//
// Returns true if the sprite was installed (or was already non-zero), false
// if KeyMap singleton or required fn pointers are not yet resolved.
bool EnsureFadeSprite(UI::UserInterface* userInterface);

// Trigger the standard "Startup reveal" curve: instant fade IN to opaque
// black, then animated fade OUT (transparent) over ~0.25s. Mirrors the
// two-call pattern in OptionsDialog_impl_8fa0 (exe 0x140098FA0):
//   SetFullscreenFade(ui, true,  color, true);    // instant: alpha = 1.0
//   SetFullscreenFade(ui, false, color, false);   // animated: rate = -1.0
// Use this from each loop's Startup so the new loop's first frames are
// covered by the overlay then revealed.
//
// color: packed 0xRRGGBBAA. Pass 0xFF000000 for black (the game's default).
void TriggerReveal(UI::UserInterface* userInterface, uint32_t color = System::kColorBlack);

// Trigger a fade IN (toward opaque) over ~0.25s. Mirrors the call in
// FUN_140098DA0 (LoaderLoop::Update) when IsFinished first transitions:
//   SetFullscreenFade(ui, true, color, false);    // animated: rate = +1.0
// Use when leaving a loop (after fade completes the loop transitions).
void TriggerFadeIn(UI::UserInterface* userInterface, uint32_t color = System::kColorBlack);

// Diagnostic — log "[UIFade::tag] alpha=X rate=Y" exactly once per call (no
// throttling). Caller is responsible for throttling. Useful from Stub_Draw
// to capture the fade curve in the standalone log.
void LogState(const char* tag, const UI::UserInterface* userInterface);

} // namespace UIFade
