#pragma once
// ============================================================================
// src/ext/better_trees/falling_leaves.h -- natural tree growth's falling leaves (NATURAL_GROWTH.md §7)
// ============================================================================
// NOT RETAIL, class A (render only: nothing is saved, the sim never sees it). While natural tree growth is
// on, the deciduous trees near the camera drop leaves during their own autumn leaf fall. The leaves are
// CC0 leaf photographs coloured into an atlas (leaf_fall.h), drawn lit and shadowed by the installed
// AlphaTestMaterial (the herbs' material): no shader is added or rewritten. Two seams in GameUpdateLoop: Frame (Draw step 2) and Shutdown (with the other
// global scene objects).
// ============================================================================

class GameUpdateLoop;
namespace Graphics { class Scene; class Geometry; }

namespace Ext {
namespace BetterTrees {

// Once per drawn frame, main thread: the tree scan, the leaves' step and the frame's cards.
void LeavesFrame(GameUpdateLoop& game);
// GameUpdateLoop::Shutdown, before the scene is destroyed: the geometry leaves the scene and is freed.
void LeavesShutdown(Graphics::Scene* scene);
// Main thread: the leaves' scene object, or null while there is none. ext.render_pipe's snapshot
// leaves a draw item of it at the live object (P13_OVERLAP 2.2): its Draw reads only the card set
// it shares with LeavesFrame under their lock, which no sim step writes, and the object stays
// allocated from its first autumn to LeavesShutdown.
const Graphics::Geometry* LeavesLiveGeometry();

}  // namespace BetterTrees
}  // namespace Ext
