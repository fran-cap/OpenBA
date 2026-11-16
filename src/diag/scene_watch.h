// src/diag/scene_watch.h — Graphics::Scene node-array integrity probe.
//
// Enabled with BANISHED_SCENE_WATCH=1 (off = both calls return immediately).
//
// Why: Graphics::Scene::Clean (Runtime 0x1801023f0) walks the scene's node array
// and dispatches vtable +0x40 on every live node's Geometry. Every drawable
// component in this port embeds its Geometry INSIDE the component allocation
// (ModelComponent +0x28, StatusIconComponent +0x28, ...), so a component freed
// while its scene id is still registered leaves a node pointing into recycled
// heap — Clean then calls through a garbage vtable ("EXEC of <heap address>").
//
// SceneWatch_Check finds that node the frame it appears instead of whenever Clean
// happens to reach it; SceneWatch_Note records which of our AddObject sites last
// registered each scene id, so the report names the owner.
#pragma once
#include <cstdint>

namespace Graphics { class Scene; class Renderer; class Camera; }   // src/engine/graphics

void SceneWatch_Note(uint32_t sceneId, void* geometry, const char* who);
void SceneWatch_Check(Graphics::Scene* scene);

// BANISHED_SCENE_CENSUS=1 — per-frame membership + position churn of the scene.
//
// "Models keep popping in and out while I play" has two candidate mechanisms and
// this separates them in one run:
//   MEMBERSHIP  — geometries leaving and re-entering the scene node array. Shows up
//                 as non-zero added/removed on a settled world.
//   POSITION    — geometries staying in the scene but having their transform
//                 REASSIGNED, so the model jumps somewhere else. Shows up as a
//                 non-zero moved/jumped count.
// Graphics::Geometry keeps its live transform at +0x84..+0x9F (quat at +0x84,
// position at +0x94/+0x98/+0x9C) and the PREVIOUS one at +0xA0..+0xBB, which the
// renderer interpolates between; Graphics::Geometry::SetTransform (Runtime
// 0x1800155a0) is the only writer and it copies current -> previous first. So a
// stale or wild previous is itself a visible streak/jump, and is counted too.
//
// Citizens and animals move legitimately every frame, so a raw "moved" count is
// meaningless on its own — what matters is `jumped` (a discontinuous hop no walker
// makes in one frame) and the membership deltas.
void SceneWatch_Census(Graphics::Scene* scene);

// BANISHED_LOD_CENSUS=1 — replicate the retail per-frame LOD pick and report it.
//
// The scene census (above) proved every CULL input healthy while models still
// popped. The cull path and the LOD path read DIFFERENT inputs, so a healthy cull
// says nothing about LOD:
//
//   Graphics::Scene::AddCulledDrawItems (Runtime 0x1801017a0) frustum-tests the
//     CURRENT world box, geo+0x4c..+0x60, alone.
//   Graphics::MeshInstance::AddDrawItems (Runtime 0x180100f20) picks a detail level
//     from the UNION of the current box and the PREVIOUS one, geo+0x64..+0x78
//     (Graphics::Geometry::Clean 0x1800a4420 rolls current -> previous), measured
//     against the draw camera at Camera+0x88/+0x8c/+0x90 and compared with the mesh's
//     own distance table: GraphicsMesh+0x50 array, stride 0xb0, distance at +0x10,
//     count at GraphicsMesh+0x58. First level whose distance EXCEEDS the object
//     distance wins; if none does the pick falls back to level 0.
//
// So a mesh with a corrupt distance table, or one whose previous box never rolls,
// gets a level that flips as the camera drifts — which on screen is a model popping.
// This probe recomputes that exact pick every frame and counts the flips, and dumps
// each distinct mesh's distance table once so a corrupt table is visible directly.
//
// Pass the same Camera the frame hands to Graphics::Renderer::Draw.
// variantIdx is the main pass's shader-variant index (the scene entry's +0xec; the caller passes 0):
// Graphics::Renderer::AddDrawItem drops, with no diagnostic, any item whose material
// has no variant there, which is a model that is simply never drawn.
void SceneWatch_LodCensus(Graphics::Scene* scene, const Graphics::Camera* camera, int variantIdx);

// BANISHED_LOD_CENSUS also enables this: what the frame ACTUALLY drew.
//
// Every probe so far measures an INPUT to the draw decision. This measures the
// output. Graphics::Renderer::AddDrawItem (Runtime 0x1801057b0) appends each
// accepted item into one of three per-pass lists on the draw group's state block:
// groupState + 0x1c0/0x1d0/0x1e0, each a {void* data; int count} pair, with the
// group state array at renderer+0x10, stride 0x1f0, and renderer+0x38 groups used.
// Graphics::Renderer::Reset (0x180105110) empties them at the top of every Draw,
// so reading them immediately AFTER Draw returns gives that frame's totals.
//
// Call it right after Graphics::Renderer::Draw.
void SceneWatch_DrawItemCensus(Graphics::Renderer* renderer);

// BANISHED_FORCE_LOD0=1 — pin every GraphicsMesh to detail level 0 (implies the
// census above). Clamps GraphicsMesh+0x58 to 1, which is the count
// Graphics::MeshInstance::AddDrawItems tests before running its distance pick, so
// level 0 draws at every distance and the swap can never happen. Use it as the A/B
// for "is the detail swap the thing I see popping?" — if the popping survives this,
// the LOD is not the cause.
