// src/game/Game/worldgeometry_ray.cpp
// The terrain ray cast: the height and surface normal of the heightmap under a point.
//
//   0x1400aa810  Pf_RayTriIntersect_aa810         ray/triangle (Moller-Trumbore) intersect
//   0x1400aaa20  WorldGeometry::CellQuadRay        one cell's quad as two triangles
//   0x1400b0f90  WorldGeometry::HeightUnderPoint   a vertical ray down through the cell
//
// Moved out of Action/action_pf_geom.cpp (aa810) and Action/action_pf_road.cpp (aa20,
// b0f90) unchanged, so that Map::GetTerrainPos (Game/mapquery.cpp) and the host unit
// tests can link the bodies without the road-mesh and pathfinding files.

#include "Action/exe_bodies.h"      // Pf_RayTriIntersect_aa810
#include "Game/WorldGeometry.h"
#include "Game/exe_constants.h"     // kRdata_*
#include "math/Math.h"
#include <cstdint>
#include <cstring>

namespace {

// Float constants (read from exe .rdata in the original; inlined here as bit-exact
// values -- verified via read_memory):
//   DAT_14017226c = 0xb8d1b717 = -1.0e-4f   (barycentric epsilon)
//   DAT_14017214c = 0x3f800000 =  1.0f      (g_fUVScale)
static inline float C_eps()    { uint32_t b = 0xb8d1b717u; float f; __builtin_memcpy(&f, &b, 4); return f; }
static inline float C_one()    { return 1.0f; }

// The .rdata float constants that are stored as bit patterns.
inline float RdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }

// DAT_1401720bc = heightmap int16 -> world-height scale = 0x3b800000 = 1/256.
static inline float f1720bc()  { return RdataF(kRdata_1401720bc); }   // 0x1401720bc

static inline int ClampCell(int v, int w) {
    if (v < 0) v = 0;
    if (v >= w) v = w - 1;
    return v;
}

} // namespace

// [COPY-PINNED begin] tests/unit/copy_drift_test.cpp pins the text from here to the end
// marker: tests/unit/still_terrain_test.cpp (World::SampleHeight and its two helpers) holds a
// literal copy over a plain heightmap, and drives these bodies through Map::GetTerrainPos too.
// ============================================================
// 0x1400aa810 — ray/triangle (Moller-Trumbore) barycentric intersect
// ============================================================
// Args: (unused this), rayOrigin(float*P), rayDir(Vector3*D), maxT(float*),
//   V0,V1,V2 (Vector3*), outT(float*), outNormal(Vector3* or null).
// edge1=V1-V0, edge2=V2-V0, pvec=D x edge2, det=edge1.pvec; cull det<=0;
// u=tvec.pvec (tvec=P-V0); cull u<-eps or u>det; qvec=tvec x edge1; v=D.qvec;
// cull v<-eps or u+v>det; t=(1/det)*edge2.qvec; cull t<0 or t>*maxT; *outT=t;
// if outNormal: *outNormal = normalize(edge1 x edge2). Returns 1 on hit, 0 else.
// Transcribed from disasm 0x1400aa810 (decompiler retbuf-aliasing is misleading).
int Pf_RayTriIntersect_aa810(void* /*self*/, const float* P, const float* D,
                             const float* maxT, const float* V0, const float* V1,
                             const float* V2, float* outT, float* outNormal) {
    const Math::Vector3 Dv(D[0], D[1], D[2]);   // the ray direction (a copy of the three floats)
    // edge1 = V1 - V0
    Math::Vector3 edge1(V1[0] - V0[0], V1[1] - V0[1], V1[2] - V0[2]);
    // edge2 = V2 - V0
    Math::Vector3 edge2(V2[0] - V0[0], V2[1] - V0[1], V2[2] - V0[2]);
    // pvec = D.Cross(edge2)
    Math::Vector3 pvec = Dv.Cross(edge2);
    // det = edge1.Dot(pvec)
    float det = edge1.Dot(pvec).v;
    // tvec = P - V0
    Math::Vector3 tvec(P[0] - V0[0], P[1] - V0[1], P[2] - V0[2]);
    // qvec = tvec.Cross(edge1)
    Math::Vector3 qvec = tvec.Cross(edge1);
    // u = tvec.Dot(pvec)
    float u = tvec.Dot(pvec).v;
    // v = D.Dot(qvec)
    float v = Dv.Dot(qvec).v;

    const float eps = C_eps();
    if (det <= 0.0f) return 0;
    if (eps > u) return 0;
    if (u > det) return 0;
    if (eps > v) return 0;
    if (v + u > det) return 0;

    // t = (1.0/det) * edge2.Dot(qvec)
    float tdot = edge2.Dot(qvec).v;
    float t = (C_one() / det) * tdot;
    *outT = t;
    if (t < 0.0f) return 0;
    if (*maxT < t) return 0;
    if (outNormal != nullptr) {
        // outNormal = Normalize(edge1.Cross(edge2))
        Math::Vector3 ncross = edge1.Cross(edge2);
        Math::Vector3 nnorm  = ncross.Normalize();
        outNormal[0] = nnorm.x;
        outNormal[1] = nnorm.y;
        outNormal[2] = nnorm.z;
    }
    return 1;
}

// ============================================================
// FUN_1400aaa20 — terrain-cell quad (2-tri) ray test.
// ============================================================
// Reads the heightmap (_fieldD0 width, _arrayD8) for the 4 corners of cell (x,y), builds
// two triangles split by parity of (x+y), and ray-tests each via aa810. Writes the world
// hit point (origin + t*dir) into outHit and t into outT. Returns whether any tri was hit.
static int RayTestCell(WorldGeometry* geo, int x, int y, const float* dir, const float* origin,
                const float* maxT, float* outHit, float* outT, float* outNormal) {
    int half = geo->_gridSize >> 1;
    int hmW = geo->_fieldD0;
    int64_t lW = hmW;
    const int16_t* hm = geo->_heights.data;
    float hscale = f1720bc();
    // Cell-corner world spacing. The original (disasm 0x1400aaaf7) adds *0x14017214c = 1.0
    // to step to the next corner — NOT *0x1401720b8=0.001, which collapses the quad
    // to a near-degenerate sliver so aa810's det≈0 and every ray misses.
    float uv = kRdata_14017214c;   // 0x14017214c

    // Heightmap sample at (col,row), clamped to the map — matches the original's
    // per-corner ClampCell of x/x+1 and y/y+1. height = int16 sample * hscale.
    auto H = [&](int col, int row) -> float {
        int cc = ClampCell(col, hmW);
        int cr = ClampCell(row, hmW);
        return static_cast<float>(static_cast<int>(hm[cr * lW + cc])) * hscale;
    };

    float bx = static_cast<float>(x - half);   // base world X (cell corner)
    float by = static_cast<float>(y - half);   // base world Y

    // The 4 quad corners as CONTIGUOUS Vector3 {x,y,z}: aa810 reads each as a Vector3.
    // z = corner height.
    float V00[3] = { bx,      by,      H(x,     y)     };  // (x,   y)
    float V01[3] = { bx + uv, by,      H(x + 1, y)     };  // (x+1, y)
    float V10[3] = { bx,      by + uv, H(x,     y + 1) };  // (x,   y+1)
    float V11[3] = { bx + uv, by + uv, H(x + 1, y + 1) };  // (x+1, y+1)

    float maxTbuf[1] = { *maxT };
    float tOut[1] = { 0.0f };

    // Split the quad into two triangles by (x+y) parity, ray-test each (aa810). aa810's
    // first argument (RCX) is never read.
    bool hit;
    if (((x + y) & 1) == 0) {
        uint64_t r = Pf_RayTriIntersect_aa810(nullptr, origin, dir, maxTbuf, V00, V01, V10, tOut, outNormal);
        hit = (static_cast<char>(r) != 0);
        if (hit) maxTbuf[0] = tOut[0];
        uint64_t r2 = Pf_RayTriIntersect_aa810(nullptr, origin, dir, maxTbuf, V01, V11, V10, tOut, outNormal);
        if (static_cast<char>(r2) != 0) hit = true;
    } else {
        uint64_t r = Pf_RayTriIntersect_aa810(nullptr, origin, dir, maxTbuf, V00, V01, V11, tOut, outNormal);
        hit = (static_cast<char>(r) != 0);
        if (hit) maxTbuf[0] = tOut[0];
        uint64_t r2 = Pf_RayTriIntersect_aa810(nullptr, origin, dir, maxTbuf, V00, V11, V10, tOut, outNormal);
        if (static_cast<char>(r2) != 0) hit = true;
    }

    // outHit = origin + t*dir.
    float t = tOut[0];
    outHit[0] = t * dir[0] + origin[0];
    outHit[1] = t * dir[1] + origin[1];
    outHit[2] = t * dir[2] + origin[2];
    *outT = t;
    return hit ? 1 : 0;
}

// ============================================================
// FUN_1400b0f90 — the terrain height under a point: a downward ray through aa20.
// ============================================================
// Builds a downward ray (disassembly-derived, 0x1400b0f90):
//   origin = (point[0], point[1], DAT_140172208)   [RSP+0x50..0x58]
//   dir    = (0, 0, DAT_140172280)                 [RSP+0x60..0x68]
//   maxT   = DAT_140172228 (single float)
// cell = ((point + half) truncated).
//
// ⚠ RETURNS A FLOAT — the terrain hit HEIGHT. Ghidra types this `void`; the disassembly:
//     CALL 0x1400aaa20 ; TEST AL,AL ; JZ miss
//     MOVSS XMM0,[RSP+0x78] ; RET      <- hit : return outHit[2]  (Z of the ray hit)
//   miss: XORPS XMM0,XMM0 ; RET        <- miss: return 0.0f
// (PathTool::Draw's worker-route segments read it as each segment's Z; found live
// 2026-08-20.)
static float TerrainHeightUnder(WorldGeometry* geo, const float* point, float* outNormal) {

    float origin[3];   // [RSP+0x50..0x58]
    origin[0] = point[0];
    origin[1] = point[1];
    origin[2] = RdataF(kRdata_140172208);  // DAT_140172208

    float dir[3] = {0.0f, 0.0f, 0.0f};     // [RSP+0x60..0x68]
    dir[2] = kRdata_140172280;             // DAT_140172280

    float maxT = RdataF(kRdata_140172228);  // DAT_140172228 (single float)

    float half = static_cast<float>(geo->_gridSize >> 1);
    // outHit spans [R11-0x18 .. R11-0x10] — three floats, and the third IS the return value.
    // The t out is a separate scratch (retail's local_res10, a float[6]); size both
    // generously so a callee that writes more than we expect cannot clobber a neighbour.
    float outHit[4]  = {0, 0, 0, 0};       // outHit[2] is the returned height
    float scratch[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    int hit = RayTestCell(geo,
                          static_cast<int>(origin[0] + half),
                          static_cast<int>(origin[1] + half),
                          dir, origin, &maxT, outHit, scratch, outNormal);   // 0x1400aaa20
    return hit ? outHit[2] : 0.0f;
}

// [COPY-PINNED end]

int WorldGeometry::CellQuadRay(int x, int y, const float* dir, const float* origin,
                        const float* maxT, float* outHit, float* outT, float* outNormal) {
    return RayTestCell(this, x, y, dir, origin, maxT, outHit, outT, outNormal);
}

float WorldGeometry::HeightUnderPoint(const float* point, float* outNormal) {
    return TerrainHeightUnder(this, point, outNormal);
}
