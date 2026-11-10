#pragma once
// ============================================================================
// src/ext/leaf_lod/leaf_lod_params.h -- ext.leaf_lod's look (docs/extensions/LEAF_LOD.md)
// ============================================================================
// NOT RETAIL. The defaults the extension ships with; the debug tweak overlay (Alt+F10) edits a live copy
// through Ext::Tweak::LeafLodValues(). No engine types.
// ============================================================================

namespace Ext {
namespace LeafLod {

constexpr int kLevels = 64;   // reduced canopies per tree mesh, the last the lightest

struct Look {
    float start = 60.0f;    // the camera's distance from its target up to which the mesh's own canopy draws
    float end   = 150.0f;   // ...and from which the last level does (the levels between are evenly spaced)
    float keep  = 0.60f;    // the share of a canopy's leaf cards the last level keeps
    float grow  = 0.10f;    // how much the kept cards grow: 1 keeps the crown's total card area, 0 no growth
    float outline = 0.025f; // a tree stops losing cards where its outline would move in by more than this
                            // share of its crown's size (leaf_lod_build.h THE CAP)
    int   force = -1;       // debug: -1 by the camera's distance, 0..kLevels that level everywhere
};

}  // namespace LeafLod
}  // namespace Ext
