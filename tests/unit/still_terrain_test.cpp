// ============================================================================
// tests/unit/still_terrain_test.cpp -- STILLTERRAIN: the last iteration's terrain-query memo
// of a still character (src/game/Component/still_terrain.h, citizenai_executor.cpp)
// ============================================================================
// The helpers are checked directly, then two copies of one scripted world run side by side.
// Both hold a fake heightmap (int16, N x N, the 1/256 scale, the edge / stride fields), a
// cell array with floorCount, and a stub picking scene whose Intersect counts its queries
// (the real one bumps the scene's query stamp, so a skipped Intersect is visible). World A
// always runs a literal copy of Map::GetTerrainPos (mapquery.cpp, flag 0, no normal); world
// B wraps it exactly as the move tick does (Find / Hit / FromBits on a hit, the full query
// and Put unless MayReachFloor on a miss). A script moves characters, writes heightmap words
// (the three sim writers' idiom: store, then Bump), places and removes floors (floorCount +
// the scene, then Bump) and rebuilds the world (Bump). After every step every character's
// query result bits and the scene's query count must be identical. Broken variants (a
// writer that forgets its bump; a memo that also records floor cells) must diverge.
//
// THE REAL BODY (2026-10-17): the last test drives the production Map::GetTerrainPos
// (src/game/Game/mapquery.cpp, linked) over a WorldGeometry record built from the same
// heightmap, through a GameUpdateLoop record the test installs as the singleton, and
// compares every result bit with World A's copy on random and edge queries (off-grid,
// cell corners, negative). The copy is of the terrain ray cast (WorldGeometry::
// HeightUnderPoint, worldgeometry_ray.cpp). The floor branch (Picking::Scene::Intersect)
// cannot be linked, so the real body is driven with no map grid: the no-floor path, which
// is every query the copy's SampleHeight answers. The copy's text is also pinned by copy_drift_test.cpp.
// ============================================================================
#include "unit.h"
#include "Component/still_terrain.h"
#include "Game/GameUpdateLoop.h"     // the singleton the real GetTerrainPos reads
#include "Game/Map.h"
#include "Game/WorldGeometry.h"
#include "math/Math.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using StillTerrain::Bits;

uint64_t Mix(uint64_t x) {   // splitmix64
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
uint64_t Hash(uint64_t a, uint64_t b, uint64_t c = 0, uint64_t d = 0) {
    return Mix(Mix(Mix(Mix(a) ^ b) ^ c) ^ d);
}

struct Floor { int x0, y0, w, h; float z; bool live; };

// The world: what GetTerrainPos(flag 0) reads.
struct World {
    int dim = 0;                       // Map::size (cells per edge)
    int gridSize = 0;                  // WorldGeometry::_gridSize (heightmap edge, dim here)
    int N = 0;                         // WorldGeometry::_fieldD0 (gridSize + 1)
    std::vector<int16_t> heights;      // WorldGeometry::_heights
    std::vector<int8_t> floorCount;    // MapCell::floorCount
    std::vector<Floor> floors;         // the map's picking scene (Map +0x08)
    long long sceneQueries = 0;        // the scene's query stamp (Intersect bumps it)
    uint64_t epoch = 1;                // this world's StillTerrain::g_epoch
    StillTerrain::MemoTable memo;      // world B only

    void Build(int d, uint64_t seed) {
        dim = d; gridSize = d; N = d + 1;
        heights.assign((size_t)N * N, 0);
        for (int i = 0; i < N * N; ++i) heights[(size_t)i] = (int16_t)(Hash(seed, (uint64_t)i) % 4096) - 1024;
        floorCount.assign((size_t)d * d, 0);
        floors.clear();
        ++epoch;                       // world create / load
    }

    // WorldGeometry::HeightUnderPoint (src/game/Game/worldgeometry_ray.cpp: TerrainHeightUnder,
    // RayTestCell, Pf_RayTriIntersect_aa810), literally, with no normal out.
    static int RayTri(const float* P, const float* D, const float* maxT, const float* V0,
                      const float* V1, const float* V2, float* outT) {
        const Math::Vector3 Dv(D[0], D[1], D[2]);
        Math::Vector3 edge1(V1[0] - V0[0], V1[1] - V0[1], V1[2] - V0[2]);
        Math::Vector3 edge2(V2[0] - V0[0], V2[1] - V0[1], V2[2] - V0[2]);
        Math::Vector3 pvec = Dv.Cross(edge2);
        float det = edge1.Dot(pvec).v;
        Math::Vector3 tvec(P[0] - V0[0], P[1] - V0[1], P[2] - V0[2]);
        Math::Vector3 qvec = tvec.Cross(edge1);
        float u = tvec.Dot(pvec).v;
        float v = Dv.Dot(qvec).v;
        uint32_t epsBits = 0xb8d1b717u;
        float eps;
        std::memcpy(&eps, &epsBits, 4);
        if (det <= 0.0f) return 0;
        if (eps > u) return 0;
        if (u > det) return 0;
        if (eps > v) return 0;
        if (v + u > det) return 0;
        float tdot = edge2.Dot(qvec).v;
        float t = (1.0f / det) * tdot;
        *outT = t;
        if (t < 0.0f) return 0;
        if (*maxT < t) return 0;
        return 1;
    }
    float SampleHeight(float worldX, float worldZ) const {
        const float origin[3] = { worldX, worldZ, 100.0f };   // DAT_140172208
        const float dir[3] = { 0.0f, 0.0f, -1.0f };           // DAT_140172280
        const float maxT = 200.0f;                            // DAT_140172228
        const float halfF = (float)(gridSize >> 1);
        const int x = (int)(origin[0] + halfF), y = (int)(origin[1] + halfF);
        const int half = gridSize >> 1;
        auto H = [this](int col, int row) -> float {
            const int cc = col < 0 ? 0 : (col >= N ? N - 1 : col);
            const int cr = row < 0 ? 0 : (row >= N ? N - 1 : row);
            return (float)(int)heights[(size_t)((int64_t)cr * N + cc)] * (1.0f / 256.0f);
        };
        const float bx = (float)(x - half), by = (float)(y - half);
        const float V00[3] = { bx,        by,        H(x,     y)     };
        const float V01[3] = { bx + 1.0f, by,        H(x + 1, y)     };
        const float V10[3] = { bx,        by + 1.0f, H(x,     y + 1) };
        const float V11[3] = { bx + 1.0f, by + 1.0f, H(x + 1, y + 1) };
        float maxTbuf = maxT;
        float t = 0.0f;
        bool hit;
        if (((x + y) & 1) == 0) {
            hit = RayTri(origin, dir, &maxTbuf, V00, V01, V10, &t) != 0;
            if (hit) maxTbuf = t;
            if (RayTri(origin, dir, &maxTbuf, V01, V11, V10, &t) != 0) hit = true;
        } else {
            hit = RayTri(origin, dir, &maxTbuf, V00, V01, V11, &t) != 0;
            if (hit) maxTbuf = t;
            if (RayTri(origin, dir, &maxTbuf, V00, V11, V10, &t) != 0) hit = true;
        }
        const float z = t * dir[2] + origin[2];
        return hit ? z : 0.0f;
    }
    int CellOf(float x, float y) const {
        int ix = (int)(x + (float)(dim >> 1));
        int iy = (int)(y + (float)(dim >> 1));
        if (ix < 0) ix = 0;
        if (ix >= dim) ix = dim - 1;
        if (iy < 0) iy = 0;
        if (iy >= dim) iy = dim - 1;
        return dim * iy + ix;
    }
    // Picking::Scene::Intersect, stubbed: a vertical ray from z 50 down; the highest live
    // floor whose rect holds the cell. Every query bumps the stamp, hit or miss.
    int Intersect(float x, float y, float* hit) {
        ++sceneQueries;
        const int c = CellOf(x, y), cx = c % dim, cy = c / dim;
        int best = -1;
        for (int i = 0; i < (int)floors.size(); ++i) {
            const Floor& f = floors[(size_t)i];
            if (!f.live || cx < f.x0 || cy < f.y0 || cx >= f.x0 + f.w || cy >= f.y0 + f.h) continue;
            if (best < 0 || f.z > floors[(size_t)best].z) best = i;
        }
        if (best < 0) return -1;
        hit[0] = x + 0.0f; hit[1] = y; hit[2] = floors[(size_t)best].z;
        return best;
    }
    // Map::GetTerrainPos(out, in, flag 0, nullptr), literally (the canonical grid is this).
    float* GetTerrainPos(float* out, const float* in) {
        const float inX = in[0], inZ = in[1];
        const float height = SampleHeight(inX, inZ);
        if (floorCount[(size_t)CellOf(inX, inZ)] > 0) {
            float hit[3] = { 0, 0, 0 };
            if (Intersect(inX, inZ, hit) >= 0 && hit[2] > height) {
                out[0] = hit[0]; out[1] = hit[1]; out[2] = hit[2];
                return out;
            }
        }
        out[0] = inX; out[1] = inZ; out[2] = height;
        return out;
    }
    bool MayReachFloor(float x, float y) const { return floorCount[(size_t)CellOf(x, y)] > 0; }
};

enum Broken { kNone = 0, kNoBumpHeight = 1, kNoBumpFloor = 2, kRecordFloor = 3 };

// The tick's terrain block for one still character (citizenai_executor.cpp), world B.
float* MemoQuery(World& w, int idx, float* triOut, const float* pos, Broken broken,
                 long long& hits) {
    const StillTerrain::Memo* tm = w.memo.Find(idx);
    if (StillTerrain::Hit(tm, pos[0], pos[1], w.epoch)) {
        ++hits;
        triOut[0] = pos[0]; triOut[1] = pos[1]; triOut[2] = StillTerrain::FromBits(tm->z);
        return triOut;
    }
    float* pf = w.GetTerrainPos(triOut, pos);
    if (broken == kRecordFloor || !w.MayReachFloor(pos[0], pos[1]))
        w.memo.Put(idx, pos[0], pos[1], pf[2], w.epoch);
    return pf;
}

struct Char { float x, y; };

struct RunResult { long long hits = 0, queries = 0, floorQueries = 0, bumps = 0; bool identical = true; };

RunResult RunTwoWorlds(Broken broken, int steps, uint64_t seed) {
    const int kDim = 32, kChars = 40;
    World a, b;
    a.Build(kDim, seed); b.Build(kDim, seed);
    std::vector<Char> ch((size_t)kChars);
    for (int i = 0; i < kChars; ++i) {
        const uint64_t h = Hash(seed, 1, (uint64_t)i);
        ch[(size_t)i] = { (float)(int)(h % 30) - 15.0f + 0.25f * (float)((h >> 8) % 4),
                          (float)(int)((h >> 16) % 30) - 15.0f + 0.125f * (float)((h >> 24) % 8) };
    }
    RunResult r;
    for (int s = 0; s < steps; ++s) {
        const uint64_t hs = Hash(seed, 2, (uint64_t)s);
        // 1. the script's writers, applied to both worlds the same way
        const int kind = (int)(hs % 16);
        if (kind == 0 || kind == 1) {   // heightmap words near a character (Reset / Quarry / Terraform idiom)
            const Char& c = ch[(size_t)((hs >> 8) % kChars)];
            const int n = 1 + (int)((hs >> 16) % 4);
            for (int k = 0; k < n; ++k) {
                const int hx = (int)(c.x + 16.0f) + (int)((hs >> (20 + k)) % 2);
                const int hy = (int)(c.y + 16.0f) + (int)((hs >> (24 + k)) % 2);
                if (hx < 0 || hy < 0 || hx >= a.N || hy >= a.N) continue;
                const int16_t v = (int16_t)((int)(Hash(hs, (uint64_t)k) % 2048) - 512);
                a.heights[(size_t)(hy * a.N + hx)] = v; ++a.epoch;
                b.heights[(size_t)(hy * b.N + hx)] = v;
                if (broken != kNoBumpHeight) ++b.epoch;
                ++r.bumps;
            }
        } else if (kind == 2) {   // place a floor over a character's cell (FloorComponent::PlaceFloor)
            const Char& c = ch[(size_t)((hs >> 8) % kChars)];
            const int cell = a.CellOf(c.x, c.y);
            Floor f{ cell % kDim, cell / kDim, 1 + (int)((hs >> 16) % 3), 1 + (int)((hs >> 20) % 3),
                     (float)((int)((hs >> 24) % 64)) * 0.25f - 4.0f, true };
            for (World* w : { &a, &b }) {
                for (int j = 0; j < f.h; ++j)
                    for (int i = 0; i < f.w; ++i)
                        if (f.x0 + i < kDim && f.y0 + j < kDim)
                            ++w->floorCount[(size_t)((f.y0 + j) * kDim + f.x0 + i)];
                w->floors.push_back(f);
            }
            ++a.epoch;
            if (broken != kNoBumpFloor) ++b.epoch;
            ++r.bumps;
        } else if (kind == 3) {   // remove a live floor (Map::RemoveFloor)
            int pick = -1;
            for (int i = 0; i < (int)a.floors.size(); ++i)
                if (a.floors[(size_t)i].live && (pick < 0 || (Hash(hs, (uint64_t)i) & 1))) pick = i;
            if (pick >= 0) {
                for (World* w : { &a, &b }) {
                    Floor& f = w->floors[(size_t)pick];
                    for (int j = 0; j < f.h; ++j)
                        for (int i = 0; i < f.w; ++i)
                            if (f.x0 + i < kDim && f.y0 + j < kDim)
                                --w->floorCount[(size_t)((f.y0 + j) * kDim + f.x0 + i)];
                    f.live = false;
                }
                ++a.epoch;
                if (broken != kNoBumpFloor) ++b.epoch;
                ++r.bumps;
            }
        } else if (kind == 4 && (hs >> 8) % 50 == 0) {   // a load: a new world, the memo table survives
            const uint64_t ws = Hash(seed, 3, (uint64_t)s);
            a.Build(kDim, ws); b.Build(kDim, ws);
            r.bumps += 1;
        }
        // 2. some characters move (they are not "still" this step: the tick skips the memo)
        std::vector<bool> still((size_t)kChars, true);
        for (int i = 0; i < kChars; ++i) {
            const uint64_t hc = Hash(hs, 4, (uint64_t)i);
            if (hc % 8 == 0) {
                Char& c = ch[(size_t)i];
                c.x += ((float)(int)((hc >> 8) % 5) - 2.0f) * 0.375f;
                c.y += ((float)(int)((hc >> 12) % 5) - 2.0f) * 0.375f;
                if (c.x < -16.0f) c.x = -16.0f;
                if (c.x > 15.5f) c.x = 15.5f;
                if (c.y < -16.0f) c.y = -16.0f;
                if (c.y > 15.5f) c.y = 15.5f;
                still[(size_t)i] = false;
            } else if (hc % 97 == 1) {   // a teleport within the same step: still, new x / y
                Char& c = ch[(size_t)i];
                c.x = (float)(int)((hc >> 16) % 30) - 15.0f;
                c.y = (float)(int)((hc >> 24) % 30) - 15.0f;
            }
        }
        // 3. the last iteration's terrain query of every character; index sharing: two
        //    characters map to one CharacterGrid index now and then
        for (int i = 0; i < kChars; ++i) {
            const int idx = (Hash(hs, 5, (uint64_t)i) % 211 == 0) ? (i ^ 1) : i;
            const float pos[3] = { ch[(size_t)i].x, ch[(size_t)i].y, 0.0f };
            float outA[4] = { 0, 0, 0, 0 }, outB[4] = { 0, 0, 0, 0 };
            const float* pa = a.GetTerrainPos(outA, pos);
            const float* pb = nullptr;
            if (still[(size_t)i]) {
                ++r.queries;
                if (a.MayReachFloor(pos[0], pos[1])) ++r.floorQueries;
                pb = MemoQuery(b, idx, outB, pos, broken, r.hits);
            } else {
                pb = b.GetTerrainPos(outB, pos);
            }
            if (Bits(pa[0]) != Bits(pb[0]) || Bits(pa[1]) != Bits(pb[1]) || Bits(pa[2]) != Bits(pb[2]))
                r.identical = false;
        }
        if (a.sceneQueries != b.sceneQueries) r.identical = false;
        if (!r.identical) return r;
    }
    return r;
}

}  // namespace

UNIT_TEST(still_terrain_memo_table_and_hit) {
    StillTerrain::MemoTable t;
    CHECK(t.Find(0) == nullptr);
    CHECK(!StillTerrain::Hit(t.Find(0), 0.0f, 0.0f, 1));
    t.Put(3, 1.5f, -2.25f, 7.0f, 5);
    const StillTerrain::Memo* m = t.Find(3);
    CHECK(m != nullptr);
    CHECK(StillTerrain::Hit(m, 1.5f, -2.25f, 5));
    CHECK(!StillTerrain::Hit(m, 1.5f, -2.25f, 6));    // another epoch
    CHECK(!StillTerrain::Hit(m, 1.5f, -2.0f, 5));     // another y
    CHECK(!StillTerrain::Hit(m, -0.0f, -2.25f, 5));   // bits, not values
    CHECK_EQ(Bits(StillTerrain::FromBits(m->z)), Bits(7.0f));
    // A never-written entry (epoch 0) never hits, even for (+0, +0): g_epoch starts at 1.
    const StillTerrain::Memo* z = t.Find(0);
    CHECK(z != nullptr);
    CHECK(!StillTerrain::Hit(z, 0.0f, 0.0f, StillTerrain::g_epoch));
    CHECK(StillTerrain::g_epoch >= 1u);
    t.Put(-1, 0, 0, 0, 1);   // ignored
    CHECK(t.Find(-1) == nullptr);
}

UNIT_TEST(still_terrain_shadow_diff_and_window) {
    const float pf[3] = { 1.0f, 2.0f, 3.0f };
    CHECK_EQ(StillTerrain::ShadowDiff(1.0f, 2.0f, 3.0f, pf), 0LL);
    CHECK_EQ(StillTerrain::ShadowDiff(-1.0f, 2.0f, 3.0f, pf), 1LL);
    CHECK_EQ(StillTerrain::ShadowDiff(1.0f, 2.5f, 3.5f, pf), 6LL);
    StillTerrain::ProfWindow w;
    w.Add(100, false); w.Add(20, true); w.Add(20, true); w.Add(60, false);
    CHECK_EQ(w.calls, 4LL);
    CHECK_EQ(w.hits, 2LL);
    CHECK_EQ(w.Full(), 2LL);
    CHECK_EQ(w.FullTicks(), 160LL);
    CHECK(w.SavedTicks() > 119.0 && w.SavedTicks() < 121.0);   // 2 x (80 - 20)
    w.Reset();
    CHECK_EQ(w.calls, 0LL);
}

UNIT_TEST(still_terrain_two_worlds_bit_identical) {
    long long hits = 0, queries = 0, floorQ = 0, bumps = 0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        const RunResult r = RunTwoWorlds(kNone, 4000, seed);
        CHECK(r.identical);
        hits += r.hits; queries += r.queries; floorQ += r.floorQueries; bumps += r.bumps;
    }
    std::printf("  still_terrain two worlds: %lld still queries, %lld memo hits, %lld on floor cells, %lld bumps\n",
                queries, hits, floorQ, bumps);
    CHECK(hits > queries / 2);   // the memo really is exercised
    CHECK(floorQ > 0);           // and floors really are in play
}

UNIT_TEST(still_terrain_broken_variants_diverge) {
    // A heightmap writer that forgets its bump, a floor writer that forgets its bump, and a
    // memo that also records floor cells (it skips Intersect: the scene's stamp diverges).
    for (Broken v : { kNoBumpHeight, kNoBumpFloor, kRecordFloor }) {
        bool diverged = false;
        for (uint64_t seed = 1; seed <= 6 && !diverged; ++seed)
            diverged = !RunTwoWorlds(v, 4000, seed).identical;
        CHECK(diverged);
    }
}

// ---- the real Map::GetTerrainPos against the copy -------------------------------------------
UNIT_TEST(still_terrain_real_get_terrain_pos_matches_the_copy) {
    for (uint64_t seed = 1; seed <= 3; ++seed) {
        const int dim = seed == 1 ? 8 : (seed == 2 ? 33 : 64);
        World w;
        w.Build(dim, seed);

        // The WorldGeometry record the body reads: edge +0xC0, row stride +0xD0, the int16
        // heightmap at +0xD8. Zeroed raw storage, as the test never runs its ctor.
        alignas(16) static uint8_t wgBytes[sizeof(WorldGeometry)];
        std::memset(wgBytes, 0, sizeof(wgBytes));
        WorldGeometry* wg = reinterpret_cast<WorldGeometry*>(wgBytes);
        wg->_gridSize = w.gridSize;
        wg->_fieldD0 = w.N;
        wg->_heights.data = w.heights.data();
        wg->_heights.size = static_cast<int32_t>(w.heights.size());
        wg->_heights.capacity = wg->_heights.size;

        // The loop record: m_worldGeometry set, m_game null (no canonical grid: the floor
        // branch, Picking::Scene::Intersect, is never reached).
        alignas(16) static uint8_t loopBytes[sizeof(GameUpdateLoop)];
        std::memset(loopBytes, 0, sizeof(loopBytes));
        GameUpdateLoop* loop = reinterpret_cast<GameUpdateLoop*>(loopBytes);
        loop->m_worldGeometry = wg;
        loop->m_game = nullptr;
        GameUpdateLoop::_singleton = loop;
        // `this` is only compared with the (null) canonical grid; any non-null Map* will do.
        alignas(16) static uint8_t mapBytes[sizeof(Map)];
        Map* someMap = reinterpret_cast<Map*>(mapBytes);

        long long compared = 0, bitDiffs = 0;
        auto query = [&](float x, float y) {
            const float in[2] = { x, y };
            float a[3] = { 0, 0, 0 }, b[3] = { 0, 0, 0 };
            float* pa = w.GetTerrainPos(a, in);
            float* pb = someMap->GetTerrainPos(b, in, '\0', nullptr);
            CHECK(pa == a && pb == b);
            ++compared;
            if (std::memcmp(a, b, sizeof(a)) != 0) ++bitDiffs;
        };
        // Random queries inside and well outside the grid (a miss: height 0), cell corners
        // and centres (on a triangle edge or the diagonal), and negative coordinates.
        const int half = dim >> 1;
        for (int i = 0; i < 2000; ++i) {
            const uint64_t h = Hash(seed, 0x7e55u, static_cast<uint64_t>(i));
            const float span = static_cast<float>(half) * 2.0f + 16.0f;
            const float x = (static_cast<float>(h & 0xffff) / 65536.0f - 0.5f) * span;
            const float y = (static_cast<float>((h >> 16) & 0xffff) / 65536.0f - 0.5f) * span;
            query(x, y);
        }
        for (int cx = -half - 2; cx <= half + 2; ++cx)
            for (int cy = -half - 2; cy <= half + 2; ++cy) {
                query(static_cast<float>(cx), static_cast<float>(cy));
                query(static_cast<float>(cx) + 0.5f, static_cast<float>(cy) - 0.25f);
            }
        query(-0.0f, 0.0f);
        query(1e-7f, -1e-7f);
        query(1e9f, -1e9f);                 // far off the grid: a degenerate quad, a miss
        GameUpdateLoop::_singleton = nullptr;

        CHECK(compared > 2000);
        CHECK_EQ(bitDiffs, 0LL);
        if (bitDiffs) std::printf("    dim %d seed %llu: %lld of %lld queries differ\n", dim,
                                  static_cast<unsigned long long>(seed), bitDiffs, compared);
    }
}
