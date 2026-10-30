// ============================================================================
// tests/unit/terrain_ray_test.cpp -- WorldGeometry::HeightUnderPoint (0x1400b0f90), the
// terrain ray cast behind Map::GetTerrainPos (src/game/Game/worldgeometry_ray.cpp)
// ============================================================================
// Retail's terrain height is a vertical ray through the cell (trunc(x + half),
// trunc(y + half)) against that cell's quad split into two triangles, the diagonal
// alternating with (x + y) & 1 (0x1400aaa20), each tested by 0x1400aa810. So the height
// is piecewise PLANAR over the triangle that holds the point, and the normal written to
// outTri is that triangle's unit normal, written only on a hit. These checks compute the
// same surface analytically and compare it with the real body:
//   * heights and normals inside cells, for both diagonals;
//   * a point outside its truncated cell (x + half in (-1, 0)) misses: height 0, normal
//     untouched;
//   * a non-planar quad: the result is not the bilinear blend of its corners.
// ============================================================================
#include "unit.h"
#include "Game/WorldGeometry.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

uint64_t Mix(uint64_t x) {   // splitmix64
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// A WorldGeometry record over a plain heightmap: edge +0xC0, row stride +0xD0, the int16
// heightmap at +0xD8. Zeroed raw storage, as no test runs its ctor.
struct Terrain {
    int gridSize = 0;
    int N = 0;
    std::vector<int16_t> heights;
    alignas(16) uint8_t bytes[sizeof(WorldGeometry)];
    WorldGeometry* wg = nullptr;

    void Build(int grid, uint64_t seed) {
        gridSize = grid;
        N = grid + 1;
        heights.assign((size_t)N * N, 0);
        for (int i = 0; i < N * N; ++i)
            heights[(size_t)i] = (int16_t)((int)(Mix(seed ^ (uint64_t)i) % 4096) - 2048);
        std::memset(bytes, 0, sizeof(bytes));
        wg = reinterpret_cast<WorldGeometry*>(bytes);
        wg->_gridSize = gridSize;
        wg->_fieldD0 = N;
        wg->_heights.data = heights.data();
        wg->_heights.size = (int32_t)heights.size();
        wg->_heights.capacity = wg->_heights.size;
    }
    float H(int col, int row) const {
        return (float)heights[(size_t)(row * N + col)] * (1.0f / 256.0f);
    }
};

// The plane of the triangle holding (fx, fy) in cell (x, y), and its upward unit normal.
float Planar(const Terrain& t, int x, int y, double fx, double fy, double n[3]) {
    const double h00 = t.H(x, y), h10 = t.H(x + 1, y);
    const double h01 = t.H(x, y + 1), h11 = t.H(x + 1, y + 1);
    double dx, dy, h;
    if (((x + y) & 1) == 0) {   // diagonal (x+1, y) - (x, y+1)
        if (fx + fy <= 1.0) {
            dx = h10 - h00;
            dy = h01 - h00;
            h = h00 + dx * fx + dy * fy;
        } else {
            dx = h11 - h01;
            dy = h11 - h10;
            h = h11 - dx * (1.0 - fx) - dy * (1.0 - fy);
        }
    } else {                    // diagonal (x, y) - (x+1, y+1)
        if (fx >= fy) {
            dx = h10 - h00;
            dy = h11 - h10;
        } else {
            dx = h11 - h01;
            dy = h01 - h00;
        }
        h = h00 + dx * fx + dy * fy;
    }
    const double len = std::sqrt(dx * dx + dy * dy + 1.0);
    n[0] = -dx / len;
    n[1] = -dy / len;
    n[2] = 1.0 / len;
    return (float)h;
}

}  // namespace

UNIT_TEST(terrain_ray_height_and_normal_are_the_hit_triangles) {
    Terrain t;
    t.Build(32, 7);
    const int half = t.gridSize >> 1;
    long long checked = 0, evenCells = 0, oddCells = 0;
    for (int i = 0; i < 4000; ++i) {
        const uint64_t h = Mix(0xa11ce + (uint64_t)i);
        const int x = (int)(h % (uint64_t)t.gridSize);
        const int y = (int)((h >> 8) % (uint64_t)t.gridSize);
        // Inside the cell, away from its edges and the diagonal by more than float noise.
        const double fx = 0.02 + 0.96 * (double)((h >> 16) & 0xffff) / 65536.0;
        const double fy = 0.02 + 0.96 * (double)((h >> 32) & 0xffff) / 65536.0;
        if (std::fabs(fx + fy - 1.0) < 0.01 || std::fabs(fx - fy) < 0.01) continue;
        const float point[2] = { (float)(x - half) + (float)fx, (float)(y - half) + (float)fy };
        double want[3];
        const float wantH = Planar(t, x, y, (double)point[0] + half - x, (double)point[1] + half - y, want);
        float n[3] = { 7.0f, 7.0f, 7.0f };
        const float got = t.wg->HeightUnderPoint(point, n);
        CHECK(std::fabs(got - wantH) < 1e-3f);
        CHECK(std::fabs(n[0] - want[0]) < 1e-4 && std::fabs(n[1] - want[1]) < 1e-4 &&
              std::fabs(n[2] - want[2]) < 1e-4);
        ++checked;
        (((x + y) & 1) == 0 ? evenCells : oddCells) += 1;
    }
    CHECK(checked > 3000);
    CHECK(evenCells > 1000 && oddCells > 1000);
}

UNIT_TEST(terrain_ray_outside_the_truncated_cell_misses) {
    Terrain t;
    t.Build(16, 3);
    const int half = t.gridSize >> 1;
    // x + half in (-1, 0) truncates to cell 0, whose quad starts at x + half == 0: the ray
    // passes outside both triangles. Retail returns 0 and leaves the normal alone.
    const float point[2] = { -(float)half - 0.5f, 2.25f };
    float n[3] = { 7.0f, 8.0f, 9.0f };
    const float got = t.wg->HeightUnderPoint(point, n);
    CHECK_EQ(got, 0.0f);
    CHECK(n[0] == 7.0f && n[1] == 8.0f && n[2] == 9.0f);
    // The null normal out is allowed (PathTool, the tile renderer).
    CHECK_EQ(t.wg->HeightUnderPoint(point, nullptr), 0.0f);
}

UNIT_TEST(terrain_ray_is_not_bilinear) {
    Terrain t;
    t.Build(4, 1);
    // One saddle cell (0,0): corners 0, 0, 0 and 1 (h11). Bilinear at the centre gives
    // 0.25; the planar triangles (the cell is even: diagonal (1,0)-(0,1)) give 0 on the
    // diagonal and 0.5 * h11 at (0.75, 0.75).
    std::fill(t.heights.begin(), t.heights.end(), (int16_t)0);
    t.heights[(size_t)(1 * t.N + 1)] = 256;   // h(1,1) = 1.0
    const int half = t.gridSize >> 1;
    const float centre[2] = { (float)(0 - half) + 0.5f, (float)(0 - half) + 0.5f };
    CHECK(std::fabs(t.wg->HeightUnderPoint(centre, nullptr)) < 1e-4f);
    const float upper[2] = { (float)(0 - half) + 0.75f, (float)(0 - half) + 0.75f };
    CHECK(std::fabs(t.wg->HeightUnderPoint(upper, nullptr) - 0.5f) < 1e-4f);
}

namespace {

// TerrainHeightOnly (the height-only path HeightUnderPoint takes with no normal out) against
// the generic body, bit for bit: one point, every heightmap below.
struct RaySweep {
    long long points = 0, hits = 0, misses = 0, even = 0, odd = 0, diverged = 0;

    void Probe(Terrain& t, float px, float py) {
        const float point[2] = { px, py };
        const float fast = t.wg->HeightUnderPoint(point, nullptr);
        const float ref = t.wg->HeightUnderPointReference(point, nullptr);
        if (std::memcmp(&fast, &ref, sizeof fast) != 0) {
            if (diverged < 8)
                std::printf("  diverged at (%a, %a): fast %a, reference %a\n", px, py, fast, ref);
            ++diverged;
        }
        // Hit or miss by the reference's normal out (written on a hit only).
        float n[3] = { 7.0f, 7.0f, 7.0f };
        t.wg->HeightUnderPointReference(point, n);
        (n[0] == 7.0f && n[1] == 7.0f && n[2] == 7.0f ? misses : hits) += 1;
        const float half = (float)(t.gridSize >> 1);
        const float ox = px + half, oy = py + half;
        if (ox > -4194304.0f && ox < 4194304.0f && oy > -4194304.0f && oy < 4194304.0f)
            ((((int)ox + (int)oy) & 1) == 0 ? even : odd) += 1;
        ++points;
    }
};

}  // namespace

UNIT_TEST(terrain_ray_height_only_path_is_bit_identical) {
    RaySweep s;
    const int grids[] = { 4, 16, 32 };
    for (int g = 0; g < 3; ++g) {
        for (uint64_t seed = 1; seed <= 4; ++seed) {
            Terrain t;
            t.Build(grids[g], seed * 977 + (uint64_t)g);
            if (seed == 2) {   // the int16 extremes, alternating
                for (size_t i = 0; i < t.heights.size(); ++i)
                    t.heights[i] = (Mix(i ^ 0x5eed) & 1) ? (int16_t)32767 : (int16_t)-32768;
            } else if (seed == 3) {   // flat: zero products everywhere
                std::fill(t.heights.begin(), t.heights.end(), (int16_t)0);
            } else if (seed == 4) {   // full int16 range
                for (size_t i = 0; i < t.heights.size(); ++i)
                    t.heights[i] = (int16_t)(Mix(i * 31 + seed) & 0xffff);
            }
            const int half = t.gridSize >> 1;
            // Every cell from two outside the map to two past it: interiors, both diagonals,
            // the cell's edges and corners, a float step either side of each.
            const float offs[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f / 3.0f, 2.0f / 3.0f,
                                   0.1f, 0.9f, 0.999f, 1e-7f };
            for (int cx = -3; cx <= t.gridSize + 2; ++cx) {
                for (int cy = -3; cy <= t.gridSize + 2; ++cy) {
                    const float bx = (float)(cx - half);
                    const float by = (float)(cy - half);
                    for (float fx : offs) {
                        for (float fy : offs) {
                            const float px = bx + fx;
                            const float py = by + fy;
                            s.Probe(t, px, py);
                            s.Probe(t, std::nextafter(px, -1e9f), std::nextafter(py, 1e9f));
                            s.Probe(t, std::nextafter(px, 1e9f), std::nextafter(py, -1e9f));
                        }
                        s.Probe(t, bx + fx, by + (1.0f - fx));   // the even diagonal
                        s.Probe(t, bx + fx, by + fx);            // the odd diagonal
                    }
                }
            }
            // Random points over and around the map, and random bit patterns.
            for (int i = 0; i < 20000; ++i) {
                const uint64_t h = Mix(seed * 0x9e37 + (uint64_t)i + (uint64_t)g * 1000003);
                const float span = (float)(t.gridSize + 8);
                const float px = ((float)(h & 0xffffff) / 16777216.0f - 0.5f) * span;
                const float py = ((float)((h >> 24) & 0xffffff) / 16777216.0f - 0.5f) * span;
                s.Probe(t, px, py);
                uint32_t bxb = (uint32_t)(h >> 32), byb = (uint32_t)h;
                float rx, ry;
                std::memcpy(&rx, &bxb, 4);
                std::memcpy(&ry, &byb, 4);
                if (!std::isnan(rx) && !std::isnan(ry) && std::fabs(rx) < 1e6f && std::fabs(ry) < 1e6f)
                    s.Probe(t, rx, ry);
            }
            // Far off the map, the range bound, and the non-finite inputs (the generic body).
            const float far[] = { -1e30f, -4194304.0f, -4194303.5f, -1e6f, -65536.0f,
                                  65536.0f, 1e6f, 4194303.5f, 4194304.0f, 1e30f,
                                  INFINITY, -INFINITY, NAN, -0.0f };
            for (float a : far) {
                s.Probe(t, a, 0.5f);
                s.Probe(t, 0.5f, a);
                s.Probe(t, a, a);
            }
        }
    }
    std::printf("  %lld points (%lld hits, %lld misses; %lld even / %lld odd cells in range), %lld diverged\n",
                s.points, s.hits, s.misses, s.even, s.odd, s.diverged);
    CHECK_EQ(s.diverged, 0LL);
    CHECK(s.hits > 100000 && s.misses > 1000);
    CHECK(s.even > 100000 && s.odd > 100000);
}
