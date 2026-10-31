#pragma once
// ============================================================================
// src/ext/path_query_first/queue_first.h -- NOT RETAIL. The pure parts of fix.path_query_first
// (path_query_first.h), header-only so the host unit tests (tests/unit/ext_path_query_first_test.cpp)
// check them on their own: the push at the head of the worker's command list, and running one
// search under the query's settings with the worker's own put back after it.
// docs/extensions/handins/PATHQUERYFIRST.md
// ============================================================================
#include <cstdint>
#include <cstring>

namespace Ext {
namespace PathQueryFirst {

// The search settings a query was made under: the passable mask, the 32 step costs and the
// three search flags (GridPathSearch's, or PathfinderThread's staged copies of them).
struct Settings {
    int32_t  stepCost[0x20];
    uint32_t passableTypes;
    uint8_t  allowDiagonal;
    uint8_t  manhattan;
    uint8_t  respectBlocked;
};

template <class Grid>
void ReadSettings(const Grid& grid, Settings* out) {
    std::memcpy(out->stepCost, grid.stepCost, sizeof(out->stepCost));
    out->passableTypes = grid.passableTypes;
    out->allowDiagonal = grid.allowDiagonal;
    out->manhattan = grid.manhattan;
    out->respectBlocked = grid.respectBlocked;
}

template <class Grid>
void WriteSettings(const Settings& in, Grid* grid) {
    std::memcpy(grid->stepCost, in.stepCost, sizeof(grid->stepCost));
    grid->passableTypes = in.passableTypes;
    grid->allowDiagonal = in.allowDiagonal;
    grid->manhattan = in.manhattan;
    grid->respectBlocked = in.respectBlocked;
}

// Links `node` in at the head of the worker's list (the end the worker pops next). The list
// is PathCommand's: `next` points toward the tail, `prev` toward the head. The caller holds
// the queue lock.
template <class Node>
void PushHead(Node*& head, Node*& tail, int32_t& count, Node* node) {
    node->prev = nullptr;
    node->next = head;
    if (head == nullptr) {
        tail = node;
    } else {
        head->prev = node;
    }
    head = node;
    count = count + 1;
}

// Runs `search` with `query`'s settings on `grid`, then writes the grid's own settings back,
// so every command still queued runs with the settings it was queued under.
template <class Grid, class Search>
void RunWith(Grid* grid, const Settings& query, Search search) {
    Settings own;
    ReadSettings(*grid, &own);
    WriteSettings(query, grid);
    search();
    WriteSettings(own, grid);
}

}  // namespace PathQueryFirst
}  // namespace Ext
