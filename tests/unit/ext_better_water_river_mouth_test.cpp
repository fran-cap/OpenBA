// ============================================================================
// tests/unit/ext_better_water_river_mouth_test.cpp -- ext.better_water's river mouths
// (src/ext/better_water/river_mouth.h)
// ============================================================================
// A lake range whose box cuts a channel on one side fades toward that side only; a lake with no cut side
// and the river's vertices are left as they are. The draw (the copied buffer bound for the contour
// ranges) needs the running game.
// ============================================================================
#include "unit.h"
#include "ext/better_water/river_mouth.h"

#include <cstdint>
#include <vector>

namespace {

using namespace Ext::BetterWater::RiverMouth;

constexpr int kStride = 32;
constexpr int kHalf = 16;

// The heightmap: wet (-1) in rows 1..3 for every column >= 1 (a lake running into a channel past column 10),
// dry (+1) elsewhere.
std::vector<int16_t> Heights() {
    std::vector<int16_t> h(kStride * kStride, static_cast<int16_t>(256));
    for (int row = 1; row <= 3; ++row) {
        for (int col = 1; col < kHalf; ++col) h[(row + kHalf) * kStride + (col + kHalf)] = -256;
    }
    return h;
}

TerrainVertex V(int col, int row, uint8_t alpha) {
    TerrainVertex v{};
    v.position[0] = static_cast<int16_t>(col * 48);
    v.position[1] = static_cast<int16_t>(row * 48);
    v.normal[0] = 0x7f;
    v.normal[1] = 0x7f;
    v.normal[2] = alpha;
    return v;
}

int Find(const std::vector<TerrainVertex>& vs, int col, int row) {
    for (size_t i = 0; i < vs.size(); ++i) {
        if (vs[i].position[0] == col * 48 && vs[i].position[1] == row * 48) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

UNIT_TEST(ext_better_water_river_mouth_fades_toward_the_cut_side_only) {
    const std::vector<int16_t> heights = Heights();
    std::vector<TerrainVertex> mesh;
    mesh.push_back(V(12, 2, 0xff));   // a river vertex before the lake range
    const int32_t lakeStart = static_cast<int32_t>(mesh.size());
    for (int row = 0; row <= 4; ++row) {
        for (int col = 0; col <= 10; ++col) mesh.push_back(V(col, row, 200));
    }
    const Range lake{lakeStart, static_cast<int32_t>(mesh.size()) - lakeStart};
    std::vector<TerrainVertex> out;
    const int changed = Fade(mesh.data(), static_cast<int32_t>(mesh.size()), &lake, 1, heights.data(), kStride,
                             kHalf, kFadeTiles, out);
    CHECK(changed > 0);
    CHECK(out.size() == mesh.size());
    CHECK(out[0].normal[2] == 0xff);                       // the river is untouched
    CHECK(out[Find(out, 10, 2)].normal[2] == 0);           // on the cut: fully handed over
    CHECK(out[Find(out, 4, 2)].normal[2] == 200);          // a fade's width in: the lake as it was
    const uint8_t mid = out[Find(out, 8, 2)].normal[2];
    CHECK(mid > 0 && mid < 200);                            // in between
    CHECK(out[Find(out, 0, 2)].normal[2] == 200);          // the dry shore side keeps its alpha
    CHECK(out[Find(out, 2, 0)].normal[2] == 200);
}

UNIT_TEST(ext_better_water_river_mouth_leaves_a_closed_lake_alone) {
    // Every edge of the range dry: no cut side.
    std::vector<int16_t> heights(kStride * kStride, static_cast<int16_t>(256));
    heights[(2 + kHalf) * kStride + (2 + kHalf)] = -256;   // one wet corner inside
    std::vector<TerrainVertex> mesh;
    for (int row = 0; row <= 4; ++row) {
        for (int col = 0; col <= 4; ++col) mesh.push_back(V(col, row, 180));
    }
    const Range lake{0, static_cast<int32_t>(mesh.size())};
    std::vector<TerrainVertex> out;
    CHECK(Fade(mesh.data(), static_cast<int32_t>(mesh.size()), &lake, 1, heights.data(), kStride, kHalf,
               kFadeTiles, out) == 0);
    for (const TerrainVertex& v : out) CHECK(v.normal[2] == 180);
}

UNIT_TEST(ext_better_water_river_mouth_ignores_a_bad_range) {
    const std::vector<int16_t> heights = Heights();
    std::vector<TerrainVertex> mesh(4, V(1, 1, 100));
    const Range bad{2, 10};   // past the mesh's end
    std::vector<TerrainVertex> out;
    CHECK(Fade(mesh.data(), 4, &bad, 1, heights.data(), kStride, kHalf, kFadeTiles, out) == 0);
    CHECK(out.size() == 4);
}
