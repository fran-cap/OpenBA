// ============================================================================
// src/ext/building_finder/toolbar_page.cpp -- ext.building_finder's page in the extension window (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/BUILDINGFINDER.md, src/ext/ui/Dialog/BuildingFinderPage.rsc. Every building the
// toolbar can place, narrowed by the search box (every word typed must be in the building's name or its
// group's name). A row's name button starts placing that building (what the toolbar's own button does:
// PlayerToolManager::SelectTool(template)); the small button beside it adds the building to the favorites or
// takes it out. The favorites are shown in their own small window (favorites_window.cpp), opened by the
// page's "Open Favorites Window" button.
//
// The shared parts (the buildings, the favorites file, the rows) are finder_core.cpp.
//
// A press never changes a table from inside its own event: it is parked in g_pending and the per-frame tick
// applies it (as the Family page's rows do). The search box is read in the tick.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/Font.h"
#include "ui/ButtonElement.h"
#include "ui/EditElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/RibbonElement.h"
#include "ui/TableElement.h"
#include "ui/StringTable.h"
#include "ui/String.h"
#include "ui/Dialog.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"

#include "ext/building_finder/building_finder.h"
#include "ext/building_finder/finder_core.h"
#include "ext/building_finder/finder_math.h"
#include "ext/registry.h"
#include "ext/toolbar/toolbar.h"
#include "ext/ui/find_element.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Ext {
namespace BuildingFinder {

namespace {

// Local element ids (the host adds `idBase`).
constexpr int32_t kClearId = 10;               // the button that empties the search box
constexpr int32_t kOpenFavId = 11;             // the button that opens the favorites window
constexpr int32_t kKeyId = 12;                 // the button that sets the hotkey
constexpr int32_t kPickId = 100;               // a row's name button, + its position
constexpr int32_t kStarId = 500;               // its favorite button
constexpr int32_t kMaxResultRows = 300;        // rows built; the rest wait for a longer search
constexpr uint32_t kSearchFill = 0x38ffffffu;
constexpr const char* kWindowRsc = "Dialog/ExtensionsWindow.rsc";

enum class Kind { None, Pick, Star };

std::vector<int> g_resultItems;                  // the item at each row
std::vector<Cell> g_resultCells;
int32_t g_idBase = 0;
std::string g_query;                             // the search as the list was last built
bool g_open = false;
uint32_t g_seenFavVersion = 0;                   // the favorites as the "+" / "-" buttons were last drawn
Kind g_pendingKind = Kind::None;
int g_pendingRow = -1;
bool g_pendingClear = false;
bool g_pendingOpenFav = false;
bool g_pendingKey = false;
int g_placingItem = -1;   // the item whose "Placing ..." the status line shows, else -1
std::string g_keyShown;                         // the hotkey button's label as last drawn
Object::InternalPtr<UI::Element> g_keyText;
Object::InternalPtr<UI::Element> g_page;
Object::InternalPtr<UI::Element> g_search;
Object::InternalPtr<UI::Element> g_count;
Object::InternalPtr<UI::Element> g_status;
Object::InternalPtr<UI::Element> g_resultTable;

UI::TextElement* AsText(UI::Element* e) {
    if (!::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) {
        return nullptr;
    }
    return static_cast<UI::TextElement*>(e);
}

void SetParameterText(UI::Element* e, const std::string& text) {
    if (UI::TextElement* t = AsText(e)) {
        t->SetParameter(0, System::String(text.c_str()));
    }
}

// The status line is one line in a box that cannot grow: a long text would make the window wider.
void Say(const std::string& text) {
    SetParameterText(g_status.Get(), Clip(text, 48));
}

const UI::Dialog* WindowDialog() {
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kWindowRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) return nullptr;
    return static_cast<const UI::Dialog*>(ext);
}

void BuildList() {
    const std::vector<Item>& items = Items();
    g_resultItems.clear();
    int matches = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!Matches(items[i].haystack, g_query)) continue;
        ++matches;
        if (static_cast<int32_t>(g_resultItems.size()) < kMaxResultRows) {
            g_resultItems.push_back(static_cast<int>(i));
        }
    }
    FillSpec spec;
    spec.dialog = WindowDialog();
    spec.pickId0 = g_idBase + kPickId;
    spec.starId0 = g_idBase + kStarId;
    spec.withGroup = true;
    spec.isFavorite = false;
    g_resultCells = FillTable(g_resultTable.Get(), g_resultItems, spec);
    g_seenFavVersion = FavoritesVersion();
    std::string count = std::to_string(matches) + (matches == 1 ? " building" : " buildings");
    if (matches > static_cast<int>(g_resultItems.size())) {
        count = "first " + std::to_string(g_resultItems.size()) + " of " + std::to_string(matches);
    }
    SetParameterText(g_count.Get(), count);
}

std::string SearchText() {
    UI::Element* e = g_search.Get();
    if (!::Object::IsDerivedFromNullSafe(e, UI::EditElement::TypeId())) return std::string();
    const System::String& text = static_cast<UI::EditElement*>(e)->GetText();
    return text._data != nullptr ? std::string(text._data) : std::string();
}

void ClearSearch() {
    UI::Element* e = g_search.Get();
    if (::Object::IsDerivedFromNullSafe(e, UI::EditElement::TypeId())) {
        static_cast<UI::EditElement*>(e)->SetText(System::String(""));
    }
}

// The hotkey button reads "Favorites Window Hotkey: H"; "Press a key..." while it waits for the key. Written
// only when it changed.
void ShowKeyLabel() {
    const std::string label = HotkeyLabel();
    if (label == g_keyShown) return;
    g_keyShown = label;
    SetParameterText(g_keyText.Get(), label);
}

void PageOpen(UI::Element* page, int32_t idBase) {
    g_idBase = idBase;
    g_open = true;
    g_pendingKind = Kind::None;
    g_pendingClear = false;
    g_pendingOpenFav = false;
    g_pendingKey = false;
    g_placingItem = -1;
    g_keyShown.clear();
    TakeHotkeyMessage();   // one from an earlier visit is stale
    AcquireItems();
    g_page = Object::InternalPtr<UI::Element>(page);
    g_query.clear();   // a window that opens now has an empty search box
    g_search = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::EditElement>(page, "editFinderSearch"));
    if (UI::Element* field = g_search.Get()) {
        // the border has no color of its own: a light fill shows where to type
        Object::InternalPtr<UI::Element> slot;
        Object::InternalPtr<UI::Element> border = *field->GetBorder(&slot);
        if (border.Get() != nullptr) border->SetColor(kSearchFill);
    }
    g_count = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(page, "textFinderCount"));
    g_status = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(page, "textFinderStatus"));
    g_resultTable =
        Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TableElement>(page, "tableFinderAll"));
    if (UI::Element* clear = Ext::FindElement<UI::ButtonElement>(page, "buttonFinderClear")) {
        clear->SetElementId(idBase + kClearId);
    }
    if (UI::Element* open = Ext::FindElement<UI::ButtonElement>(page, "buttonFinderOpenFav")) {
        open->SetElementId(idBase + kOpenFavId);
    }
    if (UI::Element* key = Ext::FindElement<UI::ButtonElement>(page, "buttonFinderKey")) {
        key->SetElementId(idBase + kKeyId);
    }
    g_keyText = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(page, "textFinderKey"));
    ShowKeyLabel();
    Say("");
    if (g_resultTable.Get() == nullptr) {
        std::fprintf(stderr, "[BuildingFinder] tableFinderAll not in the page: empty page\n");
        return;
    }
    BuildList();
}

bool PageChanged(UI::Element* /*page*/, int32_t id) {
    if (id == kClearId) {
        g_pendingClear = true;
        return true;
    }
    if (id == kOpenFavId) {
        g_pendingOpenFav = true;
        return true;
    }
    if (id == kKeyId) {
        g_pendingKey = true;
        return true;
    }
    if (id >= kPickId && id < kPickId + kMaxResultRows) {
        g_pendingKind = Kind::Pick;
        g_pendingRow = id - kPickId;
        return true;
    }
    if (id >= kStarId && id < kStarId + kMaxResultRows) {
        g_pendingKind = Kind::Star;
        g_pendingRow = id - kStarId;
        return true;
    }
    return false;
}

// Every frame while the page is showing: apply a press, and build the list again when the search text
// changed.
void PageTick() {
    if (!g_open) return;
    ShowKeyLabel();
    if (g_placingItem >= 0) {   // "Placing X." goes when the placing does (placed, right click, Esc)
        const std::vector<Item>& items = Items();
        if (g_placingItem >= static_cast<int>(items.size()) ||
            !IsPlacing(items[static_cast<size_t>(g_placingItem)])) {
            g_placingItem = -1;
            Say("");
        }
    }
    const std::string keyMessage = TakeHotkeyMessage();
    if (!keyMessage.empty()) Say(keyMessage);
    if (g_pendingKey) {
        g_pendingKey = false;
        BeginHotkeyCapture();
        Say("Press a letter, digit or F-key. Esc cancels.");
        return;
    }
    if (g_pendingClear) {
        g_pendingClear = false;
        ClearSearch();
        return;   // the next frame sees the empty box and builds the list
    }
    if (g_pendingOpenFav) {
        g_pendingOpenFav = false;
        OpenFavoritesWindow(UiOfElement(g_page.Get()));
        return;
    }
    if (g_pendingKind != Kind::None) {
        const Kind kind = g_pendingKind;
        const int row = g_pendingRow;
        g_pendingKind = Kind::None;
        if (row >= 0 && row < static_cast<int>(g_resultItems.size())) {
            const Item& it = Items()[static_cast<size_t>(g_resultItems[static_cast<size_t>(row)])];
            if (kind == Kind::Pick) {
                Say(StartPlacing(it));
                g_placingItem = IsPlacing(it) ? g_resultItems[static_cast<size_t>(row)] : -1;
            } else {
                bool saved = false;
                const bool now = ToggleFavoriteAndSave(it, saved);
                Say(std::string(now ? "Added: " : "Removed: ") + Clip(it.name, 28) +
                    (saved ? "" : " (not saved)"));
                BuildList();
            }
        }
        return;
    }
    const std::string typed = SearchText();
    if (typed != g_query) {   // the search box is outside the table: safe to build the list here
        g_query = typed;
        ScrollToTop(g_resultTable.Get());   // a shorter result under the old scroll position would be blank
        BuildList();
    } else if (FavoritesVersion() != g_seenFavVersion) {
        BuildList();          // the favorites window took one out: the "+" / "-" buttons follow
    }
}

void PageClose() {
    g_open = false;
    g_pendingKind = Kind::None;
    g_pendingClear = false;
    g_pendingOpenFav = false;
    g_pendingKey = false;
    g_keyText = Object::InternalPtr<UI::Element>();
    g_page = Object::InternalPtr<UI::Element>();
    g_search = Object::InternalPtr<UI::Element>();
    g_count = Object::InternalPtr<UI::Element>();
    g_status = Object::InternalPtr<UI::Element>();
    g_resultTable = Object::InternalPtr<UI::Element>();
    g_resultCells.clear();
    g_resultItems.clear();
    ReleaseItems();
}

}  // namespace
}  // namespace BuildingFinder

namespace Toolbar {

const Contribution& BuildingFinderPage() {
    static const Contribution c = { BuildingFinder::kId, BuildingFinder::kPageCaption,
                                    BuildingFinder::kPageRsc, BuildingFinder::PageOpen,
                                    BuildingFinder::PageChanged, BuildingFinder::PageClose,
                                    BuildingFinder::PageTick };
    return c;
}

}  // namespace Toolbar
}  // namespace Ext
