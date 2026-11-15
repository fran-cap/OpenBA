// ============================================================================
// src/ext/ambient_occlusion/ambient_occlusion.h -- ext.ambient_occlusion (class A, default ON)
// ============================================================================
// NOT RETAIL. docs/extensions/AO.md (the plan), docs/extensions/handins/AOCORE.md.
//
// Screen-space ambient occlusion (Intel ASSAO) multiplied into the finished
// opaque colour of the game's main scene, between the Opaque and Blended lists.
//
//   Sync()     boot (Ext::InitFromSettings) and every Extensions-dialog toggle
//              (Ext::SetAndRecord): decides Enabled() = the row && feature level
//              11_0, and asks the backend for the matching main-depth format
//              (a format change runs the existing rebuild path).
//   Arm()      once per frame, right before a Renderer::Draw: the game's draw
//              (GameUpdateLoop::Draw step 7) and the main-menu backdrop's
//              (MenuDialogLoopT::DrawScene_PhaseA, handins/AO-MENU.md). Reads
//              Ext::AmbientOcclusion::Current() ONCE, builds the frame record and points
//              Graphics::Renderer::_extAo at it. Disarm() nulls it right after
//              that Draw. Nothing else arms it, so the minimap never gets AO.
//   BuildFrame()  the same record for ext.render_pipe's main half, which carries
//              it to pass 2 in its packet (passes.cpp) instead of the static.
// ============================================================================
#pragma once

class PlayerController;
namespace Graphics { struct ExtAoFrame; }

namespace Ext {
namespace AmbientOcclusion {

void Sync();

namespace detail {
extern bool g_enabled;   // Sync's decision; read by Enabled()
}
// The row is on and the device can run it. One load: the faithful profile's
// whole per-frame cost on the game's side.
inline bool Enabled() { return detail::g_enabled; }

// This frame's record from the live params and the player camera; false when
// there is nothing to draw (quality off / opacity 0).
bool BuildFrame(const PlayerController& pc, Graphics::ExtAoFrame& out);

// GameUpdateLoop::Draw step 7. Arm returns whether the seam was set.
bool Arm(const PlayerController& pc);
void Disarm();

}  // namespace AmbientOcclusion
}  // namespace Ext
