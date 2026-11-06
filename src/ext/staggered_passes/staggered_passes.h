#pragma once
// ============================================================================
// src/ext/staggered_passes/staggered_passes.h -- ext.staggered_passes (class A, live, default OFF)
// ============================================================================
// NOT RETAIL (issue #253). The sun's shadow map and the water's reflection image may each keep last
// frame's contents on a near-still view instead of being rendered again: each renders at least every
// ext.staggered_passes_shadow / ext.staggered_passes_reflection frames (2 by default, the maintainer's
// ruling: the staleness stays within the frame the pipeline already buffers), the shadow on even frames
// and the reflection on odd ones, and at once on a turn, a move past a small tolerance, a zoom, a
// settings change, a target rebuild or another camera's draw. A held shadow map is sampled with the sun
// camera it was rendered with. The decision is src/engine/graphics/stagger_hold.h; the two seams are
// Graphics::SunLight::_extStaggerEvery and Graphics::ReflectionImageBuffer::_extStaggerEvery.
// Class A: frames only, nothing saved. Under the faithful profile the row is off: both seams null.
// No Enhanced-tab entry yet (it needs a combo in the kit-compiled dialog resource): settings.cfg only,
// `ext.staggered_passes=1`, `ext.staggered_passes_shadow=N`, `ext.staggered_passes_reflection=N`.

namespace Ext {
namespace StaggeredPasses {

// Points both seams at their values while the row is active, clears them otherwise, and invalidates
// any held target. Boot (registry_boot.cpp, after the rows resolved) and every toggle or value change
// (SetAndRecord, SetValueAndRecord). Main thread, between frames.
void Sync();

}  // namespace StaggeredPasses
}  // namespace Ext
