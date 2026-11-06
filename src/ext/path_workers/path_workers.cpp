// ============================================================================
// src/ext/path_workers/path_workers.cpp -- ext.path_workers: the game glue
// ============================================================================
// NOT RETAIL. path_workers.h has the seams; pool.h the dispatch model.
//
// THE POOL THREADS are System::Thread subclasses (src/engine/system/Thread.h,
// the engine's primitive: the tree had no std::thread): each one is Started
// once and its Run is Pool::WorkerMain, which blocks on the pool's condition
// variable between jobs and returns only after Pool::Stop. ~Thread then signals
// its stop event and joins it.
//
// EACH WORKER'S SCRATCH is a GridPathSearch of its own, built with the stores
// of retail's grid ctor 0x1400a6110 (pathfinder_seed.cpp GridCtor): its own
// node array (0x18 bytes per cell) and open-set pool, retail's search bodies
// run on it unchanged (GridPathSearch::Run 0x14001ce00). Its `cellFlags` is the
// pathfinder's own array, shared read-only: only commands 2 / 3 write it, and
// they wait for the pool to be idle first. The parameter fields are copied from
// the job's snapshot before each search. With ext.fast_paths also selected,
// each worker has its own GridSearch::Searcher too.
//
// THE PUBLISH (RunJob) is the extension's own, under the pathfinder's lock:
// size the slot's path, copy the cells, store `ready` (release), unlock. Retail's
// case 5 sizes under the lock and copies UNLOCKED (row 961, kept in the faithful
// body); with N writers that window would widen, so here the whole publish is
// inside the lock, which also excludes AllocResult's table grow (main thread,
// under the same lock). What TakePath reads -- the slot's ready byte, its path
// array, its token -- is exactly what retail's case 5 leaves.
// ============================================================================
#include "ext/path_workers/path_workers.h"
#include "ext/path_workers/pool.h"
#include "ext/fast_paths/fast_paths.h"
#include "Action/grid_search.h"
#include "ext/registry.h"
#include "platform/common/cpu/roster.h"     // Cpu::PathWorkers (block CPULIMIT)
#include "platform/common/cpu/topology.h"   // Cpu::Get().physical

#include "Game/PathfinderThread.h"
#include "Action/PathSearch.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemFree (the popped command node)
#include "system/Thread.h"
#include "platform_thread.h"           // Platform::ThreadSetBelowNormalPriority (the pool threads)
#include "stress_harness.h"            // PFBENCH: g_pfProfOn / PfProf_Search / CompProf_Now (src/diag)

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace Ext {
namespace PathWorkers {

static_assert(Cpu::kMaxPathWorkers == kMaxWorkers, "the roster's cap is the pool's");

std::atomic<uint8_t> g_mode{0};

namespace {

std::atomic<int> g_workerCount{1};
bool g_reportedForeignOwner = false;   // the dispatcher thread only

constexpr int kPathCap = 0x4000;   // ApplyCommand's 0x10000-byte path buffer (0x4000 cells)

// One pool worker's search state.
struct Scratch {
    GridPathSearch       grid;   // retail's search record, this worker's own
    GridSearch::Searcher  fast;   // ext.fast_paths' scratch, this worker's own
    std::vector<int32_t> path;   // the 0x4000-cell result buffer

    Scratch(const GridPathSearch& shared) : grid(), path(kPathCap) {
        // FUN_1400a6110's stores (pathfinder_seed.cpp GridCtor), but `cellFlags`
        // is the pathfinder's array, not a fresh one.
        std::memset(&grid, 0, sizeof(grid));
        grid.search.generation = 1;
        grid.search.open.pool.growCount = 8;
        SearchNodesResize(&grid.search.nodes, shared.width * shared.width);   // 0x14001d830
        grid.width = shared.width;
        grid.cellFlags = shared.cellFlags;
        for (int i = 0; i < 0x20; ++i) grid.stepCost[i] = 1;
    }
    ~Scratch() {
        // ~PathfinderThread's grid teardown (0x1400a6720), minus the cell bytes it does not own.
        grid.search.open.Clear();          // 0x14001ced0
        RawArrayFree(grid.search.nodes);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};

// Runs a job on its worker's scratch and publishes it under the lock.
class GameRunner final : public Runner {
public:
    PathfinderThread* pf = nullptr;
    std::vector<std::unique_ptr<Scratch>> scratch;

    void RunJob(int worker, const Job& job) override {
        Scratch& s = *scratch[worker];
        GridPathSearch& g = s.grid;
        std::memcpy(g.stepCost, job.params.stepCost, sizeof(g.stepCost));
        g.passableTypes = job.params.passableTypes;
        g.allowDiagonal = job.params.allowDiagonal;
        g.manhattan = job.params.manhattan;
        g.respectBlocked = job.params.respectBlocked;

        // [PFBENCH] one PfProf_Search per request, from the worker that ran it,
        // before the publish (as case 5 does: TakePath clears the enqueue stamp).
        const long long t0 = g_pfProfOn ? CompProf_Now() : 0;
        int count;
        int expanded;
        if (job.params.fast) {
            count = Ext::FastPaths::RunWith(s.fast, g, job.start, job.goal, s.path.data(),
                                       job.params.presentTypes, &expanded);
        } else {
            count = g.Run(job.start, job.goal, s.path.data());   // 0x14001ce00
            expanded = g.search.sequence - g.search.open.count;
        }
        if (t0 != 0) PfProf_Search(job.slot, t0, CompProf_Now(), count, expanded);

        pf->lock.Lock();
        PathResult& r = pf->results.data[static_cast<uint32_t>(job.slot)];
        r.path.SetSize(count);
        for (int i = 0; i < count; ++i) r.path._data[i] = s.path[i];
        // [PFBENCH] e2e split: the publish stamp (stress harness only), before the
        // release store so it is ordered with it (review F-279).
        if (t0 != 0) PfProf_Ready(job.slot);
        __atomic_store_n(&r.ready, static_cast<uint8_t>(1), __ATOMIC_RELEASE);
        pf->lock.Unlock();
    }
};

// A pool thread: System::Thread's Start / park protocol, Run = WorkerMain.
class PoolThread final : public System::Thread {
public:
    PoolThread(Pool* pool, int index) : System::Thread(0x10000), pool_(pool), index_(index) {}
    ~PoolThread() = default;   // ~Thread: stop event, join, close
    void Run() override { pool_->WorkerMain(index_); }
private:
    Pool* pool_;
    int   index_;
};

struct State {
    PathfinderThread*        owner = nullptr;
    GameRunner               runner;
    std::unique_ptr<Pool>    pool;
    std::vector<PoolThread*> threads;
};
// Built by the dispatcher (the pathfinder's thread), freed by ReleaseFor (the
// main thread, after Map::~Map's DrainQueue has waited for the dispatcher).
std::atomic<State*> g_state{nullptr};

State* BuildState(PathfinderThread& pf) {
    const int n = g_workerCount.load(std::memory_order_relaxed);
    State* st = new State();
    st->owner = &pf;
    st->runner.pf = &pf;
    for (int i = 0; i < n; ++i) st->runner.scratch.emplace_back(new Scratch(pf.grid));
    st->pool.reset(new Pool(st->runner, n));
    for (int i = 0; i < n; ++i) {
        PoolThread* t = new PoolThread(st->pool.get(), i);
        // CPULIMIT (THREADRESEARCH §7.4): below the main / render threads, so a path burst
        // yields to them on a small machine. Only WHEN a result lands can change, which the
        // class B lock already covers. (Retail's own threads, the dispatcher included, keep
        // NORMAL.)
        Platform::ThreadSetBelowNormalPriority(t->_hThread);
        st->threads.push_back(t);
        t->Start();
    }
    g_state.store(st, std::memory_order_release);
    g_mode.fetch_or(kModePoolAlive, std::memory_order_release);
    const double mb = static_cast<double>(pf.grid.width) * pf.grid.width * sizeof(PathSearchNode) *
                      n / (1024.0 * 1024.0);
    std::fprintf(stderr, "[PathWorkers] pool up: %d workers, grid %dx%d, node scratch %.1f MB\n",
                 n, pf.grid.width, pf.grid.width, mb);
    std::fflush(stderr);
    return st;
}

// The pool commands go to, or null for the serial path.
State* PoolFor(PathfinderThread& pf) {
    State* st = g_state.load(std::memory_order_acquire);
    const bool selected = (g_mode.load(std::memory_order_relaxed) & kModeSelected) != 0;
    if (st != nullptr && st->owner != &pf) {
        // Another pathfinder's pool (retail destroys the old Map before a new one
        // runs, so this is not expected): this one stays serial, which is exact.
        if (!g_reportedForeignOwner) {
            g_reportedForeignOwner = true;
            std::fprintf(stderr, "[PathWorkers] a second pathfinder while a pool is alive: "
                         "it runs serially\n");
            std::fflush(stderr);
        }
        return nullptr;
    }
    if (st != nullptr && !selected) {
        st->pool->Barrier();   // deselected: let this pathfinder's jobs finish, then serial
        return nullptr;
    }
    if (st == nullptr && selected) return BuildState(pf);
    return st;
}

// DispatchCommand's host on the pathfinder.
struct GameHost {
    PathfinderThread& pf;

    void Apply(const Command& c) {
        PathCommandArgs a;
        a.type = c.type;
        a.arg1 = c.arg1;
        a.arg2 = c.arg2;
        a.arg3 = c.arg3;
        pf.ApplyCommand(a);   // 0x1400a6430: retail's cases 0-4 (case 2 keeps ext.fast_paths' count hook)
    }

    Job MakeJob(const Command& c) {
        Job j;
        const GridPathSearch& g = pf.grid;
        std::memcpy(j.params.stepCost, g.stepCost, sizeof(j.params.stepCost));
        j.params.passableTypes = g.passableTypes;
        j.params.allowDiagonal = g.allowDiagonal;
        j.params.manhattan = g.manhattan;
        j.params.respectBlocked = g.respectBlocked;
        j.params.fast = Ext::FastPaths::FastSelected() ? 1 : 0;
        j.params.presentTypes = j.params.fast ? Ext::FastPaths::PresentTypesNow(g) : 0u;
        j.start = c.arg1;
        j.goal = c.arg2;
        j.slot = c.arg3;
        return j;
    }
};

}  // namespace

void SelectForGame() {
    const bool on = Ext::Ext().path_workers;
    // RENDERPIPE-1: the ACTIVE ext.render_pipe at game select (THREADPLAN section 1 row 5: a
    // render thread takes a core, so one worker fewer from 4 physical cores up to 6).
    const bool renderPipe = Ext::Ext().render_pipe;
    g_workerCount.store(Cpu::PathWorkers(Cpu::Get().physical, renderPipe), std::memory_order_relaxed);
    if (on) g_mode.fetch_or(kModeSelected, std::memory_order_release);
    else    g_mode.fetch_and(static_cast<uint8_t>(~kModeSelected), std::memory_order_release);
    if (on) {
        std::fprintf(stderr, "[PathWorkers] selected for this game: workers=%d "
                     "(Cpu::PathWorkers of %d physical cores, render_pipe=%d, below-normal priority)\n",
                     g_workerCount.load(std::memory_order_relaxed), Cpu::Get().physical, renderPipe ? 1 : 0);
        std::fflush(stderr);
    }
}

void Dispatch(PathfinderThread& pf) {
    State* st = PoolFor(pf);
    GameHost host{pf};
    while (LoadQueueCount(&pf.queueCount) > 0) {
        // Run 0x1400a6360's pop, statement for statement (pathfinder_seed.cpp).
        pf.lock.Lock();
        PathCommand* node = pf.queueHead;
        const PathCommandArgs a = node->args;
        if (node == pf.queueTail) pf.queueTail = node->prev;
        pf.queueHead = node->next;
        if (node->next != nullptr) node->next->prev = node->prev;
        if (node->prev != nullptr) node->prev->next = node->next;
        node->next = nullptr;
        node->prev = nullptr;
        StoreQueueCount(&pf.queueCount, LoadQueueCount(&pf.queueCount) - 1);
        System::MemoryAllocator::MemFree(node);
        pf.lock.Unlock();

        if (st != nullptr) {
            const Command c{a.type, a.arg1, a.arg2, a.arg3};
            DispatchCommand(*st->pool, host, c);
        } else {
            pf.ApplyCommand(a);   // 0x1400a6430: the serial path
        }
    }
}

void ReleaseFor(PathfinderThread* pf) {
    State* st = g_state.load(std::memory_order_acquire);
    if (st == nullptr || st->owner != pf) return;
    st->pool->Stop();                             // queued jobs still run and publish
    for (PoolThread* t : st->threads) delete t;   // ~Thread joins each one
    g_state.store(nullptr, std::memory_order_release);
    g_mode.fetch_and(static_cast<uint8_t>(~kModePoolAlive), std::memory_order_release);
    std::fprintf(stderr, "[PathWorkers] pool down: %d workers joined, %lld requests\n",
                 static_cast<int>(st->threads.size()), st->pool->Completed());
    std::fflush(stderr);
    delete st;
}

}  // namespace PathWorkers
}  // namespace Ext
