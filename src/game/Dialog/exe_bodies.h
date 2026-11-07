#pragma once
// src/game/Dialog/exe_bodies.h -- declarations of the exe bodies DEFINED in
// src/game/Dialog/ (ENDGAME B.3R, block W0-P0c, B3R_RULES R5).
//
// Every body the registry reaches by retail address, declared once, copied
// from its DEFINITION (linkage included), grouped by defining file; the
// retail address(es) it is registered under are the trailing comment. The
// defining .cpp includes this header so the compiler checks each pair.
// Readers call these by name (W1); nothing here changes what runs. Making
// them members is B.4's work.
//
// Also here: the declarations and inline bodies of this module that lived in
// engine/funcs/engine_funcs.h, moved verbatim with their comments (that
// header is gone since B3R W2-EZ: its includers include this one directly).
//
// Declared in another header already (not redeclared here):
//   AutoSaveDialog_Slot4                   0x14005e120  (src/game/Dialog/AutoSaveDialog.h)
//   ToolbarDialog::ToolbarDialog           0x1400b9910  (src/game/Dialog/ToolbarDialog.h)

#include <cstdint>
#include "system/Value.h"   // System::Random; was reached through engine_funcs.h
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree (B3R Leftovers)
#include "ui/DialogController.h"     // SA_DialogEntityAlive
#include "Entity/ComponentEntity.h"                // ComponentEntity::_entityFlags
#include "Component/TrackerComponent.h"            // fn_TrackerComponent_Rebuild
#include "Component/ConsumeProduceComponent.h"

class MainMenuDialog;  // src/game/Dialog/MainMenuDialog.h
class TradeUI;         // src/game/Dialog/TradeUI.h

// ---- autosavedialog.cpp
namespace UI { class UserInterface; class Dialog; class DialogInstance; }
namespace Object { template <class T> class InternalPtr; }
class AutoSaveDialog;  // src/game/Dialog/AutoSaveDialog.h

// ---- citizenui.cpp
// B.4 P2 DLG-1: fn_FindComponent_CitizenAI (0x140011050) had no caller; deleted
// (callers use ComponentEntity::FindComponent<CitizenAIComponent>()).

// ---- edittool_select.cpp
// B.4 P2 DLG-1: EditTool_SelectEntity_Impl (0x1400fd3b0) had no caller left; deleted
// (callers use EditTool::SelectEntity, Player/EditTool.h).

// ---- entityui.cpp
// fn_EntityUI_SetEditMode (FUN_140102b60) is PlayerToolManager::SelectTool (Game/PlayerToolManager.h).

// fn_EntityUI_SelectEntity — reimplemented in entityui.cpp as EntityUI_SelectEntity_Impl
// FUN_140102e30: BST insert/update for entity tracking
class PlayerToolManager; class ComponentEntity;

// ---- eventnotifydialog.cpp
class EventNotifyDialog;  // src/game/Dialog/EventNotifyDialog.h

// ---- optionsdialog.cpp
// Free: reads the GameContainer (Game/game.h) through its first argument.
struct GameContainer;
class OptionsDialog;
namespace UI { class UserInterface; class Dialog; class DialogInstance; }
namespace Object { template <class T> class InternalPtr; }
// B4 FORWARD: 1 outside caller (Dialog/mainmenudialog.cpp) -> OptionsDialog's ctor

// ---- saveloaddialog.cpp
// B4 FORWARD: 1 outside caller file (Dialog/mainmenudialog.cpp) -> SaveLoadDialog's ctor
class SaveLoadDialog;
class MenuDialogLoopT;

// ---- storageui.cpp
// B.4 DLG-d: the item sort (0x1400213f0) is file-local to storageui.cpp, typed.

// ---- toolbar_utility_helpers.cpp
// B.4 DLG-d: 0x140100270 is PlayerToolManager::FindUtilityDialog (Game/PlayerToolManager.h).
// 0x140014890: the find-or-insert shared by the 0x38-node sets (one template
// core, typed wrappers per node). The InventoryNode wrapper is declared in
// Dialog/TownHallUI.h.
template <class Node> struct AvlTreeHead;   // Action/PathAvl.h
struct PinnedToolNode; struct PinnedToolEntry;   // Game/PlayerToolManager.h
PinnedToolNode* Avl38SetInsert(AvlTreeHead<PinnedToolNode>* set, const PinnedToolEntry& rec);   // 0x140014890  // B8: stays free -- already typed (B.4 B13): a typed overload per node type over the one template core BstInsertCore<Node> (0x140014890) (toolbar_utility_helpers.cpp); nothing erased

// ---- toolui.cpp
// B4 DLG-d: FUN_14012e100 is UIDescription::AddControllers (Component/UIDescription.h),
// its body in toolui.cpp.

// ---- townhall_helpers.cpp
class CitizenBehavior; class Profession; struct TownHallInventoryEntry;   // Entity/CitizenBehavior.h, Game/Profession.h, Dialog/TownHallUI.h
// TownHall_GroupRatingInit (0x14006abf0) / TownHall_GroupRatingUpdate (0x14006aed0):
// Dialog/TownHallUI.h (they take TownHallUI::RatingGroup). The two row-array
// pushes 0x140015800 / 0x1400159b0 are Array<Row>::SetSize (TownHallUI's members).
extern "C" void TownHall_InventorySortAsc(TownHallInventoryEntry* items, int count, void*, void*);  // 0x140015200  // B8: free in retail -- sort helper: RCX the item array base, EDX the count (0x14007ca2d MOV RCX,R12)
extern "C" void TownHall_InventorySortDesc(TownHallInventoryEntry* items, int count, void*, void*);  // 0x140015500  // B8: free in retail -- sort helper: RCX the item array base, EDX the count (0x14007ca44, as Asc)

// ---- townhallui.cpp
// B.4 DLG-e: the fifteen TownHallUI bodies are members (Dialog/TownHallUI.h).

// ---- tradeui_buyentries.cpp
class TradeComponent;

// ---- tradeui_checkout.cpp

// ---- tradeui_leaves.cpp
class TradeComponent;        // src/game/Component/TradeComponent.h
class ComponentDescription;  // src/game/Component/ComponentDescription.h
namespace System { class String; }
// B4 P2 DLG-3: TradeUI_StorageTree_Insert_e1c0 (0x14000e1c0) is declared typed in Dialog/TradeUI.h.

// ---- tradeui_price.cpp
class ComponentDescription;

// ---- tradeui_sort.cpp
// B4 P2 DLG-3: the two introsort drivers are declared typed in Dialog/TradeUI.h.


// ---- inline bodies (moved from engine/funcs/engine_funcs.h; no .cpp)

// ============================================================
// Shared SA dialog guard
// ============================================================
// Is a DialogController's data-source entity (+0x28) still safe to read? The
// weak ref is nulled when the entity is destroyed, and while the
// marked-for-removal flag (entity+0x40 bit 0x400) is set the entity survives
// until the EntityList drain with its components already torn down. Reading a
// roster/storage in either state crashed live 2026-07-20 (upgrade path
// demolishing a house with its panel open). Wrappers bail — the panel freezes
// until the dialog closes.
// (B.4 P2 DLG-1: the controller's +0x28 is DialogController::_dataSource; its
// +0x40 the entity's ComponentEntity::_entityFlags, read as the tree always did
// whatever the data source is.)
inline bool SA_DialogEntityAlive(const UI::DialogController* self) {
    const Object::Base* e = self->_dataSource;
    if (!e) return false;
    if (static_cast<const ComponentEntity*>(e)->_entityFlags & Bits(ComponentFlags::MarkedForRemoval))
        return false;
    return true;
}

// TrackerComponent rebuild: FUN_14000d9d0 — moves/rebuilds tracker item arrays
// Called when game world item count changes.
// param_1 = dst array descriptor, param_2 = src array descriptor: each a
// TrackerComponent::EntryList (Array<Entry>: _data, _size, _capacity).
// Frees old dst buffer, allocates count*16 bytes, copies 16-byte entries from src.
inline void fn_TrackerComponent_Rebuild(TrackerComponent::EntryList* dst, const TrackerComponent::EntryList* src) {
    if (dst == src) return;
    System::MemoryAllocator::MemFree(dst->_data);
    dst->_size = 0;          // the old qword store over count | capacity
    dst->_capacity = 0;
    dst->_data = nullptr;
    int count = src->_size;
    dst->_size = count;      // the qword store (uint32)count: capacity 0
    dst->_capacity = 0;
    if (count > 0) {
        // capacity = count on the fresh allocation
        dst->_capacity = count;
        dst->_data = static_cast<TrackerComponent::Entry*>(
            System::MemoryAllocator::MemAlloc((uint64_t)count * 16, 8));
    }
    // Copy entries
    if (dst->_size > 0) {
        TrackerComponent::Entry* dstBuf = dst->_data;
        const TrackerComponent::Entry* srcBuf = src->_data;
        int n = dst->_size;
        for (int i = 0; i < n; i++) {
            if (dstBuf) {
                dstBuf[i] = srcBuf[i];   // the 16 bytes (two qwords)
            }
        }
    }
}

// (0x140119470, the weighted recipe pick, is PickConsumeRecipe in Action/exe_bodies.h:
// row 443 (B7 RES) merged the second port that stood here into it.)
