// src/game/Game/worldgen_f390_ranges.cpp
// ============================================================================
// M5 5b worldgen — f390 MOUNTAIN-RANGES + SECONDARY-PATHS stages.
// ============================================================================
//
// Reimplements two of the feature stages that run inside the heightmap pipeline
// f390 (docs/history/notes/WORLDGEN_TERRAIN_BLUEPRINT.md §0), in the verified order:
//
//   e5d0  base perlin heightmap        (already reimpl'd in worldgen.cpp)
//   cd090 coastline ring
//   cd150 mountain ranges   <-- wg_f390_ranges  (THIS FILE)
//   ccf60 secondary paths   <-- wg_f390_paths   (THIS FILE)
//   ...
//
//   FUN_1400cd150  EmitMountainRanges    -> wg_f390_ranges
//   FUN_1400ccf60  EmitSecondaryPaths    -> wg_f390_paths
//   sub-workers (all static here):
//     FUN_1400245d0  AppendVec3   (0xC-stride array resize-and-fill)
//     FUN_1400cc850  TryPlacePath (build+validate a candidate polyline)
//     FUN_1400cc510  SubdivideSeg (recursive fractal midpoint displacement)
//     FUN_140023b70  ResizeVec1c  (0x1C-stride array resize)
//     FUN_140024460  DeepAppendAoA(array-of-arrays deep append)
//     FUN_140024ac0  (the deep copy: kSpec_WgPathVec's assign, worldgen_f390_coastline.cpp)
//
// ----------------------------------------------------------------------------
// HEIGHT EFFECT (these two stages do NOT touch the float heightmap directly):
//   Neither cd150 nor ccf60 writes heightfield+0x08. They only POPULATE the
//   scratch feature lists that the LATER stamp stages (e910/ec50) read to carve
//   the heightmap:
//     * cd150 pushes "satellite" + "center" points into the radial-stamp list
//       (+0x38) and one record per range into the water-feature list (+0x48).
//       CORRECTION (verified against retail 0x1400ceac2/aca and the eee0 call at
//       worldgen_f390_rivers.cpp): e910/ec50 stamp the +0x38 entries into maskC
//       with MAX, strength 2.0, exp 2.25 — and the final height is
//       `h = maskB*h*maskA - maskC`, i.e. maskC is SUBTRACTED. These CARVE
//       circular depressions; they do NOT raise terrain. The older "positive
//       radial bumps / RAISE terrain into mountain ranges" note here was wrong
//       and sent a reader looking for a sign bug that does not exist.
//       The +0x48 records are what b260 phase 2 reads as its tarn features.
//     * ccf60 pushes validated polylines (river/coast branch paths) into the
//       array-of-arrays at hf+0x18 (via cc850's deep-append at param_1+0x18).
//       The river network (cd460) + stamp stages later carve these DOWN toward
//       sea level (maskA, a MIN mask, exp 1.33) — channels/valleys.
//   So in isolation these stages change NO height values; their magnitude shows
//   up only after e910/ec50 run. Recorded magnitudes (for the orchestrator):
//     - mountain center "radius" field = bandX - radiusMin  (in grid units)
//     - mountain satellite "radius" field = RandFloat*(radiusMin-radiusMax)+radiusMax
//     - both are CARVE radii: e910/ec50 stamp them into maskC (MAX, str 2.0,
//       exp 2.25) and SUBTRACT — round lake pits, not peaks (see CORRECTION above).
//     - secondary paths are zero-height polylines; they become carve channels.
// ----------------------------------------------------------------------------
//
// All offsets verified against live Ghidra disassembly of Application-x64.exe.

#include "Game/TerrainHeightfield.h"   // TerrainHeightfield, the stage declarations
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "math/Math.h"
#include "collection/Collection.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)

// The heightfield record and its size row are TerrainHeightfield / TerrainSizeRow
// (TerrainHeightfield.h): gridSize +0x00, paths +0x18 (the polylines cc850
// deep-appends), basinStamps +0x38 (radial carve stamps -> maskC, SUBTRACTED),
// waterFeatures +0x48 (b260 phase-2 tarn features), random +0x58, sizeRow +0xA28.

// ============================================================
// .rdata float constants (verified via read_memory; address=value).
// ============================================================
static constexpr float kHalf       = 0.5f;     // 0x140172114  amplitude seed / subdivide half
static constexpr float kEighth      = 0.125f;  // 0x1401720ec  cc850 closed-ring midpoint jitter
static constexpr float kQuarter     = 0.25f;   // 0x140172100  cc510 endpoint amplitude (cc850 param_6 seed)
static constexpr float kMidAmp      = 0.66602f;// 0x14017211c  cc510 midpoint displacement amplitude factor
static constexpr float kNegHalf     = -0.5f;   // 0x140172278  cc850 ring first-point Y
// ccf60 path params (constants, address=value):
static constexpr float kPathP46     = 1.25f;   // 0x140172158  ccf60 param_6
static constexpr float kPathP47     = 1.5f;    // 0x140172160  ccf60 param_4 & param_7
static constexpr float kPathP5      = 8.0f;    // 0x1401721a8  ccf60 param_5
static constexpr float kPathP8      = 16.0f;   // 0x1401721c0  ccf60 param_8
// float-negate sign mask: 0x1401722c0 = 0x80000000
static constexpr uint32_t kSignMask = 0x80000000u;
// g_fUVScale (0x14017214c) = 1.0f — intersection-parameter (0,1) bound.
static constexpr float kOne = 1.0f;

// ============================================================
// Vector2::Normalize(this)  (in-place use of the by-value member; slot 0x1401526b0)
// ============================================================
// The ResolveExport below looked up "?Normalize@Vector2@Math@@QEAAAEAV12@XZ"
// (an in-place, non-const overload) which does NOT exist in Runtime-x64.dll's
// export table (verified with pefile 2026-09-06) — the DLL has no in-place
// Vector2::Normalize at all, only a by-value one returning through a hidden
// RDX slot (see src/engine/math/Math.h's ABI note):
//   ?Normalize@Vector2@Math@@QEBA?AV12@XZ   retail 0x18006c600
// This adapter calls the real member and copies its result back into `self` so
// the in-place call sites below (vec2_normalize2) don't change (B3R: called by
// name; the pointer cache and its init went).
static Math::Vector2* vec2_normalize_real(Math::Vector2* self) {
    Math::Vector2 out = self->Normalize();
    *self = out;
    return self;
}


// float negate matching the exe's `(uint)x ^ 0x80000000`.
static inline float fneg(float x) {
    uint32_t u; __builtin_memcpy(&u, &x, 4); u ^= kSignMask;
    float r; __builtin_memcpy(&r, &u, 4); return r;
}
// Vector2::Normalize in place.
static inline void vec2_normalize2(Math::Vector2* v) {
    vec2_normalize_real(v);
}
static inline float rftol(System::Random* rng) {
    return rng->RandFloatTol();
}
static inline float rfloat(System::Random* rng) {
    return rng->RandFloat();
}

// ============================================================
// FUN_1400245d0  @0x1400245d0  —  AppendVec3 (0xC-stride resize-and-fill).
// Resizes the {data,count,cap} header at `hdr` to `newCount` (grow ×3 on the
// count+1 fast path), copying any tail-overrun slots from `src` (a 12-byte
// {float x,float y,float z} record). Callers always pass newCount = count+1 and
// src = the element to push, so the net effect is push_back(src). The original's
// 4-wide unrolled copy is purely a perf optimization of this element-wise fill.
//
// ENDGAME B.3.2 step 2: now the 3-argument `Collection::Array<WgVec3Rec>::SetSize`
// (src is the fill). Trait-derived row, and it IS trait-equivalent: the element
// is three floats, `assign` a 12-byte copy (byteCopy == is_trivially_copyable),
// no destroy, and the 2-argument form's `init` is never reached by a fill call.
// Compared arm for arm against rtcoll::SetSizeCore: exact-equal early return,
// `_size` first, treble-on-single-append-from-non-empty, cap*0xC at align 4,
// prefix copied only when the block moved, fill for every index at or past the
// old count, old block freed only when it moved. The only dropped line was the
// `if (!data) return` out-of-memory bail.
// The record is TerrainStamp {x, y, radius}; the lists are the heightfield's
// basinStamps (+0x38) and waterFeatures (+0x48).
// ============================================================
static void AppendVec3(Collection::Array<TerrainStamp>* list, int newCount, const TerrainStamp& src) {
    list->SetSize(newCount, src);
}

// ============================================================
// FUN_140024460  @0x140024460  —  DeepAppendAoA: append `srcVec` (a 0x1C-stride
// {data,count,cap} header) to the heightfield's path list, resizing to
// newCount = count + 1 and deep-copying srcVec (0x140024ac0) into the new tail.
// The list is the heightfield's TerrainPathList (TerrainHeightfield.h), over
// the ONE row retail has for it, kSpec_WgPathVec (worldgen_f390_coastline.cpp:
// SetSizeCore + the 0x140024ac0 deep copy). B.4 Z CODEA: this file's twin row
// kSpec_RgPathVec and its CopyVec1c copy of 0x140024ac0 are gone.
// ============================================================
static void DeepAppendAoA(TerrainPathList* paths, int newCount, const void* srcVec) {
    paths->SetSizeFill(newCount, srcVec);
}

// ============================================================
// FUN_140023b70  @0x140023b70  —  ResizeVec1c: resize a 0x1C-stride
// {data,count,cap} vector to `newCount` (grow ×3 on count+1), copying retained
// elements and zeroing the new tail. cc850 calls this then overwrites the tail.
//
// ENDGAME B.3.2 step 2: now `Collection::Array<>::SetSize` over this site's own
// zeroing 0x1C-byte element (the closing pass collapsed what was a hand-derived
// `kSpec_WgPathPointZeroInit` row in worldgen_f390_coastline.cpp onto the
// trait-derived form — the row was a whole-stride byte copy, a whole-stride zero
// tail, no destroy and no deliberately-untouched extent, which is precisely what
// `rtcoll::SpecFor<Bytes<0x1C,4>>()` (the old bridge element) derives).
// Compared arm for arm against rtcoll::SetSizeCore and the retail body
// 0x140023b70 (re-decompiled 2026-09-13): every structural arm matched.
//
// The new-tail arm is THIS SITE'S, not retail's and not the coastline twin's.
// One retail body has two transcriptions in this tree: this one zeroes all 0x1C
// bytes of a new record, coastline's zeroes none, and retail's own arm is a
// single QWORD store. Both of ours are believed unobservable — `pts` is a fresh
// local here, so every record is new, and the loop right after the resize writes
// [8,0x1C) of every record before anything reads them — but this refactor's
// contract is no behaviour change in a deterministic world build, so each site
// keeps its own arm and the reconciliation onto retail's is left as a separate,
// separately-verified change. Coastline's arm is `kSpec_WgPathPointNoInit`,
// which stays explicit because "leave the whole stride as found" is a probe
// contract; this one is an element type.
// ============================================================
// B.4: the element is TerrainPathPoint. Its row is the one
// `SpecFor<Bytes<0x1C,4>>()` (the old bridge element) derived (whole-stride byte copy, the new
// tail value-initialised = zeroed, no destroy), spelled for the named record:
// TerrainPathPoint is trivially default-constructible, so its own trait-derived
// row would leave the tail untouched (coastline's arm), not this site's.
static const rtcoll::ArraySpec kSpec_RgPathPointZeroInit = {  // exe 0x140023b70, ranges arm
    "Array<PathPoint> (tail zeroed)", sizeof(TerrainPathPoint), alignof(TerrainPathPoint),
    &rtcoll::TAssign<TerrainPathPoint>, &rtcoll::TInit<TerrainPathPoint>, nullptr, /*byteCopy*/true,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0, 0 };
using WgPathPointArray = Collection::Array<TerrainPathPoint, Collection::DefaultAllocator,
                                           Collection::ExplicitSpec<&kSpec_RgPathPointZeroInit>>;

static void ResizeVec1c(WgPathPointArray* pts, int newCount) {
    pts->SetSize(newCount);
}

// ============================================================
// FUN_1400cc510  @0x1400cc510  —  SubdivideSeg: recursive fractal midpoint
// displacement of the polyline in *vec between indices [param3, param4] using a
// half-plane reject sampler.
//
//   pts are 0x1C-stride; the leading 8 bytes of each = {float x, float y}.
//   a = pts[param3], b = pts[param4]; mid = (a+b)/2; dir = (b-a).
//   normal = normalize( (-dir.y, dir.x) ).                 [perpendicular]
//   limit = -param5  (the half-plane bound).
//   Repeatedly draw t = RandFloatTol()*param6*0.66602 ; candidate = mid+normal*t,
//   until the candidate passes the reject test (lies within the [limit, param5]
//   band and the two edge directions from a/b are not back-facing — dot>0 reject).
//   Store candidate at the midpoint index, then recurse on both halves with the
//   displacement amplitude halved (param6*0.5). Terminates when span <= 1.
// ============================================================
static void SubdivideSeg(TerrainHeightfield* hf, WgPathPointArray* vec, int param_3, int param_4,
                         float param_5, float param_6) {
    int span = param_4 - param_3;
    if (span <= 1) return;

    System::Random* rng = &hf->random;
    TerrainPathPoint* base = vec->_data;
    TerrainPathPoint* a = &base[param_3];   // endpoint A {x,y}
    TerrainPathPoint* b = &base[param_4];   // endpoint B {x,y}
    float ax = a->x, ay = a->y;
    float bx = b->x, by = b->y;
    float dirX = bx - ax;                 // local_120
    float dirY = by - ay;                 // local_11c
    float midX = (bx + ax) * kHalf;       // fVar11
    float midY = (by + ay) * kHalf;       // fVar12

    // half-plane normal: (-dir.y, dir.x), normalized.  (local_128 = dir.y ^ sign)
    Math::Vector2 perp(fneg(dirY), dirX);
    vec2_normalize2(&perp);

    float limit = fneg(param_5);           // fVar9 = -param5

    float candX = 0.0f, candY = 0.0f;
    do {
        float t = rftol(rng) * param_6 * kMidAmp;   // displacement magnitude
        candX = perp.x * t + midX;         // fVar8
        candY = perp.y * t + midY;         // fVar7

        Math::Vector2 dB(bx - candX, by - candY);   // local_f8/local_f4
        Math::Vector2 dA(ax - candX, ay - candY);   // local_f0/local_ec
        vec2_normalize2(&dB);
        vec2_normalize2(&dA);
        float facing = dA.x*dB.x + dA.y*dB.y;        // dot (pfVar4 . pfVar5)

        // reject conditions transcribed verbatim:
        //   while (cand.x < limit);
        //   while ( (cand.x<=limit && cand.y<limit) || param5<cand.x
        //           || (param5<=cand.x && param5<cand.y) || facing>0 );
        bool retry = false;
        if (candX < limit) retry = true;
        else if (((candX <= limit) && (candY < limit)) ||
                 (param_5 < candX) ||
                 ((param_5 <= candX) && (param_5 < candY)) ||
                 (0.0f < facing)) retry = true;
        if (!retry) break;   // unbounded, as retail 0x1400cc510
    } while (true);

    int mid = (span >> 1) + param_3;
    TerrainPathPoint* m = &base[mid];
    m->x = candX; m->y = candY;

    float halfAmp = param_6 * kHalf;
    SubdivideSeg(hf, vec, param_3, mid, param_5, halfAmp);
    SubdivideSeg(hf, vec, mid, param_4, param_5, halfAmp);
}

// ============================================================
// FUN_1400cc850  @0x1400cc850  —  TryPlacePath: build a candidate polyline of
// `param_2` points (closed ring if branchSel<0, else a branch off the existing
// path[branchSel]) via SubdivideSeg, validate it has no self-intersections and
// no crossings with existing paths (segment intersection params both in (0,1)),
// and on success deep-append it to the heightfield's paths (+0x18).
// Returns 1 on success (placed), 0 on reject.
// param_4..8 are the five per-point brush values (stored verbatim into
// TerrainPathPoint +0x08..+0x18).
// ============================================================
static int TryPlacePath(TerrainHeightfield* hf, int param_2 /*nptCount*/, float param_3 /*scale*/,
                        float param_4, float param_5, float param_6,
                        float param_7, float param_8, float param_9 /*branchSel*/) {
    System::Random* rng = &hf->random;

    // ENDGAME B.3.2 step 2: a real Collection::Array with RAII; the manual free
    // at the single exit below is the destructor now (the early `return 0` on
    // a null buffer had nothing to free either way).
    WgPathPointArray pts;
    ResizeVec1c(&pts, param_2);
    if (!pts._data) return 0;

    TerrainPathPoint* pd = pts._data;
    // seed band params into bytes [8..0x1c) of every element
    for (int i = 0; i < param_2; ++i) {
        TerrainPathPoint* e = &pd[i];
        e->strength     = param_4;
        e->innerRadius  = param_5;
        e->innerFalloff = param_6;
        e->outerRadius  = param_7;
        e->outerFalloff = param_8;
    }

    int lo, hi;
    float subAmp, subAmp2;   // cc510 param_5 (fVar24), param_6 (fVar22)

    if ((int)param_9 < 0) {
        // ---- CLOSED RING ----
        int last = pts._size - 1;
        TerrainPathPoint* p0 = &pd[0];
        p0->x = 0.0f; p0->y = param_3 * kNegHalf;     // first: {0, scale*-0.5}

        uint32_t r = rng->Rand(3);
        TerrainPathPoint* pl = &pd[last];
        if (r == 0) {
            pl->x = 0.0f; pl->y = param_3 * kHalf;    // {0, scale*0.5}
        } else {
            float v = (r == 1) ? (param_3 * kHalf)    // {scale*0.5, 0}
                               : (param_3 * kNegHalf);// r==2: {scale*-0.5, 0}
            pl->x = v; pl->y = 0.0f;
        }
        // midpoint jitter: pts[count/2] = (RandFloatTol*scale*0.125, RandFloatTol*scale*0.125)
        float jy = rftol(rng) * param_3 * kEighth;
        float jx = rftol(rng) * param_3 * kEighth;
        TerrainPathPoint* pm = &pd[pts._size >> 1];
        pm->x = jx; pm->y = jy;

        subAmp2 = param_3 * kQuarter;                 // fVar22
        subAmp  = param_3 * kHalf;                    // fVar24
        SubdivideSeg(hf, &pts, 0, pts._size >> 1, subAmp, subAmp2);
        lo = pts._size >> 1;
        hi = pts._size - 1;
    } else {
        // ---- BRANCH off existing path[branchSel] ----
        RawArray<TerrainPathPoint>* selHdr = &hf->paths._data[(int)param_9];
        int selCount = selHdr->size;
        int q = (selCount + ((selCount >> 31) & 3)) >> 2;          // selCount/4 (toward 0)
        int startIdx = rng->Rand(q, q * 3);
        const TerrainPathPoint* sel = &selHdr->data[startIdx];
        pd[0].x = sel->x;                             // first pt = selected pt {x,y}
        pd[0].y = sel->y;                             // (retail: one qword copy; H7)

        float jitter = rftol(rng) * param_3 * kHalf;  // fVar17
        int last = pts._size - 1;
        TerrainPathPoint* pl = &pd[last];
        uint32_t r = rng->Rand(4);
        float half = param_3 * kHalf;                 // fVar24 = scale*0.5
        // CONCAT44(hi,lo) -> x=lo, y=hi
        if (r == 0)      { pl->x = jitter;            pl->y = half;             } // {jitter, scale*0.5}
        else if (r == 1) { pl->x = half;              pl->y = jitter;           } // {scale*0.5, jitter}
        else if (r == 2) { pl->x = param_3*kNegHalf;  pl->y = jitter;           } // {scale*-0.5, jitter}
        else /* r==3 */  { pl->x = jitter;            pl->y = param_3*kNegHalf; } // {jitter, scale*-0.5}

        subAmp2 = param_3 * kQuarter;
        subAmp  = param_3 * kHalf;
        lo = 0;
        hi = pts._size - 1;
    }
    SubdivideSeg(hf, &pts, lo, hi, subAmp, subAmp2);

    // ---- VALIDATION ----
    // Then: self-intersection + cross-path intersection; reject if both
    // intersection parameters are strictly in (0,1).
    int n = pts._size;
    const TerrainPathPoint* P = pts._data;
    bool reject = false;

    // Nearest-neighbour adjacency pre-pass (retail FUN_1400cc850 first loop, the
    // DAT_140172268=1e6 running-min block). For each vertex i, find the closest
    // OTHER vertex by LengthSquared and reject the whole candidate unless that
    // closest index is i's immediate neighbour (i-1 or i+1). Draws NO RNG, so it
    // does not perturb the MT stream — it only rejects self-approaching polylines
    // that retail retries away (prevents merged/over-wide troughs & spurious pits).
    {
        int prevPick = -1;
        for (int i = 0; i < n && !reject; ++i) {
            int   bestIdx = -2;
            float best    = 1e6f;                     // DAT_140172268
            const TerrainPathPoint* pa = &P[i];
            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                const TerrainPathPoint* pb = &P[j];
                float dvx = pa->x - pb->x;
                float dvy = pa->y - pb->y;
                float lensq = dvx * dvx + dvy * dvy;  // Math::Vector2::LengthSquared
                if (lensq < best) { best = lensq; bestIdx = j; }
            }
            if (bestIdx != prevPick + 2 && bestIdx != prevPick) reject = true;
            prevPick++;
        }
    }

    for (int i = 0; i < n - 1 && !reject; ++i) {
        const TerrainPathPoint* a0 = &P[i];
        const TerrainPathPoint* a1 = &P[i + 1];
        float ax = a0->x, ay = a0->y;
        float dAx = a1->x - ax, dAy = a1->y - ay;

        // self vs other (non-adjacent) segments of this path
        for (int j = 0; j < n - 1 && !reject; ++j) {
            if (j == i || j + 1 == i || j == i + 1 || j + 1 == i + 1) continue;
            const TerrainPathPoint* b0 = &P[j];
            const TerrainPathPoint* b1 = &P[j + 1];
            float dBx = b1->x - b0->x, dBy = b1->y - b0->y;
            // denom/t/u transcribed byte-faithfully from FUN_1400cc850's self-
            // intersection block (the puVar12 loop): denom = dAx*dBy - dBx*dAy,
            // t = (dBx*wy - wx*dBy)/denom, u = (dAx*wy - wx*dAy)/denom. The prior
            // port flipped denom's sign AND used a garbage u numerator
            // (dAy*wy - wx*dBy), which landed u in (0,1) for most segment pairs and
            // rejected nearly every candidate -> ccf60 placed 0 of numSecondary.
            float denom = dAx * dBy - dBx * dAy;       // fVar18
            if (denom != 0.0f) {
                float wx = ax - b0->x, wy = ay - b0->y;
                float t = (dBx * wy - wx * dBy) / denom;   // fVar20
                float u = (dAx * wy - wx * dAy) / denom;   // fVar18
                if (t > 0.0f && t < kOne && u > 0.0f && u < kOne) { reject = true; }
            }
        }
        if (reject) break;

        // vs existing paths (hf->paths)
        int pathCount = hf->paths._size;
        if (pathCount > 0) {
            const RawArray<TerrainPathPoint>* aoa = hf->paths._data;
            for (int pi = 0; pi < pathCount && !reject; ++pi) {
                const RawArray<TerrainPathPoint>* hdr = &aoa[pi];
                int pc = hdr->size;
                const TerrainPathPoint* pdat = hdr->data;
                for (int k = 0; k < pc - 1 && !reject; ++k) {
                    const TerrainPathPoint* q0 = &pdat[k];
                    const TerrainPathPoint* q1 = &pdat[k + 1];
                    float dBx = q1->x - q0->x, dBy = q1->y - q0->y;
                    // Same canonical crossing test as FUN_1400cc850's puVar13 loop
                    // (cross-path): denom = dAx*dBy - dBx*dAy, t = (dBx*wy - wx*dBy)/denom,
                    // u = (dAx*wy - wx*dAy)/denom. (Was sign-flipped denom + wrong u.)
                    float denom = dAx * dBy - dBx * dAy;   // fVar20
                    if (denom != 0.0f) {
                        float wx = ax - q0->x, wy = ay - q0->y;
                        float t = (dBx * wy - wx * dBy) / denom;   // fVar21
                        float u = (dAx * wy - wx * dAy) / denom;   // fVar20
                        if (t > 0.0f && t < kOne && u > 0.0f && u < kOne) { reject = true; }
                    }
                }
            }
        }
    }

    int result;
    if (reject) {
        result = 0;
    } else {
        DeepAppendAoA(&hf->paths, hf->paths._size + 1, &pts);
        result = 1;
    }
    return result;
}

// ============================================================
// FUN_1400cd150  @0x1400cd150  —  wg_f390_ranges (EmitMountainRanges).
//
//   rangeCount = minRangeCount + Rand( maxRangeCount - minRangeCount + 1 )
//   bandX      = minBandX + Rand( maxBandX - minBandX )
//   yExtent    = (dim/2 - 2) - bandX            (vertical span from map center)
//   for each range (rangeCount):
//     dx = RandFloatTol() * yExtent             (range center jitter, x)
//     dy = RandFloatTol() * yExtent             (range center jitter, y)
//     amp = 0.5                                  (fVar19, RESET to 0.5 per satellite)
//     for each satellite (satellitesPerRange):
//       radius = RandFloat()*(radiusMin - radiusMax) + radiusMax
//       g      = RandFloat()
//       _t     = RandFloatTol()                  (feeds the normalize input pair)
//       amp    = g*0.5 + 0.5                     (retail reloads 0.5 each iteration)
//       n = normalize( RandFloatTol(), RandFloatTol() )   (random unit dir)
//       reach = bandX - radius
//       satX = reach * n.x * amp + dy
//       satY = reach * n.y * amp + dx
//       listC.push_back( { satX, satY, radius } )            // +0x38
//     listC.push_back(   { dy, dx, bandX - radiusMin } )     // +0x38  (range center)
//     detailRecs.push_back( { dy, dx, bandX } )              // +0x48  (per-range record)
//
//   HEIGHT EFFECT: writes NO heights. Populates listC/detailRecs that the later
//   stamp stages (e910/ec50) read to CARVE round lake pits (maskC MAX, exp 2.25,
//   strength 2.0, SUBTRACTED). Carve radius = the "radius" fields above.
// ============================================================
void wg_f390_ranges(TerrainHeightfield* hf) {
    System::Random* rng = &hf->random;

    TerrainSizeRow* sizeRow = hf->sizeRow;
    if (!sizeRow) return;
    int dim          = sizeRow->mapSize;
    int minRangeCnt  = sizeRow->minRangeCount;              // +0x28
    int maxRangeCnt  = sizeRow->maxRangeCount;              // +0x2c
    int minBandX     = sizeRow->minRangeBand;               // +0x30
    int maxBandX     = sizeRow->maxRangeBand;               // +0x34
    int satPerRange  = sizeRow->basinsPerRange;             // +0x38
    int radiusMax    = sizeRow->basinRadiusMax;             // +0x3c
    int radiusMin    = sizeRow->basinRadiusMin;             // +0x40

    uint32_t rRange = rng->Rand((uint32_t)(maxRangeCnt - minRangeCnt + 1));
    uint32_t rBand  = rng->Rand((uint32_t)(maxBandX - minBandX));
    int bandX       = (int)rBand + minBandX;                // iVar10
    int rangeCount  = (int)rRange + minRangeCnt;            // uVar8 + iVar2
    if (rangeCount <= 0) return;

    float fBandX  = (float)bandX;                           // fVar21
    float yExtent = (float)((dim >> 1) - 2) - fBandX;       // fVar20

    for (int r = 0; r < rangeCount; ++r) {
        float dx = rftol(rng) * yExtent;                    // fVar14
        float dy = rftol(rng) * yExtent;                    // fVar15

        for (int s = 0; s < satPerRange; ++s) {
            float radius = rfloat(rng) * (float)(radiusMin - radiusMax) + (float)radiusMax;  // fVar18
            float g      = rfloat(rng);                     // fVar16
            float t3     = rftol(rng);                      // fVar17 (3rd RandFloatTol)
            // fVar19 = fVar16*fVar19 + fVar19, but retail RELOADS fVar19 = DAT_140172114
            // (0.5) at the tail of EVERY satellite iteration, so amp is per-satellite in
            // [0.5, 1.0] — never compounding. reach*amp <= bandX keeps every satellite
            // inside the range's bandX radius (= the +0x48 water-feature radius b260
            // phase 2 meshes).
            float amp = g * kHalf + kHalf;

            // random unit direction. The original packs {local_res18, fVar17}:
            //   local_res18 = RandFloatTol() (drawn AFTER fVar17), local_res1c = fVar17.
            //   {x = local_res18 (4th RandFloatTol), y = local_res1c = fVar17}
            Math::Vector2 dir(rftol(rng), t3);
            vec2_normalize2(&dir);

            float reach = fBandX - radius;                  // fVar18 = fVar21 - radius
            float satX = reach * dir.x * amp + dy;          // local_res20
            float satY = reach * dir.y * amp + dx;          // fStackX_24
            TerrainStamp rec = { satX, satY, radius };
            AppendVec3(&hf->basinStamps, hf->basinStamps._size + 1, rec);
        }

        // range center -> radial carve stamps (+0x38): {dy, dx, bandX - radiusMin}
        TerrainStamp center = { dy, dx, (float)(bandX - radiusMin) };
        AppendVec3(&hf->basinStamps, hf->basinStamps._size + 1, center);

        // per-range record -> b260 phase-2 tarn features (+0x48): {dy, dx, bandX}
        TerrainStamp detail = { dy, dx, fBandX };
        AppendVec3(&hf->waterFeatures, hf->waterFeatures._size + 1, detail);
    }
}

// ============================================================
// FUN_1400ccf60  @0x1400ccf60  —  wg_f390_paths (EmitSecondaryPaths).
//
//   numSecondaryPaths = sizeRow[+0x20]
//   for each of numSecondaryPaths:
//     retry up to 1000 times:
//       branchSel = (float)Rand( pathCount )   ; pathCount = hf+0x20 (live count)
//       ok = TryPlacePath(hf, dim+1, (float)dim, 1.5, 8.0, 1.25, 1.5, 16.0, branchSel)
//       if ok: break
//
//   params (address=value): param_4=1.5(0x140172160), param_5=8.0(0x1401721a8),
//   param_6=1.25(0x140172158), param_7=1.5(0x140172160), param_8=16.0(0x1401721c0).
//   branchSel >= 0 here (Rand returns >=0), so every secondary path BRANCHES off
//   an existing path (tributaries of the coastline/river ring placed by cd090).
//
//   HEIGHT EFFECT: writes NO heights. Appends validated polylines to hf+0x18,
//   later carved DOWN toward sea level by the river/stamp stages (maskA, MIN
//   mask) — secondary channels/valleys branching off the main paths.
// ============================================================
void wg_f390_paths(TerrainHeightfield* hf) {
    TerrainSizeRow* sizeRow = hf->sizeRow;
    if (!sizeRow) return;
    int numSecondary = sizeRow->secondaryPathCount;
    if (numSecondary <= 0) return;

    System::Random* rng = &hf->random;
    int dim = hf->gridSize;   // *param_1 (== mapDim*2, set by f390)

    // the path params cc850 stores verbatim into the per-point brush fields.
    const float P4 = kPathP47;   // 1.5  (0x140172160)
    const float P5 = kPathP5;    // 8.0  (0x1401721a8)
    const float P6 = kPathP46;   // 1.25 (0x140172158)
    const float P7 = kPathP47;   // 1.5  (0x140172160)
    const float P8 = kPathP8;    // 16.0 (0x1401721c0)

    for (int i = 0; i < numSecondary; ++i) {
        for (int attempt = 0; attempt < 1000; ++attempt) {
            int pathCount = hf->paths._size;
            // No test of the count (0x1400ccf60): the coastline stage (0x1400cd090) leaves at
            // least one path.
            float branchSel = (float)rng->Rand((uint32_t)pathCount);
            int ok = TryPlacePath(hf, dim + 1, (float)dim, P4, P5, P6, P7, P8, branchSel);
            if (ok) break;
        }
        // re-read the loop bound each outer iteration (matches the original).
        numSecondary = sizeRow->secondaryPathCount;
    }
}
