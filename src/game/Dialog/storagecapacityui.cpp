// engine/src/ui/storagecapacityui.cpp
// Clean reimplementation of StorageCapacityUI game logic.
//
// StorageCapacityUI (0x38 bytes) — storage capacity progress bar.
// Extends DialogController. Fields: Dialog/StorageCapacityUI.h (the progress
// element at +0x30).
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Dialog/exe_bodies.h"
#include "Component/exe_bodies.h"
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ProgressElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Object/factory_registration.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)
#include "Component/StorageComponent.h"   // X::TypeId() (B.3.9 CG2)

namespace storagecapacityui_sa {

// ENDGAME B.3.9 CG2: TypeId_ProgressElement -> UI::ProgressElement::TypeId(),
// TypeId_ComponentEntity -> ComponentEntity::TypeId(), TypeId_StorageComponent_FC
// -> StorageComponent::TypeId(). The class statics always answer, so the lazy-fill of
// the exe-.rdata globals retires. (B3R: the empty EnsureGlobals that was left is
// gone with its calls.)

bool Ready() {
    return true;
}

static void Trace(const char* what, StorageCapacityUI* self) {
    fprintf(stderr, "[StorageCapacityUI] %s self=%p ready=%d progress(+0x30)=%p\n",
            what, static_cast<void*>(self), Ready() ? 1 : 0,
            self ? static_cast<void*>(self->progress) : nullptr);
    fflush(stderr);
}

}  // namespace storagecapacityui_sa

// ============================================================
// StorageCapacityUI::OnCreated (vtable +0x20) — Find progress element
// ============================================================
// Finds the ProgressElement in the dialog by type ID and stores it
// at +0x30. Sets the progress element's max value to 1.0.
//
// Original: 0x14007b600.

// [4] 0x14007b600
void StorageCapacityUI::OnCreated() {
    if (!storagecapacityui_sa::Ready()) { storagecapacityui_sa::Trace("Serialize SKIP", this); return; }
    storagecapacityui_sa::Trace("Serialize", this);

    // Find ProgressElement by type under the panel's root, type-checked
    uint32_t typeId = UI::ProgressElement::TypeId();
    UI::Element* found = _container->FindElementType(typeId).Get();
    progress = ::Object::IsDerivedFromNullSafe(found, typeId)
                   ? static_cast<UI::ProgressElement*>(found) : nullptr;

    // Set progress max value (+0x124) to 1.0f
    progress->SetMaximumValue(1.0f);
}

// ============================================================
// StorageCapacityUI::OnUpdateLayout (vtable +0x38) — Update progress display
// ============================================================
// Queries the entity's storage component for current count and capacity,
// calculates the fill ratio, and updates the progress bar with a
// percentage string.
//
// Original: 0x14007b4d0.

// [7] 0x14007b4d0
void StorageCapacityUI::OnUpdateLayout() {
    if (!storagecapacityui_sa::Ready()) return;
    // Per-frame (+0x38): bail on a dead / marked-for-removal datasource before the
    // IsDerivedFrom probe. Reliable now that removals are one-frame-deferred
    // (entitylist.cpp Step 0) — the entity is valid marked-but-alive memory here.
    if (!SA_DialogEntityAlive(this)) return;

    // The entity from the data source (IsA ComponentEntity)
    Object::Base* dataSource = _dataSource;
    ComponentEntity* entity = nullptr;
    if (::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId()))
        entity = static_cast<ComponentEntity*>(dataSource);

    // Find StorageComponent (retail 0x140006270). The tree's finder returned null
    // for a null entity: kept.
    StorageComponent* component = entity ? entity->FindComponent<StorageComponent>() : nullptr;
    // SA safety: a partially-materialized storage entity can resolve a null-wrapped
    // component IBP. Skip crash-safe.
    if (!component) return;

    // Query current count
    int count = component->QueryCount();   // a real function (storagecomponent.cpp), not a door

    // The component's total capacity
    int capacity = component->capacity;

    // Calculate fill ratio
    float ratio;
    if (capacity == 0) {
        ratio = 0.0f;
    } else {
        ratio = (float)count / (float)capacity;
    }

    // Calculate display percentage: (int)(ratio * 100.0 + 0.5)
    float displayVal = ratio * 100.0f + 0.5f;

    // SA safety: OnCreated only caches the element when the dialog template
    // actually contains a ProgressElement. Skip if it did not.
    UI::ProgressElement* progElem = progress;
    if (!progElem) return;
    progElem->SetCurrentValue(ratio);

    // Format percentage string
    System::String str = System::String::Format("%i", (int)(uint32_t)(int)displayVal);

    // Set on progress element
    progElem->SetProgressTextParameter(0, str);
}

// ============================================================
// STANDALONE promotion — real StorageCapacityUI controller vtable
// ============================================================
// A storage building's info panel (StorageBarn / StockpileYard) contains a
// ProgressElement bound to a StorageCapacityUI controller that shows the "X%"
// fill number. In standalone this type was a StubNop, so OnCreated (find the
// progress element) and OnUpdateLayout (compute count/capacity and set the %
// text) never ran and the number never appeared.
//
// Vtable offsets (verified from the exe StorageCapacityUI vtable @ 0x14015d950):
//   +0x00 (idx0) ScalarDtor  (0x140014450 — trivial: reset vtable + free)
//   +0x10 (idx2) GetTypeId   (0x1400143d0)
//   +0x20 (idx4) OnCreated       (0x14007b600) — find ProgressElement, cache at +0x30
//   +0x38 (idx7) OnUpdateLayout  (0x14007b4d0) — compute % and set the progress text
//   +0x40 (idx8)             — base DialogController::OnUpdate, inherited (table slot
//                0x14003a96c). OnCreated runs from DialogInstance::AddController
//                (0x18011343d); OnUpdateLayout every frame from
//                DialogInstance::ComputeLayout (B6 row 36).

// create 0x1400143f0: MemAlloc(0x38, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* StorageCapacityUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(StorageCapacityUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) StorageCapacityUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* StorageCapacityUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) StorageCapacityUI());
}

DEFINE_EXE_TYPE(StorageCapacityUI, "UI::DialogController")

