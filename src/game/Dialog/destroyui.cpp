// src/game/Dialog/destroyui.cpp
// Clean reimplementation of DestroyUI game logic.
//
// DestroyUI (0x78 bytes) — building destruction/reclaim panel.
// Extends DialogController. Fields +0x30..+0x70 store element pointers:
//   +0x30 progressRemove, +0x38 progressDestruct (InternalPtr<Element>)
//   +0x40 textRemove, +0x48 textUpgrade, +0x50 textDamage, +0x58 textFixing (InternalBasePtr)
//   +0x60 buttonReclaim, +0x68 buttonDestroy, +0x70 buttonRebuild (InternalBasePtr)
//
// Decompiled from Application-x64.exe.

#include "Dialog/ui_panels.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Component/exe_bodies.h"
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "ui/ProgressElement.h"  // ENDGAME B.3.6 UI callers round 2 (K3)
#include "Object/factory_registration.h"
#include "Component/Component.h"
#include "Component/DestroyDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)
#include "Component/DestroyComponent.h"       // B4 DLG-a: by-name reads
#include "Component/StateMachineComponent.h"
#include "Component/StorageComponent.h"
#include "Component/ComponentDescription.h"
#include "Dialog/exe_bodies.h"   // SA_DialogEntityAlive

// ============================================================
// DestroyComponent bodies (retail reads DestroyComponent fields through RCX:
// DestroyComponent members). LEFT file-local functions taking the typed
// component: the class header is COMP-c's.
// ============================================================
//
// The finders the tree called (the generic walk) returned null for a null
// entity (a standalone guard; retail's FindComponent<T> instances have none):
// kept on every find below.
static StateMachineComponent* DestroyUI_FindStateMachine(ComponentEntity* entity) {
    return entity ? entity->FindComponent<StateMachineComponent>() : nullptr;   // 0x140003ea0
}

// The state byte the StateMachine reports: the pending byte (+0x52) unless it is
// negative, then the input byte (+0x50).
static int8_t DestroyUI_StateByte(const StateMachineComponent* buildState) {
    int8_t stateByte = buildState->stateMachine.pendingState;
    if (stateByte < 0) {
        stateByte = buildState->stateMachine.currentInput;
    }
    return stateByte;
}

// ============================================================
// DestroyComponent_CanReclaim (0x14011e690)
// ============================================================
// Returns true if the StateMachine's state byte is 6 (damaged) or 7.
static bool DestroyComponent_CanReclaim_Impl(DestroyComponent* destroyComp)
{
    if (!destroyComp) return false;

    // Find the StateMachineComponent on the owning entity (Component::_entity)
    StateMachineComponent* buildState = DestroyUI_FindStateMachine(destroyComp->_entity.Get());
    if (!buildState) return false;

    int8_t stateByte = DestroyUI_StateByte(buildState);

    if (stateByte != 6) {
        // Check again (second pass from decompile)
        stateByte = DestroyUI_StateByte(buildState);
        if (stateByte != 7) return false;
    }
    return true;
}

// ============================================================
// DestroyComponent_CanRebuild (0x14011e600)
// ============================================================
// Returns true if an upgrade is pending: the component's target description
// (+0x60) is neither the entity's own description nor the DestroyDescription's
// replacement (+0x20).
static bool DestroyComponent_CanRebuild_Impl(DestroyComponent* destroyComp)
{
    if (!destroyComp) return false;

    // Check if the description is the DestroyDescription
    Description* desc = destroyComp->_description;
    bool isType = ::Object::IsDerivedFromNullSafe(desc, DestroyDescription::TypeId());
    DestroyDescription* typedDesc = isType ? static_cast<DestroyDescription*>(desc) : nullptr;

    // Also check separately (decompile does it twice)
    ::Object::IsDerivedFromNullSafe(destroyComp->_description, DestroyDescription::TypeId());

    void* upgradeRef = destroyComp->upgradeTarget;   // COMP-c named +0x60 (merge fix)

    // Get entity
    ComponentEntity* entity = destroyComp->_entity.Get();

    // Return: upgradeRef != the entity's description AND != the replacement
    if (upgradeRef == entity->_description) {
        return false;  // matches entity state
    }
    void* descUpgradeTarget = typedDesc ? typedDesc->_replaceEntity : nullptr;
    if (upgradeRef == descUpgradeTarget) {
        return false;  // matches description's upgrade target
    }
    return true;
}

// ============================================================
// DestroyComponent action helpers (common pattern)
// ============================================================
// fn_FindComponent_BuildState is FUN_140003ea0, the STATEMACHINECOMPONENT finder.
//
// Reclaim/Rebuild/Destroy all POST AN EVENT on the state machine at SMC+0x48:
//   the current node's Controller::PostEvent (slot 2) (&SMC[0x48], current_sub_state, eventId)
// where eventId = 0 (reclaim/rebuild) or 1 (destroy). SMC+0x48 is the Tick context:
// +0x00 is the CURRENT node (StateMachine_Tick re-points it to the deepest active
// node each pass) and +0x51 is its sub-state index — so for a collapsed building
// this lands on the DestroyComponent's node in state 7. The handlers for events 0
// and 1 there are Destroy_State7Event0_Rebuild_d2a0 / Destroy_State7Event1_Remove_d1a0
// in destroycomponent_states.cpp.
//
// Reclaim (14011e400): also calls the component's slot 4 (Startup)
// Rebuild (14011e470): event 0
// Destroy (14011e4d0): event 1

static void DestroyComponent_Reclaim_Impl(DestroyComponent* destroyComp)
{
    if (!destroyComp) return;
    // Find the StateMachineComponent on the entity (param_1[2] = Component::_entity)
    StateMachineComponent* buildState = DestroyUI_FindStateMachine(destroyComp->_entity.Get());

    int8_t stateFrom = buildState->stateMachine.activeState;
    ControllerComponent::Controller* node = buildState->stateMachine.current;
    node->PostEvent(&buildState->stateMachine, (int)stateFrom, 0);

    // Also call the component's slot 4 — the "Reclaim" notification
    destroyComp->Startup();
}

static void DestroyComponent_Rebuild_Impl(DestroyComponent* destroyComp)
{
    if (!destroyComp) return;
    StateMachineComponent* buildState = DestroyUI_FindStateMachine(destroyComp->_entity.Get());

    int8_t stateFrom = buildState->stateMachine.activeState;
    ControllerComponent::Controller* node = buildState->stateMachine.current;
    node->PostEvent(&buildState->stateMachine, (int)stateFrom, 0);
}

static void DestroyComponent_Destroy_Impl(DestroyComponent* destroyComp)
{
    if (!destroyComp) return;
    StateMachineComponent* buildState = DestroyUI_FindStateMachine(destroyComp->_entity.Get());

    int8_t stateFrom = buildState->stateMachine.activeState;
    ControllerComponent::Controller* node = buildState->stateMachine.current;
    node->PostEvent(&buildState->stateMachine, (int)stateFrom, 1);
}

// ============================================================
// 0x14011dfa0 state-machine driver — moved (2026-07-20)
// ============================================================
// The authoritative, disasm-verified port is RemovalCalc_StateMachine_dfa0 in
// src/game/Player/keystone_a_removalcalcpath.cpp, registered for 0x14011dfa0;
// UpgradeUI_OnRemove calls it by name.
//
// StateMachine state bytes (StateMachineComponent +0x50..+0x53):
//   +0x50 previousState, +0x51 currentSubState,
//   +0x52 targetState (-1 = needs processing), +0x53 transitionTarget

// Helper: Find element by name in the dialog's container
static UI::Element* FindElemByName(UI::Element* dialogElem, const char* name) {
    System::String nameStr(name);
    return dialogElem->FindElement(nameStr).Get();
}

// The data source's ComponentEntity (retail's checked cast of +0x28).
static ComponentEntity* DestroyUI_Entity(Object::Base* dataSource) {
    bool isEntity = ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId());
    return isEntity ? static_cast<ComponentEntity*>(dataSource) : nullptr;
}

// ============================================================
// DestroyUI::OnCreated (vtable +0x20) — Setup/refresh
// ============================================================
// Finds all named UI elements in the dialog and stores them in
// the panel's fields. Sets element IDs on the action buttons.
// Checks if the building can be reclaimed and hides the button
// if appropriate.
//
// The SA guard on the data source is SA_DialogEntityAlive (the file's own
// copy, destroyui_sa::DataSourceAlive, was the same three reads; gone).
//
// Original: 0x140066150.

// [4] 0x140066150
void DestroyUI::OnCreated() {
    if (!SA_DialogEntityAlive(this)) return;
    if (!_container)
        return;  // dialog element tree not bound yet
    UI::Element* dialogElem = _container;

    // --- Progress elements (stored unchecked, read as ProgressElements) ---

    // progressRemove → +0x30
    progressRemove = static_cast<UI::ProgressElement*>(FindElemByName(dialogElem, "progressRemove"));
    // Set progress max (SA-guard: retail derefs blindly; skip when the element
    // is missing from the bound dialog)
    if (progressRemove) progressRemove->_maxValue = 1.0f;

    // progressDestruct → +0x38
    progressDestruct = static_cast<UI::ProgressElement*>(FindElemByName(dialogElem, "progressDestruct"));
    if (progressDestruct) progressDestruct->_maxValue = 1.0f;

    // --- Text elements ---
    textRemove  = FindElemByName(dialogElem, "textRemove");
    textUpgrade = FindElemByName(dialogElem, "textUpgrade");
    textDamage  = FindElemByName(dialogElem, "textDamage");
    textFixing  = FindElemByName(dialogElem, "textFixing");

    // --- Get entity for component checks ---
    ComponentEntity* entity = DestroyUI_Entity(_dataSource);

    // --- Button elements (with element IDs) ---

    // buttonReclaim → +0x60, ID = 0x1771
    buttonReclaim = FindElemByName(dialogElem, "buttonReclaim");
    buttonReclaim->SetElementId(0x1771);

    // buttonDestroy → +0x68, ID = 0x1773
    buttonDestroy = FindElemByName(dialogElem, "buttonDestroy");
    buttonDestroy->SetElementId(0x1773);

    // buttonRebuild → +0x70, ID = 0x1772
    buttonRebuild = FindElemByName(dialogElem, "buttonRebuild");
    buttonRebuild->SetElementId(0x1772);

    // --- Check if reclaim should be hidden ---
    // Find DestroyComponent on entity
    DestroyComponent* destroyComp = entity ? entity->FindComponent<DestroyComponent>() : nullptr;   // 0x140005780

    bool canReclaim = DestroyComponent_CanReclaim_Impl(destroyComp);
    // SA-guard: entity may have no DestroyComponent in standalone
    bool compFlag = destroyComp && destroyComp->burning;   // COMP-c named +0x50 (merge fix)

    if (canReclaim || compFlag) {
        // Hide the reclaim button (SetFlagsOnOff: set flag 1 = hidden)
        buttonReclaim->SetFlagsOnOff(1, 0);
    }
}

// ============================================================
// DestroyUI::OnUpdateLayout (vtable +0x38) — Update handler
// ============================================================
// Complex visibility state machine for the destroy/reclaim panel.
// Two main branches depending on whether the building can be reclaimed.
//
// CanReclaim == true:  Show rebuild/destroy buttons, hide progress bars,
//                      enable/disable buttons based on build state.
// CanReclaim == false: Show reclaim button, update storage/destruction
//                      progress bars, show appropriate status text.
//
// Original: 0x140065B00.

// [7] 0x140065b00
void DestroyUI::OnUpdateLayout() {
    if (!SA_DialogEntityAlive(this)) return;
    // Get entity
    ComponentEntity* entity = DestroyUI_Entity(_dataSource);

    // Find StorageComponent and Destroy component. (Ghidra-verified: retail
    // 0x140065b00 calls FUN_140006270, the StorageComponent finder.)
    StorageComponent* smComp = entity ? entity->FindComponent<StorageComponent>() : nullptr;       // 0x140006270
    DestroyComponent* destroyComp = entity ? entity->FindComponent<DestroyComponent>() : nullptr;  // 0x140005780
    if (!destroyComp) return;  // SA-guard: +0x54/+0x58 are read blindly below

    // Check if building can be reclaimed
    bool canReclaim = DestroyComponent_CanReclaim_Impl(destroyComp);

    // Guard: if critical UI elements aren't initialized yet, bail out
    if (!progressRemove || !progressDestruct || !buttonReclaim || !buttonDestroy) return;

    // The last element to be hidden at function exit
    UI::Element* hideAtEnd = nullptr;

    if (!canReclaim) {
        // --- Building is being demolished: show reclaim, update progress ---

        // Hide rebuild, destroy buttons and textDamage
        if (buttonRebuild) buttonRebuild->SetFlagsOnOff(1, 0);
        buttonDestroy->SetFlagsOnOff(1, 0);
        if (textDamage) textDamage->SetFlagsOnOff(1, 0);

        // Show reclaim button
        buttonReclaim->SetFlagsOnOff(0, 1);

        // --- Update storage removal progress bar ---
        if (smComp != nullptr) {
            int capacity = smComp->capacity;
            int count = smComp->QueryCount();

            float fillRatio;
            if (capacity < 1) {
                fillRatio = 0.0f;
            } else {
                fillRatio = (float)count / (float)capacity;
            }

            float fillPercent = fillRatio * 100.0f + 0.5f;

            // Set progress bar value
            progressRemove->_currentValue = fillRatio;

            // Format and set percentage text
            System::String strObj = System::String::Format("%i", (uint64_t)(uint32_t)(int)fillPercent);
            progressRemove->SetProgressTextParameter(0, strObj);
        }

        // --- Update destruction progress bar ---
        int destroyDone = destroyComp->progress;   // COMP-c named +0x54 (merge fix)
        // LEFT: DestroyComponent +0x58 is declared void* in COMP-c's header; the
        // demolition target is its low dword.
        int destroyTotal = destroyComp->progressTarget;   // COMP-c named +0x58 (merge fix)

        float destroyRatio;
        if (destroyTotal == 0) {
            destroyRatio = 0.0f;
        } else {
            destroyRatio = (float)destroyDone / (float)destroyTotal;
        }

        float destroyPercent = destroyRatio * 100.0f + 0.5f;

        progressDestruct->_currentValue = destroyRatio;

        System::String strObj2 = System::String::Format("%i", (uint64_t)(uint32_t)(int)destroyPercent);
        progressDestruct->SetProgressTextParameter(0, strObj2);

        // --- Show/hide progress bars based on destruction state ---
        bool isDestroying = (destroyRatio != 0.0f);
        if (isDestroying) {
            progressRemove->SetFlagsOnOff(1, 0);   // hide storage bar
        } else {
            progressRemove->SetFlagsOnOff(0, 1);   // show storage bar
        }
        progressDestruct->SetFlagsOnOff(
                                 isDestroying ? 0 : 1,
                                 isDestroying ? 1 : 0);

        // --- Text visibility based on component state ---
        // The component's target description against the entity's own
        if (destroyComp->upgradeTarget == destroyComp->_entity->_description) {
            // Building state matches target → show "fixing" text
            if (textFixing) textFixing->SetFlagsOnOff(0, 1);
            if (textRemove) textRemove->SetFlagsOnOff(1, 0);
            hideAtEnd = textUpgrade;
        } else {
            // Check if building can be rebuilt (upgraded)
            bool canRebuild = DestroyComponent_CanRebuild_Impl(destroyComp);
            if (!canRebuild) {
                // Regular removal: show textRemove
                if (textUpgrade) textUpgrade->SetFlagsOnOff(1, 0);
                if (textRemove) textRemove->SetFlagsOnOff(0, 1);
            } else {
                // Can rebuild: show textUpgrade
                if (textRemove) textRemove->SetFlagsOnOff(1, 0);
                if (textUpgrade) textUpgrade->SetFlagsOnOff(0, 1);
            }
            hideAtEnd = textFixing;
        }
    } else {
        // --- Building can be reclaimed: show rebuild/destroy buttons ---

        // FUN_140003ea0 finds the StateMachineComponent on the entity
        ComponentEntity* compEntity = destroyComp->_entity.Get();
        StateMachineComponent* buildState = DestroyUI_FindStateMachine(compEntity);

        // Read state byte: prefer +0x52, fall back to +0x50 if negative
        int8_t stateByte = DestroyUI_StateByte(buildState);

        // Element slot 8 (Enable) on buttonRebuild: enabled when state != 6
        buttonRebuild->Enable(stateByte != 6);

        // Retail finds the StateMachineComponent again for buttonDestroy
        // (the result is not read)
        (void)DestroyUI_FindStateMachine(compEntity);
        buttonDestroy->Enable(true);

        // Show rebuild and destroy buttons
        buttonRebuild->SetFlagsOnOff(0, 1);
        buttonDestroy->SetFlagsOnOff(0, 1);

        // Show textDamage
        if (textDamage) textDamage->SetFlagsOnOff(0, 1);

        // Hide everything else
        if (textFixing) textFixing->SetFlagsOnOff(1, 0);
        if (textRemove) textRemove->SetFlagsOnOff(1, 0);
        if (textUpgrade) textUpgrade->SetFlagsOnOff(1, 0);
        progressRemove->SetFlagsOnOff(1, 0);
        progressDestruct->SetFlagsOnOff(1, 0);

        hideAtEnd = buttonReclaim;
    }

    // Hide the last element (buttonReclaim when reclaim active, or a text element)
    if (hideAtEnd) hideAtEnd->SetFlagsOnOff(1, 0);
}

// ============================================================
// DestroyUI::OnElementEvent (vtable +0x48) — Notification handler
// ============================================================
// Handles button press notifications. Dispatches to reclaim,
// rebuild, or destroy actions on the DestroyComponent based
// on the button element ID.
//
// Original: 0x140066080.

// [9] 0x140066080
void DestroyUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, int32_t param3, int32_t param4,
                               int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (!SA_DialogEntityAlive(this)) return;
    if (param3 != 0x12) return;

    // Get entity
    ComponentEntity* entity = DestroyUI_Entity(_dataSource);

    // Find DestroyComponent on entity
    DestroyComponent* destroyComp = entity ? entity->FindComponent<DestroyComponent>() : nullptr;
    if (!destroyComp) return;  // SA-guard

    // Dispatch based on button ID
    if (param4 == 0x1771) {
        DestroyComponent_Reclaim_Impl(destroyComp);
    } else if (param4 == 0x1772) {
        DestroyComponent_Rebuild_Impl(destroyComp);
    } else if (param4 == 0x1773) {
        DestroyComponent_Destroy_Impl(destroyComp);
    }
}

// ============================================================
// Standalone promotion — real DestroyUI vtable (2026-07-20)
// ============================================================
// Retail vtable @ 0x14015bb00 (11 slots): overrides 0,1,2,4,7,9; slots
// 3,5,6,8,10 are inherited UI::DialogController base thunks in the exe. The
// visibility state machine (OnUpdateLayout, slot 7) runs every frame through
// DialogInstance::OnUpdateLayout, as in retail.


// create 0x140011130: MemAlloc(0x78, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* DestroyUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(DestroyUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) DestroyUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* DestroyUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) DestroyUI());
}

DEFINE_EXE_TYPE(DestroyUI, "UI::DialogController")
