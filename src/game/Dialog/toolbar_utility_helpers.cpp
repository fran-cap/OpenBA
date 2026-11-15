// src/game/Dialog/toolbar_utility_helpers.cpp
// GUI_PLAN Keystone F, Phase 4 — Utility dialogs (toolbar case 2).
//
// Two exe helpers that PlayerToolManager::ToggleUtilityDialog (toolbardialog.cpp,
// FUN_140101a80) calls: PlayerToolManager::FindUtilityDialog and Avl38SetInsert.
//
//   FUN_140100270  FindDialogByControllerType — walk the live UI dialog list and
//     return the open dialog whose ToolUI controller's +0x30 (its Description
//     pointer) == param. Used to detect an already-open utility dialog (toggle).
//   FUN_140014890  BST_Insert_0x14 — AVL find-or-insert of a 0x38-byte
//     utility-dialog position node keyed by the Description ptr; rebalances via
//     the shared retrace 0x140014b40 (Avl38Retrace).
//
// Faithful 1:1 against the Ghidra disassembly (offsets verified).

#include "Player/exe_bodies.h"   // Avl38Retrace (0x140014b40)
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/PlayerToolManager.h"   // PinnedToolNode / PinnedToolEntry
#include "Dialog/TownHallUI.h"         // TownHallUI::InventoryNode / InventoryEntry
#include "Object/factory_registration.h"
#include "ui/UserInterface.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "ui/DialogInstance.h"  // ENDGAME B.3.6 UI callers (K3)
#include "ui/UserInterface.h"  // ENDGAME B.3.6 UI callers (K3)
#include <new>  // ENDGAME B.3.6 UI callers (K3)
#include <type_traits>  // BstInsertCore's node type
#include "Dialog/ToolUI.h"   // X::TypeId() (B.3.8 callers)
#include "Dialog/exe_bodies.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc

namespace {

// Retrace/rebalance (0x140014b40, TradeWork_impl_4b40) — shared AVL retrace,
// called by name (B3R).

// B.4 Y GAMEB: 0x140014890 is ONE body that serves every set of 0x38-byte nodes
// whose 0x18-byte element is copied whole (B13): the core is a template over
// the node, whose element is its base class (PinnedToolNode : PinnedToolEntry,
// TownHallUI::InventoryNode : InventoryEntry); the typed wrappers below are the
// only entry points.

// sub_4a90 — allocate + init a new 0x38-byte AVL node (copies the 0x18-byte
// element, sets parent, zeroes children + balance).
template <class Node, class Element>
Node* BstNodeAlloc(const Element& rec, Node* parent) {
    Node* n = static_cast<Node*>(System::MemoryAllocator::MemAlloc(0x38, 8));
    if (!n) return nullptr;
    Element& element = *n;
    element = rec;                        // +0x00..+0x17 (three qword copies in retail)
    n->parent = parent;
    n->left = nullptr;
    n->right = nullptr;
    n->balance = 0;     // +0x30 / +0x32: one dword 0 in retail (H7)
    n->height = 0;
    return n;
}

// ---- FUN_140014890: BST_Insert_0x14 ----------------------------------------
// (RCX=set, RDX=retNode, R8=the element)
//   set:      the set header, root node and int32 count
//   returns:  the found-or-inserted node (retail: *retNode)
// Faithful AVL find-or-insert keyed by the element's +0x00 (compared as an
// unsigned 64-bit value); rebalances from the insertion point via 0x140014b40.
// SetT is the set header {Node* root, int32 count} (AvlTreeHead<Node>, or
// TownHallUI::OwnedTree<Node> for the inventory tree).
template <class SetT, class Element>
auto BstInsertCore(SetT* set, const Element& rec) -> decltype(set->root) {
    using Node = typename std::remove_pointer<decltype(set->root)>::type;
    Node* result = nullptr;
    if (set->root == nullptr) {
        // Empty tree — the new node becomes the root.
        Node* n = static_cast<Node*>(System::MemoryAllocator::MemAlloc(0x38, 8));
        if (!n) {
            set->root = nullptr;
            result = nullptr;
        } else {
            Element& element = *n;
            element = rec;
            n->parent = nullptr; n->left = nullptr; n->right = nullptr;
            n->balance = 0;
            n->height = 0;
            result = n;
            set->root = n;
        }
    } else {
        const void* const key = rec.key;
        Node* next = set->root;
        Node* node = nullptr;      // current node examined (== insertion parent)
        Node* newNode = nullptr;
        bool inserted = false;

        while (!inserted) {
            node = next;
            // Walk right while node->key < key.
            for (;;) {
                const void* const nodeKey = node->key;
                if (!(nodeKey <= key)) break;
                if (key <= nodeKey) {      // equal → existing node found (no count++)
                    return node;
                }
                if (node->right == nullptr) {   // right child null → insert right
                    newNode = BstNodeAlloc(rec, node);
                    node->right = newNode;
                    inserted = true;
                    break;
                }
                next = node->right;
                node = next;
            }
            if (inserted) break;
            // node->key > key → go left.
            if (node->left == nullptr) {     // left child null → insert left
                newNode = BstNodeAlloc(rec, node);
                node->left = newNode;
                inserted = true;
                break;
            }
            next = node->left;
        }

        Avl38Retrace(&set->root, &node);   // 0x140014b40
        result = newNode;
    }

    // count — incremented on every non-early-return path, matching the exe
    // (even the alloc-failure sub-case).
    set->count += 1;
    return result;
}

}  // namespace

// ---- FUN_140100270: FindDialogByControllerType ------------------------------
// The open dialog whose ToolUI controller's +0x30 (its description) is
// `description`, or null. Used to detect an already-open utility dialog (toggle).
UI::DialogInstance* PlayerToolManager::FindUtilityDialog(const ComponentDescription* description) {
    GameUpdateLoop* game = GameUpdateLoop::_singleton;
    if (!game) return nullptr;
    UI::UserInterface* ui = game->m_userInterface;
    if (!ui) return nullptr;

    // ToolUI type-id (DWORD_1401c5788, RegisterType_ToolUI writer), cached.
    static uint32_t s_toolUITid = 0;
    if (s_toolUITid == 0) s_toolUITid = ToolUI::TypeId();
    const uint32_t toolUITid = s_toolUITid;

    // Walk the dialog linked list. `_dialogs.GetFirst()` IS retail's
    // GetFirstDialog (0x180031150 = `*retval = this->_dialogs._head`), and the
    // iterator is the bare node pointer, so `it.node` is the address retail
    // hands to CheckPtr and `it.node->element` is the DialogInstance.
    for (auto it = ui->_dialogs.GetFirst(); it; ++it) {
        UI::DialogInstance* dialog = it.node->element;

        // GetController(ToolUI tid), narrowed to a ToolUI
        const ToolUI* controller = nullptr;
        if (dialog) {
            UI::DialogController* c = dialog->GetController(toolUITid).Get();
            if (c && ::Object::IsDerivedFromNullSafe(c, toolUITid)) controller = static_cast<const ToolUI*>(c);
        }

        if (controller != nullptr && controller->description == description) {
            return dialog;
        }
    }
    return nullptr;
}

// The typed wrappers (Dialog/exe_bodies.h, Dialog/TownHallUI.h) over the one core 0x140014890.
PinnedToolNode* Avl38SetInsert(AvlTreeHead<PinnedToolNode>* set, const PinnedToolEntry& rec) {
    return BstInsertCore(set, rec);
}
TownHallUI::InventoryNode* Avl38SetInsert(TownHallUI::OwnedTree<TownHallUI::InventoryNode>* set,
                                          const TownHallUI::InventoryEntry& rec) {
    return BstInsertCore(set, rec);
}
