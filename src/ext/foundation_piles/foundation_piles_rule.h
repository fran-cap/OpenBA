// ============================================================================
// src/ext/foundation_piles/foundation_piles_rule.h -- fix.foundation_piles: the pure rule
// ============================================================================
// NOT RETAIL. docs/extensions/handins/FOUNDATIONPILES.md. Standard C++ only, so
// tests/unit/ext_foundation_piles_test.cpp links it without the world; the map tests
// and the action chain are foundation_piles.cpp.
//
// Where a pile leaving a foundation is carried: the nearest cell OUTSIDE the site's
// rectangle that the caller's test accepts (free, buildable, reachable). The search
// walks square rings around the pile (r = 1, 2, ... maxRing; ring r holds the cells
// whose larger axis offset is r); inside a ring the cell with the smallest squared
// straight-line distance wins, ties to the first in row-major order. Cells inside the
// rectangle and cells off the map are never offered to the test.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace FoundationPiles {

struct Rect {
    int x0 = 0;   // first column (MapComponent +0x28)
    int y0 = 0;   // first row (+0x2a)
    int w = 0;    // width (+0x2c)
    int h = 0;    // height (+0x2e)

    bool Contains(int col, int row) const {
        return col >= x0 && col < x0 + w && row >= y0 && row < y0 + h;
    }
};

// The ring count that reaches `extra` cells beyond the rectangle's far edge from any
// pile inside it.
inline int RingsFor(const Rect& site, int extra) {
    const int side = site.w > site.h ? site.w : site.h;
    return side + extra;
}

// The chosen cell index (row * mapSize + col), or -1 when no cell within maxRing
// passes `accept(col, row)`.
template <class Accept>
int ChooseDropCell(const Rect& site, int col, int row, int mapSize, int maxRing, Accept&& accept) {
    for (int r = 1; r <= maxRing; ++r) {
        int best = -1;
        int bestDist = 0;
        for (int dy = -r; dy <= r; ++dy) {
            const int y = row + dy;
            if (y < 0 || y >= mapSize) continue;
            for (int dx = -r; dx <= r; ++dx) {
                const int adx = dx < 0 ? -dx : dx;
                const int ady = dy < 0 ? -dy : dy;
                if (adx != r && ady != r) continue;   // inside the ring, seen at a smaller r
                const int x = col + dx;
                if (x < 0 || x >= mapSize) continue;
                if (site.Contains(x, y)) continue;
                const int dist = dx * dx + dy * dy;
                if (best >= 0 && dist >= bestDist) continue;
                if (!accept(x, y)) continue;
                best = y * mapSize + x;
                bestDist = dist;
            }
        }
        if (best >= 0) return best;
    }
    return -1;
}

}  // namespace FoundationPiles
}  // namespace Ext
