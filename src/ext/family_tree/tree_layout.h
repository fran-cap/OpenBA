// src/ext/family_tree/tree_layout.h -- the drawn tree of ext.family_tree: where each box and each line piece
// goes
//
// NOT RETAIL. Docs: docs/extensions/handins/FAMILYTREE.md section 4a. The page draws the picked person's tree
// as four rows of boxes (the hourglass): the grandparents, the parents, the person with the siblings (before)
// and the spouses (after), and the children. This file is the arithmetic only (no UI), so a unit test can
// check it.
//
// The grid is made of tiles. A box is three tiles wide and its CENTER is a tile's center, so a vertical line
// from a box's middle is the vertical line of one tile. Two boxes side by side have centers 4 tiles apart
// (one empty tile between them). Between two rows of boxes sits a row of line tiles (4 arms: up, right, down,
// left); inside a row of boxes the one tile between a couple is a short horizontal line, with a stem down
// when the couple has children.
#pragma once

#include <cstdint>
#include <vector>

#include "ext/family_tree/family_tree.h"

namespace Ext {
namespace FamilyTree {

constexpr int kTreeBoxRows = 4;        // grandparents, parents, the person's own row, children
constexpr int kTreeLinkRows = 3;       // between row r and r + 1
constexpr int kTreeMaxSiblings = 12;
constexpr int kTreeMaxChildren = 12;
constexpr int kTreeMaxSpouses = 3;

// The arms of a line tile.
constexpr uint8_t kArmUp = 1;
constexpr uint8_t kArmRight = 2;
constexpr uint8_t kArmDown = 4;
constexpr uint8_t kArmLeft = 8;

struct TreeNode {
    uint32_t id = 0;
    int row = 0;
    int x = 0;          // the tile of the box's center
};

// One tile between two boxes of a row: a horizontal line, with a stem down when `stem`.
struct TreeGap {
    int row = 0;
    int x = 0;
    bool stem = false;
};

struct TreeLayout {
    std::vector<TreeNode> nodes;
    std::vector<TreeGap> gaps;
    // [kTreeLinkRows][width]: the arms of each line tile, 0 for none
    std::vector<std::vector<uint8_t>> link;
    int width = 0;                            // tiles
    int selectedX = 0;                        // the picked person's center
    bool rowUsed[kTreeBoxRows] = {false, false, false, false};
};

// The tree around `selected`; an empty layout (width 0) when the id is not in the tree.
TreeLayout BuildTreeLayout(const Tree& tree, uint32_t selected);

}  // namespace FamilyTree
}  // namespace Ext
