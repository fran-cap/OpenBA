// ============================================================================
// src/ext/item_limits/panel_row.cpp -- ext.item_limits' rows on the workshop panel
// ============================================================================
// docs/extensions/handins/ITEMLIMITS.md section 4. A workshop's panel (ConsumeProduceUI, consumeproduceui.cpp)
// gets item limit rows: a check box, "<product> limit" and a spinner (panel_common.h). A row is our own
// description (ItemLimitsPanel.rsc:ribbonItemLimitsRow: the check box and the text); the spinner is a copy of
// the category limit's own spinner ("editLimit", ResourceLimitUI), made from its description, so it looks
// exactly like the category limit's in the retail panel or a mod's. The rows sit in a vertical column
// (ribbonItemLimitsColumn).
//
// With a Product combo there is one row, and it follows the product the combo selects (retail resets a combo
// workshop's random mode to recipe 0, so it always makes the selected product). A column takes the Product
// row's slot (the combo's row, at the panel's foot) and holds that row, then ours, so the panel grows by
// their height.
//
// WITHOUT a combo (a clay or sand pit's single recipe, a mod's butcher that makes meat and tallow in turn) the
// workshop runs every recipe, so the panel gets one row per product its recipes make, under the panel's button
// bar (the strip that holds the category limit): a column takes the bar's slot in its page and holds the bar,
// then the rows. The single row that followed recipe 0 there left the other products unlimitable, and its
// place (the category limit's group, which a pit shares with the remaining-stock bar) overlapped.
//
// A GATHERER (HerbalistComponent: a gatherer's hut, a mod's florist or mushroom shelter) has no
// ConsumeProduceUI either: its panel gets one row per item the plants of its list give (flowers, berries,
// mushrooms ...), placed as a pit's. A list with more items than kMaxRows (a mod's gatherer gathers dozens)
// gets an item picker instead: a combo listing every item by name, above one row that follows the item the
// combo selects.
//
// Under the category limit the rows overlap: that row sits in the tab strip, the page below does not follow
// its height, and a second category row (a sawmill's) has its own fixed place. A panel with no category limit
// spinner gets no row (logged once).
//
// Checked = that item has its own limit, the spinner's value; unchecked = only the category limit applies.
// The limit is the item's, town-wide (one value per item, whichever panel sets it): every panel showing the
// item follows a change at its next layout. Changing the spinner of an unchecked row checks it.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/ComboElement.h"
#include "ui/Sprite.h"
#include "ui/PageElement.h"
#include "ui/SpinnerElement.h"
#include "ui/TextElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/Dialog.h"
#include "ui/String.h"
#include "ui/StringTable.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "system/String.h"

#include "Component/ComponentDescription.h"
#include "Component/ConsumeProduceComponent.h"
#include "Component/ConsumeProduceDescription.h"
#include "Component/HerbalistComponent.h"
#include "Component/HerbalistDescription.h"
#include "Component/RawMaterialDescription.h"
#include "Dialog/ConsumeProduceUI.h"
#include "Dialog/ResourceLimitUI.h"
#include "Entity/ComponentEntity.h"
#include "Action/exe_bodies.h"   // ResourceCategory

#include "ext/item_limits/item_limits.h"
#include "ext/item_limits/panel_common.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Ext {
namespace ItemLimits {

namespace {

// Row i's check box is kFirstId + 2i, its spinner the next id: clear of the panel's own ids (the combo's 22000,
// the category spinner's 0x3039), of the other controllers a workshop panel carries and of the pasture rows'
// 0x7E40.. (pasture_rows.cpp). Row 0 keeps the single row's 0x7E10 / 0x7E11. The picker's combo is the id after
// the last row's.
constexpr int32_t kFirstId = 0x7E10;
constexpr int kMaxRows = 16;
constexpr int32_t kPickId = kFirstId + 2 * kMaxRows;
constexpr const char* kPickRsc = "Dialog/ItemLimitsPanel.rsc:comboItemLimitsItem";

// What a row shows: the item, its name and its stock category (the limit an unchecked row shows).
struct Entry {
    const ComponentDescription* product = nullptr;
    System::String label;
    int32_t category = 0;
};

struct Rows {
    bool followsCombo = false;   // one row, following the product the combo selects
    std::vector<PanelRows::Row> rows;
    UI::ComboElement* picker = nullptr;   // the item picker (more items than rows): one row follows it
    std::vector<Entry> pick;              // the picker's items, in its order
};

// Each open panel with rows. The elements are owned by the panel's element tree, so raw pointers are enough;
// an entry is used only from its own live panel's calls and replaced when a panel is made at the same
// address again.
// Keyed by the controller that built them: a ConsumeProduceUI, or a ResourceLimitUI on a panel without one.
std::unordered_map<const void*, Rows> g_rows;
// The ResourceLimitUI controllers whose first layout has decided whether they build rows.
std::unordered_set<const void*> g_limitDecided;

ConsumeProduceComponent* ComponentOf(Object::Base* source) {
    if (!::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())) return nullptr;
    return static_cast<ComponentEntity*>(source)->FindComponent<ConsumeProduceComponent>();
}

const ConsumeProduceDescription* DescriptionOf(const ConsumeProduceComponent* component) {
    Description* desc = component != nullptr ? component->_description : nullptr;
    if (!::Object::IsDerivedFromNullSafe(desc, ConsumeProduceDescription::TypeId())) return nullptr;
    return static_cast<const ConsumeProduceDescription*>(desc);
}

// The recipe the combo shows: the component's selection (the combo writes it), recipe 0 in the random mode,
// as the panel's own OnCreated shows it.
int ShownRecipe(const ConsumeProduceComponent* component, const ConsumeProduceDescription* desc) {
    const int n = desc->_recipeCount;
    const int sel = component->consumeResourceId;
    return (sel >= 0 && sel < n) ? sel : 0;
}

// Points `row` at recipe `index`: its item's limit (or the category's, unchecked) and a name. A combo's row
// takes the recipe's name, as the combo shows it; a row of a panel without a combo takes the item's own name
// (a pit's recipe often has no name: its panel never shows one), as does a combo row whose recipe name is empty.
Entry RecipeEntry(const ConsumeProduceDescription* desc, int index, bool recipeName) {
    const ConsumeProduceRecipe& recipe = desc->_recipes[index];
    System::String label;
    if (recipeName) {
        label = System::String(UI::String(Object::ExternalPtr<UI::StringTable>(desc->_stringTable), recipe.name));
    }
    const RawMaterialDescription* material =
        recipe.product != nullptr ? recipe.product->FindDescription<RawMaterialDescription>() : nullptr;
    if (label.GetLength() == 0 && material != nullptr) {
        label = System::String(UI::String(Object::ExternalPtr<UI::StringTable>(material->_stringTable),
                                          material->_name));
    }
    Entry e;
    e.product = recipe.product;
    e.label = label;
    e.category = static_cast<int32_t>(recipe.materialType);
    return e;
}

void PointAtEntry(const Entry& e, PanelRows::Row& row) {
    PanelRows::PointAt(row, e.product, e.label, PanelRows::CategoryLimit(e.category));
}

// An item template, or null when `item` is not one (no RawMaterialDescription).
const void* AsItem(const ComponentDescription* item) {
    return item != nullptr && item->FindDescription<RawMaterialDescription>() != nullptr ? item : nullptr;
}

// The recipes the rows show: the combo's one, else the first recipe of each product (a product two recipes
// make from different inputs gets one row: the limit is the item's).
std::vector<int> RecipesToShow(const ConsumeProduceComponent* component, const ConsumeProduceDescription* desc,
                               bool followsCombo) {
    std::vector<int> out;
    if (desc == nullptr || desc->_recipeCount <= 0) return out;
    if (followsCombo) {
        out.push_back(ShownRecipe(component, desc));
        return out;
    }
    std::vector<const void*> products;
    for (int i = 0; i < desc->_recipeCount; ++i) {
        products.push_back(AsItem(desc->_recipes[i].product));
    }
    for (int32_t i : FirstOfEach(products, kMaxRows)) {
        out.push_back(static_cast<int>(i));
    }
    return out;
}

// A workshop panel's entries: RecipesToShow's recipes.
std::vector<Entry> WorkshopEntries(Object::Base* source, bool followsCombo) {
    std::vector<Entry> out;
    const ConsumeProduceComponent* component = ComponentOf(source);
    const ConsumeProduceDescription* desc = DescriptionOf(component);
    for (int index : RecipesToShow(component, desc, followsCombo)) {
        out.push_back(RecipeEntry(desc, index, followsCombo));
    }
    return out;
}

const HerbalistDescription* GathererOf(Object::Base* source) {
    if (!::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())) return nullptr;
    const HerbalistComponent* gatherer = static_cast<ComponentEntity*>(source)->FindComponent<HerbalistComponent>();
    Description* desc = gatherer != nullptr ? gatherer->_description : nullptr;
    if (!::Object::IsDerivedFromNullSafe(desc, HerbalistDescription::TypeId())) return nullptr;
    return static_cast<const HerbalistDescription*>(desc);
}

// A gatherer panel's entries: the item of each plant of its list, once per item (wild and planted sunflowers
// give one), named by the item.
std::vector<Entry> GathererEntries(Object::Base* source) {
    std::vector<Entry> out;
    const HerbalistDescription* desc = GathererOf(source);
    if (desc == nullptr) return out;
    std::vector<const void*> items;
    for (int32_t i = 0; i < desc->_herbCount; ++i) {
        items.push_back(AsItem(HerbItem(desc->_herbs[i])));
    }
    for (int32_t i : FirstOfEach(items, items.size())) {
        const ComponentDescription* item = static_cast<const ComponentDescription*>(items[static_cast<size_t>(i)]);
        const RawMaterialDescription* material = item->FindDescription<RawMaterialDescription>();
        Entry e;
        e.product = item;
        e.label = System::String(UI::String(Object::ExternalPtr<UI::StringTable>(material->_stringTable),
                                            material->_name));
        e.category = ResourceCategory(item);
        out.push_back(e);
    }
    if (static_cast<int>(out.size()) > kMaxRows) {
        // The picker lists them by name.
        std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.label < b.label; });
    }
    return out;
}

// The element of the panel's page that holds the category limit (the button bar) and its parent (the page's
// vertical column): where a panel without a combo puts its rows under. False when the spinner is not on a page.
bool ButtonBarOf(UI::Element* category, UI::Element** holder, UI::Element** target) {
    UI::Element* e = category;
    while (e != nullptr && e->_parent != nullptr) {
        UI::Element* parent = e->_parent;
        if (::Object::IsDerivedFromNullSafe(parent->_parent, UI::PageElement::TypeId())) {
            *holder = parent;
            *target = e;
            return true;
        }
        e = parent;
    }
    return false;
}

// The item picker: a combo of every entry's name, appended to `column`; its row is the first one Build makes.
// False (logged, nothing left over) when the combo did not load.
bool MakePicker(const UI::Dialog* window, const std::vector<Entry>& entries, UI::Element* column, Rows& rows) {
    UI::ElementDescription* desc = PanelRows::Template(kPickRsc);
    Object::InternalPtr<UI::Element> elem;
    if (desc != nullptr) {
        desc->CreateInstance(&elem, window);
    }
    if (!::Object::IsDerivedFromNullSafe(elem.Get(), UI::ComboElement::TypeId())) {
        std::fprintf(stderr, "[ItemLimits] %s did not load: no item picker\n", kPickRsc);
        PanelRows::Discard(elem);
        return false;
    }
    UI::ComboElement* picker = static_cast<UI::ComboElement*>(elem.Get());
    for (const Entry& e : entries) {
        picker->AddItem(UI::Sprite(), e.label, nullptr);
    }
    picker->SetCurrentItem(0);
    picker->SetElementId(kPickId);
    PanelRows::Append(column, &elem);
    rows.picker = picker;
    rows.pick = entries;
    return true;
}

// The panel's rows for `entries`, placed in `dialog`. `combo` is the panel's Product combo (ConsumeProduceUI's),
// null when it has none. False when nothing was built (no category spinner, no entry).
bool Build(UI::DialogInstance* dialog, const std::vector<Entry>& entries, UI::ComboElement* combo, Rows& rows) {
    UI::SpinnerElement* category = PanelRows::CategorySpinner(dialog);
    UI::Element* categoryRow = category != nullptr ? category->_parent : nullptr;
    UI::Element* holder = categoryRow != nullptr ? categoryRow->_parent : nullptr;
    if (holder == nullptr) {
        static int logged = 0;
        if (logged < 4) {
            ++logged;
            std::fprintf(stderr, "[ItemLimits] the workshop panel has no category limit spinner (editLimit %p): "
                         "no item limit row\n", static_cast<void*>(category));
        }
        return false;
    }
    rows.followsCombo = combo != nullptr;
    if (entries.empty()) return false;
    const bool picking = static_cast<int>(entries.size()) > kMaxRows;

    const UI::Dialog* window = PanelRows::WindowDialog();
    Object::InternalPtr<UI::Element> column;
    if (!PanelRows::MakeColumn(window, &column)) {
        std::fprintf(stderr, "[ItemLimits] the rows' column did not load: no item limit row\n");
        return false;
    }
    if (picking && !MakePicker(window, entries, column.Get(), rows)) {
        PanelRows::Discard(column);
        return false;
    }
    for (const Entry& e : entries) {
        if (picking && !rows.rows.empty()) break;   // the picker's one row
        const int32_t checkId = kFirstId + 2 * static_cast<int32_t>(rows.rows.size());
        PanelRows::Row row;
        Object::InternalPtr<UI::Element> rowElem;
        if (!PanelRows::MakeRow(category, window, checkId, checkId + 1, row, &rowElem)) break;
        PanelRows::Append(column.Get(), &rowElem);
        PointAtEntry(e, row);
        rows.rows.push_back(row);
    }
    if (rows.rows.empty()) {
        // No row was made (MakeRow logged why): no empty column, and no picker with nothing to point.
        PanelRows::Discard(column);   // frees the picker with it
        rows.picker = nullptr;
        rows.pick.clear();
        return false;
    }
    // With a combo: under the Product row (the combo's row) at the panel's foot, where nothing is laid out
    // below. Without one: under the button bar.
    UI::Element* placeHolder = nullptr;
    UI::Element* placeTarget = nullptr;
    if (combo != nullptr) {
        placeTarget = combo->_parent;
        placeHolder = placeTarget != nullptr ? placeTarget->_parent : nullptr;
    } else if (!ButtonBarOf(category, &placeHolder, &placeTarget)) {
        placeHolder = nullptr;
    }
    if (placeHolder == nullptr || !PanelRows::AddUnder(placeHolder, placeTarget, window, &column)) {
        std::fprintf(stderr, "[ItemLimits] no Product row or button bar (comboProduct %p), or the column did not "
                     "load: the rows go at the category limit holder's end\n", static_cast<void*>(combo));
        PanelRows::Append(holder, &column);
    }
    return true;
}

void Layout(Object::Base* source, Rows& rows) {
    if (rows.followsCombo && !rows.rows.empty()) {
        const ConsumeProduceComponent* component = ComponentOf(source);
        const ConsumeProduceDescription* desc = DescriptionOf(component);
        if (desc == nullptr || desc->_recipeCount <= 0) return;
        const int shown = ShownRecipe(component, desc);
        if (desc->_recipes[shown].product != rows.rows[0].product) {
            PointAtEntry(RecipeEntry(desc, shown, true), rows.rows[0]);   // the combo picked another product
            return;
        }
    }
    for (PanelRows::Row& row : rows.rows) {
        PanelRows::Follow(row);   // another panel (or a load) may have changed an item's limit
    }
}

bool Event(const void* controller, UI::EventId msg, int32_t id) {
    if (id < kFirstId || id > kPickId) return false;
    const auto it = g_rows.find(controller);
    if (it == g_rows.end()) return false;
    if (id == kPickId) {
        Rows& rows = it->second;
        if (msg == UI::EventId::SelectionChanged && rows.picker != nullptr && !rows.rows.empty()) {
            const int32_t index = rows.picker->GetCurrentItem();
            if (index >= 0 && index < static_cast<int32_t>(rows.pick.size())) {
                PointAtEntry(rows.pick[static_cast<size_t>(index)], rows.rows[0]);   // the picker chose an item
            }
        }
        return true;
    }
    const size_t index = static_cast<size_t>((id - kFirstId) / 2);
    if (index >= it->second.rows.size()) return false;
    PanelRows::Apply(it->second.rows[index], msg, ((id - kFirstId) % 2) == 0);
    return true;
}

}  // namespace

void PanelOpenedSlow(ConsumeProduceUI* panel) {
    g_rows.erase(panel);
    Rows rows;
    const bool followsCombo = panel->comboProduct != nullptr;
    if (Build(panel->_dialog, WorkshopEntries(panel->_dataSource, followsCombo), panel->comboProduct, rows)) {
        g_rows[panel] = rows;
    }
}

void PanelLayoutSlow(ConsumeProduceUI* panel) {
    const auto it = g_rows.find(panel);
    if (it != g_rows.end()) {
        Layout(panel->_dataSource, it->second);
    }
}

bool PanelEventSlow(ConsumeProduceUI* panel, UI::EventId msg, int32_t id) {
    return Event(panel, msg, id);
}

// A workshop panel with no ConsumeProduceUI (a mod's butcher: the work place and the category limit only), and a
// gatherer's panel, get their rows from the category limit's controller. ResourceLimitUI::OnCreated runs before the dialog's later
// controllers are added (UI::DialogInstance::AddController calls each one's OnCreated in turn), so whether a
// ConsumeProduceUI is there is known only at the first layout: the decision is made there, once per controller.
// A panel with a ConsumeProduceUI gets its rows from that one; a panel with two category limits (a sawmill's)
// from the first; a panel of something that is neither a workshop nor a gatherer (the Town Hall's Limits tab)
// none.
void LimitRowCreatedSlow(ResourceLimitUI* limit) {
    g_rows.erase(limit);
    g_limitDecided.erase(limit);
}

void LimitRowLayoutSlow(ResourceLimitUI* limit) {
    if (g_limitDecided.insert(limit).second) {
        UI::DialogInstance* dialog = limit->_dialog;
        const bool first = dialog != nullptr && dialog->GetController(ResourceLimitUI::TypeId()).Get() == limit;
        const bool hasWorkshopUI =
            dialog != nullptr && dialog->GetController(ConsumeProduceUI::TypeId()).Get() != nullptr;
        Rows rows;
        if (first && !hasWorkshopUI) {
            Object::Base* source = limit->_dataSource;
            const std::vector<Entry> entries = ComponentOf(source) != nullptr ? WorkshopEntries(source, false)
                                                                              : GathererEntries(source);
            if (Build(dialog, entries, nullptr, rows)) {
                g_rows[limit] = rows;
            }
        }
        return;
    }
    const auto it = g_rows.find(limit);
    if (it != g_rows.end()) {
        Layout(limit->_dataSource, it->second);
    }
}

bool LimitRowEventSlow(ResourceLimitUI* limit, UI::EventId msg, int32_t id) {
    return Event(limit, msg, id);
}

void PanelsForget() {
    g_rows.clear();
    g_limitDecided.clear();
    PanelRows::ForgetPastureRows();
}

}  // namespace ItemLimits
}  // namespace Ext
