# Lighter far trees (`ext.leaf_lod`)

Class A (drawing only, nothing saved, live). On the Experimental tab. Code: `src/ext/leaf_lod/`.

Zoomed out, a forest is the most expensive thing on screen: every leafy tree draws 20 to 40 leaf cards that
mostly cover each other (`docs/android/I253_FOLIAGE_RESEARCH.md`: about ten leaf fragments per pixel at
full zoom-out). With the row on, a tree draws fewer of its leaf cards the farther the camera is zoomed
out. The trunk, the shader, the lighting and the shadows are the game's own; only which cards draw, and
how large, changes.

An earlier attempt drew far trees as one baked picture each (an impostor card). It was dropped: the flat
picture loses the crown's parallax, its cut-out edges shimmer in motion, and the hand-over to the real
tree showed as a line across the forest.

## 1. What is built

For every mesh whose first detail level draws a rigid triangle list with a billboard material (the birches,
the oak, the fruit and nut trees, the blueberry bush; pines are alpha-test meshes and are left alone),
`kLevels` (64) reduced copies of that level's geometry are made the first time the mesh draws:

- The mesh's vertex and index bytes are read back from its own buffers through the buffers' save arm
  (`Video::VertexBufferData::Serialize` into a writing `IO::MemoryStream`). Nothing is read from a file
  and nothing is shipped: the copies are a transform of the installed (or a mod's) mesh, made at run time.
- `leaf_lod_build.cpp` (pure, unit-tested) finds the leaf cards: in the 24-byte rigid vertex the last four
  bytes are two int16 corner weights, 0 on the trunk; a card is a connected set of triangles whose
  vertices all carry weights.
- A copy is the whole vertex buffer with some cards' corner weights scaled, and an index buffer without the
  cards that are gone. The copies live in our own `Video::VertexBuffer` / `IndexBuffer` pairs; the mesh
  is never changed (its buffers can be evicted and re-read by `ext.create_on_demand`, and its detail-level
  array cannot be grown safely).

On the installed game the six canopy meshes of a plain map take about 2.5 MB.

## 2. Which cards go

The order is worked out once per list by taking cards away one at a time, always the card that costs
least. The cost is how far the crown's outline moves in without the card, over 200 directions (the root
mean square of the moves as shares of the crown's size; a direction counts 1 straight up, 0.7 level, 0.1
straight down, because the camera looks down on a crown), plus a little for the card's distance to its
nearest neighbour left. A card that only doubles a neighbour goes first; the cards that draw the outline
stay to the end.

Each step records the largest single move so far. A list stops losing cards where that passes
`Look::outline` (2.5% of the crown's size), whatever `Look::keep` asks. The birches' cards fill their
crowns and the oak's sit on a dome, so they tolerate different shares; the cap is per mesh.

Two orders were tried first and rejected on screen: an even spread (farthest point first) cut holes in the
oak's dome, and the area of the outline's convex hull let the top of a crown flatten, because a round
outline loses little area for each card.

## 3. The levels

`Look::start` and `Look::end` are camera distances from the camera's target (the zoom). Up to `start` the
mesh's own canopy draws; from `end` the last level; the 64 levels between are evenly spaced. A card does
not vanish between two levels: it shrinks over four cards' worth of progress, a few percent per level,
so a zoom reads as leaves drawing in. The cards that stay grow by `Look::grow` (1 would keep the crown's
total card area; the default 0.10 is a touch).

The level is one number for the whole view, not per tree: every tree changes together while the camera
zooms, so there is no line across a forest. A dithered cross-fade between levels was considered and not
built: it draws both canopies during the fade (twice the leaf cost where the row is meant to cut it) and
shows its pattern without temporal anti-aliasing.

## 4. The seams

- `Graphics::MeshInstance::_extDrawLists` (`meshinstance_draw.cpp`): where `Draw` binds a rigid list's
  buffers, the extension may replace the vertex buffer, the index buffer and the list's range. Every pass
  goes through it, so the sun's depth pass draws the same reduced canopy and a tree's shadow matches it.
  Null (the row off, the faithful profile): retail's bindings.
- `Ext::LeafLod::Frame` in `GameUpdateLoop::Draw` step 2 and in `ext.render_pipe`'s copy of that step
  (`render_pipe.cpp`): the level for this frame's zoom, and the levels of meshes seen since the last frame.
- `Ext::LeafLod::Forget` in `GameUpdateLoop::Shutdown`, `OnVideoLost` at a device restart.

Threads: `Frame` on the main thread; the hook on the drawing thread. A mesh's levels are immutable once
published; a rebuild (the debug overlay moved a slider) publishes a new set and frees the old one 60
frames later.

## 5. Tuning

Alt+F10, "Lighter far trees" (debug build): the two zoom distances, the share kept at the last level, the
growth, the outline tolerance, and a forced level (0..64) to compare at any zoom. The defaults are in
`src/ext/leaf_lod/leaf_lod_params.h`.

## 6. Open

- Seasons: natural tree growth's autumn leaf loss is a per-texel alpha cut, independent of the cards; the
  two stack. Not looked at in autumn.
- The switch distances are camera distances, not screen sizes: a phone and a desktop reduce at the same zoom.
