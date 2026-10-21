// engine/src/ui/resourcelimitui.cpp
// Clean reimplementation of ResourceLimitUI game logic.
//
// ResourceLimitUI (0x40 bytes) — resource limit spinner panel.
// Extends DialogController. Fields:
//   +0x10 _config (the row's ResourceLimitUIConfig: resource index at +0x20)
//   +0x30 editLimit (the SpinnerElement, InternalBasePtr)
//
// The resource limit value is stored in a global array accessed through
// the game world singleton: world->+0xF0->+0x170[index*0x20+8].
//
// Decompiled from Application-x64.exe.
//
// B4 DLG-c: the three retail-faithful C bodies ResourceLimitUI_Slot4 / _Slot7 /
// _Slot9 (0x140077970 / 0x1400778d0 / 0x1400777e0) had no caller: the class's
// members below are what the vtable dispatches. They read the resource index as
// retail does (IsDerivedFrom(config) ? config : null, then +0x20 unguarded, no
// bound) and differ from retail only in their null guards. The unreferenced
// copies were deleted with resourcelimitui_funcs.h.
//
// LEFT: the limit table is read through GameUpdateLoop::m_citizenList (void*,
// block GAME-a: CitizenBehavior::professionResources, +0x170).

#include "Dialog/ui_panels.h"
#include "Dialog/ResourceLimitUI.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Entity/CitizenBehavior.h"
#include <cstdio>
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/SpinnerElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Dialog/ResourceLimitUIConfig.h"   // X::TypeId() (B.3.9 CG2)
#include "ext/high_limits/high_limits.h"   // EXT SEAM ext.high_limits (class B): the spinner range (NOT RETAIL)

// ============================================================
// Standalone entry points — resource index supplied explicitly
// ============================================================
// The limit read / write helpers below take the ledger entry explicitly:
// townMgr(gw+0xf0)+0x170[idx*0x20+8] (CitizenBehavior::professionResources, 0x20 bytes an
// entry). The index is the row's config index (resourcelimitui_sa::IdxFor). Entries 0..10
// are the Town Hall's built-in limits (Log=0, Stone=1, Iron=2, Herbs=3, Tools=4, Fuel=5,
// Food=6, Clothing=7, Alcohol=8, Coal=9, Textile=10); 11..20 are the custom limits
// Custom0..Custom9.

static int GetResourceLimitByIdx(int resourceIndex) {
    if (!GameUpdateLoop::_singleton) return 0;
    GameUpdateLoop* worldSingleton = GameUpdateLoop::_singleton;
    if (!worldSingleton) return 0;
    CitizenBehavior* subObj = worldSingleton->m_citizenList;
    if (!subObj) return 0;
    ProfessionResourceEntry* arrayBase = subObj->professionResources._data;
    if (!arrayBase) return 0;
    return arrayBase[resourceIndex].maxCount;
}

static void SetResourceLimitByIdx(int resourceIndex, int value) {
    if (!GameUpdateLoop::_singleton) return;
    GameUpdateLoop* worldSingleton = GameUpdateLoop::_singleton;
    if (!worldSingleton) return;
    CitizenBehavior* subObj = worldSingleton->m_citizenList;
    if (!subObj) return;
    ProfessionResourceEntry* arrayBase = subObj->professionResources._data;
    if (!arrayBase) return;
    arrayBase[resourceIndex].maxCount = value;
}

// Read the live default/limit for a row (used by the trace + external checks).
int ResourceLimitUI_ReadLimit_SA(int resourceIndex) {
    return GetResourceLimitByIdx(resourceIndex);
}

// ---- ResourceLimitUI: PROMOTED to the real reimpl (resource-limit bring-up) --
// Each row of the Town Hall "Limits" tab (Log/Stone/Iron/Herbs/Tools/Fuel/Food/
// Clothing/Alcohol/Coal/Textile) is a ResourceLimitUI child controller bound to a
// ResourceLimitUIConfig (resource index at config+0x20). Slot4 (+0x20, setup)
// finds the row's "editLimit" spinner and seeds it from the live per-resource
// limit at townMgr(gw+0xf0)+0x170[idx*0x20+8]; Slot7 (+0x38) keeps it synced;
// Slot9 (+0x48) writes an edited value back. The generic stub left every slot a
// no-op, so all limit spinners showed the .rsc default 0 even though the
// town-manager ctor (TradeComponent_impl_0a30) seeds real per-resource defaults
// (200 / 5000 / 100 / 50 / 500 / 1000 ...). Construction stays the stub
// alloc+zero+vtable; the framework fills the base fields (dialog +0x18,
// config +0x10, element root +0x20) afterward. Same shape as the
// BuildRequirementUI / MapUI promotions above.
namespace resourcelimitui_sa {
    // The row's config (DialogController::_config, +0x10) as a ResourceLimitUIConfig. Retail's
    // Slot4 / Slot7 / Slot9 do IsDerivedFrom(config) ? config : null and read the resource index
    // at +0x20 off the result unguarded, so a row with no config would crash retail too.
    // AddController (dialog_create.cpp) always stores the config before OnCreated.
    static const ResourceLimitUIConfig* ConfigOf(ResourceLimitUI* self) {
        UI::DialogControllerConfig* config = self->_config.Get();
        if (config->IsDerivedFrom(ResourceLimitUIConfig::TypeId()))
            return static_cast<const ResourceLimitUIConfig*>(config);
        return nullptr;
    }
    // The ledger entry this row edits: the config's resource index, used as retail uses it, with no
    // bound.
    int IdxFor(ResourceLimitUI* self) {
        return ConfigOf(self)->_resourceIndex;
    }
    void Trace([[maybe_unused]] const char* slot, ResourceLimitUI* self, [[maybe_unused]] int idx) {
        static int n=0; if (n>=24) return; ++n;
        const ResourceLimitUIConfig* cfg = ConfigOf(self);
        [[maybe_unused]] int cfgIdx = cfg ? cfg->_resourceIndex : -1;
        printf("[ResourceLimitUI] %s self=%p cfg=%p cfg+0x20=%d idx=%d liveLimit=%d\n",
               slot, self, cfg, cfgIdx, idx, ResourceLimitUI_ReadLimit_SA(idx));
        fflush(stdout);
    }
}

// Setup (vtable +0x20): find "editLimit" within this row's element subtree, cache
// it at editLimit (+0x30), configure id/min/max, seed it from the live limit.
// [4] 0x140077970
void ResourceLimitUI::OnCreated() {
    int resourceIndex = resourcelimitui_sa::IdxFor(this);
    resourcelimitui_sa::Trace("Slot4", this, resourceIndex);
    UI::Element* dialogElem = _container;
    if (!dialogElem) return;
    System::String nameStr("editLimit");
    this->editLimit = static_cast<UI::SpinnerElement*>(dialogElem->FindElement(nameStr).Get());
    UI::SpinnerElement* spinner = this->editLimit;
    if (!spinner) return;
    spinner->SetElementId(0x3039);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HIGHLIMITS.md): 0..999,999,999 and nine typed digits
    // when the row is on; retail's range in the else arm.
    if (Ext::HighLimits::Active())
        Ext::HighLimits::ApplyToSpinner(spinner);
    else
        spinner->SetMinMax(0, 999999);
    spinner->SetValue(GetResourceLimitByIdx(resourceIndex));
}

// Update (vtable +0x38): keep the spinner synced with the live limit unless the
// user is actively editing it.
// [7] 0x1400778d0
void ResourceLimitUI::OnUpdateLayout() {
    int resourceIndex = resourcelimitui_sa::IdxFor(this);
    UI::SpinnerElement* spinner = this->editLimit;
    if (!spinner) return;
    int currentLimit = GetResourceLimitByIdx(resourceIndex);
    int spinnerVal = spinner->GetValue();
    if (currentLimit != spinnerVal && !spinner->IsActive())
        spinner->SetValue(currentLimit);
}

// Event (vtable +0x48): on spinner change (msg 0x15, id 0x3039) write the new
// value back to the live limit.
// [9] 0x1400777e0
void ResourceLimitUI::OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t msg, int32_t id,
                                     int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (msg != 0x15 || id != 0x3039) return;
    UI::SpinnerElement* spinner = this->editLimit;
    if (!spinner || !element) return;
    UI::Element* sender = element->Get();
    if (sender != spinner) {
        UI::Element* child = spinner->Child(0).Get();
        if (sender != child) return;
    }
    int resourceIndex = resourcelimitui_sa::IdxFor(this);
    resourcelimitui_sa::Trace("Slot9", this, resourceIndex);
    SetResourceLimitByIdx(resourceIndex, spinner->GetValue());
}
