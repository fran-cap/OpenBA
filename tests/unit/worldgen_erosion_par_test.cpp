// ============================================================================
// tests/unit/worldgen_erosion_par_test.cpp -- EROSIONPAR: the parallel droplet erosion ==
// the sequential body, bit for bit, for every worker count and window
// ============================================================================
// Block EROSIONPAR (docs/extensions/handins/EROSIONPAR.md) runs wg_f390_erosion's droplets
// speculatively on the System::ParallelFor pool and commits them in droplet order. These tests
// run wg_f390_erosion_tuned with 1 / 2 / 4 / 7 workers and several lookahead windows and step
// caps against wg_f390_erosion_sequential (the 0-worker path; worldgen_erosion_test.cpp proves it
// equal to the pre-EROSION body) on the EROSION inputs -- value noise with sea, ramps (every
// direction, the 0.8 clamp), tie fields, special values -- and compare the whole heightmap and
// the whole Random state byte for byte.
//
// The stress tests force the slow paths and check their counters, so the proof covers them:
//   * conflicts: tiny grids (every droplet reads every cell), a bowl (every droplet drains to the
//     middle), ramps with a wide window;
//   * overflows: step cap 1 / 2 (every speculation of a moving droplet gives up);
//   * an unbounded footprint: a commit-run droplet longer than the log (step cap 1 on a ramp);
//   * SOLO: conflict-heavy blocks make the commit run alone, then probe again;
//   * the commit outrunning the speculators: lookahead 1; the commit taking droplets: gap.
// ============================================================================
#include "unit.h"
#include "Game/TerrainHeightfield.h"
#include "Game/worldgen_erosion_par.h"
#include "system/parallel_for.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace {

union FieldStorage {
    FieldStorage() {}
    ~FieldStorage() {}
    TerrainHeightfield hf;
};

struct Outcome {
    std::vector<float> heights;
    uint8_t random[sizeof(System::Random)];
    WgErosionStats stats;
};

// One run on a copy of `heights`: the sequential body (tuning == nullptr) or the tuned one.
Outcome Run(const WgErosionTuning* tuning, int dim, const std::vector<float>& heights, uint32_t seed, int iters) {
    FieldStorage storage;
    TerrainHeightfield* hf = new (&storage.hf) TerrainHeightfield();
    Outcome out;
    out.heights = heights;
    TerrainSizeRow row{};
    row.erosionIterations = iters;
    hf->gridSize = dim;
    hf->heightmap.data = out.heights.data();
    hf->heightmap.size = static_cast<int32_t>(out.heights.size());
    hf->heightmap.capacity = hf->heightmap.size;
    new (&hf->random) System::Random(seed);
    hf->sizeRow = &row;
    if (tuning) wg_f390_erosion_tuned(hf, *tuning, &out.stats);
    else wg_f390_erosion_sequential(hf);
    std::memcpy(out.random, &hf->random, sizeof(out.random));
    return out;
}

bool Same(const Outcome& a, const Outcome& b) {
    return a.heights.size() == b.heights.size() &&
           std::memcmp(a.heights.data(), b.heights.data(), a.heights.size() * sizeof(float)) == 0 &&
           std::memcmp(a.random, b.random, sizeof(a.random)) == 0;
}

WgErosionTuning Tuning(int lookahead, int stepCap) {
    WgErosionTuning t;
    t.lookahead = lookahead;
    t.stepCap = stepCap;
    t.minParallel = 0;   // always the parallel path
    t.speculators = 0;
    t.gap = 0;
    return t;
}

// Totals over every run of a test, to check the slow paths were taken.
struct Totals {
    long long spec = 0, conflicts = 0, overflows = 0, unspeculated = 0, cellChecks = 0, solo = 0;
    void Add(const WgErosionStats& s) {
        spec += s.committedSpec;
        conflicts += s.conflicts;
        overflows += s.overflows;
        unspeculated += s.unspeculated;
        cellChecks += s.cellChecks;
        solo += s.solo;
    }
};

const int kWorkers[] = {1, 2, 4, 7};

// The tuned path with every worker count and every tuning in `tunings` == the sequential body.
bool AllMatch(int dim, const std::vector<float>& heights, uint32_t seed, int iters,
              const std::vector<WgErosionTuning>& tunings, Totals* totals) {
    const Outcome ref = Run(nullptr, dim, heights, seed, iters);
    bool ok = true;
    for (int workers : kWorkers) {
        System::Parallel::SetWorkers(workers);
        for (const WgErosionTuning& t : tunings) {
            const Outcome now = Run(&t, dim, heights, seed, iters);
            if (!Same(ref, now)) ok = false;
            if (now.stats.workers != workers) ok = false;
            const WgErosionStats& s = now.stats;
            if (s.committedSpec + s.conflicts + s.overflows + s.unspeculated != iters) ok = false;
            if (totals) totals->Add(s);
        }
    }
    return ok;
}

// The game's tuning (forced parallel at these small sizes) and windows / caps / gaps around it.
std::vector<WgErosionTuning> StandardTunings() {
    WgErosionTuning game = WgErosionDefaultTuning();
    game.minParallel = 0;
    WgErosionTuning noGap = game, wideGap = game;
    noGap.gap = 0;
    wideGap.gap = wideGap.lookahead;
    return {game, noGap, wideGap, Tuning(1, 1024), Tuning(4, 1024), Tuning(24, 1024), Tuning(127, 1024),
            Tuning(16, 3)};
}

struct Rng {
    uint64_t s;
    uint64_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    uint32_t U32() { return static_cast<uint32_t>(Next() >> 32); }
    float Uniform(float lo, float hi) {
        return lo + (hi - lo) * static_cast<float>(U32() >> 8) * (1.0f / 16777216.0f);
    }
    int Below(int n) { return static_cast<int>(U32() % static_cast<uint32_t>(n)); }
};

float FromBits(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }

std::vector<float> Noise(int dim, uint64_t seed, float scale, float offset) {
    const int edge = dim + 1;
    Rng r{seed};
    const int cells = 8;
    std::vector<float> lattice((cells + 2) * (cells + 2));
    for (float& v : lattice) v = r.Uniform(0.0f, 1.0f);
    std::vector<float> h(static_cast<size_t>(edge) * edge);
    for (int y = 0; y < edge; ++y)
        for (int x = 0; x < edge; ++x) {
            const float fx = static_cast<float>(x) * cells / edge, fy = static_cast<float>(y) * cells / edge;
            const int xi = static_cast<int>(fx), yi = static_cast<int>(fy);
            const float tx = fx - xi, ty = fy - yi;
            const float a = lattice[yi * (cells + 2) + xi], b = lattice[yi * (cells + 2) + xi + 1];
            const float c = lattice[(yi + 1) * (cells + 2) + xi], d = lattice[(yi + 1) * (cells + 2) + xi + 1];
            const float v = (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
            h[static_cast<size_t>(y) * edge + x] = v * scale - offset + r.Uniform(0.0f, 0.5f);
        }
    return h;
}

std::vector<float> Ramp(int dim, int variant, float sx, float sy) {
    const int edge = dim + 1;
    std::vector<float> h(static_cast<size_t>(edge) * edge);
    for (int y = 0; y < edge; ++y)
        for (int x = 0; x < edge; ++x) {
            const int xx = (variant & 1) ? dim - x : x, yy = (variant & 2) ? dim - y : y;
            h[static_cast<size_t>(y) * edge + x] = 10.0f + sx * xx + sy * yy;
        }
    return h;
}

}  // namespace

UNIT_TEST(erosion_par_noise_matches_sequential) {
    Totals tot;
    // Each AllMatch is 33 erosion runs (the reference, then 4 worker counts x 8 tunings); the droplet
    // counts were halved from 40000 / 20000 for CI time. The slow paths are forced by the stress test
    // below, not by droplet volume here.
    for (uint32_t seed : {1u, 0xBADC0DEu}) {
        CHECK(AllMatch(128, Noise(128, seed * 7919ull + 1, 60.0f, 18.0f), seed, 20000, StandardTunings(), &tot));
        CHECK(AllMatch(47, Noise(47, seed * 104729ull + 3, 30.0f, 5.0f), seed, 10000, StandardTunings(), &tot));
    }
    CHECK(tot.spec > 0);
    CHECK(tot.conflicts > 0);
    CHECK(tot.cellChecks > 0);
    System::Parallel::Shutdown();
}

UNIT_TEST(erosion_par_ramps_match_sequential) {
    Totals tot;
    CHECK(AllMatch(96, Ramp(96, 0, 3.0f, 1.0f), 99u, 3000, StandardTunings(), &tot));
    for (int variant = 0; variant < 4; ++variant)   // every direction, steep: the 0.8 clamp
        CHECK(AllMatch(96, Ramp(96, variant, 40.0f, 0.25f), 1000u + variant, 2000, StandardTunings(), &tot));
    CHECK(tot.conflicts > 0);
    CHECK(tot.overflows > 0);   // Tuning(16, 3): ramp droplets walk far
    System::Parallel::Shutdown();
}

UNIT_TEST(erosion_par_ties_match_sequential) {
    const int dim = 40, edge = dim + 1;
    Rng r{12345};
    for (int round = 0; round < 3; ++round) {
        std::vector<float> h(static_cast<size_t>(edge) * edge);
        for (float& v : h) v = 1.0f + static_cast<float>(r.Below(4));
        CHECK(AllMatch(dim, h, 77u + round, 20000, StandardTunings(), nullptr));
    }
    System::Parallel::Shutdown();
}

UNIT_TEST(erosion_par_special_values_match_sequential) {
    const int dim = 33, edge = dim + 1;
    const float specials[] = {
        0.0f, -0.0f, FromBits(0x00000001u), FromBits(0x80000001u), FromBits(0x007fffffu),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
        FromBits(0x7fc00000u), FromBits(0xffc00000u), FromBits(0x7f800001u), FromBits(0xff812345u),
        std::numeric_limits<float>::min(), 1e-30f, 1.0e6f};
    // Not salted: finite heights above ~2^27 (the retail endless droplet, worldgen_erosion_test.cpp).
    Rng r{777};
    for (int round = 0; round < 12; ++round) {
        std::vector<float> h = Noise(dim, 31337ull + round, 20.0f, -2.0f);
        const int salt = 1 + r.Below(60);
        for (int i = 0; i < salt; ++i)
            h[static_cast<size_t>(r.Below(edge * edge))] = specials[r.Below(sizeof(specials) / sizeof(specials[0]))];
        CHECK(AllMatch(dim, h, 5000u + round, 4000, StandardTunings(), nullptr));
    }
    System::Parallel::Shutdown();
}

UNIT_TEST(erosion_par_stress_forced_conflicts_and_overflows) {
    Totals tot;
    // dim 2 / 3 / 4: every droplet starts within a cell or two of every other -- nearly every
    // speculation in a window conflicts.
    const std::vector<WgErosionTuning> wide = {Tuning(127, 1024), Tuning(32, 1), Tuning(8, 2), Tuning(1, 1024)};
    CHECK(AllMatch(2, {5.0f, 4.0f, 6.0f, 3.0f, 9.0f, 2.0f, 7.0f, 1.0f, 8.0f}, 3u, 5000, wide, &tot));
    CHECK(AllMatch(3, Noise(3, 5, 10.0f, -1.0f), 4u, 5000, wide, &tot));
    CHECK(AllMatch(4, Noise(4, 6, 10.0f, -1.0f), 5u, 5000, wide, &tot));
    // A converging field: a bowl, every droplet drains to the middle and they all overlap.
    {
        const int dim = 64, edge = dim + 1;
        std::vector<float> bowl(static_cast<size_t>(edge) * edge);
        for (int y = 0; y < edge; ++y)
            for (int x = 0; x < edge; ++x) {
                const float dx = static_cast<float>(x - dim / 2), dy = static_cast<float>(y - dim / 2);
                bowl[static_cast<size_t>(y) * edge + x] = 5.0f + 0.05f * (dx * dx + dy * dy);
            }
        CHECK(AllMatch(dim, bowl, 6u, 20000, wide, &tot));
    }
    // Step cap 1 on a ramp: every commit-run droplet stores far more than the log holds
    // (2 entries), so its footprint is unbounded and every droplet in flight after it re-runs.
    CHECK(AllMatch(96, Ramp(96, 1, 3.0f, 1.0f), 8u, 3000, {Tuning(16, 1)}, &tot));
    // Randomised: seeds x windows x caps on noise.
    Rng r{4242};
    for (int round = 0; round < 6; ++round) {
        const int dim = 16 + r.Below(80);
        const std::vector<WgErosionTuning> t = {Tuning(1 + r.Below(127), 1 + r.Below(64))};
        CHECK(AllMatch(dim, Noise(dim, 900 + round, 40.0f, 10.0f), 7000u + round, 10000, t, &tot));
    }
    CHECK(tot.conflicts > 1000);
    CHECK(tot.overflows > 1000);
    CHECK(tot.spec > 0);
    CHECK(tot.solo > 0);
    System::Parallel::Shutdown();
}

UNIT_TEST(erosion_par_noop_arms_and_small_runs) {
    // No size row / 0 / negative iterations / below minParallel: the sequential body, no draw.
    const std::vector<float> field = Noise(16, 5, 10.0f, 1.0f);
    System::Parallel::SetWorkers(4);
    for (int iters : {0, -5, 1, 7}) {
        WgErosionTuning t = Tuning(8, 64);
        const Outcome ref = Run(nullptr, 16, field, 9u, iters);
        const Outcome now = Run(&t, 16, field, 9u, iters);
        CHECK(Same(ref, now));
        t.minParallel = 100;   // below it: sequential, no workers
        const Outcome seq = Run(&t, 16, field, 9u, iters);
        CHECK(Same(ref, seq));
        CHECK_EQ(seq.stats.workers, 0);
    }
    System::Parallel::Shutdown();
}
