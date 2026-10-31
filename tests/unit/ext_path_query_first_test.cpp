// ============================================================================
// tests/unit/ext_path_query_first_test.cpp -- PATHQUERYFIRST: fix.path_query_first
// ============================================================================
// src/ext/path_query_first/queue_first.h on its own: the query pushed at the head of the worker's
// command list is the next command the worker pops, the commands queued before it follow in
// their order, and the search runs under the query's settings with the worker's put back after it.
// The pop is a literal copy of PathfinderThread::Run's (src/game/Game/pathfinder_seed.cpp); the
// list uses the real PathCommand. The registry row is checked in ext_registry_test.cpp.
// docs/extensions/handins/PATHQUERYFIRST.md
// ============================================================================
#include "unit.h"
#include "Action/PathSearch.h"
#include "ext/path_query_first/queue_first.h"

#include <cstring>
#include <vector>

namespace {

using Ext::PathQueryFirst::PushHead;
using Ext::PathQueryFirst::RunWith;
using Ext::PathQueryFirst::Settings;

struct Queue {
    PathCommand* tail = nullptr;
    PathCommand* head = nullptr;
    int32_t      count = 0;
};

// RequestPath's push (0x1400a6c10, the tail).
void PushTail(Queue& q, PathCommand* node) {
    node->next = nullptr;
    node->prev = nullptr;
    if (q.tail == nullptr) {
        q.head = node;
    } else {
        q.tail->next = node;
        node->prev = q.tail;
    }
    q.tail = node;
    q.count = q.count + 1;
}

// PathfinderThread::Run's pop (the head).
PathCommand* Pop(Queue& q) {
    PathCommand* node = q.head;
    if (node == q.tail) q.tail = node->prev;
    q.head = node->next;
    if (node->next != nullptr) node->next->prev = node->prev;
    if (node->prev != nullptr) node->prev->next = node->next;
    node->next = nullptr;
    node->prev = nullptr;
    q.count = q.count - 1;
    return node;
}

PathCommand Command(int32_t type, int32_t slot) {
    PathCommand c{};
    c.args.type = type;
    c.args.arg3 = slot;
    return c;
}

// The fields of GridPathSearch that Settings covers.
struct Grid {
    int32_t  stepCost[0x20];
    uint32_t passableTypes;
    uint8_t  allowDiagonal;
    uint8_t  manhattan;
    uint8_t  respectBlocked;
};

Grid MakeGrid(int32_t seed) {
    Grid g{};
    for (int i = 0; i < 0x20; ++i) g.stepCost[i] = seed + i;
    g.passableTypes = 0x380u + static_cast<uint32_t>(seed);
    g.allowDiagonal = 1;
    g.manhattan = 0;
    g.respectBlocked = 1;
    return g;
}

}  // namespace

UNIT_TEST(ext_path_query_first_push_head_on_an_empty_queue) {
    Queue q;
    PathCommand query = Command(6, 9);
    PushHead(q.head, q.tail, q.count, &query);
    CHECK(q.head == &query);
    CHECK(q.tail == &query);
    CHECK(q.count == 1);
    CHECK(Pop(q) == &query);
    CHECK(q.head == nullptr);
    CHECK(q.tail == nullptr);
    CHECK(q.count == 0);
}

UNIT_TEST(ext_path_query_first_query_pops_first_and_the_rest_keep_their_order) {
    for (int queued = 1; queued <= 5; ++queued) {
        Queue q;
        std::vector<PathCommand> before(static_cast<size_t>(queued));
        for (int i = 0; i < queued; ++i) {
            before[static_cast<size_t>(i)] = Command(i % 2 == 0 ? 5 : 2, i);
            PushTail(q, &before[static_cast<size_t>(i)]);
        }
        PathCommand query = Command(6, 100);
        PushHead(q.head, q.tail, q.count, &query);
        CHECK(q.count == queued + 1);

        // A command queued after the query goes behind everything.
        PathCommand after = Command(5, 200);
        PushTail(q, &after);

        CHECK(Pop(q) == &query);
        for (int i = 0; i < queued; ++i) CHECK(Pop(q) == &before[static_cast<size_t>(i)]);
        CHECK(Pop(q) == &after);
        CHECK(q.head == nullptr);
        CHECK(q.tail == nullptr);
        CHECK(q.count == 0);
    }
}

UNIT_TEST(ext_path_query_first_search_runs_with_the_query_settings_and_restores_the_worker) {
    Grid grid = MakeGrid(10);
    const Grid workerOwn = grid;
    const Grid queryGrid = MakeGrid(40);
    Settings query;
    Ext::PathQueryFirst::ReadSettings(queryGrid, &query);
    query.allowDiagonal = 0;
    query.manhattan = 1;
    query.respectBlocked = 0;

    int runs = 0;
    RunWith(&grid, query, [&]() {
        ++runs;
        CHECK(std::memcmp(grid.stepCost, query.stepCost, sizeof(grid.stepCost)) == 0);
        CHECK(grid.passableTypes == query.passableTypes);
        CHECK(grid.allowDiagonal == 0);
        CHECK(grid.manhattan == 1);
        CHECK(grid.respectBlocked == 0);
    });
    CHECK(runs == 1);
    CHECK(std::memcmp(&grid, &workerOwn, sizeof(Grid)) == 0);
}

UNIT_TEST(ext_path_query_first_settings_read_and_write_the_real_grid) {
    // GridPathSearch's fields, through the same templates the worker uses.
    static GridPathSearch real;
    Settings s;
    Grid source = MakeGrid(3);
    Ext::PathQueryFirst::ReadSettings(source, &s);
    Ext::PathQueryFirst::WriteSettings(s, &real);
    Settings back;
    Ext::PathQueryFirst::ReadSettings(real, &back);
    CHECK(std::memcmp(back.stepCost, source.stepCost, sizeof(back.stepCost)) == 0);
    CHECK(back.passableTypes == source.passableTypes);
    CHECK(back.allowDiagonal == source.allowDiagonal);
    CHECK(back.manhattan == source.manhattan);
    CHECK(back.respectBlocked == source.respectBlocked);
}
