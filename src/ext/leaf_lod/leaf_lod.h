#pragma once
// ============================================================================
// src/ext/leaf_lod/leaf_lod.h -- ext.leaf_lod: trees drawn with fewer, larger leaf cards when zoomed out
// ============================================================================
// NOT RETAIL. docs/extensions/LEAF_LOD.md.
//
// THE LEVELS. For every mesh drawn with a billboard canopy material, kLevels reduced copies of its first
// detail level's geometry are built at run time from the installed mesh's own buffers (read back through
// the buffers' save arm): the trunk and branches as they are, a share of the leaf cards dropped and the
// rest grown (leaf_lod_build.h). The copies live in our own buffers; the mesh is not touched.
//
// THE DRAW. Graphics::MeshInstance::_extDrawLists (meshinstance_draw.cpp): where the draw binds a rigid
// list's buffers, the level in force replaces the vertex buffer, the index buffer and the list's range.
// Every pass draws the same canopy (the colour pass, the sun's depth pass, the reflection), so a tree's
// shadow is its reduced canopy's.
//
// THE LEVEL IN FORCE is one number for the whole view, from the player camera's distance to its target
// (the zoom): every tree changes together while the camera zooms, so there is no line across a forest
// where one kind of tree meets the other.
//
// Threads: Frame / Forget on the main thread; the hook on the drawing thread (the render pipe's, or main).
// ============================================================================

class GameUpdateLoop;

namespace Ext {
namespace LeafLod {

// Once per game frame before the scene draws (GameUpdateLoop::Draw step 2, and the render pipe's copy of
// it): the row's switch, the level in force, the levels of meshes seen since the last frame.
void Frame(GameUpdateLoop& game);

// The world is going away (GameUpdateLoop::Shutdown): every level is released and every mesh forgotten.
void Forget();

// A video device restart (MainLoop::_extVideoLost): the levels' buffers go with the device and are built
// again when their meshes next draw.
void OnVideoLost();

}  // namespace LeafLod
}  // namespace Ext
