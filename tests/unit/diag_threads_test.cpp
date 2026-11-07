// ============================================================================
// tests/unit/diag_threads_test.cpp -- the DiagThreads roster (src/diag/diag_threads.h)
// ============================================================================
// Review F-293: when the OS reuses a thread id, the new owner must get a slot the
// sampler has not looked at yet (its cursor only moves forward), and RoleOf must
// name the newest owner. Review S-034: the busy bit the sampler reads lives in the
// roster, not in the thread object, so it can never point into freed storage.
// ============================================================================
#include "unit.h"
#include "diag_threads.h"

#include <thread>

UNIT_TEST(diag_threads_reused_tid_gets_a_new_slot) {
    const uint32_t tid = 0x7ffffff1u;   // no real thread has it
    // The roster is append-only and per process: in a worker that already ran enough threaded tests
    // every slot is taken, and the test's two registrations cannot land (seen on the macOS runner,
    // whose 3 workers x 24 chunks put this test late in a thread-heavy chunk).
    if (DiagThreads::Count() > DiagThreads::kMaxThreads - 2)
        UNIT_SKIP("the roster is full in this worker: earlier tests' threads took every slot");
    const int first = DiagThreads::Detail::RegisterAs(tid, DiagThreads::kLoader);
    CHECK(first >= 0);
    const int countAfterFirst = DiagThreads::Count();
    CHECK(DiagThreads::RoleOf(tid) == DiagThreads::kLoader);

    // The id comes back with another owner.
    const int second = DiagThreads::Detail::RegisterAs(tid, DiagThreads::kPathfinder);
    CHECK(second > first);
    CHECK(DiagThreads::Count() == countAfterFirst + 1);   // past the sampler's cursor
    CHECK(DiagThreads::TidAt(second) == tid);
    CHECK(DiagThreads::RoleAt(second) == DiagThreads::kPathfinder);
    CHECK(DiagThreads::RoleOf(tid) == DiagThreads::kPathfinder);
    // The old slot keeps the old pair whole.
    CHECK(DiagThreads::RoleAt(first) == DiagThreads::kLoader);
}

UNIT_TEST(diag_threads_busy_bit_lives_in_the_roster) {
    // Same as above: with every roster slot taken the thread below cannot register and has no slot.
    if (DiagThreads::Count() > DiagThreads::kMaxThreads - 1)
        UNIT_SKIP("the roster is full in this worker: earlier tests' threads took every slot");
    int slot = -1;
    bool busyInJob = false;
    std::thread t([&] {
        DiagThreads::Register(DiagThreads::kOther);
        slot = DiagThreads::Detail::CurrentSlot();
        DiagThreads::SetBusy(true);
        busyInJob = slot >= 0 && DiagThreads::BusyAt(slot);
        DiagThreads::SetBusy(false);
        DiagThreads::SetBusy(true);   // the thread ends mid-"job"
    });
    t.join();
    CHECK(slot >= 0);
    CHECK(busyInJob);
    // Readable after the thread (and anything it owned) is gone.
    if (slot >= 0) CHECK(DiagThreads::BusyAt(slot));
}

UNIT_TEST(diag_threads_set_busy_unregistered_is_a_no_op) {
    std::thread t([] { DiagThreads::SetBusy(true); });
    t.join();
    CHECK(true);
}
