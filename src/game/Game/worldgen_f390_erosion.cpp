// src/game/Game/worldgen_f390_erosion.cpp
// ============================================================================
// M5 5b — worldgen f390 LATE STAGES: droplet erosion + feature carve + smoothing.
// ============================================================================
//
// Faithful clean-C++ reimplementation of the trailing f390 heightmap stages
// (blueprint §0 order — these run AFTER e5d0 base + the path/river/stamp build
// pass, operating in-place on the float heightmap at hf+0x08):
//
//   gworld+0x24 = 4 ; cc080(hf)   ; droplet erosion        -> wg_f390_erosion
//   gworld+0x24 = 5 ; ec50(hf)    ; feature stamp finalize -> wg_f390_carve
//                     c350(hf)    ; weighted box blur      -> wg_f390_smooth (pass 1)
//                     c350(hf)    ; second smoothing pass  -> wg_f390_smooth (pass 2)
//
// The orchestrator (wg_HeightmapPipeline_f390 in worldgen.cpp, kFullPipeline path)
// calls these in that order; wg_f390_smooth is ONE pass — the orchestrator invokes
// it twice in a row, matching the two consecutive c350 calls in f390.
//
// Verified against live Ghidra disassembly of Application-x64.exe:
//   FUN_1400cc080                  (droplet erosion)            @ 0x1400cc080
//   ConsumeProduceComponent_impl_ec50 (feature finalize/carve)  @ 0x1400cec50
//   ConsumeProduceComponent_impl_c350 (box-blur smoothing)      @ 0x1400cc350
//   ConsumeProduceComponent_impl_sub_eee0 (radial falloff brush)@ 0x1400ceee0  (shared callee)
//   ConsumeProduceComponent_impl_sub_1460 == CRT powf           @ 0x140131460  (-> std::pow)
//   FUN_140132010                  == CRT sqrtf                 @ 0x140132010  (-> std::sqrt)
//
// Heightfield scratch-struct field offsets (blueprint §1.1) — Ghidra's int* indexing:
//   param_1[0]     +0x00  gridSize (dim = mapDim*2; (dim+1) verts/side)
//   param_1[2]     +0x08  float* heightmap data
//   param_1[4]     +0x10  heightmap cell count = (dim+1)^2 (count slot of +0x08 header)
//   param_1[6]     +0x18  patchList.data  (vector of {void* recs, int count} 16B groups)
//   param_1[8]     +0x20  patchList group count (detailCountA)
//   param_1[0xe]   +0x38  listC.data (mountain satellites/centers, 0xC-stride {x,y,radius})
//   param_1[0x10]  +0x40  listC.count
//   param_1[0x28a] +0xa28 sizeRow (int*) ; sizeRow[+0x24] = erodeIterations
//   (Random @ hf+0x58 == param_1+0x16)
//
// ADD-ONLY: this file does not modify worldgen.cpp. The orchestrator wires these
// three extern "C" entry points into the kFullPipeline branch separately.

#include "Game/TerrainHeightfield.h"   // TerrainHeightfield, the stage declarations
#include "Game/exe_bodies.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "crt/msvc_libm.h"            // MsvcLibm::Powf (wg_powf below)
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "Game/worldgen_brush_curve.h"   // [WGPAR] the exact falloff memo of the eee0 brush
#include "Game/worldgen_erosion_par.h"   // [EROSIONPAR] the parallel droplet commit
#include "system/parallel_for.h"         // [EROSIONPAR] System::ParallelFor
#include <atomic>
#include <thread>
#include <vector>
#if defined(__SSE2__) || defined(__x86_64__)
#include <emmintrin.h>   // _mm_pause
#endif

// The heightfield's fields are TerrainHeightfield's (TerrainHeightfield.h):
//   param_1[0] gridSize, [2] heightmap.data, [4] heightmap.size, [6] / [8]
//   paths._data / _size, [0xe] / [0x10] basinStamps._data / _size, +0x58 random,
//   [0x28a] sizeRow (sizeRow->erosionIterations is its +0x24).

// ----------------------------------------------------------------------------
// .rdata float constants (verified via read_memory; address = raw LE dword = value)
// ----------------------------------------------------------------------------
static constexpr float kOne             = 1.0f;     // 0x14017214c (g_fUVScale) = 0x3f800000
static constexpr float kErodeFactor     = 0.0625f;  // 0x1401720dc = 0x3d800000 (slope->transfer, 1/16)
static constexpr float kErodeMaxXfer    = 0.8f;     // 0x140172128 = 0x3f4ccccd (max-transfer clamp)
                                                    //   NB: blueprint mislabeled this as 0.5; the raw
                                                    //   dword 0x3f4ccccd is 0.8, used live in cc080.
static constexpr float kCenterWeight    = 2.0f;     // 0x140172174 = 0x40000000 (c350 center / eee0 mul)
static constexpr float kRadiusThreshold = 0.99f;    // 0x140172144 = 0x3f7d70a4 (eee0 radius-growth)
static constexpr float kListCExp        = 2.25f;    // 0x14017217c = 0x40100000 (ec50 listC powf base arg = DAT_14017217c)
static constexpr float kListCStrength   = 2.0f;     // 0x140172174 = 0x40000000 (ec50 listC strength   = DAT_140172174)
static const     uint32_t kSignBitMask  = 0x80000000u; // 0x1401722c0 (float-negate XOR mask)

// ============================================================================
// powf / sqrtf — the exe's sub_1460 / FUN_140132010 are the CRT libm routines.
// Worldgen never feeds inf/NaN, so the special-case branches are dead. sqrtf maps to
// std::sqrt (one instruction everywhere); powf is the exe's own MSVC CRT body, ported
// bit-exact in MsvcLibm (crt/msvc_libm.h): MinGW's differs in the last bit and differs
// again between mingw-w64 runtimes (WorldHash f32 changed from GCC 13 to 16, 2026-10-26).
// sub_1460(base, exp) == powf(base, exp).
// ============================================================================
static inline float wg_powf(float base, float exp) { return MsvcLibm::Powf(base, exp); }
static inline float wg_sqrtf(float v)               { return std::sqrt(v); }

// XOR a float's bit pattern with the sign bit (== negate). Matches the exe's
// `(uint)x ^ 0x80000000` idiom in eee0 (used to compute -(d - cutoff)).
static inline float wg_negate_bits(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    bits ^= kSignBitMask;
    float out;
    std::memcpy(&out, &bits, 4);
    return out;
}

// ============================================================================
// sub_eee0 — RADIAL FALLOFF BRUSH (shared callee of ec50; also used by e910).
//   @ 0x1400ceee0  ConsumeProduceComponent_impl_sub_eee0
//
// Stamps a circular falloff into a target float mask. The falloff at a cell at
// Euclidean distance d from the brush centre is
//        f(d) = 1 / ( powf(base, -(d - cutoff)) + 1 )      [logistic-ish curve]
// where base = `falloffBase` (arg), cutoff = `cutoff` (arg, param_3).
//
// Step 1 — grow the integer radius `r` (float, +1.0 each step) starting at 1.0
//   while  1/(powf(base, -(r - cutoff)) + 1) < 0.99  (kRadiusThreshold).
//   i.e. expand until the on-axis falloff reaches ~0.99 of full. `r` becomes the
//   half-extent of the square scan window (+1).
//
// Step 2 — scan the square window centred on (cx,cy), bounds-checked to [0, dim],
//   computing per-cell falloff and blending into the target mask:
//     mode 0 (MIN):  v = f(d) * strength ;          if (v < mask)  mask = v;
//     mode 1 (MAX):  v = (1 - f(d)) * strength ;    if (v > mask)  mask = v;  (strict >)
//
// Params (matching the disasm):
//   hf          = heightfield (only *hf == dim is read, for row stride dim+1)
//   center      = float[2] {x, y} brush centre (fractional)
//   cutoff      = param_3 (falloff midpoint distance)
//   falloffBase = param_4 (powf base)
//   strength    = param_5
//   mode        = param_6 (0 = MIN blend, 1 = MAX blend)
//   maskHdr     = param_7 ({float* data, ...} target mask header; *maskHdr == data ptr)
// ============================================================================
static void wg_eee0_radial_brush(const TerrainHeightfield* hf, const float* center, float cutoff,
                                 float falloffBase, float strength, char mode,
                                 RawArray<float>* maskHdr) {
    int dim  = hf->gridSize;
    int edge = dim + 1;   // row stride (iVar1 = iVar9 + 1)

    // Step 1: grow radius until on-axis falloff >= 0.99.
    //   exponent argument = negate_bits(g_fUVScale - cutoff) on first eval, then
    //   negate_bits(r - cutoff) as r grows. (g_fUVScale == 1.0 == the start r.)
    // [WGPAR] step 1 and the per-cell falloff come from an exact memo
    // (Game/worldgen_brush_curve.h): the same wg_powf / wg_sqrtf, negate and float
    // expression as this body, keyed on the bits of (falloffBase, cutoff) -- the stored
    // weights are the values this body computed; each distinct (brush, s) is computed
    // once instead of once per cell. One cache per process: worldgen runs on one
    // thread at a time (the loader).
    static WgBrushCurveCache s_curves(&wg_powf, &wg_sqrtf, kOne, kRadiusThreshold);
    WgBrushCurveCache::Curve& curve = s_curves.Get(falloffBase, cutoff);

    int half  = dim >> 1;                  // iVar9 = iVar9 >> 1
    int cxBase = (int)center[0] + half;    // (int)param_2[0] + half  (col)  -- see note
    int cyBase = (int)center[1] + half;    // (int)param_2[1] + half  (row)
    int span   = curve.span;               // iVar10 = (int)fVar17 + 1

    float* mask = maskHdr->data;

    // Disasm: outer var iVar15 = dy in [-span, span] (row), inner uVar13/iVar12 =
    // dx in [-span, span] (col). Distance uses (dx^2 + dy^2). The exe maps
    // param_2[1] (center.y) to the ROW offset (cyBase) and param_2[0] (center.x)
    // to the COL offset (cxBase); fVar2 = *param_2 (center.x) seeds the col base.
    for (int dy = -span; dy <= span; ++dy) {
        int row = dy + cyBase;
        if (row < 0 || row >= edge) continue;
        for (int dx = -span; dx <= span; ++dx) {
            int col = dx + cxBase;
            if (col < 0 || col >= edge) continue;

            float f = s_curves.Weight(curve, dx * dx + dy * dy);   // 1/(powf(base,-(d-cutoff))+1)

            float* cell = &mask[(int64_t)row * edge + col];
            if (mode == 0) {
                float v = f * strength;
                if (v < *cell) *cell = v;                   // MIN blend
            } else {
                float v = (kOne - f) * strength;
                if (v >= *cell && v != *cell) *cell = v;    // MAX blend (strict >)
            }
        }
    }
}

// ============================================================================
// wg_f390_erosion == FUN_1400cc080 — DROPLET / THERMAL EROSION.
//   @ 0x1400cc080
//
// Runs `erodeIterations` (sizeRow[+0x24]) independent droplet passes. Each pass:
//   * pick a random start cell (col,row) in [1, dim-1] via Random::Rand(dim-1)+1.
//   * repeatedly: examine the cell's full 3x3 Moore neighbourhood (9 cells incl.
//     itself, scanned in a fixed tie-break order: top row L->R, mid row L->R,
//     bottom row L->R), find the LOWEST cell (strict <, first wins).
//   * if no cell is lower than the current cell (best == self), or current cell
//     height <= 0, or we stepped to the [0]/[dim] border, stop this droplet.
//   * otherwise transfer material between current and lowest:
//        slope  = (h_cur - h_low) * 0.0625          (kErodeFactor)
//        t      = min(slope, 0.8)                    (kErodeMaxXfer clamp)
//        h_cur' = (1 - t) * h_cur + t * h_low
//        h_low' = (1 - t) * h_low + t * h_cur
//     i.e. fraction t of the gap is exchanged, pulling the pair toward its mean.
//     Then walk to the lowest cell and repeat.
//
// HEIGHT EFFECT — *conservative*: each transfer is a symmetric exchange about the
// pair mean (h_cur' + h_low' == h_cur + h_low EXACTLY). Erosion therefore does NOT
// change the global SUM or MEAN of the heightmap (mass-conserving). It only
// REDISTRIBUTES: peaks get shaved, adjacent lower cells rise, slopes relax toward
// local flatness. No global lowering toward sea level comes from this stage.
// Net signed change of the whole field ~ 0 (the start cell never starts on the
// border ring, so the outermost row/col is left untouched). Local amplitude
// (max-min over a slope) shrinks; valley-floor mean is essentially unchanged.
// => NOT the source of the ~8-unit normalization.
// ============================================================================
// EROSION (loading-speed block, docs/extensions/handins/EROSION.md): the same float operations in
// the same order on the same cells as cc080, the same branches, the same Random draws in the
// same order -- cheaper only, by three exact changes:
//   * the heightmap base is read once. cc080 re-reads hf+0x08 at the top of every step and again
//     after its first store; nothing in this stage resizes or moves the heightmap.
//   * the cell's height is carried in a register from the step that wrote it: after a move,
//     hm[bestIdx] holds exactly newBest (the step's last store, to a cell its first store did not
//     touch), so cc080's load of it returns those bits. That takes a store-to-load forward off
//     the step's dependency chain.
//   * the self compare is dropped: there hm[curIdx] == hCur and bestH <= hCur, and h < h is false
//     for every float (NaN included), so it never fires.
// The scan stays branchy on purpose: the next cell's index is computed in the branch arms, so a
// predicted branch hands the next step its addresses without waiting on the compares. Branch-free
// selects (cmov / minss) were measured slower where a droplet's path is predictable and only
// ~10-20% faster where it is not (EROSION.md).
//
// EROSIONPAR (docs/extensions/handins/EROSIONPAR.md): the droplet is one function template over
// its heightmap access (`Mem`), so the sequential body, the speculating workers and the commit's
// own runs execute the SAME float expressions; only where a height is read from / stored to differs.
//   Mem::Load(i)          the height of cell i as this droplet sees it
//   Mem::Store(i, v)      this droplet stores v to cell i
//   Mem::Center(i, c, r)  the droplet is about to read the 3x3 around cell i = (col c, row r)
//                         (its first read: cell i itself); false = give up (a speculation over
//                         its step cap). Every cell a droplet reads or stores lies in the 3x3
//                         around one of its centres.
// Returns false only when Center gave up.
template <class Mem>
static inline bool wg_erosion_droplet(Mem& mem, int dim, int edge, int col, int row) {
    int curIdx = row * edge + col;
    float hCur = mem.Load(curIdx);
    while (true) {
        if (!mem.Center(curIdx, col, row)) return false;
        if (hCur <= 0.0f) break;            // dead/sea cell — stop droplet

        // Scan 3x3 Moore neighbourhood for the strictly-lowest cell.
        // Tie-break: first-encountered wins (scan order fixed).
        int bestCol = -1, bestRow = -1;     // iVar9 / iVar14 (start -1,-1)
        int bestIdx = curIdx;               // iVar15 (start self)
        float bestH = hCur;                 // fVar20

        int rowTop = curIdx - edge;
        {
            float h = mem.Load(rowTop - 1);
            if (h < bestH) { bestCol = col - 1; bestIdx = rowTop - 1; bestRow = row - 1; bestH = h; }
            h = mem.Load(rowTop);
            if (h < bestH) { bestIdx = rowTop; bestRow = row - 1; bestH = h; bestCol = col; }
            h = mem.Load(rowTop + 1);
            if (h < bestH) { bestCol = col + 1; bestIdx = rowTop + 1; bestRow = row - 1; bestH = h; }
        }
        {
            float h = mem.Load(curIdx - 1);
            if (h < bestH) { bestCol = col - 1; bestIdx = curIdx - 1; bestH = h; bestRow = row; }
            // self (hm[curIdx] == hCur): never < bestH, see above
            h = mem.Load(curIdx + 1);
            if (h < bestH) { bestCol = col + 1; bestIdx = curIdx + 1; bestH = h; bestRow = row; }
        }
        int rowBot = curIdx + edge;
        {
            float h = mem.Load(rowBot - 1);
            if (h < bestH) { bestCol = col - 1; bestIdx = rowBot - 1; bestH = h; bestRow = row + 1; }
            h = mem.Load(rowBot);
            if (h < bestH) { bestIdx = rowBot; bestH = h; bestCol = col; bestRow = row + 1; }
            h = mem.Load(rowBot + 1);
            if (h < bestH) { bestIdx = rowBot + 1; bestH = h; bestCol = col + 1; bestRow = row + 1; }
        }

        // Conservative symmetric transfer toward the pair mean.
        float t = (hCur - bestH) * kErodeFactor;     // slope * 1/16
        if (t > kErodeMaxXfer) t = kErodeMaxXfer;    // clamp to 0.8
        const float newBest = (kOne - t) * bestH + t * hCur;
        mem.Store(curIdx, (kOne - t) * hCur + t * bestH);
        mem.Store(bestIdx, newBest);        // second store: wins when bestIdx == curIdx

        // Step to the lowest cell; stop if we didn't move (best == self),
        // or stepped onto the [0]/[dim] border ring.
        if (bestIdx == curIdx || bestCol < 1 || bestRow < 1 || bestCol >= dim || bestRow >= dim)
            break;
        col = bestCol;
        row = bestRow;
        curIdx = bestIdx;
        hCur = newBest;                     // == hm[curIdx], just stored
    }
    return true;
}

namespace {

// The sequential body's access: the heightmap itself.
struct ErodeDirect {
    float* hm;
    float Load(int i) const { return hm[i]; }
    void Store(int i, float v) { hm[i] = v; }
    bool Center(int, int, int) const { return true; }
};

}  // namespace

void wg_f390_erosion_sequential(TerrainHeightfield* hfv) {
    int dim  = hfv->gridSize;                   // *param_1
    int edge = dim + 1;                         // iVar19 = iVar2 + 1 (row stride)

    const TerrainSizeRow* sizeRow = hfv->sizeRow;
    if (!sizeRow) return;
    int iters = sizeRow->erosionIterations;     // sizeRow +0x24

    if (iters <= 0) {
        return;
    }

    System::Random* rng = &hfv->random;
    ErodeDirect mem{hfv->heightmap.data};       // cc080 re-reads +0x08 per step; it never changes here

    for (int it = 0; it < iters; ++it) {
        // Start cell in [1, dim-1] (Rand(dim-1) -> [0, dim-2], +1).
        int col = (int)rng->Rand((uint32_t)(dim - 1)) + 1;
        int row = (int)rng->Rand((uint32_t)(dim - 1)) + 1;
        wg_erosion_droplet(mem, dim, edge, col, row);
    }
}

// ============================================================================
// EROSIONPAR -- the droplets speculated in parallel, committed in order.
// ============================================================================
// One System::ParallelFor call over W+1 roles (W = pool workers): role 0 is the COMMIT, roles
// 1..W are SPECULATORS. They share the heightmap and a ring of droplet slots.
//
//   COMMIT (one thread)  for droplet j = 0, 1, 2, ...:
//     * draws the start cells of droplets up to j + lookahead with the stage's Random, in droplet
//       order, two Rand(dim - 1) per droplet -- the very draws of the sequential body, so the
//       Random ends in the same state; publishes each into its slot (state FREE(j));
//     * takes droplet j's slot: if no speculator claimed it, runs j itself (TAKEN); if one is
//       running it, waits for it; a speculation that finished is VALIDATED and APPLIED (each
//       cell it stored gets the last value it stored there); a failed validation or an
//       overflow re-runs j itself;
//     * records j's footprint (below) and publishes `committed` = j + 1 (release).
//   SPECULATOR  claims the next droplet j (FREE -> SPEC), loads g0 = `committed` (acquire), runs j
//     against the live heightmap through a private overlay (its own stores, which its later steps
//     read back), logs the centre of every step and, per cell it stored, the last value stored
//     there; then DONE (or FAIL past stepCap). It leaves the droplets closer than `gap` to the
//     commit to the commit (with few speculators the commit would otherwise idle, waiting).
//
// THE FOOTPRINT of a committed droplet k: its stores (the log) and their bounding box, the box of
// its centres grown by one cell (every cell it read or stored lies inside). The commit's own runs
// (ErodeLive) store with relaxed atomics (speculators read concurrently) and log the same way; a
// run storing more than the log holds marks its footprint unbounded.
//
// VALIDATION of speculated droplet j: no droplet k with g0 <= k < j stored to a cell in the 3x3
// around a centre of j. Box against box first (commit-local, almost always disjoint); only
// overlapping boxes compare k's stores against j's centres cell by cell; an unbounded k fails it.
//
// WHY IT IS EXACT.
//   * If j validates: every store any droplet made to a cell j read came from a droplet < g0 --
//     droplets >= j had not run (only the commit stores, in order), and g0 .. j-1 stored none of
//     them. The release of `committed` after droplet g0-1 and the speculator's acquire of g0 put
//     all those stores before j's reads, and there is no later one in the cells' modification
//     order, so each heightmap read of j returned exactly the committed value at commit time. j's
//     reads of cells it stored itself come from its overlay, which holds what j stored; live, j
//     would read back the same. Every float j computed is then the one the sequential body
//     computes for droplet j after droplets 0 .. j-1 (the same template, the same inputs, the same
//     MXCSR: ParallelFor copies the caller's into its workers). Writing each cell j stored with
//     the last value j stored there leaves the heightmap exactly as the sequential body's stores
//     of droplet j leave it (between two stores of droplet j to a cell, only j reads it).
//   * If not (or it overflowed, or nobody speculated it): j runs live on the commit thread after
//     0 .. j-1, against the committed heightmap -- the sequential body's droplet j, literally.
//   * The Random: only the commit draws, in droplet order, exactly two per droplet.
// Speculation only reads the heightmap and writes its own slot and overlay, so its timing, its
// order, or whether it ran at all cannot change the result -- only the speed.
//
// NO DEADLOCK. The commit never waits for a droplet nobody runs: an unclaimed slot it takes
// itself. It waits only for a CLAIMED speculation, which runs on a live thread, never waits, and
// ends within stepCap steps. ParallelFor hands chunk 0 (the commit) to the first thread that asks,
// and a speculator exits once the commit has finished; run inline (a nested call) the commit
// simply runs every droplet itself.
//
// THE RING. lookahead < kWgErosionRing / 2: a speculator claims j only once it is drawn, i.e. when
// committed >= j - lookahead, so g0 >= j - lookahead; the footprints of g0 .. j-1 are then still in
// their slots (a slot is redrawn kWgErosionRing droplets later).
namespace {

struct ErodeWrite {
    int32_t idx;
    float   val;
};

struct ErodeBox {   // inclusive cell rows / cols
    int r0, r1, c0, c1;
    void Start(int c, int r) { r0 = r1 = r; c0 = c1 = c; }
    void Add(int c, int r) {
        if (r < r0) r0 = r;
        if (r > r1) r1 = r;
        if (c < c0) c0 = c;
        if (c > c1) c1 = c;
    }
};

// The commit thread's access: the heightmap, every store logged for the footprint.
struct ErodeLive {
    float*      hm;
    ErodeWrite* writes;
    int         wcap;
    int         nw;
    bool        started;
    ErodeBox    box;
    float Load(int i) const { return hm[i]; }   // the only writer: its own plain reads do not race
    void Store(int i, float v) {
        __atomic_store(hm + i, &v, __ATOMIC_RELAXED);   // speculators read concurrently
        if (nw < wcap) { writes[nw].idx = i; writes[nw].val = v; }
        ++nw;                                           // nw > wcap: the footprint is unbounded
    }
    bool Center(int, int c, int r) {
        if (started) box.Add(c, r);
        else { box.Start(c, r); started = true; }
        return true;
    }
};

// The commit thread's access while it runs alone: the heightmap (a speculator may still be reading).
struct ErodeSolo {
    float* hm;
    float Load(int i) const { return hm[i]; }
    void Store(int i, float v) { __atomic_store(hm + i, &v, __ATOMIC_RELAXED); }
    bool Center(int, int, int) const { return true; }
};

// A speculator's access: its own stores in a log, every other height from the live heightmap.
// A droplet stores exactly two cells per step, cur then best, and best is the next step's cur, so
// the only cells it stored that it can read back are earlier cells of its path. The one it reads
// back every step -- the previous cell, stored by the previous step's first store -- is kept in
// registers (prevIdx / prevVal, picked without a branch); every older path cell is marked in a
// bitmap over the grid when it stops being the previous one, and found by a search of the log.
// Load(i) == the value of the droplet's last store to i if it stored i, else the heightmap's.
struct ErodeSpec {
    const float* hm;
    uint64_t*    bits;      // older path cells (see above); empty between droplets
    ErodeWrite*  writes;    // 2 * cap: every store, in order (the speculator's scratch)
    int32_t*     centers;   // cap
    int          nw;
    int          nc;
    int          cap;
    ErodeBox     box;
    int          prevIdx;   // the previous step's cur (-1: none yet)
    float        prevVal;   // its stored value
    void Mark(int i) { bits[i >> 6] |= uint64_t(1) << (i & 63); }
    float Load(int i) const {
        float v;
        if ((bits[i >> 6] >> (i & 63)) & 1u) {
            int n = nw;
            while (writes[--n].idx != i) {}
            v = writes[n].val;                            // the droplet's latest store to i
        } else {
            __atomic_load(hm + i, &v, __ATOMIC_RELAXED);
        }
        return (i == prevIdx) ? prevVal : v;               // prev's last store is the newest
    }
    void Store(int i, float v) {
        writes[nw].idx = i;
        writes[nw].val = v;
        ++nw;
    }
    bool Center(int i, int c, int r) {
        if (nc == cap) return false;
        if (nc == 0) {
            box.Start(c, r);
        } else {   // a new step: the old previous cell joins the bitmap, the last cur becomes prev
            box.Add(c, r);
            if (prevIdx >= 0) Mark(prevIdx);
            prevIdx = writes[nw - 2].idx;
            prevVal = writes[nw - 2].val;
        }
        centers[nc++] = i;
        return true;
    }
    // Every stored cell into the bitmap: the lazy marks miss only the last two steps' stores.
    void MarkTail() {
        for (int k = (nw > 4 ? nw - 4 : 0); k < nw; ++k) Mark(writes[k].idx);
    }
};

// A slot's state: (droplet + 1) << 3 | phase; 0 = never drawn.
enum : uint64_t { kFree = 0, kSpec = 1, kDone = 2, kFail = 3, kTaken = 4 };
inline uint64_t SlotState(int j, uint64_t phase) { return ((uint64_t(uint32_t(j)) + 1u) << 3) | phase; }

struct alignas(64) ErodeSlot {
    std::atomic<uint64_t> state{0};
    int         col = 0, row = 0;   // the start cell (commit -> speculator)
    uint32_t    g0 = 0;             // `committed` when the speculation began
    int         nc = 0, nw = 0;     // the speculation's centres; its stores (one per cell, the last)
    ErodeBox    box{};              // the speculation's centre box
    int32_t*    centers = nullptr;
    ErodeWrite* writes = nullptr;
};

// The commit's record of a committed droplet (commit-local, indexed like the slots).
struct ErodeFootprint {
    ErodeBox box;          // centre box grown by one cell
    int      nw;           // stores in the slot's log; -1 = unbounded
};

inline void SpinPause(int& spins) {
#if defined(__SSE2__) || defined(__x86_64__)
    _mm_pause();
#endif
    if (++spins >= 256) { spins = 0; std::this_thread::yield(); }
}

struct ErodeJob {
    // the stage
    System::Random* rng;
    float*          hm;
    int             dim, edge, iters;
    int             lookahead, cap;
    // shared state
    std::vector<ErodeSlot>  slots;       // kWgErosionRing
    std::vector<int32_t>    centerArena;
    std::vector<ErodeWrite> writeArena;
    std::vector<std::vector<uint64_t>> bits;      // per speculator role: the overlay filter
    std::vector<std::vector<ErodeWrite>> scratch; // per speculator role: the overlay (every store)
    int             speculators;
    int             gap;
    std::vector<ErodeFootprint> foot;          // commit-local, kWgErosionRing
    std::vector<uint8_t>        mark;          // commit-local, per cell: read by the droplet validated
    alignas(64) std::atomic<int> committed{0};   // droplets < committed are in the heightmap
    alignas(64) std::atomic<int> specNext{0};    // the next droplet a speculator claims
    alignas(64) std::atomic<bool> finished{false};
    alignas(64) std::atomic<bool> solo{false};   // the commit runs alone for now (SOLO)
    WgErosionStats st;

    void Run(int role) {
        if (role == 0) Commit();
        else Speculate(role);
    }

    // Droplet j on the commit thread, against the committed heightmap; its footprint recorded.
    void RunLive(int j, ErodeSlot& s) {
        ErodeLive mem{hm, s.writes, 2 * cap, 0, false, ErodeBox{}};
        wg_erosion_droplet(mem, dim, edge, s.col, s.row);
        ErodeFootprint& f = foot[j % kWgErosionRing];
        f.box = ErodeBox{mem.box.r0 - 1, mem.box.r1 + 1, mem.box.c0 - 1, mem.box.c1 + 1};
        f.nw = (mem.nw <= 2 * cap) ? mem.nw : -1;
    }

    // Droplet j on the commit thread while it runs alone (SOLO): nothing logged, its footprint
    // is the whole grid (a speculation whose window holds it re-runs).
    void RunSolo(int j, ErodeSlot& s) {
        ErodeSolo mem{hm};
        wg_erosion_droplet(mem, dim, edge, s.col, s.row);
        ErodeFootprint& f = foot[j % kWgErosionRing];
        f.box = ErodeBox{0, dim, 0, dim};
        f.nw = -1;
    }

    // Mark / unmark the 3x3 around every centre of speculation s in the commit-local grid.
    void MarkReads(const ErodeSlot& s, uint8_t v) {
        uint8_t* m = mark.data();
        for (int c = 0; c < s.nc; ++c) {
            uint8_t* p = m + s.centers[c] - edge - 1;
            p[0] = v; p[1] = v; p[2] = v;
            p += edge;
            p[0] = v; p[1] = v; p[2] = v;
            p += edge;
            p[0] = v; p[1] = v; p[2] = v;
        }
    }

    // No droplet g0 <= k < j stored to a cell speculation s (droplet j) read. Footprint boxes
    // first; for the rare overlapping pair, k's stores against j's read cells, marked in a grid.
    bool Validate(const ErodeSlot& s, int j) {
        const int r0 = s.box.r0 - 1, r1 = s.box.r1 + 1, c0 = s.box.c0 - 1, c1 = s.box.c1 + 1;
        bool marked = false, ok = true;
        for (int k = int(s.g0); k < j && ok; ++k) {
            const int ks = k % kWgErosionRing;
            const ErodeFootprint& f = foot[ks];
            // One branch, not four: random boxes make each compare a coin flip.
            const bool apart = (f.box.r1 < r0) | (f.box.r0 > r1) | (f.box.c1 < c0) | (f.box.c0 > c1);
            if (__builtin_expect(apart, 1)) continue;
            if (f.nw < 0) { ok = false; break; }
            if (!marked) { MarkReads(s, 1); marked = true; ++st.cellChecks; }
            const ErodeWrite* w = slots[ks].writes;
            for (int i = 0; i < f.nw; ++i)
                if (mark[size_t(w[i].idx)]) { ok = false; break; }
        }
        if (marked) MarkReads(s, 0);
        return ok;
    }

    // Replay a validated speculation's stores in order; its footprint recorded.
    void Apply(const ErodeSlot& s, int j) {
        for (int k = 0; k < s.nw; ++k) {
            ErodeWrite w = s.writes[k];
            __atomic_store(hm + w.idx, &w.val, __ATOMIC_RELAXED);
        }
        ErodeFootprint& f = foot[j % kWgErosionRing];
        f.box = ErodeBox{s.box.r0 - 1, s.box.r1 + 1, s.box.c0 - 1, s.box.c1 + 1};
        f.nw = s.nw;
    }

    // SOLO. Where droplets keep conflicting (long droplets on a steep converging slope: the
    // ramp of the tests and the bench) speculation costs more than it saves: the commit validates
    // long footprints and then re-runs the droplet anyway. Per block of kSoloBlock droplets the
    // commit counts its speculations' failures; over kSoloFailPct % it runs alone (the speculators
    // claim nothing) for soloLen droplets, then probes speculation again for a block; soloLen
    // doubles while the probes fail (up to kSoloMax) and resets when one passes. Only WHO runs a
    // droplet changes, never what it computes.
    static constexpr int kSoloBlock = 1024;
    static constexpr int kSoloFailPct = 30;
    static constexpr int kSoloMax = 65536;

    void Commit() {
        int nDrawn = 0;
        int blockEnd = kSoloBlock, soloLen = kSoloBlock;
        int blockOk = 0, blockFail = 0;
        bool soloNow = false;
        for (int j = 0; j < iters; ++j) {
            // Draw ahead, in droplet order (the sequential body's draws, two per droplet).
            const int want = (j + lookahead < iters) ? j + lookahead + 1 : iters;
            if (nDrawn < want) {
                do {
                    ErodeSlot& s = slots[nDrawn % kWgErosionRing];   // its droplet nDrawn-R is long committed
                    s.col = (int)rng->Rand((uint32_t)(dim - 1)) + 1;
                    s.row = (int)rng->Rand((uint32_t)(dim - 1)) + 1;
                    s.state.store(SlotState(nDrawn, kFree), std::memory_order_release);
                    ++nDrawn;
                } while (nDrawn < want);
            }

            {   // the next droplet's slot and log, written by another core: start the transfers
                const ErodeSlot& n = slots[(j + 1) % kWgErosionRing];
                __builtin_prefetch(&n);
                __builtin_prefetch(n.writes);
                __builtin_prefetch(n.writes + 8);
                __builtin_prefetch(n.writes + 16);
                __builtin_prefetch(n.writes + 24);
            }
            ErodeSlot& s = slots[j % kWgErosionRing];
            uint64_t cur = s.state.load(std::memory_order_acquire);
            if (cur == SlotState(j, kFree) &&
                s.state.compare_exchange_strong(cur, SlotState(j, kTaken), std::memory_order_acq_rel)) {
                ++st.unspeculated;
                if (soloNow) RunSolo(j, s);
                else RunLive(j, s);
            } else {
                int spins = 0;
                while (cur == SlotState(j, kSpec)) {
                    SpinPause(spins);
                    cur = s.state.load(std::memory_order_acquire);
                }
                if (cur == SlotState(j, kDone) && Validate(s, j)) {
                    ++st.committedSpec;
                    ++blockOk;
                    Apply(s, j);
                } else {
                    if (cur == SlotState(j, kDone)) { ++st.conflicts; ++blockFail; }
                    else ++st.overflows;
                    RunLive(j, s);
                }
            }
            committed.store(j + 1, std::memory_order_release);
            if (soloNow) ++st.solo;
            if (j + 1 == blockEnd) {
                if (soloNow) {   // probe speculation again
                    soloNow = false;
                    solo.store(false, std::memory_order_relaxed);
                    blockEnd = j + 1 + kSoloBlock;
                } else if ((blockOk + blockFail) * 4 >= kSoloBlock &&
                           blockFail * 100 > (blockOk + blockFail) * kSoloFailPct) {
                    soloNow = true;
                    solo.store(true, std::memory_order_relaxed);
                    blockEnd = j + 1 + soloLen;
                    soloLen = soloLen * 2 > kSoloMax ? kSoloMax : soloLen * 2;
                } else {
                    soloLen = kSoloBlock;
                    blockEnd = j + 1 + kSoloBlock;
                }
                blockOk = blockFail = 0;
            }
        }
        finished.store(true, std::memory_order_release);
    }

    void Speculate(int role) {
        if (role > speculators) return;
        ErodeSpec mem{hm, bits[role - 1].data(), scratch[role - 1].data(), nullptr, 0, 0, cap, ErodeBox{}, -1, 0.0f};
        for (;;) {
            if (solo.load(std::memory_order_relaxed)) {
                int spins = 0;
                while (solo.load(std::memory_order_relaxed) && !finished.load(std::memory_order_relaxed))
                    SpinPause(spins);
                if (finished.load(std::memory_order_acquire)) return;
                continue;
            }
            const int j = specNext.fetch_add(1, std::memory_order_relaxed);
            if (j >= iters) return;
            if (gap > 0) {   // too close to the commit: leave j to it, claim further on
                const int c = committed.load(std::memory_order_relaxed);
                if (j < c + gap) {
                    int next = j + 1;
                    while (next < c + gap && !specNext.compare_exchange_weak(next, c + gap, std::memory_order_relaxed)) {}
                    continue;
                }
            }
            // Wait on the slot itself (one line shared with the commit only) until j is drawn.
            ErodeSlot& s = slots[j % kWgErosionRing];
            const uint64_t mine = SlotState(j, kFree);
            uint64_t st = s.state.load(std::memory_order_acquire);
            int spins = 0;
            while (st < mine) {   // an older droplet's state: j is not drawn yet
                if ((spins & 63) == 63 && finished.load(std::memory_order_relaxed)) return;
                SpinPause(spins);
                st = s.state.load(std::memory_order_acquire);
            }
            if (st != mine || !s.state.compare_exchange_strong(st, SlotState(j, kSpec), std::memory_order_acq_rel)) {
                // The commit took j (or is past it): catch up with the commit.
                const int c = committed.load(std::memory_order_relaxed);
                int next = j + 1;
                while (next < c && !specNext.compare_exchange_weak(next, c, std::memory_order_relaxed)) {}
                continue;
            }
            const uint32_t g0 = uint32_t(committed.load(std::memory_order_acquire));
            mem.centers = s.centers;
            mem.nw = 0;
            mem.nc = 0;
            mem.prevIdx = -1;
            const bool ok = wg_erosion_droplet(mem, dim, edge, s.col, s.row);
            mem.MarkTail();
            // The slot gets each stored cell once, with its last value (the overlay walked from the
            // end; a cell's first hit is its last store); the filter is emptied on the way.
            ErodeWrite* out = s.writes;
            int nOut = 0;
            for (int k = mem.nw - 1; k >= 0; --k) {
                const int i = mem.writes[k].idx;
                uint64_t& word = mem.bits[i >> 6];
                const uint64_t bit = uint64_t(1) << (i & 63);
                if (word & bit) {
                    word &= ~bit;
                    out[nOut++] = mem.writes[k];
                }
            }
            s.g0 = g0;
            s.nc = mem.nc;
            s.nw = nOut;
            s.box = mem.box;
            s.state.store(SlotState(j, ok ? kDone : kFail), std::memory_order_release);
        }
    }
};

}  // namespace

WgErosionTuning WgErosionDefaultTuning() {
    WgErosionTuning t;
    t.lookahead   = 16;      // EROSIONPAR.md §3: the conflict rate vs keeping 7 speculators fed
    t.stepCap     = 1024;    // realistic droplets run 19-36 steps
    t.minParallel = 16384;   // below this the fork-join is not worth a wake-up
    t.speculators = 0;       // every pool worker
    t.gap         = 6;       // EROSIONPAR.md §3: with few speculators the commit runs ~1 in 8 itself
    return t;
}

void wg_f390_erosion_tuned(TerrainHeightfield* hfv, const WgErosionTuning& tuning, WgErosionStats* stats) {
    const TerrainSizeRow* sizeRow = hfv->sizeRow;
    const int iters = sizeRow ? sizeRow->erosionIterations : 0;
    const int workers = (iters >= tuning.minParallel && iters > 0) ? System::Parallel::Workers() : 0;
    if (workers <= 0) {
        wg_f390_erosion_sequential(hfv);
        if (stats) {
            *stats = WgErosionStats{};
            stats->droplets = iters > 0 ? iters : 0;
        }
        return;
    }

    ErodeJob job;
    job.rng       = &hfv->random;
    job.hm        = hfv->heightmap.data;
    job.dim       = hfv->gridSize;
    job.edge      = job.dim + 1;
    job.iters     = iters;
    constexpr int kMaxLookahead = kWgErosionRing / 2 - 1;   // see THE RING
    job.lookahead = tuning.lookahead < 1 ? 1 : (tuning.lookahead > kMaxLookahead ? kMaxLookahead : tuning.lookahead);
    job.cap       = tuning.stepCap < 1 ? 1 : tuning.stepCap;
    job.gap       = tuning.gap < 0 ? 0 : (tuning.gap > job.lookahead ? job.lookahead : tuning.gap);
    const size_t cells = size_t(job.edge) * size_t(job.edge);
    job.slots = std::vector<ErodeSlot>(kWgErosionRing);
    job.foot.assign(kWgErosionRing, ErodeFootprint{});
    job.mark.assign(cells, uint8_t(0));
    job.centerArena.resize(size_t(kWgErosionRing) * size_t(job.cap));
    job.writeArena.resize(size_t(kWgErosionRing) * size_t(job.cap) * 2u);
    for (int i = 0; i < kWgErosionRing; ++i) {
        job.slots[i].centers = job.centerArena.data() + size_t(i) * size_t(job.cap);
        job.slots[i].writes  = job.writeArena.data() + size_t(i) * size_t(job.cap) * 2u;
    }
    job.speculators = (tuning.speculators > 0 && tuning.speculators < workers) ? tuning.speculators : workers;
    job.bits.resize(size_t(job.speculators));
    for (std::vector<uint64_t>& b : job.bits) b.assign((cells + 63) / 64, 0u);
    job.scratch.resize(size_t(job.speculators));
    for (std::vector<ErodeWrite>& w : job.scratch) w.resize(size_t(job.cap) * 2u);
    job.st.workers  = workers;
    job.st.droplets = iters;

    // Roles 0 .. workers: one chunk each (grain 1). See the EXACT / NO DEADLOCK notes above.
    System::ParallelFor(0, workers + 1, 1, [&job](int b, int e) {
        for (int role = b; role < e; ++role) job.Run(role);
    });

    if (stats) *stats = job.st;
}

void wg_f390_erosion(TerrainHeightfield* hfv) {
    wg_f390_erosion_tuned(hfv, WgErosionDefaultTuning(), nullptr);
}

// ============================================================================
// wg_f390_carve == ConsumeProduceComponent_impl_ec50 — FEATURE STAMP FINALIZE/CARVE.
//   @ 0x1400cec50
//
// Builds two temp float masks sized param_1[4] (= (dim+1)^2 cells):
//     mulMask (local_a0)  — init 1.0 everywhere ; MIN-blended  (mode 0)
//     subMask (local_90)  — init 0.0 everywhere ; MAX-blended  (mode 1)
// Stamps features into them via the shared radial brush eee0:
//
//   Pass A — patch groups (param_1[6]/[8]): for each group g (count param_1[8]),
//     for each 0x1C-stride record in the group's recs[] (count at grp+8):
//       rec = {float pt[2]@0, float a@8, float b@0xc, float c@0x10, float d@0x14, float e@0x18}
//       eee0(hf, pt, cutoff=c, base=b, strength=1.0, mode 0 -> mulMask)   // MIN
//       eee0(hf, pt, cutoff=d, base=e, strength=a,   mode 1 -> subMask)   // MAX
//
//   Pass B — listC points (param_1[0xe]/[0x10]): for each 0xC-stride record
//       {float pt[2]@0, float radius@8}:
//       eee0(hf, pt, cutoff=radius, base=2.25, strength=2.0, mode 1 -> subMask)  // MAX
//
// Finalize (the carve) — for every heightmap cell i in [0, param_1[4]):
//       h[i] = mulMask[i] * h[i] - subMask[i]
//
// HEIGHT EFFECT — a MULTIPLY-then-SUBTRACT carve, NOT mass-conserving:
//   * mulMask in (0, 1]: where features are stamped it DROPS below 1.0, SCALING the
//     height DOWN toward 0 (lowering). Untouched cells keep mul=1.0 (NO change).
//   * subMask in [0, ~2.25]: where features are stamped it RISES above 0.0,
//     SUBTRACTING an absolute amount AFTER the scale — pulling heights toward / below
//     sea level (0). Untouched cells keep sub=0.0 (NO change).
//   Net: h' = mul*h - sub. Stamped regions are lowered both multiplicatively and
//   additively (rivers/lakes/resource cuts carve channels DOWN toward 0 = sea level),
//   and CAN go negative (below sea). This is a LOCAL operation: only cells under a
//   stamp move; bare land (mul=1, sub=0) is left EXACTLY as-is. Globally it lowers
//   the mean only by the total stamped mass, NOT by a uniform field-wide offset.
//   => ec50 is NOT the global ~8-unit lowerer; it carves localized water/feature beds.
// ============================================================================
void wg_f390_carve(TerrainHeightfield* hfv) {
    int cellCount = hfv->heightmap.size;    // param_1[4]

    // Two temp masks (sub_3a00 resize: {data,count,cap} headers on the stack).
    RawArray<float> mulHdr = {nullptr, 0, 0};
    RawArray<float> subHdr = {nullptr, 0, 0};
    RawArraySetSize(mulHdr, cellCount);   // local_a0 (MIN mask), 0x140023a00
    RawArraySetSize(subHdr, cellCount);   // local_90 (MAX mask), 0x140023a00

    float* mulMask = mulHdr.data;
    float* subMask = subHdr.data;
    if (mulMask && subMask) {
        for (int i = 0; i < cellCount; ++i) {
            mulMask[i] = kOne;   // init 1.0 (0x3f800000 in disasm)
            subMask[i] = 0.0f;   // init 0.0
        }
    }

    RawArray<float>* mulMaskHdr = &mulHdr;   // eee0 reads the data pointer (*param_7)
    RawArray<float>* subMaskHdr = &subHdr;

    // Pass A — patch groups (0x1C-stride records).
    int groupCount = hfv->paths._size;    // param_1[8]
    if (groupCount > 0) {
        RawArray<TerrainPathPoint>* groupBase = hfv->paths._data;   // param_1[6]
        for (int g = 0; g < groupCount; ++g) {
            // Each group elem (16B) = {recs, count, ...}.
            RawArray<TerrainPathPoint>* grp = &groupBase[g];
            int recN = grp->size;
            TerrainPathPoint* recs = grp->data;
            for (int r = 0; r < recN; ++r) {
                TerrainPathPoint* rec = &recs[r];   // stride 0x1c
                float center[2];
                center[0] = rec->x;
                center[1] = rec->y;
                float a = rec->strength;       // +0x08 pass-2 strength
                float b = rec->innerRadius;    // +0x0c pass-1 cutoff (inner radius)
                float c = rec->innerFalloff;   // +0x10 pass-1 falloff base
                float d = rec->outerRadius;    // +0x14 pass-2 cutoff (outer radius)
                float e = rec->outerFalloff;   // +0x18 pass-2 falloff base
                // MIN into mulMask: eee0(center, cutoff=b, base=c, strength=1.0, mode 0)
                wg_eee0_radial_brush(hfv, center, b, c, kOne, 0, mulMaskHdr);
                // MAX into subMask: eee0(center, cutoff=d, base=e, strength=a, mode 1)
                wg_eee0_radial_brush(hfv, center, d, e, a, 1, subMaskHdr);
            }
        }
    }

    // Pass B — listC points (0xC-stride {pt[2], radius}). MAX into subMask.
    int listcCount = hfv->basinStamps._size;    // param_1[0x10]
    if (listcCount > 0) {
        TerrainStamp* base = hfv->basinStamps._data;   // param_1[0xe]
        for (int i = 0; i < listcCount; ++i) {
            TerrainStamp* rec = &base[i];   // stride 0xc
            float center[2];
            center[0] = rec->x;
            center[1] = rec->y;
            float radius = rec->radius;
            // eee0(center, cutoff=radius, base=2.0, strength=2.25, mode 1 -> subMask)
            wg_eee0_radial_brush(hfv, center, radius, kListCExp, kListCStrength, 1, subMaskHdr);
        }
    }

    // Finalize carve: h[i] = mulMask[i] * h[i] - subMask[i].
    float* hm = hfv->heightmap.data;          // param_1[2]
    if (hm && mulMask && subMask) {
        for (int i = 0; i < cellCount; ++i) {
            hm[i] = mulMask[i] * hm[i] - subMask[i];
        }
    }

    {
        System::MemoryAllocator::MemFree(subHdr.data);   // frees local_90 first (matches disasm order)
        System::MemoryAllocator::MemFree(mulHdr.data);   // then local_a0
    }
}

// ============================================================================
// wg_f390_smooth == ConsumeProduceComponent_impl_c350 — WEIGHTED BOX BLUR (one pass).
//   @ 0x1400cc350   (f390 calls it twice — orchestrator repeats it)
//
// Disassembly-exact kernel (re-derived from 0x1400cc350 asm, NOT the flattened
// decompiler output). Outer loop y = EBP in [0, dim); inner loop x = EBX in [0, dim).
// Indices into the heightmap use row stride edge = dim+1, with every index CLAMPED
// to [0, dim] (edge-replicate). For each (x, y):
//
//   centre term:  accum = h[ edge*clamp(y) + clamp(x) ] * 2.0   (kCenterWeight)
//                 wsum  = 2.0
//   neighbour loop (R10 = -1, 0, +1; SKIPS R10 == 0):  column is FIXED at clamp(x-1),
//                 row = clamp(y + R10)  ->  contributes (x-1, y-1) and (x-1, y+1):
//                 accum += h[ edge*clamp(y-1) + clamp(x-1) ]
//                 accum += h[ edge*clamp(y+1) + clamp(x-1) ]
//                 wsum  += 1.0  (twice)   -> wsum total = 4.0
//   out[ edge*y + x ] = accum / wsum
//
// So the kernel is the ASYMMETRIC 3-tap stencil
//       { (x,y):2.0,  (x-1,y-1):1.0,  (x-1,y+1):1.0 } / 4.0
// (the neighbour column is x-1 only — verified in asm: `LEA R8D,[RBX-1]` is the
// only column ever formed in the inner loop; the centre uses x). Results go to a
// temp buffer (sub_3a00, sized param_1[4]) laid out edge-wide, then the first
// param_1[4] dwords are copied back over the heightmap.
//
// HEIGHT EFFECT — a normalized weighted average (Σw·h / Σw, all w > 0): it is
// approximately mean-PRESERVING for a locally-flat field. Because the kernel is
// biased toward the (x-1) column, it shifts/diffuses height very slightly toward
// +x and rounds local curvature, but the divisor (4.0) exactly matches the weight
// sum (2+1+1) so a constant field maps to itself (no offset). It shrinks local
// extrema (peaks down, pits up); the global mean drifts only at second order near
// curved/edge regions (edge-replicate clamping barely pulls border cells inward).
// Running twice doubles the diffusion but not any bias.
// => smoothing is NOT the source of the ~8-unit global lowering.
// ============================================================================
void wg_f390_smooth(TerrainHeightfield* hfv) {
    int cellCount = hfv->heightmap.size;    // param_1[4]

    // Temp output buffer. Retail 0x1400cc388 calls Array<float>::SetSize(n) (0x140023a00), which leaves
    // the buffer as the allocation found it; the loop below writes only [0, dim) of each edge-wide row
    // and the copy-back (retail 0x1400cc4b0) reads all cellCount floats, so retail moves uninitialised
    // floats into the last column and row of the heightmap. The port keeps them 0.0f (the fill form)
    // until the user decides (known_port_issues row 1162).
    RawArray<float> tmpHdr = {nullptr, 0, 0};
    RawArraySetSize(tmpHdr, cellCount, 0.0f);
    float* tmp = tmpHdr.data;

    int dim  = hfv->gridSize;                  // *param_1
    int edge = dim + 1;                        // iVar3 = iVar6 + 1 (row stride)
    float* hm = hfv->heightmap.data;           // *(longlong*)(param_1+2)

    auto clamp = [&](int v) { return v < 0 ? 0 : (v > dim ? dim : v); };

    if (tmp && hm) {
        for (int y = 0; y < dim; ++y) {
            // out index advances 1 per col, `edge` per row (tmp is edge-wide;
            // only [0,dim) per row is written, matching the disasm RSI walk).
            float* outRow = tmp + (int64_t)y * edge;
            for (int x = 0; x < dim; ++x) {
                int cyC = clamp(y);
                int cxC = clamp(x);
                float accum = hm[(int64_t)edge * cyC + cxC] * kCenterWeight;   // centre * 2.0
                float wsum  = kCenterWeight;

                int colL = clamp(x - 1);                  // fixed neighbour column = x-1
                // R10 = -1 -> row = y-1 ; R10 = +1 -> row = y+1 (R10 = 0 skipped)
                accum += hm[(int64_t)edge * clamp(y - 1) + colL];
                wsum  += kOne;
                accum += hm[(int64_t)edge * clamp(y + 1) + colL];
                wsum  += kOne;

                outRow[x] = accum / wsum;
            }
        }

        // Copy temp back over the heightmap (cellCount floats).
        for (int i = 0; i < cellCount; ++i) hm[i] = tmp[i];
    }

    System::MemoryAllocator::MemFree(tmpHdr.data);
}
