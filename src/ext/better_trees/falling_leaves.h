#pragma once
// ============================================================================
// src/ext/better_trees/falling_leaves.h -- natural tree growth's falling leaves (NATURAL_GROWTH.md §7)
// ============================================================================
// NOT RETAIL, class A (render only: nothing is saved, the sim never sees it). While natural tree growth is
// on, the deciduous trees near the camera drop leaves during their own autumn leaf fall. The leaves are our
// own procedural atlas (leaf_fall.h) drawn by the installed snow weather's particle material: no shader
// is added or rewritten. Two seams in GameUpdateLoop: Frame (Draw step 2) and Shutdown (with the other
// global scene objects).
// ============================================================================

class GameUpdateLoop;
namespace Graphics { class Scene; }

namespace Ext {
namespace BetterTrees {

// Once per drawn frame, main thread: the tree scan, the leaves' step and the frame's cards.
void LeavesFrame(GameUpdateLoop& game);
// GameUpdateLoop::Shutdown, before the scene is destroyed: the geometry leaves the scene and is freed.
void LeavesShutdown(Graphics::Scene* scene);

}  // namespace BetterTrees
}  // namespace Ext
