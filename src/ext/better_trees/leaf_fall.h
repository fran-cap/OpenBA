#pragma once
// ============================================================================
// src/ext/better_trees/leaf_fall.h -- natural tree growth's falling leaves, the pure part
// (docs/extensions/NATURAL_GROWTH.md §7)
// ============================================================================
// NOT RETAIL. No engine types: the host unit test (tests/unit/ext_natural_growth_test.cpp) runs all of it.
// falling_leaves.cpp feeds it the deciduous trees near the camera and draws what it builds.
//
// THE ATLAS. 4 x 4 leaves of 64 x 64 texels in a 256 x 256 RGBA8 sheet: a column is a leaf (the three CC0
// photographs of leaf_textures_data.h, tools/leaf_textures_bake.py, and the first one mirrored), a row a
// colour (gold, orange, red, brown) multiplied into the photograph's luminance. No game content (rule 7).
//
// THE CARDS. AlphaTestMaterial's ModelLayout (MeshLayout/ModelLayout.rsc; the opaque Normal_vs and
// DepthAlpha_vs read it): position and texture coordinates as R16G16B16A16_SINT / 512 (the second texture
// set zw / 8192), the normal as R8G8B8A8_UNORM * 2 - 1, 20 bytes. A position is relative to an origin the
// draw puts in the instance's translation, so the cards stay inside +-64 units of it.
//
// THE TIMING. A tree drops its leaves while its own autumn loss (NgRewrite::SeasonOf's autumn half, the
// same terms the canopy shader computes) is between 0 and 1: about three weeks of game time per tree,
// staggered across the map as the canopies are.
//
// THE MOTION. Each leaf falls at its own speed with a side-to-side flutter, a drift, a spin about the
// view axis and a tumble (the card squashed by |cos|), and is gone at the tree's foot.
// ============================================================================

#include <cstdint>
#include <vector>

namespace Ext {
namespace BetterTrees {
namespace LeafFall {

constexpr int kAtlasSize  = 256;
constexpr int kCell       = 64;
constexpr int kShapes     = 4;
constexpr int kColours    = 4;    // 0 gold, 1 orange, 2 red, 3 brown
constexpr int kMaxLeaves  = 2400; // 4 vertices each: inside one 16-bit dynamic index range
constexpr float kLeafSize = 0.15f; // the card's edge, world units: about a leaf of the canopy billboards
constexpr float kReach    = 60.0f;   // the cards' reach around the draw's origin (the int16 / 512 range)

// The atlas, straight RGBA8, row-major, top-down (kAtlasSize^2 * 4 bytes).
std::vector<uint8_t> PaintAtlas();

// One deciduous tree near the camera.
struct Tree {
    float x, y;          // the instance translation (the hash's input)
    float baseZ;         // the ground at its foot
    float crown[6];      // its world box: min xyz, max xyz
};

struct Leaf {
    float pos[3];
    float fall;          // units per second
    float drift[2];      // units per second
    float flutterAmp, flutterRate, phase;
    float spin, spinRate;
    float tumbleRate;
    float floorZ;
    uint8_t shape, colour;
};

// The tree's autumn leaf loss at `months` (0..12): 0 before its fall, 1 once bare.
float AutumnLoss(float x, float y, float months);
// Leaves per second a tree drops at a given autumn loss (0 outside (0, 1)).
float DropRate(float loss);
// The colour row of a tree's leaves (from its hue, as the canopy palette picks gold / orange / red).
int ColourOf(float x, float y);

class Field {
public:
    // Advances `dt` real seconds (0 = paused: nothing moves, nothing drops) at day `months`, dropping from
    // `trees` (only those in their fall window drop).
    void Step(float dt, float months, const std::vector<Tree>& trees);
    const std::vector<Leaf>& Leaves() const { return _leaves; }
    void Clear() { _leaves.clear(); }

private:
    float Rand();
    std::vector<Leaf>  _leaves;
    uint32_t _seed = 0x9e3779b9u;
};

// One card corner in ModelLayout (above).
struct Vertex {
    int16_t position[4];   // (p - origin) * 512, w 0
    int16_t texcoord[4];   // atlas uv * 512, then the second set * 8192 (the ambient lookup: a fixed texel)
    uint8_t normal[4];     // n * 127.5 + 127.5
};
static_assert(sizeof(Vertex) == 20, "ModelLayout's stride");
// The leaves as camera-facing cards around `origin`: `right` / `up` two unit axes of the view plane,
// `facing` the normal the cards are lit with. Appends four vertices per leaf; a leaf beyond kReach of the
// origin is left out.
void BuildQuads(const std::vector<Leaf>& leaves, const float right[3], const float up[3], const float facing[3],
                const float origin[3], std::vector<Vertex>& out);

}  // namespace LeafFall
}  // namespace BetterTrees
}  // namespace Ext
