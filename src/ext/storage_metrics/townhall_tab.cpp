// ============================================================================
// src/ext/storage_metrics/townhall_tab.cpp -- ext.storage_metrics: the Storage tab of the Town Hall window
// ============================================================================
// NOT RETAIL. docs/extensions/handins/STORAGEMETRICS.md, src/ext/ui/Dialog/StorageMetricsTab.rsc.
// Standalone code, not a retail body: guards are ours to place, and each failed step is logged as
// `[StorageMetrics]` and leaves the window as the game made it.
//
// The tab is added to the window's own tab control by UI::TabElement::AddTab, after the game's tabs, the
// way the extension window and the Options dialog's Enhanced tab get theirs. Its list is a one-column table
// of rows cloned from two templates (a kind's total, a building type under it), rebuilt only when the
// lines change (a first building of a kind's second type, a type gone); otherwise a refresh sets the
// figures that changed. The figures are re-read every kRefreshFrames frames, and only while the tab is the
// one showing.
//
// Two Town Hall windows can be open at once (the toolbar's, and a town hall building's own), so the
// elements are kept per window.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/TableElement.h"
#include "Dialog/TownHallUI.h"
#include "ui/DialogInstance.h"
#include "ui/TabElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/Dialog.h"
#include "ui/String.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"

#include "ext/storage_metrics/storage_metrics.h"
#include "ext/registry.h"
#include "ext/toolbar/toolbar.h"       // OurString: Dialog/ExtensionsStringTable.rsc
#include "ext/ui/find_element.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Ext {
namespace StorageMetrics {

namespace {

constexpr const char* kWindowRsc   = "Dialog/ExtensionsWindow.rsc";
constexpr const char* kButtonRsc   = "Dialog/StorageMetricsTab.rsc:buttonStorageMetricsTab";
constexpr const char* kPageRsc     = "Dialog/StorageMetricsTab.rsc:pageStorageMetrics";
constexpr const char* kTotalRowRsc = "Dialog/StorageMetricsTab.rsc:ribbonStorageMetricsTotalRow";
constexpr const char* kTypeRowRsc  = "Dialog/StorageMetricsTab.rsc:ribbonStorageMetricsTypeRow";
constexpr int kRefreshFrames = 30;   // the game's own Town Hall refresh step (TownHallUI::OnUpdate)

// The names of a kind's total line: keys of Dialog/ExtensionsStringTable.rsc.
const char* const kKindKeys[kKindCount] = {
    "StorageMetricsStockpiles", "StorageMetricsBarns", "StorageMetricsTradingPosts",
};

// One row of the list: its line, as last shown, and the four texts.
struct Row {
    Line line;
    UI::TextElement* name = nullptr;
    UI::TextElement* buildings = nullptr;
    UI::TextElement* space = nullptr;
    UI::TextElement* full = nullptr;
};

// One Town Hall window's tab. The elements are owned by the window's element tree.
struct Tab {
    UI::TabElement* tabs = nullptr;
    UI::TableElement* table = nullptr;
    int32_t index = -1;      // the tab's index in `tabs`
    int frames = 0;          // frames the tab has been showing since the last refresh
    bool shown = false;      // the figures were set at least once
    std::vector<Row> rows;
};

std::unordered_map<const TownHallUI*, Tab> g_tabs;

UI::ElementDescription* LoadDescription(const char* path) {
    if (!::Object::ExternalFactory::_singleton) {
        return nullptr;
    }
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(path));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) {
        std::fprintf(stderr, "[StorageMetrics] %s did not load as an ElementDescription (%p)\n", path,
                     static_cast<const void*>(ext));
        return nullptr;
    }
    // AddTab takes the description as an ExternalPtr (non-const); the factory hands out const objects.
    return const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
}

const UI::Dialog* LoadDialog(const char* path) {
    if (!::Object::ExternalFactory::_singleton) {
        return nullptr;
    }
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(path));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        return nullptr;
    }
    return static_cast<const UI::Dialog*>(ext);
}

// The text of one of our table's strings (empty when the table did not load).
std::string OurText(const char* key) {
    const UI::String s = Toolbar::OurString(key);
    if (!s.IsValid()) {
        return std::string();
    }
    const System::String text = s;
    return text._data != nullptr ? std::string(text._data) : std::string();
}

// What a line is called: a total by its kind; a building type by its title, or by its kind when the
// template has none.
std::string NameOf(const Line& line) {
    if (line.total || line.name.empty()) {
        return OurText(kKindKeys[static_cast<int>(line.kind)]);
    }
    return line.name;
}

// The row texts take their text as the parameter of our table's "@0" string (StorageMetricsTab.rsc).
void SetText(UI::TextElement* e, const std::string& text) {
    if (e != nullptr) {
        e->SetParameter(0, System::String(text.c_str()));
    }
}

void ShowRow(Row& row, const Line& line, bool all) {
    if (all) {
        SetText(row.name, NameOf(line));
    }
    if (all || row.line.space.buildings != line.space.buildings) {
        SetText(row.buildings, Grouped(line.space.buildings));
    }
    if (all || !(row.line.space == line.space)) {
        SetText(row.space, SpaceText(line.space));
        SetText(row.full, FullText(line.space));
    }
    row.line = line;
}

// (Re)creates the rows for `lines`, as TownHallUI::InitInventory fills its inventory table: size the table,
// then SetChild a clone of the row template into each slot.
void BuildRows(Tab& tab, const std::vector<Line>& lines) {
    tab.rows.clear();
    const UI::Dialog* dialog = LoadDialog(kWindowRsc);
    UI::ElementDescription* totalRow = LoadDescription(kTotalRowRsc);
    UI::ElementDescription* typeRow = LoadDescription(kTypeRowRsc);
    if (dialog == nullptr || totalRow == nullptr || typeRow == nullptr) {
        std::fprintf(stderr, "[StorageMetrics] the row templates did not load: empty tab\n");
        return;
    }
    tab.table->SetSize(static_cast<int32_t>(lines.size()), 1);   // clears the old rows
    for (size_t i = 0; i < lines.size(); ++i) {
        Object::InternalPtr<UI::Element> cell;
        (lines[i].total ? totalRow : typeRow)->CreateInstance(&cell, dialog);
        if (cell.Get() == nullptr) {
            std::fprintf(stderr, "[StorageMetrics] row %d was not created\n", static_cast<int>(i));
            break;
        }
        Row row;
        row.name = FindElement<UI::TextElement>(
            cell.Get(), lines[i].total ? "textStorageMetricsTotalName" : "textStorageMetricsTypeName");
        row.buildings = FindElement<UI::TextElement>(cell.Get(), "textStorageMetricsBuildings");
        row.space = FindElement<UI::TextElement>(cell.Get(), "textStorageMetricsSpace");
        row.full = FindElement<UI::TextElement>(cell.Get(), "textStorageMetricsFull");
        ShowRow(row, lines[i], true);
        tab.table->SetChild(&cell, static_cast<int32_t>(i));
        tab.rows.push_back(row);
    }
}

void Refresh(Tab& tab) {
    const std::vector<Line> lines = Lines(Gather());
    bool same = tab.shown && lines.size() == tab.rows.size();
    for (size_t i = 0; same && i < lines.size(); ++i) {
        same = lines[i].SameRow(tab.rows[i].line);
    }
    if (!same) {
        BuildRows(tab, lines);
        tab.shown = true;
        return;
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        ShowRow(tab.rows[i], lines[i], false);
    }
}

// The window's page frame is as wide as its widest page: TabElement::ComputeSize (0x1800d9ea0) leaves the tab
// strip out of the window's width. The game's six buttons fit inside its widest page; a seventh runs past the
// frame's right edge. So the Storage page gets a floor that brings the frame out to the last button. The first
// button starts at the frame's left edge and the buttons are a gap apart; `_tabBarWidth` counts a gap before
// each button and one after the last, so the last button ends at the frame's right edge when the widest page is
// `_tabBarWidth` less two gaps wide. Every tab shows in the one frame, so all of them get the few pixels.
// Nothing in TownHall.rsc is sized to the screen, so measuring before the window is shown reads nothing that is
// not there yet.
void FitTabStrip(UI::TabElement* tabs, UI::Element* page) {
    tabs->ComputeSize();   // AddTab left the layout dirty
    const int32_t strip = tabs->_tabBarWidth - 2 * tabs->_gap;
    if (page->_minWidth < strip) {
        page->_minWidth = strip;
        page->SetNeedsSizeCalc();
    }
}

}  // namespace

void TownHallCreated(const TownHallUI* hall, UI::TabElement* tabs) {
    if (!Ext().storage_metrics || tabs == nullptr || g_tabs.count(hall) != 0) {
        return;
    }
    UI::ElementDescription* button = LoadDescription(kButtonRsc);
    UI::ElementDescription* page = LoadDescription(kPageRsc);
    if (button == nullptr || page == nullptr) {
        return;
    }
    Object::InternalPtr<UI::Element> root = tabs->AddTab(Object::ExternalPtr<UI::ElementDescription>(button),
                                                         Object::ExternalPtr<UI::ElementDescription>(page));
    if (root.Get() == nullptr) {
        std::fprintf(stderr, "[StorageMetrics] AddTab failed: no Storage tab\n");
        return;
    }
    FitTabStrip(tabs, root.Get());
    Tab tab;
    tab.tabs = tabs;
    tab.index = tabs->_tabCount - 1;
    tab.table = FindElement<UI::TableElement>(root.Get(), "tableStorageMetrics");
    if (tab.table == nullptr) {
        std::fprintf(stderr, "[StorageMetrics] no tableStorageMetrics in the page: empty tab\n");
    }
    g_tabs[hall] = tab;
    std::fprintf(stderr, "[StorageMetrics] the Storage tab is tab %d of the Town Hall window\n",
                 static_cast<int>(tab.index));
}

void TownHallUpdate(const TownHallUI* hall) {
    if (g_tabs.empty()) {
        return;
    }
    const auto found = g_tabs.find(hall);
    if (found == g_tabs.end()) {
        return;
    }
    Tab& tab = found->second;
    if (tab.table == nullptr || tab.tabs->GetActiveTab() != tab.index) {
        tab.frames = 0;   // the next time it shows, it refreshes at once
        return;
    }
    if (tab.frames++ % kRefreshFrames != 0) {
        return;
    }
    Refresh(tab);
}

void TownHallClosed(const TownHallUI* hall) {
    if (!g_tabs.empty()) {
        g_tabs.erase(hall);
    }
}

namespace {

// The buildings whose Town Hall window closed on an added tab, by their settings buffer (the building's
// UIComponent data, the one thing a window has of its building that outlives the window). Session only.
std::unordered_set<const void*> g_closedOnAddedTab;

const void* BuildingKey(const TownHallUI* hall) {
    return hall->_dialog != nullptr ? hall->_dialog->_userData : nullptr;
}

}  // namespace

void TownHallSettings(const TownHallUI* hall, bool reading) {
    if (g_tabs.empty() && g_closedOnAddedTab.empty()) {
        return;
    }
    const void* key = BuildingKey(hall);
    if (key == nullptr) {
        return;
    }
    const auto found = g_tabs.find(hall);
    if (!reading) {
        if (found != g_tabs.end() && found->second.tabs->GetActiveTab() >= found->second.index) {
            g_closedOnAddedTab.insert(key);
        } else {
            g_closedOnAddedTab.erase(key);
        }
        return;
    }
    if (g_closedOnAddedTab.count(key) == 0 || found == g_tabs.end()) {
        return;   // not noted, or the window has no added tab now (the row is off): the game's own tab stays
    }
    UI::TabElement* tabs = found->second.tabs;
    const int32_t last = tabs->_tabCount - 1;
    if (last >= found->second.index) {
        tabs->SetActiveTab(last);
    }
}

int32_t SavedTab(const TownHallUI* hall, int32_t tab) {
    if (g_tabs.empty()) {
        return tab;
    }
    const auto found = g_tabs.find(hall);
    if (found == g_tabs.end() || tab < found->second.index) {
        return tab;
    }
    return 0;
}

}  // namespace StorageMetrics
}  // namespace Ext
