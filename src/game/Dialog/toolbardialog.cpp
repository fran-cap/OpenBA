// src/game/Dialog/toolbardialog.cpp
// Clean reimplementation of ToolbarDialog game logic.
//
// ToolbarDialog (0x108 bytes) — main gameplay toolbar controller.
// Extends DialogInstance (0xB8 base).
//
// Field layout:
//   +0xB8: InternalBasePtr radioElement (toolbar radio element)
//   +0xC0: void* buttonElements (dynamic array of Element* ptrs, parallel to items)
//   +0xC8: int32 buttonCount (count for +0xC0 array)
//   +0xD0: void* toolbarItems (dynamic array of ToolbarItem* ptrs)
//   +0xD8: int32 itemCount (count for +0xD0 array)
//   +0xE0: uint64 toolbarGroupId (which toolbar group this dialog shows)
//   +0xE8: InternalBasePtr subDialog (child toolbar dialog instance)
//   +0xF0: int32 openSubIndex (-1 = none)
//   +0xF4: int32 currentSpeedIndex
//   +0xF8: void* speedValues (int32[] array)
//   +0x100: int32 speedValueCount
//
// Toolbar item fields (accessed via pointer from +0xD0 array):
//   +0x48 (int32): action type enum
//     0=Tool, 1=ShowGroup, 2=Utility, 3=ToggleAll, 4=FocusEntity,
//     5=CenterOnPoint, 6=SpeedDown, 7=Pause, 8=Unpause, 9=SpeedUp,
//     10=SpeedDisplay
//   +0x4C (int32): sub-parameter (e.g., point index for CenterOnPoint)
//   +0x58 (int64): action data (group ID, entity type, etc.)
//
// Decompiled from Application-x64.exe.

#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ImageElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/TableElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/RadioElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ElementDescription.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/MainMenuDialog.h"
#include "Dialog/ToolbarDialog.h"
#include "Dialog/ToolRegistryNode.h"
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/SpriteSheet.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Dialog.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/ui_dialogs.h"
#include "graphics/Camera.h"   // ENDGAME B.3.5 closing pass -- Graphics::Camera
#include "math/Math.h"
#include "system/Value.h"
#include "ui/DialogInstance.h"
#include "ui/CheckElement.h"
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/PlayerToolManager.h"
// EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): the extension window's toolbar button
#include "ext/toolbar/toolbar.h"
#include "ext/dynamic_professions/dynamic_professions.h"
#include "Game/PlayerController.h"
#include "Game/Map.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Component/ModelComponent.h"
#include "Component/MultiObjectComponent.h"
#include "Component/MapComponent.h"
#include "Player/EditTool.h"   // ENDGAME B.3.9 TR
#include "Player/Tool.h"       // B.3.x W0: Tool::OnCancel by name
#include "ui/DialogController.h"   // B.3.x W0: DialogController::OnCreated / OnOpened by name
#include "Dialog/EntityUI.h"   // ENDGAME B.3.9 TR
#include "system/String.h"
#include "system/Filename.h"
#include "Object/factory_registration.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/TypeInfo.h"   // ENDGAME B.3.3 step 6C
#include "Player/Tool.h"
#include "Player/ToolSetting.h"   // ToolSetting::TypeId() (ENDGAME B.3.9 RF)
#include "Component/UIDescription.h"   // X::TypeId() (B.3.8 callers)
#include "Component/ToolbarDescription.h"   // ToolbarDescription::TypeId() (ENDGAME B.3.9 TR)
#include "Game/Toolbar.h"                  // Toolbar::TypeId() (ENDGAME B.3.9 TR)
#include "ui/ButtonElement.h"              // UI::ButtonElement::TypeId() (ENDGAME B.3.9 TR)
#include "ui/CheckElement.h"               // UI::CheckElement::TypeId() (ENDGAME B.3.9 TR)
#include "video/Device.h"   // ENDGAME B.3.9 SS2: Video::Device::_singleton
#include "Dialog/exe_bodies.h"
#include "Player/exe_bodies.h"   // B3R: PlayerTrade_EntityUI_66b0
#include "system/MemoryAllocator.h"   // B3R: the allocator doors


#ifdef _DEBUG
// Debug sprite logging wrappers (src/engine/ui/)
#include "ui/sprite_debug.h"   // Debug_Sprite_Ctor
extern bool Debug_Sprite_IsValid(const UI::Sprite* sprite);
extern void Debug_ImageElement_SetSprite(UI::ImageElement* self, const UI::Sprite* sprite);
#endif

// ============================================================
// Static inline implementations (replaces fn_* exe pointer calls)
// ============================================================

// ToolbarDialog_CloseSubDialog (FUN_1400b9730)
// Closes the active sub-dialog if present, resets radio selection.
// self+0xE8 = InternalBasePtr to sub-dialog, self+0xF0 = open sub index, self+0xB8 = radio.
void ToolbarDialog::CloseSubDialog() {
    if (subDialog.Get() != nullptr) {
        _ui->CloseDialog(&subDialog);
        // Null out the sub-dialog ptr
        subDialog = Object::InternalPtr<UI::DialogInstance>(nullptr);
        // Reset sub-dialog index to -1
        openSubIndex = -1;
        // Reset radio selection
        radioElement->SetActiveRadio(-1);
    }
}

// ToolbarDialog::ChangeSpeed (FUN_1400b91b0)
// Changes game speed by direction (+1 or -1): the tool manager's +0x34 steps
// through speedValues, clamped to its ends.
void ToolbarDialog::ChangeSpeed(int direction) {
    PlayerToolManager* toolManager = GameUpdateLoop::_singleton->m_toolManager;
    int32_t speedVal = toolManager->field_0x34;
    int32_t count = speedValues.GetSize();
    // Find current speed in array
    int32_t idx = 0;
    for (int i = 0; i < count; i++) {
        if (speedValues[i] == speedVal) { idx = i; break; }
    }
    // Apply direction, clamp to [0, count-1]
    idx += direction;
    if (idx < 0) idx = 0;
    currentSpeedIndex = idx;
    if (count <= idx) {
        currentSpeedIndex = count - 1;
    }
    // Write new speed value
    toolManager->field_0x34 = speedValues[currentSpeedIndex];
}

// The town's per-template entity tree is CitizenBehavior +0x160: its node is
// TemplateBucket (Entity/CitizenBehavior.h, 0x38 bytes: the template, the entities
// built from it, the AVL links).

// The +0x160 node of `key`, or null.
static TemplateBucket* Toolbar_FindTemplateNode(const CitizenBehavior* town, const void* key) {
    TemplateBucket* node = town->field_0x160._root;
    while (node != nullptr) {
        const void* nodeKey = node->element.key;
        if (nodeKey > key) {
            node = node->left;
        } else if (nodeKey < key) {
            node = node->right;
        } else {
            return node;
        }
    }
    return nullptr;
}

// The town's count of a template: the size of its +0x160 node's entity list (0 with no node). The toolbar's
// ShowGroup and FocusEntity buttons test it, and so does the Find Buildings page (ext.building_finder).
int32_t Toolbar_BuiltCount(const CitizenBehavior* town, const void* tmpl) {
    const TemplateBucket* node = Toolbar_FindTemplateNode(town, tmpl);
    return node != nullptr ? node->element.entities._size : 0;
}

// Toolbar_FindEntity (FUN_140088d40)
// BST search: walks the town's +0x160 tree for the template, returns its
// index-th entity, or null.
static ComponentEntity* Toolbar_FindEntity_Impl(const CitizenBehavior* town, const void* key, int index) {
    TemplateBucket* node = Toolbar_FindTemplateNode(town, key);
    if (node != nullptr) {
        return node->element.entities._data[index].Get();
    }
    return nullptr;
}

// ============================================================
// ToolbarDialog::Startup (vtable +0x20, exe 0x1400b97c0): RET 0 (3 bytes)
// ============================================================
// No-op startup; all initialization happens in the constructor (Ctor_Impl).
void ToolbarDialog::OnOpened() {}

// ============================================================
// ToolbarDialog::Shutdown (vtable +0x28, exe 0x1400b97b0): JMP rel to FUN_1400b9730
// ============================================================
// Thunk to CloseSubDialog: closes any open sub-toolbar dialog.
void ToolbarDialog::OnClosed() {
    CloseSubDialog();
}

// ============================================================
// ToolbarDialog_UpdateButtons_Impl (was fn_ToolbarDialog_UpdateButtons, FUN_1400b8fd0)
//
// Secondary update for ShowGroup (case 1) and FocusEntity (case 4) buttons.
// For each toolbar item:
//   - ShowGroup: enables button if the group has entities in the entity list BST
//     (checks item+0x58 type against entity count in BST at entityList+0x160)
//   - FocusEntity: enables button if entities of that type exist
// Uses the same entity BST search as Toolbar_FindEntity_Impl.
// ============================================================
void ToolbarDialog::UpdateButtons() {
    const CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;

    int32_t itemCount = toolbarItems.GetSize();
    if (itemCount <= 0) return;

    for (int i = 0; i < itemCount; i++) {
        const Toolbar* item = toolbarItems[i];
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): the extension button
        // has no Toolbar item
        if (item == nullptr) {
            continue;
        }
        // The item's template
        ComponentDescription* typeId = item->_field58;

        if (typeId == nullptr) continue;

        // Find the ToolbarDescription among the template's sub-descriptions.
        ToolbarDescription* matchedComponent = nullptr;
        int32_t subCount = typeId->_subDescCount;
        if (subCount > 0) {
            uint32_t targetType = ToolbarDescription::TypeId();
            for (int j = 0; j < subCount; j++) {
                Description* subItem = typeId->_subDescs[j];
                if (!subItem) continue;
                uint32_t subType = subItem->Type();
                if (subType == targetType) {
                    bool isDerived = ::Object::IsDerivedFromNullSafe(subItem, targetType);
                    if (isDerived) matchedComponent = static_cast<ToolbarDescription*>(subItem);
                    break;
                }
            }
        }

        int32_t actionType = item->_field48;

        if (actionType == 1) {
            // ShowGroup: enabled while the town has fewer of the template than
            // its ToolbarDescription's limit (_val0; negative = no limit)
            bool enabled;
            if (matchedComponent == nullptr || matchedComponent->_val0 < 0) {
                enabled = true;
            } else {
                enabled = Toolbar_BuiltCount(town, typeId) < matchedComponent->_val0;
            }

            // Set button enabled (Element slot 8)
            UI::Element* elem = buttonElements[i];
            if (!elem) continue;
            elem->Enable(enabled);
        }
        else if (actionType == 4) {
            // FocusEntity: check if any entities of that type exist
            UI::Element* elem = buttonElements[i];
            if (!elem) continue;

            bool enabled = Toolbar_BuiltCount(town, typeId) > 0;
            elem->Enable(enabled);
        }
    }
}

// ============================================================
// PlayerToolManager::SelectTool (FUN_140102b60; the tree's Toolbar_ShowGroup_Impl /
// EntityUI_SetEditMode_Impl, both B4 FORWARD forwarders now)
//
// Makes the tool of a tool group (a template: the toolbar item's +0x58) the
// current tool. Called by the toolbar's ShowGroup buttons, EntityUI, the tutorial,
// NomadComponent and GameUpdateLoop.
//
// Logic:
//   1. If the tutorial lock (+0x80) is set and group != 0, use the locked group (+0x88)
//   2. If same group already active AND a tool exists AND group != 0: early return
//   3. Otherwise: store the new group, shut down and delete the current tool, find
//      the matching tool node in the registry, create the Tool + its ToolSetting,
//      configure & start it
//
// The tool registry (+0x10): an AVL tree of ToolRegistryNode (0x38 bytes), keyed
// by the type id of a sub-description a group template carries.
//
// group == 0 IS A REAL LOOKUP, NOT A "CLEAR" NO-OP.
// The tool registry always contains a node keyed 0 -> EditTool (see
// BuildToolModeTree in tool_activation_standalone.cpp). With group 0 the
// component scan finds nothing, foundTypeId stays 0, and the exact-match pass
// below then hits that key-0 node and CREATES the EditTool. That is how the
// default EditTool gets installed at all — retail EntityUI::Update relies on it
// via SetEditMode(pc, 0). Do not add an early-out on foundTypeId == 0.
// ============================================================

// The tool registry node: Dialog/ToolRegistryNode.h.

void PlayerToolManager::SelectTool(ComponentDescription* group) {
    // Step 1: If the lock is set and the group is non-zero, override with the
    // locked group (the tutorial's TutorialSetupRestrictPlayer restriction —
    // +0x80/+0x88 are written by TutorialSetupRestrictPlayer_Slot8. A step whose
    // setup list contains `disableSelect` locks +0x88 to NULL, which turns EVERY
    // toolbar selection into "no group".)
    if (field_0x80 != 0 && group != nullptr) {
        group = field_0x88;
    }

    // Step 2: Early return if same group is already active with a live tool
    if (field_0x20 == group && currentTool != nullptr && group != nullptr) {
        return;
    }

    // Step 3: Store new group, shut down and delete the current tool if any
    field_0x20 = group;
    if (currentTool != nullptr) {
        currentTool->Shutdown();   // slot 5
        // InternalBasePtr::DeletePtr: the scalar deleting dtor, then null
        delete currentTool.Get();
        currentTool = nullptr;
    }

    // The registry root
    ToolRegistryNode* const root = toolRegistry.root;

    // Step 4: In-order traversal to find the registry node whose typeId matches a
    // sub-description of the group. The first match sets foundTypeId.
    uint32_t foundTypeId = 0;

    // Navigate to the in-order minimum (leftmost node)
    ToolRegistryNode* node = root;
    if (node != nullptr) {
        // Go to leftmost
        while (node->left != nullptr) {
            node = node->left;
        }

        // In-order traversal
        while (node != nullptr) {
            // Check if this node's typeId matches a sub-item in the group data
            ComponentDescription* groupData = field_0x20;
            if (groupData != nullptr) {
                uint32_t nodeTypeId = node->typeId;
                int32_t subCount = groupData->_subDescCount;
                for (int i = 0; i < subCount; i++) {
                    Description* subItem = groupData->_subDescs[i];
                    uint32_t subTypeId = subItem->Type();
                    if (subTypeId == nodeTypeId) {
                        // Verify sub-item pointer is non-null (redundant but matches original)
                        if (groupData->_subDescs[i] != nullptr) {
                            foundTypeId = nodeTypeId;
                        }
                        break;
                    }
                }
            }

            // In-order successor: if right child exists, go to its leftmost descendant
            ToolRegistryNode* right = node->right;
            if (right != nullptr) {
                node = right;
                while (node->left != nullptr) {
                    node = node->left;
                }
            } else {
                // Walk up to find ancestor where we came from the left
                ToolRegistryNode* parent = node->parent;
                if (parent != nullptr) {
                    if (parent->left == node) {
                        // We are the left child — parent is successor
                        node = parent;
                    } else {
                        // We are the right child — walk up
                        while (parent != nullptr && parent->right == node) {
                            node = parent;
                            parent = parent->parent;
                        }
                        node = node->parent;
                    }
                } else {
                    // No parent — we were at root, done
                    node = nullptr;
                }
            }
        }
    }

    // Step 5: search the registry for foundTypeId. Retail is exact-match-or-NULL.
    ToolRegistryNode* foundNode = nullptr;
    ToolRegistryNode* searchNode = root;
    while (searchNode != nullptr) {
        uint32_t key = searchNode->typeId;
        if (foundTypeId < key) {
            searchNode = searchNode->left;
        } else if (foundTypeId > key) {
            searchNode = searchNode->right;
        } else {
            foundNode = searchNode;
            break;
        }
    }

    // DELIBERATE DIVERGENCE FROM RETAIL (a guard is legitimate only where our
    // standalone genuinely differs from retail). Retail does NOT test the search
    // result — it goes straight to Factory::CreateObject and null-derefs if the
    // registry held no matching key, because retail's registry always does. Ours is
    // built by BuildToolModeTree and can legitimately be empty or partial in a
    // standalone run, so guard the deref rather than fault.
    if (foundNode == nullptr) {
        return;
    }

    // Step 6: Create the Tool via Factory::CreateObject
    // Retail: **import slot 0x140152760 = Object::Factory::_singleton (the class static).
    Object::Base* newObj = ::Object::Factory::_singleton->CreateObject(foundNode->factoryTypeId);
    // IsDerivedFrom check for the Tool type
    bool isTool = ::Object::IsDerivedFromNullSafe(newObj, Tool::TypeId());   // cell 0x1401c6eb8
    currentTool = isTool ? static_cast<Tool*>(newObj) : nullptr;

    // Step 7: Create the ToolSetting if needed (lazy init on the registry node)
    if (foundNode->toolSetting == nullptr) {
        // Ask the tool for its setting type (slot 11, retslot shape)
        Object::Type settingBuf(0);
        const Object::Type* settingTypePtr = currentTool->GetSettingType(&settingBuf);

        if (settingTypePtr->_type != 0) {
            // Re-query (matches original's double-call pattern)
            uint32_t settingFactoryId = currentTool->GetSettingType(&settingBuf)->_type;

            // Create the ToolSetting object
            Object::Base* settingObj = ::Object::Factory::_singleton->CreateObject(settingFactoryId);
            bool isToolSetting = ::Object::IsDerivedFromNullSafe(settingObj, ToolSetting::TypeId());   // cell 0x1401c6ef0
            foundNode->toolSetting = isToolSetting ? static_cast<ToolSetting*>(settingObj) : nullptr;
        }
    }

    // Step 8: Configure and start the tool
    ToolSetting* toolSettingRef = foundNode->toolSetting.Get();
    Tool* tool = currentTool.Get();
    tool->dialogPtr = this;                 // back-pointer to the manager
    tool->descriptionPtr = field_0x20;      // the group
    tool->toolSetting = toolSettingRef;     // ToolSetting ref

    // Start the tool (slot 4)
    currentTool->Startup();
}

// B4 X6: the forwarder Toolbar_ShowGroup_Impl(void*, int64_t) is deleted; its two
// callers (PlayerToolManager::OnEntityRemoved, GameUpdateLoop) call SelectTool(nullptr).

// ============================================================
// PlayerToolManager::ToggleUtilityDialog (FUN_140101a80; the tree's
// Toolbar_Utility_Impl)
//
// Opens/closes a utility dialog (e.g., the map, the town hall panels) for a
// template (`description`, the toolbar item's +0x58).
//
// The template has a UIDescription among its sub-descriptions, whose +0x28 is
// the dialog resource. The +0x50 set (PinnedToolNode, keyed by the
// template's address) keeps each utility dialog's position and open flag.
//
// Flow:
//   1. FindDialogByControllerType — search open dialogs for one whose ToolUI
//      shows `description`
//   2. If found: close the dialog, clear the set's open flag
//   3. If not found: find the UIDescription, create the DialogInstance, look
//      up / insert the set node, position it, add it to the UI
// ============================================================
UI::DialogInstance* PlayerToolManager::ToggleUtilityDialog(ComponentDescription* description) {
    // The UserInterface
    UI::UserInterface* userInterface = GameUpdateLoop::_singleton->m_userInterface;

    // The set's key: the template's address
    const void* key = description;

    // Step 1: Check if a dialog with this controller type is already open
    UI::DialogInstance* existingDialog = FindUtilityDialog(description);   // 0x140100270

    if (existingDialog != nullptr) {
        // Dialog is already open — close it
        Object::InternalPtr<UI::DialogInstance> closeRef(existingDialog);
        userInterface->CloseDialog(&closeRef);

        // Clear the set node's open flag
        PinnedToolNode* node = field_0x50.root;
        while (node != nullptr) {
            const void* nodeKey = node->key;
            if (key < nodeKey) {
                node = node->left;
            } else if (key > nodeKey) {
                node = node->right;
            } else {
                node->active = 0;
                break;
            }
        }

        // Return null
        return nullptr;
    }

    // Step 2: No existing dialog — find the UIDescription sub-description
    int32_t subCount = description->_subDescCount;
    UIDescription* uiDescItem = nullptr;

    for (int i = 0; i < subCount; i++) {
        Description* subItem = description->_subDescs[i];
        if (!subItem) continue;

        uint32_t subType = subItem->Type();

        if (subType == UIDescription::TypeId()) {
            bool isDerived = ::Object::IsDerivedFromNullSafe(subItem, UIDescription::TypeId());
            if (isDerived) {
                uiDescItem = static_cast<UIDescription*>(subItem);
            }
            break;
        }
    }

    if (uiDescItem == nullptr) {
        return nullptr;
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/DYNAMICPROFESSIONS.md): ext.dynamic_professions (A) opens
    // its own window in place of retail's Professions window (and closes it again on the next press)
    if (Ext::DynamicProfessions::Handles(uiDescItem)) {
        bool declined = false;
        UI::DialogInstance* opened = Ext::DynamicProfessions::Toggle(userInterface, declined);
        if (!declined) {
            return opened;
        }
    }

    // Step 3: Create a new DialogInstance from the UIDescription's resource
    void* newBuf = System::MemoryAllocator::MemAlloc(0xB8, 8);
    UI::DialogInstance* dialogInst = nullptr;

    if (newBuf != nullptr) {
        // DialogInstance ctor: (ui, resource, parent = null, dataSource, 0, 0);
        // the template is the data source (identifies the utility type).
        dialogInst = ::new (newBuf) UI::DialogInstance(
            userInterface,
            uiDescItem->_dialog,
            Object::InternalPtr<UI::DialogInstance>(),               // parent: null
            description,
            nullptr, 0);
        extern void* g_HarnessLastUtilityDialog;
        g_HarnessLastUtilityDialog = dialogInst;   // harness: drive its controllers
    }

    if (dialogInst == nullptr) {
        return nullptr;
    }

    // Step 4: Look up the position node in the +0x50 set
    PinnedToolNode* bstNode = nullptr;
    PinnedToolNode* current = field_0x50.root;
    while (current != nullptr) {
        const void* nodeKey = current->key;
        if (key < nodeKey) {
            current = current->left;
        } else if (key > nodeKey) {
            current = current->right;
        } else {
            bstNode = current;
            break;
        }
    }

    if (bstNode == nullptr) {
        // Not in the set — insert a new node (0x140014890): key, x = y = 0, flags 0.
        // Every byte of the record is written (three qword stores in retail).
        PinnedToolEntry rec;
        rec.key = description;                             // the set's key (`key` above)
        rec.x = 0;                                         // x=0, y=0 (one qword, H7)
        rec.y = 0;
        rec.active = 0;                                    // flags=0 (one qword with the pad)
        for (int i = 0; i < 7; ++i) rec._pad11[i] = 0;

        bstNode = Avl38SetInsert(&field_0x50, rec);        // 0x140014890
    }

    // Set the open flag
    bstNode->active = 1;

    // Position dialog at the saved coordinates
    dialogInst->SetUpperLeft(bstNode->x, bstNode->y, false);

    // Add dialog to the UI
    Object::InternalPtr<UI::DialogInstance> addRef(dialogInst);
    userInterface->AddDialog(&addRef);

    // Return the dialog reference
    return dialogInst;
}


// Captures the last DialogInstance created by ToggleUtilityDialog so the harness
// can reach its controllers (e.g. drive MapUI::SetupImage after open).
void* g_HarnessLastUtilityDialog = nullptr;

// Recursively dump a UI::Element tree: its name, type id and children.
static void HarnessDumpElemTree(UI::Element* elem, int depth, int* counter) {
    if (!elem || depth > 8 || *counter > 300) return;
    (*counter)++;
    const char* nameData = elem->_name;
    int len = nameData ? *reinterpret_cast<const int*>(nameData - 8) : 0;  // CAST-OK: string_header
    [[maybe_unused]] uint32_t tid = elem->Type();   // slot 2
    printf("[ElemTree] %*selem=%p tid=%u name='%.*s'\n", depth * 2, "",
           static_cast<void*>(elem), tid, len > 0 ? len : 0, nameData ? nameData : "");
    UI::Element** kids = elem->_children.GetData();
    int kc = elem->_children.GetSize();
    for (int i = 0; kids && i < kc; i++) HarnessDumpElemTree(kids[i], depth + 1, counter);
}

// GUI_PLAN Keystone F, Phase 4 — harness entry. Fires the utility-dialog open path
// (toolbar case 2) headlessly: pass the utility item's Description ptr (item+0x58,
// e.g. a resolved Template\Utility*.rsc) as `param`. Mirrors the case-2 dispatch.
// Its one caller is src/diag/tool_harness.cpp.
extern "C" void ToolbarUtility_HarnessOpen(PlayerToolManager* gameSubsys, ComponentDescription* param) {
    g_HarnessLastUtilityDialog = nullptr;
    gameSubsys->ToggleUtilityDialog(param);
}

// Harness: fetch a named controller from the just-opened utility dialog and
// dispatch one of its vtable slots (slotIndex, e.g. 4=+0x20 MapUI "SetupImage",
// 5=+0x28 ProfessionUI setup/populate). Used to drive controller setup after open,
// since AddDialog doesn't reach the setup through the standalone ToolUI child-dialog
// vtable. slotIndex defaults to 4 for the existing map callers.
extern "C" void ToolbarUtility_HarnessDriveController(const char* controllerName, int slotIndex) {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    if (!gameWorld) { printf("[HarnessDrive] gameWorld null\n"); return; }
    UI::UserInterface* ui = gameWorld->m_userInterface;
    if (!ui) { printf("[HarnessDrive] UserInterface null\n"); return; }

    uint32_t tid = ::Object::GetTypeIdByName(controllerName);
    printf("[HarnessDrive] searching open dialogs for '%s' (tid=%u)\n", controllerName, tid);

    // The open-dialog collection is the _addDialogs array (+0x28) — that's where
    // UserInterface::AddDialog (Runtime 0x180120ee0) pushes. Each element is an
    // InternalPtr<DialogInstance>. Enumerate it, dumping each dialog's controllers
    // to locate the (nested) MapUI.
    UI::DialogInstancePtr* data = ui->_addDialogs.GetData();
    int count = ui->_addDialogs.GetSize();
    printf("[HarnessDrive] UI+0x28 dialog array=%p count=%d\n", static_cast<void*>(data), count);
    for (int idx = 0; data && idx < count && idx < 32; idx++) {
        UI::DialogInstance* dialog = data[idx];
        if (!dialog) { printf("[HarnessDrive] dialog[%d]=null\n", idx); continue; }
        UI::DialogController** ctrlArr = dialog->_controllers.GetData();
        int cnt = dialog->_controllers.GetSize();
        [[maybe_unused]] uint32_t dlgTid = dialog->Type();   // slot 2
        printf("[HarnessDrive] dialog[%d]=%p dlgTid=%u controllers=%d:", idx, static_cast<void*>(dialog), dlgTid, cnt);
        for (int i = 0; ctrlArr && i < cnt && i < 16; i++) {
            UI::DialogController* c = ctrlArr[i]; uint32_t ctid = 0;
            if (c) ctid = c->Type();   // slot 2
            printf(" [%d]tid=%u", i, ctid);
        }
        printf("\n");
        // Dump the dialog's element tree (its root) so we see exactly what content
        // was built (names, types) and whether the minimap is there.
        {
            UI::Element* root = dialog->_root.Get();
            printf("[HarnessDrive] dialog[%d] root elem=%p — tree:\n", idx, static_cast<void*>(root));
            int counter = 0;
            HarnessDumpElemTree(root, 0, &counter);
            printf("[HarnessDrive] (tree had %d elements)\n", counter);
        }
        // Probe the dialog's element tree for the minimap content elements — if
        // they exist here, the content is embedded (no MapUI controller needed to
        // locate them; we can bind textures directly).
        {
            const char* names[] = { "imageMap", "imageOverlay", "buttonOk", "map", "image" };
            for (const char* nm : names) {
                System::String nb(nm);
                [[maybe_unused]] UI::Element* r = dialog->FindElement(nb).Get();
                printf("[HarnessDrive]   FindElement('%s')=%p\n", nm, static_cast<void*>(r));
            }
        }
        UI::DialogController* dc = dialog->GetController(tid).Get();
        if (dc) {
            printf("[HarnessDrive] FOUND '%s' ctrl=%p in dialog[%d] — driving Slot%d\n",
                   controllerName, static_cast<void*>(dc), idx, slotIndex);
            // The caller (src/diag/tool_harness.cpp) names the member by its
            // RETAIL slot number: 4 = OnCreated, 5 = OnOpened (DialogController.h).
            switch (slotIndex) {
            case 4: dc->OnCreated(); break;
            case 5: dc->OnOpened();  break;
            default:
                printf("[HarnessDrive] Slot%d is not a harness member (4 or 5)\n", slotIndex);
                return;
            }
            printf("[HarnessDrive] Slot%d done\n", slotIndex);
            return;
        }
    }
    printf("[HarnessDrive] '%s' not found in %d open dialogs\n", controllerName, count);
}

// ============================================================
// PlayerToolManager::OpenPauseMenu (FUN_140101ce0; the tree's EntityUI_OpenPauseMenu)
//
// The IN-GAME pause menu opener. Retail has exactly two callers, and both mean
// "bring up the in-game menu":
//   - ToolbarDialog::OnElementEvent case 3 (0x1400b96ae) -- the toolbar's menu button
//   - the manager's Update (0x1401026a0) -- Application::IsPaused() && the
//     pause-on-focus-loss setting, i.e. the game lost focus
//
// It builds the same MainMenuDialog class the front-end menu uses, but from
// Dialog/Pause.rsc and with +0xB8 = gameWorld (instead of +0xC0 = menu loop).
// That +0xB8 is what makes MainMenuDialog::Setup (slot 4) light up the in-game
// buttons -- buttonResume / buttonSave / buttonMenu / buttonQuit -- and what
// makes OnCommand's Quit reach Application::Quit.
//
// Flow:
//   1. If a tool is active, call its OnCancel (slot 9)
//   2. SelectTool(0): back to the EditTool
//   3. Recompute the camera transform and apply it to the camera
//   4. Load "Dialog/Pause.rsc" via ExternalFactory::FindOrLoad
//   5. MemAlloc 0xC8 bytes, construct the MainMenuDialog with the game world
//   6. Add the dialog to the UI
// ============================================================
// Returns the dialog it created. Retail is void -- nothing there needs the
// pointer, because a real click arrives with the dialog as `this`. Handing it
// back costs nothing and is the only way an unattended driver can address the
// menu it just opened (src/diag/pausemenu_harness.cpp).
UI::DialogInstance* PlayerToolManager::OpenPauseMenu() {
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    UI::UserInterface* userInterface = gameWorld->m_userInterface;
    // Step 1: If there's an active tool, cancel it (slot 9; result discarded)
    if (currentTool != nullptr) {
        currentTool->OnCancel();
    }

    // Step 2: Back to the default tool
    SelectTool(nullptr);   // 0x140102b60

    // Step 3: Recompute camera view transform
    // Time::Time(0.0f) — constructs a zero time value (unused, but matches original)
    System::Time timeVal;
    RtSystem_Time_Ctor(&timeVal, 0.0f);

    // The camera controller (gameWorld+0xE0)
    PlayerController* cameraSys = gameWorld->m_playerController;

    // The look-at transform (0x1400983f0), then the camera's world transform
    {
        Math::Transform viewTransform = cameraSys->CalcTransform();   // 0x1400983f0

        // ?AsTransform3@Transform@Math@@QEBA?AVTransform3@2@XZ  retail 0x18001fcd0
        Math::Transform3 transform3 = viewTransform.AsTransform3();
        // ?SetCameraToWorld@Camera@Graphics@@QEAAXAEBVTransform3@Math@@@Z.
        cameraSys->camera.SetCameraToWorld(transform3);
    }

    // Step 4: Load "Dialog/Pause.rsc" resource
    System::Filename filename("Dialog/Pause.rsc");

    const ::Object::External* externalRes = nullptr;
    if (::Object::ExternalFactory::_singleton) {
        externalRes = ::Object::ExternalFactory::_singleton->FindOrLoad(filename);
    }

    // Step 5: Allocate the pause menu (0xC8 bytes)
    void* pauseDialog = System::MemoryAllocator::MemAlloc(0xC8, 8);
    MainMenuDialog* finalDialog = nullptr;

    if (pauseDialog != nullptr) {
        // The pause menu IS a MainMenuDialog (retail stamps its table,
        // 0x14015f550): DialogInstance(userInterface, externalRes, nullParent,
        // 0, 0, 0), then +0xB8 = gameWorld, +0xC0 = 0.
        finalDialog = ::new (pauseDialog) MainMenuDialog(
            userInterface,
            static_cast<const UI::Dialog*>(externalRes),
            Object::InternalPtr<UI::DialogInstance>(),
            gameWorld, nullptr);
    }

    // Step 6: add it to the UI
    Object::InternalPtr<UI::DialogInstance> addRef(finalDialog);
    userInterface->AddDialog(&addRef);
    return finalDialog;
}


// ============================================================
// Helper: push_back for the engine's inlined dynamic arrays (data, count,
// capacity as three independent locals). Mimics the inlined vector growth
// pattern seen throughout the exe. Kept for the constructor's two local lists
// (the Toolbar item list and the shortcut id pool): they are three locals, not
// an Array header, AND the shortcut pool is later mutated directly
// (element-shift + count-- on consumption).
// ============================================================
template <class T>
static void DynArray_Push(T** pData, int32_t* pCount, int32_t* pCapacity, T value) {
    int32_t oldCount = *pCount;
    int32_t newCount = oldCount + 1;
    if (newCount == oldCount) return; // overflow guard (matches original)

    *pCount = newCount;
    T* oldData = *pData;

    if (*pCapacity < newCount) {
        int32_t newCap = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) {
            newCap = newCount * 3;
        }
        *pCapacity = newCap;
        *pData = static_cast<T*>(System::MemoryAllocator::MemAlloc((int64_t)newCap * sizeof(T), alignof(T)));
    }

    // Copy existing elements to new buffer (if buffer changed)
    int32_t copyStart = (oldData == *pData) ? oldCount : 0;
    T* dst = *pData;
    for (int32_t i = copyStart; i < newCount; i++) {
        if (i < oldCount) {
            dst[i] = oldData[i];
        } else {
            dst[i] = value; // new element
        }
    }

    if (oldData != *pData && oldData != nullptr) {
        System::MemoryAllocator::MemFree(oldData);
    }
}

// ============================================================
// ToolbarDialog::ToolbarDialog (FUN_1400b9910)
//
// The main constructor:
//   1. Calls base DialogInstance ctor
//   2. The compiler's vtable store
//   3. Iterates all Toolbar externals whose parent group is `group`
//   4. Sorts them by priority (_field50)
//   5. Determines the keyboard shortcut range from the nesting depth
//   6. For each toolbar item, clones a UI template element, sets up
//      name, tooltip, sprite, keyboard shortcut, and adds to table
//   7. Calls RadioElement::FindCheckBoxes to wire up radio behavior
//   8. Populates the speed values array {1, 2, 5, 10, 20, 50} (retail: {1, 2, 5, 10})
//   9. Finds current game speed index in the array
// ============================================================
ToolbarDialog::ToolbarDialog(UI::UserInterface* userInterface, const UI::Dialog* dialog,
                             Object::InternalPtr<UI::DialogInstance> parent,
                             const Toolbar* group)
    : UI::DialogInstance(userInterface, dialog, parent, nullptr, nullptr, 0) {
    // --- Step 3: Initialize ToolbarDialog-specific fields (the three arrays
    // are zeroed by their own constructors) ---
    radioElement = nullptr;
    toolbarGroup = group;                         // the parent toolbar item (null = the main bar)
    subDialog = Object::InternalPtr<UI::DialogInstance>(nullptr);
    openSubIndex = -1;

    // --- Step 4: Find key UI elements ---
    // Find "toolbarContent" table element
    UI::TableElement* tableElement;
    {
        System::String strStr("toolbarContent");
        tableElement = static_cast<UI::TableElement*>(FindElement(strStr).Get());
    }

    // Find "toolBar" radio element
    {
        System::String strStr("toolBar");
        radioElement = static_cast<UI::RadioElement*>(FindElement(strStr).Get());
    }

    // Set radio element ID to 1000
    radioElement->SetElementId(1000);

    // --- Step 5: Collect all Toolbar items of our group ---
    // Use ExternalFactory::FindNext to iterate all Toolbar externals
    const ::Object::ExternalFactory* etbFactory = ::Object::ExternalFactory::_singleton;
    uint32_t toolbarTypeId = Toolbar::TypeId();

    // Temporary dynamic array for matching toolbar items
    const Toolbar** itemList = nullptr;
    int32_t itemListCount = 0;
    int32_t itemListCap = 0;

    const ::Object::External* ext = etbFactory->FindNext(toolbarTypeId, nullptr);
    while (ext != nullptr) {
        // IsDerivedFrom check
        bool isDerived = ::Object::IsDerivedFromNullSafe(ext, toolbarTypeId);
        const Toolbar* toolbarItem = isDerived ? static_cast<const Toolbar*>(ext) : nullptr;

        // Check: item must be shown (_field45 != 0) and its group must match
        if (toolbarItem != nullptr) {
            if (toolbarItem->_field45 != 0 && toolbarItem->_field60 == group) {
                DynArray_Push(&itemList, &itemListCount, &itemListCap, toolbarItem);
            }
        }

        ext = etbFactory->FindNext(toolbarTypeId, ext);
    }

    // Sort the collected items by their priority field (_field50), ascending.
    // The original calls fn_ToolbarSort (exe-only introsort @0x140021790, keyed on
    // the item's +0x50). Ported directly so the buttons order correctly.
    // itemListCount is small (~10) and a stable insertion sort matches the
    // ascending-by-priority result; equal priorities keep their load order.
    if (itemListCount > 1) {
        const Toolbar** items = itemList;
        for (int i = 1; i < itemListCount; i++) {
            const Toolbar* key = items[i];
            int32_t keyPri = key->_field50;
            int j = i - 1;
            while (j >= 0 && items[j]->_field50 > keyPri) {
                items[j + 1] = items[j];
                j--;
            }
            items[j + 1] = key;
        }
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): the extension window's one button
    // (src/ext/toolbar) in the Reports group. A null entry in the item list marks its slot; the readers of
    // toolbarItems below and in UpdateButtons / OnUpdateLayout / OnElementEvent skip it.
    {
        const int extSlot = Ext::Toolbar::InsertAt(group, itemList, itemListCount);
        if (extSlot >= 0 && extSlot <= itemListCount) {
            DynArray_Push(&itemList, &itemListCount, &itemListCap, static_cast<const Toolbar*>(nullptr));
            for (int i = itemListCount - 1; i > extSlot; --i) {
                itemList[i] = itemList[i - 1];
            }
            itemList[extSlot] = nullptr;
        }
    }

    // --- Step 6: Determine the nesting depth ---
    // Walk the parent chain (_field60) of the first item
    int depth = 0;
    if (itemListCount > 0) {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): the chain starts at `group`, the
        // parent of every collected item (retail's `itemList[0]->_field60`). The first slot may be the
        // extension button's null entry. (the parent chain, ExternalPtr<Toolbar>)
        for (const Toolbar* link = group;
             link != nullptr;
             link = link->_field60) {
            depth++;
        }
    }

    // Map depth to keyboard shortcut ID range
    int shortcutStart, shortcutEnd;
    if (depth == 0) {
        shortcutStart = 0x18; shortcutEnd = 0x20;
    } else if (depth == 1) {
        shortcutStart = 0x23; shortcutEnd = 0x2c;
    } else if (depth == 2) {
        shortcutStart = 0x2d; shortcutEnd = 0x31;
    } else {
        shortcutStart = 0; shortcutEnd = -1; // no shortcuts
    }

    // Build array of available shortcut IDs
    int32_t* shortcutIds = nullptr;
    int32_t shortcutCount = 0;
    int32_t shortcutCap = 0;
    for (int id = shortcutStart; id <= shortcutEnd; id++) {
        DynArray_Push(&shortcutIds, &shortcutCount, &shortcutCap, (int32_t)id);
    }

    // --- Step 7: Create UI elements for each toolbar item ---
    int buttonIndex = 0;
    for (int i = 0; i < itemListCount; i++) {
        const Toolbar* item = itemList[i];
        if (item == nullptr) {
            // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): the extension button, a
            // utility-style button (template 3) with no Toolbar item behind it.
            const UI::ElementDescription* extTemplate = dialog->GetTemplate(3).Get();
            Object::InternalPtr<UI::Element> extClone;
            extTemplate->CreateInstance(&extClone, dialog);
            UI::Element* extElement = extClone.Get();
            buttonElements.SetSize(buttonElements.GetSize() + 1, extElement);
            toolbarItems.SetSize(toolbarItems.GetSize() + 1, nullptr);
            Object::InternalPtr<UI::Element> extPtr(extElement);
            tableElement->SetChild(&extPtr, buttonIndex);
            Ext::Toolbar::Style(extElement, buttonIndex);
            buttonIndex++;
            continue;
        }
        // LEFT: Toolbar's +0x30 / +0x38 Strings are declared void* (GAME-c's header,
        // H5): the button text and sprite names are read through a cast.
        ComponentDescription* itemTemplate = item->_field58;

        // Determine template index based on action type
        int32_t actionType = item->_field48;
        int templateIndex;
        if (actionType == 10) {
            templateIndex = 0;  // SpeedDisplay: plain text template
        } else if (actionType == 0) {
            templateIndex = 1;  // Tool/group: expandable button
        } else if (actionType == 1) {
            templateIndex = 2;  // ShowGroup: standard button
        } else {
            templateIndex = 3;  // Other: utility button
        }

        // Get template element from Dialog and clone it (ElementDescription
        // slot 8, RETSLOT: RCX = this, RDX = the return buffer, R8 = the dialog).
        const UI::ElementDescription* templateElem = dialog->GetTemplate(templateIndex).Get();
        Object::InternalPtr<UI::Element> clone;
        templateElem->CreateInstance(&clone, dialog);
        UI::Element* newElement = clone.Get();

        // Push to the button and item arrays (Array<T>::SetSize with the fill)
        buttonElements.SetSize(buttonElements.GetSize() + 1, newElement);
        toolbarItems.SetSize(toolbarItems.GetSize() + 1, item);

        // Add element as child of table at current buttonIndex
        Object::InternalPtr<UI::Element> elemPtr(newElement);
        tableElement->SetChild(&elemPtr, buttonIndex);

        // Set element name from toolbar item's resource path.
        // `GetPath` returns by value through a hidden return slot and the callee
        // CONSTRUCTS into it; copy-initialising the local is the same store.
        System::Filename pathObj = item->GetPath();
        System::Filename pathIdObj = pathObj.GetPathIdentifer();
        newElement->SetName(pathIdObj.AsString());

        // Get toolbar item's display string and sprite
        UI::String uiString(Object::ExternalPtr<UI::StringTable>(item->_field20),
                            item->_field30);
#ifdef _DEBUG
        UI::Sprite uiSprite;
        Debug_Sprite_Ctor(&uiSprite, item->_field28, &item->_field38);
#else
        UI::Sprite uiSprite(Object::ExternalPtr<UI::SpriteSheet>(item->_field28),
                            item->_field38);
#endif

        // --- Configure button element (skip for SpeedDisplay) ---
        if (templateIndex != 0) {
            // Cast element to ButtonElement via IsDerivedFrom
            bool isButton = ::Object::IsDerivedFromNullSafe(newElement, UI::ButtonElement::TypeId());
            UI::ButtonElement* button = isButton ? static_cast<UI::ButtonElement*>(newElement) : nullptr;

            // For ShowGroup (1) and Utility (2), override string/sprite from
            // the item's template's ToolbarDescription
            if (actionType == 1 || actionType == 2) {
                ToolbarDescription* buildReqComp = nullptr;
                if (itemTemplate != nullptr) {
                    int32_t descCount = itemTemplate->_subDescCount;
                    for (int j = 0; j < descCount; j++) {
                        Description* subItem = itemTemplate->_subDescs[j];
                        uint32_t subType = subItem->Type();
                        if (subType == ToolbarDescription::TypeId()) {
                            bool derived = ::Object::IsDerivedFromNullSafe(subItem, ToolbarDescription::TypeId());
                            if (derived) {
                                buildReqComp = static_cast<ToolbarDescription*>(subItem);
                            }
                            break;
                        }
                    }
                }

                // Override the UI::String (the description's table and _str3)
                if (buildReqComp != nullptr) {
                    uiString = UI::String(Object::ExternalPtr<UI::StringTable>(buildReqComp->_stringTable),
                                          buildReqComp->_str3);

                    // Override the sprite (the description's sheet and _name)
#ifdef _DEBUG
                                        UI::Sprite overrideSpr;
                    Debug_Sprite_Ctor(&overrideSpr, buildReqComp->_spriteSheet,
                                      &buildReqComp->_name);
#else
                    UI::Sprite overrideSpr(Object::ExternalPtr<UI::SpriteSheet>(buildReqComp->_spriteSheet),
                                           buildReqComp->_name);
#endif
                    uiSprite = overrideSpr;
                }
            }

            // Assign keyboard shortcut ID
            if (item->_field44 == 0) {
                // Item has explicit shortcut override (_field40)
                button->_keyIndex = item->_field40;

                // Remove this shortcut ID from the available pool
                int32_t targetId = item->_field40;
                for (int j = 0; j < shortcutCount; j++) {
                    if (shortcutIds[j] == targetId) {
                        // Shift remaining elements left
                        for (int k = j; k < shortcutCount - 1; k++) {
                            shortcutIds[k] = shortcutIds[k + 1];
                        }
                        shortcutCount--;
                        j--; // re-check same index
                    }
                }
            } else if (shortcutCount > 0) {
                // Auto-assign first available shortcut
                button->_keyIndex = shortcutIds[0];

                // Remove first shortcut from pool
                for (int k = 0; k < shortcutCount - 1; k++) {
                    shortcutIds[k] = shortcutIds[k + 1];
                }
                shortcutCount--;
            }

            // Set element ID (button index within this toolbar)
            button->SetElementId(buttonIndex);

            button->SetUserData(itemTemplate);

            button->SetToolTip(uiString);

            button->SetFlagsOnOff(UI::kElementFlagRaisesEvents, 0);

            // Set sprite on all ImageElement children
            int childIdx = 0;
            int childCount = button->ChildCount();
            while (childIdx < childCount) {
                UI::Element* childElem = button->Child(childIdx).Get();

                // Cast to ImageElement
                bool isImage = ::Object::IsDerivedFromNullSafe(childElem, UI::ImageElement::TypeId());
                UI::ImageElement* imageElem = isImage ? static_cast<UI::ImageElement*>(childElem) : nullptr;

                // Set sprite if valid and imageElem is non-null
#ifdef _DEBUG
                bool spriteValid = Debug_Sprite_IsValid(&uiSprite);
#else
                bool spriteValid = uiSprite.IsValid();
#endif
                if (spriteValid && imageElem != nullptr) {
                    UI::Sprite sprCopy = uiSprite;
#ifdef _DEBUG
                    Debug_ImageElement_SetSprite(imageElem, &sprCopy);
#else
                    imageElem->SetSprite(&sprCopy);
#endif
                }

                childIdx++;
                childCount = button->ChildCount();
            }
        }

        buttonIndex++;
    }

    // --- Step 8: Wire up radio button behavior ---
    radioElement->FindCheckBoxes();

    // --- Step 9: Populate speed values array {1, 2, 5, 10, 20, 50} ---
    currentSpeedIndex = 0;

    speedValues.SetSize(speedValues.GetSize() + 1, 1);
    speedValues.SetSize(speedValues.GetSize() + 1, 2);
    speedValues.SetSize(speedValues.GetSize() + 1, 5);
    speedValues.SetSize(speedValues.GetSize() + 1, 10);
    // [NON-RETAIL] retail stops at 10x. 20x and 50x extend the sequence so the perf
    // harness (BANISHED_STRESS_SPEED) and the in-game speed-up button can push the
    // sim harder than retail allows; the speed cascade below is table-driven.
    speedValues.SetSize(speedValues.GetSize() + 1, 20);
    speedValues.SetSize(speedValues.GetSize() + 1, 50);

    // --- Step 10: Find current game speed in speed array ---
    int32_t speedIndex = 0;
    PlayerToolManager* toolManager = GameUpdateLoop::_singleton->m_toolManager;
    int32_t currentSpeed = toolManager->field_0x34;
    int32_t speedCount = speedValues.GetSize();

    for (int i = 0; i < speedCount; i++) {
        if (speedValues[i] == currentSpeed) {
            if (i >= 0) {
                speedIndex = i;
                goto found_speed;
            }
            break;
        }
    }
    speedIndex = 0; // default to first speed
found_speed:
    currentSpeedIndex = speedIndex;
    // Write the speed value back to the game system
    toolManager->field_0x34 = speedValues[speedIndex];

    // --- Cleanup ---
    System::MemoryAllocator::MemFree(shortcutIds);
    System::MemoryAllocator::MemFree(itemList);
}

// ============================================================
// ToolbarDialog::Update (vtable +0x30) — Sync button states
// ============================================================
// Iterates toolbar items, updates button checked/enabled states
// based on current game state (speed, pause, group visibility).
// Then calls the secondary update helper for ShowGroup/FocusEntity buttons.
//
// Original: 0x1400b8d40.

void ToolbarDialog::OnUpdateLayout() {
    // The game world, read unguarded (0x1400b8d40: [GameWorld+0x120] at once)
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    PlayerToolManager* toolManager = gameWorld->m_toolManager;

    // The tool manager's active group (+0x20) and game speed (+0x34)
    const ComponentDescription* activeDialog = toolManager->field_0x20;
    uint32_t gameSpeed = (uint32_t)toolManager->field_0x34;

    int32_t itemCount = toolbarItems.GetSize();

    for (int i = 0; i < itemCount; i++) {
        const Toolbar* item = toolbarItems[i];
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): the extension button
        // has no Toolbar item
        if (item == nullptr) {
            continue;
        }
        int32_t actionType = item->_field48;

        if (actionType == 1) {
            // ShowGroup: uncheck button if this group is not the active one
            // (the address compare retail makes: +0x20 against the item's template)
            if (activeDialog != item->_field58) {
                // The button element from the parallel array, narrowed (null when not
                // a CheckElement) and its +0xd8 read unguarded, as retail
                UI::Element* elem = buttonElements[i];
                bool isDerived = ::Object::IsDerivedFromNullSafe(elem, UI::CheckElement::TypeId());
                UI::CheckElement* check = isDerived ? static_cast<UI::CheckElement*>(elem) : nullptr;

                // If button is currently checked, uncheck it
                if (check->_checked != 0) {
                    // The ONLY place a toolbar button is silently unchecked from the
                    // per-frame update; it fires only on the 1->0 transition.
                    check->_checked = 0;
                }
            }
        }
        else if (actionType == 9) {
            // SpeedUp: enable/disable based on whether current speed < max speed
            UI::Element* elem = buttonElements[i];

            int32_t speedCount = speedValues.GetSize();
            bool canSpeedUp = (int32_t)gameSpeed < speedValues[speedCount - 1];
            elem->Enable(canSpeedUp);   // slot 8
        }
        else if (actionType == 6) {
            // SpeedDown: enable/disable based on whether current speed > min speed
            UI::Element* elem = buttonElements[i];

            bool canSpeedDown = speedValues[0] < (int32_t)gameSpeed;
            elem->Enable(canSpeedDown);   // slot 8
        }
        else if (actionType == 8) {
            // Unpause button: enabled while paused
            UI::Element* elem = buttonElements[i];

            uint8_t isPaused = toolManager->paused;
            elem->Enable(isPaused);   // slot 8
        }
        else if (actionType == 7) {
            // Pause button: enabled while NOT paused
            UI::Element* elem = buttonElements[i];

            uint8_t isPaused = toolManager->paused;
            elem->Enable(isPaused == 0);   // slot 8
        }
        else if (actionType == 10) {
            // SpeedDisplay: the button's child 1 (a TextElement) shows "%ix"
            UI::Element* elem = buttonElements[i];

            UI::Element* childElem = elem->Child(1).Get();

            // IsDerivedFrom check for TextElement type (exref at 0x140151508)
            bool isDerived = ::Object::IsDerivedFromNullSafe(childElem, UI::TextElement::TypeId());
            UI::TextElement* textElem = isDerived ? static_cast<UI::TextElement*>(childElem) : nullptr;

            // Format speed string: "%ix" with gameSpeed value
            System::String fmtStr = System::String::Format("%ix", (int)gameSpeed);

            textElem->SetParameter(0, fmtStr);   // unguarded, as retail
        }
    }

    // Secondary update — the ShowGroup and FocusEntity button enable states
    UpdateButtons();
}

// ============================================================
// ToolbarDialog::OnEvent (vtable +0x40) — Handle toolbar clicks
// ============================================================
// Dispatches toolbar button click events to game actions.
// Event type 0x12 = button click, 0x16 = toolbar selection change.
//
// Original: 0x1400b92a0.

void ToolbarDialog::OnElementEvent(Object::InternalPtr<UI::Element>* element, UI::EventId eventType, int32_t eventData, int32_t /*c*/, int32_t /*d*/, int32_t p7, void* /*userData*/) {
    // The game world and its tool manager
    GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
    PlayerToolManager* gameSys = gameWorld->m_toolManager;

    if (eventType == UI::EventId::Activated) {
        // Button click: eventData = button index
        int32_t buttonIndex = eventData;

        // The toolbar item of the button
        const Toolbar* item = toolbarItems[buttonIndex];

        // The element as a CheckElement (IsDerivedFrom)
        UI::Element* elemObj = element->Get();
        bool isDerived = ::Object::IsDerivedFromNullSafe(elemObj, UI::CheckElement::TypeId());
        UI::CheckElement* checkElem = isDerived ? static_cast<UI::CheckElement*>(elemObj) : nullptr;

        if (item == nullptr) {
            // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): the extension button. Like a
            // utility button: it opens the window or closes it. The button is released at once (the window's
            // own close button can close it too); any open sub-toolbar closes as for every non-Tool button.
            if (subDialog.Get() != nullptr) {
                Object::InternalPtr<UI::DialogInstance> closeRef = subDialog;
                _ui->CloseDialog(&closeRef);
                subDialog = Object::InternalPtr<UI::DialogInstance>(nullptr);
                openSubIndex = -1;
            }
            if (checkElem != nullptr) {
                checkElem->_checked = 0;
            }
            Ext::Toolbar::Toggle(_ui);   // open, or close when open
            return;
        }

        int32_t actionType = item->_field48;

        switch (actionType) {
        case 0: {
            // Tool: open/close sub-toolbar dialog

            // If there's already a sub-dialog open, close it
            if (subDialog.Get() != nullptr) {
                Object::InternalPtr<UI::DialogInstance> closeRef = subDialog;
                _ui->CloseDialog(&closeRef);
            }

            if (buttonIndex == openSubIndex) {
                // Same button clicked again — toggle off
                openSubIndex = -1;
                subDialog = Object::InternalPtr<UI::DialogInstance>(nullptr);
            } else {
                // Open new sub-toolbar
                // Allocate new ToolbarDialog (0x108 bytes)
                void* newDialog = System::MemoryAllocator::MemAlloc(0x108, 8);
                ToolbarDialog* childDialog = nullptr;
                if (newDialog) {
                    // Construct child ToolbarDialog (recursive), parented to this one;
                    // its group is the clicked item.
                    childDialog = ::new (newDialog) ToolbarDialog(
                        _ui, _dialog,
                        Object::InternalPtr<UI::DialogInstance>(this),
                        item);
                }

                // Store child dialog reference
                subDialog = Object::InternalPtr<UI::DialogInstance>(childDialog);

                // Position the sub-dialog relative to toolbar
                // Root element of the sub-dialog (GetRoot inlined)
                UI::Element* subRoot = subDialog.Get()->_root.Get();

                // Get padded size of sub-dialog
                int subWidth, subHeight;
                subRoot->GetPaddedSize(subWidth, subHeight);

                // The toolbar's own layout: the radio's layout height, the root's bottom pad
                int parentBottom = radioElement->_layoutHeight;   // +0x64
                int selfTop = _root->_bottomPad;                   // +0x24

                // The clicked element's layout span, for centering
                int elemX = checkElem->_layoutLeft;       // +0x68
                int elemWidth = checkElem->_layoutRight;  // +0x70

                // Calculate horizontal position: center sub-dialog on button
                const Video::Device* videoDevice = ::Video::Device::_singleton;
                float uiScale = _ui->_uiScale;
                int screenWidth = videoDevice->backBufferWidth;
                int xPos = (int)((float)screenWidth * uiScale) -
                           (elemWidth + elemX) / 2 - subWidth / 2;
                if (xPos < 0) xPos = 0;

                // Set padding on sub-dialog root to position it
                subDialog.Get()->_root->SetPadding(0, xPos, 0, parentBottom + selfTop);

                // Record which button index is open
                openSubIndex = buttonIndex;

                // Add the sub-dialog to the UI
                Object::InternalPtr<UI::DialogInstance> addRef = subDialog;
                _ui->AddDialog(&addRef);
            }
            break;
        }
        case 1: {
            // ShowGroup: select the item's tool group (null when unchecked)
            ComponentDescription* groupId = nullptr;
            if (checkElem != nullptr && checkElem->_checked != 0) {
                groupId = item->_field58;
            }
            gameSys->SelectTool(groupId);   // 0x140102b60
            break;
        }
        case 2: {
            // Utility action: toggle the item's utility dialog
            gameSys->ToggleUtilityDialog(item->_field58);   // 0x140101a80
            break;
        }
        case 3: {
            // Open the in-game pause menu (Resume / Save / Load / Options /
            // Quit to Menu / Quit Game), exactly what retail does at 0x1400b96ae.
            gameSys->OpenPauseMenu();   // 0x140101ce0
            break;
        }
        case 4: {
            // Focus on entity: the first entity built from the item's template
            const CitizenBehavior* town = gameWorld->m_citizenList;

            // A null dialog: no panel to track
            UI::DialogInstance* noDialog = nullptr;

            // Find entity by template (0x140088d40)
            ComponentEntity* entity = Toolbar_FindEntity_Impl(town, item->_field58, 0);

            // Center camera on entity: retail 0x1400b92a0 hands 0x140103040 the
            // ADDRESS of 0x140088d40's result slot (the tree handed the entity
            // itself, which CenterOnEntity then read as the slot).
            gameSys->CenterOnEntity(&noDialog, &entity);   // 0x140103040
            break;
        }
        case 5: {
            // CenterOnPoint: save the current view into the item's camera slot
            int32_t pointIndex = item->selectedIndex;
            PlayerCameraView* point = &gameSys->cameraViews[pointIndex];

            const PlayerController* cameraSys = gameWorld->m_playerController;

            // Set point as active
            point->used = 1;
            // Copy the view angles from the tool manager
            point->field_0x14 = gameSys->field_0x3C;
            point->field_0x10 = gameSys->field_0x40;
            // Copy the camera's yaw and look-at target
            point->field_0x0C = cameraSys->yaw;
            float camY = cameraSys->target.y;
            float camZ = cameraSys->target.z;
            point->position.x = cameraSys->target.x;
            point->position.y = camY;
            point->position.z = camZ;

            // Close sub-dialog and reset radio selection on the parent toolbar.
            // Retail resolves the parent InternalBasePtr BEFORE reading it.
            UI::DialogInstance* parentDialog = _parent.Get();
            bool isDerivedParent = ::Object::IsDerivedFromNullSafe(parentDialog, ToolbarDialog::TypeId());
            // No null guard: retail's FUN_1400b9730 derefs +0xe8 unguarded, so this
            // cast is expected to always succeed.
            ToolbarDialog* typedParent = isDerivedParent ? static_cast<ToolbarDialog*>(parentDialog) : nullptr;
            typedParent->CloseSubDialog();
            break;
        }
        case 6: {
            // SpeedDown
            ChangeSpeed(-1);
            break;
        }
        case 7: {
            // Pause
            gameSys->paused = 1;
            break;
        }
        case 8: {
            // Unpause
            gameSys->paused = 0;
            break;
        }
        case 9: {
            // SpeedUp
            ChangeSpeed(1);
            break;
        }
        }
    }
    else if (eventType == UI::EventId::RadioSelected && eventData == 1000 && p7 >= 0) {
        // Toolbar selection change: p7 = selected item index
        int32_t selIndex = p7;
        const Toolbar* item = toolbarItems[selIndex];

        // FUN_140010e70: IsDerivedFrom check + cast (result unused)
        UI::Element* elemObj = element->Get();
        (void)::Object::IsDerivedFromNullSafe(elemObj, UI::CheckElement::TypeId());

        // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): the extension button
        // acts as a Utility
        int32_t actionType = item != nullptr ? item->_field48 : 2;

        // If not a Tool (case 0) and we have an open sub-dialog, close it
        if (actionType != 0 && subDialog.Get() != nullptr) {
            Object::InternalPtr<UI::DialogInstance> closeRef = subDialog;
            _ui->CloseDialog(&closeRef);

            subDialog = Object::InternalPtr<UI::DialogInstance>(nullptr);
            openSubIndex = -1;
        }
    }
}

// ============================================================
// ENDGAME B.3.8 DLG: construction and teardown of the real class.
// ============================================================

// 0x1400b9890 (create 0x140021710's ctor): DialogInstance(), the table, then
// +0xB8..+0xE8 and +0xF8/+0x100 zeroed as qwords and +0xF4 as a dword. +0xF0
// (openSubIndex) is left as allocated, as retail leaves it.
ToolbarDialog::ToolbarDialog() {
    // the three arrays are zeroed by their own constructors
    radioElement = nullptr;
    toolbarGroup = nullptr;
    subDialog = Object::InternalPtr<UI::DialogInstance>(nullptr);
    currentSpeedIndex = 0;
}

// [0] 0x140021760: the three arrays (the members' destructors, in reverse
// order: +0xF8, +0xD0, +0xC0 -- MemFree the data, zero the header), then
// ~DialogInstance.
ToolbarDialog::~ToolbarDialog() {
}

// create 0x140021710: MemAlloc(0x108, 8), then the default ctor.
::Object::Base* ToolbarDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ToolbarDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) ToolbarDialog());
}
// createInPlace (the tree's in-place build is kept, B8-13).
::Object::Base* ToolbarDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) ToolbarDialog());
}

// The reflection record stays at the TU position of the UI_DIALOG_STUB row it
// replaces (ui_dialog_stub_factories.cpp, B38 D4).

// ============================================================
// PlayerToolManager::CenterOnEntity (FUN_140103040)
// ============================================================
void PlayerToolManager::CenterOnEntity(UI::DialogInstance* const* dialog, ComponentEntity* const* entityPtr) {
    GameUpdateLoop* const game = GameUpdateLoop::_singleton;

    // === Section 1: Enable tracking on the dialog's EntityUI ===
    if (*dialog != nullptr) {
        // Retail FUN_140103040 reads DWORD_1401c4f00 — whose registrar is
        // RegisterType_EntityUI — TWICE, and both reads matter. Disassembled at
        // 0x14010307a..0x1401030af (the decompiled C shows only the second; it drops
        // GetController's third argument entirely):
        //
        //     MOV  R8D,[0x1401c4f00]   ; arg 3 = the EntityUI type-id FILTER
        //     MOV  RCX,[RBX]           ; this = DialogInstance*
        //     LEA  RDX,[RBP+0x6f]      ; hidden retval slot
        //     CALL [0x140151768]       ; UI::DialogInstance::GetController
        //     ...
        //     MOV  EDX,[0x1401c4f00]   ; and again, for IsDerivedFrom
        //     CALL [0x140152a98]       ; fn_IsDerivedFrom
        //
        // GetController's third parameter is a TYPE-ID FILTER, not an index: retail
        // 0x180113320 walks the controller array and compares the argument against
        // each controller's own type id, returning null when nothing matches.
        // (reference_getcontroller_third_arg_is_a_type_filter.)
        // Guard the VALUE, not the pointer: a null-but-resolvable id is not a valid filter.
        const uint32_t tidEntityUI = EntityUI::TypeId();   // cell 0x1401c4f00

        // Get the EntityUI controller from the dialog instance.
        UI::DialogController* controllerRaw = (*dialog)->GetController(tidEntityUI).Get();
        EntityUI* panelController = nullptr;
        if (controllerRaw != nullptr && tidEntityUI) {
            if (::Object::IsDerivedFromNullSafe(controllerRaw, tidEntityUI)) {
                panelController = static_cast<EntityUI*>(controllerRaw);
            }
        }

        if (panelController != nullptr) {
            // The panel's pin check box
            if (panelController->checkPin->_checked == 0) {
                // Pin the panel (it follows the entity)
                panelController->checkPin->_checked = 1;

                panelController->SyncPin();   // 0x1400666b0
            }
        }
    }

    // === Section 2: Get entity world position ===
    float targetPos[3] = {0.0f, 0.0f, 0.0f};

    ComponentEntity* entityRaw = *entityPtr;

    // Try ModelComponent (cell 0x1401c7a50). The tree's finders returned null for
    // a null entity: kept.
    ModelComponent* modelRef = entityRaw ? entityRaw->FindComponent<ModelComponent>() : nullptr;

    bool gotPosition = false;
    if (modelRef != nullptr) {
        // Has ModelComponent — check for MultiObjectComponent
        MultiObjectComponent* multiRef = entityRaw ? entityRaw->FindComponent<MultiObjectComponent>() : nullptr;

        if (multiRef == nullptr) {
            // Single object (citizen/animal/tree): the model's transform translation
            ModelComponent* model = entityRaw ? entityRaw->FindComponent<ModelComponent>() : nullptr;
            targetPos[0] = model->transform.translation.x;
            targetPos[1] = model->transform.translation.y;
            targetPos[2] = model->transform.translation.z;
            gotPosition = true;
        }
    }

    if (!gotPosition) {
        // Building/multi-object: calculate center from the MapComponent's footprint
        ComponentEntity* entity2 = *entityPtr;
        MapComponent* build = entity2 ? entity2->FindComponent<MapComponent>() : nullptr;

        Map* mapGrid = game->m_game;
        int gridSize = mapGrid->size;
        float halfGrid = (float)(gridSize >> 1);

        int16_t posX = build->tileX0;
        int16_t posY = build->tileY0;
        int16_t extX = build->tileX1;
        int16_t extY = build->tileY1;

        // Far corner of footprint
        int idx1 = (int)extX + ((int)extY + (int)posY) * gridSize + (int)posX;
        float inXY1[3] = {
            (float)(idx1 % gridSize) - halfGrid,
            (float)(idx1 / gridSize) - halfGrid,
            0.0f
        };
        float pos1[3];
        mapGrid->GetTerrainPos(pos1, inXY1, '\0', nullptr);   // 0x14009f3e0

        // Origin corner of footprint
        int idx2 = gridSize * (int)posY + (int)posX;
        float inXY2[3] = {
            (float)(idx2 % gridSize) - halfGrid,
            (float)(idx2 / gridSize) - halfGrid,
            0.0f
        };
        float pos2[3];
        mapGrid->GetTerrainPos(pos2, inXY2, '\0', nullptr);   // 0x14009f3e0

        // Midpoint of origin and far corner = footprint center
        targetPos[0] = (pos1[0] + pos2[0]) * 0.5f;
        targetPos[1] = (pos1[1] + pos2[1]) * 0.5f;
        targetPos[2] = (pos1[2] + pos2[2]) * 0.5f;
    }

    // === Section 3: Snap camera to entity position ===
    PlayerController* cameraSys = game->m_playerController;

    // Set camera target position
    cameraSys->target.x = targetPos[0];
    cameraSys->target.y = targetPos[1];
    cameraSys->target.z = targetPos[2];

    // Zero-duration time for instant snap (matches original)
    {
        System::Time timeVal;
        RtSystem_Time_Ctor(&timeVal, 0.0f);
    }

    // Compute camera view transform and apply
    {
        Math::Transform viewTransform = cameraSys->CalcTransform();   // 0x1400983f0

        // ?AsTransform3@Transform@Math@@QEBA?AVTransform3@2@XZ  retail 0x18001fcd0
        Math::Transform3 transform3 = viewTransform.AsTransform3();
        // ?SetCameraToWorld@Camera@Graphics@@QEAAXAEBVTransform3@Math@@@Z.
        cameraSys->camera.SetCameraToWorld(transform3);
    }

    // === Section 4: Select entity in EditTool ===
    // EditTool::TypeId() (cell 0x1401c6dd8).
    EditTool* editTool = nullptr;
    if (currentTool) {
        if (::Object::IsDerivedFromNullSafe(currentTool, EditTool::TypeId())) {
            editTool = static_cast<EditTool*>(currentTool.Get());
        }
    }

    if (editTool == nullptr) {
        // Switch to EditTool mode
        SelectTool(nullptr);

        // Re-read current tool after mode switch
        editTool = nullptr;
        if (currentTool) {
            if (::Object::IsDerivedFromNullSafe(currentTool, EditTool::TypeId())) {
                editTool = static_cast<EditTool*>(currentTool.Get());
            }
        }
    }

    // Select the entity in the EditTool
    if (editTool != nullptr) {
        Object::InternalPtr<ComponentEntity> entityRefCopy(*entityPtr);
        editTool->SelectEntity(&entityRefCopy);   // 0x1400fd3b0
    }
}

