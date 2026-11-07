// src/game/Dialog/followdialog.cpp
// Clean reimplementation of FollowDialog game logic.
//
// FollowDialog (0xD8 bytes) — camera follow dialog.
// Extends DialogInstance (0xB8 base). Fields: FollowDialog.h.
//
// Decompiled from Application-x64.exe.

#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "input/Input.h"   // ENDGAME B.3.7 callers (K3)
#include "Game/inputbindings.h"   // GameKey (the KeyMap binding ids)
#include "Dialog/FollowDialog.h"
#include "Dialog/ui_dialogs.h"
#include "system/Value.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include <cstdint>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Work/Work.h"         // B.3.x W0: Work::GetTarget by name
#include "Dialog/CitizenUI.h"
#include "Component/CitizenComponent.h"
#include "Component/CitizenAIComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/ModelComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/Profession.h"
#include "Game/ProfessionList.h"
#include "Game/PlayerToolManager.h"
#include "Game/PlayerController.h"
#include "Game/WorkScheduler.h"        // WorkScheduler::trees (GameUpdateLoop +0x118)
#include "Game/WorkSchedulerNodes.h"   // WorkSchedulerIdNode

// Standalone profession-list data is not fully live (the CitizenAI +0x288 desc
// backref and the ProfessionList entries can be garbage/-1). Mirror the
// citizenui.cpp CitUI_OkPtr guard so Slot6's profession-text build can't deref a
// bogus pointer and hardfault (the Follow crash). Byte-faithful when these
// pointers are valid (as they are in the live game).
static inline bool FD_OkPtr(const void* ptr) {   // LEFT: void* is a raw address (diagnostic probe), not an object: checks it lies in [0x10000, 0x7FFFFFFFFFF0) before Slot6's profession-text derefs
    const uintptr_t p = reinterpret_cast<uintptr_t>(ptr);   // CAST-OK: ptr_int
    return p >= 0x10000 && p < 0x7FFFFFFFFFF0ULL;
}

// ============================================================
// WorldGrid_FindEntity (0x1400f9a60)
// ============================================================
// BST search by work id in the work scheduler's first id tree (WorkScheduler
// +0x30, trees[0]; the nodes are WorkSchedulerIdNode: key +0x00, work +0x08,
// left +0x18, right +0x20 -- Game/WorkSchedulerNodes.h).
// On match: copies the node's Work into retval (InternalBasePtr).
static Object::InternalPtr<Work>* WorldGrid_FindEntity_Impl(const WorkScheduler* scheduler,
                                                            Object::InternalPtr<Work>* retval, int tileIndex)
{
    const WorkSchedulerIdNode* node = scheduler->trees[0].root;
    while (true) {
        if (node == nullptr) {
            new (retval) ::Object::InternalBasePtr(nullptr);
            return retval;
        }
        if (node->key <= tileIndex) {
            if (tileIndex <= node->key) {
                // Exact match
                if (node != nullptr) {
                    *retval = Object::InternalPtr<Work>();
                    *retval = node->work;
                    return retval;
                }
                new (retval) ::Object::InternalBasePtr(nullptr);
                return retval;
            }
            node = node->right;  // right child
        } else {
            node = node->left;   // left child
        }
    }
}

// ============================================================
// FollowDialog::OnOpened (vtable +0x20) — Setup follow camera UI
// ============================================================
// Finds buttonClose (ID 100), textFollow, textAction.
// Sets textFollow parameter 0 to the followed citizen's name.
// Sets the follow-active flag on the player tool manager (+0x38).
//
// Original: 0x140069340.

void FollowDialog::OnOpened() {
    // Find "buttonClose" and set element ID 100
    {
        System::String nameStr("buttonClose");
        UI::Element* elem = FindElement(nameStr).Get();

        if (elem) {
            elem->SetElementId(100);
        }
    }

    // Find "textFollow", set parameter 0 to the citizen's name
    {
        System::String nameStr("textFollow");
        UI::TextElement* elem = static_cast<UI::TextElement*>(FindElement(nameStr).Get());

        textFollow = elem;

        CitizenComponent* entity = citizen;
        if (entity && elem) {
            elem->SetParameter(0, entity->name);   // +0x28 System::String
        }
    }

    // Find "textAction"
    {
        System::String nameStr("textAction");
        textAction = static_cast<UI::TextElement*>(FindElement(nameStr).Get());
    }

    // Set the follow-active flag: the tool manager's +0x38 byte = 1.
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    PlayerToolManager* subsys = gameWorld ? gameWorld->m_toolManager : nullptr;
    if (subsys) subsys->field_0x38 = true;
}

// ============================================================
// FollowDialog::OnUpdateLayout (vtable +0x30) — profession and action text
// ============================================================
// Called every frame to refresh the follow dialog's text elements:
//   - textFollow: parameter 1 set to current profession description string
//   - textAction: text set to current action string by index
//
// Logic mirrors CitizenUI's profession/action text update block.
//
// Original: 0x140068F70.

void FollowDialog::OnUpdateLayout() {
    CitizenComponent* ai = citizen;
    if (!ai) return;  // guard: followed-entity ref may be uninit/cleared

    // --- Profession text (parameter 1 on textFollow) ---
    // If isChild (age < adultAge) OR hasSchool: show the child / student profession
    // Otherwise: show the current profession
    // isChild: guard the description backref (garbage/-1 in standalone).
    CitizenDescription* descBack = ai->description;
    bool isChild = FD_OkPtr(descBack) &&
                   (ai->age < descBack->adultAge);
    bool hasWorkplace = (ai->school != nullptr);

    {
        Profession* profEntry = nullptr;
        if (isChild || hasWorkplace) {
            // The ProfessionList's child / student entries (retail 0x140068f70:
            // the game's +0xB0, then its +0x40 / +0x48).
            ProfessionList* professions = GameUpdateLoop::_singleton->m_professionList;
            if (FD_OkPtr(professions)) {
                if (ai->school == nullptr) {
                    profEntry = professions->childProfession;
                } else {
                    profEntry = professions->studentProfession;
                }
            }
        } else {
            profEntry = ai->profession;  // current profession
        }

        // UI::String from the profession's string table and name key; its
        // System::String is parameter 1 on textFollow.
        // Guard: the profession (and its string table) can be garbage in standalone
        // where the profession data is not live — deref would hardfault.
        if (FD_OkPtr(profEntry) && FD_OkPtr(profEntry->stringTable) && textFollow) {
            UI::String uiStr(Object::ExternalPtr<UI::StringTable>(profEntry->stringTable),
                             profEntry->stringName);
            System::String sysStr(uiStr.operator System::String());

            textFollow->SetParameter(1, sysStr);
        }
    }

    // --- Action text (textAction, set by index from the element's string table) ---
    // The CitizenAIComponent on the citizen's entity
    CitizenAIComponent* aiComp = ai->_entity->FindComponent<CitizenAIComponent>();

    // The AI state is the action index -- unguarded, as retail (0x140069121).
    int actionIndex = static_cast<int32_t>(aiComp->state);

    // Special case: action Idle (idle/searching for home)
    // Check if citizen's home tile entity has a different profession — if so, show index 0x10
    if (actionIndex == static_cast<int32_t>(CitizenAIState::Idle)) {
        Work* homeRef = nullptr;
        int homeTile = ai->field_0x30;
        if (homeTile >= 0) {
            // Look up the work by id in the work scheduler's id tree
            // (GameUpdateLoop +0x118 is the WorkScheduler).
            WorkScheduler* scheduler = GameUpdateLoop::_singleton->m_tutorial;
            Object::InternalPtr<Work> foundRef;
            WorldGrid_FindEntity_Impl(scheduler, &foundRef, homeTile);
            homeRef = foundRef.Get();
        }
        if (homeRef != nullptr) {
            // The work's target (slot 10) against the citizen's profession
            // The gw+0x118 work tree's node payload is a Work (0x1400f88a0 fills it).
            Profession* descRef = nullptr;
            homeRef->GetTarget(&descRef);   // slot 10
            if (descRef != ai->profession) {
                actionIndex = 0x10;  // profession mismatch — show "looking for work" string
            }
        }
    }

    // UI::String from textAction's own string table, by index
    {
        if (!textAction) return;  // guard: element may be unresolved
        UI::String textVal(Object::ExternalPtr<UI::StringTable>(textAction->_text.table),
                           actionIndex);
        textAction->SetText(&textVal);
    }
}

// ============================================================
// FollowDialog::OnClosed (vtable +0x28) — clear the follow-active flag
// ============================================================
// Clears the follow-active byte on the tool manager (+0x38).
// Mirror of OnOpened which sets it to 1.
//
// Original: 0x140069320 (19 bytes).

void FollowDialog::OnClosed() {
    PlayerToolManager* subsys = GameUpdateLoop::_singleton->m_toolManager;
    subsys->field_0x38 = false;
}

// ============================================================
// FollowDialog::Close (0x140068f00)
// ============================================================
//   1. The owning CitizenUI's followDialog back-reference = null
//   2. Wraps self in an InternalPtr, calls UserInterface::CloseDialog.
void FollowDialog::Close() {
    citizenUI->followDialog = Object::InternalPtr<UI::DialogInstance>(nullptr);

    Object::InternalPtr<UI::DialogInstance> selfIBP(this);
    _ui->CloseDialog(&selfIBP);
}

// ============================================================
// FollowDialog::OnElementEvent (vtable +0x40) — buttonClose click
// ============================================================
// When msg==0x12 (button click) and id==100 (buttonClose): Close().
//
// Original: 0x140069230 (18 bytes).

void FollowDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId param3, int32_t param4, int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (param3 != UI::EventId::Activated || param4 != 100)
        return;

    Close();
}

// ============================================================
// FollowDialog::OnUpdate (vtable +0x38) — follow camera position
// ============================================================
// This is the DialogInstance per-frame self-tick. The Runtime framework calls
// it every frame from UI::DialogInstance::Update (0x180113980):
//     (**(code **)(*(longlong *)this + 0x38))(this);   // idx7 (+0x38) on the dialog
// which is driven by UI::UserInterface::Update (0x1801213C0) iterating each open
// dialog. So idx7 is the correct slot for the camera-follow tick (idx8/+0x40 is
// OnElementEvent, only invoked on element events with msg==0x12).
//
// Reads position from the followed entity's ModelComponent and writes it to the
// camera target. Checks for Escape key to close.
//
// Original: 0x140069250.

void FollowDialog::OnUpdate(const System::Time& /*time*/) {
    CitizenComponent* followed = citizen;
    if (!followed) return;  // guard: followed-entity ref may be uninit/cleared

    // The ModelComponent on the citizen's entity (retail's type-id cell 0x1401c7a50).
    ModelComponent* model = followed->_entity->FindComponent<ModelComponent>();

    // Model position: x at +0x158, z at +0x15C (the transform's position).
    // Matches the exe (0x140069250: MOV RAX,[retbuf] then MOVSS [RAX+0x158]).
    // Only update the camera when the ModelComponent resolved (guard the null).
    if (model) {
        float posX = model->transform.translation.x;
        float posZ = model->transform.translation.y;

        // The camera's look-at target
        GameUpdateLoop* world = GameUpdateLoop::_singleton;
        PlayerController* cameraSys = world ? world->m_playerController : nullptr;
        if (cameraSys) {
            cameraSys->target.x = posX;
            cameraSys->target.y = posZ;
            cameraSys->target.z = 0.0f;
        }
    }

    // Escape key (key ID 8) — if pressed, close the follow dialog (0x140068f00).
    const Input::KeyMap* keyMap = Input::Configuration::_singleton ? Input::Configuration::_singleton->_keyMap : nullptr;
    if (keyMap && keyMap->Pressed(0, ToUnderlying(GameKey::Cancel))) {
        Close();
    }
}

// ============================================================
// FollowDialog: construction and reflection
// ============================================================
// Vtable layout (exe 0x14015be00, 10 slots):
//   +0x00 idx0  ScalarDtor          (0x140011AF0)
//   +0x08 idx1  TypeName            (0x140011A10)
//   +0x10 idx2  Type                (0x140011A00)
//   +0x18 idx3  Persist             (DialogInstance)
//   +0x20 idx4  OnOpened            (0x140069340)
//   +0x28 idx5  OnClosed            (0x140069320)
//   +0x30 idx6  OnUpdateLayout      (0x140068F70)
//   +0x38 idx7  OnUpdate (per-frame)(0x140069250)
//   +0x40 idx8  OnElementEvent      (0x140069230)
//   +0x48 idx9  OnSerializeSettings (DialogInstance)

#include "Object/factory_registration.h"
#include "Component/ModelComponent.h"   // X::TypeId() (B.3.8 callers)

// ENDGAME B.3.8 block DLG: FollowDialog is a real class (Dialog/FollowDialog.h);
// the hand-built table above is gone. Since the CB2 caller batch CitizenUI's
// Follow button (citizenui.cpp) builds the dialog with the real ctor below and
// fills +0xB8/+0xC0 itself; the door + stamp (and its FollowDialog_GetStandalone-
// Vtable getter) are gone. 0x14015be00 stays unregistered; engine_funcs.cpp's
// g_FollowDialog_Vtable has no reader left.

// ENDGAME B.3.9: ModelComponent (cell 0x1401c7a50, the class TypeId_ModelComponent_FC
// is really bound to -- Table 1's authority) has a ported TypeId() now, so the
// EnsureFollowGlobals() lazy-cache is dead.

FollowDialog::FollowDialog()
    : citizen(nullptr), citizenUI(nullptr), textFollow(nullptr), textAction(nullptr) {}

FollowDialog::FollowDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                           Object::InternalPtr<UI::DialogInstance> parent)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0),
      citizen(nullptr), citizenUI(nullptr), textFollow(nullptr), textAction(nullptr) {}

// create 0x140011a20: MemAlloc(0xd8, 8), then the ctor.
::Object::Base* FollowDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FollowDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) FollowDialog());
}
// createInPlace 0x140011a90 (the tree's in-place build is kept, B8-13).
::Object::Base* FollowDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) FollowDialog());
}

// The reflection record stays at the TU position of the UI_DIALOG_STUB row it
// replaces (ui_dialog_stub_factories.cpp, B38 D4).

