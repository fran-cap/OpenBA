// ============================================================================
// src/game/Game/worldgen_erosion_par.h -- the exact parallel droplet erosion (EROSIONPAR)
// ============================================================================
// NOT RETAIL (loading-speed block EROSIONPAR, docs/extensions/handins/EROSIONPAR.md).
// wg_f390_erosion (TerrainHeightfield.h, retail 0x1400cc080) runs its droplets
// speculatively on the System::ParallelFor pool and commits them strictly in droplet
// order; the heightmap and the Random state come out bit-identical to the sequential
// body for every input and every worker count. This header exposes the knobs and the
// counters for the unit tests and tools/erosion_bench only: game code calls
// wg_f390_erosion, which uses WgErosionDefaultTuning().
// ============================================================================
#pragma once

#include <cstdint>

struct TerrainHeightfield;

struct WgErosionTuning {
    int lookahead;     // droplets drawn ahead of the commit (the speculation window), 1 .. kWgErosionRing/2-1
    int stepCap;       // a speculation longer than this many steps gives up (the commit runs it)
    int minParallel;   // fewer droplets than this run the sequential body
    int speculators;   // speculating workers, at most the pool's (0 = all of them)
    int gap;           // speculators leave droplets < committed + gap to the commit (it runs them)
    // Unit tests only: the commit leaves no droplet to itself outside SOLO -- it waits for a
    // speculator to claim and finish droplet j, and for droplet j + 1's speculation to finish before
    // it commits j, so that speculation started before j was in the heightmap. The slow paths
    // (conflicts, overflows, SOLO) are then taken whatever the scheduler does. Needs gap 0 (ignored
    // otherwise) and a pool call that is not nested (every role on its own thread).
    bool forceSpec = false;
};

constexpr int kWgErosionRing = 256;   // droplet slots (lookahead < half of this)

struct WgErosionStats {
    int       workers = 0;          // speculating threads (0 = the sequential body ran)
    long long droplets = 0;
    long long committedSpec = 0;    // speculation validated and applied
    long long conflicts = 0;        // speculation read a cell an in-flight droplet wrote: re-run
    long long overflows = 0;        // speculation hit stepCap: run by the commit
    long long unspeculated = 0;     // the commit got there first: run by the commit
    long long cellChecks = 0;       // validations whose footprint boxes overlapped (cell-level check)
    long long solo = 0;             // droplets committed while the commit ran alone (conflict-heavy blocks)
};

WgErosionTuning WgErosionDefaultTuning();

// The parallel path with explicit knobs; `stats` may be null. With 0 pool workers, or fewer
// than tuning.minParallel droplets, it runs wg_f390_erosion_sequential.
void wg_f390_erosion_tuned(TerrainHeightfield* hf, const WgErosionTuning& tuning, WgErosionStats* stats);

// The single-thread body (block EROSION's), the reference and the 0-worker path.
void wg_f390_erosion_sequential(TerrainHeightfield* hf);
