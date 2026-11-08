// engine/src/ui/selectresourceui.cpp
// Clean reimplementation of SelectResourceUI shared methods.
//
// SelectResourceUI is the abstract base for CropUI, OrchardUI, PastureUI.
// Fields: Dialog/SelectResourceUI.h (+0x30 the "selectResource" combo).
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "Dialog/SelectResourceUI.h"
#include "Dialog/CropUI.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/DialogInstance.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/String.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/Sprite.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/SpriteSheet.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K3)
#include "object/External.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ComboElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Component/SelectDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/CitizenBehavior.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Object/exe_type.h"   // DEFINE_EXE_ABSTRACT_TYPE

// ============================================================
// The resource list: the town's template list (CitizenBehavior +0x120 /
// +0x128, GameUpdateLoop +0xF0). A template is listed when its
// SelectDescription's category (+0x40) is the leaf's GetCategory().
// ============================================================

// Helper: iterate the town's templates, find their SelectDescription
// sub-descriptions matching the category filter, and either count them
// or populate a combo box.
//
// When combo is non-null, adds items to the combo and sets selectedIdx
// to the combo index matching the current resource type. When combo is
// null, just counts matching items.
//
// This pattern is shared by PopulateCombo (0x140078830) and
// GetResourceCount (0x140078a40).

static int SelectResourceUI_IterateResources(SelectResourceUI* ui, UI::ComboElement* combo, int* selectedIndexOut) {
    int count = 0;
    int selectedIdx = -1;

    // ENDGAME B.3.9 CG2: SelectDescription::TypeId() (cell 0x1401c48a8) and
    // ComponentEntity::TypeId() (cell 0x1401c5d70): the statics always answer.

    // The town's template list
    const CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;
    int numTypes = town->availableFarmTypes._size;
    ComponentDescription** typeArray = town->availableFarmTypes._data;

    // The current resource type (slot 12, the ExternalPtr retslot)
    ComponentDescription* currentResource = nullptr;
    if (combo) {
        ui->GetResource(&currentResource);                        // slot 12 (+0x60)
    }

    // The category filter (slot 13)
    int categoryFilter = ui->GetCategory();                       // slot 13 (+0x68)

    // The data source as a ComponentEntity (retail builds this InternalPtr and
    // does not use it)
    Object::Base* entity = ui->_dataSource;
    (void)::Object::IsDerivedFromNullSafe(entity, ComponentEntity::TypeId());

    int comboIdx = 0;

    for (int i = 0; i < numTypes; i++) {
        ComponentDescription* resType = typeArray[i];

        // Iterate the template's sub-descriptions
        int subCount = resType->_subDescCount;

        for (int j = 0; j < subCount; j++) {
            Description* subComp = resType->_subDescs[j];

            // Check if the sub-description's type is SelectDescription (slot 2)
            int subTypeId = subComp->Type();

            if (subTypeId == (int)SelectDescription::TypeId()) {
                Description* subObj = resType->_subDescs[j];
                bool isDerived = ::Object::IsDerivedFromNullSafe(subObj, SelectDescription::TypeId());
                if (isDerived && subObj != nullptr) {
                    const SelectDescription* select = static_cast<const SelectDescription*>(subObj);
                    // Check category filter matches
                    int category = (int)select->_group;
                    if (category == categoryFilter) {
                        if (combo) {
                            // The name: a UI::String of the table and key, as System::String
                            UI::String uiStr(Object::ExternalPtr<UI::StringTable>(select->_stringTable), select->_stringName);
                            System::String sysStr(uiStr.operator System::String());

                            // The icon: a Sprite of the sheet and name
                            UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(select->_spriteSheet), select->_spriteName);

                            // Add item to combo with the template's index as userData
                            combo->AddItem(sprite, sysStr, reinterpret_cast<void*>(static_cast<intptr_t>(comboIdx)));  // CAST-OK: ptr_int

                            // Check if this is the currently selected resource
                            if (resType == currentResource) {
                                selectedIdx = count;
                            }
                        }
                        count++;
                    }
                }
                break;  // Found the SelectDescription for this type, move to next
            }
        }
        comboIdx++;
    }

    if (selectedIndexOut) *selectedIndexOut = selectedIdx;
    return count;
}

// ============================================================
// SelectResourceUI::PopulateCombo (0x140078830) — Fill combo
// ============================================================
// Clears the combo, lists the templates of the category with their sprites
// and names, and selects the component's current resource.

void SelectResourceUI::PopulateCombo() {
    // The selectResource combo element
    UI::ComboElement* combo = selectResource;

    // Clear existing items
    combo->RemoveAllItems();

    // Populate combo and get selected index
    int selectedIdx = -1;
    SelectResourceUI_IterateResources(this, combo, &selectedIdx);

    // Set current selection if we found a match
    if (selectedIdx >= 0) {
        selectResource->SetCurrentItem(selectedIdx);
    }
}

// ============================================================
// SelectResourceUI::GetResourceCount (0x140078a40) — Count matching
// ============================================================
// Same iteration as PopulateCombo but just counts matching types.
// Used by OnUpdate to detect when the combo needs refreshing.

int SelectResourceUI::GetResourceCount() {
    return SelectResourceUI_IterateResources(this, nullptr, nullptr);
}

// ============================================================
// SelectResourceUI::OnCreated (vtable +0x20) — Initialize combo element
// ============================================================
// Finds the "selectResource" combo element in the dialog, stores at +0x30,
// sets its element ID to 500, and populates it with resources.
//
// Original: 0x140078be0 (shared by CropUI, OrchardUI, PastureUI).

// [4] 0x140078be0.
void SelectResourceUI::OnCreated() {
    // Find "selectResource" combo element via the owning DialogInstance
    System::String nameBuf("selectResource");

    // Store the combo element (IsDerivedFrom check skipped -- null check only)
    selectResource = static_cast<UI::ComboElement*>(_dialog->FindElement(nameBuf).Get());

    // Set element ID to 500 (combo selection event ID)
    selectResource->SetElementId(500);

    // Populate combo with available resources
    PopulateCombo();
}

// ============================================================
// SelectResourceUI::OnUpdate (vtable +0x40) — Repopulate combo if count changed
// ============================================================
// Checks if the combo element's item count matches the expected resource count.
// If different, repopulates the combo with current resources.
//
// Original: 0x140078D30 (shared by OrchardUI and PastureUI).

// [8] 0x140078d30.
void SelectResourceUI::OnUpdate(const System::Time& /*time*/) {
    int currentCount = selectResource->GetItemCount();
    int expectedCount = GetResourceCount();

    if (currentCount != expectedCount) {
        PopulateCombo();
    }
}

// ============================================================
// SelectResourceUI::OnElementEvent (vtable +0x48) — Combo selection handler
// ============================================================
// On combo selection (msg=0x14, id=500), looks up the selected resource
// type in the town's template list and calls SetResource (slot 11)
// to apply it to the component.
//
// Original: 0x140078D80 (shared by CropUI and OrchardUI).

// [9] 0x140078d80 — the element pointer's address and (event, id) as the body reads them.
void SelectResourceUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId param3, int32_t param4,
                                      int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (param3 != UI::EventId::SelectionChanged || param4 != 500)
        return;

    // Get current selected item index from combo
    int currentItem = selectResource->GetCurrentItem();

    // The item's user data: the template's index in the town's list
    void* itemData = selectResource->GetItemData(currentItem);
    int resourceIndex = (int)reinterpret_cast<intptr_t>(itemData);  // CAST-OK: ptr_int

    // The template
    ComponentDescription* resourceType = GameUpdateLoop::_singleton->m_citizenList->availableFarmTypes._data[resourceIndex];

    // Slot 11 (polymorphic -- sets resource type on component)
    SetResource(resourceType);   // slot 11, virtual (as retail)
}

// ============================================================================
// ENDGAME B.3.8 PNL — SelectResourceUI as a REAL abstract class
// (SelectResourceUI.h). Retail never builds one (create 0x140003730 returns
// 0); its table 0x14015d330 is what every leaf ctor stamps first and every
// leaf dtor restamps last. Every leaf overrides 4 and 7; slot 9 runs for
// CropUI (inherited, as in retail); 8 is overridden by all three in the tree.
// ============================================================================

// [1] TypeName / [2] Type, the record and retail's abstract factory (create =
// the logging stub, in-place = null): ENDGAME B.3.9 RF.
DEFINE_EXE_ABSTRACT_TYPE(SelectResourceUI, "UI::DialogController")




