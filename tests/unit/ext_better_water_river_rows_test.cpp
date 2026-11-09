// ============================================================================
// tests/unit/ext_better_water_river_rows_test.cpp -- ext.better_water's river rows
// (src/ext/better_water/river_rows.h)
// ============================================================================
// Strips laid out as retail's Stage C lays them (five vertices a row, four quads a step, (V0,V2,V1),
// (V0,V3,V2)): a straight run is left alone, one skewed row is straightened, a right-angle turn spreads over
// several rows with no row end running backwards, the ends stay put, and indices that are not a strip are
// skipped. The draw needs the running game.
// ============================================================================
#include "unit.h"
#include "ext/better_water/river_rows.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace Ext::BetterWater::RiverRows;

struct Strip {
    std::vector<TerrainVertex> vertices;
    std::vector<uint16_t> indices;
};

// One row per (left, right) pair, in tiles.
Strip MakeStrip(const std::vector<P2>& left, const std::vector<P2>& right) {
    Strip s;
    const int n = static_cast<int>(left.size());
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < kColumns; ++c) {
            const float t = static_cast<float>(c) / static_cast<float>(kColumns - 1);
            TerrainVertex v{};
            v.position[0] = Pack(left[r].x + (right[r].x - left[r].x) * t);
            v.position[1] = Pack(left[r].y + (right[r].y - left[r].y) * t);
            v.uv[0] = static_cast<int16_t>(t * 1024.0f);
            v.uv[1] = static_cast<int16_t>(r * 32);
            v.normal[2] = 0xff;
            s.vertices.push_back(v);
        }
    }
    for (int r = 0; r + 1 < n; ++r) {
        for (int c = 0; c < kColumns - 1; ++c) {
            const uint16_t v0 = static_cast<uint16_t>(r * kColumns + c);
            const uint16_t v1 = static_cast<uint16_t>(r * kColumns + c + 1);
            const uint16_t v2 = static_cast<uint16_t>((r + 1) * kColumns + c + 1);
            const uint16_t v3 = static_cast<uint16_t>((r + 1) * kColumns + c);
            const uint16_t quad[6] = {v0, v2, v1, v0, v3, v2};
            s.indices.insert(s.indices.end(), quad, quad + 6);
        }
    }
    return s;
}

P2 At(const std::vector<TerrainVertex>& vs, int r, int c) {
    const TerrainVertex& v = vs[static_cast<size_t>(r * kColumns + c)];
    return {static_cast<float>(v.position[0]) / kPosScale, static_cast<float>(v.position[1]) / kPosScale};
}

// The angle (degrees) between rows r - 1 and r.
float Turn(const std::vector<TerrainVertex>& vs, int r) {
    const P2 a = Sub(At(vs, r - 1, kColumns - 1), At(vs, r - 1, 0));
    const P2 b = Sub(At(vs, r, kColumns - 1), At(vs, r, 0));
    const float cosine = Dot(a, b) / (Len(a) * Len(b));
    return std::acos(cosine > 1.0f ? 1.0f : cosine) * 57.29578f;
}

int RelayAll(Strip& s, int* skipped) {
    const Range range{0, 0, static_cast<int32_t>(s.vertices.size()), static_cast<int32_t>(s.indices.size())};
    return Relay(s.vertices, s.indices.data(), static_cast<int32_t>(s.indices.size()), &range, 1, skipped);
}

}  // namespace

UNIT_TEST(ext_better_water_river_rows_leave_a_straight_run_alone) {
    std::vector<P2> left;
    std::vector<P2> right;
    for (int r = 0; r <= 20; ++r) {
        left.push_back({static_cast<float>(r), 3.0f});
        right.push_back({static_cast<float>(r), -3.0f});
    }
    Strip s = MakeStrip(left, right);
    int skipped = 0;
    CHECK_EQ(RelayAll(s, &skipped), 0);
    CHECK_EQ(skipped, 0);
}

UNIT_TEST(ext_better_water_river_rows_straighten_one_skewed_row) {
    std::vector<P2> left;
    std::vector<P2> right;
    for (int r = 0; r <= 20; ++r) {
        const float skew = r == 10 ? 2.5f : 0.0f;   // row 10 swings about 40 degrees
        left.push_back({static_cast<float>(r) + skew, 3.0f});
        right.push_back({static_cast<float>(r) - skew, -3.0f});
    }
    Strip s = MakeStrip(left, right);
    const std::vector<TerrainVertex> before = s.vertices;
    CHECK(Turn(before, 10) > 35.0f);
    int skipped = 0;
    CHECK(RelayAll(s, &skipped) > 0);
    for (int r = 1; r <= 20; ++r) CHECK(Turn(s.vertices, r) < 5.0f);
    // The ends and the uvs stay where retail put them.
    for (int c = 0; c < kColumns; ++c) {
        CHECK_EQ(s.vertices[c].position[0], before[c].position[0]);
        CHECK_EQ(s.vertices[20 * kColumns + c].position[1], before[20 * kColumns + c].position[1]);
    }
    for (size_t i = 0; i < before.size(); ++i) CHECK_EQ(s.vertices[i].uv[1], before[i].uv[1]);
}

UNIT_TEST(ext_better_water_river_rows_spread_a_right_angle_without_folding) {
    // Along +x to (10, 0), then along +y: the row at the corner is the last vertical one.
    std::vector<P2> left;
    std::vector<P2> right;
    for (int r = 0; r <= 10; ++r) {
        left.push_back({static_cast<float>(r), 3.0f});
        right.push_back({static_cast<float>(r), -3.0f});
    }
    for (int r = 1; r <= 10; ++r) {
        left.push_back({7.0f, static_cast<float>(r)});
        right.push_back({13.0f, static_cast<float>(r)});
    }
    Strip s = MakeStrip(left, right);
    const std::vector<TerrainVertex> before = s.vertices;
    const int n = static_cast<int>(left.size());
    CHECK(Turn(before, 11) > 85.0f);
    int skipped = 0;
    CHECK(RelayAll(s, &skipped) > 0);
    float worst = 0.0f;
    for (int r = 1; r < n; ++r) {
        const float t = Turn(s.vertices, r);
        if (t > worst) worst = t;
    }
    CHECK(worst < 30.0f);
    // No row end runs backwards along the flow (the inside of the bend is pulled in, not folded).
    for (int r = 1; r < n; ++r) {
        const P2 c0 = Mul(Add(At(s.vertices, r - 1, 0), At(s.vertices, r - 1, kColumns - 1)), 0.5f);
        const P2 c1 = Mul(Add(At(s.vertices, r, 0), At(s.vertices, r, kColumns - 1)), 0.5f);
        const P2 flow = Sub(c1, c0);
        for (int c = 0; c < kColumns; c += kColumns - 1) {
            CHECK(Dot(Sub(At(s.vertices, r, c), At(s.vertices, r - 1, c)), flow) > -0.05f);
        }
    }
    // The outer bank (the right side, round the corner's outside) covers at least retail's corner: the
    // relaid strip still reaches the corner point (13, -3) region within a tile.
    float nearest = 1e9f;
    for (int r = 0; r < n; ++r) {
        const float d = Len(Sub(At(s.vertices, r, kColumns - 1), P2{13.0f, -3.0f}));
        if (d < nearest) nearest = d;
    }
    CHECK(nearest < 2.5f);
}

UNIT_TEST(ext_better_water_river_rows_skip_indices_that_are_not_a_strip) {
    std::vector<P2> left;
    std::vector<P2> right;
    for (int r = 0; r <= 6; ++r) {
        left.push_back({static_cast<float>(r), 3.0f});
        right.push_back({static_cast<float>(r), -3.0f});
    }
    Strip s = MakeStrip(left, right);
    std::swap(s.indices[6], s.indices[8]);   // the second quad's winding broken
    const std::vector<TerrainVertex> before = s.vertices;
    int skipped = 0;
    CHECK_EQ(RelayAll(s, &skipped), 0);
    CHECK_EQ(skipped, 1);
    for (size_t i = 0; i < before.size(); ++i) CHECK_EQ(s.vertices[i].position[0], before[i].position[0]);
}
