// ============================================================================
// src/ext/storage_filter/panel_tab.cpp -- ext.storage_filter: the "Accepts" tab of a
// storage building's window
// ============================================================================
// NOT RETAIL. docs/extensions/handins/STORAGEFILTER.md, src/ext/ui/Dialog/StorageFilterTab.rsc. Standalone
// code, not a retail body: guards are ours to place, and each failed step is logged as `[StorageFilter]` and
// leaves the window as the game made it.
//
// The tab is added to the window's own tab control (the TabElement named "pageTab" of the building dialog the
// StorageUI sits in) by UI::TabElement::AddTab, after the game's own tabs, the way the Town Hall's Storage
// tab and the extension window's pages are. Its list is a table of cells, one per item the town knows, each a
// check box and the item's icon and name, laid out like ext.restricted_foods' page. A checked item is
// delivered to this building; an item the building's flags do not allow (the game's own test, as
// StorageComponent::AcceptsMaterial makes it) is not listed at all: the tab shows only what the building can
// store. A change is applied at once: the building's entry in the town's list (storage_filter.h) changes, and
// BeginFrame resolves it the next frame.
//
// One panel per StorageUI (a window): the elements are kept per window and dropped when the window goes.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/CheckElement.h"
#include "ui/TableElement.h"
#include "ui/LabelElement.h"
#include "ui/TabElement.h"
#include "ui/SpinnerElement.h"
#include "ui/SpinnerDescription.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/Dialog.h"
#include "ui/Sprite.h"
#include "ui/SpriteSheet.h"
#include "ui/StringTable.h"
#include "ui/String.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"
#include "Dialog/StorageUI.h"
#include "Dialog/ModDialog.h"   // ModDialog::kRowTintIdle (the Mods list's row tint)
#include "Game/GameUpdateLoop.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Component/MapComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/StorageComponent.h"
#include "Component/StorageLocationComponent.h"

#include "ext/registry.h"
#include "ext/storage_filter/storage_filter.h"
#include "ext/toolbar/toolbar.h"   // Lower, SetTableText
#include "ext/ui/find_element.h"
#include "ext/ui/list_width.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Ext {
namespace StorageFilter {

namespace {

constexpr const char* kButtonRsc = "Dialog/StorageFilterTab.rsc:buttonStorageFilterTab";
constexpr const char* kPageRsc   = "Dialog/StorageFilterTab.rsc:pageStorageFilter";
constexpr const char* kCellRsc   = "Dialog/StorageFilterTab.rsc:ribbonStorageFilterCell";
constexpr int32_t kRowId = 0x7A00;      // a cell's check box has the element id kRowId + its index
constexpr int32_t kSpinId = 0x9A00;     // a cell's cap spinner has the element id kSpinId + its index
constexpr int32_t kMaxRows = 4096;
constexpr uint32_t kRowTintRefused = 0x20ffa0a0u;   // a refused item's row (red; ARGB)
constexpr int kMinColumns = 2;   // the fewest columns the list is shaped to, however long the names
// pixels the list's width is allowed beyond what ListWidth reports, when counting columns
constexpr int32_t kSlack = 12;
// StorageFilterTab.rsc:tableStorageFilter's _width: the columns when the list's width is not known
constexpr int kColumns = 3;

// One item of the list.
struct Row {
    const RawMaterialDescription* mat = nullptr;
    std::string path;      // its template path: what the town's list stores
    std::string display;   // the name the game shows (its string table), else the internal name in words
    bool allowed = false;  // the check box: delivered here
    int32_t cap = 0;       // the spinner: most units delivered here, 0 = no limit
    Object::InternalPtr<UI::Element> spin;     // SpinnerElement (null while the cell is not made)
    Object::InternalPtr<UI::Element> check;    // CheckElement (null while the cell is not made)
    Object::InternalPtr<UI::Element> border;   // the cell's border (colour)
};

// One window's tab. The elements are owned by the window's element tree.
struct Panel {
    UI::TabElement* tabs = nullptr;
    int32_t index = -1;                       // the tab's index in `tabs`
    // the width of the window's first page (Inventory): the list is made as wide
    int32_t pageWidth = 0;
    int32_t barShift = 0;                     // how far the capacity bar moves right to clear our button
    bool barMoved = false;                    // PanelLayoutSlow has moved it
    int32_t tileX = 0;                        // the building's footprint corner: the list's key
    int32_t tileY = 0;
    Object::InternalPtr<UI::Element> table;   // "tableStorageFilter"
    std::vector<Row> rows;
};

std::unordered_map<const StorageUI*, Panel> g_panels;

UI::ElementDescription* LoadDescription(const char* path) {
    if (!::Object::ExternalFactory::_singleton) {
        return nullptr;
    }
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(path));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) {
        std::fprintf(stderr, "[StorageFilter] %s did not load as an ElementDescription (%p)\n", path,
                     static_cast<const void*>(ext));
        return nullptr;
    }
    // AddTab takes the description as an ExternalPtr (non-const); the factory hands out const objects.
    return const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
}

// The game's small number spinner (the Town Hall's count spinner), by its fixed path. A failure is logged
// once and the rows are built without a cap field.
constexpr const char* kSpinnerRsc = "Dialog/TownHall.rsc:spinnerCount";

UI::ElementDescription* FindSpinnerDescription() {
    static bool logged = false;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    const ::Object::External* ext =
        factory != nullptr ? factory->FindOrLoad(System::Filename(kSpinnerRsc)) : nullptr;
    if (!::Object::IsDerivedFromNullSafe(ext, UI::SpinnerDescription::TypeId())) {
        if (!logged) {
            logged = true;
            std::fprintf(stderr, "[StorageFilter] %s did not load as a spinner: rows without a cap field\n",
                         kSpinnerRsc);
        }
        return nullptr;
    }
    return const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
}

std::string TemplatePath(const void* external) {
    if (external == nullptr) {
        return std::string();
    }
    const System::Filename path = static_cast<const ::Object::External*>(external)->GetPath();
    return path._data != nullptr ? std::string(path._data) : std::string();
}

UI::TextElement* AsText(UI::Element* e) {
    return ::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId()) ? static_cast<UI::TextElement*>(e)
        : nullptr;
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

// The name of the material's icon in its sprite sheet: the small one the game's lists use, else
// the full-size one.
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

std::string DisplayName(const RawMaterialDescription* m) {
    std::string display = PrettyWords(m->_name._data != nullptr ? m->_name._data : "");
    if (m->_stringTable != nullptr && ::Object::IsDerivedFromNullSafe(m->_stringTable,
                                                                      UI::StringTable::TypeId())) {
        const int32_t index = m->_stringTable->GetIndexFromName(m->_name);
        if (index >= 0) {
            const System::String text = m->_stringTable->GetText(index);
            if (text._data != nullptr && text._data[0] != 0) {
                display = text._data;
            }
        }
    }
    return display;
}

const RawMaterialDescription* RawOf(const ComponentDescription* cd) {
    if (cd == nullptr) {
        return nullptr;
    }
    const uint32_t tid = RawMaterialDescription::TypeId();
    for (int32_t j = 0; j < cd->_subDescCount; ++j) {
        const Description* sub = cd->_subDescs[j];
        if (sub != nullptr && ::Object::IsDerivedFromNullSafe(sub, tid)) {
            return static_cast<const RawMaterialDescription*>(sub);
        }
    }
    return nullptr;
}

// The items of the list: every item the town has ever made or held (the list the Town Hall inventory shows),
// what this building holds now, and what it already refuses by name. Once per template path, by display name.
std::vector<Row> CollectRows(const StorageComponent* storage, int32_t tileX, int32_t tileY) {
    std::unordered_set<const RawMaterialDescription*> seen;
    std::vector<const RawMaterialDescription*> mats;
    auto add = [&](const RawMaterialDescription* m) {
        if (m != nullptr && m->_name._data != nullptr && m->_name._data[0] != 0 && seen.insert(m).second) {
            mats.push_back(m);
        }
    };
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game != nullptr && game->m_citizenList != nullptr) {
        const auto& list = game->m_citizenList->knownRawMaterials;
        for (int32_t i = 0; i < list._size; ++i) {
            add(RawOf(list._data[i]));
        }
    }
    for (int32_t i = 0; i < storage->storageSlots._size; ++i) {
        add(RawOf(storage->storageSlots[i]._resource));
    }
    // An item this building refuses but the town has not met this session (a saved choice): found
    // by its path.
    std::vector<std::string> refused = RefusedOf(tileX, tileY);
    for (const Entry& e : Entries()) {
        if (e.tileX == tileX && e.tileY == tileY) {
            for (const Entry::Cap& c : e.caps) {
                refused.push_back(c.path);
            }
        }
    }
    if (!refused.empty() && ::Object::ExternalFactory::_singleton != nullptr) {
        const uint32_t tid = RawMaterialDescription::TypeId();
        const ::Object::External* prev = nullptr;
        int guard = 0;
        while ((prev = ::Object::ExternalFactory::_singleton->FindNext(tid, prev)) != nullptr &&
               guard++ < 20000) {
            if (!::Object::IsDerivedFromNullSafe(prev, tid)) {
                continue;
            }
            const std::string path = TemplatePath(prev);
            for (const std::string& r : refused) {
                if (Toolbar::Lower(r.c_str()) == Toolbar::Lower(path.c_str())) {
                    add(static_cast<const RawMaterialDescription*>(prev));
                    break;
                }
            }
        }
    }
    std::vector<Row> rows;
    for (const RawMaterialDescription* m : mats) {
        if (static_cast<int32_t>(rows.size()) >= kMaxRows) {
            std::fprintf(stderr, "[StorageFilter] more than %d items: the rest are left out of the tab\n",
                         kMaxRows);
            break;
        }
        Row r;
        r.mat = m;
        r.path = TemplatePath(m);
        if (r.path.empty()) {
            continue;
        }
        r.display = DisplayName(m);
        if ((m->_flags & storage->rawMaterialFlags) == 0) {
            // the building's flags do not allow it (the game's own test, AcceptsMaterial): not listed
            continue;
        }
        r.allowed = !IsRefused(tileX, tileY, r.path);
        r.cap = CapOf(tileX, tileY, r.path);
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return Toolbar::Lower(a.display.c_str()) < Toolbar::Lower(b.display.c_str());
    });
    return rows;
}

void RefreshRow(Row& r) {
    if (::Object::IsDerivedFromNullSafe(r.check, UI::CheckElement::TypeId())) {
        static_cast<UI::CheckElement*>(r.check.Get())->_checked = r.allowed ? 1 : 0;
    }
    if (::Object::IsDerivedFromNullSafe(r.spin, UI::SpinnerElement::TypeId())) {
        UI::SpinnerElement* spin = static_cast<UI::SpinnerElement*>(r.spin.Get());
        if (!spin->IsActive() && spin->GetValue() != r.cap) {
            spin->SetValue(r.cap);
        }
        spin->Enable(r.allowed);   // a refused item has no cap to set
    }
    if (r.border.Get() != nullptr) {
        r.border->SetColor(r.allowed ? ModDialog::kRowTintIdle : kRowTintRefused);
    }
}

// (Re)creates the cells in the table, as ext.restricted_foods does: the cells first, filled, so their widths
// are known before the table is shaped; then as many columns as fit the list's width.
void BuildRows(const StorageUI* ui, Panel& panel) {
    if (!::Object::IsDerivedFromNullSafe(panel.table, UI::TableElement::TypeId())) {
        std::fprintf(stderr, "[StorageFilter] tableStorageFilter is not a TableElement\n");
        return;
    }
    UI::TableElement* table = static_cast<UI::TableElement*>(panel.table.Get());
    UI::ElementDescription* cellTemplate = LoadDescription(kCellRsc);
    if (cellTemplate == nullptr || ui->_dialog == nullptr) {
        return;
    }
    UI::ElementDescription* spinnerTemplate = FindSpinnerDescription();   // null: rows without a cap field
    int32_t listWidth = Ext::ListWidth(table);
    // The window is as wide as its first page (the Inventory list); our list sized itself from its own
    // contents and stopped short of the right edge. Widen the table (the list's content) by what is missing,
    // then measure the viewport again.
    if (panel.pageWidth > 0 && listWidth > 0 && table->_parent != nullptr &&
        table->_parent->_parent != nullptr) {
        UI::Element* container = table->_parent->_parent;
        int32_t cw = 0;
        int32_t ch = 0;
        container->GetPaddedSize(cw, ch);
        const int32_t overhead = cw - listWidth;   // the container's padding and its scroll bar
        const int32_t want = panel.pageWidth - overhead;
        if (want > listWidth) {
            table->_minWidth = want;
            table->SetNeedsSizeCalc();
            container->SetNeedsSizeCalc();
            listWidth = Ext::ListWidth(table);
        }
    }
    std::vector<Object::InternalPtr<UI::Element>> cells;
    std::vector<UI::LabelElement*> labels;   // each cell's name label (to shorten a name that will not fit)
    std::vector<int32_t> widths;             // each cell's padded width
    cells.reserve(panel.rows.size());
    int32_t widest = 0;
    for (Row& r : panel.rows) {
        Object::InternalPtr<UI::Element> cell;
        cellTemplate->CreateInstance(&cell, ui->_dialog->_dialog);
        if (cell.Get() == nullptr) {
            break;
        }
        UI::LabelElement* labelElem = Ext::FindElement<UI::LabelElement>(cell.Get(), "labelStorageFilterRow");
        Toolbar::SetTableText(labelElem, "StorageFilterParam");
        if (UI::TextElement* t = AsText(labelElem)) {
            t->SetParameter(0, System::String(r.display.c_str()));
        }
        if (const char* spriteName = SpriteName(r.mat)) {
            if (labelElem != nullptr) {
                UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(r.mat->_spriteSheet),
                                  System::String(spriteName));
                if (sprite.IsValid()) {
                    labelElem->SetSprite(&sprite);
                }
            }
        }
        r.spin = Object::InternalPtr<UI::Element>();
        if (spinnerTemplate != nullptr) {
            Object::InternalPtr<UI::Element> spinElem;
            spinnerTemplate->CreateInstance(&spinElem, ui->_dialog->_dialog);
            if (::Object::IsDerivedFromNullSafe(spinElem.Get(), UI::SpinnerElement::TypeId())) {
                cell->AddChild(&spinElem);
                r.spin = spinElem;
            }
        }
        int32_t w = 0;
        int32_t h = 0;
        cell->GetPaddedSize(w, h);
        widest = std::max(widest, w);
        cells.push_back(cell);
        labels.push_back(labelElem);
        widths.push_back(w);
    }
    int columns = kColumns;
    int32_t columnWidth = 0;
    int32_t oddPixels = 0;
    const int32_t gap = table->_spacing;
    if (listWidth > 0 && widest > 0) {
        // Two columns at least: a long name is shortened to fit (below) rather than dropping the list to one
        // column. The slack is the few pixels the list leaves free beside its scroll bar.
        columns = std::max(kMinColumns, static_cast<int>((listWidth + kSlack + gap) / (widest + gap)));
        const int32_t room = listWidth - gap * (columns - 1);
        columnWidth = room / columns;
        oddPixels = room % columns;
    }
    // At least one row: a table of zero rows has no height, which made the container's scroll bar run off
    // the screen. A name that is wider than its column is cut and ends in "...": the cell is measured again
    // until it fits.
    if (columnWidth > 0) {
        for (size_t k = 0; k < cells.size(); ++k) {
            if (widths[k] <= columnWidth || labels[k] == nullptr) {
                continue;
            }
            std::string name = panel.rows[k].display;
            for (int guard = 0; guard < 64 && name.size() > 3 && widths[k] > columnWidth; ++guard) {
                do {
                    name.pop_back();
                } while (name.size() > 3 && (static_cast<unsigned char>(name.back()) & 0xC0) == 0x80);
                if (UI::TextElement* t = AsText(labels[k])) {
                    t->SetParameter(0, System::String((name + "...").c_str()));
                }
                labels[k]->SetNeedsSizeCalc();
                int32_t w = 0;
                int32_t h = 0;
                cells[k]->GetPaddedSize(w, h);
                widths[k] = w;
            }
        }
    }
    const int rowCount = std::max(1, (static_cast<int>(cells.size()) + columns - 1) / columns);
    table->SetSize(rowCount, columns);   // clears the old cells
    for (size_t k = 0; k < cells.size(); ++k) {
        Row& r = panel.rows[k];
        Object::InternalPtr<UI::Element> cell = cells[k];
        if (columnWidth > 0) {
            const int column = static_cast<int>(k % static_cast<size_t>(columns));
            const int32_t target = columnWidth + (column < oddPixels ? 1 : 0);
            cell->_minWidth = target;
            // The room the cell has beyond its contents goes to the name, so the number box ends at the
            // cell's right edge.
            if (labels[k] != nullptr && widths[k] < target) {
                int32_t lw = 0;
                int32_t lh = 0;
                labels[k]->GetPaddedSize(lw, lh);
                labels[k]->_minWidth = std::max(labels[k]->_minWidth, lw + (target - widths[k]));
                labels[k]->SetNeedsSizeCalc();
            }
        }
        table->SetChild(&cell, static_cast<int32_t>(k));
        r.check = Object::InternalPtr<UI::Element>(
            Ext::FindElement<UI::CheckElement>(cell.Get(), "checkStorageFilterRow"));
        Object::InternalPtr<UI::Element> border;
        r.border = *cell->GetBorder(&border);
        if (r.check.Get() != nullptr) {
            r.check->SetElementId(kRowId + static_cast<int32_t>(k));
        }
        if (r.spin.Get() != nullptr) {
            UI::SpinnerElement* spin = static_cast<UI::SpinnerElement*>(r.spin.Get());
            spin->SetElementId(kSpinId + static_cast<int32_t>(k));
            spin->SetMinMax(0, kMaxCap);
            spin->SetToolTip(Toolbar::OurString("StorageFilterCapTip"));
        }
        RefreshRow(r);
    }
}

}  // namespace

void PanelOpenedSlow(StorageUI* ui) {
    if (ui == nullptr || g_panels.count(ui) != 0 || ui->_dialog == nullptr) {
        return;
    }
    ComponentEntity* entity = nullptr;
    if (::Object::IsDerivedFromNullSafe(ui->_dataSource, ComponentEntity::TypeId())) {
        entity = static_cast<ComponentEntity*>(ui->_dataSource);
    }
    StorageComponent* storage = entity != nullptr ? entity->FindComponent<StorageComponent>() : nullptr;
    const MapComponent* footprint = entity != nullptr ? entity->FindComponent<MapComponent>() : nullptr;
    if (storage == nullptr || footprint == nullptr || footprint->tileX0 < 0 || footprint->tileY0 < 0) {
        return;   // not a building we can key
    }
    // Only a storage building: one deliveries are routed to (barn, yard, cart, a mod's storage). A house, a
    // workshop or the Town Hall has a StorageComponent for its own stock and no storage location; its window
    // gets no Accepts tab.
    if (entity->FindComponent<StorageLocationComponent>() == nullptr) {
        return;
    }
    UI::Element* root = ui->_dialog->_root.Get();
    UI::TabElement* tabs = root != nullptr ? Ext::FindElement<UI::TabElement>(root, "pageTab") : nullptr;
    if (tabs == nullptr) {
        return;   // this building's window has no tab layout: the game's window stays as it is
    }
    UI::ElementDescription* button = LoadDescription(kButtonRsc);
    UI::ElementDescription* page = LoadDescription(kPageRsc);
    if (button == nullptr || page == nullptr) {
        return;
    }
    tabs->ComputeSize();
    int32_t pageWidth = 0;
    if (tabs->_tabCount > 0) {
        // the first tab's page: TabElement::GetContainerIndex(0) is 4 + 0 * 2 (private)
        UI::Element* first = tabs->Child(4).Get();
        if (first != nullptr) {
            int32_t ph = 0;
            first->GetPaddedSize(pageWidth, ph);
        }
    }
    const int32_t barBefore = tabs->_tabBarWidth;   // the tab buttons' width before ours
    Object::InternalPtr<UI::Element> pageRoot =
        tabs->AddTab(Object::ExternalPtr<UI::ElementDescription>(button),
                     Object::ExternalPtr<UI::ElementDescription>(page));
    if (pageRoot.Get() == nullptr) {
        std::fprintf(stderr, "[StorageFilter] AddTab failed: no Accepts tab\n");
        return;
    }
    tabs->ComputeSize();   // AddTab left the layout dirty
    // The capacity bar beside the tab buttons (Storage.rsc:capacity) starts 84 px from the left, which
    // clears one button; it would sit on top of ours. It is made after this tab (its controller comes later
    // in the window), so PanelLayoutSlow moves it right, by the width our button added, on the first update
    // that finds it.
    int32_t extra = tabs->_tabBarWidth - barBefore;
    if (extra <= 0) {
        extra = 84;   // the strip's width was not measured: one button's room, as Storage.rsc:capacity leaves
    }
    Panel panel;
    panel.tabs = tabs;
    panel.index = tabs->_tabCount - 1;
    panel.barShift = extra;
    panel.pageWidth = pageWidth;
    panel.tileX = footprint->tileX0;
    panel.tileY = footprint->tileY0;
    panel.table = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TableElement>(pageRoot.Get(),
                                                                                      "tableStorageFilter"));
    if (panel.table.Get() == nullptr) {
        std::fprintf(stderr, "[StorageFilter] no tableStorageFilter in the page: empty tab\n");
    }
    panel.rows = CollectRows(storage, panel.tileX, panel.tileY);
    BuildRows(ui, panel);
    g_panels[ui] = panel;
}

void PanelLayoutSlow(StorageUI* ui) {
    if (g_panels.empty()) {
        return;
    }
    const auto found = g_panels.find(ui);
    if (found == g_panels.end() || found->second.barMoved || ui->_dialog == nullptr) {
        return;
    }
    Panel& panel = found->second;
    UI::Element* root = ui->_dialog->_root.Get();
    UI::Element* header = root != nullptr ? Ext::FindElement<UI::Element>(root, "pageTabHeader") : nullptr;
    UI::Element* bar = (header != nullptr && header->ChildCount() > 0) ? header->Child(0).Get() : nullptr;
    if (bar == nullptr) {
        return;   // not made yet: the next update
    }
    bar->_leftPad += panel.barShift;
    bar->SetNeedsSizeCalc();
    panel.tabs->SetNeedsSizeCalc();
    panel.barMoved = true;
}

bool PanelEventSlow(StorageUI* ui, UI::EventId msg, int32_t id) {
    if (g_panels.empty() || (msg != UI::EventId::Activated && msg != UI::EventId::ValueChanged)) {
        return false;
    }
    const auto found = g_panels.find(ui);
    if (found == g_panels.end()) {
        return false;
    }
    Panel& panel = found->second;
    const int32_t count = static_cast<int32_t>(panel.rows.size());
    if (msg == UI::EventId::ValueChanged && id >= kSpinId && id < kSpinId + count) {
        Row& row = panel.rows[static_cast<size_t>(id - kSpinId)];
        if (::Object::IsDerivedFromNullSafe(row.spin, UI::SpinnerElement::TypeId())) {
            row.cap = static_cast<UI::SpinnerElement*>(row.spin.Get())->GetValue();
        }
        SetCap(panel.tileX, panel.tileY, row.path, row.cap);
        return true;
    }
    if (msg != UI::EventId::Activated || id < kRowId || id >= kRowId + count) {
        return false;
    }
    Row& r = panel.rows[static_cast<size_t>(id - kRowId)];
    if (::Object::IsDerivedFromNullSafe(r.check, UI::CheckElement::TypeId())) {
        r.allowed = static_cast<UI::CheckElement*>(r.check.Get())->_checked != 0;
    } else {
        r.allowed = !r.allowed;
    }
    SetRefused(panel.tileX, panel.tileY, r.path, !r.allowed);
    RefreshRow(r);
    return true;
}

void PanelGone(const StorageUI* ui) {
    if (!g_panels.empty()) {
        g_panels.erase(ui);
    }
}

}  // namespace StorageFilter
}  // namespace Ext
