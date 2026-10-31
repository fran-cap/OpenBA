// engine/src/ui/pastureui.cpp
// Clean reimplementation of PastureUI game logic.
//
// PastureUI (0x40 bytes) — pasture animal selection panel.
// Extends SelectResourceUI > DialogController.
// +0x30 = combo element (SelectResourceUI base), +0x38 = slider element.
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Entity/CitizenBehavior.h"   // m_citizenList->field_0x120 (B4 DLG-d merge fix)
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ComboElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/SliderElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Object/factory_registration.h"
#include <cstdlib>
#include <cstdio>
#include "Component/AnimalDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/Component.h"   // B.3.x W0: Component::Command by name
#include "Component/PastureComponent.h"
#include "Component/ComponentDescription.h"

// ============================================================
// PastureUI::UpdateSlider (FUN_1400b4fa0)
// ============================================================
// Finds PastureComponent on the data-source entity.
// Updates the slider element (+0x38):
//   _minimum = 0
//   _maximum = (float)PastureComponent::ComputeCapacity
//   _value   = (float)pastureComp->currentAnimalCount (+0x40)
//
// Original: FUN_1400b4fa0.
//
void PastureUI::UpdateSlider() {
    // Reached from per-frame + event slots; bail on a dead / marked datasource.
    if (!SA_DialogEntityAlive(this)) return;
    // Get entity from the data source, checked as a ComponentEntity
    Object::Base* entity = _dataSource;
    bool isEntity = ::Object::IsDerivedFromNullSafe(entity, ComponentEntity::TypeId());
    ComponentEntity* entityVal = isEntity ? static_cast<ComponentEntity*>(entity) : nullptr;

    // Find PastureComponent
    PastureComponent* pasture = entityVal->FindComponent<PastureComponent>();
    if (!pasture) return;

    // The slider element (+0x38)
    if (!slider) return;

    // Set slider min to 0
    slider->_minimum = 0.0f;

    // Set slider max = PastureComponent::ComputeCapacity (0x140128570)
    uint32_t maxCount = (uint32_t)pasture->ComputeCapacity();
    slider->_maximum = (float)(int)maxCount;

    // Set slider current = the pasture's capacity (+0x40)
    int currentCount = pasture->capacity;
    slider->_value = (float)currentCount;
}

namespace pastureui_sa {

static void Trace(const char* what, void* self) {
    fprintf(stderr, "[PastureUI] %s self=%p\n", what, self);
    fflush(stderr);
}

}  // namespace pastureui_sa

// ============================================================
// PastureUI::Slot4 (vtable +0x20) — Initialize combo + slider elements
// ============================================================
// Calls base SelectResourceUI::Slot4 (find combo, populate),
// then finds "slider" element, stores at +0x38, sets ID 501,
// and updates slider display.
//
// Original: 0x1400B5090.

// [4] 0x1400b5090
void PastureUI::OnCreated() {
    if (!_dialog) {
        pastureui_sa::Trace("Slot4 SKIP (no DialogInstance)", this);
        return;
    }
    pastureui_sa::Trace("Slot4", this);

    // Initialize combo via base class
    SelectResourceUI::OnCreated();

    // Find "slider" element in the container (+0x20)
    System::String nameBuf("slider");

    UI::Element* rawElem = _container->FindElement(nameBuf).Get();

    // Store slider element at +0x38 (skip IsDerivedFrom, null check only)
    slider = static_cast<UI::SliderElement*>(rawElem);

    // Set slider element ID to 501 (0x1f5)
    slider->SetElementId(0x1f5);

    // Update slider display with current/max animal counts
    UpdateSlider();
}

// ============================================================
// PastureUI::Slot7 (vtable +0x38) — Update slider text + combo enable
// ============================================================
// Enables/disables combo based on item count and current animal count.
// Updates slider text parameters with current and max animal counts.
//
// Original: 0x1400B4E50.

// [7] 0x1400b4e50
void PastureUI::OnUpdateLayout() {
    // Per-frame (+0x38): bail on a dead / marked-for-removal datasource before the
    // IsDerivedFrom probe (reliable via the one-frame removal deferral, see
    // entitylist.cpp Step 0).
    if (!SA_DialogEntityAlive(this)) return;
    // Get entity → find PastureComponent
    Object::Base* dataSource = _dataSource;
    bool isEntity = ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId());
    ComponentEntity* entity = isEntity ? static_cast<ComponentEntity*>(dataSource) : nullptr;

    PastureComponent* comp = entity->FindComponent<PastureComponent>();
    // SA guard (retail unguarded): entity without a PastureComponent, or the
    // combo/slider elements not seated yet (Slot4 hasn't run) — bail.
    if (!comp) return;
    if (!selectResource || !slider) return;

    // Enable combo only if it has items AND pasture has no animals yet
    UI::ComboElement* comboElem = selectResource;
    int itemCount = comboElem->GetItemCount();
    bool enableCombo = (itemCount > 0) && (comp->animals._size == 0);

    // Set combo enabled/disabled via vtable[8]
    comboElem->Enable(enableCombo);   // Element slot 8 Enable

    // Format current animal count → slider param 0
    UI::SliderElement* sliderElem = slider;
    {
        System::String fmtStr = System::String::Format("%i", comp->animals._size);
        sliderElem->SetParameter(0, fmtStr);
    }

    // Format max animal count → slider param 1
    {
        System::String fmtStr = System::String::Format("%i", comp->capacity);
        sliderElem->SetParameter(1, fmtStr);
    }
}

// ============================================================
// PastureUI::Slot11 (vtable +0x58) — Set resource type
// ============================================================
// Sets the selected animal/resource type on the PastureComponent.
// If changing from no resource (0) to a valid resource, calls
// vtable[14](comp, 2) to initialize pasture. Then recalculates
// max count and updates the slider display.
//
// Original: 0x1400B4C40.

// [11] 0x1400b4c40
void PastureUI::SetResource(ComponentDescription* newType) {
    // Bail on a dead / marked-for-removal datasource before probing it (see Slot7).
    if (!SA_DialogEntityAlive(this)) return;
    // Get entity at +0x28, check ComponentEntity
    Object::Base* entity = _dataSource;
    uint32_t entityTypeId = ComponentEntity::TypeId();
    ComponentEntity* checkedEntity = nullptr;
    if (::Object::IsDerivedFromNullSafe(entity, entityTypeId)) {
        checkedEntity = static_cast<ComponentEntity*>(entity);
    }

    // Find PastureComponent on entity
    PastureComponent* pasture = checkedEntity->FindComponent<PastureComponent>();
    if (!pasture) return;   // SA guard (retail unguarded)

    // If currently no resource set (comp+0x28 == 0) and new resource is valid,
    // set resource first then call initialization via vtable[14](comp, 2)
    if (pasture->livestockType == nullptr && newType != nullptr) {
        pasture->livestockType = newType;
        pasture->Command(2);   // slot 14
    }

    // Set the resource type
    pasture->livestockType = newType;

    // Recalculate max animal count from pasture dimensions
    uint32_t maxCount = (uint32_t)pasture->ComputeCapacity();
    pasture->capacity = (int32_t)maxCount;

    // Update slider display
    UpdateSlider();
}

// ============================================================
// PastureUI::Slot12 (vtable +0x60) — Get resource type
// ============================================================
// Returns the current animal/resource type from the PastureComponent
// at comp+0x28 via the output parameter.
//
// Original: 0x1400B4D00.

// [12] 0x1400b4d00
ComponentDescription** PastureUI::GetResource(ComponentDescription** param2) {
    // Get entity at +0x28, check ComponentEntity
    Object::Base* entity = _dataSource;
    uint32_t entityTypeId = ComponentEntity::TypeId();
    ComponentEntity* checkedEntity = nullptr;
    if (::Object::IsDerivedFromNullSafe(entity, entityTypeId)) {
        checkedEntity = static_cast<ComponentEntity*>(entity);
    }

    // Find PastureComponent on entity
    PastureComponent* pasture = checkedEntity->FindComponent<PastureComponent>();
    // (the ExternalPtr retslot)
    if (!pasture) { *param2 = nullptr; return param2; }  // SA guard

    // Return resource type at comp+0x28
    *param2 = pasture->livestockType;
    return param2;
}

// ============================================================
// PastureUI::Slot9 (vtable +0x48) — Combo/slider event handler
// ============================================================
// Handles two event types:
//   0x14/500: Combo selection — dispatches resource type via Slot11.
//   0x17/501: Slider change — updates max animal count on PastureComponent.
//
// Original: 0x1400B4E20.

// [9] 0x1400b4e20
void PastureUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, int32_t param3, int32_t param4,
                               int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    pastureui_sa::Trace("Slot9(event)", this);

    if (param3 == 0x14 && param4 == 500) {
        // Combo selection: get selected resource, call Slot11 via vtable
        const UI::ComboElement* comboElem = selectResource;
        int currentItem = comboElem->GetCurrentItem();
        void* itemData = comboElem->GetItemData(currentItem);
        int resourceIndex = (int)(intptr_t)itemData;  // CAST-OK: ptr_int

        // The town's template list (CitizenBehavior +0x120)
        GameUpdateLoop* world = GameUpdateLoop::_singleton;
        ComponentDescription* resourceType = world->m_citizenList->field_0x120._data[resourceIndex];

        SetResource(resourceType);   // slot 11, virtual (as retail)
    }
    else if (param3 == 0x17 && param4 == 501) {
        // Slider change: read slider value, update max count on PastureComponent
        Object::Base* entity = _dataSource;
        bool isEntity = ::Object::IsDerivedFromNullSafe(entity, ComponentEntity::TypeId());
        ComponentEntity* checkedEntity = isEntity ? static_cast<ComponentEntity*>(entity) : nullptr;

        PastureComponent* pasture = checkedEntity->FindComponent<PastureComponent>();

        float sliderValue = slider->_value;
        pasture->capacity = (int32_t)sliderValue;
    }
}

// ============================================================
// Slot13 (vtable +0x68): GetCategoryFilter — returns 2 (pasture category)
// Original: 0x1400B4CF0 (6 bytes, MOV EAX,2; RET, unhookable)
// ============================================================
// [13] 0x1400b4cf0
int PastureUI::GetCategory() {
    return 2;
}

// ============================================================
// STANDALONE promotion — real PastureUI controller vtable
// ============================================================
// PastureUI was a UI_DIALOG_STUB (all-no-op) so every port above was dead code
// and the pasture panel's type combo / animal slider stayed empty — the
// BANISHED_PASTURE_AUTOTYPE stand-in existed only because Slot11 was
// unreachable. Promotion follows the StorageUI/ResidenceUI pattern (stub
// registration removed from ui_dialog_stub_factories.cpp).
//
// Retail vtable @ 0x14015f610 (14 slots):
//   idx4  (+0x20) 0x1400b5090 Slot4  — setup: base combo find/populate + slider
//   idx7  (+0x38) 0x1400b4e50 Slot7  — per-frame: combo enable + slider text
//   idx8  (+0x40) 0x140078d30        — SelectResourceUI::Slot8 (combo refresh;
//                                      the slot the framework ticks per frame)
//   idx9  (+0x48) 0x1400b4e20 Slot9  — events: combo 0x14/500, slider 0x17/501
//   idx11 (+0x58) 0x1400b4c40 Slot11 — SetLivestockType (drives Pasture Slot14 cmd 2)
//   idx12 (+0x60) 0x1400b4d00 Slot12 — get current type
//   idx13 (+0x68) 0x1400b4cf0 Slot13 — category filter = 2 (livestock)









// create 0x140020e10: MemAlloc(0x40, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* PastureUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PastureUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PastureUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* PastureUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PastureUI());
}

DEFINE_EXE_TYPE(PastureUI, "SelectResourceUI")
