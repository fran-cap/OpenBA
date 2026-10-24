// ============================================================================
// src/ext/restricted_foods/toolbar_page.cpp -- ext.restricted_foods's page in the extension window
// ============================================================================
// docs/extensions/handins/RESTRICTEDFOODS.md, src/ext/toolbar/toolbar.h. One cell per edible food of the
// running town with a check box; a checked food is not eaten by citizens, from the moment it is checked
// (live: no Apply). The list is laid out like the Town Hall's Inventory tab (TownHall.rsc:inventoryTable /
// storageItem): a table of small cells, as many columns as fit the list (four in the game's), each a check
// box and a compact label (20 px icon, small font), so a modded game's hundreds of foods are a short scroll.
// The page sizes the table and clones ribbonRestrictedFoodsCell into it, as TownHallUI::InitInventory does.
//
// The page's objects are ours (src/ext/ui/Dialog/RestrictedFoodsPage.rsc); the texts are in
// ExtensionsStringTable.rsc (the RestrictedFoods* keys). It looks up, by name: textRestrictedFoodsHint,
// tableRestrictedFoods, checkRestrictedFoodsShowAll, and in each cell checkRestrictedFoodsRow and
// labelRestrictedFoodsRow. The "Show all foods" check box is off by default (the foods this town has made or
// stored) and is the player's choice for the current town only.
//
// The checked list is the town's own state (class C): it lives in this extension's list (restricted_foods.h),
// which the save tail carries (save_state.cpp). The page reads and writes it through IsPathBlocked and
// SetPathBlocked. Element ids are the host's (toolbar.h): the page gets an `idBase` and answers local ids.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ImageElement.h"
#include "ui/ButtonElement.h"
#include "ui/CheckElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
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

#include "ext/registry.h"
#include "ext/restricted_foods/restricted_foods.h"
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
namespace RestrictedFoods {

namespace {

// Local element ids: the host gives the page `idBase`; the check box is idBase + kShowAllId, the cells idBase
// + kRowId + index.
constexpr int32_t kShowAllId = 2;
constexpr int32_t kRowId = 10;
constexpr int32_t kMaxRows = Toolbar::kPageIdSpan - kRowId;

constexpr const char* kWindowRsc = "Dialog/ExtensionsWindow.rsc";
constexpr const char* kCellRsc   = "Dialog/RestrictedFoodsPage.rsc:ribbonRestrictedFoodsCell";
constexpr int kColumns = 4;   // RestrictedFoodsPage.rsc:tableRestrictedFoods's _width: the columns when the
                              // list's width is not known

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

// One edible material of the running game.
struct Food {
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

// The raw-material descriptions of the town's known-material list (CitizenBehavior +0x110): a template is
// appended by RawMaterialComponent::Startup the first time any entity of it is created and never removed, so
// this is "every item the town has ever made or held", the list the Town Hall inventory shows.
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

// Every loaded RawMaterialDescription with the Edible bit, once per template path, by display name. The
// factory walk is the one the game uses to enumerate what a package loaded (ExternalFactory::FindNext).
std::vector<Food> CollectFoods() {
    std::vector<Food> foods;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) {
        return foods;
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
        if ((m->_flags & kEdible) == 0) {
            continue;
        }
        if (m->_name._data == nullptr || m->_name._data[0] == 0) {
            continue;
        }
        Food f;
        f.mat = m;
        f.path = TemplatePath(prev);
        if (f.path.empty()) {
            continue;
        }
        f.inTown = town.count(m) != 0;
        f.display = PrettyWords(m->_name._data);
        if (m->_stringTable != nullptr &&
            ::Object::IsDerivedFromNullSafe(m->_stringTable, UI::StringTable::TypeId())) {
            const int32_t index = m->_stringTable->GetIndexFromName(m->_name);
            if (index >= 0) {
                const System::String text = m->_stringTable->GetText(index);
                if (text._data != nullptr && text._data[0] != 0) {
                    f.display = text._data;
                }
            }
        }
        foods.push_back(f);
    }
    std::sort(foods.begin(), foods.end(), [](const Food& a, const Food& b) {
        return Toolbar::Lower(a.display.c_str()) < Toolbar::Lower(b.display.c_str());
    });
    return foods;
}

// -----------------------------------------------------------------------------------------------------------
// The page's state: one page at a time (the extension window is one dialog)
// -----------------------------------------------------------------------------------------------------------
struct Row {
    std::string path;                            // the material's template path (the list's key)
    std::string display;                         // the name the game shows
    const void* material = nullptr;              // its RawMaterialDescription (the icon)
    bool        inTown = false;                  // the town has had it (CitizenBehavior's material list)
    bool        pending = false;                 // the check box
    Object::InternalPtr<UI::Element> check;      // CheckElement (null while the row is not shown)
    Object::InternalPtr<UI::Element> border;     // the cell's border (colour)
};

// every edible food of the game; a cell's local id is kRowId + its index
std::vector<Row> g_rows;
int32_t g_idBase = 0;
bool g_showAll = false;                          // the check box; the player's choice for this town
uint32_t g_town = ~0u;                           // the lock epoch g_showAll belongs to
Object::InternalPtr<UI::Element> g_table;        // "tableRestrictedFoods"
Object::InternalPtr<UI::Element> g_showAllCheck; // "checkRestrictedFoodsShowAll"

bool Shown(const Row& r) {
    return g_showAll || r.inTown || r.pending;   // a checked food never vanishes
}

void RefreshShowAll() {
    UI::Element* check = g_showAllCheck.Get();
    if (::Object::IsDerivedFromNullSafe(check, UI::CheckElement::TypeId())) {
        static_cast<UI::CheckElement*>(check)->_checked = g_showAll ? 1 : 0;
    }
}

void RefreshRow(int index) {
    Row& r = g_rows[static_cast<size_t>(index)];
    if (::Object::IsDerivedFromNullSafe(r.check.Get(), UI::CheckElement::TypeId())) {
        static_cast<UI::CheckElement*>(r.check.Get())->_checked = r.pending ? 1 : 0;
    }
    // Red-tinted when blocked, the Mods list's white otherwise.
    if (r.border.Get() != nullptr) {
        r.border->SetColor(r.pending ? 0x20ffa0a0u : 0x10ffffffu);
    }
}

// (Re)creates the cells of the shown foods in the table, as TownHallUI::InitInventory fills its inventory
// table: size the table (rows = foods / columns, rounded up), then SetChild a clone of the cell template into
// each slot. The columns are as many as fit the list's width (Ext::ListWidth), and they fill it. The foods and
// their check state live in g_rows, so a rebuild keeps them.
void BuildRows() {
    if (g_table.Get() == nullptr) {
        return;
    }
    if (!::Object::IsDerivedFromNullSafe(g_table.Get(), UI::TableElement::TypeId())) {
        std::fprintf(stderr, "[RestrictedFoods] tableRestrictedFoods is not a TableElement\n");
        return;
    }
    UI::TableElement* table = static_cast<UI::TableElement*>(g_table.Get());
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
        std::fprintf(stderr, "[RestrictedFoods] %s / %s did not load: no cells\n", kWindowRsc, kCellRsc);
        return;
    }
    UI::ElementDescription* cellTemplate =
        const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(cellExt));

    std::vector<size_t> shownRows;
    for (size_t i = 0; i < g_rows.size(); ++i) {
        Row& r = g_rows[i];
        r.check = Object::InternalPtr<UI::Element>();
        r.border = Object::InternalPtr<UI::Element>();
        if (Shown(r)) {
            shownRows.push_back(i);
        }
    }
    // The cells first, filled, so their widths are known before the table is shaped.
    const int32_t listWidth = Ext::ListWidth(table);
    std::vector<Object::InternalPtr<UI::Element>> cells;
    cells.reserve(shownRows.size());
    int32_t widest = 0;
    for (size_t k = 0; k < shownRows.size(); ++k) {
        const Row& r = g_rows[shownRows[k]];
        Object::InternalPtr<UI::Element> cell;
        cellTemplate->CreateInstance(&cell, dialog);
        if (cell.Get() == nullptr) {
            break;
        }

        // The label: the food's own icon (the small one the game's lists use) and its name.
        UI::LabelElement* labelElem =
            Ext::FindElement<UI::LabelElement>(cell.Get(), "labelRestrictedFoodsRow");
        Toolbar::SetTableText(labelElem, "RestrictedFoodsParam");
        SetParameterText(labelElem, r.display.c_str());
        const RawMaterialDescription* mat = static_cast<const RawMaterialDescription*>(r.material);
        if (const char* spriteName = SpriteName(mat)) {
            if (labelElem != nullptr) {
                UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(mat->_spriteSheet),
                                  System::String(spriteName));
                if (sprite.IsValid()) {
                    labelElem->SetSprite(&sprite);
                }
            }
        }
        int32_t w = 0;
        int32_t h = 0;
        cell->GetPaddedSize(w, h);
        widest = std::max(widest, w);
        cells.push_back(cell);
    }

    // As many columns as the widest cell allows in the list (at least one; the .rsc's four when the list's
    // width is not known), each as wide as the list shares out: the columns and their gaps fill it exactly,
    // the first columns taking the odd pixels. Each cell lays its label across the width it gets
    // (RestrictedFoodsPage.rsc: ExpandWidth).
    int columns = kColumns;
    int32_t columnWidth = 0;
    int32_t oddPixels = 0;
    const int32_t gap = table->_spacing;   // the .rsc's _spacing, between two columns
    if (listWidth > 0 && widest > 0) {
        columns = std::max(1, static_cast<int>((listWidth + gap) / (widest + gap)));
        const int32_t room = listWidth - gap * (columns - 1);
        columnWidth = room / columns;
        oddPixels = room % columns;
    }
    const int rowCount = (static_cast<int>(cells.size()) + columns - 1) / columns;
    table->SetSize(rowCount, columns);                      // clears the old cells

    int made = 0;
    for (size_t k = 0; k < cells.size(); ++k) {
        const size_t i = shownRows[k];
        Row& r = g_rows[i];
        Object::InternalPtr<UI::Element> cell = cells[k];
        if (columnWidth > 0) {
            const int column = static_cast<int>(k % static_cast<size_t>(columns));
            cell->_minWidth = columnWidth + (column < oddPixels ? 1 : 0);
        }
        table->SetChild(&cell, static_cast<int32_t>(k));

        r.check = Object::InternalPtr<UI::Element>(
            Ext::FindElement<UI::CheckElement>(cell.Get(), "checkRestrictedFoodsRow"));
        Object::InternalPtr<UI::Element> border;
        r.border = *cell->GetBorder(&border);
        if (r.check.Get() != nullptr) {
            r.check->SetElementId(g_idBase + kRowId + static_cast<int32_t>(i));
        }
        RefreshRow(static_cast<int>(i));
        ++made;
    }
    std::fprintf(stderr, "[RestrictedFoods] %d of %d food(s) shown in %d row(s) x %d column(s) of %d px "
                 "(list %d px, widest cell %d px)\n", made, static_cast<int>(g_rows.size()), rowCount, columns,
                 static_cast<int>(columnWidth), static_cast<int>(listWidth), static_cast<int>(widest));
}

// One check box was used: fold that one food into the town's list, live. The box then shows
// what the list says.
void ApplyRow(int index) {
    Row& r = g_rows[static_cast<size_t>(index)];
    const bool changed = SetPathBlocked(r.path, r.pending);
    r.pending = IsPathBlocked(r.path);
    RefreshRow(index);
    std::fprintf(stderr, "[RestrictedFoods] %s -> %s, list changed=%d, %d food(s) listed\n",
                 r.path.c_str(), r.pending ? "not eaten" : "eaten", changed ? 1 : 0,
                 static_cast<int>(BlockedPaths().size()));
}

void PageOpen(UI::Element* page, int32_t idBase) {
    g_idBase = idBase;
    // The "Show all foods" choice is per town: a new game, a load or a reload starts it unchecked.
    if (g_town != LockEpoch()) {
        g_town = LockEpoch();
        g_showAll = false;
    }
    g_showAllCheck = Object::InternalPtr<UI::Element>(
        Ext::FindElement<UI::CheckElement>(page, "checkRestrictedFoodsShowAll"));
    if (g_showAllCheck.Get() != nullptr) {
        g_showAllCheck->SetElementId(idBase + kShowAllId);
    }

    g_table = Object::InternalPtr<UI::Element>(
        Ext::FindElement<UI::TableElement>(page, "tableRestrictedFoods"));
    if (g_table.Get() == nullptr) {
        std::fprintf(stderr, "[RestrictedFoods] no tableRestrictedFoods in the page: empty page\n");
        return;
    }

    // Every edible food of the game, with whether the town has had it (the Town Hall inventory's list). The
    // page opens on the foods the town has had; a town that has had none shows all.
    const std::vector<Food> foods = CollectFoods();
    g_rows.clear();
    g_rows.reserve(foods.size());
    bool anyInTown = false;
    for (const Food& food : foods) {
        if (static_cast<int32_t>(g_rows.size()) >= kMaxRows) {
            std::fprintf(stderr, "[RestrictedFoods] more than %d foods: the rest are left out of the page\n",
                         kMaxRows);
            break;
        }
        Row r;
        r.path = food.path;
        r.display = food.display;
        r.material = food.mat;
        r.inTown = food.inTown;
        r.pending = IsPathBlocked(food.path);
        anyInTown = anyInTown || food.inTown;
        g_rows.push_back(r);
    }
    if (!anyInTown) {
        g_showAll = true;
    }
    BuildRows();
    std::fprintf(stderr, "[RestrictedFoods] page opened: %d food(s), %s\n",
                 static_cast<int>(g_rows.size()), g_showAll ? "all foods" : "the town's foods");
}

bool PageChanged(UI::Element* /*page*/, int32_t id) {
    if (id == kShowAllId) {
        // The check box: foods the town has had <-> all foods. The checks are kept; only
        // the rows are rebuilt.
        UI::Element* check = g_showAllCheck.Get();
        if (::Object::IsDerivedFromNullSafe(check, UI::CheckElement::TypeId())) {
            g_showAll = static_cast<UI::CheckElement*>(check)->_checked != 0;
        } else {
            g_showAll = !g_showAll;
        }
        BuildRows();
        return true;
    }
    if (id >= kRowId && id < kRowId + static_cast<int32_t>(g_rows.size())) {
        const int index = id - kRowId;
        Row& r = g_rows[static_cast<size_t>(index)];
        if (::Object::IsDerivedFromNullSafe(r.check.Get(), UI::CheckElement::TypeId())) {
            r.pending = static_cast<UI::CheckElement*>(r.check.Get())->_checked != 0;
        } else {
            r.pending = !r.pending;
        }
        ApplyRow(index);
        return true;
    }
    return false;
}

void PageClose() {
    g_rows.clear();
    g_table = Object::InternalPtr<UI::Element>();
    g_showAllCheck = Object::InternalPtr<UI::Element>();
}

}  // namespace
}  // namespace RestrictedFoods

namespace Toolbar {

const Contribution& RestrictedFoodsPage() {
    static const Contribution c = { RestrictedFoods::kId, RestrictedFoods::kPageCaption,
                                    RestrictedFoods::kPageRsc, RestrictedFoods::PageOpen,
                                    RestrictedFoods::PageChanged, RestrictedFoods::PageClose };
    return c;
}

}  // namespace Toolbar
}  // namespace Ext
