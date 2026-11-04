// src/diag/mod_click_guard.h -- the row bound a fabricated ModDialog click must pass.
//
// The AUTOMENU_CLICKS (autostart.cpp) and PAUSEMENU_CLICKS (pausemenu_harness.cpp)
// drivers can send ModDialog any button id, with no element behind it. The real UI
// only delivers the ids of the rows it built, so ModDialog::OnElementEvent and
// Modification::Swap index the row (and, for move up / down, its neighbour) with no
// check, as retail does. The drivers check here instead (review F-294, F-295):
//   1000-1999  toggle         row = id - 1000, 0 <= row < rows
//   2000-2999  move up        swaps row and row - 1: 1 <= row < rows
//   3000-3999  move down      swaps row and row + 1: 0 <= row < rows - 1
//   4000-4999  move to top    0 <= row < rows
//   5000-5999  show details   0 <= row < rows
// Any other id is not a per-row id and passes.
#pragma once

inline bool ModDialogClickRowInRange(int id, int rows) {
    if (id < 1000 || id >= 6000) return true;
    const int block = id / 1000;
    const int row   = id % 1000;
    if (row >= rows) return false;
    if (block == 2) return row >= 1;
    if (block == 3) return row + 1 < rows;
    return true;
}
