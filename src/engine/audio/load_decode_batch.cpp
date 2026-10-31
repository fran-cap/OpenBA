// ============================================================================
// src/engine/audio/load_decode_batch.cpp -- the sound-effect decodes of one load, in parallel
// ============================================================================
// NOT RETAIL (block SNDPAR). The header says what and why; docs/extensions/handins/SNDPAR.md
// has the exactness and thread-safety argument.
//
// A JOB'S STATES (all transitions under Registry::m):
//   queued    -> decoding   a pool thread (the batch's parallel phase) or an Await claims it
//   decoding  -> decoded    the pool thread's decode returned (an Await goes straight on)
//   queued / decoded -> finishing   the batch's serial pass, or an Await, runs the finish
//   finishing -> (gone)     erased from the registry: the item is published
// A thread that meets a job in `decoding` or `finishing` it does not own waits on the
// condition variable. The Job memory belongs to the batch and is freed after its serial
// pass, when every one of its jobs is gone from the registry (the serial pass waits out the
// two owned states first), so an Await never touches a freed Job.
// ============================================================================
#include "audio/load_decode_batch.h"

#include "system/parallel_for.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <vector>

#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#define LOADDECODE_HAVE_MXCSR 1
#endif
#if defined(__x86_64__) || defined(__i386__)
#define LOADDECODE_HAVE_X87 1
#endif
#if defined(__aarch64__)
// AArch64: FPCR holds the rounding mode and the flush-to-zero bits (the MXCSR's part on x86).
#define LOADDECODE_HAVE_FPCR 1
#endif

namespace Audio {
namespace LoadDecode {

namespace detail {
std::atomic<int> g_queued{0};
}  // namespace detail

namespace {

// ---- the floating-point environment a job runs under ---------------------------------------
struct FpEnv {
    uint32_t csr = 0;   // MXCSR: SSE rounding / DAZ / FTZ (the decoder's float math)
    uint16_t fcw = 0;   // x87 control word: precision / rounding of any x87 code (libm)
    uint64_t fpcr = 0;  // AArch64 FPCR: rounding mode / FZ / FZ16 (the same role as MXCSR there)
};

FpEnv ReadFpEnv() {
    FpEnv e;
#ifdef LOADDECODE_HAVE_MXCSR
    e.csr = _mm_getcsr();
#endif
#ifdef LOADDECODE_HAVE_X87
    __asm__ volatile("fnstcw %0" : "=m"(e.fcw));
#endif
#ifdef LOADDECODE_HAVE_FPCR
    __asm__ volatile("mrs %0, fpcr" : "=r"(e.fpcr));
#endif
    return e;
}

void WriteFpEnv(const FpEnv& e) {
#ifdef LOADDECODE_HAVE_MXCSR
    _mm_setcsr(e.csr);
#endif
#ifdef LOADDECODE_HAVE_X87
    __asm__ volatile("fldcw %0" : : "m"(e.fcw));
#endif
#ifdef LOADDECODE_HAVE_FPCR
    __asm__ volatile("msr fpcr, %0" : : "r"(e.fpcr));
#endif
    (void)e;
}

// Install `want` for one call, put the thread's own back after it.
class FpEnvScope {
public:
    explicit FpEnvScope(const FpEnv& want) : _saved(ReadFpEnv()) {
        _changed = want.csr != _saved.csr || want.fcw != _saved.fcw || want.fpcr != _saved.fpcr;
        if (_changed) WriteFpEnv(want);
    }
    ~FpEnvScope() { if (_changed) WriteFpEnv(_saved); }
    FpEnvScope(const FpEnvScope&) = delete;
    FpEnvScope& operator=(const FpEnvScope&) = delete;
private:
    FpEnv _saved;
    bool  _changed = false;
};

// ---- the jobs ------------------------------------------------------------------------------
enum class State : uint8_t { kQueued, kDecoding, kDecoded, kFinishing };

struct Job {
    void*    item;
    void*    result;
    uint64_t cost;
    JobFn    decode;
    JobFn    finish;
    FpEnv    env;
    State    state;
};

struct Registry {
    std::mutex m;
    std::condition_variable changed;
    std::unordered_map<const void*, Job*> jobs;   // queued and not yet finished
};

Registry& Reg() {
    static Registry r;   // function-local: built on first use, whichever thread
    return r;
}

struct Batch {
    int depth = 0;              // open Scopes on this thread
    std::vector<Job*> jobs;     // this thread's queue, in Defer order
};
thread_local Batch tl_batch;

void RunDecode(Job* j) {
    FpEnvScope env(j->env);
    j->decode(j->item, j->result);
}

void RunFinish(Job* j) {
    FpEnvScope env(j->env);
    j->finish(j->item, j->result);
}

// The job is in the registry, in `kQueued` or `kDecoded`, and `lk` holds Registry::m. Take it
// to the end (decode if not yet, finish, erase); returns with `lk` held again.
void Complete(std::unique_lock<std::mutex>& lk, Registry& reg, Job* j) {
    if (j->state == State::kQueued) {
        j->state = State::kDecoding;
        lk.unlock();
        RunDecode(j);
        lk.lock();
    }
    j->state = State::kFinishing;
    lk.unlock();
    RunFinish(j);
    lk.lock();
    reg.jobs.erase(j->item);
    detail::g_queued.fetch_sub(1, std::memory_order_release);
    reg.changed.notify_all();
}

// The parallel phase, per job: decode it unless an Await got there first.
void TryDecode(Job* j) {
    Registry& reg = Reg();
    {
        std::lock_guard<std::mutex> lk(reg.m);
        if (j->state != State::kQueued) return;   // an Await owns it (it is still in the map)
        j->state = State::kDecoding;
    }
    RunDecode(j);
    {
        std::lock_guard<std::mutex> lk(reg.m);
        j->state = State::kDecoded;
    }
    reg.changed.notify_all();
}

// The serial pass, per job in queue order: finish it unless an Await already did.
void FinishInOrder(Job* j) {
    Registry& reg = Reg();
    std::unique_lock<std::mutex> lk(reg.m);
    for (;;) {
        const auto it = reg.jobs.find(j->item);
        if (it == reg.jobs.end() || it->second != j) return;   // an Await finished it
        if (j->state == State::kDecoding || j->state == State::kFinishing) {
            reg.changed.wait(lk);                              // an Await is running it
            continue;
        }
        Complete(lk, reg, j);
        return;
    }
}

void RunBatch(std::vector<Job*>& jobs) {
    // Largest first, so the long sounds start at once and the short ones fill in after
    // them. Only the order the pool picks jobs up in; the finishes keep the queue order.
    std::vector<Job*> byCost(jobs);
    std::stable_sort(byCost.begin(), byCost.end(),
                     [](const Job* a, const Job* b) { return a->cost > b->cost; });
    System::ParallelFor(0, static_cast<int>(byCost.size()), 1, [&](int b, int e) {
        for (int i = b; i < e; ++i) TryDecode(byCost[static_cast<size_t>(i)]);
    });
    for (Job* j : jobs) FinishInOrder(j);
    for (Job* j : jobs) delete j;
    jobs.clear();
}

}  // namespace

Scope::Scope() { ++tl_batch.depth; }

Scope::~Scope() {
    Batch& b = tl_batch;
    if (--b.depth == 0 && !b.jobs.empty()) {
        // The batch's own vector is swapped out first: a finish that loads (none does
        // today) would open a new Scope and queue into a fresh batch, not this one.
        std::vector<Job*> jobs;
        jobs.swap(b.jobs);
        RunBatch(jobs);
    }
}

bool BatchOpen() { return tl_batch.depth > 0; }

bool Defer(void* item, void* result, uint64_t cost, JobFn decode, JobFn finish) {
    Batch& b = tl_batch;
    if (b.depth <= 0) return false;
    Job* j = new Job{item, result, cost, decode, finish, ReadFpEnv(), State::kQueued};
    Registry& reg = Reg();
    {
        std::lock_guard<std::mutex> lk(reg.m);
        if (!reg.jobs.emplace(item, j).second) {   // already queued: decode now instead
            delete j;
            return false;
        }
        detail::g_queued.fetch_add(1, std::memory_order_release);
    }
    b.jobs.push_back(j);
    return true;
}

namespace detail {

void AwaitSlow(const void* item) {
    Registry& reg = Reg();
    std::unique_lock<std::mutex> lk(reg.m);
    for (;;) {
        const auto it = reg.jobs.find(item);
        if (it == reg.jobs.end()) return;          // finished (or never queued)
        Job* const j = it->second;
        if (j->state == State::kDecoding || j->state == State::kFinishing) {
            reg.changed.wait(lk);                  // another thread is running it
            continue;
        }
        Complete(lk, reg, j);                      // queued or decoded: finish it here
        return;
    }
}

}  // namespace detail

}  // namespace LoadDecode
}  // namespace Audio
