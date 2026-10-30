// src/game/Game/mapquery.cpp
// Terrain height / world-position query — the keystone "Map::GetTerrainPos" utility.
//
// Reimplements FUN_14009f3e0 (CharacterComponent_impl_f3e0), exposed project-wide as
// fn_MapGetTerrainPos. This single function is referenced by ~100 call sites (camera
// placement, tile classification, every tool/work/component that maps a world XY to a
// terrain point) and is exe-only, so standalone needs a real body for it.
//
// The height is WorldGeometry::HeightUnderPoint (0x1400b0f90, Game/worldgeometry_ray.cpp):
// a vertical ray from z 100 down through the cell ((int)(x + half), (int)(y + half)) --
// a TRUNCATING index, not floor -- tested against that cell's quad as two triangles
// (0x1400aaa20), the diagonal chosen by (x + y) & 1, each by the Moller-Trumbore test
// 0x1400aa810. The height is piecewise planar per triangle (not bilinear), the normal is the
// unit normal of the triangle the ray hit, written into outTri only on a hit; a miss (a point
// outside its truncated cell, e.g. x + half in (-1, 0), or a degenerate far-off quad) returns
// height 0 and leaves outTri as the caller set it.
// Output Vector3 = {worldX, worldZ, height} (matching f3e0's field order).
//
// flag (param_4): when flag==0 AND the cell carries a placed floor (cellRec+0x2d > 0),
// the original refines the height by vertically ray-casting the map's Picking::Scene and
// taking the hit when it is ABOVE the terrain. That branch IS the "walk on the deck"
// mechanism for docks and bridges: a citizen's world Z is recomputed every frame by
// CharacterComponent's move tick (0x1401094c0) as `max(f3e0(pos).z, desc+0x44)`, so
// whatever Z the work action resolved is discarded on the next tick and the standing
// height is entirely whatever this function returns. With the refine missing, a fisherman
// on a dock stood on the riverbed under it. Implemented below, gated on BANISHED_LIVE_FLOOR
// (the producer side — FloorComponent registering the deck mesh — shares that gate);
// default-on since 2026-08-29, so the env var is a kill-switch (=0), not an opt-in.
//
// Verified against Runtime/exe decompiles + worldgen.cpp WG_* offsets (2026-05-31);
// picking-refine branch disassembly-verified at 0x14009f4c6..0x14009f582 (2026-08-29).

#include "math/Math.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "picking/Scene.h"     // ENDGAME B.3.7 callers (K2)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "Game/exe_bodies.h"
#include "Game/Map.h"
#include "Game/WorldGeometry.h"

// BANISHED_FLOOR_TRACE is gone (ENDGAME C.2). The symbol stays only until
// src/game/Component/floorcomponent.cpp drops its call sites; it is dead.

// ---------------------------------------------------------------------------
// Picking refine — the flag==0 + cellRec+0x2d>0 branch of the original
// ---------------------------------------------------------------------------
// Disassembly-verified argument build (0x14009f4f4..0x14009f534):
//   RCX = param_1 + 8                    — the map's OWN Picking::Scene (NOT gw+0x70)
//   RDX = &{inX, inZ, DAT_1401721e8}     — ray start,  DAT_1401721e8 = 50.0f
//   R8  = &{0, 0, DAT_140172280}         — ray dir,    DAT_140172280 = -1.0f (straight down)
//   R9  = &DAT_140172208                 — max dist,   DAT_140172208 = 100.0f
//   [RSP+0x20] = &hitPos (Vector3), [RSP+0x28] = &hitNormal (Vector3)
// Accepted only when the call returns >= 0 AND hitPos.z > the heightmap height
// (0x14009f53a JS / 0x14009f543 COMISS+JBE) — so a floor BELOW the terrain never wins.
static constexpr float kRayStartZ  = 50.0f;    // DAT_1401721e8
static constexpr float kRayDirZ    = -1.0f;    // DAT_140172280
static constexpr float kRayMaxDist = 100.0f;   // DAT_140172208


// The map grid the refine indexes. Retail reads param_1 directly and every retail call
// site passes *(gw+0xf8); our ported call sites are not all provably doing so, and a wrong
// param_1 would read a bogus dim and a bogus cell-array pointer. So resolve the canonical
// grid from the singleton and only refine when param_1 agrees (or is null).
static Map* CanonicalMapGrid(Map* game) {
    if (GameUpdateLoop::_singleton == nullptr) return nullptr;
    Map* canonical = GameUpdateLoop::_singleton->m_game;
    if (!canonical) return nullptr;
    if (game && game != canonical) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::fprintf(stderr, "[MapQuery] param_1=%p != map grid %p — skipping the "
                                 "picking refine for this call site.\n", game, (void*)canonical);
        }
        return nullptr;
    }
    return canonical;
}

// FUN_14009f3e0 — Map terrain-position query. param_1 = the map grid (int*, *(gw+0xf8)),
// outPos3=float[3] result, inXY2=float[2+] {worldX, worldZ}, flag, outTri = out normal.
float* Map::GetTerrainPos(float* outPos3, const float* inXY2,
                                          char flag, float* outTri) {
    const float inX = inXY2[0];
    const float inZ = inXY2[1];

    WorldGeometry* worldGeom = nullptr;
    if (GameUpdateLoop::_singleton != nullptr)
        worldGeom = GameUpdateLoop::_singleton->m_worldGeometry;

    // 0x14009f472: CALL 0x1400b0f90 (gw+0x110, the point, param_5). outTri receives the hit
    // triangle's unit normal (0x1400aa810 stores [0], [1], [2] on a hit only).
    float* tri = outTri;
    const float height = worldGeom ? worldGeom->HeightUnderPoint(inXY2, tri) : 0.0f;

    if (flag == 0) {
        Map* mapGrid = CanonicalMapGrid(this);
        const MapCell* cells = mapGrid ? mapGrid->cells.GetData() : nullptr;
        const int dim = mapGrid ? mapGrid->size : 0;
        if (cells && dim > 0) {
            int ix = (int)(inX + (float)(dim >> 1));
            int iy = (int)(inZ + (float)(dim >> 1));
            if (ix < 0)    ix = 0;
            if (ix >= dim) ix = dim - 1;
            if (iy < 0)    iy = 0;
            if (iy >= dim) iy = dim - 1;
            // Signed byte compared > 0, exactly as 0x14009f4e9 (CMP .. ; JLE).
            const int8_t floorCount = cells[(int64_t)dim * iy + ix].floorCount;
            if (floorCount > 0) {
                {
                    const Math::Vector3 rayStart(inX, inZ, kRayStartZ);
                    const Math::Vector3 rayDir(0.0f, 0.0f, kRayDirZ);
                    const Math::Scalar  maxDist(kRayMaxDist);
                    Math::Vector3 hitPos(0.0f, 0.0f, 0.0f);
                    Math::Vector3 hitNormal(0.0f, 0.0f, 0.0f);
                    // Picking::Scene::Intersect (import slot 0x140152428); the Scene is
                    // embedded at Map +0x08.
                    const int hit = mapGrid->pickingScene.Intersect(
                        rayStart, rayDir, maxDist, hitPos, hitNormal);
                    if (hit >= 0 && hitPos.z > height) {
                        if (tri) {
                            tri[0] = hitNormal.x;
                            tri[1] = hitNormal.y;
                            tri[2] = hitNormal.z;
                        }
                        outPos3[0] = hitPos.x;
                        outPos3[1] = hitPos.y;
                        outPos3[2] = hitPos.z;
                        return outPos3;
                    }
                }
            }
        }
    }

    outPos3[0] = inX;
    outPos3[1] = inZ;
    outPos3[2] = height;
    return outPos3;
}

// [STILLTERRAIN -- NOT RETAIL, faithful perf work; Component/still_terrain.h,
// docs/extensions/handins/STILLTERRAIN.md] The flag==0 branch condition of GetTerrainPos
// above, without its side effects (no warning line, no ray cast): true when the query at
// (inX, inZ) on `game` may reach Picking::Scene::Intersect. A `game` that is not the
// canonical grid answers true (conservative: the caller then does not memoise). Keep in
// step with GetTerrainPos; the StillTerrain shadow (COMPPROF=4) checks the memo it gates.
bool MapQuery_MayReachFloor(const Map* game, float inX, float inZ) {
    if (GameUpdateLoop::_singleton == nullptr) return false;   // GetTerrainPos: no canonical grid, no refine
    const Map* mapGrid = GameUpdateLoop::_singleton->m_game;
    if (!mapGrid) return false;
    if (game && game != mapGrid) return true;
    const MapCell* cells = mapGrid->cells.GetData();
    const int dim = mapGrid->size;
    if (!cells || dim <= 0) return false;
    int ix = (int)(inX + (float)(dim >> 1));
    int iy = (int)(inZ + (float)(dim >> 1));
    if (ix < 0)    ix = 0;
    if (ix >= dim) ix = dim - 1;
    if (iy < 0)    iy = 0;
    if (iy >= dim) iy = dim - 1;
    return cells[(int64_t)dim * iy + ix].floorCount > 0;
}
