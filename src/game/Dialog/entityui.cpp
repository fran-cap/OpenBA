// engine/src/ui/entityui.cpp
// Clean reimplementation of EntityUI panel methods.
//
// EntityUI (0x48 bytes): Entity info panel.
// Layout: +0x28 = data source (entity), +0x30 = entity ptr,
//         +0x38 = checkPin element, +0x40 = pages element

#include "Dialog/ui_panels.h"
#include "Dialog/EntityUI.h"
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/Element.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/DialogInstance.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/UserInterface.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/PageElement.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include "input/Input.h"  // ENDGAME B.3.7 callers (K3)
#include "Object/factory_registration.h"
#include <cstddef>
#include "Component/MapComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/ModelComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/MultiObjectComponent.h"   // X::TypeId() (B.3.8 callers)
#include "Component/UIDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Dialog/ToolUI.h"   // X::TypeId() (B.3.8 callers)
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.8 callers)
#include "Component/BuildDescription.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Player/EditTool.h"   // X::TypeId() (ENDGAME B.3.9)
#include "Dialog/exe_bodies.h"
#include "Player/exe_bodies.h"   // B3R: Avl38SetRemove (0x140025740), Avl38Retrace
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "ui/CheckElement.h"          // B4 DLG-a: EntityUI::checkPin
#include "Component/StateMachineComponent.h"
#include "Component/BuildComponent.h"
#include "Component/ClearAreaComponent.h"
#include "Component/DestroyComponent.h"
#include "Component/ComponentDescription.h"
#include "Game/PlayerController.h"
#include "Game/PlayerToolManager.h"
#include "Game/Map.h"   // GameUpdateLoop::m_game
#include "Dialog/ToolRegistryNode.h"   // the +0x10 registry's node (Startup)
#include "Player/ToolSetting.h"        // the node's InternalPtr<ToolSetting> (complete type)
#include "Player/PathTool.h"
#include "Player/DragPlacementTool.h"
#include "Player/PlacementTool.h"
#include "Player/RoadTool.h"
#include "Player/BridgeTool.h"
#include "Player/RemovalTool.h"
#include "Player/FencedTool.h"
#include "Component/CreatePathDescription.h"
#include "Component/CreateDragDescription.h"
#include "Component/CreatePlacedDescription.h"
#include "Component/CreateRoadDescription.h"
#include "Component/CreateBridgeDescription.h"
#include "Component/CreateRemovalDescription.h"
#include "Component/CreateFencedDescription.h"
#include "Component/UIComponent.h"
#include "application/Application.h"   // Application::IsPaused (Update)
#include "Game/GameSettings.h"          // g_gameSettings->autoPause (Update)
#include "showroom.h"                   // HARNESS: Showroom_Enabled (Update, the paused arm)
#include "ext/toolbar/toolbar.h"         // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTHOSTS.md): RestoreWindow
#include "ext/registry.h"                // Ext::Ext().touch_pointer (the Update seam below)
#include "ext/touch_pointer/touch_pointer.h"   // EXT SEAM (NOT RETAIL, docs/android/P11B_POINTER.md): HoldToolAim

// ============================================================
// GetDescPage (0x140011320)
// ============================================================
// Searches the entity's ComponentDescription sub-descriptions (_subDescs /
// _subDescCount) for the UIDescription (DAT_1401c7f90). Its _valHead (+0x40)
// is the default page index. The null guards are the tree's (retail has none).
static UIDescription* GetDescPage_Impl(const ComponentDescription* entityDesc)
{
    UIDescription* result = nullptr;
    if (!entityDesc) return result;

    int count = entityDesc->_subDescCount;
    Description** compArray = entityDesc->_subDescs;   // COMP-c typed _subDescs (merge fix)

    for (int i = 0; i < count; i++) {
        Object::Base* comp = compArray ? compArray[i] : nullptr;
        if (!comp) continue;

        // vtable[2] = GetTypeId (returns int typeId)
        int typeId = comp->Type();

        if (typeId == (int)UIDescription::TypeId()) {
            if (::Object::IsDerivedFromNullSafe(comp, UIDescription::TypeId())) {
                result = static_cast<UIDescription*>(comp);
            }
            break;
        }
    }

    return result;
}

// ============================================================
// BuildComponent::IsBuildComplete (0x140109390)
// ============================================================
// True once the delivered labour covers the cost: the BuildDescription's +0x30
// (the cost) times costMultiplier (+0x4C) <= laborProgress (+0x48).
// The null-description guard is the tree's; retail derefs the checked cast
// unguarded (`CMOVNZ RDX,RBX ; MOV EDX,[RDX+0x30]`).
bool BuildComponent::IsBuildComplete() const
{
    Description* desc = _description;
    bool isResource = ::Object::IsDerivedFromNullSafe(desc, BuildDescription::TypeId());
    BuildDescription* resourceComp = isResource ? static_cast<BuildDescription*>(desc) : nullptr;

    if (!resourceComp) return false;

    int totalRequired = resourceComp->_i30;
    int multiplier    = costMultiplier;
    int delivered     = laborProgress;

    return totalRequired * multiplier <= delivered;
}

// ============================================================
// EntityUI_SetEditMode (0x140102b60)
// ============================================================
// FORWARDER. Retail 0x140102b60 was ported twice, independently: once here as
// EntityUI_SetEditMode_Impl and once in toolbardialog.cpp as
// Toolbar_ShowGroup_Impl. Same object (gameWorld+0x120), same fields, same
// retail function. The single surviving body is Toolbar_ShowGroup_Impl; this
// name is kept because ~15 call sites use it, and it forwards unchanged.
//
// What this copy had WRONG, and why the other body won:
//   * Its in-order scan did `goto done_search` on the FIRST matching node.
//     Retail's inner component loop breaks, but the OUTER traversal runs to
//     the end of the tree, so the LAST match wins. The surviving body does
//     the full traversal.
// What this copy had RIGHT, and the other body did not:
//   * No early-out on foundTypeId == 0. The registry holds a node keyed 0 ->
//     EditTool, so mode 0 exact-matches it and CREATES the default EditTool.
//     That is the whole reason SetEditMode(pc, 0) installs an EditTool and
//     entity selection works. The other body returned early there; that
//     early-out is deleted, not inherited.
//
// Sets the player controller's active tool. Walks a BST at +0x10
// (tool type registry) to find the matching tool entry for the
// given mode. Creates a new tool controller via Factory::CreateObject
// and optionally a ToolSetting, then activates the tool.
//
// PlayerController layout:
//   +0x10: tool registry BST root (nodes: +0x00=typeId(4), +0x04=pad,
//          +0x08=toolTypeId(4), +0x10=settingPtr(8), +0x18=parent(8),
//          +0x20=left(8), +0x28=right(8))
//   +0x20: current mode (uint64)
//   +0x28: active tool (InternalBasePtr)
//   +0x80: override flag (char)
//   +0x88: override mode (uint64)
//
// BST node layout (0x30 bytes, keyed by typeId at +0x00):
//   +0x00: typeId (uint32_t) - key for BST ordering
//   +0x08: toolTypeId (uint32_t) - passed to Factory::CreateObject
//   +0x10: settingPtr (void*) - cached ToolSetting object
//   +0x18: parent (node*), +0x20: left (node*), +0x28: right (node*)

// SetEditMode is PlayerToolManager::SelectTool (0x140102b60, src/game/Dialog/toolbardialog.cpp).

// ============================================================
// EntityUI_SelectEntity (0x140102e30)
// ============================================================
// Inserts or updates entity in the tracking AVL BST at
// playerController+0x60. Increments count at +0x68 on insert.
//
// BST node (0x38 bytes):
//   +0x00: key (uint64_t, entity InternalBasePtr value)
//   +0x08: x position (int32)
//   +0x0C: y position (int32)
//   +0x10: active flag (int32, set to 1)
//   +0x18: parent (node*)
//   +0x20: left child (node*)
//   +0x28: right child (node*)
//   +0x30: balance factor (int16)
//   +0x32: height (int16)
//
// NOTE: This function is used in both entityui.cpp and toolui.cpp.

// A new node: the key, the position, active, no children, balance 0.
static TrackedEntityNode* NewTrackedEntityNode(ComponentEntity* key, uint32_t x, uint32_t y,
                                               TrackedEntityNode* parent)
{
    TrackedEntityNode* node =
        static_cast<TrackedEntityNode*>(System::MemoryAllocator::MemAlloc(0x38, 8));
    if (node != nullptr) {
        node->key = key;
        node->x = x;
        node->y = y;
        node->active = 1;
        node->_pad11[0] = node->_pad11[1] = node->_pad11[2] = 0;   // the dword store of 1
        node->parent = parent;
        node->left = nullptr;
        node->right = nullptr;
        node->balance = 0;
        node->height = 0;
    }
    return node;
}

void PlayerToolManager::TrackEntity(ComponentEntity* const* entityPtr,
                                 uint32_t x, uint32_t y) {
    PlayerToolManager* manager = this;
    ComponentEntity* entityKey = *entityPtr;
    TrackedEntityNode** rootPtr = &manager->field_0x60.root;

    // Search the tree for an existing node with this key
    TrackedEntityNode* node = *rootPtr;
    while (node != nullptr) {
        if (entityKey < node->key.Get()) {
            node = node->left;
        } else {
            if (entityKey <= node->key.Get()) {
                // Found — update position and active flag
                node->active = 1;
                node->x = x;
                node->y = y;
                return;
            }
            node = node->right;
        }
    }

    // Not found — insert a new node
    if (*rootPtr == nullptr) {
        // Empty tree — the new node is the root
        *rootPtr = NewTrackedEntityNode(entityKey, x, y, nullptr);
    } else {
        // Walk the tree to the insertion point
        TrackedEntityNode* cur = *rootPtr;

        while (true) {
            TrackedEntityNode* parent = cur;

            if (cur->key.Get() <= entityKey) {
                if (entityKey <= cur->key.Get()) {
                    // Duplicate key — already handled above
                    return;
                }
                if (cur->right == nullptr) {
                    // Insert as the right child, then rebalance
                    TrackedEntityNode* newNode = NewTrackedEntityNode(entityKey, x, y, parent);
                    cur->right = newNode;
                    // 0x140025920 (Avl38Retrace, on the typed node)
                    Avl38Retrace<TrackedEntityNode>(rootPtr, &newNode);
                    break;
                }
                cur = cur->right;
            } else {
                if (cur->left == nullptr) {
                    // Insert as the left child, then rebalance
                    TrackedEntityNode* newNode = NewTrackedEntityNode(entityKey, x, y, parent);
                    cur->left = newNode;
                    Avl38Retrace<TrackedEntityNode>(rootPtr, &newNode);   // 0x140025920
                    break;
                }
                cur = cur->left;
            }
        }
    }

    // One more tracked entity (+0x68)
    manager->field_0x60.count = manager->field_0x60.count + 1;
}

// ============================================================
// EditTool_SelectEntity — NEEDS OPUS (0x1400fd3b0)
// ============================================================
// 100+ lines: iterates all dialogs finding ToolUI controllers,
// creates DialogInstance if entity doesn't have one yet.
// Kept as passthrough via fn_EditTool_SelectEntity.

// ENDGAME B.3.9: ComponentEntity / ToolUI / UIDescription / BuildDescription (was
// TypeId_ResourceComponent, itself healed here under the WRONG by-name guess
// "ResourceComponent" -- Table 1's authority for cell 0x1401c7040 is BuildDescription;
// the heal never actually ran, since the pointer is set unconditionally at
// EngineFuncsInit before this ever fires) / ModelComponent / MultiObjectComponent /
// MapComponent / EditTool all have a ported TypeId() now, so the entityui_sa::EnsureGlobals()
// lazy-cache namespace is dead and its call sites below are removed.

// The sibling ToolUI controller of this dialog (DialogInstance::GetController,
// then retail's checked cast). No null check on the dialog: retail has none.
static ToolUI* EntityUI_FindToolUI(UI::DialogInstance* dialog) {
    UI::DialogController* controller = dialog->GetController(ToolUI::TypeId()).Get();

    ToolUI* toolUI = nullptr;
    if (controller) {
        bool isTool = ::Object::IsDerivedFromNullSafe(controller, ToolUI::TypeId());
        if (isTool) toolUI = static_cast<ToolUI*>(controller);
    }
    return toolUI;
}

// Slot4 (0x1400670D0): Startup — find elements, get entity, reset ToolUI flags.
// [4] 0x1400670d0
void EntityUI::OnCreated() {
    if (!SA_DialogEntityAlive(this)) return;

    // Get entity from data source (+0x28), check if it's a ComponentEntity
    Object::Base* dataSource = _dataSource;
    bool isEntity = ::Object::IsDerivedFromNullSafe(dataSource, ComponentEntity::TypeId());
    entity = isEntity ? static_cast<ComponentEntity*>(dataSource) : nullptr;

    // Find "buttonCenter" element and set ID 101 (0x65)
    {
        System::String nameStr("buttonCenter");
        UI::Element* elem = _dialog->FindElement(nameStr).Get();
        elem->SetElementId(0x65);
    }

    // Find "checkPin" element → store at +0x38, set ID 100
    {
        System::String nameStr("checkPin");
        // Retail stores the found element unchecked; it is read as a CheckElement.
        checkPin = static_cast<UI::CheckElement*>(_dialog->FindElement(nameStr).Get());
        checkPin->SetElementId(100);
    }

    // Get dialog controller, check if it's a ToolUI type
    {
        ToolUI* toolPtr = EntityUI_FindToolUI(_dialog);

        // Reset the ToolUI's pin flag (+0x40) and, with it, +0x41
        toolPtr->pinned = 0;
        if (toolPtr->pinned == 0 &&
            toolPtr->dragging != 0) {
            toolPtr->dragging = 0;
        }
    }

    // Find "pages" element → store at +0x40
    {
        System::String nameStr("pages");
        // Retail stores the found element unchecked; it is read as a PageElement.
        pages = static_cast<UI::PageElement*>(_dialog->FindElement(nameStr).Get());
    }
}

// Slot6: Remove entity from world tracking set.
// [6] 0x140067090
void EntityUI::OnClosed() {
    // No entity test: the key is compared by address, never dereferenced, so a
    // dying entity's key leaves the set too.
    // Copy +0x30 (entity InternalBasePtr) to local
    ComponentEntity* entityKey = entity;

    // The tracking set: the tool manager (g_pGameWorldSingleton +0x120) +0x60
    PlayerToolManager* tools = GameUpdateLoop::_singleton->m_toolManager;

    Avl38SetFindRemove(&tools->field_0x60, &entityKey);   // 0x14002c690
}

// ============================================================
// EntityUI::OnUpdateLayout (vtable +0x38) — Page switching logic
// ============================================================
// Determines which page to show based on entity state (build state,
// destroy state, etc.) and sets the PageElement active page.
//
// Original: 0x140066D80.

// [7] 0x140066d80
void EntityUI::OnUpdateLayout() {
    if (!SA_DialogEntityAlive(this)) return;

        // (risky in SA, cosmetic only); the page-selection below is what makes the
        // entity/citizen content actually render, so it always runs.

    // Find components on entity to determine page state. The finders the tree
    // called returned null for a null entity (a standalone guard; retail's
    // FindComponent<T> instances have none): kept.
    ComponentEntity* entityPtr = entity;

    StateMachineComponent* buildStateComp =
        entityPtr ? entityPtr->FindComponent<StateMachineComponent>() : nullptr;   // 0x140003ea0
    BuildComponent* posComp =
        entityPtr ? entityPtr->FindComponent<BuildComponent>() : nullptr;          // 0x14000d6c0
    ClearAreaComponent* smComp =
        entityPtr ? entityPtr->FindComponent<ClearAreaComponent>() : nullptr;      // 0x14000a170
    DestroyComponent* destroyComp =
        entityPtr ? entityPtr->FindComponent<DestroyComponent>() : nullptr;        // 0x140005780

    // Determine page index based on component state
    int pageIdx;

    int8_t state52 = 0, state50 = 0;
    if (buildStateComp == nullptr) {
        pageIdx = -1;
    } else {
        // Check build state: the pending byte +0x52 (if negative) use the input +0x50
        state52 = buildStateComp->stateMachine.pendingState;
        state50 = buildStateComp->stateMachine.currentInput;
        pageIdx = (state52 < 0) ? (int)state50 : (int)state52;
    }

    if (posComp == nullptr && destroyComp == nullptr) {
        if (smComp == nullptr || pageIdx != 0) {
            // Get default page from entity's description
            UIDescription* descResult = GetDescPage_Impl(entity->_description);
            pageIdx = descResult ? descResult->_valHead : 2;
        } else {
            pageIdx = 2;
        }
    } else {
        // Build/destroy in progress — sequential checks matching original flow
        if (pageIdx >= 3 && pageIdx <= 7) {
            pageIdx = 3;
        } else {
            // Check 2: if posComp exists, verify build is complete
            bool gotoPage2 = false;
            if (posComp != nullptr) {
                bool complete = posComp->IsBuildComplete();
                if (!complete) {
                    gotoPage2 = true;
                }
            }
            // Check 3: if not redirected to page 2 and pageIdx != 0, get desc page
            if (!gotoPage2 && pageIdx != 0) {
                UIDescription* descResult = GetDescPage_Impl(entity->_description);

                UI::PageElement* pagesElem = pages;
                // Retail (0x140066d80) assigns the ACTIVE page into the same register it
                // then conditionally overwrites:
                //     iVar6 = GetActivePage(pages);
                //     if (iVar6 != 4) iVar6 = descPage;
                //     SetActivePage(pages, iVar6);
                // i.e. page 4 -- the user page, which is the production-statistics page --
                // is DELIBERATELY preserved across this per-frame pass.
                int activePage = pagesElem ? pagesElem->GetActivePage() : 0;
                pageIdx = activePage;
                if (activePage != 4) {
                    pageIdx = descResult ? descResult->_valHead : 2;
                }
            } else {
                pageIdx = 2;
            }
        }
    }

    UI::PageElement* pagesElemFinal = pages;
    if (pagesElemFinal) {
        pagesElemFinal->SetActivePage(pageIdx);
    }
}

// ============================================================
// EntityUI::UpdateInput — retail's slot 8 (vtable +0x40) body
// ============================================================
// Reads mouse position from KeyMap, scales by the UI scale.
// Gets ToolUI controller, sets +0x4C = 1, syncs the pin check with it.
// On key 5 press: checks if mouse is over this dialog and entity exists,
// then selects the entity in the edit tool (creating one if needed).
//
// Original: 0x140066AD0. Not wired (see EntityUI.h).
void EntityUI::UpdateInput() {
    // Get KeyMap singleton (triple deref from exref: slot → External → KeyMap)
    Input::KeyMap* keyMap = Input::Configuration::_singleton->_keyMap;

    // Read mouse axes and scale by the UI scale (UserInterface +0xDC)
    UI::UserInterface* ui = _dialog->_ui;
    short axisX = keyMap->GetValue(0, 0);
    int mouseX = (int)((float)(int)axisX * ui->_uiScale);

    // Re-read the UI since the CheckPtr pattern accesses +0x18 again
    ui = _dialog->_ui;
    short axisY = keyMap->GetValue(0, 1);
    int mouseY = (int)((float)(int)axisY * ui->_uiScale);

    // Get ToolUI controller from dialog
    ToolUI* toolPtr = EntityUI_FindToolUI(_dialog);

    // Set controller +0x4C = 1 (input active flag)
    toolPtr->pinOnDrag = 1;

    // Sync the pin: compare the controller's +0x40 with checkPin's _checked
    char checkVal = checkPin->_checked;
    char ctrlVal = toolPtr->pinned;

    if (ctrlVal != checkVal) {
        checkPin->_checked = ctrlVal;
    }

    // Check key 5 press (left mouse button / select)
    if (!keyMap->Pressed(0, 5))
        return;

    // Get dialog at mouse position
    UI::UserInterface* userInterface = _dialog->_ui;
    UI::DialogInstance* dialogAtPos = userInterface->GetFirstContainedDialog(mouseX, mouseY).Get();

    // Check if the dialog at mouse position is this dialog, and entity exists
    if (dialogAtPos != _dialog || entity == nullptr)
        return;

    // Get current tool from the tool manager (IBP at +0x28, needs CheckPtr)
    PlayerToolManager* playerController = GameUpdateLoop::_singleton->m_toolManager;
    Tool* currentTool = playerController->currentTool.Get();

    // Check if current tool is EditTool
    bool isEditTool = ::Object::IsDerivedFromNullSafe(currentTool, EditTool::TypeId());
    EditTool* editTool = isEditTool ? static_cast<EditTool*>(currentTool) : nullptr;

    if (editTool == nullptr) {
        // No EditTool active — deselect first, then get new EditTool
        playerController->SelectTool(nullptr);

        // Re-read current tool after deselect (with CheckPtr)
        currentTool = playerController->currentTool.Get();
        isEditTool = ::Object::IsDerivedFromNullSafe(currentTool, EditTool::TypeId());
        editTool = isEditTool ? static_cast<EditTool*>(currentTool) : nullptr;
    }

    // Select the entity in the edit tool
    if (editTool) {
        Object::InternalPtr<ComponentEntity> entityRef(entity);
        editTool->SelectEntity(&entityRef);   // 0x1400fd3b0
    }
}

// ============================================================
// EntityUI::OnElementEvent (vtable +0x48) — Button dispatch
// ============================================================
// Handles button clicks:
//   ID 1 = close entity panel (set edit mode 0)
//   ID 0x65 (101) = center camera on entity
//   ID 100 = pin/unpin entity tracking
//
// Original: 0x140067050.

// [9] 0x140067050
void EntityUI::OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t msg, int32_t id,
                              int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    void* param2 = element;
    if (msg != 0x12) return;
    // B7 LX (row 994 G33): retail 0x140067050's id-1 arm, first and unguarded:
    // `FUN_140102b60(*(g_pGameWorldSingleton + 0x120), 0); return;` -- SelectTool(0)
    // on the tool manager, no test of the world singleton. It returned here before
    // ("close -> handled by ToolUI::Slot9", which deselected the EditTool by hand).
    if (id == 1) {
        GameUpdateLoop::_singleton->m_toolManager->SelectTool(nullptr);   // 0x140102b60
        return;
    }
    if (id != 0x65 && id != 100) return;
    // pin (id 100) needs the ToolUI type-id to fetch the sibling controller; center
    // (id 0x65) tolerates null component type-ids (finders just return "not found").
    if (GameUpdateLoop::_singleton == nullptr) return;
    // Gate the entity read like every other EntityUI slot (4/6/7): this body reads
    // the cached datasource copy (+0x30) for center (0x65) and toggles tracking on
    // it for pin (100). On a just-demolished building's still-open panel that +0x30
    // weak-ref dangles, so a center/pin click would deref freed memory
    // (fn_FindComponent_Character). SA_DialogEntityAlive checks the +0x28
    // datasource (of which +0x30 is a copy) for null / marked-for-removal (0x400).
    if (!SA_DialogEntityAlive(this)) return;
    (void)param2;
    if (msg != 0x12) return;

    PlayerToolManager* playerController = GameUpdateLoop::_singleton->m_toolManager;

    if (id == 0x65) {
        // Center camera on entity (buttonCenter). Faithful to the exe center block
        // (0x140066870..0x140066a8c) — the same position math as
        // PlayerToolManager::CenterOnEntity Section 2:
        //   - ModelComponent present AND no MultiObjectComponent  -> single object
        //     (citizen/animal/small building): use the model's world Transform
        //     translation (ModelComponent+0x148 Transform, pos at +0x158/+0x15C).
        //   - otherwise -> multi-object building: midpoint of the MapComponent grid
        //     footprint's far corner and origin corner, each mapped tile->world via
        //     fn_MapGetTerrainPos, then averaged (DAT_140172114 = 0.5).
        // Writes ONLY the camera focus target (game+0xe0 +0x1c/+0x20/+0x24, z forced
        // 0 as retail does); the camera smoothly slides to it. No re-select / no snap
        // (that is what distinguishes this from Toolbar_CenterOnEntity, which also
        // reopens the target dialog).
        GameUpdateLoop* game = GameUpdateLoop::_singleton;
        // The finders the tree called returned null for a null entity (kept).
        ComponentEntity* entityPtr = entity;

        float focus[2] = {0.0f, 0.0f};
        bool gotPos = false;

        ModelComponent* model = entityPtr ? entityPtr->FindComponent<ModelComponent>() : nullptr;   // 0x140003f60
        if (model != nullptr) {
            MultiObjectComponent* multi =
                entityPtr ? entityPtr->FindComponent<MultiObjectComponent>() : nullptr;          // 0x140004740
            if (multi == nullptr) {
                // The model transform's translation (+0x158 / +0x15c).
                focus[0] = model->transform.translation.x;
                focus[1] = model->transform.translation.y;
                gotPos = true;
            }
        }

        if (!gotPos) {
            MapComponent* build = entityPtr ? entityPtr->FindComponent<MapComponent>() : nullptr;  // 0x140004680
            if (build != nullptr) {
                // The Map; its +0x00 is the width.
                Map* mapGrid = game->m_game;
                int gridSize = mapGrid->size;
                float halfGrid = (float)(gridSize >> 1);

                // Retail reads the footprint with MOVSX (0x140066955..0x140066964).
                int16_t posX = (int16_t)build->tileX0;
                int16_t posY = (int16_t)build->tileY0;
                int16_t extX = (int16_t)build->tileX1;
                int16_t extY = (int16_t)build->tileY1;

                int idx1 = (int)extX + ((int)extY + (int)posY) * gridSize + (int)posX;
                float inXY1[3] = { (float)(idx1 % gridSize) - halfGrid,
                                   (float)(idx1 / gridSize) - halfGrid, 0.0f };
                float pos1[3] = {0.0f, 0.0f, 0.0f};
                mapGrid->GetTerrainPos(pos1, inXY1, '\0', nullptr);   // 0x14009f3e0

                int idx2 = gridSize * (int)posY + (int)posX;
                float inXY2[3] = { (float)(idx2 % gridSize) - halfGrid,
                                   (float)(idx2 / gridSize) - halfGrid, 0.0f };
                float pos2[3] = {0.0f, 0.0f, 0.0f};
                mapGrid->GetTerrainPos(pos2, inXY2, '\0', nullptr);   // 0x14009f3e0

                focus[0] = (pos1[0] + pos2[0]) * 0.5f;
                focus[1] = (pos1[1] + pos2[1]) * 0.5f;
                gotPos = true;
            }
        }

        if (gotPos) {
            PlayerController* cameraTarget = game->m_playerController;
            cameraTarget->target.x = focus[0];
            cameraTarget->target.y = focus[1];
            cameraTarget->target.z = 0.0f;
        }
        return;
    }

    if (id == 100) {
        // Pin/unpin: toggle checkPin state and update entity tracking

        // Get ToolUI controller
        ToolUI* toolPtr = EntityUI_FindToolUI(_dialog);

        // The pin state lives on the checkPin element (+0x38, set in Slot4) and the
        // pin flag on the sibling ToolUI controller (+0x40). Both must exist to pin;
        // in standalone a partially-built dialog could be missing either, so guard
        // before dereferencing them.
        if (toolPtr == nullptr || checkPin == nullptr) return;

        // Read current pin state from checkPin's _checked (+0xD8)
        char pinState = checkPin->_checked;

        // Set ToolUI +0x40 flag to pin state
        toolPtr->pinned = pinState;
        if (pinState == 0 && toolPtr->dragging != 0) {
            toolPtr->dragging = 0;
        }

        // If unpinned, remove entity from the open-entity set (tool manager +0x60):
        // SyncPin 0x1400666b0 `ADD RCX,0x60` (0x1400667de), `CALL 0x14002c690` (0x1400667e2)
        if (checkPin->_checked == 0) {
            ComponentEntity* entityKey = entity;
            PlayerToolManager* worldObj = GameUpdateLoop::_singleton->m_toolManager;
            Avl38SetFindRemove(&worldObj->field_0x60, &entityKey);   // 0x14002c690
            return;
        }

        // If pinned, select entity in edit tool at the dialog's upper-left corner
        uint32_t dialogX = _dialog->_upperLeftX;
        uint32_t dialogY = _dialog->_upperLeftY;
        ComponentEntity* entityKey = entity;
        playerController->TrackEntity(&entityKey, dialogX, dialogY);
        return;
    }
}

// ============================================================
// EntityUI::Shutdown (vtable +0x18) — No-op
// ============================================================

// ============================================================
// STANDALONE promotion — real EntityUI controller vtable
// ============================================================
// EntityUI is the common outer frame controller for every entity info panel
// (center/pin/pages + tracks the selected entity). In standalone it was a StubNop,
// so its Slot4 never ran — which left the controller's entity ref (+0x30) null. The
// EditTool select-repositions the panel beside the clicked entity by reading that
// +0x30 ref, so with the stub it opened top-left. Wiring Slot4 sets +0x30 (fixes
// positioning) and discovers the buttonCenter/checkPin/pages elements.
//
// Vtable offsets (verified from the exe EntityUI vtable @ 0x14015bb90):
//   +0x20 (idx4) Slot4 = Startup/Setup  (sets +0x30 entity ref; find elements)
//   +0x30 (idx6) Slot6 = remove entity from the world tracking set
//   +0x38 (idx7) Slot7 = page switching (driven per-frame via W_OnUpdate on +0x40)
//   +0x48 (idx9) Slot9 = OnElementEvent: buttonCenter (0x65) + checkPin (100) buttons
// Slot9 is wired (W_Slot9) so the panel's "center" and "pin" options work — for every
// entity dialog, since EntityUI is the shared frame controller. It forwards only the
// center/pin ids; the close button (id 1) stays with the sibling ToolUI::Slot9 to
// avoid retail's SetEditMode(0) tool-teardown (see W_Slot9). Slot8 (the real +0x40
// input handler) remains a no-op — its +0x40 slot is reused to drive Slot7 per-frame,
// and clicking entities works via EditTool::OnKey. The panel's close/drag come from
// the ToolUI controller (already wired).





// [8] 0x140066ad0
void EntityUI::OnUpdate(const System::Time& /*time*/) {
    EntityUI::OnUpdateLayout();
}


// create 0x140011270: MemAlloc(0x48, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* EntityUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(EntityUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) EntityUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* EntityUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) EntityUI());
}

DEFINE_EXE_TYPE(EntityUI, "UI::DialogController")

// (original comment header retained below)
// ============================================================
// EntityUI::Shutdown (vtable +0x18) — No-op
// ============================================================

// ============================================================
// PlayerToolManager::Startup (0x140101ee0) and Update (0x1401026a0)
// ============================================================
// Retail's names for these two are EntityUI::Startup / EntityUI::Update (the
// tree's earlier name for the whole manager); `this` is the 0xB0-byte tool
// manager at GameUpdateLoop +0x120.

namespace {

// 0x14002df10 -- the registry node: the key, the Tool's type id and the value's
// ToolSetting (null at every caller), the parent, no children, balance 0.
ToolRegistryNode* NewToolRegistryNode(uint32_t key, uint32_t toolTypeId, ToolRegistryNode* parent) {
    ToolRegistryNode* node = static_cast<ToolRegistryNode*>(System::MemoryAllocator::MemAlloc(0x38, 8));
    if (node != nullptr) {
        node->typeId = key;
        node->factoryTypeId = toolTypeId;
        node->toolSetting = nullptr;
        node->parent = parent;
        node->left = nullptr;
        node->right = nullptr;
        node->balance = 0;   // +0x30 / +0x32: one dword store of 0
        node->height = 0;
    }
    return node;
}

// 0x14002dd10 / 0x14002ddb0 -- the registry's find-or-insert. A new root is
// stored without a retrace; any other new node is linked under its parent and
// the retrace 0x140014b40 starts at that parent. An existing key is left as it
// is. Retail's 0x14002dd10 copies the value record {tool type id,
// InternalPtr<ToolSetting>} and releases its copy and the caller's when they
// are non-null; Startup passes a null setting at all eight calls, so neither
// release runs and the setting is not spelled. The returned iterator is unread.
void InsertToolRegistry(AvlTreeHead<ToolRegistryNode>* registry, uint32_t key, uint32_t toolTypeId) {
    ToolRegistryNode* parent = registry->root;
    if (parent == nullptr) {
        ToolRegistryNode* node = NewToolRegistryNode(key, toolTypeId, nullptr);
        registry->count += 1;
        registry->root = node;
        return;
    }
    for (;;) {
        if (key < parent->typeId) {
            if (parent->left == nullptr) {
                parent->left = NewToolRegistryNode(key, toolTypeId, parent);
                break;
            }
            parent = parent->left;
        } else if (parent->typeId < key) {
            if (parent->right == nullptr) {
                parent->right = NewToolRegistryNode(key, toolTypeId, parent);
                break;
            }
            parent = parent->right;
        } else {
            return;   // the key is registered already
        }
    }
    Avl38Retrace(&registry->root, &parent);   // 0x140014b40
    registry->count += 1;
}

// The in-order walk of a 0x38-byte set, as Startup inlines it twice.
template <class Node>
Node* FirstInOrder(Node* node) {
    if (node == nullptr) return nullptr;
    while (node->left != nullptr) node = node->left;
    return node;
}

template <class Node>
Node* NextInOrder(Node* node) {
    if (node->right != nullptr) return FirstInOrder(node->right);
    Node* parent = node->parent;
    while (parent != nullptr && parent->right == node) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

}  // namespace

// 0x140101ee0 -- called once by GameUpdateLoop::Startup (after the fullscreen
// fade). Registers the eight tools by the sub-description that selects them,
// reopens the pinned utility dialogs (+0x50) and the pinned entity panels
// (+0x60) a loaded save recorded, then installs the EditTool.
void PlayerToolManager::Startup() {
    InsertToolRegistry(&toolRegistry, 0, EditTool::TypeId());
    InsertToolRegistry(&toolRegistry, CreatePathDescription::TypeId(), PathTool::TypeId());
    InsertToolRegistry(&toolRegistry, CreateDragDescription::TypeId(), DragPlacementTool::TypeId());
    InsertToolRegistry(&toolRegistry, CreatePlacedDescription::TypeId(), PlacementTool::TypeId());
    InsertToolRegistry(&toolRegistry, CreateRoadDescription::TypeId(), RoadTool::TypeId());
    InsertToolRegistry(&toolRegistry, CreateBridgeDescription::TypeId(), BridgeTool::TypeId());
    InsertToolRegistry(&toolRegistry, CreateRemovalDescription::TypeId(), RemovalTool::TypeId());
    InsertToolRegistry(&toolRegistry, CreateFencedDescription::TypeId(), FencedTool::TypeId());

    // The pinned utility dialogs (and a tutorial's panel, 0x1401001f0): every
    // node whose byte is set is toggled open. The returned dialog is unread.
    for (PinnedToolNode* node = FirstInOrder(field_0x50.root); node != nullptr; node = NextInOrder(node)) {
        if (node->active)
            ToggleUtilityDialog(node->key);   // 0x140101a80
    }
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTHOSTS.md): the extension window reopens with the save,
    // as the pinned utility windows above do.
    Ext::Toolbar::RestoreWindow(GameUpdateLoop::_singleton->m_userInterface);

    // The pinned entity panels: one EntityUI dialog per tracked entity whose
    // description carries a UIDescription, placed where it was, its pin box
    // checked and synced back into this set.
    UI::UserInterface* ui = GameUpdateLoop::_singleton->m_userInterface;   // +0x98
    for (TrackedEntityNode* node = FirstInOrder(field_0x60.root); node != nullptr; node = NextInOrder(node)) {
        ComponentEntity* entity = node->key.Get();
        UIDescription* uiDesc = entity->_description->FindDescription<UIDescription>();
        if (uiDesc == nullptr) continue;
        UIComponent* uiComp = entity->FindComponent<UIComponent>();

        void* storage = System::MemoryAllocator::MemAlloc(0xB8, 8);
        UI::DialogInstance* dialog = nullptr;
        if (storage != nullptr) {
            int32_t ctrlCount = 0;
            void* ctrlData = nullptr;
            if (uiComp != nullptr) {
                ctrlCount = uiComp->data.GetSize();   // +0x30
                ctrlData = uiComp->data.GetData();    // +0x28
            }
            dialog = ::new (storage) UI::DialogInstance(
                ui, uiDesc->_dialog, Object::InternalPtr<UI::DialogInstance>(),   // parent: null
                entity, ctrlData, ctrlCount);
        }
        UI::DialogController* controller = dialog->GetController(EntityUI::TypeId()).Get();
        EntityUI* entityUI = ::Object::IsDerivedFromNullSafe(controller, EntityUI::TypeId())
                                 ? static_cast<EntityUI*>(controller) : nullptr;
        Object::InternalPtr<UI::DialogInstance> addRef(dialog);
        ui->AddDialog(&addRef);
        dialog->SetUpperLeft(node->x, node->y, false);
        entityUI->checkPin->_checked = 1;   // CheckElement +0xd8
        entityUI->SyncPin();                // 0x1400666b0
    }

    SelectTool(nullptr);   // 0x140102b60 (the tail jump)
}

// 0x1401026a0 -- per frame, from GameUpdateLoop::Update while the UI does not
// pause the game. `inputFlag`: no dialog holds the mouse. Returns the cursor.
int32_t PlayerToolManager::Update(System::Time time, bool inputFlag) {
    if (currentTool == nullptr)
        SelectTool(nullptr);
    // A tutorial step's pending tool replaces an idle EditTool (exact type).
    if (field_0x80 && field_0x88 != nullptr) {
        if (currentTool == nullptr || currentTool.Get()->IsA(EditTool::TypeId()))
            SelectTool(field_0x88);
    }

    if (::Application::Application::_singleton->IsPaused()) {
        if (g_gameSettings->autoPause)   // GameSettings +0x18
            OpenPauseMenu();             // 0x140101ce0
        // HARNESS, not retail (BANISHED_SHOWROOM, src/diag/showroom.h): the application is paused
        // while its window has no focus, and this arm then skips the camera update, so a showroom
        // left in the background kept its camera wherever it was while its shot sequence went on
        // (whole passes of the same frame). Under the showroom only, the camera still follows the
        // harness's pin; no input is read (inputFlag 0). Normal play keeps retail's arm unchanged.
        if (Showroom_Enabled())
            UpdateCamera(0, time._ticks);
        return 1;
    }

    // EXT SEAM (NOT RETAIL, docs/android/P11B_POINTER.md §5): while ext.touch_pointer's finger presses a dialog
    // with a placement / drag tool aiming, the tool's hotkeys are skipped and it updates as if no dialog held the
    // mouse (its cursor ray kept on the aim), so its ghost stays where it was aimed while the cursor is over the
    // dialog for the click. Row off (always under the faithful profile): retail.
    const bool holdAim = Ext::Ext().touch_pointer && Ext::TouchPointer::HoldToolAim(currentTool.Get());
    if (!holdAim)
        UpdateHotkeys(inputFlag);                   // 0x1400ff6e0 (Game/camera_input.cpp)
    UpdateCamera(inputFlag, time._ticks);           // 0x1400feec0
    currentTool.Get()->Update(&time, holdAim || inputFlag);   // slot 7, +0x38
    return currentTool.Get()->GetCursor();          // slot 12, +0x60
}
