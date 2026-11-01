// src/ext/family_tree/tree_view.h -- the drawn tree of ext.family_tree, as a view onto a table of the tree
// window
//
// NOT RETAIL. Docs: docs/extensions/handins/FAMILYTREE.md section 4a. The arithmetic (where each box and line
// piece goes) is tree_layout.h; this turns it into cells of the window's table (Dialog/FamilyTreeWindow.rsc:
// tableFamilyTree) and answers its buttons. Exe only (it builds UI elements).
#pragma once

#include <cstdint>
#include <vector>

#include "object/Internal.h"

namespace UI {
class Dialog;
class Element;
}

namespace Ext {
namespace FamilyTree {

constexpr int32_t kTreeLeftId = 11;      // local element ids of the window (its own title bar uses 0 and 1)
constexpr int32_t kTreeRightId = 12;
constexpr int32_t kTreeDetailsId = 13;
constexpr int32_t kBoxId = 6000;         // a box of the tree: kBoxId + its place in the view
constexpr int32_t kMaxBoxes = 100;
constexpr int32_t kTileW = 26;           // the drawn tree's tile: tools/ext_tree_tiles.py
constexpr int32_t kTreeStep = 8;         // a click on < or > slides the tree two boxes

class TreeView {
public:
    // Takes the window's table and its two slide buttons (any may be null but the table) and gives the
    // buttons their ids. `dialog` is the window's resource (what CreateInstance reads). False when the table
    // is not a table.
    bool Attach(UI::Element* table, UI::Element* left, UI::Element* right, const UI::Dialog* dialog);
    // Lets go of every element (the window is closing).
    void Detach();
    // Draws the tree around `selected`. The window shows as many tiles as the table is wide; `center` puts
    // the selected person in the middle when the tree is wider.
    void Build(uint32_t selected, bool center);
    // Re-tints the boxes (the picked person, the dead).
    void Tint(uint32_t selected);
    // A control of the window was used (local id). 1: a box, `person` set; 2: a slide, queued; 0: not the
    // tree's.
    int Click(int32_t id, uint32_t& person);
    // A queued slide, applied (redraws); false when none was queued.
    bool TakeSlide(uint32_t selected);

private:
    Object::InternalPtr<UI::Element> table_;
    Object::InternalPtr<UI::Element> left_;
    Object::InternalPtr<UI::Element> right_;
    const UI::Dialog* dialog_ = nullptr;
    std::vector<uint32_t> boxPerson_;
    std::vector<Object::InternalPtr<UI::Element>> boxBorder_;
    int32_t off_ = 0;      // the first tile showing
    int32_t win_ = 0;      // tiles showing
    int32_t slide_ = 0;    // a slide waiting for the tick
};

}  // namespace FamilyTree
}  // namespace Ext
