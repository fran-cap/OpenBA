// ============================================================================
// src/ext/building_finder/finder_core.cpp -- what the page and the favorites window share (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/BUILDINGFINDER.md, finder_core.h. The buildings are the "ShowGroup" items of the
// Toolbar resources the loaded packages bring, walked as ToolbarDialog::OnOpened walks them. A building the
// town cannot build now (its limit is reached) is listed too, with the reason after its name.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/Font.h"
#include "ui/ButtonElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/TableElement.h"
#include "ui/ContainerElement.h"
#include "ui/ScrollElement.h"
#include "ui/ImageElement.h"
#include "ui/Sprite.h"
#include "ui/SpriteSheet.h"
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

#include "Component/ComponentDescription.h"
#include "Component/ToolbarDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/GameUpdateLoop.h"
#include "Game/PlayerToolManager.h"
#include "Game/Toolbar.h"
#include "Dialog/ToolbarDialog.h"
#include "io/settings_store.h"
#include "io/Persistence.h"

#include "ext/building_finder/finder_core.h"
#include "ext/building_finder/finder_math.h"
#include "ext/registry.h"
#include "ext/toolbar/toolbar.h"
#include "ext/ui/find_element.h"
#include "ext/ui/list_width.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ext {
namespace BuildingFinder {

namespace {

constexpr int32_t kStarWidth = 30;             // the favorite button, in pixels
constexpr const char* kCellRsc = "Dialog/BuildingFinderPage.rsc:ribbonFinderCell";
constexpr const char* kIconRsc = "Dialog/FavoritesWindow.rsc:buttonFavoriteIcon";
constexpr const char* kToolbarRsc = "Dialog/Toolbar.rsc";
// ToolbarDialog::OnOpened: action 1 (ShowGroup), the standard button
constexpr int32_t kToolbarButtonTemplate = 2;
constexpr uint32_t kDimColor = 0x80ffffffu;    // a building at its limit: the icon half see-through
constexpr const char* kFavoritesFile = "building_favorites.txt";
constexpr const char* kHotkeySetting = "building_finder_hotkey";

std::vector<Item> g_items;                       // every building, by name
int g_users = 0;                                 // how many hold g_items
std::vector<std::string> g_favorites;            // template paths, in the order they were added
bool g_favoritesLoaded = false;
constexpr int kDefaultHotkey = 'H';
int g_hotkey = kDefaultHotkey;                   // the favorites window's key (a virtual key code)
bool g_hotkeyLoaded = false;
uint32_t g_favVersion = 1;

std::string PathOf(const ::Object::External* ext) {
    if (ext == nullptr) {
        return std::string();
    }
    const System::Filename path = ext->GetPath();
    return path._data != nullptr ? std::string(path._data) : std::string();
}

// A toolbar string: its table's text, or the key itself when the table does not have it.
std::string TextOf(UI::StringTable* table, const System::String& key) {
    const std::string raw = key._data != nullptr ? std::string(key._data) : std::string();
    if (table == nullptr || raw.empty()) {
        return raw;
    }
    const UI::String s(Object::ExternalPtr<UI::StringTable>(table), key);
    if (!s.IsValid()) {
        return raw;
    }
    const System::String text = s;
    return text._data != nullptr ? std::string(text._data) : raw;
}

// The groups above a toolbar item, outermost first ("Housing" for an item in the Housing group).
std::vector<std::string> GroupChain(const ::Toolbar* item) {
    std::vector<std::string> chain;
    int guard = 0;
    for (const ::Toolbar* g = item->parentToolbar; g != nullptr && guard++ < 8; g = g->parentToolbar) {
        const std::string name = TextOf(g->textTable, g->textKey);
        chain.insert(chain.begin(), name);
    }
    return chain;
}

// Every placeable building: the Toolbar resources that are shown, act as "ShowGroup" (action 1) and carry a
// template.
std::vector<Item> CollectItems() {
    std::vector<Item> out;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) {
        return out;
    }
    const uint32_t tid = ::Toolbar::TypeId();
    std::unordered_map<std::string, size_t> seen;   // template path -> its row in `out`
    const ::Object::External* ext = nullptr;
    int guard = 0;
    while ((ext = factory->FindNext(tid, ext)) != nullptr && guard++ < 50000) {
        if (!::Object::IsDerivedFromNullSafe(ext, tid)) {
            continue;
        }
        const ::Toolbar* item = static_cast<const ::Toolbar*>(ext);
        if (item->visible == 0 || item->actionType != 1 || item->componentDescription == nullptr) {
            continue;
        }
        Item it;
        it.tmpl = item->componentDescription;
        it.key = PathOf(it.tmpl);
        if (it.key.empty()) {
            continue;
        }
        // One row per building. The same template under another toolbar group (a mod can list a building in
        // more than one place) adds that group to the row: the search finds it by any of them.
        const auto known = seen.find(it.key);
        if (known != seen.end()) {
            Item& row = out[known->second];
            const std::vector<std::string> more = GroupChain(item);
            for (const std::string& g : more) {
                if (!g.empty() && (" " + row.haystack + " ").find(" " + g + " ") == std::string::npos) {
                    row.haystack += " " + g;
                }
            }
            if (!more.empty() && !more.back().empty() &&
                (", " + row.group + ",").find(", " + more.back() + ",") == std::string::npos) {
                row.group += (row.group.empty() ? "" : ", ") + more.back();
            }
            continue;
        }
        seen[it.key] = out.size();
        // the toolbar button shows the template's own description text when it has one
        // (ToolbarDialog::OnOpened)
        const ToolbarDescription* desc = it.tmpl->FindDescription<ToolbarDescription>();
        if (desc != nullptr) {
            // _str1 is the building's title (BuildRequirementUI's and ToolUI's textTitle); _str3 is the
            // toolbar button's tooltip text, the description, which is what the name falls back to when there
            // is no title
            it.name = TextOf(desc->_stringTable, desc->_str1);
            if (it.name.empty()) {
                it.name = TextOf(desc->_stringTable, desc->_str3);
            }
            it.limit = desc->_val0;
            // the toolbar button's icon and tooltip are the description's, as ToolbarDialog::OnOpened takes
            // them
            it.tip = TextOf(desc->_stringTable, desc->_str3);
            if (desc->_spriteSheet != nullptr && desc->_name._data != nullptr &&
                desc->_name._data[0] != '\0') {
                it.sheet = desc->_spriteSheet;
                it.sprite = desc->_name._data;
            }
        }
        if (it.sheet == nullptr && item->spriteSheet != nullptr && item->spriteName._data != nullptr &&
            item->spriteName._data[0] != '\0') {
            it.sheet = item->spriteSheet;   // the toolbar item's own icon, when the description has none
            it.sprite = item->spriteName._data;
        }
        if (it.name.empty()) {
            it.name = TextOf(item->textTable, item->textKey);
        }
        if (it.name.empty()) {
            it.name = LeafName(it.key);
        }
        const std::vector<std::string> chain = GroupChain(item);
        it.group = chain.empty() ? std::string() : chain.back();
        it.haystack = it.name;
        for (const std::string& g : chain) {
            it.haystack += " " + g;
        }
        out.push_back(it);
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) {
        const std::string la = LowerAscii(a.name);
        const std::string lb = LowerAscii(b.name);
        return la != lb ? la < lb : a.key < b.key;
    });
    return out;
}

// ---- the favorites file ----

bool PathBesideSettings(const char* file, std::string& out) {
    std::string settings;
    if (!IO::SettingsStore::DefaultPath(settings)) {
        return false;
    }
    const size_t slash = settings.find_last_of("/\\");
    out = (slash == std::string::npos ? std::string() : settings.substr(0, slash + 1)) + file;
    return true;
}

bool FavoritesPath(std::string& out) {
    return PathBesideSettings(kFavoritesFile, out);
}

void LoadFavorites() {
    g_favoritesLoaded = true;
    g_favorites.clear();
    std::string path;
    if (!FavoritesPath(path)) {
        return;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    g_favorites = ParseFavorites(buf.str());
}

bool SaveFavorites() {
    std::string path;
    if (!FavoritesPath(path)) {
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "[BuildingFinder] could not write %s\n", path.c_str());
        return false;
    }
    out << FormatFavorites(g_favorites);
    return static_cast<bool>(out);
}

}  // namespace

// The key lives in settings.cfg as `ext.building_finder_hotkey` = the Win32 virtual key code, like the
// extensions' own settings (Persistence::Set, then the settings file is written).
int Hotkey() {
    if (!g_hotkeyLoaded) {
        const ::IO::Persistence* p = ::IO::Persistence::_singleton;
        if (p == nullptr) return g_hotkey;   // settings not read yet: ask again next time
        g_hotkeyLoaded = true;
        int stored = 0;
        if (p->Get("ext", kHotkeySetting, stored) && HotkeyAllowed(stored)) g_hotkey = stored;
    }
    return g_hotkey;
}

bool SetHotkey(int vk) {
    ::IO::Persistence* p = ::IO::Persistence::_singleton;
    if (!HotkeyAllowed(vk) || p == nullptr) return false;
    g_hotkey = vk;
    g_hotkeyLoaded = true;
    p->Set("ext", kHotkeySetting, vk);
    ::Ext::PersistSettings();
    return true;
}

namespace {

UI::ElementDescription* CellTemplate() {
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kCellRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) return nullptr;
    return const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
}

void SetParameterText(UI::Element* e, const std::string& text) {
    if (::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) {
        static_cast<UI::TextElement*>(e)->SetParameter(0, System::String(text.c_str()));
    }
}

}  // namespace

// ---- the buildings ----

void AcquireItems() {
    if (g_users++ == 0) {
        g_items = CollectItems();
        if (g_items.empty()) {
            std::fprintf(stderr, "[BuildingFinder] no building found in the loaded Toolbar resources\n");
        }
    }
}

void ReleaseItems() {
    if (g_users > 0 && --g_users == 0) {
        g_items.clear();
    }
}

const std::vector<Item>& Items() {
    return g_items;
}

// ---- the favorites ----

const std::vector<std::string>& Favorites() {
    if (!g_favoritesLoaded) LoadFavorites();
    return g_favorites;
}

bool ToggleFavoriteAndSave(const Item& it, bool& saved) {
    if (!g_favoritesLoaded) LoadFavorites();
    const bool now = ToggleFavorite(g_favorites, it.key);
    saved = SaveFavorites();
    ++g_favVersion;
    return now;
}

void ClearFavoritesAndSave(bool& saved) {
    if (!g_favoritesLoaded) LoadFavorites();
    g_favorites.clear();
    saved = SaveFavorites();
    ++g_favVersion;
}

uint32_t FavoritesVersion() {
    return g_favVersion;
}

std::vector<int> FavoriteItems(int& missing, int maxRows) {
    std::vector<int> out;
    missing = 0;
    for (const std::string& key : Favorites()) {
        int found = -1;
        for (size_t i = 0; i < g_items.size(); ++i) {
            if (g_items[i].key == key) {
                found = static_cast<int>(i);
                break;
            }
        }
        if (found < 0) {
            ++missing;
        } else if (static_cast<int>(out.size()) < maxRows) {
            out.push_back(found);
        }
    }
    return out;
}

// ---- the town ----

// How many of a template the town has: the toolbar's own count (its test for a disabled button is this count
// against the ToolbarDescription's limit), read from the town's per-template tree.
int BuiltOf(const Item& it) {
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    // no town (no game running): the helper, like retail's UpdateButtons, reads the town unguarded
    if (game == nullptr || game->m_citizenList == nullptr) return 0;
    return Toolbar_BuiltCount(game->m_citizenList, it.tmpl);
}

std::string RowText(const Item& it, bool withGroup, bool compact) {
    if (compact) {
        return Clip(it.name, 30) + (AtLimit(BuiltOf(it), it.limit) ? "  (limit)" : "");
    }
    std::string text = it.name;
    const std::string note = LimitNote(BuiltOf(it), it.limit);
    if (withGroup && !it.group.empty() && it.group != it.name) {
        text += "   [" + Clip(it.group, 70) + "]";   // a building in several groups lists them all
    }
    return text + note;
}

bool IsPlacing(const Item& it) {
    const GameUpdateLoop* game = GameUpdateLoop::_singleton;
    // the manager's active tool group: SelectTool stores the template, and a cancel or a finished placement
    // selects none (null: the edit tool)
    return game != nullptr && game->m_toolManager != nullptr && game->m_toolManager->activeToolGroup == it.tmpl;
}

std::string StartPlacing(const Item& it) {
    const int built = BuiltOf(it);
    if (AtLimit(built, it.limit)) {
        return Clip(it.name, 24) + ": limit reached (" + std::to_string(built) + " of " +
               std::to_string(it.limit) + ")";
    }
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (game == nullptr || game->m_toolManager == nullptr) {
        return "No game is running.";
    }
    game->m_toolManager->SelectTool(it.tmpl);
    return "Placing " + Clip(it.name, 30) + ".";
}

// ---- a list ----

std::vector<Cell> FillTable(UI::Element* tableElement, const std::vector<int>& rows, const FillSpec& spec) {
    std::vector<Cell> cells;
    if (!::Object::IsDerivedFromNullSafe(tableElement, UI::TableElement::TypeId())) return cells;
    UI::TableElement* table = static_cast<UI::TableElement*>(tableElement);
    UI::ElementDescription* tmpl = CellTemplate();
    if (spec.dialog == nullptr || tmpl == nullptr) {
        std::fprintf(stderr, "[BuildingFinder] the dialog / %s did not load: no rows\n", kCellRsc);
        table->SetSize(0, 1);
        return cells;
    }
    const int32_t listWidth = Ext::ListWidth(table);
    table->SetSize(static_cast<int>(rows.size()), 1);   // clears the old cells
    for (size_t k = 0; k < rows.size(); ++k) {
        Object::InternalPtr<UI::Element> cell;
        tmpl->CreateInstance(&cell, spec.dialog);
        if (cell.Get() == nullptr) break;
        table->SetChild(&cell, static_cast<int32_t>(k));
        Cell c;
        c.root = cell;
        c.pick = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::ButtonElement>(cell.Get(),
                "buttonFinderPick"));
        c.pickText = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(cell.Get(),
                "textFinderPickName"));
        c.star = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::ButtonElement>(cell.Get(),
                "buttonFinderStar"));
        c.starText = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(cell.Get(),
                "textFinderStarText"));
        const Item& it = g_items[static_cast<size_t>(rows[k])];
        if (c.pick.Get() != nullptr) {
            c.pick->SetElementId(spec.pickId0 + static_cast<int32_t>(k));
            if (listWidth > kStarWidth + 24) {
                c.pick->_minWidth = listWidth - kStarWidth - 24;
            }
        }
        if (c.star.Get() != nullptr) {
            c.star->SetElementId(spec.starId0 + static_cast<int32_t>(k));
        }
        SetParameterText(c.pickText.Get(), RowText(it, spec.withGroup, spec.compact));
        // "-" removes (the favorites list and a starred row), "+" adds
        SetParameterText(c.starText.Get(), spec.isFavorite || IsFavorite(Favorites(), it.key) ? "-" : "+");
        if (listWidth > 0) {
            cell->_minWidth = listWidth;
        }
        cells.push_back(c);
    }
    return cells;
}

// The picture button's tooltip: the name, the description (when it says more than the name) and the limit
// note.
std::string IconTip(const Item& it) {
    std::string tip = it.name;
    if (!it.tip.empty() && it.tip != it.name) tip += "\n" + it.tip;
    const std::string note = LimitNote(BuiltOf(it), it.limit);
    if (!note.empty()) tip += "\n" + note.substr(note.find_first_not_of(' '));
    return tip;
}

std::vector<Cell> FillIconTable(UI::Element* tableElement, const std::vector<int>& rows,
                                const IconSpec& spec) {
    std::vector<Cell> cells;
    if (!::Object::IsDerivedFromNullSafe(tableElement, UI::TableElement::TypeId())) return cells;
    UI::TableElement* table = static_cast<UI::TableElement*>(tableElement);
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kIconRsc));
    if (spec.dialog == nullptr || !::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) {
        std::fprintf(stderr, "[BuildingFinder] the dialog / %s did not load: no icons\n", kIconRsc);
        table->SetSize(0, 1);
        return cells;
    }
    const UI::ElementDescription* tmpl = static_cast<const UI::ElementDescription*>(ext);
    // The toolbar's own building button (Dialog/Toolbar.rsc, template 2: ToolbarDialog::OnOpened's
    // "ShowGroup" button), so the size, the border and the tooltip dialog are the toolbar's. Ours is the
    // fallback.
    const ::Object::External* toolbarExt =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kToolbarRsc));
    const UI::Dialog* toolbarDialog = ::Object::IsDerivedFromNullSafe(toolbarExt, UI::Dialog::TypeId())
                                          ? static_cast<const UI::Dialog*>(toolbarExt) : nullptr;
    const UI::ElementDescription* retail =
        toolbarDialog != nullptr ? toolbarDialog->GetTemplate(kToolbarButtonTemplate).Get() : nullptr;
    // The cells first, so their size is known before the table is shaped.
    std::vector<Object::InternalPtr<UI::Element>> made;
    int32_t widest = 0;
    for (size_t k = 0; k < rows.size(); ++k) {
        Object::InternalPtr<UI::Element> cell;
        if (retail != nullptr) retail->CreateInstance(&cell, toolbarDialog);
        const bool fromToolbar = ::Object::IsDerivedFromNullSafe(cell.Get(), UI::ButtonElement::TypeId());
        if (!fromToolbar) tmpl->CreateInstance(&cell, spec.dialog);
        if (cell.Get() == nullptr) break;
        const Item& it = g_items[static_cast<size_t>(rows[k])];
        const ToolbarDescription* desc =
            it.tmpl != nullptr ? it.tmpl->FindDescription<ToolbarDescription>() : nullptr;
        cell->SetElementId(spec.pickId0 + static_cast<int32_t>(k));
        if (fromToolbar && desc != nullptr) {
            // what ToolbarDialog::OnOpened gives its button: the template as the user data (the tooltip
            // dialog reads the building from it: icon, title, cost) and the description as the tip
            cell->SetUserData(it.tmpl);
            cell->SetToolTip(
                UI::String(Object::ExternalPtr<UI::StringTable>(desc->_stringTable), desc->_str3));
            cell->SetFlagsOnOff(UI::kElementFlagRaisesEvents, 0);
        } else {
            // Valid tip text first (a tooltip needs one to be shown at all), then the override that is what
            // is shown.
            cell->SetToolTip(Ext::Toolbar::OurString("FinderParam"));
            cell->SetToolTipString(System::String(IconTip(it).c_str()));
        }
        const int count = cell->ChildCount();
        for (int c = 0; c < count; ++c) {
            UI::Element* child = cell->Child(c).Get();
            if (!::Object::IsDerivedFromNullSafe(child, UI::ImageElement::TypeId())) continue;
            if (it.sheet != nullptr) {
                UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(it.sheet),
                                  System::String(it.sprite.c_str()));
                if (sprite.IsValid()) static_cast<UI::ImageElement*>(child)->SetSprite(&sprite);
            }
            if (AtLimit(BuiltOf(it), it.limit)) child->SetColor(kDimColor);
        }
        // The button's own Update paints its picture (a child) with the colour of its state every frame, so
        // the SetColor above is overwritten: a building at its limit halves the alpha of each state's colour
        // instead.
        const bool isButton = ::Object::IsDerivedFromNullSafe(cell.Get(), UI::ButtonElement::TypeId());
        if (AtLimit(BuiltOf(it), it.limit) && isButton) {
            UI::ButtonElement* button = static_cast<UI::ButtonElement*>(cell.Get());
            for (uint32_t& colour : button->_stateColor) {
                colour = (((colour >> 24) / 2) << 24) | (colour & System::kColorRgbMask);
            }
        }
        int32_t w = 0;
        int32_t h = 0;
        cell->GetPaddedSize(w, h);
        widest = std::max(widest, w);
        made.push_back(cell);
        Cell c;
        c.root = cell;
        c.pick = Object::InternalPtr<UI::Element>(cell.Get());
        cells.push_back(c);
    }
    // As many columns as fit the list's width (at least one).
    const int32_t listWidth = Ext::ListWidth(table);
    const int32_t gap = table->_spacing;
    int columns = 4;
    if (listWidth > 0 && widest > 0) {
        columns = std::max(1, static_cast<int>((listWidth + gap) / (widest + gap)));
    }
    const int rowCount = (static_cast<int>(made.size()) + columns - 1) / columns;
    table->SetSize(rowCount, columns);   // clears the old cells
    for (size_t k = 0; k < made.size(); ++k) {
        Object::InternalPtr<UI::Element> cell = made[k];
        table->SetChild(&cell, static_cast<int32_t>(k));
    }
    return cells;
}

// The list's container keeps its vertical bar as child 1 (ContainerElement::Update reads the bar's value as
// the offset); a shorter list under an old offset shows nothing.
void ScrollToTop(UI::Element* table) {
    UI::Element* e = table;
    for (int i = 0; i < 8 && e != nullptr; ++i, e = e->_parent) {
        if (!e->IsA(UI::ContainerElement::TypeId()) || e->_children._size < 2) continue;
        UI::Element* bar = e->_children._data[1];
        if (bar != nullptr && bar->IsA(UI::ScrollElement::TypeId())) {
            static_cast<UI::ScrollElement*>(bar)->_currentValue = 0.0f;
            return;
        }
    }
}

UI::UserInterface* UiOfElement(UI::Element* e) {
    if (e == nullptr) return nullptr;
    UI::Element* root = e;
    for (int i = 0; i < 64 && root->_parent != nullptr; ++i) root = root->_parent;
    return root->_dialogParent != nullptr ? root->_dialogParent->_ui : nullptr;
}

}  // namespace BuildingFinder
}  // namespace Ext
