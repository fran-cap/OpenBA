// engine/src/ui/consumeproduceui.cpp
// Clean reimplementation of ConsumeProduceUI game logic.
//
// ConsumeProduceUI (0x40 bytes) — production progress panel.
// Extends DialogController. data30 = combo element InternalPtr,
// data38 = progress element InternalPtr.
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Action/exe_bodies.h"   // PickConsumeRecipe (0x140119470)
#include "Object/factory_registration.h"
#include "system/String.h"
#include <cstdint>
#include <cstdio>
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
#include "ui/ProgressElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Component/ConsumeProduceDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Component/RawMaterialDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Component/ConsumeProduceComponent.h"   // B4 DLG-a: by-name reads
#include "Component/ComponentDescription.h"
#include "Dialog/ConsumeProduceUI.h"

// ConsumeProduceUI: PROMOTED from a bare UI_DIALOG_STUB 2026-08-24. The Tavern panel
// rendered its authored layout -- worker spinner, Inventory, "Alcohol Limit 500",
// "Potato 300" and a Product combo -- but the combo was ALWAYS EMPTY, so no product
// could ever be selected and the tavern brewed nothing. The three slot bodies in
// src/game/Dialog/consumeproduceui.cpp were fully ported and wired into NOTHING: the
// stub vtable left 4/7/9 as no-ops, so the combo was never populated (Slot4), the
// progress bar never moved (Slot7) and a selection could never be written back to the
// component (Slot9). Same shape as CemeteryUI directly above.
//
// This is also what kept HappinessComponent's GOODS arm untestable: a tavern with no
// product makes no ale, IsActive's desc+0x25 arm never finds a matching good, and the
// source can never register. Wiring the panel is the prerequisite for that A/B.
// ENDGAME B.3.9: ComponentEntity / UI::ComboElement / UI::ProgressElement /
// ConsumeProduceDescription (was TypeId_ConsumeProduceComponent, bound to 0x1401c7388
// which really is ConsumeProduceDescription's cell -- the guarded object at
// component+0x18 is that description, matching Table 1's authority) / RawMaterialDescription
// all have a ported TypeId() now, so the EnsureTypeIds()/Ready() lazy-cache is dead.

// The data source's ConsumeProduceComponent (retail's checked cast of +0x28,
// then FindComponent<ConsumeProduceComponent> 0x14000b6f0). The generic finder
// the tree called returned null for a null entity (kept).
static ConsumeProduceComponent* CPUI_FindComponent(Object::Base* entity) {
    bool isEntity = ::Object::IsDerivedFromNullSafe(entity, ComponentEntity::TypeId());
    ComponentEntity* entityPtr = isEntity ? static_cast<ComponentEntity*>(entity) : nullptr;
    return entityPtr ? entityPtr->FindComponent<ConsumeProduceComponent>() : nullptr;
}

// The component's ConsumeProduceDescription (Component::_description, checked).
static ConsumeProduceDescription* CPUI_Description(ConsumeProduceComponent* component) {
    Description* desc = component->_description;
    bool isCPComp = ::Object::IsDerivedFromNullSafe(desc, ConsumeProduceDescription::TypeId());
    return isCPComp ? static_cast<ConsumeProduceDescription*>(desc) : nullptr;
}

// ============================================================
// ConsumeProduceUI::OnCreated (vtable +0x20) — Setup/refresh
// ============================================================
// Gets entity's ConsumeProduceComponent, finds "comboProduct" and
// "progressRemaining" UI elements, populates the combo with product
// entries (sprite + name) from the component's Description, and
// sets the combo to the current product selection.
//
// Original: 0x14005F3D0.

// [4] 0x14005f3d0
void ConsumeProduceUI::OnCreated() {
    // Bail on a dead / marked-for-removal datasource before probing it (see Slot7).
    if (!SA_DialogEntityAlive(this)) return;

    // Find the ConsumeProduceComponent of the data source (+0x28)
    ConsumeProduceComponent* component = CPUI_FindComponent(_dataSource);

    // Its description, checked as a ConsumeProduceDescription
    ConsumeProduceDescription* cpComp = CPUI_Description(component);

    // Find "comboProduct" element via DialogInstance
    UI::DialogInstance* dialogInst = _dialog;

    // Check if element exists (first FindElement call to test)
    bool hasCombo;
    {
        System::String nameStr("comboProduct");
        hasCombo = (dialogInst->FindElement(nameStr).Get() != nullptr);
    }
    {
        static int nh = 0;
        if (nh < 6) {
            ++nh;
            fprintf(stderr, "[CPUI] Slot4 hasCombo(comboProduct found)=%d\n", hasCombo ? 1 : 0);
            fflush(stderr);
        }
    }

    if (hasCombo) {
        // Find "comboProduct" again, check ComboElement type
        System::String nameStr("comboProduct");
        UI::Element* foundElem = dialogInst->FindElement(nameStr).Get();

        // IsDerivedFrom ComboElement type check
        bool isCombo = ::Object::IsDerivedFromNullSafe(foundElem, UI::ComboElement::TypeId());
        comboProduct = isCombo ? static_cast<UI::ComboElement*>(foundElem) : nullptr;

        // Set element ID 22000
        comboProduct->SetElementId(22000);

        // Iterate the description's recipes and populate combo
        // (_recipes: 0x28 stride, _recipeCount)
        // [CPUI] the Tavern's Product combo renders EMPTY. Three different things
        // produce that same blank dropdown and they need different fixes:
        //   * hasCombo false      -> the "comboProduct" element was never found
        //   * cpComp null         -> the description is not a ConsumeProduceDescription
        //   * entryCount == 0     -> the recipe array on the description never
        //                           deserialized (see the unregistered-resize trap)
        // Print all three rather than guessing between them.
        {
            static int n = 0;
            if (n < 6) {
                ++n;
                fprintf(stderr, "[CPUI] Slot4 comp=%p desc(comp+0x18)=%p cpComp(afterCast)=%p "
                        "entryCount(desc+0x30)=%d entriesBase(desc+0x28)=%p\n",
                        (void*)component, component->_description, (void*)cpComp,
                        cpComp ? cpComp->_recipeCount : -1,
                        cpComp ? cpComp->_recipes : nullptr);
                fflush(stderr);
            }
        }
        int entryCount = cpComp->_recipeCount;
        // The recipe entries (ConsumeProduceRecipe, 0x28 stride: +0x10 the raw
        // material's ComponentDescription, +0x20 its name String).
        const ConsumeProduceRecipe* entriesBase = cpComp->_recipes;

        for (int i = 0; i < entryCount; i++) {
            // The recipe's raw-material description (entry +0x10)
            ComponentDescription* entryPtr = entriesBase[i].product;

            // Search its sub-descriptions for the RawMaterialDescription
            int subCount = entryPtr->_subDescCount;
            Description** subArray = entryPtr->_subDescs;   // COMP-c typed _subDescs (merge fix)
            RawMaterialDescription* resourceDesc = nullptr;

            for (int j = 0; j < subCount; j++) {
                Object::Base* subEntry = subArray[j];
                // Call GetTypeId virtual (vtable+0x10) and compare
                int typeId = subEntry->Type();
                if (typeId == (int)RawMaterialDescription::TypeId()) {
                    // Verify with IsDerivedFrom
                    if (::Object::IsDerivedFromNullSafe(subEntry, RawMaterialDescription::TypeId())) {
                        resourceDesc = static_cast<RawMaterialDescription*>(subEntry);
                    }
                    break;
                }
            }

            // UI::String from the description's string table (+0x20) and the
            // recipe's name (entry +0x20). From disasm: LEA RCX,[RSI+RCX*8] where
            // RCX = RSI + RSI*4 = i * 5, so address = base + i*0x28 + 0x20.
            const System::String* name = &entriesBase[i].name;
            // COMP-c typed ConsumeProduceDescription +0x20 as UI::StringTable* (merge fix).
            UI::String uiStr(Object::ExternalPtr<UI::StringTable>(cpComp->_stringTable), *name);

            // Convert UI::String to System::String
            System::String sysStr = uiStr;

            // UI::Sprite from the RawMaterialDescription: sheet (+0x20), _nameSmall (+0x38)
            UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(resourceDesc->_spriteSheet),
                              resourceDesc->_nameSmall);

            // Add item to combo: sprite, string, null userData
            comboProduct->AddItem(sprite, sysStr, nullptr);
        }

        // Get current product index and set combo selection
        uint32_t productIdx = static_cast<uint32_t>(component->PickConsumeRecipe());   // 0x140119470
        int selIdx = (int)productIdx;
        if ((int)productIdx < 0) {
            // Invalid — reset to 0
            component->consumeResourceId = 0;
            selIdx = 0;
        }
        comboProduct->SetCurrentItem(selIdx);
    }

    // Find "progressRemaining" element
    bool hasProgress;
    {
        System::String nameStr("progressRemaining");
        hasProgress = (dialogInst->FindElement(nameStr).Get() != nullptr);
    }

    if (hasProgress) {
        System::String nameStr("progressRemaining");
        UI::Element* progElem = dialogInst->FindElement(nameStr).Get();

        // Check ProgressElement type
        bool isProgress = ::Object::IsDerivedFromNullSafe(progElem, UI::ProgressElement::TypeId());
        progressRemaining = isProgress ? static_cast<UI::ProgressElement*>(progElem) : nullptr;

        // Set the progress maximum to 1.0f
        progressRemaining->_maxValue = 1.0f;
    }
}

// ============================================================
// ConsumeProduceUI::OnUpdateLayout (vtable +0x38) — Update progress bar
// ============================================================
// Updates the production progress bar. Reads current/total ticks
// from the entity's components, calculates ratio, and formats
// as a percentage string.
//
// Original: 0x14005F1F0.

// [7] 0x14005f1f0
void ConsumeProduceUI::OnUpdateLayout() {
    // Bail if the datasource entity is gone or marked-for-removal — otherwise the
    // IsDerivedFrom below runs on a dying entity (crash class 2026-07-21).
    if (!SA_DialogEntityAlive(this)) return;

    // The progress element
    UI::ProgressElement* progressElem = progressRemaining;
    if (!progressElem) return;

    // Find the component (FUN_14000b6f0)
    ConsumeProduceComponent* component = CPUI_FindComponent(_dataSource);

    // Its description, checked as a ConsumeProduceDescription
    ConsumeProduceDescription* cpComp = CPUI_Description(component);

    // Calculate production ratio: 1.0 - currentTicks / totalTicks
    int currentTicks = component->consumeState;
    int totalTicks = cpComp->_i48;
    float ratio = 1.0f - (float)currentTicks / (float)totalTicks;

    // Store ratio as the progress value
    progressRemaining->_currentValue = ratio;

    // Format as integer percentage: (int)(ratio * 100.0 + 0.5)
    int percent = (int)(ratio * 100.0f + 0.5f);
    System::String str = System::String::Format("%i", (int)(uint32_t)percent);

    // Set on progress element
    progressRemaining->SetProgressTextParameter(0, str);
}

// ============================================================
// ConsumeProduceUI::OnElementEvent (vtable +0x48) — Combo selection handler
// ============================================================
// When the combo selection changes (param3==0x14, param4==22000),
// reads the selected product index from the UI combo element and
// writes it to the entity's component.
//
// Original: 0x14005F330.

// [9] 0x14005f330
void ConsumeProduceUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId param3, int32_t param4,
                                      int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (param3 != UI::EventId::SelectionChanged || param4 != 22000) return;

    // Find component
    ConsumeProduceComponent* component = CPUI_FindComponent(_dataSource);

    // Read the combo's selection
    uint32_t selection = comboProduct->_currentItem;

    // Write it as the component's selected recipe (+0x50)
    component->consumeResourceId = selection;
}
