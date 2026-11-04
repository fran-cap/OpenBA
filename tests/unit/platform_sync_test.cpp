// ============================================================================
// tests/unit/platform_sync_test.cpp -- Phase D.4 THREADS E.1: the Platform:: primitives
// ============================================================================
// plans/THREADS.md E.1. Pins the semantics the engine relies on (System::Thread's
// three events, its wait-any loop, the recursive locks, the counters), so the POSIX
// impl of step E.9 has a contract to meet. The EVENT tests run against BOTH impls:
// the Win32 one (src/platform/win/sync_win.cpp) and the std one
// (src/platform/common/event_std.cpp, built and run on the Windows host too).
// ============================================================================

#include "unit.h"

#include "event_std.h"
#include "platform_sync.h"
#include "platform_thread.h"
#include "platform_time.h"

#include <atomic>
#include <cstdint>
#include <ctime>

namespace {

using Platform::EventHandle;

// One event implementation, as a table, so each test body runs against both.
struct EventImpl {
    const char* name;
    EventHandle (*create)(bool manualReset, bool initiallySignaled);
    bool (*set)(EventHandle);
    bool (*reset)(EventHandle);
    uint32_t (*wait)(EventHandle, uint32_t);
    uint32_t (*waitAny)(uint32_t, const EventHandle*, uint32_t);
    bool (*close)(EventHandle);
};

const EventImpl kImpls[] = {
    {"win32", &Platform::EventCreate, &Platform::EventSet, &Platform::EventReset,
     &Platform::EventWait, &Platform::EventWaitAny, &Platform::EventClose},
    {"std", &Platform::StdEvent::Create, &Platform::StdEvent::Set, &Platform::StdEvent::Reset,
     &Platform::StdEvent::Wait, &Platform::StdEvent::WaitAny, &Platform::StdEvent::Close},
};

}  // namespace

UNIT_TEST(platform_event_auto_reset_is_consumed_by_one_wait) {
    for (const EventImpl& I : kImpls) {
        EventHandle e = I.create(false, false);
        CHECK(e != nullptr);
        CHECK_EQ(I.wait(e, 0), Platform::kWaitTimeout);     // created unsignalled
        I.set(e);
        CHECK_EQ(I.wait(e, 0), 0u);                         // signalled ...
        CHECK_EQ(I.wait(e, 0), Platform::kWaitTimeout);     // ... and consumed
        I.close(e);
    }
}

UNIT_TEST(platform_event_manual_reset_stays_signalled_until_reset) {
    for (const EventImpl& I : kImpls) {
        // System::Thread's hFinished: CreateEventW(NULL, TRUE, TRUE, NULL).
        EventHandle e = I.create(true, true);
        CHECK_EQ(I.wait(e, 0), 0u);                         // initially signalled
        CHECK_EQ(I.wait(e, 0), 0u);                         // EventWait(e, 0) does not consume
        CHECK_EQ(I.wait(e, Platform::kInfinite), 0u);       // nor does an infinite wait
        I.reset(e);
        CHECK_EQ(I.wait(e, 0), Platform::kWaitTimeout);
        I.set(e);
        CHECK_EQ(I.wait(e, 0), 0u);
        I.close(e);
    }
}

UNIT_TEST(platform_event_initially_signalled_auto_reset) {
    for (const EventImpl& I : kImpls) {
        EventHandle e = I.create(false, true);
        CHECK_EQ(I.wait(e, 0), 0u);
        CHECK_EQ(I.wait(e, 0), Platform::kWaitTimeout);
        I.close(e);
    }
}

UNIT_TEST(platform_event_wait_any_returns_lowest_and_consumes_only_it) {
    for (const EventImpl& I : kImpls) {
        // System::Thread::ThreadFunc waits on {hStart, hStop}: index 1 is the stop.
        EventHandle evs[2] = {I.create(false, false), I.create(false, false)};
        CHECK_EQ(I.waitAny(2, evs, 0), Platform::kWaitTimeout);
        I.set(evs[1]);
        I.set(evs[0]);
        CHECK_EQ(I.waitAny(2, evs, 0), 0u);                 // the lowest signalled index
        CHECK_EQ(I.waitAny(2, evs, 0), 1u);                 // index 1 was left signalled
        CHECK_EQ(I.waitAny(2, evs, 0), Platform::kWaitTimeout);
        // A manual-reset member is reported but not consumed.
        EventHandle mixed[2] = {I.create(false, false), I.create(true, true)};
        CHECK_EQ(I.waitAny(2, mixed, 0), 1u);
        CHECK_EQ(I.waitAny(2, mixed, 0), 1u);
        I.close(evs[0]); I.close(evs[1]); I.close(mixed[0]); I.close(mixed[1]);
    }
}

UNIT_TEST(platform_event_timed_wait_times_out) {
    for (const EventImpl& I : kImpls) {
        EventHandle e = I.create(false, false);
        CHECK_EQ(I.wait(e, 20), Platform::kWaitTimeout);
        I.close(e);
    }
}

namespace {

struct WakeArgs {
    const EventImpl* impl;
    EventHandle      evs[2];
    std::atomic<uint32_t> result{0xDEADu};
};

unsigned int WakeWorker(void* p) {
    WakeArgs* a = static_cast<WakeArgs*>(p);
    a->result = a->impl->waitAny(2, a->evs, Platform::kInfinite);
    return 0;
}

}  // namespace

UNIT_TEST(platform_event_set_from_another_thread_wakes_an_infinite_wait) {
    for (const EventImpl& I : kImpls) {
        WakeArgs a;
        a.impl   = &I;
        a.evs[0] = I.create(false, false);
        a.evs[1] = I.create(false, false);
        Platform::ThreadHandle t = Platform::ThreadCreateSuspended(&WakeWorker, &a, 0x10000);
        CHECK(t != nullptr);
        Platform::ThreadResume(t);
        Platform::SleepMs(10);                              // let it block (not required)
        I.set(a.evs[1]);                                    // the "stop" index
        CHECK_EQ(Platform::ThreadJoin(t), 0u);              // WAIT_OBJECT_0 on the thread
        CHECK_EQ(a.result.load(), 1u);
        Platform::ThreadClose(t);
        I.close(a.evs[0]); I.close(a.evs[1]);
    }
}

namespace {

struct ThreadProbe {
    std::atomic<int> ran{0};
    std::atomic<uint32_t> tid{0};
    Platform::LockStorage* lock = nullptr;
};

unsigned int ProbeWorker(void* p) {
    ThreadProbe* probe = static_cast<ThreadProbe*>(p);
    if (probe->lock) {
        Platform::MutexLock(probe->lock);
        Platform::MutexUnlock(probe->lock);
    }
    probe->tid = Platform::CurrentThreadId();
    probe->ran = 1;
    return 7;
}

}  // namespace

UNIT_TEST(platform_thread_create_suspended_resume_join) {
    ThreadProbe probe;
    Platform::ThreadHandle t = Platform::ThreadCreateSuspended(&ProbeWorker, &probe, 0x10000);
    CHECK(t != nullptr);
    CHECK(Platform::ThreadSetNormalPriority(t));
    // NOTE: passes vacuously under starvation -- a worker that never got
    // scheduled in 20 ms also reads ran == 0.
    Platform::SleepMs(20);
    CHECK_EQ(probe.ran.load(), 0);                          // suspended: it has not run
    CHECK_EQ(Platform::ThreadResume(t), 1u);                // previous suspend count
    CHECK_EQ(Platform::ThreadJoin(t), 0u);
    CHECK_EQ(probe.ran.load(), 1);
    CHECK(probe.tid.load() != Platform::CurrentThreadId());
    CHECK(probe.tid.load() != 0xFFFFFFFFu);                 // Video::Device's "no owner"
    CHECK(Platform::ThreadClose(t));
}

// Not analysed: the recursive re-take is the point, and clang's thread-safety analysis does not model it.
UNIT_TEST(platform_mutex_is_recursive_and_excludes_other_threads) NO_THREAD_SAFETY_ANALYSIS {
    Platform::LockStorage lock;
    Platform::MutexInit(&lock, 5000);
    Platform::MutexLock(&lock);
    Platform::MutexLock(&lock);                             // recursion: no deadlock
    ThreadProbe probe;
    probe.lock = &lock;
    Platform::ThreadHandle t = Platform::ThreadCreateSuspended(&ProbeWorker, &probe, 0x10000);
    Platform::ThreadResume(t);
    // NOTE: both ran == 0 reads pass vacuously under starvation (a worker
    // that never got scheduled in 20 ms looks the same as a blocked one).
    Platform::SleepMs(20);
    CHECK_EQ(probe.ran.load(), 0);                          // blocked on the held lock
    Platform::MutexUnlock(&lock);
    Platform::SleepMs(20);
    CHECK_EQ(probe.ran.load(), 0);                          // still held once
    Platform::MutexUnlock(&lock);
    CHECK_EQ(Platform::ThreadJoin(t), 0u);
    CHECK_EQ(probe.ran.load(), 1);
    Platform::ThreadClose(t);
    Platform::MutexDestroy(&lock);

    Platform::LockStorage noSpin;                           // res_load_lock's spin-0 init
    Platform::MutexInit(&noSpin, 0);
    Platform::MutexLock(&noSpin);
    Platform::MutexUnlock(&noSpin);
    Platform::MutexDestroy(&noSpin);
}

UNIT_TEST(platform_perf_counter_is_monotonic) {
    int64_t freq = 0;
    CHECK(Platform::PerfFrequency(&freq));
    CHECK(freq > 0);
    int64_t a = 0, b = 0;
    CHECK(Platform::PerfCounter(&a));
    Platform::SleepMs(2);
    CHECK(Platform::PerfCounter(&b));
    CHECK(b > a);
    const uint64_t t0 = Platform::TickCount64();
    CHECK(Platform::TickCount64() >= t0);
    Platform::YieldThread();                                // may or may not switch; nothing to assert
}

UNIT_TEST(platform_filetime_of_known_epochs) {
    // 1970-01-01T00:00:00Z is 116444736000000000 in FILETIME units.
    CHECK_EQ(Platform::FileTimeFromUnix(0, 0), 116444736000000000ull);
    // 2000-01-01T00:00:00Z = 946684800 s; plus 1.5 s in nanoseconds.
    CHECK_EQ(Platform::FileTimeFromUnix(946684800, 0), 125911584000000000ull);
    CHECK_EQ(Platform::FileTimeFromUnix(946684801, 500000000), 125911584015000000ull);
    // The live clock agrees with the C library's to within a few seconds.
    const uint64_t now = Platform::NowFileTime();
    const uint64_t ref = Platform::FileTimeFromUnix(static_cast<int64_t>(std::time(nullptr)), 0);
    const uint64_t diff = now > ref ? now - ref : ref - now;
    CHECK(diff < 5ull * 10000000ull);
    Platform::LocalTime lt{};
    Platform::LocalTimeNow(&lt);
    CHECK(lt.year >= 2024 && lt.month >= 1 && lt.month <= 12 && lt.day >= 1 && lt.day <= 31);
}

UNIT_TEST(platform_wait_timer_sleeps) {
    Platform::WaitTimerHandle t = Platform::WaitTimerCreate();
    CHECK(t != nullptr);
    int64_t freq = 0, a = 0, b = 0;
    Platform::PerfFrequency(&freq);
    Platform::PerfCounter(&a);
    CHECK(Platform::WaitTimerSleep(t, 20000));              // 2 ms in 100 ns units
    Platform::PerfCounter(&b);
    CHECK((b - a) * 1000 >= freq);                          // at least ~1 ms passed
    Platform::CpuRelax();
    Platform::WaitTimerDestroy(t);   // review 2026-11-03, F-006: LSan saw the POSIX handle leak
    Platform::WaitTimerDestroy(nullptr);
}

namespace {
std::atomic<int> g_onceRuns{0};
void OnceBody() { g_onceRuns.fetch_add(1); }
}  // namespace

UNIT_TEST(platform_once_runs_exactly_once) {
    Platform::OnceFlag flag = {nullptr};
    Platform::OnceRun(&flag, &OnceBody);
    Platform::OnceRun(&flag, &OnceBody);
    CHECK_EQ(g_onceRuns.load(), 1);
}
