// src/game/Dialog/cropui_promote.cpp
//
// CropUI: the three pure virtuals of the SelectResourceUI base, CropUI's own idx4/idx7,
// and the standalone vtable promotion.
//
// ⚠ The Slot4/Slot7/Slot8 bodies in cropui.cpp are the ABSTRACT BASE's
// (0x140078be0 / 0x140078cb0 / 0x140078d30), not CropUI's — the file's comments
// attribute them to CropUI, which is wrong. Read byte-wise from retail's vtable at
// 0x14015f270 (14 slots), CropUI's own table is:
//   idx4  (+0x20) 0x1400b2b50  CropUI_Startup — base idx4, then bind "yield"
//   idx7  (+0x38) 0x1400b2a50  CropUI_Update  — maturity into the bar, then base idx7
//   idx8  (+0x40) 0x140078d30  SelectResourceUI::Slot8 (combo refresh)
//   idx9  (+0x48) 0x140078d80  SelectResourceUI::Slot9 (combo selection -> idx11)
//   idx11 (+0x58) 0x1400b2920  set crop type on CropFieldComponent+0x50
//   idx12 (+0x60) 0x1400b29d0  get current crop type
//   idx13 (+0x68) 0x1400b29c0  category filter — bytes are `33 C0 C3`
//                              (XOR EAX,EAX; RET) = 0. Orchard is 1, pasture 2.
//
// WHY THIS FILE EXISTS: CropUI was the last member of the SelectResourceUI family still
// on UI_DIALOG_STUB (OrchardUI and PastureUI were promoted earlier), so every body in
// cropui.cpp was dead code — the combo was never bound, never given element id 500, and
// never populated, and idx9 was a no-op so a pick could not reach idx11. That is why the
// crop field's "?" button did nothing: the dropdown opened with zero rows and had no
// handler behind it. Structure follows orchardui.cpp exactly.

#include "Dialog/ui_panels.h"
#include "Dialog/CropUI.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Dialog/exe_bodies.h"
#include "system/String.h"
#include "Object/factory_registration.h"
#include "Work/exe_bodies.h"        // B3R: WorkAcc_FindComponent_5380
#include "Component/exe_bodies.h"   // B3R: CropFieldComponent_Maturity
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ProgressElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Component/Component.h"
#include "Entity/ComponentEntity.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Component/CropFieldComponent.h"   // B4 DLG-a

namespace {

// FUN_140005380 = FindComponent<CropFieldComponent>, FUN_14011b410 =
// CropFieldComponent_Maturity: called by name (B3R).

// Resolve the CropFieldComponent behind this panel's entity. Mirrors the retail
// prologue shared by idx7/idx11/idx12: entity(+0x28) -> IsDerivedFrom(ComponentEntity)
// -> FindComponent<CropFieldComponent> (0x140005380; no null test there, as retail).
CropFieldComponent* FindComp(CropUI* self) {
    if (!self) return nullptr;
    Object::Base* entity = self->_dataSource;
    uint32_t entTid = ComponentEntity::TypeId();
    ComponentEntity* checked =
        ::Object::IsDerivedFromNullSafe(entity, entTid) ? static_cast<ComponentEntity*>(entity) : nullptr;
    return checked->FindComponent<CropFieldComponent>();   // 0x140005380
}

// ENDGAME B.3.9: UI::ProgressElement now has a ported TypeId() (a class static, always
// populated), so the gated-global-vs-private-resolve split collapses to the static.
uint32_t ProgressElementTypeId() {
    return UI::ProgressElement::TypeId();
}

}  // namespace

// ============================================================
// idx11 (+0x58) — retail 0x1400b2920. Set the crop type.
// ============================================================
// If the field currently has none and a real type is picked, retail dispatches the
// component's own vtable[14](comp, 2) BEFORE storing. Order matters: that handler reads
// +0x50 as "still unset" to decide this is a fresh planting.
// [11] 0x1400b2920
void CropUI::SetResource(ComponentDescription* newType) {
    CropFieldComponent* comp = FindComp(this);

    // SA guard (retail unguarded): in retail a combo event can only be routed to an
    // entity that has a CropFieldComponent.
    if (!comp) return;

    // The field's crop type (+0x50)
    if (comp->cropDescription == nullptr && newType != nullptr) {
        comp->Command(2);   // slot 14
    }
    comp->cropDescription = newType;
}

// idx12 (+0x60) — retail 0x1400b29d0. Current crop type via out-param.
// [12] 0x1400b29d0
ComponentDescription** CropUI::GetResource(ComponentDescription** out) {
    CropFieldComponent* comp = FindComp(this);
    *out = comp ? comp->cropDescription : nullptr;
    return out;
}

// idx13 (+0x68) — retail 0x1400b29c0 is literally `XOR EAX,EAX; RET`.
// [13] 0x1400b29c0
int CropUI::GetCategory() {
    return 0;
}

// ============================================================
// STANDALONE promotion — real CropUI controller vtable
// ============================================================
// idx9 (+0x48) — combo selection -> idx11 — is SelectResourceUI's slot, inherited
// (retail's CropUI table holds the base's 0x140078d80); it was StubNop in the tree
// once, the slot that swallowed every pick on the "?" combo.

// [4] 0x1400b2b50
// idx4 (+0x20) — retail 0x1400b2b50: base idx4 (combo bind + element id 500 +
// populate), then find "yield" and seat it at +0x38 with progress 1.0f.
void CropUI::OnCreated() {
    // Needs the DialogInstance (+0x18) the framework seats before dispatch.
    if (!_dialog) return;

    SelectResourceUI::OnCreated();   // 0x140078be0, the base's idx4

    UI::Element* raw;
    {
        System::String nameStr("yield");
        raw = _container->FindElement(nameStr).Get();
    }

    yield = ::Object::IsDerivedFromNullSafe(raw, ProgressElementTypeId())
                ? static_cast<UI::ProgressElement*>(raw) : nullptr;

    // Retail writes this unguarded (Crop.rsc always carries "yield"); guarded here for
    // the same reason orchardui.cpp is — the shared StandardDialog mount could reach a
    // layout without it, which retail cannot.
    if (yield)
        yield->_maxValue = 1.0f;

}

// [7] 0x1400b2a50
// idx7 (+0x38) — retail 0x1400b2a50: mean maturity into the yield bar + percent text,
// then the base's idx7 (enable the combo iff it has items).
void CropUI::OnUpdateLayout() {
    if (!SA_DialogEntityAlive(this)) return;

    // Self-heal, same as orchardui.cpp: Slot4 may not have run for this instance yet.
    if (!yield) {
        CropUI::OnCreated();
        if (!yield) return;
    }

    CropFieldComponent* comp = FindComp(this);
    if (!comp) return;

    float maturity = comp->Maturity();   // 0x14011b410

    UI::ProgressElement* yieldElem = yield;
    yieldElem->_currentValue = maturity;

    int percent = static_cast<int>(maturity * 100.0f + 0.5f);
    {
        System::String textStr = System::String::Format("%i",
                         static_cast<uint64_t>(static_cast<uint32_t>(percent)));
        yieldElem->SetProgressTextParameter(0, textStr);
    }

    if (selectResource)
        SelectResourceUI::OnUpdateLayout();   // = 0x140078cb0, the base's idx7
}



// create 0x140020b10: MemAlloc(0x60, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* CropUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CropUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CropUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* CropUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CropUI());
}

DEFINE_EXE_TYPE(CropUI, "SelectResourceUI")
