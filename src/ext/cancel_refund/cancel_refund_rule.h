// ============================================================================
// src/ext/cancel_refund/cancel_refund_rule.h -- fix.cancel_refund: the pure rule
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §26. Standard C++ only, so
// tests/unit/ext_cancel_refund_test.cpp links it without the world; the map search and
// the pile entities are cancel_refund.cpp.
//
// Where the material that DestroyComponent::ScatterContents (0x14011c530) could not lay
// on the footprint goes: piles of retail's own size (100 / weight, at most what is left)
// on the cells Map::FindPlacementCell (0x14009bd40) returns when it is started from the
// ring of cells just outside the footprint. The ring is walked clockwise from the cell
// above the top-left corner; cells off the map are skipped.
// ============================================================================
#pragma once

namespace Ext {
namespace CancelRefund {

// The size of the next pile: what ScatterContents puts on one cell (0x14011ca59
// `100 / weight`, CMOVG to what is left). A weight of 0 or less (retail would divide by
// it) takes the rest in one pile.
inline int PileSize(int weight, int remaining) {
    if (remaining <= 0) return 0;
    if (weight <= 0) return remaining;
    const int per = 100 / weight;
    if (per <= 0) return 1;
    return per < remaining ? per : remaining;
}

// Calls fn(cell) for each on-map cell of the ring one cell outside the rectangle
// (x0, y0, w, h) on a mapSize x mapSize grid, clockwise from (x0 - 1, y0 - 1), each cell
// once. Stops early and returns the cell when fn returns true; -1 when none did.
template <class Fn>
int ForEachRimCell(int x0, int y0, int w, int h, int mapSize, Fn&& fn) {
    if (w <= 0 || h <= 0 || mapSize <= 0) return -1;
    const int left = x0 - 1;
    const int top = y0 - 1;
    const int right = x0 + w;
    const int bottom = y0 + h;
    auto visit = [&](int col, int row) -> bool {
        if (col < 0 || row < 0 || col >= mapSize || row >= mapSize) return false;
        return fn(row * mapSize + col);
    };
    for (int col = left; col < right; ++col) {
        if (visit(col, top)) return top * mapSize + col;
    }
    for (int row = top; row < bottom; ++row) {
        if (visit(right, row)) return row * mapSize + right;
    }
    for (int col = right; col > left; --col) {
        if (visit(col, bottom)) return bottom * mapSize + col;
    }
    for (int row = bottom; row > top; --row) {
        if (visit(left, row)) return row * mapSize + left;
    }
    return -1;
}

}  // namespace CancelRefund
}  // namespace Ext
