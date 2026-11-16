// src/game/Game/worldgen_f390_rivers.cpp
// ============================================================================
// M5 worldgen f390 — RIVER NETWORK + FEATURE-STAMP (build pass) stages.
// ============================================================================
//
// These are two of the f390 heightmap-pipeline stages that run AFTER the base
// perlin heightmap (e5d0) and the path/range emitters (cd090/cd150/ccf60), in
// the verified f390 order (docs/history/notes/WORLDGEN_TERRAIN_BLUEPRINT.md §0):
//
//   gworld+0x24 = 3 ; cd460(hf)   <-- wg_f390_rivers   (river network)
//                     e910(hf)    <-- wg_f390_stamp     (feature stamp / build)
//
//   wg_f390_rivers  == FUN_1400cd460                    @0x1400cd460
//   wg_f390_stamp   == ConsumeProduceComponent_impl_e910 @0x1400ce910
//
// HEIGHT EFFECT SUMMARY (the important part — see per-function comments):
//   * cd460 does NOT touch the float heightmap at all. It only PRODUCES the
//     river-point vector listB (hf+0x28) of {x,y,radius} records (and consumes
//     the satellite list listC at hf+0x38 that cd150 built). Pure data stage.
//   * e910 is the stage that actually MODIFIES the heightmap. For each cell it
//     computes  h_out = maskB * h_in * maskA - maskC  where:
//       - maskA (init 1.0, MIN-blended by the river points) PINCHES height down
//         toward 0 along rivers (river cells -> h*~0 -> sea level / valley floor)
//       - maskB (init 1.0, MIN-blended by patch "inner" radii) likewise scales
//         height down inside stamped feature regions
//       - maskC (init 0.0, MAX-blended by patch "outer" radii + mountain
//         satellites) is SUBTRACTED, carving depressions / lakes BELOW the
//         multiplicative result (can drive height negative -> below sea level).
//     Net: rivers/lakes get carved down to ~0 or negative (water); the dominant
//     visible effect of THIS stage is the carve-down (subtractive maskC + river
//     pinch). Magnitude: maskA/maskB in [0,1] (multiplicative attenuation up to
//     100%); maskC adds a subtractive term up to strength*1.0 per the eee0
//     falloff (river strength 1.0, satellite strength 2.0 -> subtract up to ~2
//     height units; lake floors near/below 0).
//
// Sub-workers reimplemented here (recursively):
//   * sub_eee0 (radial powf-falloff brush)  @0x1400ceee0  — the stamp primitive,
//     shared by BOTH e910 and ec50 (the finalize twin). static here; ec50 has its
//     own copy, wg_eee0_radial_brush (worldgen_f390_erosion.cpp).
//   * FUN_1400245d0 (0xC-stride {x,y,w} vector append) @0x1400245d0 — shared by
//     cd150/ccf60/cd460. static here.
//   * sub_17640 + helper_f9e0 (vector<vector<int>> resize) @0x140017640 — cd460's
//     cluster-member buckets. static here.
//   * cd460's two int sets (AVL trees grown with 0x140014c70 / 0x140014da0, freed
//     with 0x140014c30) decide only membership and ascending order; the port keeps
//     them as plain arrays (wg_RiverClusterLabels / wg_RiverConnectClusters).
//
// All field offsets verified against Ghidra disassembly (blueprint §1.1).

#include "Game/TerrainHeightfield.h"   // TerrainHeightfield, the stage declarations
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/exe_bodies.h"
#include "math/Math.h"
#include "collection/Collection.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "Game/worldgen_brush_curve.h"   // [WGPAR] the exact falloff memo of the eee0 brush
#include "crt/msvc_libm.h"               // MsvcLibm::Powf (wg_powf below)

// ============================================================
// The heightfield is TerrainHeightfield (TerrainHeightfield.h). The originals
// index it as int*: param_1[0] gridSize, [2] / [4] heightmap data / size,
// [6] / [8] paths, [10] / [0xc] riverPoints (listB), [0xe] / [0x10] basinStamps
// (listC: radial carve stamps, NOT peaks: fed to maskC, which is SUBTRACTED),
// +0x58 random, +0xa28 sizeRow.
// ============================================================

// ============================================================
// .rdata float constants (read via read_memory; address=value documented).
// ============================================================
static constexpr float kJoinThresh = 1.2f;       // 0x140172154 = 0x3f99999a  cd460 spatial-join factor
static constexpr float kInf        = 1000000.0f; // 0x140172268 = 0x49742400  nearest-neighbour init (+inf)
static constexpr float kRiverExp   = 1.75f;      // 0x140172164 = 0x3fe00000  e910 river-point powf exponent
static constexpr float kSatExp     = 2.25f;      // 0x14017217c = 0x40100000  e910 satellite powf exponent
static constexpr float kSatStr     = 2.0f;       // 0x140172174 = 0x40000000  e910 satellite strength
static constexpr float kConnectStep   = 2.0f;    // 0x140172174 = 0x40000000  cd460 connect spacing
static constexpr float kConnectRadius = 10.0f;   // 0x1400ce395 imm 0x41200000 cd460 connect point radius
static constexpr float kRadiusThr  = 0.99f;      // 0x140172144 = 0x3f7d70a4  eee0 radius-growth threshold
static constexpr float kOne        = 1.0f;       // 0x14017214c = 0x3f800000  g_fUVScale (brush numerator)
static constexpr uint32_t kSignBit = 0x80000000u;// 0x1401722c0               float-negate XOR mask

// kJoinThresh is the cd460 labeling's join factor; kInf, kConnectStep and kConnectRadius
// belong to its connect pass.

// ============================================================
// Engine helpers used by these stages, called by name: System::MemoryAllocator::MemAlloc /
// MemFree, Math::Vector2::Dot and System::Random::RandFloat / RandFloatTol.
// ============================================================
// System::Random::RandFloatTol()  ?RandFloatTol@Random@System@@QEAAMXZ:
// called as rng->RandFloatTol().

// CRT libm leaves (verified: sub_1460 = powf, FUN_140132010 = sqrtf). powf is the exe's own MSVC CRT
// body, ported bit-exact in MsvcLibm (crt/msvc_libm.h): MinGW's powf differs in the last bit, and differs
// again between mingw-w64 runtimes (WorldHash f32 changed from GCC 13 to 16, 2026-10-26); sqrtf is one
// instruction everywhere.
static inline float wg_powf(float b, float e) { return MsvcLibm::Powf(b, e); }
static inline float wg_sqrtf(float v)         { return sqrtf(v); }

// negate a float via the XOR sign-bit mask (matches the original codegen).
static inline float wg_bitNegate(float v) {
    uint32_t u; std::memcpy(&u, &v, 4); u ^= kSignBit; float r; std::memcpy(&r, &u, 4); return r;
}

extern "C" void TradeComponent_helper_f9e0(RawArray<int32_t>* dst, const RawArray<int32_t>* src);  // deep-copy of a {data,count,cap} bucket

// ============================================================
// FUN_1400245d0  @0x1400245d0 — push one {float x, float y, float w} (0xC bytes)
// record onto a {void* data,int count,int cap,int _} vector, growing 3x when the
// capacity is exceeded. Faithful transcription (grow-by-3x idiom). newCount is the
// NEW count (count+1); rec points to the 0xC source record.
// HEIGHT EFFECT: none (pure data). Shared by cd150/ccf60/cd460.
//
// ENDGAME B.3.2 step 2: now the 3-argument `Collection::Array<TerrainStamp>::SetSize`
// (rec is the fill) — the same trait-derived instantiation worldgen_f390_ranges.cpp
// uses for its AppendVec3 twin of this retail body. Trait-equivalent: three
// floats, 12-byte `assign` (byteCopy == is_trivially_copyable), no destroy, and
// the 2-argument form's `init` is never reached by a fill call. Compared arm
// for arm against rtcoll::SetSizeCore: exact-equal early return, `_size` first,
// treble-on-single-append-from-non-empty, cap*0xC at align 4, prefix copied
// only when the block moved, fill at or past the old count, old block freed
// only when it moved. Every arm matched.
// ============================================================
// The record is TerrainStamp {x, y, radius}; listB is hf->riverPoints.
using WgVec3Array = Collection::Array<TerrainStamp>;

static void wg_VecPush0xC(WgVec3Array* hdr, int newCount, const TerrainStamp& rec) {
    hdr->SetSize(newCount, rec);
}

// ============================================================
// sub_17640 + helper_f9e0  @0x140017640 — resize a vector<vector<int>> to N empty
// {int* data,int count,int cap} buckets. cd460 uses one bucket per cluster label.
// HEIGHT EFFECT: none.
// ============================================================
static void wg_VecOfVecResize(RawArray<RawArray<int32_t>>* h, int newCount) {
    int oldCount = h->size;
    if (newCount == oldCount) return;
    RawArray<int32_t>* oldData = h->data;
    h->size = newCount;
    int cap = h->capacity;
    if (cap < newCount) {
        cap = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) cap = newCount * 3;
        h->capacity = cap;
        RawArray<int32_t>* nd = static_cast<RawArray<int32_t>*>(System::MemoryAllocator::MemAlloc((uint64_t)cap * 0x10, 8));
        h->data = nd;
    }
    RawArray<int32_t>* newData = h->data;
    for (int i = 0; i < newCount; ++i) {
        RawArray<int32_t>* slot = &newData[i];
        if (i < oldCount && newData == oldData) continue;            // keep in place
        slot->data = nullptr;                                        // {data,count,cap}=0
        slot->size = 0;
        slot->capacity = 0;
        if (i < oldCount) {                                          // deep-copy from old
            TradeComponent_helper_f9e0(slot, &oldData[i]);
        }
    }
    if (oldData && oldData != newData) {
        for (int i = 0; i < oldCount; ++i) {
            int32_t* inner = oldData[i].data;
            if (inner) System::MemoryAllocator::MemFree(inner);
        }
        System::MemoryAllocator::MemFree(oldData);
    }
}

// ============================================================
// sub_eee0  @0x1400ceee0 — RADIAL POWF-FALLOFF BRUSH (the stamp primitive).
// Shared by e910 (this file) AND ec50 (finalize twin — should reuse THIS one).
//
//   params: hf, center{x,y} (param_2[0],param_2[1]), radiusParam (param_3),
//           falloffPow (param_4), strength (param_5), mode (param_6: 0=MIN,1=MAX),
//           target mask buffer (param_7 -> float[(dim+1)^2]).
//
//   1. Grow the stamp radius: r starts at 1.0; while  1/(curve+1) < 0.99  where
//      curve = powf(falloffPow, -(r - radiusParam)), increment r by 1.0. So the
//      effective integer radius extends until the edge falloff weight reaches
//      0.99 (kRadiusThr).
//   2. For each cell (dx,dy) in the [-ri..ri] square around the center:
//        dist   = sqrt(dx^2 + dy^2)
//        weight = 1/(powf(falloffPow, -(dist - radiusParam)) + 1)   (in (0,1], lowest at center)
//        MIN mode (param_6==0):  v = weight*strength;     mask = min(mask, v)
//        MAX mode (param_6==1):  v = (1-weight)*strength; mask = max(mask, v)  (strict >)
//
//   The exponent is NEGATED via XOR sign-bit (g_fUVScale-param_3 etc. XOR
//   0x80000000) — i.e. powf(falloffPow, -(x)). Center column/row use (dim>>1)
//   offsets so center{x,y} are in [-dim/2..dim/2] grid coords.
//
// HEIGHT EFFECT: writes a MASK, not the heightmap. MIN masks (river/inner-patch)
// drive cells toward 0 (full attenuation at center). MAX masks (outer-patch/
// satellite) raise the subtractive term toward `strength` at center.
// ============================================================
static void wg_StampBrush_eee0(const TerrainHeightfield* hf, const float* center, float radiusParam,
                               float falloffPow, float strength, char mode,
                               float* mask) {
    int dim   = hf->gridSize;        // gridSize
    int edge  = dim + 1;             // (dim+1) cells per row
    int half  = dim >> 1;

    // [WGPAR] the falloff curve comes from an exact memo (Game/worldgen_brush_curve.h):
    // the radius growth and w(s) = 1/(powf(falloffPow, -(sqrt(s) - radiusParam)) + 1)
    // are evaluated by the same wg_powf / wg_sqrtf, negate and float expression as this
    // body did, keyed on the bits of (falloffPow, radiusParam) -- byte-identical values,
    // each distinct (brush, s) computed once instead of once per cell. One cache per
    // process: worldgen runs on one thread at a time (the loader).
    static WgBrushCurveCache s_curves(&wg_powf, &wg_sqrtf, kOne, kRadiusThr);
    WgBrushCurveCache::Curve& curve = s_curves.Get(falloffPow, radiusParam);

    int   ri  = curve.span;              // integer radius bound (inclusive): (int)r + 1
    int   cxC = (int)center[0] + half;   // center col in grid space
    int   cyC = (int)center[1] + half;   // center row in grid space

    for (int dy = -ri; dy <= ri; ++dy) {
        int row = dy + cyC;
        if (row < 0 || row >= edge) continue;
        for (int dx = -ri; dx <= ri; ++dx) {
            int col = dx + cxC;
            if (col < 0 || col >= edge) continue;
            float w    = s_curves.Weight(curve, dx * dx + dy * dy);   // (0,1]
            float* cell = &mask[(size_t)row * edge + col];
            if (mode == 0) {                   // MIN blend (carve-down weight)
                float v = w * strength;
                if (v < *cell) *cell = v;
            } else {                           // MAX blend (subtractive term)
                float v = (kOne - w) * strength;
                if (*cell <= v && v != *cell) *cell = v;   // strict >
            }
        }
    }
}

// ============================================================
// wg_f390_rivers  == FUN_1400cd460  @0x1400cd460 — GENERATE RIVER NETWORK.
//
// PURPOSE: scatter river points, then (a) label them into clusters of touching
// points and (b) join every other cluster to cluster 0 with a line of extra points.
// OUTPUT is the river-point vector listB (hf+0x28) of {x,y,radius} records
// consumed later by e910's river-carve pass.
//
// ALGORITHM (faithful to the decomp):
//   center = (sizeRow[0]/2) - 2          ; iVar32, radial placement reference
//   PASS 1 (edge-set 1, sizeRow[+0x10]=count1, [+0x14]=min1, [+0x18]=max1):
//     for count1 points: edge index e = RandFloat*(max1-min1)+min1;
//       off = center - (int)e; dir = (RandFloatTol, RandFloatTol);
//       pt = (off*dir.x, off*dir.y); UNCONDITIONALLY append to scratch list
//       local_d0 with radius slot = e.
//   PASS 2 (edge-set 2, sizeRow[+0x04]=count2, [+0x08]=min2, [+0x0c]=max2):
//     for count2 points: same construction, but REJECT if the new point lies
//     within sqrt(Dot(d,d)) + e of any scratch point (Vector2::Dot then sqrtf vs
//     that point's radius). Accepted points are appended to listB (hf+0x28).
//   LABELING (wg_RiverClusterLabels): one label per point; touching points
//     (sqrt(dot)*1.2 < radius_i + radius_j, kJoinThresh) share the lowest label of
//     every cluster they touch.
//   CONNECT (wg_RiverConnectClusters): one bucket of point indices per distinct
//     label; for each cluster c >= 1, two Random::Rand draws from hf->random pick a
//     member of c and a start for the nearest-member search in cluster 0, then
//     {x, y, 10.0} points every 2.0 units from that member toward its nearest
//     cluster-0 member are appended to listB, and c is merged into cluster 0.
//   Frees the buckets, the label set, the label array, the scratch list.
//
// HEIGHT EFFECT: cd460 never reads or writes hf+0x08 (the heightmap). It populates
// listB (hf+0x28) with {x,y,radius} river points, the connect points included; e910
// (below) reads listB and carves those cells down. Its connect draws advance
// hf->random, which cc080's droplets draw from next.
// ============================================================
void wg_f390_rivers(TerrainHeightfield* hfp) {
    System::Random* rng = &hfp->random;
    const TerrainSizeRow* sizeRow = hfp->sizeRow;
    if (!sizeRow) return;

    int center = (sizeRow->mapSize >> 1) - 2;       // iVar32

    // --- scratch point list local_d0 {void* data,int count,int cap,int _} ---
    // ENDGAME B.3.2 step 2: a real Collection::Array with RAII; the manual
    // free in the teardown block below is the destructor now (single exit).
    WgVec3Array scratch;
    int scratchCount = 0;

    // PASS 1: edge-set 1 (count1 @ +0x10, min1 @ +0x14, max1 @ +0x18).
    int count1 = sizeRow->riverCount1;        // +0x10
    for (int i = 0; i < count1; ++i) {
        int   minE = sizeRow->riverMinEdge1;  // +0x14
        int   maxE = sizeRow->riverMaxEdge1;  // +0x18
        float fE    = rng->RandFloat() * (float)(maxE - minE) + (float)minE;
        float draw1 = rng->RandFloatTol();
        float draw2 = rng->RandFloatTol();
        float off   = (float)center - (float)(int)fE;
        // retail CONCAT44(off*draw1, off*draw2): x=low=off*draw2, y=high=off*draw1.
        TerrainStamp rec = { off * draw2, off * draw1, fE };   // {x, y, radius=edgeParam}
        wg_VecPush0xC(&scratch, scratchCount + 1, rec);
        scratchCount = scratch._size;
    }

    // PASS 2: edge-set 2 (count2 @ +0x04, min2 @ +0x08, max2 @ +0x0c).
    int count2 = sizeRow->riverCount2;        // +0x04
    for (int i = 0; i < count2; ++i) {
        int   minE = sizeRow->riverMinEdge2;  // +0x08
        int   maxE = sizeRow->riverMaxEdge2;  // +0x0c
        float fE    = rng->RandFloat() * (float)(maxE - minE) + (float)minE;
        float draw1 = rng->RandFloatTol();
        float draw2 = rng->RandFloatTol();
        float off   = (float)center - (float)(int)fE;
        // retail CONCAT44(off*draw1, off*draw2): new.x=low=off*draw2, new.y=high=off*draw1.
        float newX  = off * draw2;
        float newY  = off * draw1;
        // reject if too close to any existing scratch point:
        //   for each pt s: d = new - s; len = sqrt(Dot(d,d)); if len+fE < s.radius -> reject
        bool reject = false;
        const TerrainStamp* sd = scratch._data;
        for (int j = 0; j < scratchCount; ++j) {
            const TerrainStamp& sp = sd[j];
            // {dx, dy} = retail local_res18/1c; retail calls Dot(&d, &d) for |d|^2.
            Math::Vector2 dv(newX - sp.x, newY - sp.y);
            float dot = dv.Dot(dv).v;
            float len = wg_sqrtf(dot);
            if (len + fE < sp.radius) { reject = true; break; }
        }
        if (!reject) {
            TerrainStamp rec = { newX, newY, fE };
            wg_VecPush0xC(&hfp->riverPoints, hfp->riverPoints._size + 1, rec);
        }
    }

    // LABELING + CONNECT over the river points pass 2 kept. The label array is one int
    // per point (0x1400cd6d4..0x1400cd761).
    int riverCount = hfp->riverPoints._size;
    // Retail stores labels[0] = 0 (0x1400cd761) through the array unguarded: with no
    // river point the array was never allocated and that store faults. The port keeps
    // the label and connect passes behind a non-empty list instead.
    if (riverCount > 0) {
        int32_t* labels = static_cast<int32_t*>(
            System::MemoryAllocator::MemAlloc((uint64_t)riverCount * 4, 4));
        wg_RiverClusterLabels(hfp->riverPoints._data, riverCount, labels);
        wg_RiverConnectClusters(hfp, labels, riverCount);
        System::MemoryAllocator::MemFree(labels);   // 0x1400ce572
    }
    // (scratch: freed by its destructor at scope exit, right here in retail's order, 0x1400ce58b)
}

// ============================================================
// cd460's labeling  0x1400cd73b..0x1400cde06 — assign every river point a cluster label.
//
// Every point starts as -1, then point 0 is 0 (0x1400cd761). For each point i in order:
//   * labels[i] = fresh, the next unused label (0 for i = 0; afterwards one more than the
//     highest label seen at the end of the previous point, 0x1400cddb9);
//   * a set of labels (an AVL tree at rsp+0x20, rebuilt for every point, 0x1400cd790) gets
//     labels[i], then labels[j] of every earlier point j (j = 0..i-1) that touches i:
//     sqrtf(Dot(p_i - p_j, p_i - p_j)) * 1.2 < r_i + r_j (0x1400cda27..0x1400cda8c);
//   * lowest = the smallest of those labels (0x1400cdcf0 cmovl);
//   * for k = 0..i, highest = max(highest, labels[k]) on the value before the relabel
//     (0x1400cdd33 cmovg, highest starts at 0), and a labels[k] in the set becomes lowest
//     (0x1400cdd57..0x1400cdd5e).
// So a point that touches several clusters merges all of them into the lowest label;
// labels are not contiguous afterwards. The set's tree shape decides nothing: every member
// maps to the same lowest label.
// HEIGHT EFFECT: none (labels only).
// ============================================================
void wg_RiverClusterLabels(const TerrainStamp* points, int count, int32_t* labels) {
    for (int i = 0; i < count; ++i) {
        labels[i] = -1;                                // 0x1400cd750
    }
    labels[0] = 0;                                     // 0x1400cd761

    std::vector<int32_t> touched;                      // retail: the AVL set at rsp+0x20
    touched.reserve((size_t)count);
    int32_t fresh = 0;                                 // r12d
    for (int i = 0; i < count; ++i) {
        labels[i] = fresh;                             // 0x1400cd79a
        touched.clear();
        touched.push_back(labels[i]);
        int32_t lowest = fresh;
        const TerrainStamp& pi = points[i];
        for (int j = 0; j < i; ++j) {
            const TerrainStamp& pj = points[j];
            // {dx, dy} = p_i - p_j; retail calls Dot(&d, &d) for |d|^2, then sqrtf.
            Math::Vector2 dv(pi.x - pj.x, pi.y - pj.y);
            float dot = dv.Dot(dv).v;
            float len = wg_sqrtf(dot);
            if (len * kJoinThresh < pi.radius + pj.radius) {
                touched.push_back(labels[j]);
                if (labels[j] < lowest) {
                    lowest = labels[j];
                }
            }
        }
        int32_t highest = 0;                           // r9d
        for (int k = 0; k <= i; ++k) {
            int32_t v = labels[k];
            if (v > highest) {
                highest = v;
            }
            for (int32_t member : touched) {
                if (v == member) {
                    labels[k] = lowest;
                    break;
                }
            }
        }
        fresh = highest + 1;
    }
}

// ============================================================
// cd460's connect pass  0x1400cde0c..0x1400ce4f1 (+ the teardown to 0x1400ce563) — join
// the river-point clusters with a line of extra river points.
//
//   1. The distinct labels, ascending (an AVL set at rsp+0x30, 0x1400cde0c..0x1400ce096),
//      give one bucket each: buckets resized to that count (0x1400ce0af call 0x140017640),
//      bucket r = the indices k (ascending) whose label is the r-th smallest
//      (0x1400ce0f0..0x1400ce211). Bucket 0 is point 0's cluster.
//   2. With more than one bucket (0x1400ce224 cmp eax,1 / jle), for c = 1..count-1:
//        from    = bucket[c][hf->random.Rand(bucket[c].size)]        0x1400ce275
//        nearest = bucket[0][hf->random.Rand(bucket[0].size)]        0x1400ce28c
//        best = 1e6 (0x140172268); for each member m of bucket 0, in order:
//          len = Length(p_from - p_m) (0x1400ce2f7); if best > len: best = len, nearest = m
//        dir = Normalize(p_nearest - p_from, &length) (0x1400ce368, length preset 0)
//        pos = p_from.xy; travelled = 0
//        if length > 0: do { append {pos.x, pos.y, 10.0} to riverPoints (0x1400ce3ab);
//                             travelled += 2.0; pos += dir * 2.0 (0x1400ce3d8) }
//                        while length > travelled
//        bucket 0 gets bucket c's members appended, in order (0x1400ce410..0x1400ce4b1);
//        bucket c is freed and zeroed (0x1400ce4c2).
//      So every cluster joins the growing cluster 0, two Rand draws per extra cluster.
//   3. Every bucket's data is freed, then the bucket array (0x1400ce4f5..0x1400ce520).
// HEIGHT EFFECT: the appended points are stamped into maskA by e910 like every other
// river point (radius 10, pow 1.75), so the connecting channels lower the heights; the
// Rand draws move hf->random for every later stage that draws from it (cc080 erosion).
// ============================================================
void wg_RiverConnectClusters(TerrainHeightfield* hfp, const int32_t* labels, int count) {
    // 1. distinct labels, ascending. Point i's fresh label is i and a relabel only lowers
    //    one, so every label lies in 0..count-1.
    std::vector<int32_t> clusterLabels;
    {
        std::vector<uint8_t> present((size_t)count, 0);
        for (int k = 0; k < count; ++k) {
            present[(size_t)labels[k]] = 1;
        }
        for (int label = 0; label < count; ++label) {
            if (present[(size_t)label]) {
                clusterLabels.push_back(label);
            }
        }
    }
    int clusterCount = (int)clusterLabels.size();

    RawArray<RawArray<int32_t>> buckets = { nullptr, 0, 0 };
    wg_VecOfVecResize(&buckets, clusterCount);                  // 0x1400ce0af
    for (int r = 0; r < clusterCount; ++r) {
        RawArray<int32_t>& bucket = buckets.data[r];
        for (int k = 0; k < count; ++k) {
            if (labels[k] == clusterLabels[(size_t)r]) {
                RawArraySetSize(bucket, bucket.size + 1, (int32_t)k);
            }
        }
    }

    // 2. join every cluster to cluster 0.
    if (clusterCount > 1) {
        System::Random* rng = &hfp->random;
        RawArray<int32_t>& merged = buckets.data[0];
        for (int c = 1; c < clusterCount; ++c) {
            RawArray<int32_t>& cluster = buckets.data[c];
            int32_t from    = cluster.data[rng->Rand((uint32_t)cluster.size)];   // 0x1400ce275
            int32_t nearest = merged.data[rng->Rand((uint32_t)merged.size)];     // 0x1400ce28c
            float best = kInf;
            for (int m = 0; m < merged.size; ++m) {
                const TerrainStamp* pts = hfp->riverPoints._data;
                int32_t member = merged.data[m];
                Math::Vector2 d(pts[from].x - pts[member].x, pts[from].y - pts[member].y);
                float len = d.Length().v;                               // 0x1400ce2f7
                if (best > len) {
                    best    = len;
                    nearest = member;
                }
            }
            const TerrainStamp* pts = hfp->riverPoints._data;
            Math::Vector2 toward(pts[nearest].x - pts[from].x, pts[nearest].y - pts[from].y);
            Math::Scalar length(0.0f);
            Math::Vector2 dir = toward.Normalize(length);               // 0x1400ce368
            Math::Vector2 pos(pts[from].x, pts[from].y);
            float travelled = 0.0f;
            if (length.v > 0.0f) {
                do {
                    TerrainStamp rec = { pos.x, pos.y, kConnectRadius };
                    wg_VecPush0xC(&hfp->riverPoints, hfp->riverPoints._size + 1, rec);   // 0x1400ce3ab
                    travelled += kConnectStep;
                    Math::Vector2 step(dir.x * kConnectStep, dir.y * kConnectStep);
                    // Vector2::operator+= (0x140151e68 import, Runtime 0x180060000): rhs + this.
                    pos.x = step.x + pos.x;
                    pos.y = step.y + pos.y;
                } while (length.v > travelled);
            }
            for (int m = 0; m < cluster.size; ++m) {
                RawArraySetSize(merged, merged.size + 1, cluster.data[m]);
            }
            System::MemoryAllocator::MemFree(cluster.data);             // 0x1400ce4c2
            cluster.data     = nullptr;
            cluster.size     = 0;
            cluster.capacity = 0;
        }
    }

    // 3. teardown (0x1400ce4f5..0x1400ce520).
    for (int r = 0; r < clusterCount; ++r) {
        System::MemoryAllocator::MemFree(buckets.data[r].data);
        buckets.data[r].data     = nullptr;
        buckets.data[r].size     = 0;
        buckets.data[r].capacity = 0;
    }
    System::MemoryAllocator::MemFree(buckets.data);
}

// ============================================================
// wg_f390_stamp  == ConsumeProduceComponent_impl_e910  @0x1400ce910 —
// FEATURE STAMP (BUILD PASS). The stage that ACTUALLY MODIFIES THE HEIGHTMAP.
//
// Builds three temp float masks over (dim+1)^2 cells, stamps features into them
// with the eee0 radial brush, then combines them into the heightmap:
//
//   maskB (init 1.0)  <- patch "inner" radii, MIN blend   (param_6=0)
//   maskA (init 1.0)  <- river points (listB), MIN blend  (param_6=0)
//   maskC (init 0.0)  <- patch "outer" radii, MAX blend   (param_6=1)
//                     <- mountain satellites (listC), MAX blend (param_6=1)
//
//   FINAL per cell:   h[i] = maskB[i] * h[i] * maskA[i] - maskC[i]
//
// STAMPS (verified from decomp):
//   * patchList (hf+0x18, param_1[6]=data,[8]=count): each patch is a list of
//     0x1c-byte records {x,y, outerStr(+8), innerRadius(+c), innerPow(+10),
//     outerRadius(+14), outerPow(+18)}. For each record:
//       eee0(center, innerRadius, innerPow, strength=1.0, MIN, maskB)
//       eee0(center, outerRadius, outerPow, strength=outerStr, MAX, maskC)
//   * listC (hf+0x38, mountain satellites, [0xe]=data,[0x10]=count): each {x,y,
//     radius(+8)}: eee0(center, radius, falloffPow=2.25(kSatExp),
//                       strength=2.0(kSatStr), MAX, maskC)
//   * listB (hf+0x28, river points, [10]=data,[0xc]=count): each {x,y,radius(+8)}:
//       eee0(center, radius, falloffPow=1.75(kRiverExp), strength=1.0, MIN, maskA)
//
// HEIGHT EFFECT (the key deliverable):
//   - maskA (rivers, MIN, init 1, str 1): along river points the mask drops from
//     1 toward 0 (1/(curve+1) ~ 0 at center). MULTIPLYING h by maskA PINCHES
//     river-cell heights toward 0 -> valley floor / sea level. Up to 100%
//     attenuation at the centerline, feathered by the 1.75 falloff.
//   - maskB (patch inner, MIN, init 1, str 1): same multiplicative pinch inside
//     building/feature regions -> flattens/lowers them toward 0.
//   - maskC (patch outer + satellites, MAX, init 0): a SUBTRACTED term. Satellites
//     use strength 2.0 -> subtract up to ~2.0 height units at peak (carving lake
//     basins / depressions; can drive h BELOW 0 = below sea level). Patch outer
//     uses each record's outerStr. Subtracted AFTER the multiply, so it guarantees
//     carved depressions even where maskA/maskB are ~1.
//   NET: this stage LOWERS terrain — pinches river/feature cells toward 0 and
//   carves satellite/outer regions below the multiplicative surface (down to/below
//   sea level for lake/river floors). It does NOT raise terrain. Given base heights
//   ~0..60 (valley ~8), river cells end ~0–2 and lake/satellite floors can reach
//   slightly negative — feeding the classifier's buildable band [-0.25, 0.75]
//   around the carved water margins.
// ============================================================
void wg_f390_stamp(TerrainHeightfield* hfp) {
    const TerrainHeightfield* hf = hfp;
    int n = hfp->heightmap.size;   // (dim+1)^2 cell count
    if (n <= 0) return;

    // three mask buffers {float* data,int count,int cap} — sized via sub_3a00.
    RawArray<float> hdrB = { nullptr, 0, 0 };   // maskB (local_b0)
    RawArray<float> hdrC = { nullptr, 0, 0 };   // maskC (local_c0)
    RawArray<float> hdrA = { nullptr, 0, 0 };   // maskA (local_a0)
    RawArraySetSize(hdrB, n);   // 0x140023a00
    RawArraySetSize(hdrC, n);   // 0x140023a00
    RawArraySetSize(hdrA, n);   // 0x140023a00
    float* maskB = hdrB.data;
    float* maskC = hdrC.data;
    float* maskA = hdrA.data;
    if (!maskB || !maskC || !maskA) {
        if (maskA) System::MemoryAllocator::MemFree(maskA);
        if (maskC) System::MemoryAllocator::MemFree(maskC);
        if (maskB) System::MemoryAllocator::MemFree(maskB);
        return;
    }

    // init: maskB=1, maskC=0, maskA=1.
    for (int i = 0; i < n; ++i) { maskB[i] = kOne; maskC[i] = 0.0f; maskA[i] = kOne; }

    // --- patches (hf+0x18): inner -> maskB(MIN), outer -> maskC(MAX) ---
    int patchCount = hfp->paths._size;              // param_1[8]
    if (patchCount > 0) {
        RawArray<TerrainPathPoint>* patchArr = hfp->paths._data;  // param_1[6]
        for (int pi = 0; pi < patchCount; ++pi) {
            RawArray<TerrainPathPoint>* patch = &patchArr[pi];      // {recs, count}
            const TerrainPathPoint* recs = patch->data;
            int      recCnt = patch->size;
            for (int ri = 0; ri < recCnt; ++ri) {
                const TerrainPathPoint& r = recs[ri];   // stride 0x1c
                // Retail record layout: {x@0, y@4, strength@0x08, innerCutoff@0x0c,
                // innerBase@0x10, outerCutoff@0x14, outerBase@0x18}. e910 reads it
                // identically to ec50 (verified 0x1400ce910 / 0x1400cec50).
                float center[2]   = { r.x, r.y };
                float outerStr    = r.strength;       // +0x08  strength
                float innerCutoff = r.innerRadius;    // +0x0c  inner radius (cutoff)
                float innerBase   = r.innerFalloff;   // +0x10  inner falloff base
                float outerCutoff = r.outerRadius;    // +0x14  outer radius (cutoff)
                float outerBase   = r.outerFalloff;   // +0x18  outer falloff base
                wg_StampBrush_eee0(hf, center, innerCutoff, innerBase, kOne, 0, maskB);     // MIN -> maskB
                wg_StampBrush_eee0(hf, center, outerCutoff, outerBase, outerStr, 1, maskC); // MAX -> maskC
            }
        }
    }

    // --- mountain satellites (hf+0x38 listC): -> maskC(MAX), pow 2.25, str 2.0 ---
    int satCount = hfp->basinStamps._size;           // param_1[0x10]
    if (satCount > 0) {
        const TerrainStamp* satArr = hfp->basinStamps._data;  // param_1[0xe]
        for (int i = 0; i < satCount; ++i) {
            const TerrainStamp& s = satArr[i];
            float center[2] = { s.x, s.y };
            float radius    = s.radius;              // +0x08
            wg_StampBrush_eee0(hf, center, radius, kSatExp, kSatStr, 1, maskC);  // MAX -> maskC
        }
    }

    // --- river points (hf+0x28 listB): -> maskA(MIN), pow 1.75, str 1.0 ---
    int riverCount = hfp->riverPoints._size;          // param_1[0xc]
    if (riverCount > 0) {
        const TerrainStamp* rivArr = hfp->riverPoints._data;  // param_1[10]
        for (int i = 0; i < riverCount; ++i) {
            const TerrainStamp& rp = rivArr[i];
            float center[2] = { rp.x, rp.y };
            float radius    = rp.radius;             // +0x08
            wg_StampBrush_eee0(hf, center, radius, kRiverExp, kOne, 0, maskA);   // MIN -> maskA
        }
    }

    // --- COMBINE: h[i] = maskB[i] * h[i] * maskA[i] - maskC[i] ---
    float* heightmap = hfp->heightmap.data;   // param_1[2]
    if (heightmap) {
        for (int i = 0; i < n; ++i) {
            heightmap[i] = maskB[i] * heightmap[i] * maskA[i] - maskC[i];
        }
    }

    // teardown (reverse of init).
    System::MemoryAllocator::MemFree(maskA);
    System::MemoryAllocator::MemFree(maskC);
    System::MemoryAllocator::MemFree(maskB);
}
