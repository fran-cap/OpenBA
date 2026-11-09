// ============================================================================
// src/ext/item_limits/panel_row.cpp -- ext.item_limits' row on the workshop panel
// ============================================================================
// docs/extensions/handins/ITEMLIMITS.md section 4. A workshop's panel (ConsumeProduceUI, consumeproduceui.cpp)
// gets one more row under its category limit ("Tools Limit [200]"): a check box, "<product> limit" and a
// spinner. The row is our own description (ItemLimitsPanel.rsc:ribbonItemLimitsRow: the check box and the
// text); the spinner is a copy of the category limit's own spinner ("editLimit", ResourceLimitUI), made from
// its description, so it looks exactly like the category limit's in the retail panel or a mod's. A vertical
// column (ribbonItemLimitsColumn) takes the Product row's slot (the combo's row, at the panel's foot) and holds
// that row, then ours, so ours gets its own line under it and the panel grows by its height. Under the category
// limit it overlapped: that row sits in the tab strip, the page below does not follow its height, and a
// second category row (a sawmill's) has its own fixed place.
// A panel with no category limit spinner gets no row (logged once).
//
// The row follows the product the panel's combo selects. Checked = that item has its own limit, the
// spinner's value; unchecked = only the category limit applies. The limit is the item's, town-wide (one
// value per item, whichever workshop's panel sets it): every panel showing the item follows a change at its
// next layout. Changing the spinner of an unchecked row checks it.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/CheckElement.h"
#include "ui/ComboElement.h"
#include "ui/RibbonElement.h"
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
#include "Component/RawMaterialDescription.h"
#include "Dialog/ConsumeProduceUI.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"

#include "ext/high_limits/high_limits.h"
#include "ext/item_limits/item_limits.h"
#include "ext/toolbar/toolbar.h"   // OurString, SetTableText

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace Ext {
namespace ItemLimits {

namespace {

// Clear of the panel's own ids (the combo's 22000, the category spinner's 0x3039) and of the other
// controllers a workshop panel carries.
constexpr int32_t kCheckId = 0x7E10;
constexpr int32_t kSpinnerId = 0x7E11;
constexpr const char* kWindowRsc = "Dialog/ExtensionsWindow.rsc";
constexpr const char* kRowRsc = "Dialog/ItemLimitsPanel.rsc:ribbonItemLimitsRow";
constexpr const char* kColumnRsc = "Dialog/ItemLimitsPanel.rsc:ribbonItemLimitsColumn";

struct Row {
    UI::CheckElement* check = nullptr;
    UI::Element* text = nullptr;
    UI::SpinnerElement* spinner = nullptr;
    const void* product = nullptr;   // the ComponentDescription the row shows; null until the first Refresh
    std::string path;                // its RawMaterialDescription's template path
};

// Each open panel with a row. The elements are owned by the panel's element tree, so raw pointers are enough;
// an entry is used only from its own live panel's calls and replaced when a panel is made at the same
// address again.
std::unordered_map<const ConsumeProduceUI*, Row> g_rows;

const UI::Dialog* WindowDialog() {
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kWindowRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) return nullptr;
    return static_cast<const UI::Dialog*>(ext);
}

UI::ElementDescription* Template(const char* rsc) {
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(rsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) return nullptr;
    return const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
}

// Frees an element made by CreateInstance that never joined the panel's tree (a parent would own it): its
// destructor frees its own children.
void Discard(Object::InternalPtr<UI::Element>& elem) {
    elem.DeletePtr();
}

// Appends `row` to `holder`. A ribbon goes through AddItem, which grows its extents with its children
// (Element::AddChild does not, and the ribbon's layout reads one extent per child).
void Append(UI::Element* holder, Object::InternalPtr<UI::Element>* row) {
    if (::Object::IsDerivedFromNullSafe(holder, UI::RibbonElement::TypeId())) {
        static_cast<UI::RibbonElement*>(holder)->AddItem(row);
    } else {
        holder->AddChild(row);
    }
}

// Puts `row` on its own line under `productRow` (the Product row): a vertical column takes that row's slot
// in `holder` (same alignment and stretch, so it sits where the row sat) and holds that row, then `row`.
// Adding `row` to `holder` itself would overlay it on the panel's controls when the holder is a group, or
// put it after the holder's other children when it is a ribbon. The holder owns the column and the column
// owns both rows, so the elements are freed as before. False (nothing changed) when the slot is not found.
bool AddUnder(UI::Element* holder, UI::Element* productRow, const UI::Dialog* window,
              Object::InternalPtr<UI::Element>* row) {
    UI::ElementDescription* columnDesc = Template(kColumnRsc);
    if (columnDesc == nullptr) return false;
    UI::Element** slots = holder->_children.GetData();
    const int32_t count = holder->_children.GetSize();
    int32_t slot = -1;
    for (int32_t i = 0; i < count; ++i) {
        if (slots[i] == productRow) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return false;
    Object::InternalPtr<UI::Element> columnElem;
    columnDesc->CreateInstance(&columnElem, window);
    UI::Element* column = columnElem.Get();
    if (!::Object::IsDerivedFromNullSafe(column, UI::RibbonElement::TypeId())) {
        Discard(columnElem);
        return false;
    }
    const uint32_t stretch = UI::kElementFlagAlignArg1 | UI::kElementFlagAlignArg2;
    column->_alignment = productRow->_alignment;
    column->_flags = (column->_flags & ~stretch) | (productRow->_flags & stretch);
    slots[slot] = column;
    column->_parent = holder;
    Object::InternalPtr<UI::Element> productElem(productRow);
    auto* ribbon = static_cast<UI::RibbonElement*>(column);
    ribbon->AddItem(&productElem);
    ribbon->AddItem(row);
    holder->SetNeedsSizeCalc();
    return true;
}

template <class T>
T* FindIn(UI::Element* root, const char* name) {
    if (root == nullptr) return nullptr;
    UI::Element* e = root->FindElement(System::String(name)).Get();
    return ::Object::IsDerivedFromNullSafe(e, T::TypeId()) ? static_cast<T*>(e) : nullptr;
}

ConsumeProduceComponent* ComponentOf(const ConsumeProduceUI* panel) {
    Object::Base* source = panel->_dataSource;
    if (!::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())) return nullptr;
    return static_cast<ComponentEntity*>(source)->FindComponent<ConsumeProduceComponent>();
}

const ConsumeProduceDescription* DescriptionOf(const ConsumeProduceComponent* component) {
    Description* desc = component != nullptr ? component->_description : nullptr;
    if (!::Object::IsDerivedFromNullSafe(desc, ConsumeProduceDescription::TypeId())) return nullptr;
    return static_cast<const ConsumeProduceDescription*>(desc);
}

// The recipe the panel shows: the component's selection (the combo writes it), recipe 0 in the random mode,
// as the panel's own OnCreated shows it.
int ShownRecipe(const ConsumeProduceComponent* component, const ConsumeProduceDescription* desc) {
    const int n = desc->_recipeCount;
    const int sel = component->consumeResourceId;
    return (sel >= 0 && sel < n) ? sel : 0;
}

// The category limit of a recipe (Town Hall Limits): the value an item limit starts at when it is checked.
int32_t CategoryLimit(const ConsumeProduceRecipe& recipe) {
    GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    CitizenBehavior* town = loop != nullptr ? loop->m_citizenList : nullptr;
    if (town == nullptr || town->professionResources._data == nullptr) return 0;
    if (static_cast<int32_t>(recipe.materialType) >= town->professionResources._size) return 0;
    return town->professionResources._data[recipe.materialType].maxCount;
}

void ShowLimit(Row& row, bool has, int32_t value) {
    row.check->_checked = has ? 1 : 0;
    if (!row.spinner->IsActive() && row.spinner->GetValue() != value) {
        row.spinner->SetValue(value);
    }
}

// Points the row at the recipe the panel shows: its name, its item's limit (or the category's, unchecked).
void Refresh(const ConsumeProduceUI* panel, Row& row) {
    const ConsumeProduceComponent* component = ComponentOf(panel);
    const ConsumeProduceDescription* desc = DescriptionOf(component);
    if (desc == nullptr || desc->_recipeCount <= 0) return;
    const ConsumeProduceRecipe& recipe = desc->_recipes[ShownRecipe(component, desc)];
    const ComponentDescription* product = recipe.product;
    const RawMaterialDescription* material =
        product != nullptr ? product->FindDescription<RawMaterialDescription>() : nullptr;
    row.product = product;
    row.path = TemplatePath(material);
    // The recipe's name from the description's string table, as the combo shows it.
    UI::String name(Object::ExternalPtr<UI::StringTable>(desc->_stringTable), recipe.name);
    Toolbar::SetTableText(row.text, "ItemLimitsLabel");
    if (::Object::IsDerivedFromNullSafe(row.text, UI::TextElement::TypeId())) {
        static_cast<UI::TextElement*>(row.text)->SetParameter(0, System::String(name));
    }
    int32_t limit = 0;
    const bool has = !row.path.empty() && LimitOf(row.path, limit);
    ShowLimit(row, has, has ? limit : CategoryLimit(recipe));
    row.text->SetNeedsSizeCalc();
}

}  // namespace

void PanelOpenedSlow(ConsumeProduceUI* panel) {
    g_rows.erase(panel);
    UI::DialogInstance* dialog = panel->_dialog;
    UI::Element* found = dialog != nullptr ? dialog->FindElement(System::String("editLimit")).Get() : nullptr;
    UI::SpinnerElement* category = ::Object::IsDerivedFromNullSafe(found, UI::SpinnerElement::TypeId())
                                       ? static_cast<UI::SpinnerElement*>(found)
                                       : nullptr;
    UI::Element* categoryRow = category != nullptr ? category->_parent : nullptr;
    UI::Element* holder = categoryRow != nullptr ? categoryRow->_parent : nullptr;
    if (holder == nullptr || category->_description == nullptr) {
        static int logged = 0;
        if (logged < 4) {
            ++logged;
            std::fprintf(stderr, "[ItemLimits] the workshop panel has no category limit spinner (editLimit %p): "
                         "no item limit row\n", static_cast<void*>(category));
        }
        return;
    }
    const UI::Dialog* window = WindowDialog();
    UI::ElementDescription* rowDesc = Template(kRowRsc);
    if (window == nullptr || rowDesc == nullptr) {
        std::fprintf(stderr, "[ItemLimits] %s / %s did not load: no item limit row\n", kWindowRsc, kRowRsc);
        return;
    }
    Object::InternalPtr<UI::Element> rowElem;
    rowDesc->CreateInstance(&rowElem, window);
    Object::InternalPtr<UI::Element> spinnerElem;
    category->_description->CreateInstance(&spinnerElem, window);
    UI::Element* rowRoot = rowElem.Get();
    UI::Element* spinnerRoot = spinnerElem.Get();
    if (rowRoot == nullptr || !::Object::IsDerivedFromNullSafe(spinnerRoot, UI::SpinnerElement::TypeId())) {
        Discard(rowElem);
        Discard(spinnerElem);
        return;
    }
    Row row;
    row.check = FindIn<UI::CheckElement>(rowRoot, "checkItemLimitsRow");
    row.text = FindIn<UI::TextElement>(rowRoot, "textItemLimitsRow");
    row.spinner = static_cast<UI::SpinnerElement*>(spinnerRoot);
    if (row.check == nullptr || row.text == nullptr) {
        std::fprintf(stderr, "[ItemLimits] %s lacks checkItemLimitsRow / textItemLimitsRow\n", kRowRsc);
        Discard(rowElem);
        Discard(spinnerElem);
        return;
    }
    row.check->SetElementId(kCheckId);
    row.spinner->SetElementId(kSpinnerId);
    // The range the category spinner has (ResourceLimitUI::OnCreated): ext.high_limits' when it is on.
    if (Ext::HighLimits::Active()) {
        Ext::HighLimits::ApplyToSpinner(row.spinner);
    } else {
        row.spinner->SetMinMax(0, Ext::HighLimits::kRetailMax);
    }
    row.spinner->SetToolTip(Toolbar::OurString("ItemLimitsSpinnerTip"));
    rowRoot->AddChild(&spinnerElem);
    // Under the Product row (the combo's row) at the panel's foot: the item the row limits is the one that
    // combo shows, and nothing is laid out below it. The category limit's row is no place for it: it sits in
    // the tab strip beside the Inventory tab, the page under the strip does not follow its height, and a
    // second category row (the sawmill's) has its own fixed place under the first.
    UI::Element* productRow = panel->comboProduct != nullptr ? panel->comboProduct->_parent : nullptr;
    UI::Element* productHolder = productRow != nullptr ? productRow->_parent : nullptr;
    if (productHolder == nullptr || !AddUnder(productHolder, productRow, window, &rowElem)) {
        std::fprintf(stderr, "[ItemLimits] no Product row (comboProduct %p) or %s did not load: the row goes at "
                     "the category limit holder's end\n", static_cast<void*>(panel->comboProduct), kColumnRsc);
        Append(holder, &rowElem);
    }
    Refresh(panel, row);
    g_rows[panel] = row;
}

void PanelLayoutSlow(ConsumeProduceUI* panel) {
    const auto it = g_rows.find(panel);
    if (it == g_rows.end()) return;
    Row& row = it->second;
    const ConsumeProduceComponent* component = ComponentOf(panel);
    const ConsumeProduceDescription* desc = DescriptionOf(component);
    if (desc == nullptr || desc->_recipeCount <= 0) return;
    if (desc->_recipes[ShownRecipe(component, desc)].product != row.product) {
        Refresh(panel, row);   // the combo picked another product
        return;
    }
    // Another panel (or a load) may have changed this item's limit.
    int32_t limit = 0;
    if (!row.path.empty() && LimitOf(row.path, limit)) {
        ShowLimit(row, true, limit);
    } else if (row.check->_checked != 0) {
        row.check->_checked = 0;
    }
}

bool PanelEventSlow(ConsumeProduceUI* panel, UI::EventId msg, int32_t id) {
    if (id != kCheckId && id != kSpinnerId) return false;
    const auto it = g_rows.find(panel);
    if (it == g_rows.end()) return false;
    Row& row = it->second;
    if (row.path.empty()) return true;
    if (id == kCheckId && msg == UI::EventId::Activated) {
        // CheckElement::Update toggles the box before the event, so it already shows the new state.
        if (row.check->_checked != 0) {
            SetLimit(row.path, row.spinner->GetValue());
        } else {
            ClearLimit(row.path);
        }
        std::fprintf(stderr, "[ItemLimits] %s: %s\n", row.path.c_str(),
                     row.check->_checked != 0 ? "limited" : "no item limit");
    } else if (id == kSpinnerId && msg == UI::EventId::ValueChanged) {
        SetLimit(row.path, row.spinner->GetValue());
        row.check->_checked = 1;
    }
    return true;
}

void PanelsForget() {
    g_rows.clear();
}

}  // namespace ItemLimits
}  // namespace Ext
