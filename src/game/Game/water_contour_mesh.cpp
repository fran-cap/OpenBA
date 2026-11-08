// src/game/Game/water_contour_mesh.cpp
// ============================================================================
// CONTOUR WATER MESH — faithful RE port of Banished's TradeWork_impl_b260.
// ============================================================================
//
// Replaces the flat substitute water with the real contour mesh that follows
// the world's water bodies (shore band / rivers flow, lake/pond shore rings).
//
// Source functions (Application-x64.exe, base 0x140000000):
//   * TradeWork_impl_b260 @ 0x1400ab260 — orchestrator. Builds the water mesh
//       into geom VB +0x160 / IB +0x180, with per-body draw-range tables at
//       +0x1c0 (range A, count +0x1c8) and +0x1d0 (range B, count +0x1d8).
//       Called as b260(param_1 = WorldGeometry geom, param_2 = hf TradeData).
//   * TradeWork_impl_fb00 @ 0x1400cfb00 — per-water-body contour vertex/UV
//       generator. Reads the body's centerline polyline and lays a flow-aligned
//       quad strip (V = arc length along the flow, U = 0..1 across the channel).
//   * helper f3e0 @ 0x14001f3e0 — vertex weld/dedup -> index (phase-2 weld).
//   * helper sub_74a0 @ 0x1400074a0 — 16-byte vertex append (the growable-vector
//       idiom, equivalent to engine_funcs' fn_Array_Resize_Stride16B).
//
// DATA FLOW (verified, this session):
//   b260's param_2 == the worldgen heightfield scratch `hf` (the same struct
//   wg_TradeCore_c4b0 builds in worldgen.cpp). Phase-1 water bodies live in the
//   patch list at hf+0x18 (record {centerlinePtr@+0, ptCount@+8}, stride 0x10;
//   count at hf+0x20). Each centerline point is stride 0x1c: {x@+0, y@+4,
//   halfWidth@+0x14}. Phase-2 features live at hf+0x48 (stride 0xc {x,y,radius},
//   count hf+0x50). Confirmed at runtime (STEP-1 de-risk): a fresh Easy map has
//   1 phase-1 body (the 513-point coastline ring, halfWidth 4.0) + 2 phase-2
//   features.
//
// FAITHFULNESS NOTES:
//   * Phase 1 packing, phase 2 (heightmap shore ring), the range tables, the
//     vertex/index accumulators, all .rdata constants, and the final
//     VertexBuffer/IndexBuffer::Create calls are byte/offset-faithful to the
//     b260 disassembly.
//   * fb00 STAGE A is now a FAITHFUL geometric port (fb00_BuildBody_Faithful),
//     replacing the old per-node offset approximation (kept as fb00_BuildBody
//     for A/B via env BANISHED_WATER_OLD_STAGEA, and as an auto-fallback for
//     degenerate <3-point bodies). The faithful path reproduces retail's actual
//     Stage-A geometry, verified against the 0x1400cfb00 disassembly:
//       - edge records: endpoints shifted (+0.5 x, -0.5 y) = DAT_140172114, unit
//         dir, unit perp = (-dir.y, dir.x), length;
//       - straight-run subdivision = max(1, int(len * 4.0))  (DAT_140172194);
//       - convex corner fans = max(1, int(acos(dot(d_i,d_{i+1})) * (57.29578/90)
//         * 20)) segments (DAT_1401721ec/204/1c4), interpolating this-perp ->
//         next-perp around the node on the convex side (sign -1/+1, DAT_140172280);
//       - validity prune (sub_ba30): a bank sample is kept only if it is >= radius
//         from EVERY centerline edge (point-to-segment), radius = cl[0].halfWidth
//         + 2.0 (DAT_140172174);
//       - bank matching (sub_bdf0): nearest bank point to each raw centerline node
//         (the tangent param is UNUSED in retail -> pure nearest-point);
//       - matched-range arc-length resample (sub_b360/b750) to a common count =
//         max(1, int(max(arcL,arcR))) with the retail gates (skip if both bank
//         arcs < 2.0; skip if avg edge < 0.001, DAT_1401720b8);
//       - Stage C 4-column strip (U 0->1 by 0.25, winding (V0,V2,V1),(V0,V3,V2));
//       - streamwise V = cumulativeMidlineArc / 32.0 - 16.0 (DAT_1401721d0 / -16).
//     FAITHFUL SIMPLIFICATIONS (flagged): the spatial-hash grids (sub_40a0 cell
//     init, helper_31d0 edge insert, sub_3450/3680 cell buckets, sub_38d0/37a0
//     cell iterators) are pure acceleration structures; we use brute-force edge
//     scans / nearest searches that return the IDENTICAL geometric result. The
//     vestigial 4x driver inner loop and the unused interp-tangent are omitted.
//     The perpendicularity gate IS now restored (was previously omitted). Re-derived
//     from the fb00 disassembly (@0x1400d1068/0x1400d1101): the decompiler's `local_2b0`
//     is NOT stale — it is [RBP+0x308] = Normalize(interpolated centerline tangent),
//     built at the top of the resample loop. The gate is
//       Abs(Dot(Normalize(matchedLeft - matchedRight), tangent)) < 0.02 (DAT_1401720d0)
//     and rejects skewed cross-sections.
//
// Constants (.rdata, verified via read_memory):
//   DAT_1401721e4 = 48.0     pos pack scale (pos.xy * 48 -> int16)
//   DAT_140172250 = 1024.0   uv  pack scale (uv     * 1024 -> int16)
//   DAT_1401722a4 = -192.0   phase-1 z base (z = -192 - bodyIdx)
//   _DAT_140172224 = 191.0   phase-2 z base (z = featureIdx - 191)
//   DAT_140172174 = 2.0      radius margin (radius = halfWidth + 2)
//   DAT_140172100 = 0.25     cross-strip U step
//   DAT_1401720bc = 1/256    heightmap int16 -> world height
//   DAT_14017227c = -0.75    phase-2 underwater test (corner.z <= -0.75)
//   DAT_14017222c = 255.0    phase-2 weight -> AO byte
//   DAT_14017228c = -16.0    streamwise V offset start
//   g_fUVScale    = 1.0
//
// Gated behind BANISHED_WATER_CONTOUR (default OFF). When OFF the flat-water /
// no-detail default is unchanged.

// ENDGAME B.3.5 Video/Graphics caller batch: Video::{Vertex,Index}Buffer are
// real classes. Ahead of "resolve.h" — resolve.h pulls <windows.h>, whose
// <wingdi.h> declares a global `Rectangle` FUNCTION that hides
// `Volume::Rectangle`'s class name inside Volume.h's own static_asserts.
#include "video/Buffers.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/WorldGeometry.h"
#include "Game/WorldGeometryElements.h"
#include "Game/TerrainHeightfield.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>

namespace {

// ============================================================
// .rdata constants (embedded; the standalone PE has no exe .rdata)
// ============================================================
constexpr float kPosScale   = 48.0f;     // DAT_1401721e4
constexpr float kUVScale    = 1024.0f;   // DAT_140172250
constexpr float kZBaseP1    = -192.0f;   // DAT_1401722a4  (phase-1 z = -192 - bodyIdx)
constexpr float kZBaseP2    = 191.0f;    // _DAT_140172224 (phase-2 z = featureIdx - 191)
constexpr float kRadMargin  = 2.0f;      // DAT_140172174
constexpr float kUStep      = 0.25f;     // DAT_140172100
constexpr float kHeightStep = 1.0f / 256.0f; // DAT_1401720bc
constexpr float kUnderwater = -0.75f;    // DAT_14017227c
constexpr float kAOScale    = 255.0f;    // DAT_14017222c
constexpr float kVOffset    = -16.0f;    // DAT_14017228c (streamwise V start)
constexpr float kVDenom     = 32.0f;     // DAT_1401721d0 (streamwise-V denominator:
                                         //   fb00 emits V = midlineArc/32 + Voffset, Voffset
                                         //   carried across chunks -> cumulativeMidlineArc/32 - 16)
constexpr float kMinArcGate = 0.001f;    // DAT_1401720b8 (avg-edge degenerate gate)

// ============================================================
// ENDGAME B.3.5 Video/Graphics caller batch — the two buffer doors are gone.
// `?Create@VertexBuffer@Video@@QEAA_NH_NPEAX@Z` (exe dispatch slot 0x140152668)
// and `?Create@IndexBuffer@Video@@QEAA_NH_NPEAX@Z` (slot 0x140152660) are real
// members of block D's classes, so the two caches, their typedefs, the
// `loadSlot` lambda, the latch and `ResolveRuntimePtrs` went with their last
// callers.
// ============================================================

// ============================================================
// Growable-vector idiom (Collection::Array's {data, count, cap} 16-byte header,
// with the "+1 append over-allocs 3x" growth, matching the b260/fb00 inlined
// resizes). The headers are RawArray<T>: WorldGeometry's own arrays and the
// per-body scratch.
// ============================================================
void* MemAllocA(uint64_t bytes, int align) {
    return (System::MemoryAllocator::_singleton) ? System::MemoryAllocator::MemAlloc(bytes, align) : nullptr;
}

// Append one element; mirrors the inlined resize (count+1).
template <class T>
void VecPush(RawArray<T>* v, const T& elem, int align) {
    int n = v->size + 1;
    if (v->capacity < n) {
        int newCap = (v->size != 0) ? n * 3 : n;   // 3x over-alloc on non-empty append
        T* nb = static_cast<T*>(MemAllocA((uint64_t)newCap * sizeof(T), align));
        if (!nb) return;
        if (v->data && v->size > 0)
            std::memcpy(nb, v->data, (size_t)v->size * sizeof(T));
        if (v->data) System::MemoryAllocator::MemFree(v->data);
        v->data = nb;
        v->capacity = newCap;
    }
    v->data[v->size] = elem;
    v->size = n;
}

// ============================================================
// Vertex weld (f3e0 equivalent): full 16-byte key dedup -> monotonic index.
// The retail f3e0 uses an RB-tree keyed on the packed vertex; a hash with exact
// key compare is behaviourally identical (same dedup set, same index order).
// ============================================================
template <class T>
struct WeldSet {
    RawArray<T> verts;   // OutVtx/16-byte GPU verts
    int32_t* buckets;    // hash head index (-1 empty)
    int32_t* chain;      // per-vertex next-in-bucket
    int32_t  mask;
    int32_t  chainCap;
    int32_t  stride;     // key/element size in bytes
};

uint32_t WeldHash(const void* key, int stride) {   // LEFT: void* is the raw byte record hashed (FNV-1a over `stride` bytes): the one body under WeldAdd<T>'s element types
    const uint8_t* k = (const uint8_t*)key;   // CAST-OK: byte_io
    uint32_t h = 2166136261u;            // FNV-1a
    for (int i = 0; i < stride; ++i) { h ^= k[i]; h *= 16777619u; }
    return h;
}

template <class T>
void WeldInit(WeldSet<T>* s, int estimate) {
    s->verts.data = nullptr; s->verts.size = 0; s->verts.capacity = 0;
    s->stride = (int)sizeof(T);
    int cap = 16;
    while (cap < estimate * 2) cap <<= 1;
    s->mask = cap - 1;
    s->buckets = (int32_t*)std::malloc((size_t)cap * sizeof(int32_t));   // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
    for (int i = 0; i < cap; ++i) s->buckets[i] = -1;
    s->chainCap = estimate > 0 ? estimate : 16;
    s->chain = (int32_t*)std::malloc((size_t)s->chainCap * sizeof(int32_t));   // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
}

template <class T>
void WeldFree(WeldSet<T>* s) {
    if (s->buckets) std::free(s->buckets);
    if (s->chain) std::free(s->chain);
    if (s->verts.data) System::MemoryAllocator::MemFree(s->verts.data);
    s->buckets = nullptr; s->chain = nullptr; s->verts.data = nullptr;
}

// Returns the index of `key` in the weld set (existing on a hit, freshly
// appended on a miss). Mirrors f3e0's "return existing index or insert".
template <class T>
uint16_t WeldAdd(WeldSet<T>* s, const T* key) {
    uint32_t b = WeldHash(key, s->stride) & s->mask;
    for (int32_t i = s->buckets[b]; i >= 0; i = s->chain[i]) {
        const T* v = &s->verts.data[i];
        if (std::memcmp(v, key, s->stride) == 0) return (uint16_t)i;
    }
    int32_t idx = s->verts.size;
    VecPush(&s->verts, *key, 2);
    if (idx >= s->chainCap) {
        int32_t nc = s->chainCap * 2;
        s->chain = (int32_t*)std::realloc(s->chain, (size_t)nc * sizeof(int32_t));   // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
        s->chainCap = nc;
    }
    s->chain[idx] = s->buckets[b];
    s->buckets[b] = idx;
    return (uint16_t)idx;
}

// ============================================================
// fb00 output vertex {posX, posY, uvU, uvV} (4 floats = 16 bytes)
// ============================================================
struct OutVtx { float posX, posY, uvU, uvV; };
struct V2 { float x, y; };
inline V2 Lerp(V2 a, V2 b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }

// ============================================================
// fb00 — per-body contour vertex/UV generator (Stage A approximated, B/C faithful)
//
//   hf = the heightfield, bodyIdx = its feature polyline, out = vtxOut(OutVtx)+idxOut(u16).
// ============================================================
void fb00_BuildBody(const TerrainHeightfield* hf, int bodyIdx, RawArray<OutVtx>* vtxOut, RawArray<uint16_t>* idxOut) {
    // record = hf->paths, stride 0x10: {centerline, ptCount}
    const RawArray<TerrainPathPoint>* recBase = hf->paths._data;
    if (!recBase) return;
    const RawArray<TerrainPathPoint>& rec = recBase[bodyIdx];
    const TerrainPathPoint* cl = rec.data;   // centerline array
    int ptCount = rec.size;
    if (!cl || ptCount < 2) return;

    // Centerline point accessor (stride 0x1c): x@+0, y@+4, halfWidth@+0x14.
    auto CP = [&](int i) -> V2 {
        return { cl[i].x, cl[i].y };
    };
    // radius = centerline[0].halfWidth + 2.0   (fb00: *(*plVar38+0x14) + DAT_140172174)
    float radius = (cl[0].outerRadius + kRadMargin);
    float channelWidth = 2.0f * radius;
    if (channelWidth < 1.0f) channelWidth = 1.0f;

    // --- Stage A (approx): banks = centerline +/- radius along per-node normal.
    // --- Stage B (faithful): arc-length V along the centerline (shared across
    //     the width band so the texture flows straight along the spine).
    V2*    bankL = (V2*)std::malloc((size_t)ptCount * sizeof(V2));        // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
    V2*    bankR = (V2*)std::malloc((size_t)ptCount * sizeof(V2));        // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
    float* vCoord = (float*)std::malloc((size_t)ptCount * sizeof(float)); // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
    if (!bankL || !bankR || !vCoord) {
        if (bankL) std::free(bankL);
        if (bankR) std::free(bankR);
        if (vCoord) std::free(vCoord);
        return;
    }

    float arc = 0.0f;
    V2 prev = CP(0);
    for (int i = 0; i < ptCount; ++i) {
        V2 c = CP(i);
        // tangent via central difference (clamped at the ends)
        V2 a = CP(i > 0 ? i - 1 : i);
        V2 b = CP(i < ptCount - 1 ? i + 1 : i);
        float tx = b.x - a.x, ty = b.y - a.y;
        float tl = std::sqrt(tx * tx + ty * ty);
        if (tl < 1e-6f) { tx = 1.0f; ty = 0.0f; tl = 1.0f; }
        tx /= tl; ty /= tl;
        // normal = perpendicular (-ty, tx)
        float nx = -ty, ny = tx;
        bankL[i] = { c.x + nx * radius, c.y + ny * radius };
        bankR[i] = { c.x - nx * radius, c.y - ny * radius };
        // arc length along the centerline
        if (i > 0) {
            float dx = c.x - prev.x, dy = c.y - prev.y;
            arc += std::sqrt(dx * dx + dy * dy);
        }
        prev = c;
        // sub_b360 streamwise V (approx): isometric to U — one V-tile per channel
        // width of flow, offset by DAT_14017228c (-16). int16 wrap = seamless tile.
        vCoord[i] = (arc / channelWidth) + kVOffset;
    }

    // --- Stage C (faithful): 4-column quad strip per centerline segment.
    //     U marches 0 -> 1 in 0.25 steps; position bilinear across the banks;
    //     V is the (shared) centerline arc-length coordinate.
    int estimate = ptCount * 4 * 2;
    WeldSet<OutVtx> weld; WeldInit(&weld, estimate);

    auto emitIdx = [&](uint16_t v) { VecPush(idxOut, v, 2); };

    for (int i = 0; i + 1 < ptCount; ++i) {
        V2 p0 = bankL[i], p1 = bankL[i + 1];
        float v0 = vCoord[i], v1 = vCoord[i + 1];
        float uLo = 0.0f, uHi = kUStep;
        for (int col = 0; col < 4; ++col) {
            float t = uHi;
            V2 pLo = Lerp(bankL[i],     bankR[i],     t);
            V2 pHi = Lerp(bankL[i + 1], bankR[i + 1], t);
            float vLo = v0;   // vA == vB (shared centerline V) -> lerp == v0/v1
            float vHi = v1;
            OutVtx V0 = { p0.x,  p0.y,  uLo, v0 };
            OutVtx V1 = { pLo.x, pLo.y, uHi, vLo };
            OutVtx V2v= { pHi.x, pHi.y, uHi, vHi };
            OutVtx V3 = { p1.x,  p1.y,  uLo, v1 };
            uint16_t i0 = WeldAdd(&weld, &V0);
            uint16_t i1 = WeldAdd(&weld, &V1);
            uint16_t i2 = WeldAdd(&weld, &V2v);
            uint16_t i3 = WeldAdd(&weld, &V3);
            // winding matches fb00 Stage C: (V0,V2,V1),(V0,V3,V2)
            emitIdx(i0); emitIdx(i2); emitIdx(i1);
            emitIdx(i0); emitIdx(i3); emitIdx(i2);
            uLo = uHi; uHi += kUStep; p0 = pLo; p1 = pHi; v0 = vLo; v1 = vHi;
        }
    }

    // Move the welded vertex set into vtxOut (transfer ownership of the buffer).
    *vtxOut = weld.verts;
    weld.verts.data = nullptr;   // don't free below
    WeldFree(&weld);

    std::free(bankL); std::free(bankR); std::free(vCoord);
}

// ============================================================
// FAITHFUL Stage A — dense rounded offset-bank contours (port of fb00 0x1400cfb00).
//   See the file header for the full per-stage verification map. Stages B/C reuse
//   the same arc-length-V + 4-column strip as fb00_BuildBody, fed from the faithful
//   dense banks via retail's nearest-point matching + matched-range resample.
// ============================================================
struct Edge { V2 p0, p1, dir, perp; float len; };
struct V2List { V2* d; int n; int cap; };

inline float V2Len(V2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
inline V2    V2Sub(V2 a, V2 b) { return { a.x - b.x, a.y - b.y }; }
inline V2    V2Norm(V2 a) { float l = V2Len(a); return (l < 1e-8f) ? V2{ 0.0f, 0.0f } : V2{ a.x / l, a.y / l }; }

void V2Push(V2List* L, V2 v) {
    if (L->n >= L->cap) {
        int nc = L->cap ? L->cap * 2 : 128;
        L->d = (V2*)std::realloc(L->d, (size_t)nc * sizeof(V2));   // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
        L->cap = nc;
    }
    L->d[L->n++] = v;
}

// Distance from point p to segment [a,b]  (sub_ba30 endpoint + perpendicular test).
float DistToSeg(V2 p, V2 a, V2 b) {
    V2 ab = V2Sub(b, a);
    float L2 = ab.x * ab.x + ab.y * ab.y;
    if (L2 < 1e-12f) return V2Len(V2Sub(p, a));
    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / L2;
    if (t <= 0.0f) return V2Len(V2Sub(p, a));
    if (t >= 1.0f) return V2Len(V2Sub(p, b));
    V2 f = { a.x + ab.x * t, a.y + ab.y * t };
    return V2Len(V2Sub(p, f));
}

// sub_ba30: keep a bank sample only if it is >= radius from EVERY centerline edge.
// (Brute-force over all edges == the spatial-hash result; far edges can't be closer.)
bool BankValid(const Edge* edges, int ne, V2 p, float radius) {
    for (int i = 0; i < ne; ++i)
        if (DistToSeg(p, edges[i].p0, edges[i].p1) < radius) return false;
    return true;
}

// sub_b360/b750 position resample: bank polyline range [s,e] -> `count` points,
// equally spaced by arc length (stepLen = totalArc / count).
void ResampleRange(const V2* bank, int s, int e, int count, V2* out) {
    if (count <= 0) return;
    float total = 0.0f;
    for (int k = s; k < e; ++k) total += V2Len(V2Sub(bank[k + 1], bank[k]));
    if (total <= 1e-6f) { for (int j = 0; j < count; ++j) out[j] = bank[s]; return; }
    float step = total / (float)count, target = 0.0f, arc = 0.0f;
    int seg = s;
    for (int j = 0; j < count; ++j) {
        float sl = V2Len(V2Sub(bank[seg + 1], bank[seg]));
        while (seg < e - 1 && arc + sl <= target) { arc += sl; ++seg; sl = V2Len(V2Sub(bank[seg + 1], bank[seg])); }
        float f = (sl > 1e-8f) ? (target - arc) / sl : 0.0f;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        out[j] = Lerp(bank[seg], bank[seg + 1], f);
        target += step;
    }
}

void fb00_BuildBody_Faithful(const TerrainHeightfield* hf, int bodyIdx, RawArray<OutVtx>* vtxOut, RawArray<uint16_t>* idxOut) {
    const RawArray<TerrainPathPoint>* recBase = hf->paths._data;
    if (!recBase) return;
    const RawArray<TerrainPathPoint>& rec = recBase[bodyIdx];
    const TerrainPathPoint* cl = rec.data;
    int ptCount = rec.size;
    if (!cl || ptCount < 3) return;   // need >=3 pts for an edge pair (fan); <3 -> caller falls back

    auto CLx = [&](int i) { return cl[i].x; };
    auto CLy = [&](int i) { return cl[i].y; };
    // radius = centerline[0].halfWidth(+0x14) + 2.0   (fb00: cl[0].halfWidth + DAT_140172174)
    float radius = (cl[0].outerRadius + kRadMargin);

    // --- Build edge records (shifted endpoints +0.5x / -0.5y, DAT_140172114=0.5). ---
    int ne = ptCount - 1;
    Edge* edges = (Edge*)std::malloc((size_t)ne * sizeof(Edge));   // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
    if (!edges) return;
    for (int i = 0; i < ne; ++i) {
        V2 p0 = { CLx(i) + 0.5f,     CLy(i) - 0.5f };
        V2 p1 = { CLx(i + 1) + 0.5f, CLy(i + 1) - 0.5f };
        V2 d = V2Sub(p1, p0);
        float l = V2Len(d);
        V2 dir = (l > 1e-8f) ? V2{ d.x / l, d.y / l } : V2{ 1.0f, 0.0f };
        edges[i] = Edge{ p0, p1, dir, V2{ -dir.y, dir.x }, l };
    }

    // --- Stage A: dense left/right banks (straight runs + convex corner fans). ---
    // fb00 main loop runs over edges [0, ptCount-3]; the fan reads edge[idx+1].
    V2List leftBank = { nullptr, 0, 0 }, rightBank = { nullptr, 0, 0 };
    const float kStraight = 4.0f;   // DAT_140172194
    const float kFanPer90 = 20.0f;  // DAT_1401721c4
    const float kRadDeg   = 57.29578f; // DAT_1401721ec (180/PI)
    const float k90       = 90.0f;  // DAT_140172204
    for (int idx = 0; idx <= ne - 2; ++idx) {
        const Edge& e = edges[idx];
        // Straight-run subdivision: max(1, int(len*4)) samples, marching p0 -> p1.
        int sub = (int)(e.len * kStraight);
        if (sub < 1) sub = 1;
        for (int j = 0; j <= sub; ++j) {
            float f = (float)j / (float)sub;
            V2 spine = { e.p0.x + (e.p1.x - e.p0.x) * f, e.p0.y + (e.p1.y - e.p0.y) * f };
            V2 Lp = { spine.x - e.perp.x * radius, spine.y - e.perp.y * radius };  // LEFT  = spine - perp*r
            V2 Rp = { spine.x + e.perp.x * radius, spine.y + e.perp.y * radius };  // RIGHT = spine + perp*r
            if (BankValid(edges, ne, Lp, radius)) V2Push(&leftBank, Lp);
            if (BankValid(edges, ne, Rp, radius)) V2Push(&rightBank, Rp);
        }
        // Convex corner fan at node e.p1, rotating this-perp -> next-perp.
        const Edge& nx = edges[idx + 1];
        float cross = e.dir.x * nx.dir.y - e.dir.y * nx.dir.x;  // cross(thisDir, nextDir)
        bool  side  = cross > 0.0f;                             // cVar17
        float sign  = side ? -1.0f : 1.0f;                      // DAT_140172280 = -1.0
        float dot   = e.dir.x * nx.dir.x + e.dir.y * nx.dir.y;
        if (dot < -1.0f) dot = -1.0f;
        if (dot > 1.0f) dot = 1.0f;
        float ang = std::acos(dot);
        int fan = (int)((ang * kRadDeg / k90) * kFanPer90);
        if (fan < 1) fan = 1;
        for (int j = 0; j < fan; ++j) {
            float t = (float)j / (float)fan;
            V2 ip = V2Norm(V2{ (1.0f - t) * e.perp.x + t * nx.perp.x,
                               (1.0f - t) * e.perp.y + t * nx.perp.y });
            V2 pt = { e.p1.x + sign * radius * ip.x, e.p1.y + sign * radius * ip.y };
            if (BankValid(edges, ne, pt, radius)) {
                if (side) V2Push(&leftBank, pt); else V2Push(&rightBank, pt);
            }
        }
    }

    int nL = leftBank.n, nR = rightBank.n;
    if (nL < 2 || nR < 2) {
        std::free(edges); std::free(leftBank.d); std::free(rightBank.d);
        return;
    }

    // --- Matching (sub_bdf0): nearest bank point to each RAW centerline node. ---
    int* lm = (int*)std::malloc((size_t)ptCount * sizeof(int));   // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
    int* rm = (int*)std::malloc((size_t)ptCount * sizeof(int));   // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
    if (!lm || !rm) {
        std::free(edges); std::free(leftBank.d); std::free(rightBank.d);
        std::free(lm); std::free(rm);
        return;
    }
    for (int i = 0; i < ptCount; ++i) {
        V2 c = { CLx(i), CLy(i) };
        int bi = 0; float bd = 1e30f;
        for (int k = 0; k < nL; ++k) { float dd = V2Len(V2Sub(leftBank.d[k], c)); if (dd < bd) { bd = dd; bi = k; } }
        lm[i] = bi;
        bi = 0; bd = 1e30f;
        for (int k = 0; k < nR; ++k) { float dd = V2Len(V2Sub(rightBank.d[k], c)); if (dd < bd) { bd = dd; bi = k; } }
        rm[i] = bi;
    }

    // --- Matched-range resample -> global cross-section lists (gL, gR, gV). ---
    V2List gL = { nullptr, 0, 0 }, gR = { nullptr, 0, 0 };
    float* gV = nullptr; int gN = 0, gCap = 0;
    float gMidArc = 0.0f; V2 prevMid = { 0.0f, 0.0f }; bool haveMid = false;
    int prevL = 0, prevR = 0;
    for (int i = 0; i < ptCount; ++i) {
        int cL = lm[i], cR = rm[i];
        if (!(cL > prevL && cR > prevR)) continue;   // both matched indices must advance
        if (cL == 0 && cR == 0) continue;

        // Perpendicularity gate (fb00 @0x1400d1101: crossVec = matchedLeft - matchedRight,
        // tangent = Normalize(interpolated centerline tangent); reject unless
        // Abs(Dot(Normalize(crossVec), tangent)) < DAT_1401720d0 = 0.02). Sign of
        // crossVec is irrelevant (Abs), so left/right bank labelling doesn't matter.
        // Skips skewed cross-sections without advancing prevL/prevR (faithful to the
        // retail goto-skip). Over-emit avoidance only.
        {
            V2 cross = V2Norm(V2Sub(leftBank.d[cL], rightBank.d[cR]));
            V2 ta = { CLx(i > 0 ? i - 1 : i),            CLy(i > 0 ? i - 1 : i) };
            V2 tb = { CLx(i < ptCount - 1 ? i + 1 : i),  CLy(i < ptCount - 1 ? i + 1 : i) };
            V2 tang = V2Norm(V2Sub(tb, ta));
            float d = cross.x * tang.x + cross.y * tang.y;
            if (std::fabs(d) >= 0.02f) continue;   // not perpendicular -> skip this cross-section
        }

        float sumL = 0.0f; for (int k = prevL; k < cL; ++k) sumL += V2Len(V2Sub(leftBank.d[k + 1], leftBank.d[k]));
        float sumR = 0.0f; for (int k = prevR; k < cR; ++k) sumR += V2Len(V2Sub(rightBank.d[k + 1], rightBank.d[k]));
        if (sumL < 2.0f && sumR < 2.0f) continue;    // both-banks-short gate (DAT_140172174=2.0)
        int cnt = (int)(sumL > sumR ? sumL : sumR);
        if (cnt < 1) cnt = 1;
        if (sumL / (float)cnt < kMinArcGate || sumR / (float)cnt < kMinArcGate) continue;  // degenerate gate

        V2* Lr = (V2*)std::malloc((size_t)cnt * sizeof(V2));   // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
        V2* Rr = (V2*)std::malloc((size_t)cnt * sizeof(V2));   // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
        if (!Lr || !Rr) { std::free(Lr); std::free(Rr); break; }
        ResampleRange(leftBank.d, prevL, cL, cnt, Lr);
        ResampleRange(rightBank.d, prevR, cR, cnt, Rr);
        for (int j = 0; j < cnt; ++j) {
            V2 mid = { (Lr[j].x + Rr[j].x) * 0.5f, (Lr[j].y + Rr[j].y) * 0.5f };
            if (haveMid) gMidArc += V2Len(V2Sub(mid, prevMid));
            prevMid = mid; haveMid = true;
            float v = gMidArc / kVDenom + kVOffset;   // V = cumulative flow arc / 32 - 16
            V2Push(&gL, Lr[j]); V2Push(&gR, Rr[j]);
            if (gN >= gCap) { gCap = gCap ? gCap * 2 : 256; gV = (float*)std::realloc(gV, (size_t)gCap * sizeof(float)); }   // LEFT: a CRT malloc / realloc result (the tree's own contour scratch, not a retail allocation): the tier-I allocation shape covers MemAlloc / Allocate only; the close decides
            gV[gN++] = v;
        }
        std::free(Lr); std::free(Rr);
        prevL = cL; prevR = cR;
    }

    // --- Stage C (faithful): 4-column strip between gL/gR, shared streamwise V. ---
    int estimate = gN * 4 * 2; if (estimate < 16) estimate = 16;
    WeldSet<OutVtx> weld; WeldInit(&weld, estimate);
    auto emitIdx = [&](uint16_t v) { VecPush(idxOut, v, 2); };
    for (int i = 0; i + 1 < gN; ++i) {
        V2 p0 = gL.d[i], p1 = gL.d[i + 1];
        float v0 = gV[i], v1 = gV[i + 1];
        float uLo = 0.0f, uHi = kUStep;
        for (int col = 0; col < 4; ++col) {
            float t = uHi;
            V2 pLo = Lerp(gL.d[i],     gR.d[i],     t);
            V2 pHi = Lerp(gL.d[i + 1], gR.d[i + 1], t);
            OutVtx V0 = { p0.x,  p0.y,  uLo, v0 };
            OutVtx V1 = { pLo.x, pLo.y, uHi, v0 };
            OutVtx V2v= { pHi.x, pHi.y, uHi, v1 };
            OutVtx V3 = { p1.x,  p1.y,  uLo, v1 };
            uint16_t i0 = WeldAdd(&weld, &V0);
            uint16_t i1 = WeldAdd(&weld, &V1);
            uint16_t i2 = WeldAdd(&weld, &V2v);
            uint16_t i3 = WeldAdd(&weld, &V3);
            emitIdx(i0); emitIdx(i2); emitIdx(i1);   // (V0,V2,V1),(V0,V3,V2)
            emitIdx(i0); emitIdx(i3); emitIdx(i2);
            uLo = uHi; uHi += kUStep; p0 = pLo; p1 = pHi;
        }
    }

    *vtxOut = weld.verts;
    weld.verts.data = nullptr;
    WeldFree(&weld);

    std::free(edges);
    std::free(leftBank.d); std::free(rightBank.d);
    std::free(lm); std::free(rm);
    std::free(gL.d); std::free(gR.d); std::free(gV);
}

} // anonymous namespace

// ============================================================
// b260 PHASE 1 — contour water bodies.
// ============================================================
void WorldGeometry::BuildWaterBodies(const TerrainHeightfield* hf) {
    RawArray<GeometryDrawRange>* rangeA = &_contourRangesA;
    RawArray<TerrainVertex>*    vtxAcc = &_contourVertices;   // 16-byte GPU verts
    RawArray<uint16_t>*       idxAcc = &_contourIndices;    // uint16 indices

    int bodyCount = hf->paths._size;
    // 0x1400ab2e5: CALL 0x14001efd0, Collection::Array<GeometryDrawRange>::SetSize
    // (stride 0x10, MemAlloc align 4, the empty else arm); sets geom+0x1c8.
    RawArraySetSize(*rangeA, bodyCount);

    // Stage A: faithful dense rounded-bank builder. It falls back to the older
    // approximation for degenerate (<3-point) bodies that produce no geometry.

    for (int body = 0; body < bodyCount; ++body) {
        RawArray<OutVtx>   vtxOut{ nullptr, 0, 0 };
        RawArray<uint16_t> idxOut{ nullptr, 0, 0 };
        {
            fb00_BuildBody_Faithful(hf, body, &vtxOut, &idxOut);
            if (vtxOut.size == 0) {   // degenerate body -> approximation fallback
                if (vtxOut.data) System::MemoryAllocator::MemFree(vtxOut.data);
                if (idxOut.data) System::MemoryAllocator::MemFree(idxOut.data);
                vtxOut = RawArray<OutVtx>{ nullptr, 0, 0 };
                idxOut = RawArray<uint16_t>{ nullptr, 0, 0 };
                fb00_BuildBody(hf, body, &vtxOut, &idxOut);
            }
        }

        // Range entry: {idxStart, vtxStart, vtxCount, idxCount}.
        GeometryDrawRange& re = rangeA->data[body];
        re.values[0]  = idxAcc->size;      // idxStart (geom+0x1b8)
        re.values[1] = vtxAcc->size;      // vtxStart (geom+0x1a8)
        re.values[2] = vtxOut.size;       // vtxCount
        re.values[3]  = idxOut.size;       // idxCount

        // Pack + append each fb00 vertex into the GPU staging buffer.
        int zBody = (int)(kZBaseP1 - (float)body);   // z = -192 - bodyIdx
        const OutVtx* ov = vtxOut.data;
        for (int i = 0; i < vtxOut.size; ++i) {
            TerrainVertex gv;
            gv.position[0] = (int16_t)(int)(ov[i].posX * kPosScale);
            gv.position[1] = (int16_t)(int)(ov[i].posY * kPosScale);
            gv.position[2] = (int16_t)zBody;
            gv.position[3] = 0;
            gv.uv[0] = (int16_t)(int)(ov[i].uvU * kUVScale);
            gv.uv[1] = (int16_t)(int)(ov[i].uvV * kUVScale);
            gv.normal[0] = 0x7f; gv.normal[1] = 0x7f; gv.normal[2] = 0xff; gv.normal[3] = 0x00;
            VecPush(vtxAcc, gv, 2);
        }
        // Append the body-local indices verbatim (GPU adds baseVertex=vtxStart).
        const uint16_t* oi = idxOut.data;
        for (int i = 0; i < idxOut.size; ++i)
            VecPush(idxAcc, oi[i], 2);

        if (vtxOut.data) System::MemoryAllocator::MemFree(vtxOut.data);
        if (idxOut.data) System::MemoryAllocator::MemFree(idxOut.data);
    }
}

// ============================================================
// b260 PHASE 2 — heightmap shore ring around the heightfield's water features.
// Faithful port: for each feature, walk the bounding box [center +/- (radius+2)],
// emit a heightmap-conformal quad per cell that straddles the water boundary
// (some corner inside radius AND some corner underwater <= -0.75). Per-vertex
// planar UV (= pos.xy * 48) and an AO byte (= weight * 255). Welded via f3e0.
// ============================================================
void WorldGeometry::BuildWaterShores(const TerrainHeightfield* hf) {
    RawArray<GeometryDrawRange>* rangeB = &_contourRangesB;
    RawArray<TerrainVertex>*    vtxAcc = &_contourVertices;
    RawArray<uint16_t>*       idxAcc = &_contourIndices;

    int featureCount = hf->waterFeatures._size;
    RawArraySetSize(*rangeB, featureCount);   // 0x1400ab66b: CALL 0x14001efd0

    int   N        = _heightRowStride;           // heightmap edge
    const int16_t* hmap = _heights.data;      // int16 heightmap
    int   halfGrid = _gridSize >> 1;
    const TerrainStamp* featBase = hf->waterFeatures._data;


    if (!hmap || N <= 0) return;
    if (!featBase) return;

    auto sampleH = [&](int worldCol, int worldRow) -> float {
        int ci = worldCol + halfGrid; if (ci < 0) ci = 0; if (ci >= N) ci = N - 1;
        int ri = worldRow + halfGrid; if (ri < 0) ri = 0; if (ri >= N) ri = N - 1;
        return (float)(int)hmap[(size_t)ri * N + ci] * kHeightStep;
    };

    for (int f = 0; f < featureCount; ++f) {
        const TerrainStamp& feat = featBase[f];
        float cx = feat.x, cy = feat.y, radius = feat.radius;
        float r2 = radius + kRadMargin;

        int xmin = (int)(cx - r2), xmax = (int)(cx + r2);
        int ymin = (int)(cy - r2), ymax = (int)(cy + r2);

        GeometryDrawRange& re = rangeB->data[f];
        re.values[0]  = idxAcc->size;     // idxStart
        re.values[1] = vtxAcc->size;     // vtxStart
        re.values[2] = 0;                // vtxCount  (filled after)
        re.values[3]  = 0;                // idxCount  (filled after)

        int zFeat = (int)((float)f - kZBaseP2);   // z = featureIdx - 191

        // Per-feature local weld set (matches the b260 per-feature local_1c8).
        int est = (xmax - xmin + 1) * (ymax - ymin + 1) * 4;
        if (est < 16) est = 16;
        WeldSet<TerrainVertex> weld; WeldInit(&weld, est);
        int idxAdded = 0;

        for (int row = ymin; row < ymax; ++row) {
            for (int col = xmin; col < xmax; ++col) {
                // 4 cell corners {x, y, z=heightmap*1/256}
                float corner[4][3];
                corner[0][0] = (float)col;     corner[0][1] = (float)row;     corner[0][2] = sampleH(col,     row);
                corner[1][0] = (float)(col+1); corner[1][1] = (float)row;     corner[1][2] = sampleH(col + 1, row);
                corner[2][0] = (float)(col+1); corner[2][1] = (float)(row+1); corner[2][2] = sampleH(col + 1, row + 1);
                corner[3][0] = (float)col;     corner[3][1] = (float)(row+1); corner[3][2] = sampleH(col,     row + 1);

                bool someInterior = false;   // !bVar6
                bool someUnderwater = false;  // !bVar7
                float weight[4];
                for (int k = 0; k < 4; ++k) {
                    if (corner[k][2] <= kUnderwater) someUnderwater = true;
                    float dx = corner[k][0] - cx;
                    float dy = corner[k][1] - cy;
                    float dz = corner[k][2] - 0.0f;
                    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    // b260 @0x1400aba4b: DIVSS XMM0,XMM10 — the divisor is XMM10, which
                    // 0x1400ab745/49 set to `radius + kRadMargin` (the SAME value that
                    // bounds the bbox), NOT the bare 2.0 constant. The old `/ 2.0f` made
                    // the falloff reach zero at radius+2 instead of 2*radius+2, so
                    // `someInterior` was a circle inscribed in the bbox and the shore
                    // read as a hard rim. With the correct divisor `w > 0` holds across
                    // the whole bbox (measured: interior == cellsTested), so the gate
                    // reduces to `someUnderwater` and the shore falloff is gradual.
                    // NOTE: this is NOT why unmeshed ponds look like dark holes — those
                    // are basins that reach neither list b260 meshes. See
                    // project_water_contour_phase2_tarns_dark.
                    float w = 1.0f - (dist - radius) / r2;
                    if (w >= 0.0f) {
                        if (w > 1.0f) w = 1.0f;
                        if (w > 0.0f) someInterior = true;     // (==0 does not mark interior)
                    } else {
                        w = 0.0f;
                    }
                    weight[k] = w;
                }

                if (!(someInterior && someUnderwater)) continue;

                // Weld the 4 corners, then emit two triangles (i0,i1,i2),(i0,i2,i3).
                uint16_t ci[4];
                for (int k = 0; k < 4; ++k) {
                    TerrainVertex gv;
                    gv.position[0] = (int16_t)(int)(corner[k][0] * kPosScale);
                    gv.position[1] = (int16_t)(int)(corner[k][1] * kPosScale);
                    gv.position[2] = (int16_t)zFeat;
                    gv.position[3] = 0;
                    gv.uv[0] = (int16_t)(int)(corner[k][0] * kPosScale);  // planar UV = pos.xy
                    gv.uv[1] = (int16_t)(int)(corner[k][1] * kPosScale);
                    gv.normal[0] = 0x7f; gv.normal[1] = 0x7f;
                    gv.normal[2] = (uint8_t)(int64_t)(weight[k] * kAOScale);
                    gv.normal[3] = 0x00;
                    ci[k] = WeldAdd(&weld, &gv);
                }
                uint16_t quad[6] = { ci[0], ci[1], ci[2], ci[0], ci[2], ci[3] };
                for (int k = 0; k < 6; ++k) VecPush(idxAcc, quad[k], 2);
                idxAdded += 6;
            }
        }

        // Bulk-append the welded verts to the GPU staging buffer.
        const TerrainVertex* wv = weld.verts.data;
        for (int i = 0; i < weld.verts.size; ++i)
            VecPush(vtxAcc, wv[i], 2);

        re.values[2] = weld.verts.size;
        re.values[3]  = idxAdded;

        WeldFree(&weld);
    }
}

// ============================================================
// b260 (0x1400ab260): build the contour water mesh into this WorldGeometry from
// the heightfield (the feature polylines and the water features), then create
// the water VB (+0x160) / IB (+0x180).
// ============================================================
void WorldGeometry::BuildWaterMesh(TerrainHeightfield* hf) {
    if (!hf) return;
    if (!System::MemoryAllocator::_singleton) {
        fprintf(stderr, "[water_contour] MemAlloc/Free unavailable — skipping\n");
        fflush(stderr);
        return;
    }

    BuildWaterBodies(hf);
    BuildWaterShores(hf);   // shore rings around the hf->waterFeatures

    // Finalize the GPU buffers (b260 tail). IndexBuffer first, then VertexBuffer.
    _waterIndexBuffer.Create(_contourIndices.size * 2, false, _contourIndices.data);
    _waterVertexBuffer.Create(_contourVertices.size << 4, false, _contourVertices.data);

    fprintf(stderr, "[water_contour] built: bodies(+0x1c8)=%d featRanges(+0x1d8)=%d "
            "verts(+0x1a8)=%d idx(+0x1b8)=%d VB=%p IB=%p\n",
            _contourRangesA.size, _contourRangesB.size,
            _contourVertices.size, _contourIndices.size,
            static_cast<void*>(_waterVertexBuffer.platformData),
            static_cast<void*>(_waterIndexBuffer.platformData));
    fflush(stderr);
}
