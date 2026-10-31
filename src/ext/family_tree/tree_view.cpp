// ============================================================================
// src/ext/family_tree/tree_view.cpp -- the drawn tree as cells of a table (see tree_view.h)
// ============================================================================
// docs/extensions/handins/FAMILYTREE.md section 4a. tree_layout.cpp says where every box and line piece goes;
// this makes the cells. A box is `ribbonFamilyBox` (a button over a thin border, tinted for the picked person
// and the dead), a run of empty tiles is one space, a line tile is one of the sprites TreeLink1..15 (named
// for the arm bits), TreeGap or TreeGapStem. The window shows as many tiles as the table is wide and slides
// by `kTreeStep`; a box shows only whole.
//
// The caller must not build while one of the tree's own buttons is dispatching (the cells would be destroyed
// under it): a click only queues (Click), and the window's per-frame update applies it (TakeSlide / Build).
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/Font.h"
#include "ui/ButtonElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/RibbonElement.h"
#include "ui/TableElement.h"
#include "ui/String.h"
#include "ui/Dialog.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"

#include "ext/family_tree/family_tree.h"
#include "ext/family_tree/tree_layout.h"
#include "ext/family_tree/tree_view.h"
#include "ext/ui/find_element.h"
#include "ext/ui/list_width.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Ext {
namespace FamilyTree {

namespace {

constexpr const char* kTreeRowRsc = "Dialog/FamilyTreeWindow.rsc:ribbonFamilyTreeRow";
constexpr const char* kBoxRsc = "Dialog/FamilyTreePage.rsc:ribbonFamilyBox";
constexpr const char* kSpaceBoxRsc = "Dialog/FamilyTreePage.rsc:ribbonFamilySpaceBox";
constexpr const char* kSpaceLinkRsc = "Dialog/FamilyTreePage.rsc:ribbonFamilySpaceLink";
constexpr const char* kGapRsc = "Dialog/FamilyTreePage.rsc:imageFamilyGap";
constexpr const char* kGapStemRsc = "Dialog/FamilyTreePage.rsc:imageFamilyGapStem";

UI::ElementDescription* CellTemplate(const char* rsc) {
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(rsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) return nullptr;
    return const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
}

void SetParameterText(UI::Element* e, const std::string& text) {
    if (::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) {
        static_cast<UI::TextElement*>(e)->SetParameter(0, System::String(text.c_str()));
    }
}

UI::RibbonElement* AsRibbon(UI::Element* e) {
    if (!::Object::IsDerivedFromNullSafe(e, UI::RibbonElement::TypeId())) return nullptr;
    return static_cast<UI::RibbonElement*>(e);
}

// One row of the tree under construction: tiles are added left to right; a run of empty tiles becomes one
// space.
struct TreeRow {
    Object::InternalPtr<UI::Element> row;
    const UI::Dialog* dialog = nullptr;
    const char* spaceRsc = nullptr;
    int emptyTiles = 0;

    bool Begin(const UI::Dialog* d, const char* space) {
        dialog = d;
        spaceRsc = space;
        emptyTiles = 0;
        UI::ElementDescription* tmpl = CellTemplate(kTreeRowRsc);
        if (tmpl == nullptr) return false;
        tmpl->CreateInstance(&row, dialog);
        return AsRibbon(row.Get()) != nullptr;
    }
    void Add(Object::InternalPtr<UI::Element>& cell) { AsRibbon(row.Get())->AddItem(&cell); }
    void FlushEmpty() {
        if (emptyTiles <= 0) return;
        UI::ElementDescription* tmpl = CellTemplate(spaceRsc);
        if (tmpl != nullptr) {
            Object::InternalPtr<UI::Element> cell;
            tmpl->CreateInstance(&cell, dialog);
            if (cell.Get() != nullptr) {
                cell->_minWidth = emptyTiles * kTileW;
                Add(cell);
            }
        }
        emptyTiles = 0;
    }
    void Empty(int tiles) { emptyTiles += tiles; }
    void Image(const char* rsc) {
        FlushEmpty();
        UI::ElementDescription* tmpl = CellTemplate(rsc);
        if (tmpl == nullptr) return;
        Object::InternalPtr<UI::Element> cell;
        tmpl->CreateInstance(&cell, dialog);
        if (cell.Get() != nullptr) Add(cell);
    }
};

}  // namespace

bool TreeView::Attach(UI::Element* table, UI::Element* left, UI::Element* right, const UI::Dialog* dialog) {
    Detach();
    dialog_ = dialog;
    if (!::Object::IsDerivedFromNullSafe(table, UI::TableElement::TypeId())) return false;
    table_ = Object::InternalPtr<UI::Element>(table);
    if (::Object::IsDerivedFromNullSafe(left, UI::ButtonElement::TypeId())) {
        left_ = Object::InternalPtr<UI::Element>(left);
        left_->SetElementId(kTreeLeftId);
    }
    if (::Object::IsDerivedFromNullSafe(right, UI::ButtonElement::TypeId())) {
        right_ = Object::InternalPtr<UI::Element>(right);
        right_->SetElementId(kTreeRightId);
    }
    return true;
}

void TreeView::Detach() {
    table_ = Object::InternalPtr<UI::Element>();
    left_ = Object::InternalPtr<UI::Element>();
    right_ = Object::InternalPtr<UI::Element>();
    dialog_ = nullptr;
    boxPerson_.clear();
    boxBorder_.clear();
    slide_ = 0;
    off_ = 0;
}

void TreeView::Tint(uint32_t selected) {
    const Tree& tree = Town();
    for (size_t k = 0; k < boxBorder_.size() && k < boxPerson_.size(); ++k) {
        if (boxBorder_[k].Get() == nullptr) continue;
        const Person* p = tree.Find(boxPerson_[k]);
        const uint32_t color = boxPerson_[k] == selected ? 0x30a0c8ffu
                                                         : (p != nullptr && !p->Alive() ? 0x20808080u
                                                                 : 0x00ffffffu);
        boxBorder_[k]->SetColor(color);
    }
}

void TreeView::Build(uint32_t selected, bool center) {
    boxPerson_.clear();
    boxBorder_.clear();
    UI::Element* t = table_.Get();
    if (!::Object::IsDerivedFromNullSafe(t, UI::TableElement::TypeId())) return;
    UI::TableElement* table = static_cast<UI::TableElement*>(t);
    const Tree& tree = Town();
    const TreeLayout lay = BuildTreeLayout(tree, selected);
    UI::ElementDescription* boxTmpl = CellTemplate(kBoxRsc);
    if (lay.width == 0 || dialog_ == nullptr || boxTmpl == nullptr) {
        if (lay.width != 0) std::fprintf(stderr, "[FamilyTree] %s did not load: no tree\n", kBoxRsc);
        table->SetSize(0, 1);
        if (left_.Get() != nullptr) left_->Hide(true);
        if (right_.Get() != nullptr) right_->Hide(true);
        return;
    }
    // the window: as many tiles as the list is wide
    const int32_t px = Ext::ListWidth(table);
    win_ = px > 0 ? std::max<int32_t>(px / kTileW, 8) : 22;
    if (lay.width <= win_) {
        off_ = 0;
    } else {
        if (center) off_ = lay.selectedX - win_ / 2;
        off_ = std::max<int32_t>(0, std::min<int32_t>(off_, lay.width - win_));
    }
    const int32_t lo = off_;
    const int32_t hi = std::min<int32_t>(lay.width, off_ + win_);   // tiles lo .. hi - 1

    // the rows that show: a row of boxes if anyone is in it, a row of lines between two such rows
    std::vector<int> order;   // 2 * r for the row of boxes r, 2 * r + 1 for the line row under it
    for (int r = 0; r < kTreeBoxRows; ++r) {
        if (!lay.rowUsed[r]) continue;
        order.push_back(2 * r);
        if (r + 1 < kTreeBoxRows && lay.rowUsed[r + 1]) order.push_back(2 * r + 1);
    }
    table->SetSize(static_cast<int>(order.size()), 1);   // clears the old rows
    for (size_t k = 0; k < order.size(); ++k) {
        const int r = order[k] / 2;
        const bool boxes = order[k] % 2 == 0;
        TreeRow row;
        if (!row.Begin(dialog_, boxes ? kSpaceBoxRsc : kSpaceLinkRsc)) continue;
        for (int32_t x = lo; x < hi;) {
            if (boxes) {
                const TreeNode* here = nullptr;
                for (const TreeNode& n : lay.nodes)
                    if (n.row == r && n.x - 1 == x) here = &n;
                if (here != nullptr && here->x + 1 < hi) {   // a box shows only whole
                    row.FlushEmpty();
                    Object::InternalPtr<UI::Element> cell;
                    boxTmpl->CreateInstance(&cell, dialog_);
                    if (cell.Get() != nullptr && boxPerson_.size() < static_cast<size_t>(kMaxBoxes)) {
                        const int32_t index = static_cast<int32_t>(boxPerson_.size());
                        UI::Element* btn = Ext::FindElement<UI::ButtonElement>(cell.Get(), "buttonFamilyBox");
                        SetParameterText(Ext::FindElement<UI::TextElement>(cell.Get(), "textFamilyBoxName"),
                                         PersonLabel(here->id));
                        if (btn != nullptr) btn->SetElementId(kBoxId + index);
                        Object::InternalPtr<UI::Element> border;
                        boxBorder_.push_back(*cell->GetBorder(&border));
                        boxPerson_.push_back(here->id);
                        row.Add(cell);
                    }
                    x += 3;
                    continue;
                }
                const TreeGap* gap = nullptr;
                for (const TreeGap& g : lay.gaps)
                    if (g.row == r && g.x == x) gap = &g;
                if (gap != nullptr) {
                    row.Image(gap->stem ? kGapStemRsc : kGapRsc);
                } else {
                    row.Empty(1);
                }
                ++x;
            } else {
                const uint8_t mask = lay.link[static_cast<size_t>(r)][static_cast<size_t>(x)];
                if (mask == 0) {
                    row.Empty(1);
                } else {
                    char rsc[96];
                    std::snprintf(rsc, sizeof(rsc), "Dialog/FamilyTreePage.rsc:imageFamilyLink%d",
                                  static_cast<int>(mask));
                    row.Image(rsc);
                }
                ++x;
            }
        }
        row.FlushEmpty();
        table->SetChild(&row.row, static_cast<int32_t>(k));
    }
    Tint(selected);
    // the slide buttons only when something is out of view
    if (left_.Get() != nullptr) left_->Hide(!(off_ > 0));
    if (right_.Get() != nullptr) right_->Hide(!(off_ + win_ < lay.width));
}

int TreeView::Click(int32_t id, uint32_t& person) {
    if (id == kTreeLeftId || id == kTreeRightId) {
        slide_ = id == kTreeLeftId ? -kTreeStep : kTreeStep;
        return 2;
    }
    if (id >= kBoxId && id < kBoxId + static_cast<int32_t>(boxPerson_.size())) {
        person = boxPerson_[static_cast<size_t>(id - kBoxId)];
        return 1;
    }
    return 0;
}

bool TreeView::TakeSlide(uint32_t selected) {
    if (slide_ == 0) return false;
    off_ += slide_;
    slide_ = 0;
    Build(selected, false);
    return true;
}

}  // namespace FamilyTree
}  // namespace Ext
