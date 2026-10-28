// ============================================================================
// src/ext/item_priority/toolbar_page.cpp -- ext.item_priority's page in the extension window
// ============================================================================
// docs/extensions/handins/ITEMPRIO.md, src/ext/toolbar/toolbar.h. The town ranks the items its citizens fetch
// from a storage, per category: a tab strip chooses the category (tools, fuel, clothing, health), and the
// showing tab's list shows the items of that category, one cell each: a check box (ranked or not), the item's
// icon and name, its place in the ranking, and Top / Up / Down. Checking an item ranks it at the bottom, unchecking removes
// it; Up / Down move a ranked item past its neighbour, Top to the front. Every change applies at once (live: no Apply). Ranked
// items come first, in rank order, then the rest by name. A "Show all items" check box under the list is off
// by default (the items the town has made or stored, and the ranked ones) and is the player's choice for the
// current town.
//
// The page's objects are ours (src/ext/ui/Dialog/ItemPriorityPage.rsc); the texts are in
// ExtensionsStringTable.rsc (the ItemPriority* keys). The ranked lists are the town's own state (class C,
// item_priority.h): this page reads and writes them through Current() and Apply(), and the save tail carries
// them (save_state.cpp). Element ids are the host's (toolbar.h): the page gets an `idBase` and answers local
// ids. ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/Font.h"
#include "ui/ImageElement.h"
#include "ui/ButtonElement.h"
#include "ui/CheckElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/RibbonElement.h"
#include "ui/TabElement.h"
#include "ui/TableElement.h"
#include "ui/LabelElement.h"
#include "ui/Sprite.h"
#include "ui/SpriteSheet.h"
#include "ui/StringTable.h"
#include "ui/String.h"
#include "ui/Dialog.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"
#include "Game/GameUpdateLoop.h"
#include "Component/RawMaterialDescription.h"
#include "Entity/CitizenBehavior.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"

#include "ext/item_priority/item_priority.h"
#include "ext/registry.h"
#include "ext/toolbar/toolbar.h"
#include "ext/ui/find_element.h"
#include "ext/ui/list_width.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

namespace Ext {
namespace ItemPriority {

namespace {

// Local element ids (the host adds `idBase`): the category tab buttons 10 + their place in the strip, the check
// box
// 20, the cells 100 + 5 * their position (+ 0 check, + 1 top, + 2 up, + 3 down, + 4 never).
constexpr int32_t kCategoryId = 10;
constexpr int32_t kShowAllId = 20;
constexpr int32_t kCellId = 100;
constexpr int32_t kCellStride = 5;   // ids per cell: check, top, up, down, never
constexpr int32_t kMaxCells = (Toolbar::kPageIdSpan - kCellId) / kCellStride;
constexpr int kButtonCount = 4;
constexpr int kCategoryOrder[kButtonCount] = { kTool, kFuel, kClothing, kHealth };   // the order in the .rsc

constexpr const char* kWindowRsc = "Dialog/ExtensionsWindow.rsc";
constexpr const char* kCellRsc = "Dialog/ItemPriorityPage.rsc:ribbonItemPriorityCell";

UI::TextElement* AsText(UI::Element* e) {
    if (!::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) {
        return nullptr;
    }
    return static_cast<UI::TextElement*>(e);
}

void SetParameterText(UI::Element* e, const char* text) {
    if (UI::TextElement* t = AsText(e)) {
        t->SetParameter(0, System::String(text));
    }
}

// The rank number ends at the right of its column: the left pad is the column width less the text's own width.
constexpr int32_t kRankWidth = 38;   // the rank column: padding + text ("Never" is the widest)
void SetRankText(UI::Element* e, const char* text) {
    SetParameterText(e, text);
    if (UI::TextElement* t = AsText(e)) {
        int32_t w = 0;
        int32_t h = 0;
        if (t->_font != nullptr) {
            t->_font->ComputeStringLayout(System::String(text), false, 0, -1, static_cast<UI::Font::Justify>(0), w, h,
                                          nullptr);
        }
        t->_leftPad = std::max<int32_t>(0, kRankWidth - w);
    }
}

// "RabbitMeat" -> "Rabbit Meat": the fallback when a material's own string table does not name it.
std::string PrettyWords(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        const char p = i ? in[i - 1] : 0;
        const bool up = std::isupper(static_cast<unsigned char>(c)) != 0;
        const bool prevLowOrDigit =
            std::islower(static_cast<unsigned char>(p)) || std::isdigit(static_cast<unsigned char>(p));
        if (i && up && prevLowOrDigit) {
            out.push_back(' ');
        }
        out.push_back(c == '_' ? ' ' : c);
    }
    if (!out.empty()) {
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    }
    return out;
}

// The place of `category` in the tab strip (kCategoryOrder); 0 for one that is not in it.
int CategoryIndex(int category) {
    for (int i = 0; i < kButtonCount; ++i) {
        if (kCategoryOrder[i] == category) {
            return i;
        }
    }
    return 0;
}

// What a category goes by in the log.
const char* CategoryName(int category) {
    switch (category) {
        case kFuel:     return "fuel";
        case kClothing: return "clothing";
        case kHealth:   return "health";
        default:        return "tools";
    }
}

// One item of the shown category in the running game.
struct Item {
    const RawMaterialDescription* mat = nullptr;
    std::string path;       // its template path: what the list stores
    std::string display;    // the name the game shows (its string table), else the internal name in words
    bool        inTown = false;   // the town has had a material of this name
};

// The name of the material's icon in its sprite sheet: the small one the game's lists use, else the full-size
// one; null when the material has no sheet or names no sprite.
const char* SpriteName(const RawMaterialDescription* m) {
    if (m == nullptr || m->_spriteSheet == nullptr) {
        return nullptr;
    }
    if (m->_nameSmall._data != nullptr && m->_nameSmall._data[0] != 0) {
        return m->_nameSmall._data;
    }
    if (m->_name._data != nullptr && m->_name._data[0] != 0) {
        return m->_name._data;
    }
    return nullptr;
}

// The raw-material descriptions of the town's known-material list (CitizenBehavior +0x110): "every item the
// town has ever made or held" (the list the Town Hall inventory shows; see the Foods page).
std::unordered_set<const void*> TownMaterials() {
    std::unordered_set<const void*> out;
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_citizenList == nullptr) {
        return out;
    }
    const auto& list = game->m_citizenList->field_0x110;
    const uint32_t tid = RawMaterialDescription::TypeId();
    for (int32_t i = 0; i < list._size; ++i) {
        const ComponentDescription* cd = list._data[i];
        if (cd == nullptr) {
            continue;
        }
        for (int32_t j = 0; j < cd->_subDescCount; ++j) {
            const Description* sub = cd->_subDescs[j];
            if (sub != nullptr && ::Object::IsDerivedFromNullSafe(sub, tid)) {
                out.insert(sub);
            }
        }
    }
    return out;
}

// Every loaded RawMaterialDescription with `category`'s flag bit, once per template path, by display name.
std::vector<Item> CollectItems(int category) {
    std::vector<Item> items;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    const int bit = FlagBit(category);
    if (factory == nullptr || bit < 0) {
        return items;
    }
    const std::unordered_set<const void*> town = TownMaterials();
    const uint32_t tid = RawMaterialDescription::TypeId();
    const ::Object::External* prev = nullptr;
    int guard = 0;
    while ((prev = factory->FindNext(tid, prev)) != nullptr && guard++ < 20000) {
        if (!::Object::IsDerivedFromNullSafe(prev, tid)) {
            continue;
        }
        const RawMaterialDescription* m = static_cast<const RawMaterialDescription*>(prev);
        if (((m->_flags >> bit) & 1) == 0) {
            continue;
        }
        if (m->_name._data == nullptr || m->_name._data[0] == 0) {
            continue;
        }
        Item it;
        it.mat = m;
        it.path = TemplatePath(prev);
        if (it.path.empty()) {
            continue;
        }
        it.inTown = town.count(m) != 0;
        it.display = PrettyWords(m->_name._data);
        if (m->_stringTable != nullptr &&
            ::Object::IsDerivedFromNullSafe(m->_stringTable, UI::StringTable::TypeId())) {
            const int32_t index = m->_stringTable->GetIndexFromName(m->_name);
            if (index >= 0) {
                const System::String text = m->_stringTable->GetText(index);
                if (text._data != nullptr && text._data[0] != 0) {
                    it.display = text._data;
                }
            }
        }
        items.push_back(it);
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return Toolbar::Lower(a.display.c_str()) < Toolbar::Lower(b.display.c_str());
    });
    return items;
}

// -----------------------------------------------------------------------------------------------------------
// The page's state: one page at a time (the extension window is one dialog)
// -----------------------------------------------------------------------------------------------------------
// A control inside a cell is handled while the dialog is still dispatching ITS event, so a click on a check
// box or on Top / Up / Down never destroys cells: it edits the list and re-fills the cells that exist (Resort).
// Cells are created or destroyed only from the category buttons and the "Show all items" check box, which sit
// outside the table.
struct Row {
    Item item;
    int  rank = kUnranked;     // the item's place in the town's list (0 = best), or kUnranked
    bool never = false;        // the item is on the town's Never list
    int  shownRank = 0;        // its number on the page among the ranked items that are loaded (1 = best)
    bool pinned = false;       // shown when the cells were built: stays shown while it is edited
};

struct Cell {
    Object::InternalPtr<UI::Element> root;       // ribbonItemPriorityCell
    Object::InternalPtr<UI::Element> label;      // labelItemPriorityRow
    Object::InternalPtr<UI::Element> check;      // CheckElement
    Object::InternalPtr<UI::Element> rankText;   // textItemPriorityRank
    Object::InternalPtr<UI::Element> border;     // the cell's border (colour)
    int32_t labelMinWidth = 0;                   // the label's own _minWidth, from the .rsc
};

std::vector<Row> g_rows;                         // every item of the shown category in the game, by name
std::vector<Cell> g_cells;                       // the table's cells, by position
std::vector<size_t> g_order;                     // g_order[k] = the g_rows index the cell at position k shows
int32_t g_idBase = 0;
int32_t g_rowWidth = 0;                          // the shown list's width (Ext::ListWidth); 0: rows keep their own
int g_category = kTool;                          // the player's choice for this town
bool g_showAll = false;                          // the check box; the player's choice for this town
uint32_t g_town = ~0u;                           // the lock epoch g_category and g_showAll belong to
Object::InternalPtr<UI::Element> g_tables[kButtonCount];   // "tableItemPriority<Category>", strip order
Object::InternalPtr<UI::Element> g_showAllCheck; // "checkItemPriorityShowAll"
Object::InternalPtr<UI::Element> g_strip;        // "tabItemPriorityCategories"

// The table of the tab that is showing.
UI::Element* ShownTable() {
    return g_tables[CategoryIndex(g_category)].Get();
}

bool Shown(const Row& r) {
    // a ranked or Never item never vanishes
    return g_showAll || r.item.inTown || r.rank != kUnranked || r.never || r.pinned;
}

void RefreshShowAll() {
    UI::Element* check = g_showAllCheck.Get();
    if (::Object::IsDerivedFromNullSafe(check, UI::CheckElement::TypeId())) {
        static_cast<UI::CheckElement*>(check)->_checked = g_showAll ? 1 : 0;
    }
}

// Re-reads the town's list into every row's rank and shown number.
void RankRows() {
    const Lists& lists = Current();
    for (Row& r : g_rows) {
        r.rank = RankOf(lists, g_category, r.item.path);
        r.never = IsNever(lists, g_category, r.item.path);
    }
    std::vector<Row*> ranked;
    for (Row& r : g_rows) {
        r.shownRank = 0;
        if (r.rank != kUnranked) {
            ranked.push_back(&r);
        }
    }
    std::sort(ranked.begin(), ranked.end(), [](const Row* a, const Row* b) { return a->rank < b->rank; });
    int n = 0;
    for (Row* r : ranked) {
        r->shownRank = ++n;
    }
}

// The order the shown rows go in: ranked first (rank order), then by name (g_rows is already by name).
void SortOrder() {
    std::stable_sort(g_order.begin(), g_order.end(),
                     [](size_t a, size_t b) { return g_rows[a].rank < g_rows[b].rank; });
}

// Fills the cell at position k from the row it shows: icon and name, check box, rank number, tint.
// The row is as wide as the list, and the label takes what the check box, the rank and the buttons leave. A
// ribbon gives the space it has left only to its LAST element (RibbonElement::ComputeLayout, the ExpandWidth
// arm) and the label sits in the middle of the row, so the label's own floor is raised by that space instead.
// Run after every change of the label's text: a cell shows another item after a re-sort.
void FitRow(Cell& cell) {
    UI::Element* root = cell.root.Get();
    UI::Element* label = cell.label.Get();
    if (root == nullptr || g_rowWidth <= 0) {
        return;
    }
    root->_minWidth = g_rowWidth;
    if (label == nullptr) {
        return;
    }
    label->_minWidth = cell.labelMinWidth;
    label->SetNeedsSizeCalc();   // and every ancestor: the cell and the table measure again
    root->ComputeSize();
    const int32_t natural = root->_layoutWidth + root->_leftPad + root->_rightPad;
    if (natural < g_rowWidth) {
        int32_t w = 0;
        int32_t h = 0;
        label->GetPaddedSize(w, h);
        label->_minWidth = w + (g_rowWidth - natural);
        label->SetNeedsSizeCalc();
    }
}

void BindCell(size_t k) {
    Cell& cell = g_cells[k];
    const Row& r = g_rows[g_order[k]];
    UI::Element* labelElem = cell.label.Get();
    Toolbar::SetTableText(labelElem, "ItemPriorityParam");
    SetParameterText(labelElem, r.item.display.c_str());
    const RawMaterialDescription* mat = r.item.mat;
    if (const char* spriteName = SpriteName(mat)) {
        if (::Object::IsDerivedFromNullSafe(labelElem, UI::LabelElement::TypeId())) {
            UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(mat->_spriteSheet),
                              System::String(spriteName));
            if (sprite.IsValid()) {
                static_cast<UI::LabelElement*>(labelElem)->SetSprite(&sprite);
            }
        }
    }
    if (::Object::IsDerivedFromNullSafe(cell.check.Get(), UI::CheckElement::TypeId())) {
        static_cast<UI::CheckElement*>(cell.check.Get())->_checked = r.rank != kUnranked ? 1 : 0;
    }
    if (cell.rankText.Get() != nullptr) {
        if (r.never) {
            SetRankText(cell.rankText.Get(), "Never");
        } else if (r.rank != kUnranked) {
            SetRankText(cell.rankText.Get(), std::to_string(r.shownRank).c_str());
        } else {
            SetRankText(cell.rankText.Get(), "-");
        }
    }
    // Tinted while the item is ranked (blue) or Never (red), the Mods list's white otherwise.
    if (cell.border.Get() != nullptr) {
        cell.border->SetColor(r.never ? 0x30ff5050u : r.rank != kUnranked ? 0x20a0c8ffu : 0x10ffffffu);
    }
    FitRow(cell);
}

// The list changed: re-rank, re-sort the shown rows and re-fill the cells that exist. Never creates or
// destroys a cell.
void Resort() {
    RankRows();
    SortOrder();
    for (size_t k = 0; k < g_cells.size(); ++k) {
        BindCell(k);
    }
}

// (Re)creates the cells of the shown items in the one-column table, as TownHallUI::InitInventory fills its
// inventory table. Called from the category buttons and the check box (outside the table), never from a
// cell's own event.
void BuildRows() {
    g_cells.clear();
    g_order.clear();
    // The other tabs' tables are emptied: their cells would carry the same ids as this tab's.
    for (int i = 0; i < kButtonCount; ++i) {
        UI::Element* other = g_tables[i].Get();
        if (i != CategoryIndex(g_category) &&
            ::Object::IsDerivedFromNullSafe(other, UI::TableElement::TypeId())) {
            static_cast<UI::TableElement*>(other)->SetSize(0, 1);
        }
    }
    if (ShownTable() == nullptr) {
        return;
    }
    if (!::Object::IsDerivedFromNullSafe(ShownTable(), UI::TableElement::TypeId())) {
        std::fprintf(stderr, "[ItemPriority] the shown tab's table is not a TableElement\n");
        return;
    }
    UI::TableElement* table = static_cast<UI::TableElement*>(ShownTable());
    RefreshShowAll();

    const UI::Dialog* dialog = nullptr;
    const ::Object::External* dlgExt =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kWindowRsc));
    if (::Object::IsDerivedFromNullSafe(dlgExt, UI::Dialog::TypeId())) {
        dialog = static_cast<const UI::Dialog*>(dlgExt);
    }
    const ::Object::External* cellExt =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kCellRsc));
    if (dialog == nullptr || !::Object::IsDerivedFromNullSafe(cellExt, UI::ElementDescription::TypeId())) {
        std::fprintf(stderr, "[ItemPriority] %s / %s did not load: no cells\n", kWindowRsc, kCellRsc);
        return;
    }
    UI::ElementDescription* cellTemplate =
        const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(cellExt));

    RankRows();
    for (Row& r : g_rows) {
        r.pinned = false;
    }
    for (Row& r : g_rows) {
        r.pinned = Shown(r);   // what is shown now stays shown while the items are edited
    }
    for (size_t i = 0; i < g_rows.size(); ++i) {
        if (g_rows[i].pinned && static_cast<int32_t>(g_order.size()) < kMaxCells) {
            g_order.push_back(i);
        }
    }
    SortOrder();
    g_rowWidth = Ext::ListWidth(table);
    table->SetSize(static_cast<int>(g_order.size()), 1);   // clears the old cells

    for (size_t k = 0; k < g_order.size(); ++k) {
        Object::InternalPtr<UI::Element> cell;
        cellTemplate->CreateInstance(&cell, dialog);
        if (cell.Get() == nullptr) {
            break;
        }
        table->SetChild(&cell, static_cast<int32_t>(k));

        Cell c;
        c.root = cell;
        c.label = Object::InternalPtr<UI::Element>(
            Ext::FindElement<UI::LabelElement>(cell.Get(), "labelItemPriorityRow"));
        if (c.label.Get() != nullptr) {
            c.labelMinWidth = c.label->_minWidth;
        }
        c.rankText = Object::InternalPtr<UI::Element>(
            Ext::FindElement<UI::TextElement>(cell.Get(), "textItemPriorityRank"));
        c.check = Object::InternalPtr<UI::Element>(
            Ext::FindElement<UI::CheckElement>(cell.Get(), "checkItemPriorityRow"));
        // by position: ids never change
        const int32_t base = g_idBase + kCellId + kCellStride * static_cast<int32_t>(k);
        if (c.check.Get() != nullptr) {
            c.check->SetElementId(base);
        }
        UI::RibbonElement* moves = Ext::FindElement<UI::RibbonElement>(cell.Get(), "ribbonItemPriorityMoves");
        if (moves != nullptr) {
            if (moves->ChildCount() >= 4) {
                moves->Child(0).Get()->SetElementId(base + 1);   // Top
                moves->Child(1).Get()->SetElementId(base + 2);   // Up
                moves->Child(2).Get()->SetElementId(base + 3);   // Down
                moves->Child(3).Get()->SetElementId(base + 4);   // Never
            }
        }
        Object::InternalPtr<UI::Element> border;
        c.border = *cell->GetBorder(&border);
        g_cells.push_back(c);
        BindCell(k);
    }
    std::fprintf(stderr, "[ItemPriority] %d of %d item(s) shown (%s), rows %d px wide\n",
                 static_cast<int>(g_cells.size()), static_cast<int>(g_rows.size()), CategoryName(g_category),
                 static_cast<int>(g_rowWidth));
}

// Reads the shown category's items from the game and builds the cells.
void Rebuild() {
    const std::vector<Item> items = CollectItems(g_category);
    g_rows.clear();
    for (const Item& it : items) {
        Row r;
        r.item = it;
        g_rows.push_back(r);
    }
    BuildRows();
}

// One cell's control was used: edit the town's list for the shown category, live, and re-fill the cells in
// place.
// action 0 = check box, 1 = top, 2 = up, 3 = down, 4 = never.
void ApplyCellAction(size_t position, int action) {
    if (position >= g_order.size()) {
        return;
    }
    const Row& r = g_rows[g_order[position]];
    Lists lists = Current();
    bool changed = false;
    if (action == 0) {
        UI::Element* check = g_cells[position].check.Get();
        const bool wantRanked = ::Object::IsDerivedFromNullSafe(check, UI::CheckElement::TypeId())
                                    ? static_cast<UI::CheckElement*>(check)->_checked != 0
                                    : r.rank == kUnranked;
        changed = wantRanked ? Rank(lists, g_category, r.item.path) : Unrank(lists, g_category, r.item.path);
    } else if (action == 4) {   // Never: on, or off again (the item is then unranked)
        changed = r.never ? ClearNever(lists, g_category, r.item.path)
                          : SetNever(lists, g_category, r.item.path);
    } else if (r.rank != kUnranked) {
        // Move past the neighbouring ranked item that is loaded in this game (a path the game no longer has
        // stays where it is in the list and is stepped over).
        std::vector<std::string>& l = lists.Of(g_category);
        if (action == 1) {   // Top: to the front of the list, the others keep their order
            if (r.rank > 0) {
                const std::string path = l[static_cast<size_t>(r.rank)];
                l.erase(l.begin() + r.rank);
                l.insert(l.begin(), path);
                changed = true;
            }
            if (changed) {
                Apply(lists);
            }
            std::fprintf(stderr, "[ItemPriority] %s action 1 -> %s\n", r.item.path.c_str(),
                         changed ? "changed" : "no change");
            Resort();
            return;
        }
        const int step = action == 2 ? -1 : +1;
        const int from = r.rank;
        int to = from + step;
        while (to >= 0 && to < static_cast<int>(l.size())) {
            bool loaded = false;
            for (const Row& o : g_rows) {
                if (RankOf(lists, g_category, o.item.path) == to) {
                    loaded = true;
                    break;
                }
            }
            if (loaded) {
                break;
            }
            to += step;
        }
        if (to >= 0 && to < static_cast<int>(l.size())) {
            std::swap(l[static_cast<size_t>(from)], l[static_cast<size_t>(to)]);
            changed = true;
        }
    }
    if (changed) {
        Apply(lists);
    }
    std::fprintf(stderr, "[ItemPriority] %s action %d -> %s\n", r.item.path.c_str(), action,
                 changed ? "changed" : "no change");
    Resort();
}

void PageOpen(UI::Element* page, int32_t idBase) {
    g_idBase = idBase;
    // The category and the "Show all items" choice are per town: a new game, a load or a reload starts them
    // at tools and unchecked.
    if (g_town != LockEpoch()) {
        g_town = LockEpoch();
        g_category = kTool;
        g_showAll = false;
    }
    // The category buttons are the strip's tab buttons: child 3 + 2i of its TabElement (TabElement.h; the
    // host numbers the window's own tabs the same way). The strip switches its own tab when one is pressed.
    g_strip = Object::InternalPtr<UI::Element>(
        Ext::FindElement<UI::TabElement>(page, "tabItemPriorityCategories"));
    if (UI::TabElement* strip = static_cast<UI::TabElement*>(g_strip.Get())) {
        UI::Element** kids = strip->_children.GetData();
        const int nKids = strip->_children.GetSize();
        for (int i = 0; i < kButtonCount; ++i) {
            if (3 + 2 * i < nKids && kids[3 + 2 * i] != nullptr) {
                kids[3 + 2 * i]->SetElementId(idBase + kCategoryId + i);
            }
        }
        strip->SetActiveTab(CategoryIndex(g_category));   // the tab the player was on in this town
    }
    g_showAllCheck = Object::InternalPtr<UI::Element>(
        Ext::FindElement<UI::CheckElement>(page, "checkItemPriorityShowAll"));
    if (g_showAllCheck.Get() != nullptr) {
        g_showAllCheck->SetElementId(idBase + kShowAllId);
    }
    static const char* const kTableNames[kButtonCount] = { "tableItemPriorityTool", "tableItemPriorityFuel",
                                                           "tableItemPriorityClothing",
                                                           "tableItemPriorityHealth" };   // the strip's order
    int found = 0;
    for (int i = 0; i < kButtonCount; ++i) {
        g_tables[i] =
            Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TableElement>(page, kTableNames[i]));
        if (g_tables[i].Get() != nullptr) {
            ++found;
        }
    }
    if (found == 0) {
        std::fprintf(stderr, "[ItemPriority] no tableItemPriority<Category> in the page: empty page\n");
        return;
    }
    Rebuild();
    std::fprintf(stderr, "[ItemPriority] page opened: %s, %s\n", CategoryName(g_category),
                 g_showAll ? "all items" : "the town's items");
}

bool PageChanged(UI::Element* /*page*/, int32_t id) {
    if (id >= kCategoryId && id < kCategoryId + kButtonCount) {
        g_category = kCategoryOrder[id - kCategoryId];
        Rebuild();
        return true;
    }
    if (id == kShowAllId) {
        // The check box: items the town has had <-> all items. Ranked items stay either way.
        UI::Element* check = g_showAllCheck.Get();
        if (::Object::IsDerivedFromNullSafe(check, UI::CheckElement::TypeId())) {
            g_showAll = static_cast<UI::CheckElement*>(check)->_checked != 0;
        } else {
            g_showAll = !g_showAll;
        }
        BuildRows();   // the same items, a different set of cells
        return true;
    }
    if (id >= kCellId && id < kCellId + kCellStride * static_cast<int32_t>(g_cells.size())) {
        const int32_t rel = id - kCellId;
        ApplyCellAction(static_cast<size_t>(rel / kCellStride), static_cast<int>(rel % kCellStride));
        return true;
    }
    return false;
}

void PageClose() {
    g_rows.clear();
    g_cells.clear();
    g_order.clear();
    for (Object::InternalPtr<UI::Element>& t : g_tables) {
        t = Object::InternalPtr<UI::Element>();
    }
    g_showAllCheck = Object::InternalPtr<UI::Element>();
    g_strip = Object::InternalPtr<UI::Element>();
}

}  // namespace
}  // namespace ItemPriority

namespace Toolbar {

const Contribution& ItemPriorityPage() {
    static const Contribution c = { ItemPriority::kId, ItemPriority::kPageCaption, ItemPriority::kPageRsc,
                                    ItemPriority::PageOpen, ItemPriority::PageChanged,
                                    ItemPriority::PageClose };
    return c;
}

}  // namespace Toolbar
}  // namespace Ext
