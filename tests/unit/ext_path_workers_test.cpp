// ============================================================================
// tests/unit/ext_path_workers_test.cpp -- ext.path_workers' pool vs the serial worker
// ============================================================================
// src/ext/path_workers/pool.{h,cpp} on its own, driven by std::thread (the game
// drives the same WorkerMain from System::Thread). The mock pathfinder below is
// retail's worker reduced to what the protocol touches: a flag byte per cell,
// the parameter record commands 0 / 1 / 4 write (ApplyCommand 0x1400a6430's
// cases), and a result table. The search is GridSearch::Searcher -- at scale 1
// it is retail's search (ext_fast_paths_test proves it), and with the real
// scale it reads the present-type mask, so the ext.fast_paths composition
// (the mask taken at dispatch) is exercised too.
//
//   (i)   random command streams (parameter commands, cell edits, requests,
//         interleaved) through the pool with N = 1..4 give, per result slot,
//         exactly the serial worker's cells and count
//   (ii)  no cell edit is ever applied while a job is queued or running, and
//         no job sees the grid change under it (the barrier)
//   (iii) Stop under load: every submitted job still runs exactly once, every
//         thread returns; Stop before the threads start; Stop on an idle pool
//   (iv)  the worker count rule
// ============================================================================
#include "unit.h"
#include "ext/path_workers/pool.h"
#include "Action/grid_search.h"
#include "Game/PathType.h"   // PathBits (the walk masks)

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using Ext::PathWorkers::Command;
using Ext::PathWorkers::DispatchCommand;
using Ext::PathWorkers::Job;
using Ext::PathWorkers::Params;
using Ext::PathWorkers::Pool;

namespace {

// ---------------------------------------------------------------------------
// The mock pathfinder.
// ---------------------------------------------------------------------------
struct MockGrid {
    int width = 0;
    std::vector<uint8_t> cells;        // GridPathSearch::cellFlags
    int32_t  stepCost[0x20] = {};
    uint32_t passableTypes = 0;
    uint8_t  allowDiagonal = 0, manhattan = 0, respectBlocked = 0;

    // ApplyCommand 0x1400a6430, cases 0-4.
    void Apply(const Command& c) {
        switch (c.type) {
        case 0: passableTypes = static_cast<uint32_t>(c.arg1); break;
        case 1: stepCost[c.arg1] = c.arg2; break;
        case 2:
            cells[c.arg1] = static_cast<uint8_t>((cells[c.arg1] & GridSearch::kCellBlocked) |
                                                 static_cast<uint8_t>(c.arg2));
            break;
        case 3:
            cells[c.arg1] = static_cast<uint8_t>((static_cast<uint8_t>(-(c.arg2 != 0)) & GridSearch::kCellBlocked) |
                                                 (cells[c.arg1] & GridSearch::kCellTypeMask));
            break;
        case 4: allowDiagonal = c.arg1 != 0; manhattan = c.arg2 != 0; respectBlocked = c.arg3 != 0; break;
        default: break;
        }
    }

    Params Snapshot(bool fast) const {
        Params p;
        std::memcpy(p.stepCost, stepCost, sizeof(p.stepCost));
        p.passableTypes = passableTypes;
        p.allowDiagonal = allowDiagonal;
        p.manhattan = manhattan;
        p.respectBlocked = respectBlocked;
        p.fast = fast ? 1 : 0;
        p.presentTypes = fast ? GridSearch::PresentTypes(cells.data(), static_cast<int>(cells.size())) : 0u;
        return p;
    }
};

struct Result {
    int count = -2;              // -2 = never published
    std::vector<int32_t> cells;
    int publishes = 0;
};

// The search one request runs: retail's at scale 1, ext.fast_paths' when `fast`.
int Search(GridSearch::Searcher& s, const Params& p, int width, const uint8_t* cells,
           int start, int goal, int32_t* out) {
    GridSearch::SearchInput in;
    in.width = width;
    in.cellFlags = cells;
    in.stepCost = p.stepCost;
    in.passableTypes = p.passableTypes;
    in.allowDiagonal = p.allowDiagonal != 0;
    in.manhattan = p.manhattan != 0;
    in.respectBlocked = p.respectBlocked != 0;
    in.start = start;
    in.goal = goal;
    in.heuristicScale = p.fast ? GridSearch::HeuristicScale(p.stepCost, p.passableTypes, p.presentTypes) : 1;
    if (in.heuristicScale < 0) return -1;
    const GridSearch::SearchResult r = s.Run(in, out);
    return r.status == GridSearch::Status::Unsupported ? -1 : r.count;
}

uint64_t Checksum(const std::vector<uint8_t>& v) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : v) { h ^= b; h *= 1099511628211ull; }
    return h;
}

// One randomised case: a grid, its starting parameters, a command stream.
struct Case {
    MockGrid start;
    std::vector<Command> commands;
    int slots = 0;
    bool fast = false;
};

Case MakeCase(std::mt19937& rng) {
    Case c;
    const int w = 4 + static_cast<int>(rng() % 37);   // 4..40
    c.start.width = w;
    c.start.cells.resize(static_cast<size_t>(w) * w);
    static const uint8_t kTypes[] = { 0, 1, 2, 2, 2, 3, 4, 7, 8, 10, 10, 11 };
    for (uint8_t& b : c.start.cells) {
        b = kTypes[rng() % sizeof(kTypes)];
        if (rng() % 10 == 0) b |= GridSearch::kCellBlocked;
    }
    for (int t = 0; t < 0x20; ++t) c.start.stepCost[t] = 1;   // the grid ctor's
    static const int32_t kProfile0[11] = { 3, 3, 4, 8, 16, 16, 16, 16, 16, 3, 4 };
    for (int t = 0; t < 11; ++t) c.start.stepCost[t] = kProfile0[t];
    c.start.passableTypes = Bits(PathBits::Walk);
    c.start.allowDiagonal = 1;
    c.fast = rng() % 2 == 0;

    static const int32_t kMasks[] = { Bits(PathBits::Walk), 0x7, 0x1ff, 0x404 | 0x4, -1,
                                      Bits(PathBits::AnimalWalk) };
    const int n = 20 + static_cast<int>(rng() % 120);
    const int cellsN = w * w;
    for (int i = 0; i < n; ++i) {
        const unsigned r = rng() % 100;
        Command cmd{};
        if (r < 50) {
            cmd = { 5, static_cast<int32_t>(rng() % cellsN), static_cast<int32_t>(rng() % cellsN), c.slots++ };
        } else if (r < 58) {
            cmd = { 0, kMasks[rng() % (sizeof(kMasks) / sizeof(kMasks[0]))], 0, 0 };
        } else if (r < 68) {
            cmd = { 1, static_cast<int32_t>(rng() % 12), 1 + static_cast<int32_t>(rng() % 16), 0 };
        } else if (r < 80) {
            cmd = { 2, static_cast<int32_t>(rng() % cellsN), static_cast<int32_t>(kTypes[rng() % sizeof(kTypes)]), 0 };
        } else if (r < 88) {
            cmd = { 3, static_cast<int32_t>(rng() % cellsN), static_cast<int32_t>(rng() % 2), 0 };
        } else {
            cmd = { 4, static_cast<int32_t>(rng() % 2), static_cast<int32_t>(rng() % 2), static_cast<int32_t>(rng() % 4 == 0) };
        }
        c.commands.push_back(cmd);
    }
    return c;
}

// The serial worker: retail's Run loop, every command in order on one thread.
std::vector<Result> RunSerial(const Case& c) {
    MockGrid g = c.start;
    std::vector<Result> out(static_cast<size_t>(c.slots));
    GridSearch::Searcher s;
    std::vector<int32_t> buf(GridSearch::kPathCap);
    for (const Command& cmd : c.commands) {
        if (cmd.type != 5) { g.Apply(cmd); continue; }
        const Params p = g.Snapshot(c.fast);
        const int n = Search(s, p, g.width, g.cells.data(), cmd.arg1, cmd.arg2, buf.data());
        Result& r = out[static_cast<size_t>(cmd.arg3)];
        r.count = n;
        r.cells.assign(buf.begin(), buf.begin() + (n > 0 ? n : 0));
        ++r.publishes;
    }
    return out;
}

// ---------------------------------------------------------------------------
// The pool side: a runner with one Searcher per worker, and the dispatcher host.
// ---------------------------------------------------------------------------
struct PoolCase {
    MockGrid grid;
    std::vector<Result> results;
    std::mutex lock;   // the pathfinder's lock: the publish runs under it
};

class TestRunner final : public Ext::PathWorkers::Runner {
public:
    explicit TestRunner(int workers) {
        for (int i = 0; i < workers; ++i) {
            searchers.emplace_back(new GridSearch::Searcher());
            buffers.emplace_back(GridSearch::kPathCap);
        }
    }
    PoolCase* pc = nullptr;   // swapped only while the pool is idle
    std::atomic<int> running{0};
    std::atomic<int64_t> gridChangedUnderJob{0};
    std::vector<std::unique_ptr<GridSearch::Searcher>> searchers;
    std::vector<std::vector<int32_t>> buffers;
    unsigned shake = 0;       // > 0: yield now and then, to vary completion order

    void RunJob(int worker, const Job& job) override {
        running.fetch_add(1);
        const uint64_t before = Checksum(pc->grid.cells);
        if (shake && (static_cast<unsigned>(job.slot) * 2654435761u) % shake == 0) std::this_thread::yield();
        std::vector<int32_t>& buf = buffers[static_cast<size_t>(worker)];
        const int n = Search(*searchers[static_cast<size_t>(worker)], job.params, pc->grid.width,
                             pc->grid.cells.data(), job.start, job.goal, buf.data());
        if (Checksum(pc->grid.cells) != before) gridChangedUnderJob.fetch_add(1);
        {
            std::lock_guard<std::mutex> lk(pc->lock);
            Result& r = pc->results[static_cast<size_t>(job.slot)];
            r.count = n;
            r.cells.assign(buf.begin(), buf.begin() + (n > 0 ? n : 0));
            ++r.publishes;
        }
        running.fetch_sub(1);
    }
};

struct TestHost {
    PoolCase& pc;
    Pool& pool;
    TestRunner& runner;
    bool fast;
    int64_t editsWhileBusy = 0;

    void Apply(const Command& c) {
        if ((c.type == 2 || c.type == 3) && (pool.InFlight() != 0 || runner.running.load() != 0))
            ++editsWhileBusy;
        pc.grid.Apply(c);
    }
    Job MakeJob(const Command& c) {
        Job j;
        j.params = pc.grid.Snapshot(fast);
        j.start = c.arg1;
        j.goal = c.arg2;
        j.slot = c.arg3;
        return j;
    }
};

struct RunningPool {
    TestRunner runner;
    Pool pool;
    std::vector<std::thread> threads;
    explicit RunningPool(int n) : runner(n), pool(runner, n) {
        for (int i = 0; i < n; ++i) threads.emplace_back([this, i] { pool.WorkerMain(i); });
    }
    ~RunningPool() {
        pool.Stop();
        for (std::thread& t : threads) t.join();
    }
};

bool SameResults(const std::vector<Result>& a, const std::vector<Result>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].count != b[i].count || a[i].cells != b[i].cells || a[i].publishes != 1 || b[i].publishes != 1)
            return false;
    return true;
}

}  // namespace

UNIT_TEST(ext_path_workers_pool_matches_serial) {
    // 750 streams and their serial results, made once; each pool size (N = 1..4) runs every stream.
    // (The serial reference per stream was ~40% of this test's time when every N drew its own 750.)
    std::mt19937 rng(0x9a7b1u);
    std::vector<Case> streams;
    std::vector<std::vector<Result>> serials;
    for (int k = 0; k < 750; ++k) {
        streams.push_back(MakeCase(rng));
        serials.push_back(RunSerial(streams.back()));
    }
    int64_t cases = 0, requests = 0, mismatches = 0, edits = 0, editsBusy = 0, changed = 0, found = 0;
    for (int n = 1; n <= Ext::PathWorkers::kMaxWorkers; ++n) {
        RunningPool rp(n);
        rp.runner.shake = n == 1 ? 0u : 7u;
        for (int k = 0; k < static_cast<int>(streams.size()); ++k) {
            const Case& c = streams[static_cast<size_t>(k)];
            const std::vector<Result>& serial = serials[static_cast<size_t>(k)];

            PoolCase pc;
            pc.grid = c.start;
            pc.results.assign(static_cast<size_t>(c.slots), Result());
            rp.runner.pc = &pc;   // the pool is idle here (the last case ended in a Barrier)
            TestHost host{pc, rp.pool, rp.runner, c.fast};
            for (const Command& cmd : c.commands) {
                DispatchCommand(rp.pool, host, cmd);
                if (cmd.type == 2 || cmd.type == 3) ++edits;
            }
            rp.pool.Barrier();

            ++cases;
            requests += c.slots;
            for (const Result& r : serial) found += r.count > 0;
            editsBusy += host.editsWhileBusy;
            if (!SameResults(serial, pc.results)) {
                if (mismatches == 0)
                    std::printf("  first mismatch: N %d case %d width %d slots %d fast %d\n",
                                n, k, c.start.width, c.slots, c.fast ? 1 : 0);
                ++mismatches;
            }
        }
        changed += rp.runner.gridChangedUnderJob.load();
    }
    std::printf("  pool vs serial: %ld cases (N = 1..4), %ld requests (%ld found), %ld cell edits; "
                "%ld mismatches, %ld edits while busy, %ld jobs saw the grid change\n",
                cases, requests, found, edits, mismatches, editsBusy, changed);
    CHECK_EQ(mismatches, 0L);
    CHECK_EQ(editsBusy, 0L);
    CHECK_EQ(changed, 0L);
    CHECK(requests > 100000);
    CHECK(found > requests / 4);
}

// The barrier carries the result: an edit stream with NO barrier would let a
// job see a later edit. Check the case generator produces streams where that
// matters, so the test above is not vacuous: replaying each stream with the
// edits applied BEFORE all requests changes some results.
UNIT_TEST(ext_path_workers_edits_matter) {
    std::mt19937 rng(0x51u);
    int64_t differ = 0, cases = 0;
    for (int k = 0; k < 300; ++k) {
        Case c = MakeCase(rng);
        const std::vector<Result> serial = RunSerial(c);
        Case hoisted = c;
        hoisted.commands.clear();
        for (const Command& cmd : c.commands) if (cmd.type == 2 || cmd.type == 3) hoisted.commands.push_back(cmd);
        for (const Command& cmd : c.commands) if (cmd.type != 2 && cmd.type != 3) hoisted.commands.push_back(cmd);
        const std::vector<Result> other = RunSerial(hoisted);
        ++cases;
        differ += SameResults(serial, other) ? 0 : 1;
    }
    std::printf("  edits matter: %ld / %ld streams change when the edits are moved ahead\n", differ, cases);
    CHECK(differ > cases / 10);
}

UNIT_TEST(ext_path_workers_stop_under_load) {
    // A 64x64 open grid; 4000 requests submitted, Stop at once.
    PoolCase pc;
    pc.grid.width = 64;
    pc.grid.cells.assign(64 * 64, 2);
    for (int t = 0; t < 0x20; ++t) pc.grid.stepCost[t] = 4;
    pc.grid.passableTypes = Bits(PathBits::Walk);
    pc.grid.allowDiagonal = 1;
    const int kJobs = 4000;
    pc.results.assign(kJobs, Result());

    TestRunner runner(4);
    runner.pc = &pc;
    Pool pool(runner, 4);
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([&pool, i] { pool.WorkerMain(i); });
    std::mt19937 rng(7);
    for (int s = 0; s < kJobs; ++s) {
        Job j;
        j.params = pc.grid.Snapshot(false);
        j.start = static_cast<int32_t>(rng() % 4096);
        j.goal = static_cast<int32_t>(rng() % 4096);
        j.slot = s;
        pool.Submit(j);
    }
    pool.Stop();
    for (std::thread& t : threads) t.join();
    int64_t once = 0;
    for (const Result& r : pc.results) once += r.publishes == 1;
    CHECK_EQ(once, static_cast<int64_t>(kJobs));
    CHECK_EQ(pool.Completed(), static_cast<long long>(kJobs));
    CHECK_EQ(pool.InFlight(), 0);
    pool.Submit(Job{});   // after Stop: ignored
    CHECK_EQ(pool.InFlight(), 0);
    std::printf("  stop under load: %lld / %d jobs ran exactly once, 4 threads joined\n",
                pool.Completed(), kJobs);
}

UNIT_TEST(ext_path_workers_stop_before_start_and_idle) {
    PoolCase pc;
    pc.grid.width = 16;
    pc.grid.cells.assign(256, 2);
    for (int t = 0; t < 0x20; ++t) pc.grid.stepCost[t] = 4;
    pc.grid.passableTypes = Bits(PathBits::Walk);
    pc.results.assign(100, Result());
    {
        // Jobs queued and Stop called before any thread exists: each still runs.
        TestRunner runner(3);
        runner.pc = &pc;
        Pool pool(runner, 3);
        for (int s = 0; s < 100; ++s) {
            Job j;
            j.params = pc.grid.Snapshot(false);
            j.start = s;
            j.goal = 255 - s;
            j.slot = s;
            pool.Submit(j);
        }
        pool.Stop();
        std::vector<std::thread> threads;
        for (int i = 0; i < 3; ++i) threads.emplace_back([&pool, i] { pool.WorkerMain(i); });
        for (std::thread& t : threads) t.join();
        CHECK_EQ(pool.Completed(), 100LL);
    }
    {
        // An idle pool: Barrier returns at once, Stop releases the parked threads.
        TestRunner runner(2);
        runner.pc = &pc;
        Pool pool(runner, 2);
        std::vector<std::thread> threads;
        for (int i = 0; i < 2; ++i) threads.emplace_back([&pool, i] { pool.WorkerMain(i); });
        pool.Barrier();
        pool.Stop();
        for (std::thread& t : threads) t.join();
        CHECK_EQ(pool.Completed(), 0LL);
    }
}
