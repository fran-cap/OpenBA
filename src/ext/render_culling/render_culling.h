// src/ext/render_culling/render_culling.h (was src/app/perf_experimental.h)
// The machinery is shared: ext.anim_batch's own header, src/ext/anim_batch/anim_batch.h, is its seam's
// view of AnimBatchDecide and the per-frame flags below; the bodies of both rows stay here.
// ============================================================================
// NOT RETAIL. "PerfX": the machinery of two extensions (docs/extensions/handins/PERFXEXT.md),
// both default OFF like every extension:
//
//   ext.render_culling  class A (live)   presentation work nobody can see is skipped:
//       animCull        an off-screen model's last-iteration MeshInstance::Update stays
//                       root-only (retail's own intermediate-iteration path)
//       geomXfLastOnly  move tick: Geometry::SetTransform only on the last iteration
//       geomCull        ... and not for an off-screen citizen (pushed on pause)
//       iconLastOnly    status-icon repositioning only on the last iteration
//       iconCull        ... and not for an off-screen owner (replayed on pause)
//       pickCull        an off-screen citizen's Picking::Scene::SetTransform deferred
//       particleCull    an off-screen particle system that is not fading is not ticked
//                       (PERFXEXT §3.1: only then is the skipped tick presentation-only)
//   ext.anim_batch      class B (locked per game): at speed N, the N-1 intermediate
//                       root-only updates of a model whose root offset comes from the
//                       instanced pool or is zero are folded into the last one. The sim
//                       reads the animation clock (IsAnimating), so it diverges.
//
// The knobs of render_culling are compile-time (kRenderCulling); the extension is the one
// toggle (the dialog has no sub-options). The faithful profile pays one load + branch at
// each seam. Toggles are read ONCE per frame (BeginFrame, from Ext::Ext()) into g_flags;
// nothing below reads the registry.
//
// NOT part of either extension, faithful perf work (same results by proof, unconditional):
// the TREE_SNAPSHOT walk (Entity/entitylist.cpp), WALKCACHE (Entity/tick_list.h) and the
// terrain-type sidecar (Game/terrain_sidecar.h). PERFXTBL's side table (side_table.h) is
// built only while one of the two extensions is on.
// ============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include "ext/render_culling/render_culling_rules.h"   // Knobs / Flags / FlagsFor and the particle and clock rules

class ComponentEntity;                   // Entity/ComponentEntity.h (PERFXTBL side table key)
class ModelComponent;                    // Component/ModelComponent.h
class StatusIconComponent;               // Component/StatusIconComponent.h (NoteDeferred)
namespace System { class Time; }         // src/engine/system/Value.h
namespace Graphics { class Renderer; }   // src/engine/graphics/Camera.h
namespace Math { class Transform; }      // src/engine/math/Math.h
namespace Picking { class Scene; }       // src/engine/picking/Scene.h

namespace Ext {
namespace RenderCulling {

// Knobs, kRenderCulling, kCullMargin and Flags are in render_culling_rules.h.
extern Flags g_flags;
// PERFXTBL (side_table.h): the TREE_SNAPSHOT walk stores the index of the entity it is
// ticking here (-1 outside the walk), so a tick reaches its side-table slot with one
// compare. A wrong or -1 index only costs a binary search. One unconditional store per
// entity (Entity/entitylist.cpp).
extern int64_t g_walkIndex;

inline bool AnimCull()       { return g_flags.animCull; }
inline bool GeomXfLastOnly() { return g_flags.geomXfLastOnly; }
inline bool GeomCull()       { return g_flags.geomCull; }
inline bool IconLastOnly()   { return g_flags.iconLastOnly; }
inline bool IconCull()       { return g_flags.iconCull; }
inline bool PickCull()       { return g_flags.pickCull; }
inline bool ParticleCull()   { return g_flags.particleCull; }

// GameUpdateLoop::Update start: read the two toggles (Ext::Ext()) into g_flags, handle a
// render_culling toggle (off: flush everything deferred, so nothing stays stale; on: drop
// the old frustum), refresh the per-frame world frustum from the renderer's cull camera
// and flush any deferred visual state when the sim did not run last frame (pause), so a
// paused pan never shows a stale pose.
void BeginFrame(Graphics::Renderer* renderer);
// Draw side: the terrain collect pass hands over the world-space Volume::Frustum it
// computed (6 planes {nx,ny,nz,d}); the cull camera reference is only valid inside Draw.
void SetWorldFrustum(const void* frustum);
// World teardown: forget every cached pointer (unconditional).
void Reset();
// Entity removal (the components' OnRemove, at MarkForRemoval): drop every deferred entry
// that names this component. The pick mesh is freed at once (PickingComponent::OnRemove ->
// Scene::RemoveMesh) and the component at the next frame's removal drain, while a deferred
// entry lives until the next sim frame's first culled tick or a flush on a frame the sim
// did not run; replaying it then wrote through a freed pick entry (null transforms).
void ForgetModel(ModelComponent* modelComponent);
void ForgetPick(Picking::Scene* pickScene, uint32_t pickId);
void ForgetIcon(StatusIconComponent* obj);

// Current MeshInstance::_rootOnlyUpdates value (0 on the last iteration / speed 1).
bool RootOnlyNow();
bool* RootOnlyFlag();

// Conservative visibility test of a world position against the cached frustum
// (returns true when no frustum is available).
bool PointVisible(const float* pos);
// OFFSCREEN (diagnostic, BANISHED_STRESS_COMPPROF=2 only): the same test with the same
// margin against this frame's planes, kept whether or not ext.render_culling is on, for
// the [COMPPROF] /char rows. Uncached; true when no frustum was handed over. Nothing in
// the sim or the extensions reads it.
bool DiagPointVisible(const float* pos);

// Sim loop: which speed iteration is running (0-based) out of how many this frame.
void SetIteration(int index, int count);

// ModelComponent tick, ext.anim_batch (call only when g_flags.animBatch): returns true when
// the update must be SKIPPED this iteration; otherwise *timeOut is the Time to pass (the
// tick, or (skipped+1) x the tick on the last iteration when intermediate ones were
// skipped for this model).
bool AnimBatchDecide(ModelComponent* modelComponent, const System::Time* time, int64_t* timeOut);
// ModelComponent tick, render_culling: may this model's pose update stay root-only this
// iteration? Records the model for a later flush.
bool AnimCullSkipSlow(ModelComponent* modelComponent);
inline bool AnimCullSkip(ModelComponent* modelComponent) {
    return g_flags.animCull && AnimCullSkipSlow(modelComponent);
}

// Picking: an off-screen citizen skips its Picking::Scene::SetTransform; the deferred
// push is replayed on pause so a click while paused still hits the right mesh.
typedef Math::Transform* (*PickXfCtor_t)(void* xformOut, const Math::Transform* transformIn);
typedef void (*PickSetXf_t)(Picking::Scene* scene, uint32_t id, const Math::Transform* xform);
bool PickCullSkip(ModelComponent* modelComponent, Picking::Scene* pickScene, uint32_t pickId,
                  PickXfCtor_t xfCtor, PickSetXf_t setXf);
// True when the model's position (model+0x158) is outside the frustum margin and the
// anim-cull machinery is active (so a later flush will repair anything deferred).
bool ModelCulled(ModelComponent* modelComponent);
// Deferral: `replay(obj)` is called with the root-only flag clear when the sim goes
// idle (pause), so deferred presentation state catches up before a paused pan. The
// one user is the status icon's culled reposition (statusiconcomponent.cpp).
void NoteDeferred(StatusIconComponent* obj, void (*replay)(StatusIconComponent*));
// Particles: true = this off-screen particle system is not ticked. The verdict is kept in
// the owner entity's side-table slot for the Update frame (PERFXTBL). The caller has
// already checked that the component is not fading (PERFXEXT §3.1).
bool ParticleCullSkip(const float* pos, const ComponentEntity* entity);
// PERFXTBL: the TREE_SNAPSHOT walk array was rebuilt (entitylist.cpp RebuildWalkSnapshot;
// n = 0 / sorted = false on a failed rebuild): the side table follows it while an
// extension that uses it is on, and stays empty otherwise.
void OnWalkSnapshot(ComponentEntity* const* snap, size_t n, bool sorted);

} // namespace RenderCulling
}  // namespace Ext
