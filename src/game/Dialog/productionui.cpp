// src/game/Dialog/productionui.cpp
// Clean reimplementation of ProductionUI game logic.
//
// ProductionUI (0x50 bytes) -- production display panel for buildings.
// Extends DialogController. Two dynamic arrays at +0x30 and +0x40 hold
// pairs of {labelStorage, numberCount} element InternalBasePtrs for
// consumed items (array1) and produced items (array2).
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"   // Object::IsDerivedFrom
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/String.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/Sprite.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/SpriteSheet.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/StringTable.h"  // ENDGAME B.3.6 UI callers (K3)
#include "object/External.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/LabelElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/NumberElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/RawMaterialDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/TrackerComponent.h"   // X::TypeId() (B.3.9 CG2)
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Dialog/ProductionUI.h"
#include "Component/ComponentDescription.h"
#include "Game/Calendar.h"

// ============================================================
// ProductionUI layout: Dialog/ProductionUI.h (two Collection::Arrays of
// ProductionUIRow {labelStorage, numberCount} at +0x30 consumed, +0x40 produced).
// ============================================================

// ---- element type ids for retail's two casts --------------------------------
// Retail Slot4 stores `IsDerivedFrom(hit, id) ? hit : nullptr`, reading the ids
// through exe .rdata exrefs 0x140151668 (UI::LabelElement) and 0x1401516c8
// (UI::NumberElement — the engine_funcs label "SpinnerElement" is a known
// misnomer, see standalone/reg_element_typeids.cpp:35).
//
// ENDGAME B.3.9 CG2: TypeId_LabelElement / TypeId_NumberElement -> the ported
// Runtime statics UI::LabelElement::TypeId() / UI::NumberElement::TypeId(). That
// static is a source independent of the exe-side reflection cell (and the
// BANISHED_LIVE_ELEMENT_TIDS gate) this file used to read through the shared
// global via a private by-name fallback; it answers correctly either way, so the
// ResolveTypeIdByNameLazy fallback retires with this conversion.
static uint32_t ProductionUI_LabelElementTypeId() {
    return UI::LabelElement::TypeId();
}
static uint32_t ProductionUI_NumberElementTypeId() {
    return UI::NumberElement::TypeId();
}

static UI::LabelElement* CastLabel(UI::Element* elem) {
    if (!elem) return nullptr;
    return ::Object::IsDerivedFromNullSafe(elem, ProductionUI_LabelElementTypeId())
        ? static_cast<UI::LabelElement*>(elem) : nullptr;
}
static UI::NumberElement* CastNumber(UI::Element* elem) {
    if (!elem) return nullptr;
    return ::Object::IsDerivedFromNullSafe(elem, ProductionUI_NumberElementTypeId())
        ? static_cast<UI::NumberElement*>(elem) : nullptr;
}

// Helper: find a named element under a parent element.
static UI::Element* FindChildElem(UI::Element* parent, const char* name) {
    System::String nameStr(name);
    return parent->FindElement(nameStr).Get();
}

// ---- ProductionUI: PROMOTED to the real reimpl (production-statistics content) -
// ProductionUI drives Dialog/Production.rsc:production4 at pageUser — the page
// PageToggleUI above flips to. Retail bodies:
//   Slot4 (+0x20, Setup)  0x140075da0 — find "production"/"production3"/"production4",
//                         take child 0 (consumed) + child 1 (produced), and cache
//                         each row's {labelStorage, numberCount} pair into the two
//                         dynamic arrays at +0x30 / +0x40.
//   Slot7 (+0x38, Update) 0x140075a20 -> helper 0x140075a50 twice (list 0, list 1) —
//                         reads the entity's TrackerComponent and fills sprite/text/
//                         count per row, hiding the leftovers.
//   Slot3 (+0x18)         0x140076300 decompiles to a bare `return;` — left a no-op.
// The generic stub made both no-ops, so even once the page could be reached it was
// blank.
//
// Slot7 runs every frame the dialog is open, after Slot4 (OnCreated, at the dialog's
// construction) cached the rows; with no rows the helper's loops run zero times. The
// helper reads [tracker+0x48] of the data-source entity's TrackerComponent untested:
// every template whose UIDescription carries Dialog/Production.rsc has one (B9 RES,
// known_port_issues row 1201).

// ============================================================
// ProductionUI::Slot4 (vtable +0x20) -- Setup
// ============================================================
// Finds the "production" container (fallback "production3", "production4"),
// gets two child containers (consumed items, produced items), and for
// each child's sub-children (starting from index 1), finds "labelStorage"
// and "numberCount" elements and caches them in the dynamic arrays.
//
// Original: 0x140075DA0.

// [4] 0x140075da0
void ProductionUI::OnCreated() {
    // The controller's container element (+0x20)
    UI::Element* dialogElem = _container;

    // Find "production" element (with fallbacks)
    UI::Element* productionElem = FindChildElem(dialogElem, "production");

    if (productionElem == nullptr) {
        productionElem = FindChildElem(dialogElem, "production3");

        if (productionElem == nullptr) {
            productionElem = FindChildElem(dialogElem, "production4");
        }
    }

    // Get two child containers: Child(0) = consumed, Child(1) = produced
    UI::Element* consumedContainer = productionElem->Child(0).Get();
    UI::Element* producedContainer = productionElem->Child(1).Get();

    // Iterate consumed container children (start at index 1)
    int childCount = consumedContainer->ChildCount();
    int idx = 1;
    while (idx < childCount) {
        // Get child element at index
        UI::Element* childElem = consumedContainer->Child(idx).Get();

        // Retail casts the "labelStorage" hit through IsDerivedFrom(LabelElement) and
        // stores NULL when it isn't one (0x140075da0 uses TypeId_LabelElement_exref
        // @0x140151668); "numberCount" through IsDerivedFrom(NumberElement) (exref
        // 0x1401516c8, engine_funcs label "SpinnerElement" — the id is NumberElement's).
        ProductionUIRow row;
        row.labelStorage = CastLabel(FindChildElem(childElem, "labelStorage"));
        row.numberCount = CastNumber(FindChildElem(childElem, "numberCount"));

        // Push pair to the consumed list (+0x30)
        lists[0].SetSize(lists[0].GetSize() + 1, row);

        idx++;
        childCount = consumedContainer->ChildCount();
    }

    // Iterate produced container children (start at index 1)
    childCount = producedContainer->ChildCount();
    idx = 1;
    while (idx < childCount) {
        UI::Element* childElem = producedContainer->Child(idx).Get();

        ProductionUIRow row;
        row.labelStorage = CastLabel(FindChildElem(childElem, "labelStorage"));
        row.numberCount = CastNumber(FindChildElem(childElem, "numberCount"));

        // Push pair to the produced list (+0x40)
        lists[1].SetSize(lists[1].GetSize() + 1, row);

        idx++;
        childCount = producedContainer->ChildCount();
    }
}

// ============================================================
// ProductionUI::UpdateList — update one production list
// ============================================================
// Updates the UI for either consumed (listIndex=0) or produced (listIndex=1)
// items by reading from the TrackerComponent and setting sprite/text/count
// on cached label+number elements.
//
// Original helper: 0x140075A50.

void ProductionUI::UpdateList(int listIndex) {
    // Get entity from the controller's data source (+0x28): 0x140075a71-0x140075a96,
    // IsDerivedFrom on [RCX+0x28] with no null test, CMOVNZ.
    Object::Base* entity = _dataSource;
    bool isEntity = entity->IsDerivedFrom(ComponentEntity::TypeId());
    ComponentEntity* entityPtr = isEntity ? static_cast<ComponentEntity*>(entity) : nullptr;

    // Find the TrackerComponent (FindComponent<TrackerComponent> 0x140013320), called
    // at 0x140075ab2 on the cast result with no test. B8 RES: the null-entity test ENT
    // kept here is gone -- the controller's data source is bound before it can tick:
    // DialogInstance's ctor runs AddController(type, _dataSource = the clicked entity,
    // edittool_select.cpp) for every controller before UserInterface::AddDialog opens
    // and queues the dialog, so no slot of this controller runs with it unbound.
    // The result is read ([tracker+0x48], 0x140075ab2..) with no test: the dialog is
    // built from the entity's OWN UIDescription, and every template carrying
    // Production.rsc carries a TrackerComponent (row 1201).
    TrackerComponent* tracker = entityPtr->FindComponent<TrackerComponent>();

    // Check if game world item count changed; rebuild tracker if so
    int gameItemCount = GameUpdateLoop::_singleton->m_dayCounter->year;
    if (gameItemCount != tracker->trackerId) {
        // Call tracker rebuild: FUN_14000d9d0 moves data from +0x38 to +0x28
        fn_TrackerComponent_Rebuild(&tracker->produced, &tracker->displayed);
        System::MemoryAllocator::MemFree(tracker->displayed._data);
        tracker->displayed._data = nullptr;
        tracker->displayed._size = 0;      // retail: one qword store over count / capacity
        tracker->displayed._capacity = 0;
        tracker->trackerId = gameItemCount;
    }

    // Get item count for this list
    int itemCount;
    if (listIndex == 0) {
        itemCount = tracker->displayed._size;
    } else {
        itemCount = tracker->produced._size;
    }

    // Get element array for this list (+0x30 consumed, +0x40 produced)
    ProductionUIRow* elemArray = lists[listIndex]._data;
    int elemCount = lists[listIndex]._size;


    int i = 0;
    while (i < itemCount) {
        // Stop if we've used all cached UI elements
        if (i == elemCount) break;

        ProductionUIRow& elemPair = elemArray[i];

        // Get item array from tracker
        TrackerComponent::Entry* itemArray;
        if (listIndex == 0) {
            itemArray = tracker->displayed._data;
        } else {
            itemArray = tracker->produced._data;
        }

        ComponentDescription* item = itemArray[i]._resource;

        // Search sub-entries for ResourceType description
        int subCount = item->_subDescCount;
        Description** subArray = item->_subDescs;
        RawMaterialDescription* resourceDesc = nullptr;

        for (int j = 0; j < subCount; j++) {
            Description* subEntry = subArray[j];
            // Call GetTypeId virtual (vtable+0x10)
            int typeId = subEntry->Type();
            if (typeId == (int)RawMaterialDescription::TypeId()) {
                if (::Object::IsDerivedFromNullSafe(subEntry, RawMaterialDescription::TypeId())) {
                    resourceDesc = static_cast<RawMaterialDescription*>(subEntry);
                }
                break;
            }
        }

        // Get count from item entry (+0x08 relative to item array base)
        if (listIndex == 0) {
            itemArray = tracker->displayed._data;
        } else {
            itemArray = tracker->produced._data;
        }
        int count = itemArray[i]._count;
        // Create sprite from the RawMaterialDescription
        UI::Sprite sprite(resourceDesc->_spriteSheet, resourceDesc->_name);

        // Set sprite on label element
        UI::LabelElement* labelElem = elemPair.labelStorage;
        labelElem->SetSprite(&sprite);

        // Create UI::String from the RawMaterialDescription
        UI::String uiStr(resourceDesc->_stringTable, resourceDesc->_name);

        // Set text on label element
        labelElem->SetText(&uiStr);

        // Set count on number element
        UI::NumberElement* numberElem = elemPair.numberCount;
        numberElem->SetValue(static_cast<int32_t>(count));

        // Show both elements
        labelElem->SetFlagsOnOff(0, UI::kElementFlagHidden);
        numberElem->SetFlagsOnOff(0, UI::kElementFlagHidden);

        i++;
    }

    // Hide unused elements
    while (i < elemCount) {
        ProductionUIRow& elemPair = elemArray[i];

        // Set empty sprite on label element
        UI::Sprite emptySprite;
        UI::LabelElement* labelElem = elemPair.labelStorage;
        labelElem->SetSprite(&emptySprite);

        // Set empty text on label element
        UI::String emptyStr;
        labelElem->SetText(&emptyStr);

        // Hide number element
        UI::NumberElement* numberElem = elemPair.numberCount;
        numberElem->SetFlagsOnOff(UI::kElementFlagHidden, 0);

        i++;
    }
}

// ============================================================
// ProductionUI::Slot7 (vtable +0x38) -- Update
// ============================================================
// Updates both consumed and produced item lists.
//
// Original: 0x140075A20.

// [7] 0x140075a20
void ProductionUI::OnUpdateLayout() {
    // 0x140075a20: `XOR EDX,EDX; CALL 0x140075a50; MOV EDX,1; JMP 0x140075a50`, no test
    // (with no cached rows the helper still runs the year-change tracker rebuild).
    UpdateList(0);
    UpdateList(1);
}

// ============================================================
