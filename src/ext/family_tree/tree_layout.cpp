// src/ext/family_tree/tree_layout.cpp -- see tree_layout.h
#include "ext/family_tree/tree_layout.h"

#include <algorithm>

namespace Ext {
namespace FamilyTree {

namespace {

bool Known(const Tree& tree, uint32_t id) { return id != 0 && tree.Find(id) != nullptr; }

bool HasKnownParent(const Tree& tree, uint32_t id) {
    const Person* p = tree.Find(id);
    return p != nullptr && (Known(tree, p->mother) || Known(tree, p->father));
}

// The line tiles of one row between two rows of boxes: one stem comes down from above at `upper`, and a stem
// goes down to each of `lower`; a bar joins them.
void Link(std::vector<uint8_t>& row, int upper, const std::vector<int>& lower) {
    if (lower.empty()) return;
    int lo = upper, hi = upper;
    for (const int x : lower) {
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    for (int x = lo; x <= hi; ++x) {
        uint8_t m = 0;
        if (x > lo) m |= kArmLeft;
        if (x < hi) m |= kArmRight;
        if (x == upper) m |= kArmUp;
        if (std::find(lower.begin(), lower.end(), x) != lower.end()) m |= kArmDown;
        row[static_cast<size_t>(x)] |= m;
    }
}

}  // namespace

TreeLayout BuildTreeLayout(const Tree& tree, uint32_t selected) {
    TreeLayout out;
    const Person* me = tree.Find(selected);
    if (me == nullptr) return out;

    // Coordinates are built around the picked person at x = 4 * (number of siblings shown) and shifted at the
    // end.
    std::vector<uint32_t> sibs = tree.Siblings(selected);
    if (sibs.size() > static_cast<size_t>(kTreeMaxSiblings))
        sibs.resize(static_cast<size_t>(kTreeMaxSiblings));
    std::vector<uint32_t> spouses;
    for (const uint32_t s : tree.Spouses(selected)) {
        if (Known(tree, s) && std::find(spouses.begin(), spouses.end(),
                                        s) == spouses.end()) spouses.push_back(s);
    }
    if (spouses.size() > static_cast<size_t>(kTreeMaxSpouses))
        spouses.resize(static_cast<size_t>(kTreeMaxSpouses));
    std::vector<uint32_t> kids = tree.Children(selected);
    if (kids.size() > static_cast<size_t>(kTreeMaxChildren))
        kids.resize(static_cast<size_t>(kTreeMaxChildren));

    const uint32_t fatherId = Known(tree, me->father) ? me->father : 0;
    const uint32_t motherId = Known(tree, me->mother) ? me->mother : 0;
    if (fatherId == 0 && motherId == 0) sibs.clear();   // siblings are found through the parents

    struct Placed {
        uint32_t id;
        int row;
        int x;
    };
    std::vector<Placed> placed;
    struct GapAt {
        int row, x;
        bool stem;
    };
    std::vector<GapAt> gaps;
    // (row, x) of the line tiles, per link row, as the arms to add
    struct LinkOp {
        int row, upper;
        std::vector<int> lower;
    };
    std::vector<LinkOp> ops;

    // row 2: the siblings, the person, then the spouses
    std::vector<int> stripX;   // the siblings' and the person's centers
    for (size_t i = 0; i < sibs.size(); ++i) {
        placed.push_back({sibs[i], 2, 4 * static_cast<int>(i)});
        stripX.push_back(4 * static_cast<int>(i));
    }
    const int meX = 4 * static_cast<int>(sibs.size());
    placed.push_back({selected, 2, meX});
    stripX.push_back(meX);
    for (size_t j = 0; j < spouses.size(); ++j) {
        placed.push_back({spouses[j], 2, meX + 4 * static_cast<int>(j + 1)});
        gaps.push_back({2, meX + 4 * static_cast<int>(j) + 2, j == 0 && !kids.empty()});
    }
    const int mid = meX / 2;   // the middle of the siblings and the person: where the parents' stem lands

    // row 1: the parents, a couple (gap tiles between them, the stem at mid) or one box at mid
    if (fatherId != 0 || motherId != 0) {
        if (fatherId != 0 && motherId != 0) {
            const bool wide = HasKnownParent(tree, fatherId) || HasKnownParent(tree, motherId);
            const int half = wide ? 4 : 2;
            placed.push_back({fatherId, 1, mid - half});
            placed.push_back({motherId, 1, mid + half});
            for (int x = mid - half + 2; x <= mid + half - 2; ++x) gaps.push_back({1, x, x == mid});
        } else {
            placed.push_back({fatherId != 0 ? fatherId : motherId, 1, mid});
        }
        ops.push_back({1, mid, stripX});
        // row 0: each parent's own parents, a couple around the parent or one box over them
        for (const Placed& par : std::vector<Placed>(placed)) {
            if (par.row != 1) continue;
            const Person* pp = tree.Find(par.id);
            const uint32_t gf = Known(tree, pp->father) ? pp->father : 0;
            const uint32_t gm = Known(tree, pp->mother) ? pp->mother : 0;
            if (gf == 0 && gm == 0) continue;
            if (gf != 0 && gm != 0) {
                placed.push_back({gf, 0, par.x - 2});
                placed.push_back({gm, 0, par.x + 2});
                gaps.push_back({0, par.x, true});
            } else {
                placed.push_back({gf != 0 ? gf : gm, 0, par.x});
            }
            ops.push_back({0, par.x, {par.x}});
        }
    }

    // row 3: the children, centered under the couple (or under the person)
    if (!kids.empty()) {
        const int stemX = spouses.empty() ? meX : meX + 2;
        const int k = static_cast<int>(kids.size());
        std::vector<int> kidX;
        for (int i = 0; i < k; ++i) {
            const int x = stemX - 2 * (k - 1) + 4 * i;
            placed.push_back({kids[static_cast<size_t>(i)], 3, x});
            kidX.push_back(x);
        }
        ops.push_back({2, stemX, kidX});
    }

    // shift so the leftmost box starts at tile 0
    int minX = placed.front().x;
    int maxX = placed.front().x;
    for (const Placed& p : placed) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
    }
    const int shift = 1 - minX;   // a box covers x - 1 .. x + 1
    out.width = maxX + shift + 2;
    out.selectedX = meX + shift;
    out.link.assign(static_cast<size_t>(kTreeLinkRows),
                    std::vector<uint8_t>(static_cast<size_t>(out.width), 0));
    for (const Placed& p : placed) {
        out.nodes.push_back({p.id, p.row, p.x + shift});
        out.rowUsed[p.row] = true;
    }
    for (const GapAt& g : gaps) out.gaps.push_back({g.row, g.x + shift, g.stem});
    for (const LinkOp& op : ops) {
        std::vector<int> lower;
        for (const int x : op.lower) lower.push_back(x + shift);
        Link(out.link[static_cast<size_t>(op.row)], op.upper + shift, lower);
    }
    return out;
}

}  // namespace FamilyTree
}  // namespace Ext
