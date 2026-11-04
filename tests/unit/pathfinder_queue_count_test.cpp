// ============================================================================
// tests/unit/pathfinder_queue_count_test.cpp -- PathfinderThread::queueCount (+0x70) race
// ============================================================================
// The pathfinder worker (PathfinderThread::Run, src/game/Game/pathfinder_seed.cpp) reads the
// queue count OUTSIDE the queue lock while an enqueuer on the main thread writes it under the
// lock. Every access goes through LoadQueueCount / StoreQueueCount
// (src/game/Game/pathfinder_queue_count.h). Here a producer thread pushes with the tree's own
// fix.path_query_first PushHead (src/ext/path_query_first/queue_first.h, one of the real
// writers) under a lock, and a consumer thread runs Run's loop shape: the unlocked read, then
// the pop and the decrement under the lock. The functional check is that every command is
// popped exactly once; the race check is the ThreadSanitizer build (BANISHED_SANITIZE=tsan),
// which reports a plain write of the count against the worker's unlocked read (a WARNING and
// exit code 66).
// ============================================================================
#include "unit.h"
#include "Action/PathSearch.h"
#include "Game/pathfinder_queue_count.h"
#include "ext/path_query_first/queue_first.h"

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct Queue {
    PathCommand* tail = nullptr;
    PathCommand* head = nullptr;
    int32_t      count = 0;
};

// PathfinderThread::Run's pop (the tail end here: PushHead links at the head, so the oldest
// command is at the tail; the order does not matter to this test).
PathCommand* PopTail(Queue& q) {
    PathCommand* node = q.tail;
    q.tail = node->prev;
    if (q.tail == nullptr) {
        q.head = nullptr;
    } else {
        q.tail->next = nullptr;
    }
    node->prev = nullptr;
    StoreQueueCount(&q.count, LoadQueueCount(&q.count) - 1);
    return node;
}

}  // namespace

UNIT_TEST(pathfinder_queue_count_unlocked_read_against_locked_writes) {
    const int kCommands = 20000;
    std::vector<PathCommand> nodes(static_cast<size_t>(kCommands));
    std::vector<int> seen(static_cast<size_t>(kCommands), 0);
    // On the heap, as the real PathfinderThread is (MemAlloc(0x1f8)): ThreadSanitizer does not
    // report this race on a queue in the main thread's stack frame.
    std::unique_ptr<Queue> owner(new Queue);
    Queue& q = *owner;
    std::mutex lock;

    std::thread worker([&] {
        int popped = 0;
        while (popped < kCommands) {
            int count = LoadQueueCount(&q.count);   // Run: the read outside the lock
            while (count > 0) {
                PathCommand* node;
                {
                    std::lock_guard<std::mutex> hold(lock);
                    node = PopTail(q);
                }
                ++seen[static_cast<size_t>(node->args.arg3)];
                ++popped;
                count = LoadQueueCount(&q.count);
            }
            std::this_thread::yield();
        }
    });

    for (int i = 0; i < kCommands; ++i) {
        PathCommand& node = nodes[static_cast<size_t>(i)];
        node.args.type = 5;
        node.args.arg3 = i;
        std::lock_guard<std::mutex> hold(lock);
        Ext::PathQueryFirst::PushHead(q.head, q.tail, q.count, &node);
    }
    worker.join();

    CHECK_EQ(LoadQueueCount(&q.count), 0);
    CHECK(q.head == nullptr);
    CHECK(q.tail == nullptr);
    int once = 0;
    for (int n : seen) {
        if (n == 1) ++once;
    }
    CHECK_EQ(once, kCommands);
}
