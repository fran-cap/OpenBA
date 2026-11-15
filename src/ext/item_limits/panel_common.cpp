// ============================================================================
// src/ext/item_limits/panel_common.cpp -- NOT RETAIL. ext.item_limits: the panel rows' shared parts (see
// panel_common.h). docs/extensions/handins/ITEMLIMITS.md section 4.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/CheckElement.h"
#include "ui/RibbonElement.h"
#include "ui/SpinnerElement.h"
#include "ui/TextElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/Dialog.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "system/String.h"

#include "Component/ComponentDescription.h"
#include "Component/RawMaterialDescription.h"
#include "Entity/CitizenBehavior.h"
#include "Game/GameUpdateLoop.h"

#include "ext/high_limits/high_limits.h"
#include "ext/item_limits/item_limits.h"
#include "ext/item_limits/panel_common.h"
#include "ext/toolbar/toolbar.h"   // OurString, SetTableText

#include <cstdio>

namespace Ext {
namespace ItemLimits {
namespace PanelRows {

namespace {

constexpr const char* kWindowRsc = "Dialog/ExtensionsWindow.rsc";
constexpr const char* kRowRsc = "Dialog/ItemLimitsPanel.rsc:ribbonItemLimitsRow";
constexpr const char* kColumnRsc = "Dialog/ItemLimitsPanel.rsc:ribbonItemLimitsColumn";

template <class T>
T* FindIn(UI::Element* root, const char* name) {
    if (root == nullptr) return nullptr;
    UI::Element* e = root->FindElement(System::String(name)).Get();
    return ::Object::IsDerivedFromNullSafe(e, T::TypeId()) ? static_cast<T*>(e) : nullptr;
}

void ShowLimit(Row& row, bool has, int32_t value) {
    row.check->_checked = has ? 1 : 0;
    if (!row.spinner->IsActive() && row.spinner->GetValue() != value) {
        row.spinner->SetValue(value);
    }
}

}  // namespace

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

void Discard(Object::InternalPtr<UI::Element>& elem) {
    elem.DeletePtr();
}

bool MakeColumn(const UI::Dialog* window, Object::InternalPtr<UI::Element>* out) {
    UI::ElementDescription* columnDesc = Template(kColumnRsc);
    if (columnDesc == nullptr) return false;
    columnDesc->CreateInstance(out, window);
    if (!::Object::IsDerivedFromNullSafe(out->Get(), UI::RibbonElement::TypeId())) {
        Discard(*out);
        return false;
    }
    return true;
}

void Append(UI::Element* holder, Object::InternalPtr<UI::Element>* elem) {
    if (::Object::IsDerivedFromNullSafe(holder, UI::RibbonElement::TypeId())) {
        static_cast<UI::RibbonElement*>(holder)->AddItem(elem);
    } else {
        holder->AddChild(elem);
    }
}

// Adding `elem` to `holder` itself would overlay it on the panel's controls when the holder is a group, or
// put it after the holder's other children when it is a ribbon. The holder owns the column and the column
// owns both, so the elements are freed as before.
bool AddUnder(UI::Element* holder, UI::Element* target, const UI::Dialog* window,
              Object::InternalPtr<UI::Element>* elem) {
    UI::Element** slots = holder->_children.GetData();
    const int32_t count = holder->_children.GetSize();
    int32_t slot = -1;
    for (int32_t i = 0; i < count; ++i) {
        if (slots[i] == target) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return false;
    Object::InternalPtr<UI::Element> columnElem;
    if (!MakeColumn(window, &columnElem)) return false;
    UI::Element* column = columnElem.Get();
    const uint32_t stretch = UI::kElementFlagAlignArg1 | UI::kElementFlagAlignArg2;
    column->_alignment = target->_alignment;
    column->_flags = (column->_flags & ~stretch) | (target->_flags & stretch);
    slots[slot] = column;
    column->_parent = holder;
    Object::InternalPtr<UI::Element> targetElem(target);
    auto* ribbon = static_cast<UI::RibbonElement*>(column);
    ribbon->AddItem(&targetElem);
    ribbon->AddItem(elem);
    holder->SetNeedsSizeCalc();
    return true;
}

bool Replace(UI::Element* old, Object::InternalPtr<UI::Element>* fresh) {
    UI::Element* parent = old != nullptr ? old->_parent : nullptr;
    if (parent == nullptr) return false;
    UI::Element** slots = parent->_children.GetData();
    const int32_t count = parent->_children.GetSize();
    for (int32_t i = 0; i < count; ++i) {
        if (slots[i] == old) {
            slots[i] = fresh->Get();
            fresh->Get()->_parent = parent;
            Object::InternalPtr<UI::Element> gone(old);
            Discard(gone);
            parent->SetNeedsSizeCalc();
            return true;
        }
    }
    return false;
}

UI::SpinnerElement* CategorySpinner(UI::DialogInstance* dialog) {
    UI::Element* found = dialog != nullptr ? dialog->FindElement(System::String("editLimit")).Get() : nullptr;
    if (!::Object::IsDerivedFromNullSafe(found, UI::SpinnerElement::TypeId())) return nullptr;
    UI::SpinnerElement* spinner = static_cast<UI::SpinnerElement*>(found);
    return spinner->_description != nullptr ? spinner : nullptr;
}

bool MakeRow(UI::SpinnerElement* category, const UI::Dialog* window, int32_t checkId, int32_t spinnerId, Row& row,
             Object::InternalPtr<UI::Element>* out) {
    UI::ElementDescription* rowDesc = Template(kRowRsc);
    if (window == nullptr || rowDesc == nullptr) {
        std::fprintf(stderr, "[ItemLimits] %s / %s did not load: no item limit row\n", kWindowRsc, kRowRsc);
        return false;
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
        return false;
    }
    row.check = FindIn<UI::CheckElement>(rowRoot, "checkItemLimitsRow");
    row.text = FindIn<UI::TextElement>(rowRoot, "textItemLimitsRow");
    row.spinner = static_cast<UI::SpinnerElement*>(spinnerRoot);
    if (row.check == nullptr || row.text == nullptr) {
        std::fprintf(stderr, "[ItemLimits] %s lacks checkItemLimitsRow / textItemLimitsRow\n", kRowRsc);
        Discard(rowElem);
        Discard(spinnerElem);
        return false;
    }
    row.check->SetElementId(checkId);
    row.spinner->SetElementId(spinnerId);
    // The range the category spinner has (ResourceLimitUI::OnCreated): ext.high_limits' when it is on.
    if (Ext::HighLimits::Active()) {
        Ext::HighLimits::ApplyToSpinner(row.spinner);
    } else {
        row.spinner->SetMinMax(0, Ext::HighLimits::kRetailMax);
    }
    row.spinner->SetToolTip(Toolbar::OurString("ItemLimitsSpinnerTip"));
    rowRoot->AddChild(&spinnerElem);
    *out = rowElem;
    return true;
}

int32_t CategoryLimit(int32_t category) {
    GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    CitizenBehavior* town = loop != nullptr ? loop->m_citizenList : nullptr;
    if (town == nullptr || town->professionResources._data == nullptr) return 0;
    if (category < 0 || category >= town->professionResources._size) return 0;
    return town->professionResources._data[category].maxCount;
}

void PointAt(Row& row, const ComponentDescription* product, const System::String& name, int32_t categoryLimit) {
    const RawMaterialDescription* material =
        product != nullptr ? product->FindDescription<RawMaterialDescription>() : nullptr;
    row.product = product;
    row.path = TemplatePath(material);
    Toolbar::SetTableText(row.text, "ItemLimitsLabel");
    if (::Object::IsDerivedFromNullSafe(row.text, UI::TextElement::TypeId())) {
        static_cast<UI::TextElement*>(row.text)->SetParameter(0, name);
    }
    int32_t limit = 0;
    const bool has = !row.path.empty() && LimitOf(row.path, limit);
    ShowLimit(row, has, has ? limit : categoryLimit);
    row.text->SetNeedsSizeCalc();
}

void Follow(Row& row) {
    int32_t limit = 0;
    if (!row.path.empty() && LimitOf(row.path, limit)) {
        ShowLimit(row, true, limit);
    } else if (row.check->_checked != 0) {
        row.check->_checked = 0;
    }
}

void Apply(Row& row, UI::EventId msg, bool isCheck) {
    if (row.path.empty()) return;
    if (isCheck && msg == UI::EventId::Activated) {
        // CheckElement::Update toggles the box before the event, so it already shows the new state.
        if (row.check->_checked != 0) {
            SetLimit(row.path, row.spinner->GetValue());
        } else {
            ClearLimit(row.path);
        }
        std::fprintf(stderr, "[ItemLimits] %s: %s\n", row.path.c_str(),
                     row.check->_checked != 0 ? "limited" : "no item limit");
    } else if (!isCheck && msg == UI::EventId::ValueChanged) {
        SetLimit(row.path, row.spinner->GetValue());
        row.check->_checked = 1;
    }
}

}  // namespace PanelRows
}  // namespace ItemLimits
}  // namespace Ext
