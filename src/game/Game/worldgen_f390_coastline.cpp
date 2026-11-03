// src/game/Game/worldgen_f390_coastline.cpp
// ============================================================================
// f390 worldgen pipeline — COASTLINE-RING stage.
// ============================================================================
//
//   FUN_1400cd090  (EmitCoastlineRing)   -> wg_f390_coastline   [stage entry]
//     -> FUN_1400cc850 (TryPlacePath)    -> coastline_TryPlacePath_cc850
//        -> FUN_140023b70 (resize 0x1c)  -> coastline_Resize_Stride1C_23b70
//        -> FUN_1400cc510 (Subdivide)    -> coastline_Subdivide_cc510
//        -> FUN_140024460 (AoA append)   -> coastline_AppendPath_24460
//           -> FUN_140024ac0 (deep copy) -> coastline_DeepCopyPath_24ac0
//
// PURPOSE OF THIS STAGE (verified from disassembly):
//   cd090 builds ONE closed coastline ring polyline (a 2D point loop in the
//   horizontal plane, stored as a polygon in the heightfield "patchList" vector
//   at hf+0x18). It is a PATH GENERATOR: it produces (x,y) polyline geometry +
//   per-vertex {param_4..param_8} attributes. It does NOT touch the float
//   heightmap at hf+0x08 at all (no reads, no writes). The ring it emits is
//   consumed LATER by the feature-stamp stages (e910 / ec50, via the patchList),
//   which carve/raise the heightmap along it. So the HEIGHT EFFECT of THIS stage
//   in isolation is ZERO (see header RETURN item 3).
//
//   The whole stage is a rejection-sampling loop: keep generating a candidate
//   closed ring (fractal midpoint-displaced loop) until one is produced that has
//   no self-intersections and no crossings with previously-placed paths, then
//   deep-append it into the patchList. cd090 loops cc850 until the patchList
//   count (hf+0x20) becomes nonzero — i.e. until exactly one ring lands.
//
// This file ADDS a self-contained reimpl of the coastline stage. All sub-workers
// are file-local `static`. Shared engine functions go through fn_* pointers.
//
// Field offsets verified against Ghidra disassembly of Application-x64.exe.

#include "Game/TerrainHeightfield.h"   // TerrainHeightfield, the stage declarations
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "math/Math.h"
#include "collection/Collection.h"
#include <cstdint>
#include <cmath>
#include <cstdio>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)

// The heightfield record is TerrainHeightfield (TerrainHeightfield.h): gridSize
// +0x00 (read by cd090 as *param_1), paths +0x18 (the polyline list; its count
// +0x20 is cd090's loop guard), random +0x58.

// ============================================================
// PathPoint record: 0x1C bytes.
//   +0x00 float x        (point position X)
//   +0x04 float y        (point position Y)
//   +0x08 float a4       (param_4 attribute — propagated to every vertex)
//   +0x0C float a5       (param_5)
//   +0x10 float a6       (param_6)
//   +0x14 float a7       (param_7)
//   +0x18 float a8       (param_8)
// The patchList element is a vector<PathPoint> = {void* data; int count; int cap}
// (16 bytes per outer element).
// ============================================================
static constexpr int PATHPT_STRIDE = 0x1c;

// ============================================================
// .rdata float constants (all verified via read_memory; address = value).
// ============================================================
// cd090 (the ring-stage args):
static constexpr float kRing_a4_2_0   = 2.0f;   // 0x140172174 = 0x40000000
static constexpr float kRing_a5_10_0  = 10.0f;  // 0x1401721b0 = 0x41200000
static constexpr float kRing_a6_1_25  = 1.25f;  // 0x140172158 = 0x3fa00000
static constexpr float kRing_a78_4_0  = 4.0f;   // 0x140172194 = 0x40800000
// cc850 / cc510:
static constexpr float kHalf          = 0.5f;       // 0x140172114 = 0x3f000000
static constexpr float kNegHalf       = -0.5f;      // 0x140172278 = 0xbf000000
static constexpr float kJitter_0_125  = 0.125f;     // 0x1401720ec = 0x3e000000  (closed-ring midpoint jitter amp)
static constexpr float kQuarter       = 0.25f;      // 0x140172100 = 0x3e800000  (subdiv coord amplitude seed)
static constexpr float kBigDist       = 1000000.0f; // 0x140172268 = 0x49742400  (nearest-neighbour init = INF)
static constexpr float kMidAmp_0_666  = 0.66602f;   // 0x14017211c = 0x3f2a7efa  (midpoint displacement amp factor)
static constexpr float kOne           = 1.0f;       // 0x14017214c = 0x3f800000  (g_fUVScale; intersection (0,1) bound)
static constexpr uint32_t kSignMask   = 0x80000000u;// 0x1401722c0  (float-negate XOR mask)

// ============================================================
// Late-bound Runtime resolves (not in engine_funcs.h yet).
//   System::Random::RandFloatTol() -> float in [-1,1]
//   System::Random::Rand(int max)  -> [0,max)
//   System::Random::Rand(int min,int max) -> [min,max)
//   Math::Vector2::Length()        const -> float
//   Math::Vector2::LengthSquared() const -> float
//   Math::Vector2::Normalize()     -> Vector2& (returns self ptr; normalizes in place)
// ============================================================
// The three ResolveExport calls below used to look up mangled names that DO
// NOT EXIST in Runtime-x64.dll's export table (verified with pefile 2026-09-06):
//   "?Length@Vector2@Math@@QEBAMXZ"        (assumed a plain-float return)
//   "?LengthSquared@Vector2@Math@@QEBAMXZ" (assumed a plain-float return)
//   "?Normalize@Vector2@Math@@QEAAAEAV12@XZ" (assumed an in-place, non-const overload)
// So the three lookups always resolved null and every call silently ran the
// fallback instead of the DLL. The real exports exist
// under different signatures (Length/LengthSquared return Math::Scalar via a
// hidden RDX return-slot pointer, per the ABI note in
// src/engine/math/Math.h; Normalize has NO in-place overload, only a
// by-value one). These adapters call the real members and preserve this file's
// existing call-site shape (float return / in-place self-mutation); they are
// called by name (B3R: the pointer cache and its init went).
static float coast_vec2len_real(const Math::Vector2* self) {
    return self->Length().v;
}
static float coast_vec2lensq_real(const Math::Vector2* self) {
    return self->LengthSquared().v;
}
static Math::Vector2* coast_vec2norm_real(Math::Vector2* self) {
    // Retail has no in-place Normalize, only the by-value one; copy back.
    Math::Vector2 out = self->Normalize();
    *self = out;
    return self;
}

// Helper: bit-cast negate (float XOR 0x80000000), matching the original's
// integer sign-flip (avoids -0.0 / signalling-NaN ABI surprises).
static inline float fnegate(float v) {
    uint32_t b; __builtin_memcpy(&b, &v, 4); b ^= kSignMask;
    float r; __builtin_memcpy(&r, &b, 4); return r;
}

// ============================================================
// FUN_140023b70 — resize a {void* data; int count; int cap} vector whose
// element stride is 0x1C bytes, copying existing records on grow.
// Exe addr: 0x140023b70.  (Same grow-3x idiom as the shared Stride04/16 helpers,
// but stride 0x1C is unique to PathPoint, so we keep a private copy.)
//
// Faithful transcription: on grow it MemAllocs cap*0x1C, copies the old [0,count)
// records (each 0x1C bytes = x/y + the 6 attribute dwords... 0x1C/4 = 7 dwords)
// forward, frees the old buffer. We simplify the decompiler's manually-unrolled
// 4-at-a-time copy into a straight per-record loop — byte-identical result.
//
// ENDGAME B.3.2 step 2: the hand-written grow below is now one
// `Collection::Array<>::SetSize` over the `kSpec_WgPathPoint` row. Compared arm
// for arm against rtcoll::SetSizeCore and against the retail body (0x140023b70,
// re-decompiled 2026-09-13): exact-equal early return, `_size` stored before the
// capacity test, treble-on-single-append-from-non-empty, cap*0x1C at align 4,
// copy the preserved prefix only when the block moved, free the old block only
// then, no destroy.
//
// THE NEW-TAIL ARM IS SITE-EXACT, NOT SHARED. One retail body (0x140023b70) has
// two transcriptions in this tree and they disagree about the new tail: THIS one
// leaves the record untouched, and the twin in worldgen_f390_ranges.cpp
// (ResizeVec1c) zeroes all 0x1C bytes. Retail's own arm is a single QWORD store
// of 0 (x,y zeroed, [8,0x1C) untouched), so neither transcription matches it.
// Both are believed unobservable — each caller writes [8,0x1C) of every record
// immediately after the resize and every x,y before any read — but "believed
// unobservable" is not a licence to change world generation inside a refactor
// whose contract is no behaviour change (B32_RULES / B31_RULES step 2). So each
// site keeps ITS OWN arm: `kSpec_WgPathPointNoInit` here, and for ranges the
// zeroing element `Collection::Array<Bytes<0x1C,4>>` (the old bridge element) it now spells
// itself (ENDGAME B.3.2 closing pass — that arm's row was a whole-stride byte
// copy plus a whole-stride zero tail with no destroy and no untouched extent,
// i.e. exactly what `SpecFor<Bytes<0x1C,4>>()` derives, so the shared row object
// this file used to export for it is gone). Collapsing the two arms onto
// retail's single-QWORD arm is a separate, separately-verified change.
// ============================================================
// The rows this stage instantiates. EXTERNAL linkage on purpose:
// worldgen.cpp (wg_FreePatchList, the hf+0x18 teardown) names the PathVec row
// through an `extern` declaration — the same row instance.
static void AssignWgPathPoint(uint8_t* d, const uint8_t* s) {
    __builtin_memcpy(d, s, PATHPT_STRIDE);          // 7 dwords, the whole stride
}
// This site's arm: NOTHING. `init == nullptr` is how an ArraySpec spells "the
// new tail keeps whatever the allocation contained", which is exactly what the
// hand-written loop here did (it only ran for i < oldCount).
// stays explicit: the whole stride is a deliberately-untouched extent
// (`init unwritten` = [0, 0x1C)) — the probe contract for "this site's new tail
// keeps whatever the allocation held", which no element type can state.
extern const rtcoll::ArraySpec kSpec_WgPathPointNoInit;
const rtcoll::ArraySpec kSpec_WgPathPointNoInit = {  // exe 0x140023b70, coastline arm
    "Array<PathPoint> (tail untouched)", PATHPT_STRIDE, 4,
    &AssignWgPathPoint, nullptr, nullptr, /*byteCopy*/true,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, PATHPT_STRIDE,
    /*seed zero       */ 0, 0 };
using WgPathPointArray = Collection::Array<TerrainPathPoint, Collection::DefaultAllocator,
                                           Collection::ExplicitSpec<&kSpec_WgPathPointNoInit>>;

static void coastline_Resize_Stride1C_23b70(WgPathPointArray* pts, int newCount) {
    pts->SetSize(newCount);
}

// ============================================================
// FUN_140024ac0 — deep-copy a vector<PathPoint> (0x1C-stride) into a destination
// vector header, freeing the destination's prior buffer first.
// Exe addr: 0x140024ac0.
//   dst, src are {void* data; int count; int cap} headers (16 bytes).
// ============================================================
static RawArray<TerrainPathPoint>* coastline_DeepCopyPath_24ac0(RawArray<TerrainPathPoint>* d,
                                                                const RawArray<TerrainPathPoint>* s) {
    if (d == s) return d;

    // 0x140024ae3..0x140024af7: unconditional MemFree of the old data, then ONE qword
    // zero of size AND capacity (+0x08), then data.
    System::MemoryAllocator::MemFree(d->data);
    d->size = 0;                    // count    (+0x08, low dword of the qword store)
    d->capacity = 0;                // capacity (+0x0c, high dword of the qword store)
    d->data = nullptr;              // data

    int n = s->size;                // src count
    d->size = n;
    if (n > 0) {
        d->capacity = n;            // cap
        TerrainPathPoint* nb = static_cast<TerrainPathPoint*>(
            System::MemoryAllocator::MemAlloc((uint64_t)n * PATHPT_STRIDE, 4));
        d->data = nb;

        const TerrainPathPoint* ss = s->data;
        for (int i = 0; i < n; ++i) {
            nb[i] = ss[i];
        }
    }
    return d;
}

// ============================================================
// FUN_140024460 — append one path (deep-copied) to the array-of-vectors at
// hf+0x18 (an outer vector whose elements are 16-byte vector<PathPoint> headers).
// Exe addr: 0x140024460.
//   outer  = hf+0x18  ({void* data; int count; int cap}, element = 16B header)
//   newLen = patchList.count + 1
//   srcVec = candidate path header (&local_e0 in cc850)
// On grow (cap*16, align 8) it deep-copies the surviving headers via 24ac0; the
// new tail slot is zero-initialized then 24ac0-filled from srcVec.
//
// ENDGAME B.3.2 step 2: the body is now `Collection::Array<>::SetSizeFill` (the
// 3-argument form: srcVec is the fill) over the `kSpec_WgPathVec` row. Compared
// arm for arm against rtcoll::SetSizeCore and the retail body (0x140024460,
// re-decompiled 2026-09-13): exact-equal early return, `_size` first, treble
// rule, cap*16 at align 8; per element `assign` = zero the header then
// 24ac0-deep-copy (prefix from the old block only when it moved, otherwise the
// fill); `destroy` = free the inner buffer and zero the header, applied to the
// truncated tail in place or to every old element before the old block is
// freed. Every arm matched, nothing changed. The row's `init` (the 2-argument
// form's new-tail arm) is not exercised by any site in this tree; it is the
// three zero stores of Collection::Array's constructor.
// ============================================================
// The row's callbacks receive the element as the bytes rtcoll::ArraySpec hands
// them; each takes ONE typed view of its element (LEFT: the callback signature
// is Collection.h's). The element is RawArray<TerrainPathPoint>; retail's qword
// zero of {size, capacity} is the two named stores (H7).
// (reached through rtcoll::AssignAs / DestroyAs, Collection.h: the row's byte view)
static void AssignWgPathVec(RawArray<TerrainPathPoint>* dst, const RawArray<TerrainPathPoint>* src) {
    dst->data = nullptr;                            // zero the dst header first
    dst->size = 0;
    dst->capacity = 0;
    coastline_DeepCopyPath_24ac0(dst, src);
}
static void DestroyWgPathVec(RawArray<TerrainPathPoint>* slots, int count) {
    for (int i = 0; i < count; ++i) {
        RawArray<TerrainPathPoint>* slot = &slots[i];
        rtcoll::MemFree(slot->data);                // null-guarded inside
        slot->data = nullptr;
        slot->size = 0;
        slot->capacity = 0;
    }
}
// stays explicit: `assign` is a deep copy (24ac0), not a byte copy, and
// `destroy` frees each element's inner buffer — byteCopy=false with a destroy is
// a combination C++ traits cannot produce.
extern const rtcoll::ArraySpec kSpec_WgPathVec;
const rtcoll::ArraySpec kSpec_WgPathVec = {         // exe 0x140024460 (grow), 0x1400175d0 (dtor)
    "Array<Array<PathPoint>>", 0x10, 8,
    &rtcoll::AssignAs<RawArray<TerrainPathPoint>, &AssignWgPathVec>, &rtcoll::TInit<RawArray<TerrainPathPoint>>,
    &rtcoll::DestroyAs<RawArray<TerrainPathPoint>, &DestroyWgPathVec>, /*byteCopy*/false,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0, 0 };
// (the list type over this row is TerrainPathList, TerrainHeightfield.h)
static void coastline_AppendPath_24460(TerrainPathList* outer, int newLen, const void* srcVec) {
    outer->SetSizeFill(newLen, srcVec);
}

// ============================================================
// FUN_1400cc510 — recursive midpoint subdivision (fractal displacement) of a
// path segment [lo,hi] in the point array, with a half-plane rejection sampler.
// Exe addr: 0x1400cc510.
//   self  = hf base   (RNG at +0x58)
//   arrPtr= &pointArrayPtr  (*arrPtr -> base of 0x1C-stride records)
//   lo,hi = endpoint indices in the array
//   limA  = param_5  (an axis bound; original: fVar9 = negate(limA))
//   limB  = param_6  (amplitude scale; halved each recursion)
//
// Algorithm (faithful):
//   span = hi - lo;  if (span <= 1) return.
//   p_lo = arr[lo], p_hi = arr[hi]
//   d   = p_hi - p_lo                                 (segment vector)
//   mid = (p_hi + p_lo) * 0.5                          (midpoint)
//   Length(d)   (original computes; result not numerically reused but called)
//   n = normalize( (-d.y, d.x) )                       (left normal, via XOR sign)
//   negLimA = negate(limA)
//   do {
//     do {
//       t = RandFloatTol(rng) * limB * 0.66602         (displacement magnitude)
//       cand = mid + n*t
//       e_hi = normalize(p_hi - cand);  e_lo = normalize(p_lo - cand)
//     } while (cand.x < negLimA);
//   } while ( reject:
//       (cand.x <= negLimA && cand.y < negLimA) || limA < cand.x
//       || (limA <= cand.x && limA < cand.y)
//       || dot(e_lo, e_hi) > 0 );
//   arr[(span>>1)+lo] = cand
//   recurse(lo, midIdx, limA, limB*0.5)
//   recurse(midIdx, hi, limA, limB*0.5)
// ============================================================
static void coastline_Subdivide_cc510(TerrainHeightfield* self, WgPathPointArray* arrPtr, int lo, int hi,
                                       float limA, float limB) {
    int span = hi - lo;
    if (span <= 1) return;

    // RNG is the hf-embedded System::Random at self+0x58 — NOT self (hf base). Passing
    // `self` made the MT core read/write a window shifted -0x58, and its twist clobbered
    // the real Random's index at hf+0x58 with a state value -> later Rand() did state[~1.7e9]
    // -> OOB crash. (cc850 / SubdivideSeg already pass +0x58; only this call was wrong.)
    System::Random* rng = &self->random;

    TerrainPathPoint* base = arrPtr->_data;
    TerrainPathPoint* pLo = &base[lo];
    TerrainPathPoint* pHi = &base[hi];

    float loX = pLo->x, loY = pLo->y;
    float hiX = pHi->x, hiY = pHi->y;

    float dX = hiX - loX;       // segment delta
    float dY = hiY - loY;
    float midX = (hiX + loX) * kHalf;
    float midY = (hiY + loY) * kHalf;

    // Length(d) is computed by the original (call it to preserve parity / state).
    { Math::Vector2 dvec(dX, dY); (void)coast_vec2len_real(&dvec); }

    // Perpendicular (left normal): n = normalize( (-dY, dX) )  via integer XOR.
    Math::Vector2 nvec(fnegate(dY), dX);
    coast_vec2norm_real(&nvec);
    float nX = nvec.x, nY = nvec.y;

    float negLimA = fnegate(limA);

    float candX = 0.0f, candY = 0.0f;
    for (;;) {
        bool innerOK = false;
        Math::Vector2 eHi, eLo;
        while (!innerOK) {
            float t = rng->RandFloatTol() * limB * kMidAmp_0_666;
            candX = nX * t + midX;
            candY = nY * t + midY;

            eHi.x = hiX - candX;  eHi.y = hiY - candY;
            eLo.x = loX - candX;  eLo.y = loY - candY;
            coast_vec2norm_real(&eHi); coast_vec2norm_real(&eLo);

            // inner do/while continues while (cand.x < negLimA)
            innerOK = !(candX < negLimA);
        }
        // outer reject condition (loop again while TRUE):
        float dot = eLo.x * eHi.x + eLo.y * eHi.y;
        bool reject =
            ((candX <= negLimA) && (candY < negLimA)) ||
            (limA < candX) ||
            ((limA <= candX) && (limA < candY)) ||
            (dot > 0.0f);
        if (!reject) break;
    }

    int midIdx = (span >> 1) + lo;
    TerrainPathPoint* pMid = &base[midIdx];
    pMid->x = candX;
    pMid->y = candY;

    float half = limB * kHalf;
    coastline_Subdivide_cc510(self, arrPtr, lo, midIdx, limA, half);
    coastline_Subdivide_cc510(self, arrPtr, midIdx, hi, limA, half);
}

// ============================================================
// FUN_1400cc850 — TryPlacePath: build one candidate polyline, validate it has no
// self-intersection and no crossing with previously-placed paths, and on success
// deep-append it into the patchList (hf+0x18). Returns 1 on success, 0 on reject.
// Exe addr: 0x1400cc850.
//
//   self      = hf base
//   nPts      = number of points in the path (cd090 passes gridSize+1)
//   span      = (float)gridSize           (the ring's coordinate extent)
//   a4..a8    = per-vertex attributes propagated to every record (+0x08..+0x18)
//   branchSel = param_9  ( <0 -> closed coastline ring;  >=0 -> branch off path[branchSel] )
//
// IMPORTANT: this stage operates ENTIRELY in the 2D point plane. It never reads
// or writes the float heightmap at hf+0x08.
// ============================================================
static int coastline_TryPlacePath_cc850(TerrainHeightfield* hf, int nPts, float span,
                                          float a4, float a5, float a6, float a7, float a8,
                                          float branchSel) {
    System::Random* rng = &hf->random;

    // candidate path header {void* data; int count; int cap}.
    // ENDGAME B.3.2 step 2: a real Collection::Array with RAII. Exactly one
    // destruction per path: the three reject-path frees and the success-path
    // free that used to sit at each `return` are the destructor now.
    WgPathPointArray cand;

    coastline_Resize_Stride1C_23b70(&cand, nPts);

    // Fill attributes for all nPts records (disasm store order: a6,a5,a8,a7,a4).
    {
        TerrainPathPoint* e = cand._data;
        for (int i = 0; i < nPts; ++i) {
            TerrainPathPoint* r = &e[i];
            r->strength     = a4; // +0x08
            r->innerRadius  = a5; // +0x0c
            r->innerFalloff = a6; // +0x10
            r->outerRadius  = a7; // +0x14
            r->outerFalloff = a8; // +0x18
        }
    }

    int midIdx;
    if ((int)branchSel < 0) {
        // ---- closed coastline ring branch (cd090's path) ----
        TerrainPathPoint* base = cand._data;
        TerrainPathPoint* p0 = &base[0];

        // point[0]: original packs (uint)(span*-0.5) << 0x20 -> low dword 0,
        // high dword = span*-0.5. i.e. x=0, y=span*-0.5.
        p0->x = 0.0f;
        p0->y = span * kNegHalf;

        int lastIdx = cand._size - 1;
        TerrainPathPoint* pLast = &base[lastIdx];

        uint32_t which = rng->Rand(3);
        float spanHalf = span * kHalf;
        // Disasm cases (CONCAT44 packing):
        //  case 0: local_f8 = (ull)(uint)(span*0.5) << 0x20  -> x=0, y=span*0.5
        //  case 1: fVar22 = span*0.5; local_f8 = (ull)(uint)fVar22 -> x=span*0.5, y=0
        //  case 2: local_f8 = (ull)(uint)fVar22 where fVar22 = span*-0.5 (initial)
        //          -> x=span*-0.5, y=0
        if (which == 0) {
            p0->y = span * kNegHalf;  // (point[0] already set above; harmless)
            pLast->x = 0.0f;
            pLast->y = spanHalf;
        } else if (which == 1) {
            pLast->x = spanHalf;
            pLast->y = 0.0f;
        } else { // which == 2
            pLast->x = span * kNegHalf;
            pLast->y = 0.0f;
        }

        // midpoint: random jitter, both lanes RandFloatTol * span * 0.125.
        // disasm: CONCAT44(jx, jy) -> low dword=jy (x lane), high dword=jx (y lane).
        midIdx = cand._size >> 1;
        float jx = rng->RandFloatTol() * span * kJitter_0_125;
        float jy = rng->RandFloatTol() * span * kJitter_0_125;
        TerrainPathPoint* pMid = &base[midIdx];
        pMid->x = jy;
        pMid->y = jx;

        float amp  = span * kQuarter;   // fVar22 = span*0.25
        float limA = span * kHalf;      // fVar24 = span*0.5
        coastline_Subdivide_cc510(hf, &cand, 0, midIdx, limA, amp);
    } else {
        // ---- branch-off-existing-path branch (NOT used by cd090; ccf60 uses it) ----
        RawArray<TerrainPathPoint>* outerData = hf->paths._data;
        int bi = (int)branchSel;
        int srcCount = outerData[bi].size;
        int q = (srcCount + ((srcCount >> 31) & 3)) >> 2;
        int pick = rng->Rand(q, q * 3);
        const TerrainPathPoint* srcVec = outerData[bi].data;
        cand._data[0].x = srcVec[pick].x;   // point[0] = src[pick].xy (one 8-byte copy; H7)
        cand._data[0].y = srcVec[pick].y;

        float rf = rng->RandFloatTol() * span * kHalf;
        int lastIdx = cand._size - 1;
        TerrainPathPoint* pLast = &cand._data[lastIdx];
        uint32_t which = rng->Rand(4);
        // Retail CONCAT44 packing (verified 0x1400cc850, param_9>=0 block):
        //  which0: (x=rf,        y=span*0.5)   which1: (x=span*0.5,  y=rf)
        //  which2: (x=span*-0.5, y=rf)         which3: (x=rf,        y=span*-0.5)
        if (which == 0)      { pLast->x = rf;              pLast->y = span * kHalf; }
        else if (which == 1) { pLast->x = span * kHalf;    pLast->y = rf; }
        else if (which == 2) { pLast->x = span * kNegHalf; pLast->y = rf; }
        else                 { pLast->x = rf;              pLast->y = span * kNegHalf; }

        midIdx = 0;
        float amp  = span * kQuarter;
        float limA = span * kHalf;
        (void)amp; (void)limA;
        midIdx = 0;
    }

    // Subdivide the second half [midIdx, count-1].
    {
        float amp  = span * kQuarter;
        float limA = span * kHalf;
        coastline_Subdivide_cc510(hf, &cand, midIdx, cand._size - 1, limA, amp);
    }

    int count = cand._size;

    // ---- nearest-neighbour adjacency validation pass (self-consistency) ----
    // Walk along the polyline; for each point find the nearest OTHER point via
    // LengthSquared. If the nearest is not the previous-pick or previous-pick+2,
    // reject. (The original seeds the running min with branchSel(param_9); since
    // branchSel is hugely negative for the ring, the first comparison degenerates
    // to "always replace", i.e. plain nearest-neighbour search.)
    {
        int prevPick = -1;
        int row = 0;
        const TerrainPathPoint* data = cand._data;
        bool rejected = false;
        for (int li = 0; li < count && !rejected; ++li) {
            int bestIdx = -2;
            float best = kBigDist;
            const TerrainPathPoint* pa = &data[row];
            for (int j = 0; j < count; ++j) {
                if (row == j) continue;
                const TerrainPathPoint* pb = &data[j];
                float dvx = pa->x - pb->x;
                float dvy = pa->y - pb->y;
                float lensq;
                { Math::Vector2 v(dvx, dvy); lensq = coast_vec2lensq_real(&v); }
                if (lensq < best) { best = lensq; bestIdx = j; }
            }
            if (bestIdx != prevPick + 2 && bestIdx != prevPick) { rejected = true; break; }
            row++;
            prevPick++;
        }
        if (rejected) {
            return 0;
        }
    }

    // ---- self-intersection: segment vs non-adjacent segment in the SAME path ----
    {
        int lim = count - 1;
        const TerrainPathPoint* data = cand._data;
        for (int i = 0; i < lim; ++i) {
            const TerrainPathPoint* s0 = &data[i];
            const TerrainPathPoint* s1 = &data[i + 1];
            float ax = s0->x, ay = s0->y;
            float bx = s1->x, by = s1->y;
            float dx = bx - ax;
            float dy = by - ay;
            for (int j = 0; j < lim; ++j) {
                // skip adjacent / shared-vertex segments
                if (j == i || j + 1 == i || j == i + 1 || j + 1 == i + 1) continue;
                const TerrainPathPoint* t0 = &data[j];
                const TerrainPathPoint* t1 = &data[j + 1];
                float cx = t1->x - t0->x;
                float cy = t1->y - t0->y;
                float ex = ax - t0->x;   // (seg-i start) - (seg-j start)
                float ey = ay - t0->y;
                // denom must be dA x dB = dx*cy - dy*cx to match FUN_1400cc850. The
                // numerators below are already canonical (u->t, v->u); the prior
                // sign-flipped denom negated both params, degenerating the crossing
                // test to the backward region (harmless for the first ring, but wrong).
                float denom = dx * cy - dy * cx;
                if (denom != 0.0f) {
                    float u = (cx * ey - cy * ex) / denom;
                    float v = (dx * ey - dy * ex) / denom;
                    if (u > 0.0f && u < kOne && v > 0.0f && v < kOne) {
                        return 0;
                    }
                }
            }

            // ---- cross-path: this segment vs every segment of every existing path ----
            int nPaths = hf->paths._size;   // patchList count
            if (nPaths > 0) {
                const RawArray<TerrainPathPoint>* outer = hf->paths._data;
                for (int pIdx = 0; pIdx < nPaths; ++pIdx) {
                    const RawArray<TerrainPathPoint>* hdr = &outer[pIdx];
                    int oc = hdr->size;
                    const TerrainPathPoint* od = hdr->data;
                    for (int k = 0; k < oc - 1; ++k) {
                        const TerrainPathPoint* q0 = &od[k];
                        const TerrainPathPoint* q1 = &od[k + 1];
                        float cx = q1->x - q0->x;
                        float cy = q1->y - q0->y;
                        float ex = ax - q0->x;
                        float ey = ay - q0->y;
                        // canonical dA x dB (was sign-flipped); see self-intersection note above.
                        float denom = dx * cy - dy * cx;
                        if (denom != 0.0f) {
                            float u = (cx * ey - cy * ex) / denom;
                            float v = (dx * ey - dy * ex) / denom;
                            if (u > 0.0f && u < kOne && v > 0.0f && v < kOne) {
                                return 0;
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- success: deep-append the candidate into patchList (hf+0x18) ----
    coastline_AppendPath_24460(&hf->paths, hf->paths._size + 1, &cand);

    return 1;
}

// ============================================================
// FUN_1400cd090 — EmitCoastlineRing (STAGE ENTRY).
// Exe addr: 0x1400cd090.
//   while (patchList.count == 0)               // hf+0x20
//     cc850(hf, gridSize+1, (float)gridSize,
//           2.0, 10.0, 1.25, 4.0, 4.0, -NAN);   // closed ring (branchSel < 0)
// Loops until exactly one valid closed coastline ring is appended.
//
// HEIGHT EFFECT: NONE. cd090 emits only 2D polyline geometry into the patchList
// (hf+0x18); the heightmap at hf+0x08 is untouched. Later stamp stages (e910/ec50)
// consume the ring to carve the heightmap; the height change happens THERE.
// ============================================================
void wg_f390_coastline(TerrainHeightfield* hf) {
    if (!hf) return;

    int gridSize = hf->gridSize;

    // branchSel = -NAN in the original (negative-as-int). Use -1.0f:
    // (int)(-1.0f) = -1 < 0 selects the closed-ring branch.
    const float branchSel = -1.0f;

    // Unbounded, as retail (0x1400cd118 `CMP [RBX+0x20],0 ; JZ 0x1400cd0e0`): a size row whose
    // grid makes a closed ring impossible hangs the world build in retail too.
    while (hf->paths._size == 0) {
        coastline_TryPlacePath_cc850(hf, gridSize + 1, (float)gridSize,
                                     kRing_a4_2_0, kRing_a5_10_0, kRing_a6_1_25,
                                     kRing_a78_4_0, kRing_a78_4_0, branchSel);
    }
}
