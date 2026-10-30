// src/game/Dialog/buildrequirementui.cpp
// Clean reimplementation of BuildRequirementUI::Slot4.
//
// BuildRequirementUI (0x60 bytes) — shows building icon, title, and resource requirements.
// Layout:
//   +0x10 = datasource description (from DialogController)
//   +0x18 = dialog instance (InternalBasePtr, from DialogController)
//   +0x20 = dialog element (InternalBasePtr, from DialogController)
//   +0x28 = entity/data source (InternalBasePtr, from DialogController)
//   +0x30 = resolved description pointer
//   +0x38 = initial value 1 (unused after init)
//   +0x40 = label element array ptr (dynamic array)
//   +0x44 = label count (uint32), +0x48 padding -> actually +0x48 = count, +0x4c = capacity
//   +0x50 = requirement data array ptr (dynamic array)
//   +0x58 = req data count (uint32)
//
// Decompiled from Application-x64.exe at 0x14005E7C0.

#include "Dialog/ui_panels.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Object/factory_registration.h"
#include "system/String.h"
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
#include "ui/ImageElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/LabelElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Component/BuildComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/BuildDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Component/ComponentDescription.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Component/ToolbarDescription.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Component/RawMaterialDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)
#include "Dialog/BuildRequirementUI.h"
#if defined(_DEBUG)
#include <cstdio>
#include <cstdlib>
#endif

#ifdef _DEBUG
// Debug sprite logging wrappers (src/engine/ui/)
#include "ui/sprite_debug.h"   // Debug_Sprite_Ctor
extern void Debug_ImageElement_SetSprite(UI::ImageElement* self, const UI::Sprite* sprite);
extern void Debug_LabelElement_SetSprite(UI::LabelElement* self, const UI::Sprite* sprite);
#endif

// The description find retail inlines three times in 0x14005e7c0 (ToolbarDescription
// cell 0x1401c7f58, BuildDescription 0x1401c7040, RawMaterialDescription 0x1401c7cb8):
// count re-read per pass, slot 2 ([vtbl+0x10]) against T's cell, IsDerivedFrom on the
// first match, no test of the description, the array or an element.
template <class T>
static T* FindDescInDesc(const ComponentDescription* desc) {
    for (int i = 0; i < desc->_subDescCount; i++) {
        if (desc->_subDescs[i]->Type() == T::TypeId()) {
            Description* match = desc->_subDescs[i];
            return match->IsDerivedFrom(T::TypeId()) ? static_cast<T*>(match) : nullptr;   // the member (B7 LX row 993)
        }
    }
    return nullptr;
}

// Helper: find element by name via DialogInstance::FindElement
static UI::Element* FindDialogElem(UI::DialogInstance* dialogInst, const char* name) {
    System::String nameStr(name);
    return dialogInst->FindElement(nameStr).Get();
}

// Helper: find element by name, checked against UI::Element
static UI::Element* FindDialogElemChecked(UI::DialogInstance* dialogInst, const char* name) {
    UI::Element* raw = FindDialogElem(dialogInst, name);
    bool isInternal = ::Object::IsDerivedFromNullSafe(raw, UI::Element::TypeId());
    return isInternal ? raw : nullptr;
}

// ---- BuildRequirementUI: PROMOTED to the real reimpl (2026-06-28) ------------
// This dialog is constructed + its OnCreated (+0x20) dispatched on hover of a
// build-tool button (verified live: valid dlgInst@+0x18 / datasource@+0x28).
// It composes the title (the ToolbarDescription's title key), the icon, and the
// cost labels.

// ============================================================
// BuildRequirementUI::OnCreated (vtable +0x20) — Setup/populate
// ============================================================
// Resolves the building description from the datasource,
// finds ToolbarDescription for icon/title display,
// finds the BuildDescription for the material requirement list.
// Populates label elements with resource sprites and counts.
//
// Original: 0x14005E7C0.

// [4] 0x14005e7c0.
void BuildRequirementUI::OnCreated() {
    // --- Resolve description from datasource (+0x28) ---
    // Three possible datasource types:
    //   1. ComponentDescription — use it directly
    //   2. ComponentEntity — its _description
    //   3. UI::Element — its user data, if a ComponentDescription
    Object::Base* dataSource = _dataSource;

    // NULL-datasource guard (standalone): the Runtime's Object::Base::IsDerivedFrom
    // (0x1800c0390) reads `this->vtable[2]()` with NO null check on `this`, so calling
    // it with a NULL dataSource faults immediately. Retail always binds a
    // description to a build-requirement panel's +0x28; in standalone a house/info
    // dialog can leave +0x28 NULL. Skip all three IsDerivedFrom probes when null — the
    // desc stays unresolved and the function returns cleanly at `if (!desc)` below.
    // Try path 1: datasource IsA ComponentDescription
    bool isBuild = dataSource && ::Object::IsDerivedFromNullSafe(dataSource, ComponentDescription::TypeId());
    if (isBuild && dataSource) {
        // Re-verify (matches original double-check pattern)
        isBuild = ::Object::IsDerivedFromNullSafe(dataSource, ComponentDescription::TypeId());
        description = isBuild ? static_cast<ComponentDescription*>(dataSource) : nullptr;
    }
    // Try path 2: datasource IsA ComponentEntity
    else {
        bool isEntity = dataSource && ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId());
        if (isEntity && dataSource) {
            // Re-verify and take the entity's description
            isEntity = ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId());
            ComponentEntity* entity = isEntity ? static_cast<ComponentEntity*>(dataSource) : nullptr;
            description = entity->_description;
        }
        // Try path 3: datasource IsA UI::Element
        else {
            uint32_t internalTypeId = UI::Element::TypeId();
            bool isInternal = dataSource && ::Object::IsDerivedFromNullSafe(dataSource, internalTypeId);
            UI::Element* elemPtr = isInternal ? static_cast<UI::Element*>(dataSource) : nullptr;

            if (elemPtr) {
                Object::Base* userData = elemPtr->GetUserData();
                // Element_GetUserData can return NULL (element carries no user data);
                // Runtime IsDerivedFrom has no null-this check, so guard exactly as the
                // +0x28 datasource probes above do.
                isBuild = userData && ::Object::IsDerivedFromNullSafe(userData, ComponentDescription::TypeId());
                // The toolbar's sub-item buttons carry the item's ComponentDescription
                // (toolbardialog.cpp SetUserData(button, item+0x58)).
                description = isBuild ? static_cast<ComponentDescription*>(userData) : nullptr;
            }
            // else: +0x30 stays as-is (from previous init or nullptr)
        }
    }

    // --- Process description if resolved ---
    ComponentDescription* desc = description;
    if (!desc) return;

    // Find ToolbarDescription in description's sub-descriptions
    ToolbarDescription* buildReqComp = FindDescInDesc<ToolbarDescription>(desc);

    // The owning dialog instance (+0x18)
    UI::DialogInstance* dialogInst = _dialog;

    // --- Set icon sprite from ToolbarDescription ---
    {
        UI::ImageElement* imagePtr =
            static_cast<UI::ImageElement*>(FindDialogElemChecked(dialogInst, "imageIcon"));

        if (imagePtr) {   // 0x14005e99b; the description is read unguarded (0x14005e9a2)
            // The icon: sprite sheet (+0x20) and sprite name (+0x28)
#ifdef _DEBUG
            uint8_t sprite[16];
            Debug_ImageElement_SetSprite(imagePtr,
                Debug_Sprite_Ctor(sprite, buildReqComp->_spriteSheet, &buildReqComp->_name));
#else
            UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(buildReqComp->_spriteSheet),
                              buildReqComp->_name);
            imagePtr->SetSprite(&sprite);
#endif
        }
    }

    // --- Set title text from ToolbarDescription ---
    {
        UI::TextElement* textPtr =
            static_cast<UI::TextElement*>(FindDialogElemChecked(dialogInst, "textTitle"));

        if (textPtr) {    // 0x14005ea40; the description is read unguarded (0x14005ea47)
            // The title: string table (+0x30) and key (+0x38)
            UI::String uiStr(Object::ExternalPtr<UI::StringTable>(buildReqComp->_stringTable),
                             buildReqComp->_str1);
            textPtr->SetText(&uiStr);
        }
    }

    // --- Find "labelBuildMulti" element, hide initially ---
    UI::Element* labelMulti = FindDialogElem(dialogInst, "labelBuildMulti");
    if (labelMulti) {
        labelMulti->SetFlagsOnOff(1, 0);  // hide
    }

    // --- Build dynamic label array: find "labelBuild0", "labelBuild1", etc. ---
    // Reset BOTH counters. Retail resets neither: it starts the FindElement loop at the
    // current count and appends, because the controller is constructed fresh per
    // dialog and this runs exactly once on it. In standalone the controller is reused
    // across opens, so without a reset the arrays grow every time.
    labels._size = 0;
    counts._size = 0;

    // Find first label element using format "labelBuild%i"
    {
        System::String fmtBuf = System::String::Format("labelBuild%i", (uint64_t)(uint32_t)labels._size);
        UI::Element* labelElem = _dialog->FindElement(fmtBuf).Get();

        while (labelElem) {

            // Checked against UI::Element
            bool isInternal = ::Object::IsDerivedFromNullSafe(labelElem, UI::Element::TypeId());
            UI::Element* wrappedElem = isInternal ? labelElem : nullptr;
            if (!wrappedElem) break;

            // Hide the label initially
            wrappedElem->SetFlagsOnOff(1, 0);

            // Push into the label array at +0x40.
            // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8;
            // the new slot is written right after.
            uint32_t newCount = (uint32_t)labels._size + 1;
            labels.SetSize((int)newCount);
            labels._data[newCount - 1] = wrappedElem;

            // Find next label
            fmtBuf = System::String::Format("labelBuild%i", (uint64_t)newCount);
            labelElem = _dialog->FindElement(fmtBuf).Get();
        }
    }

    // --- Find the build-cost BuildDescription in the desc ---
    // Each entry's sub-entity (the resource-type description) is resolved during
    // BuildDescription_Serialize. No BuildDescription (or a match that fails
    // IsDerivedFrom) returns: retail's `if (!bVar5) return; if (pvVar2 == 0) return;`.
    BuildDescription* resourceComp = FindDescInDesc<BuildDescription>(desc);
    if (!resourceComp) return;

    // Determine item count: min of label count and cost entry count
    int labelTotal = labels._size;
    int entryCount = resourceComp->_costCount;
    int itemCount = (entryCount < labelTotal) ? entryCount : labelTotal;

    if (itemCount <= 0) goto check_multi;

    // --- Populate labels with resource sprites and counts ---
    {
        BuildCost* entryArray = resourceComp->_costs;

        for (int i = 0; i < itemCount; i++) {
            BuildCost& entry = entryArray[i];

            // Push the requirement count into the array at +0x50.
            // B.3.2: Collection::Array<T>::SetSize(n, fill) — retail 0x140003600,
            // stride 4 align 4, no init arm (every new slot takes the fill). SetSize
            // writes the new count itself (the tree once double-counted here).
            counts.SetSize(counts._size + 1, entry.quantity);

            // The sub-entity of this entry (its raw material's description)
            ComponentDescription* subEntity = entry.material;

            // Find RawMaterialDescription in the sub-entity's list
            RawMaterialDescription* resType = FindDescInDesc<RawMaterialDescription>(subEntity);

            // Set sprite on label element (retail reads resType +0x20 / +0x38 unguarded)
            UI::LabelElement* label = static_cast<UI::LabelElement*>(labels._data[i]);
            {
                // The small sprite: sheet (+0x20) and _nameSmall (+0x38)
                #ifdef _DEBUG
                uint8_t sprite[24];
                Debug_LabelElement_SetSprite(label,
                    Debug_Sprite_Ctor(sprite, resType->_spriteSheet, &resType->_nameSmall));
#else
                UI::Sprite sprite(Object::ExternalPtr<UI::SpriteSheet>(resType->_spriteSheet),
                                  resType->_nameSmall);
                label->SetSprite(&sprite);
#endif
            }

            // Format count as "%i" and set as text parameter 0
            System::String countStr = System::String::Format("%i", (uint64_t)(uint32_t)entry.quantity);
            label->SetParameter(0, countStr);

            // Show the label
            label->SetFlagsOnOff(0, 1);
        }
    }

check_multi:
    // If the BuildDescription's multi-build flag (+0x34) is set, show "labelBuildMulti"
    if (resourceComp->_b34 != 0) {
        if (labelMulti) {
            labelMulti->SetFlagsOnOff(0, 1);  // show
        }
    }
}
