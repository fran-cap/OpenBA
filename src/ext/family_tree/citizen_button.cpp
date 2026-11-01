// ============================================================================
// src/ext/family_tree/citizen_button.cpp -- ext.family_tree's "Family" button on the citizen panel
// ============================================================================
// docs/extensions/handins/FAMILYTREE.md section 4b. The citizen panel (CitizenUI, citizenui.cpp) gets one
// more button beside Follow. It is made from our own description (FamilyTreePage.rsc:buttonFamilyCitizen) and
// added to the element that holds the Follow button, so whatever the panel's layout is (the retail one or a
// mod's), the button lands in the same container. The container must be a plain group (every child laid out
// by its own alignment); anything else is left alone and logged. The button is right-aligned, with a right
// pad that clears the Follow button's width, which is known only after the panel's first layout, so
// OnUpdateLayout keeps the pad right.
//
// A click picks the citizen's person on the Family page (ShowPerson) and opens the tree window on them
// (OpenTreeWindow).
// A citizen the tree does not know (turned on after the citizen was made, before the first sweep) has no
// person: nothing happens.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/ButtonElement.h"
#include "ui/TextElement.h"
#include "ui/GroupElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include "ui/Dialog.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"

#include "Component/CitizenComponent.h"
#include "Dialog/CitizenUI.h"
#include "Entity/ComponentEntity.h"

#include "ext/family_tree/family_tree.h"
#include "ext/toolbar/toolbar.h"

#include <cstdint>
#include <cstdio>
#include <unordered_map>

namespace Ext {
namespace FamilyTree {

namespace {

// after the panel's own 0x226 (home), 0x227 (profession), 0x228 (follow)
constexpr int32_t kButtonId = 0x229;
constexpr int32_t kGap = 4;            // between our button and Follow
constexpr const char* kWindowRsc = "Dialog/ExtensionsWindow.rsc";
constexpr const char* kButtonRsc = "Dialog/FamilyTreePage.rsc:buttonFamilyCitizen";

// Each open panel with a button: the button is owned by the panel's element tree, so a raw pointer is enough,
// and an entry is replaced when a panel is made at the same address again.
std::unordered_map<const CitizenUI*, UI::Element*> g_button;

const UI::Dialog* WindowDialog() {
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kWindowRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) return nullptr;
    return static_cast<const UI::Dialog*>(ext);
}

UI::ElementDescription* ButtonTemplate() {
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kButtonRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) return nullptr;
    return const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
}

// Our word on the first text inside `e` (the Follow button's label, copied).
bool SetLabel(UI::Element* e) {
    if (::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) {
        Toolbar::SetTableText(e, "FamilyCitizenButton");
        return true;
    }
    const int n = e->ChildCount();
    for (int i = 0; i < n; ++i) {
        if (SetLabel(e->Child(i).Get())) return true;
    }
    return false;
}

CitizenComponent* CitizenOf(const CitizenUI* panel) {
    Object::Base* source = panel->_dataSource;
    if (!::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())) return nullptr;
    return static_cast<ComponentEntity*>(source)->FindComponent<CitizenComponent>();
}

}  // namespace

void CitizenPanelOpenedSlow(CitizenUI* panel) {
    g_button.erase(panel);
    UI::Element* follow = panel->buttonFollow;
    UI::Element* parent = follow != nullptr ? follow->_parent : nullptr;
    if (parent == nullptr || !::Object::IsDerivedFromNullSafe(parent, UI::GroupElement::TypeId())) {
        std::fprintf(stderr,
                     "[FamilyTree] the citizen panel's Follow button is not in a group (parent %p): no "
                "Family "
                     "button\n", static_cast<void*>(parent));
        return;
    }
    const UI::Dialog* dialog = WindowDialog();
    // The Follow button's own description makes ours look exactly like it (the retail one, or a mod's); our
    // own Mod.rsc-based description is the fallback when Follow's is not at hand.
    const UI::ElementDescription* tmpl = follow->_description;
    const bool likeFollow = tmpl != nullptr;
    if (!likeFollow) tmpl = ButtonTemplate();
    if (dialog == nullptr || tmpl == nullptr) {
        std::fprintf(stderr, "[FamilyTree] %s / %s did not load: no Family button on the citizen panel\n",
                     kWindowRsc,
                     kButtonRsc);
        return;
    }
    Object::InternalPtr<UI::Element> button;
    tmpl->CreateInstance(&button, dialog);
    if (button.Get() == nullptr) return;
    if (likeFollow) {
        SetLabel(button.Get());
        button->SetToolTip(Toolbar::OurString("FamilyCitizenTip"));
    }
    button->SetElementId(kButtonId);
    parent->AddChild(&button);
    g_button[panel] = button.Get();
}

void CitizenPanelLayoutSlow(CitizenUI* panel) {
    const auto it = g_button.find(panel);
    if (it == g_button.end() || panel->buttonFollow == nullptr) return;
    const UI::Element* follow = panel->buttonFollow;
    const int32_t width = follow->GetLayoutWidth();
    if (width <= 0) return;   // Follow has not been sized yet
    const int32_t right = follow->GetRightPad() + follow->GetLeftPad() + width + kGap;
    UI::Element* button = it->second;
    if (button->GetRightPad() != right || button->GetTopPad() != follow->GetTopPad()) {
        button->SetPadding(0, right, follow->GetTopPad(), 0);
        button->SetNeedsSizeCalc();   // SetPadding does not mark the layout dirty
    }
}

bool CitizenPanelEventSlow(CitizenUI* panel, int32_t id) {
    if (id != kButtonId || g_button.find(panel) == g_button.end()) return false;
    const uint32_t person = PersonOfCitizen(CitizenOf(panel));
    if (person != 0 && ShowPerson(person)) {
        OpenTreeWindow(panel->_dialog != nullptr ? panel->_dialog->_ui : nullptr);
    }
    return true;
}

void CitizenPanelClosedSlow(CitizenUI* panel) {
    g_button.erase(panel);
}

// CitizenPanelClosed is gated on g_on, so a panel that closes after the row went off would leave its entry (a
// raw element pointer) behind: BeginFrame clears them all when the row goes off.
void CitizenButtonsForget() {
    g_button.clear();
}

}  // namespace FamilyTree
}  // namespace Ext
