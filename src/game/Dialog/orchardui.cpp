// engine/src/ui/orchardui.cpp
// Clean reimplementation of OrchardUI game logic.
//
// OrchardUI (0x48 bytes) — orchard crop selection and growth display.
// Extends SelectResourceUI > DialogController.
// +0x30 = combo element (SelectResourceUI base), +0x38 = yield progress element.
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Object/factory_registration.h"   // Object::IsDerivedFrom
#include "system/String.h"
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ComboElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "ui/ProgressElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Entity/ComponentEntity.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Component/Component.h"   // B.3.x W0: Component::Command by name
#include "Component/OrchardComponent.h"
#include "Component/ComponentDescription.h"
#include "Dialog/OrchardUI.h"

// Retail's Slot4 filters the "yield" FindElement result through
// IsDerivedFrom(elem, *TypeId_ProgressElement) before storing it at +0x38.
// That exe-.rdata slot (0x1401515b8) is filled ONLY by
// standalone/reg_element_typeids.cpp, which is gated on BANISHED_LIVE_ELEMENT_TIDS
// and is NOT in the default-on table — so on the default path it is null, and
// IsDerivedFrom(elem, 0) is always false. That would leave +0x38 null and turn the
// unguarded `+0x124 = 1.0f` write two lines later into a null-deref crash.
// See [[reference-gated-element-typeid-disables-controller]] — the same null field
// that made every pasture ControlUI button inert.
//
// ENDGAME B.3.9: UI::ProgressElement now has a ported TypeId() (a class static,
// always populated), so the gated-global-vs-private-resolve split collapses to it.
static uint32_t OrchardUI_ProgressElementTypeId() {
    return UI::ProgressElement::TypeId();
}

namespace orchardui_sa {

static void Trace(const char* what, OrchardUI* self) {
    fprintf(stderr, "[OrchardUI] %s self=%p combo(+0x30)=%p yield(+0x38)=%p\n",
            what, self,
            self ? self->selectResource : nullptr,
            self ? self->yield : nullptr);
    fflush(stderr);
}

// The data source (DialogController +0x28) as the ComponentEntity it is checked to be.
static ComponentEntity* CheckedEntity(OrchardUI* self) {
    Object::Base* entity = self->_dataSource;
    uint32_t entityTypeId = ComponentEntity::TypeId();
    ComponentEntity* checkedEntity = nullptr;
    if (::Object::IsDerivedFromNullSafe(entity, entityTypeId)) {
        checkedEntity = static_cast<ComponentEntity*>(entity);
    }
    return checkedEntity;
}

}  // namespace orchardui_sa

// ============================================================
// OrchardUI::Slot4 (vtable +0x20) — Initialize yield progress + combo
// ============================================================
// Finds the "yield" progress element, stores at +0x38, sets initial
// progress value to 1.0f, then calls SelectResourceUI::Slot4 to
// initialize the combo element.
//
// Original: 0x1400755B0.

// [4] 0x1400755b0
void OrchardUI::OnCreated() {
    if (!_dialog) {
        orchardui_sa::Trace("Slot4 SKIP (no DialogInstance)", this);
        return;
    }

    // Find "yield" progress element in the container (+0x20)
    System::String nameStr("yield");

    UI::Element* rawElem = _container->FindElement(nameStr).Get();

    // Store yield element at +0x38, faithful to retail: the find result is kept only
    // if it really is a ProgressElement (see the type-id note at the top of the file —
    // this check used to be skipped, which is safe but unfaithful).
    yield = ::Object::IsDerivedFromNullSafe(rawElem, OrchardUI_ProgressElementTypeId())
                ? static_cast<UI::ProgressElement*>(rawElem) : nullptr;

    // Set initial progress value to 1.0f (100%).
    // Retail writes this unguarded — Orchard.rsc always carries a "yield" progress
    // element, so the deref is load-bearing there. SA guard: the panel template is
    // reached through the shared StandardDialog mount, so a dialog whose layout has
    // no "yield" would fault here where retail never can. Skip the write, don't fake it.
    if (yield)
        yield->_maxValue = 1.0f;

    // Initialize combo via base class
    SelectResourceUI::OnCreated();
    orchardui_sa::Trace("Slot4", this);
}

// ============================================================
// OrchardUI::Slot7 (vtable +0x38) — Update growth progress
// ============================================================
// Gets the OrchardComponent from the entity, queries growth progress
// as a float ratio, updates the yield progress bar with percentage text,
// then enables/disables the combo based on item count.
//
// Original: 0x1400754B0.

// [7] 0x1400754b0
void OrchardUI::OnUpdateLayout() {

    // Per-frame (+0x38): bail on a dead / marked-for-removal datasource before the
    // IsDerivedFrom probe. Same guard PastureUI_Slot7 carries, same reason.
    if (!SA_DialogEntityAlive(this)) return;

    // Get entity at +0x28, check ComponentEntity
    ComponentEntity* checkedEntity = orchardui_sa::CheckedEntity(this);

    // Find OrchardComponent on entity
    OrchardComponent* orchard = checkedEntity->FindComponent<OrchardComponent>();

    // SA guard (retail unguarded): an entity without an OrchardComponent, or the
    // combo/yield elements not seated yet because Slot4 has not run for this
    // instance. Retail cannot reach either state — the panel is only mounted on
    // Orchard.rsc and Slot4 always precedes the first tick.
    if (!orchard) return;
    if (!selectResource || !yield) return;

    // Get average growth ratio (0.0 - 1.0)
    float growth = orchard->GetGrowth();   // 0x140051640

    // Set progress bar value directly (ProgressElement::_currentValue)
    UI::ProgressElement* yieldElem = yield;
    yieldElem->_currentValue = growth;

    // Format percentage text: (int)(growth * 100.0f + 0.5f)
    int percent = (int)(growth * 100.0f + 0.5f);
    System::String textStr = System::String::Format("%i", (uint64_t)(uint32_t)percent);

    // Set progress text parameter
    yieldElem->SetProgressTextParameter(0, textStr);

    // Enable/disable combo based on item count (SelectResourceUI base logic)
    UI::ComboElement* combo = selectResource;
    int itemCount = combo->GetItemCount();
    combo->Enable(itemCount > 0);   // Element slot 8 Enable
}

// ============================================================
// OrchardUI::Slot11 (vtable +0x58) — Set resource type
// ============================================================
// Sets the selected crop/resource type on the OrchardComponent.
// If changing from no resource (0) to a valid resource, calls
// vtable[14](comp, 2) to initialize production.
//
// Original: 0x140075380.

// [11] 0x140075380
void OrchardUI::SetResource(ComponentDescription* newType) {

    // Get entity at +0x28, check ComponentEntity
    ComponentEntity* checkedEntity = orchardui_sa::CheckedEntity(this);

    // Find OrchardComponent on entity
    OrchardComponent* orchard = checkedEntity->FindComponent<OrchardComponent>();

    // SA guard (retail unguarded): fires from a combo event, which can only be
    // routed to an entity that has an OrchardComponent in retail.
    if (!orchard) return;

    // If no tree type is set yet and the new one is valid, start production
    // (Command(2), slot 14).
    if (orchard->treeType == nullptr && newType != nullptr) {
        orchard->Command(2);   // slot 14
    }

    // Set the tree type (+0x60)
    orchard->treeType = newType;
}

// ============================================================
// OrchardUI::Slot12 (vtable +0x60) — Get resource type
// ============================================================
// Returns the current crop/resource type from the OrchardComponent
// at comp+0x60 via the output parameter.
//
// Original: 0x140075430.

// [12] 0x140075430
ComponentDescription** OrchardUI::GetResource(ComponentDescription** param2) {

    // Get entity at +0x28, check ComponentEntity
    ComponentEntity* checkedEntity = orchardui_sa::CheckedEntity(this);

    // Find OrchardComponent on entity
    OrchardComponent* orchard = checkedEntity->FindComponent<OrchardComponent>();

    // Return the tree type (+0x60) (SA guard: see Slot11).
    // (the ExternalPtr retslot)
    *param2 = orchard ? orchard->treeType : nullptr;
    return param2;
}

// ============================================================
// Slot13 (vtable +0x68): GetCategoryFilter — returns 1 (orchard category)
// Original: 0x140075420 (6 bytes, MOV EAX,1; RET, unhookable)
// ============================================================
// [13] 0x140075420
int OrchardUI::GetCategory() {
    return 1;
}

// ============================================================
// STANDALONE promotion — real OrchardUI controller vtable
// ============================================================
// OrchardUI was a UI_DIALOG_STUB (all-no-op), so every port above was dead code:
// the orchard panel's tree-type combo stayed empty and the yield bar never moved.
// Follows pastureui.cpp exactly — same class family, same base.
//
// ⚠ Vtable address corrected 2026-08-09 by a byte-wise read. Three tables in this
// family were being confused, and decomp/application.md had two of them wrong:
//   0x14015cf30 = vtable_OrchardUI          (doc said 0x14015d330 — WRONG)
//   0x14015f610 = vtable_PastureUI          (doc said 0x14015d330 — WRONG)
//   0x14015f270 = vtable_CropUI
//   0x14015d330 = vtable_SelectResourceUI, the ABSTRACT base. Ghidra labels it
//                 `vtable_CropUI`, which is a misnomer; its _purecall (0x14003addc)
//                 entries at idx11/12/13 are simply the three pure virtuals
//                 (SetResource / GetResource / GetCategoryFilter).
//
// Retail vtable @ 0x14015cf30 (14 slots), read byte-wise:
//   idx3,5,6,10 -> the shared 5-6 byte micro-rets at 0x14003a95a-0x14003a978,
//                  for which the generic StubNop is already byte-faithful.
//   idx4  (+0x20) 0x1400755b0 Slot4  — find "yield" ProgressElement, set 100%, base setup
//   idx7  (+0x38) 0x1400754b0 Slot7  — per-frame: growth % into the yield bar
//   idx8  (+0x40) 0x140078d30        — SelectResourceUI::Slot8 (combo refresh)
//   idx9  (+0x48) 0x140078d80        — SelectResourceUI::Slot9 (combo selection)
//   idx11 (+0x58) 0x140075380 Slot11 — set resource type on OrchardComponent
//   idx12 (+0x60) 0x140075430 Slot12 — get current type
//   idx13 (+0x68) 0x140075420 Slot13 — category filter = 1 (orchard)
// That closes the work list at the vtable boundary: every real slot is covered.
//
// Retail's Slot4/Slot7 tail-call 0x140078be0 / 0x140078cb0 — Ghidra names those
// `CropUI_Serialize` / `CropUI_OnCreate`, but they are idx4/idx7 of the abstract
// base table above, i.e. SelectResourceUI's own. Slot7's tail is exactly the
// "enable the combo iff it has items" logic already inlined in OrchardUI_Slot7.





// [9] STANDALONE DEVIATION (retail inherits the base slot) — the tree's wiring, kept.
void OrchardUI::OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t a, int32_t b,
                               int32_t c, int32_t d, int32_t e, void* userData) {
    orchardui_sa::Trace("Slot9(event)", this);
    SelectResourceUI::OnElementEvent(element, a, b, c, d, e, userData);
}




// create 0x140012ff0: MemAlloc(0x48, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* OrchardUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(OrchardUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) OrchardUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* OrchardUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) OrchardUI());
}

DEFINE_EXE_TYPE(OrchardUI, "SelectResourceUI")
