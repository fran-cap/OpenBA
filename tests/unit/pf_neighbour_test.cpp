// ============================================================================
// tests/unit/pf_neighbour_test.cpp -- GridPathSearch::Neighbour's carried row / column
// ============================================================================
// GridPathSearch::FindPath (0x14001d040, src/game/Action/action_pf_astar.cpp) carries the
// expanded cell's (row, col) instead of dividing every neighbour by the grid width, and
// Neighbour (0x1400a5ec0, inline in src/game/Action/PathSearch.h) takes and returns the
// pair. The reference below is retail's Neighbour on the cell index (its own
// `index / width`, `index % width` split). For every cell and direction:
//   - the carried form returns the same cell (or -1)
//   - a returned cell's `/ width` and `% width` are the pair Neighbour handed back, so
//     FindPath's carried differences equal retail's divided ones
// on square grids of every retail map size (256, 384 -- not a power of two -- and 512),
// odd and tiny widths, and 2048, past the largest retail map.
// ============================================================================
#include "unit.h"
#include "Action/PathSearch.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t Next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<uint32_t>(s >> 11);
    }
};

// Retail's 0x1400a5ec0 on the cell index.
int RefNeighbour(const GridPathSearch& g, int index, uint32_t dir) {
    int w = g.width;
    int row = index / w;
    int col = index % w;
    switch (dir) {
    case 0: col = col + 1; break;
    case 1: col = col - 1; break;
    case 2: row = row + 1; break;
    case 3: row = row - 1; break;
    case 4: col = col + 1; row = row + 1; break;
    case 5: col = col + 1; row = row - 1; break;
    case 6: col = col - 1; row = row - 1; break;
    case 7: col = col - 1; row = row + 1; break;
    default: break;
    }
    if ((-1 < col) && (col < w) && (-1 < row) && (row < w)) {
        int cell = w * row + col;
        if (g.respectBlocked == 0 || (g.cellFlags[cell] & 0x80) == 0) {
            if (((g.passableTypes >> (g.cellFlags[cell] & 0x1f)) & 1) != 0)
                return cell;
            return -1;
        }
    }
    return -1;
}

// Every cell when `stride` is 1; otherwise the border rows and columns plus every
// stride-th cell (the 2048 grid). Returns the number of mismatches.
int CheckGrid(int w, uint8_t respectBlocked, uint32_t passableTypes, int stride, uint64_t seed) {
    std::vector<uint8_t> flags(static_cast<size_t>(w) * w);
    Rng rng(seed);
    for (uint8_t& f : flags)
        f = static_cast<uint8_t>(rng.Next());

    GridPathSearch g;
    std::memset(&g, 0, sizeof(g));
    g.width = w;
    g.cellFlags = flags.data();
    g.respectBlocked = respectBlocked;
    g.passableTypes = passableTypes;

    int bad = 0;
    const int count = w * w;
    for (int cell = 0; cell < count; ++cell) {
        const int curRow = cell / w;
        const int curCol = cell % w;
        const bool edge = curRow <= 1 || curCol <= 1 || curRow >= w - 2 || curCol >= w - 2;
        if (stride > 1 && !edge && cell % stride != 0)
            continue;
        for (uint32_t dir = 0; dir < 8; ++dir) {
            const int ref = RefNeighbour(g, cell, dir);
            int nbrRow = curRow;
            int nbrCol = curCol;
            const int got = g.Neighbour(nbrRow, nbrCol, dir);
            if (got != ref) {
                ++bad;
                continue;
            }
            if (got < 0)
                continue;
            // FindPath's operands: retail's divisions against the carried pair.
            if (got / w != nbrRow || got % w != nbrCol)
                ++bad;
            if (cell % w - got % w != curCol - nbrCol || cell / w - got / w != curRow - nbrRow)
                ++bad;
        }
    }
    return bad;
}

}  // namespace

UNIT_TEST(pf_neighbour_carried_matches_retail_every_map_size) {
    const int widths[] = { 1, 2, 3, 7, 64, 256, 384, 512 };
    uint64_t seed = 1;
    for (int w : widths) {
        CHECK_EQ(CheckGrid(w, 0, 0xFFFFFFFFu, 1, seed++), 0);
        CHECK_EQ(CheckGrid(w, 1, 0xFFFFFFFFu, 1, seed++), 0);
        CHECK_EQ(CheckGrid(w, 1, 0x0000A5C3u, 1, seed++), 0);
    }
}

UNIT_TEST(pf_neighbour_carried_matches_retail_2048) {
    CHECK_EQ(CheckGrid(2048, 1, 0x0000A5C3u, 7, 99), 0);
}

UNIT_TEST(pf_neighbour_carried_negative_index) {
    // A negative index (retail would read nodes[-1] first; the split must still agree):
    // FindPath passes the same truncating split retail's Neighbour makes.
    std::vector<uint8_t> flags(384 * 384, 0);
    GridPathSearch g;
    std::memset(&g, 0, sizeof(g));
    g.width = 384;
    g.cellFlags = flags.data();
    g.passableTypes = 1;
    const int cells[] = { -1, -383, -384, -385, -1000 };
    for (int cell : cells) {
        for (uint32_t dir = 0; dir < 8; ++dir) {
            int row = cell / 384;
            int col = cell % 384;
            CHECK_EQ(g.Neighbour(row, col, dir), RefNeighbour(g, cell, dir));
        }
    }
}
