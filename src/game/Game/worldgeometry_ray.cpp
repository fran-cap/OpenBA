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

// [TERRAINRAY -- NOT RETAIL, faithful perf work, exact] The height-only form of
// TerrainHeightUnder above (no normal out): the same float operations, minus the ones whose
// result is fixed by the vertical ray. tests/unit/terrain_ray_test.cpp compares the two bit
// for bit over a sweep (cell interiors, both diagonals, edges, grid lines, off the map).
//
// The proof. Let px, py be finite, |px + half| and |py + half| < 2^22 and |half| < 2^22
// (otherwise the generic body runs). Then bx, by and bx + 1, by + 1 are exact integers, the
// corner heights are finite multiples of 1/256, so every edge vector's x / y is exactly
// -1, +0 or 1 and every intermediate is finite. With D = (0, 0, -1):
//   * a product by +-0 is +-0 and a product by +-1 is exact, so dropping a `+ x * 0` term or
//     a `* 1` changes at most the SIGN of a zero intermediate;
//   * a zero's sign never reaches the result: it is lost in any sum with a nonzero term,
//     the culls only compare, det is exactly 1 in all four triangles (so t = 1.0f * tdot
//     = tdot), and the height is t * -1 + 100, which is 100 for either zero.
// So each u, v and t below equals the generic one up to the sign of a zero, and the height,
// which t written last decides (*outT is stored once the culls pass, even on a t miss), is
// bit-identical. Per triangle (V0, V1, V2), tvec = P - V0 = (tx, ty, tz):
//   u = pvec.y * ty + pvec.x * tx    pvec = D x e2 = (e2.y, -e2.x, 0)
//   v = -qvec.z                      qvec = tvec x e1
//   t = qvec.y * e2.y + qvec.x * e2.x + qvec.z * e2.z
static constexpr float kHeightOnlyRange = 4194304.0f;   // 2^22

static bool TerrainHeightOnly(const WorldGeometry* geo, const float* point, float* outHeight) {
    const int half = geo->_gridSize >> 1;
    if (half <= -(1 << 22) || half >= (1 << 22)) return false;
    const float px = point[0];
    const float py = point[1];
    const float fhalf = static_cast<float>(half);
    const float ox = px + fhalf;
    const float oy = py + fhalf;
    // false for a NaN as well.
    if (!(ox > -kHeightOnlyRange && ox < kHeightOnlyRange && oy > -kHeightOnlyRange && oy < kHeightOnlyRange))
        return false;
    const int x = static_cast<int>(ox);
    const int y = static_cast<int>(oy);

    const int hmW = geo->_fieldD0;
    const int64_t lW = hmW;
    const int16_t* hm = geo->_heights.data;
    const float hscale = f1720bc();
    const float uv = kRdata_14017214c;
    const int c0 = ClampCell(x, hmW);
    const int c1 = ClampCell(x + 1, hmW);
    const int r0 = ClampCell(y, hmW);
    const int r1 = ClampCell(y + 1, hmW);
    const float z00 = static_cast<float>(static_cast<int>(hm[r0 * lW + c0])) * hscale;   // V00 (x,   y)
    const float z01 = static_cast<float>(static_cast<int>(hm[r0 * lW + c1])) * hscale;   // V01 (x+1, y)
    const float z10 = static_cast<float>(static_cast<int>(hm[r1 * lW + c0])) * hscale;   // V10 (x,   y+1)
    const float z11 = static_cast<float>(static_cast<int>(hm[r1 * lW + c1])) * hscale;   // V11 (x+1, y+1)

    const float bx = static_cast<float>(x - half);
    const float by = static_cast<float>(y - half);
    const float originZ = RdataF(kRdata_140172208);
    const float dirZ = kRdata_140172280;
    const float eps = C_eps();
    float maxT = RdataF(kRdata_140172228);
    float tOut = 0.0f;

    // aa810's culls with det == 1, then its t test; *outT is written once the culls pass.
    auto tri = [&](float u, float v, float t) -> bool {
        if (eps > u) return false;
        if (u > 1.0f) return false;
        if (eps > v) return false;
        if (v + u > 1.0f) return false;
        tOut = t;
        if (t < 0.0f) return false;
        if (maxT < t) return false;
        return true;
    };

    const float tx = px - bx;        // tvec from V00
    const float ty = py - by;
    const float tz = originZ - z00;
    bool hit;
    if (((x + y) & 1) == 0) {
        // (V00, V01, V10): e1 = (1, 0, a), e2 = (0, 1, b); u = tx, v = ty.
        const float a = z01 - z00;
        const float b = z10 - z00;
        const float qy = tz - tx * a;
        hit = tri(tx, ty, qy - ty * b);
        if (hit) maxT = tOut;
        // (V01, V11, V10): tvec from V01, e1 = (0, 1, c), e2 = (-1, 1, d); u = ty + tx1,
        // v = -tx1.
        const float tx1 = px - (bx + uv);
        const float tz1 = originZ - z01;
        const float c = z11 - z01;
        const float d = z10 - z01;
        const float qx1 = ty * c - tz1;
        const float qy1 = -(tx1 * c);
        if (tri(ty + tx1, -tx1, (qy1 - qx1) + tx1 * d)) hit = true;
    } else {
        // (V00, V01, V11): e1 = (1, 0, a), e2 = (1, 1, e); u = -ty + tx, v = ty.
        const float a = z01 - z00;
        const float e = z11 - z00;
        const float qx = ty * a;
        const float qy = tz - tx * a;
        hit = tri(-ty + tx, ty, (qy + qx) - ty * e);
        if (hit) maxT = tOut;
        // (V00, V11, V10): e1 = (1, 1, f), e2 = (0, 1, b); u = tx, v = -(tx - ty).
        const float f = z11 - z00;
        const float b = z10 - z00;
        const float qz1 = tx - ty;
        const float qy1 = tz - tx * f;
        if (tri(tx, -qz1, qy1 + qz1 * b)) hit = true;
    }
    *outHeight = hit ? tOut * dirZ + originZ : 0.0f;
    return true;
}

int WorldGeometry::CellQuadRay(int x, int y, const float* dir, const float* origin,
                        const float* maxT, float* outHit, float* outT, float* outNormal) {
    return RayTestCell(this, x, y, dir, origin, maxT, outHit, outT, outNormal);
}

float WorldGeometry::HeightUnderPoint(const float* point, float* outNormal) {
    float height;
    if (outNormal == nullptr && TerrainHeightOnly(this, point, &height))
        return height;
    return TerrainHeightUnder(this, point, outNormal);
}

float WorldGeometry::HeightUnderPointReference(const float* point, float* outNormal) {
    return TerrainHeightUnder(this, point, outNormal);
}
