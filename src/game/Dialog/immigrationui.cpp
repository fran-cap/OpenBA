// engine/src/ui/immigrationui.cpp
// Clean reimplementation of ImmigrationUI game logic.
//
// ImmigrationUI (0x50 bytes) — immigration allow/deny panel.
// Extends DialogController. Fields: Dialog/ImmigrationUI.h.
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdio>
#include "Object/factory_registration.h"
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/TextElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Component/ImmigrationComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/StateMachineComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)

// Helper: the named element under the panel's root
static UI::Element* FindElemByName(UI::Element* root, const char* name) {
    System::String nameBuf(name);
    return root->FindElement(nameBuf).Get();
}

// ---- ImmigrationUI: PROMOTED to a real SA vtable (immigrationui.cpp) ----------
// The Town Hall "Nomads" tab: Dialog/Immigration.rsc is inserted into the empty
// nomadTab GroupDescription and driven by ImmigrationUI (see Template/TownHall.rsc
// _controllers). The generic stub left every slot a no-op, so the tab showed the
// .rsc placeholder text and dead Allow/Deny buttons. Wiring Slot4 (+0x20 setup —
// finds textNone/textNomad/buttonAllow/buttonDeny, sets button ids), Slot7 (+0x38
// update — finds the ImmigrationComponent on the town-hall entity, enables the
// buttons + shows the nomad-request text when a group is waiting), and Slot9
// (+0x48 event — Allow=39000/Deny=0x9859 dispatch to the nomad camp's StateMachine)
// makes the tab live. Construction stays the stub alloc+zero+vtable; the framework
// fills the base fields (dialog +0x18, datasource entity +0x28) afterward — the
// same shape as the ResourceLimitUI/StatusBarUI promotions. The ported Slot7/Slot9
// degrade to the "no nomads" state if the entity/component is unbound in standalone
// (see immigrationui.cpp guards), so a missing ImmigrationComponent can't fault.
namespace immigrationui_sa {
    // ENDGAME B.3.9: ComponentEntity / ImmigrationComponent / StateMachineComponent
    // all have a ported TypeId() now; only the game-world singleton check stays.
    bool Ready() {
        return GameUpdateLoop::_singleton != nullptr;
    }
    // stderr, not stdout: the perf build strips printf, and the Immigrants wave's
    // harness runs (tools/stress_run.sh) only keep standalone_error.log.
    void Trace(const char* slot, ImmigrationUI* self) {
        static int n=0; if (n>=16) return; ++n;
        fprintf(stderr, "[ImmigrationUI] %s self=%p entity(+0x28)=%p dlg(+0x18)=%p\n", slot, self,
                self ? self->_dataSource : nullptr, self ? self->_dialog : nullptr);
        fflush(stderr);
    }

    // The data source as the ComponentEntity it is (IsDerivedFrom), else null.
    ComponentEntity* Entity(ImmigrationUI* self) {
        Object::Base* entity = self->_dataSource;
        if (::Object::IsDerivedFromNullSafe(entity, ComponentEntity::TypeId())) {
            return static_cast<ComponentEntity*>(entity);
        }
        return nullptr;
    }

    // The ImmigrationComponent on it. The finder the tree called returned null for
    // a null entity (a standalone guard; retail's FindComponent<T> has none): kept.
    ImmigrationComponent* FindImmigration(ComponentEntity* entity) {
        return entity ? entity->FindComponent<ImmigrationComponent>() : nullptr;   // 0x140012020
    }
    StateMachineComponent* FindStateMachine(ComponentEntity* entity) {
        return entity ? entity->FindComponent<StateMachineComponent>() : nullptr;   // 0x140003ea0
    }
}

// ============================================================
// ImmigrationUI::Slot4 (vtable +0x20) — Setup/refresh
// ============================================================
// Finds textNone, textNomad, buttonAllow, and buttonDeny
// elements in the dialog and sets element IDs on the buttons.
//
// Original: 0x14006B330.

// [4] 0x14006b330
void ImmigrationUI::OnCreated() {
    immigrationui_sa::Trace("Slot4", this);
    UI::Element* root = _container;

    // Find text elements
    textNone = static_cast<UI::TextElement*>(FindElemByName(root, "textNone"));
    textNomad = static_cast<UI::TextElement*>(FindElemByName(root, "textNomad"));

    // Find button elements
    buttonAllow = FindElemByName(root, "buttonAllow");
    buttonDeny = FindElemByName(root, "buttonDeny");

    // Set element IDs on buttons
    buttonAllow->SetElementId(39000);
    buttonDeny->SetElementId(0x9859);
}

// ============================================================
// ImmigrationUI::Slot7 (vtable +0x38) — Update handler
// ============================================================
// Finds ImmigrationComponent, checks build state to determine
// if nomads are available. Enables/disables buttons accordingly,
// shows/hides text, formats immigration count + world name.
//
// Original: 0x14006B130.

// [7] 0x14006b130
void ImmigrationUI::OnUpdateLayout() {
    if (!immigrationui_sa::Ready()) return;
    immigrationui_sa::Trace("Slot7", this);

    // The ImmigrationComponent on the data source (IsA ComponentEntity)
    ImmigrationComponent* immigComp =
        immigrationui_sa::FindImmigration(immigrationui_sa::Entity(this));

    // Standalone safety: retail always finds the ImmigrationComponent on the
    // town-hall entity, so it never guards. In standalone the dialog's entity
    // (+0x28) or the component may be unbound; degrade to the "no nomads" state
    // (buttons disabled, textNone shown) instead of dereferencing NULL.
    if (immigComp == nullptr) {
        if (buttonAllow) buttonAllow->Enable(false);
        if (buttonDeny)  buttonDeny->Enable(false);
        // Show textNone, hide textNomad.
        textNomad->SetFlagsOnOff(1, 0);
        textNone->SetFlagsOnOff(0, 1);
        return;
    }

    // The immigration count: the low dword of +0x60 (the array's count).
    uint32_t immigCount = static_cast<uint32_t>(immigComp->group._size);

    // The StateMachineComponent on the component's entity
    StateMachineComponent* buildState = immigrationui_sa::FindStateMachine(immigComp->_entity.Get());

    // Check build state: byte at +0x52, if negative use +0x50, compare to 0x11.
    // Guard the sub-entity's BuildState too (unbuilt/absent nomad camp in SA).
    bool buildComplete = false;
    if (buildState != nullptr) {
        int8_t state = buildState->stateMachine.pendingState;
        if (state < 0) {
            state = buildState->stateMachine.currentInput;
        }
        buildComplete = (state == 0x11);
    }

    // Enable/disable the buttons (Element slot 8)
    buttonAllow->Enable(buildComplete);
    buttonDeny->Enable(buildComplete);

    // Show/hide text elements based on build state
    // If build complete: show textNomad, hide textNone
    textNomad->SetFlagsOnOff((uint32_t)!buildComplete, (uint32_t)buildComplete);
    textNone->SetFlagsOnOff((uint32_t)buildComplete, (uint32_t)!buildComplete);

    // Format immigration count as "%i" and set on textNomad parameter 0
    System::String countStr = System::String::Format("%i", (int)immigCount);
    textNomad->SetParameter(0, countStr);

    // The save name, as parameter 1
    System::String worldNameStr(GameUpdateLoop::_singleton->m_saveName);
    textNomad->SetParameter(1, worldNameStr);
}

// ============================================================
// ImmigrationUI::Slot9 (vtable +0x48) — Button handler
// ============================================================
// Handles Allow (id=39000) and Deny (id=0x9859) button clicks.
// Gets ImmigrationComponent, finds its entity's StateMachine,
// then posts event 7 (allow) or 8 (deny) to accept/reject nomads.
//
// Original: 0x14006B300.

// [9] 0x14006b300
void ImmigrationUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, int32_t msg, int32_t id,
                                   int32_t /*param5*/, int32_t /*param6*/, int32_t /*e*/, void* /*userData*/) {
    if (!immigrationui_sa::Ready()) return;
    immigrationui_sa::Trace("Slot9", this);

    if (msg != 0x12) return;

    // Determine action mode: 7 = allow, 8 = deny
    int mode;
    if (id == 39000) {
        mode = 7;
    } else if (id == 0x9859) {
        mode = 8;
    } else {
        return;
    }

    // The ImmigrationComponent on the data source (IsA ComponentEntity)
    ImmigrationComponent* immigComp =
        immigrationui_sa::FindImmigration(immigrationui_sa::Entity(this));
    if (immigComp == nullptr) return;   // no component bound (standalone safety)

    // The StateMachineComponent on the component's entity
    StateMachineComponent* buildState = immigrationui_sa::FindStateMachine(immigComp->_entity.Get());
    if (buildState == nullptr) return;

    // The current node's Controller::PostEvent (slot 2) (&SMC[0x48], active byte, mode)
    if (!buildState->stateMachine.current) return;
    int currentState = (int)buildState->stateMachine.activeState;
    ControllerComponent::Controller* node = buildState->stateMachine.current;
    node->PostEvent(&buildState->stateMachine, currentState, mode);
}
