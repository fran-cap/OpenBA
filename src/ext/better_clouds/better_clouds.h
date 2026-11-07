// ============================================================================
// src/ext/better_clouds/better_clouds.h -- ext.better_clouds (class A, default ON): drifting clouds
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERCLOUDS.md.
//
// Retail's sky is a still painted cube map. With the row on, the sky draws with a pixel program of ours
// (src/render/common/better_clouds/cloud_sky.hlsl): the painted sky blurred, under procedural clouds
// that drift down one wind direction at the game's speed (capped at 3x; still while paused), denser
// and darker with the season and the weather, lit by the scene's sun. The water's reflection of the
// sky gets a cheaper variant of the same clouds. Drawing only: nothing is saved, the sim is untouched.
//
// THE MODE (ext.better_clouds_mode): 1 On, the sky; 2 Fancy, the sky and the clouds' shadows on the
// ground (a full-screen pass at the ground-draw seam: src/render/common/better_clouds/
// cloud_shadow_backend.h; DX11 and Vulkan, Metal not yet). The Enhanced tab's "Better clouds" combo: Off / On / Fancy.
//
// HOW. For each pass of the sky material that has a vertex program, a twin VideoProgram of ours keeps
// the pass's vertex layout, vertex program and render state (the user's installed ones) and takes our
// pixel program: the full one for the sky's own pass, the cheap one for the others (the reflection).
// With the row on, the material's pass slots point at the twins; with it off, at the installed programs
// again. A pass whose installed vertex program does not write what ours reads (sky_fit.h) keeps its own.
// The sky draw's constants (the clouds' offsets, the sun) and our noise texture (t1) go in through
// Graphics::MaterialInstance::_extAfterSetup for the sky's material instances alone
// (src/ext/material_setup.h); while the row is off that seam is not ours.
//
// THREADS. SkyLoaded runs on the loader thread (GameUpdateLoop::Load, after the sky mesh is built: it
// also bakes the noise, once per process); Advance, BeginFrame, Forget, OnVideoLost and OnVideoRestored
// on the main thread; the setup hook on the drawing thread, reading what BeginFrame published.
// ============================================================================
#pragma once

namespace Graphics { class Camera; class MeshInstance; }
namespace Video { class Context; }
namespace System { class Time; }

namespace Ext {
namespace BetterClouds {

// The row (ext.better_clouds), as the Extensions dialog and the Enhanced tab leave it.
bool Enabled();

// Loader thread, GameUpdateLoop::Load right after the sky mesh is built: the sky's materials and
// material instances remembered; the noise baked the first time.
void SkyLoaded(const Graphics::MeshInstance* sky);

// Main thread, GameUpdateLoop::Update where the game time advances: the frame's time and the game's
// speed (the simulation steps it runs this frame; not called while paused) move the clouds.
void Advance(const System::Time& frameTime, int speed);

// Main thread, once per frame (GameUpdateLoop's ext block): with the row on, the twins and the noise
// texture made, the passes pointed at the twins, this frame's constants published; with it off, the
// installed programs back.
void BeginFrame();

// Main thread, when GameUpdateLoop::Draw arms the ground-draw seam (src/ext/ground_draw.cpp): Fancy's
// shadow pass while the clouds draw, the mode is Fancy and the backend can draw it; else null.
using GroundFn = void (*)(Video::Context& context, const Graphics::Camera& camera);
GroundFn ShadowDraw();

// Main thread, GameUpdateLoop::Shutdown before the sky goes: the installed programs back, the sky forgotten.
void Forget();

// Engine::MainLoop::_extVideoLost / _extVideoRestored (registry_boot.cpp): the installed programs back
// before the restart walk reloads them, our programs and twins forgotten (made again on the next frame),
// the noise texture released and made again from its kept levels.
void OnVideoLost();
void OnVideoRestored();

}  // namespace BetterClouds
}  // namespace Ext
