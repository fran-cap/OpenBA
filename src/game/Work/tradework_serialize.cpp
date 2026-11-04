// src/game/Work/tradework_serialize.cpp
// TradeWork's assignment set (Work/TradeWork.h): the insert and the serialize the
// post's slots call. Faithful 1:1 ports of the retail bodies (Ghidra disassembly-
// verified, offset by offset); B4 WORK-c made them members reading the typed
// TradeWork::Node / Assignment by name.
//
// AVL node (TradeWork::Node, 0x38 bytes) — the same engine set template whose
// rotation / retrace / remove leaves src/game/Player/tool_base_trade.cpp ports:
//   +0x00 Assignment {citizen, resource, quantity}   +0x18 parent
//   +0x20 left   +0x28 right   +0x30 int16 balance   +0x32 int16 height
// Tree: TradeWork::bstRoot (+0x48) and its count TradeWork::workerCount (+0x50).
//
// Type-id global (reads 0 until filled after Engine::Startup):
//   DWORD_1401c7158 = "CitizenComponent" (node-key object validation in c8a0).

#include "Work/TradeWork.h"
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"
#include "Game/exe_bodies.h"        // B3R: 0x140002340
#include "Player/exe_bodies.h"      // Avl38Retrace (0x140025920)
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "Component/CitizenComponent.h"   // ENDGAME B.3.9 D5: CitizenComponent::TypeId()
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)


// ============================================================
// 0x14002c830 — the insert's node allocation. Allocates a 0x38-byte node, copies
// the 0x18-byte record (citizen / resource / quantity) from `rec`, links `parent`,
// and zeroes the child / balance fields. RCX (the descent node) is unused by the body.
// Returns the new node (or null on OOM).
// ============================================================
TradeWork::Node* TradeWork::AllocNode(const Assignment& rec, Node* parent) {
    Node* n = static_cast<Node*>(System::MemoryAllocator::MemAlloc(0x38, 8));
    if (n == nullptr) return nullptr;
    n->value.citizen = nullptr;
    n->value = rec;          // the full 0x18 bytes (quantity's qword copied as in disasm)
    n->parent = parent;
    n->left = nullptr;
    n->right = nullptr;
    n->balance = 0;          // retail: one dword store over balance + height
    n->height = 0;
    return n;
}

// ============================================================
// 0x14002c710 — AVL insert keyed on the record's citizen pointer (unsigned
// compare). RCX = &bstRoot, RDX = the out slot, R8 = the record.
// ============================================================
TradeWork::Node* TradeWork::InsertAssignment(const Assignment& rec) {
    Node* inserted;
    if (assignments.root == nullptr) {
        // Empty tree: allocate the root node inline (matches the decompile's MemAlloc
        // here rather than via the c830 sub-helper).
        Node* n = static_cast<Node*>(System::MemoryAllocator::MemAlloc(0x38, 8));
        if (n != nullptr) {
            n->value.citizen = nullptr;
            n->value = rec;
            n->parent = nullptr;
            n->left = nullptr;
            n->right = nullptr;
            n->balance = 0;
            n->height = 0;
        }
        assignments.root = n;
        inserted = n;
    } else {
        CitizenComponent* key = rec.citizen.Get();
        Node* cur = assignments.root;
        Node* cursor;     // local_res8 — descent node (parent of the new node)
        for (;;) {
            cursor = cur;
            if (key < cur->value.citizen.Get()) {
                // descend LEFT (+0x20)
                if (cur->left == nullptr) {
                    inserted = AllocNode(rec, cursor);
                    cursor->left = inserted;
                    break;
                }
                cur = cur->left;
                continue;
            }
            if (key <= cursor->value.citizen.Get()) {
                // exact match — return the existing node, no structural change, no count.
                return cursor;
            }
            // descend RIGHT (+0x28)
            if (cursor->right == nullptr) {
                inserted = AllocNode(rec, cursor);
                cursor->right = inserted;
                break;
            }
            cur = cursor->right;
        }
        // Rebalance from the descent node (parent of the new node) up to the root.
        // Matches the disasm: retrace cursor = &local_res8 (the parent), not the new node.
        Node* retraceCursor = cursor;
        Avl38Retrace(&assignments.root, &retraceCursor);   // 0x140025920
    }

    assignments.count = assignments.count + 1;
    return inserted;
}

// ============================================================
// 0x14002c8a0 — the set's serialize (in-order). RCX = &bstRoot, RDX = the
// Serializer. Read mode: the serializer's direction is 0.
// ============================================================
void TradeWork::SerializeAssignments(IO::Serializer& ser) {
    // Serialize the node count (read or write).
    int32_t count = assignments.count;
    ser.Serialize(count, nullptr);

    bool readMode = (static_cast<int>(ser.GetDirection()) == 0);   // IO::Serializer::GetDirection, retail 0x180006210

    if (readMode) {
        // ---- read: deserialize `count` records, rebuild the tree ----
        for (int i = 0; i < count; ++i) {
            Assignment rec;
            rec.citizen = nullptr; rec.resource = nullptr; rec.quantity = 0; rec._pad14 = 0;

            // LEFT: the key is an InternalPtr retail serializes through
            // InternalBasePtr::Serialize; the field is declared T*.
            rec.citizen.InternalBasePtr::Serialize(ser);
            // Validate the deserialized object against CitizenComponent (DWORD_1401c7158,
            // ENDGAME B.3.9 D5: by cell, read directly).
            if (rec.citizen != nullptr &&
                !Object::IsDerivedFromNullSafe(rec.citizen, CitizenComponent::TypeId())) {
                rec.citizen = nullptr;
            }
            ser.Serialize(rec.quantity, nullptr);
            // The ExternalPtr (0x140002340 = fn_ExternalPtr_BuildSerialize).
            ::Object::SerializeExternal<ComponentDescription>(ser, rec.resource);

            InsertAssignment(rec);
        }
    } else {
        // ---- write: walk the AVL in-order, serializing each node ----
        Node* node = assignments.root;
        if (node == nullptr) return;
        // Descend to the leftmost (minimum) node.
        Node* n = node;
        do {
            node = n;
            n = node->left;
        } while (n != nullptr);

        while (node != nullptr) {
            // Serialize this node's citizen (+0x00), quantity (+0x10), resource (+0x08).
            node->value.citizen.InternalBasePtr::Serialize(ser);
            if (node->value.citizen != nullptr &&
                !Object::IsDerivedFromNullSafe(node->value.citizen, CitizenComponent::TypeId())) {
                node->value.citizen = nullptr;
            }
            ser.Serialize(node->value.quantity, nullptr);
            ::Object::SerializeExternal<ComponentDescription>(ser, node->value.resource);

            // In-order successor.
            Node* right = node->right;
            if (right != nullptr) {
                // leftmost of the right subtree
                Node* s = right;
                do {
                    node = s;
                    s = node->left;
                } while (s != nullptr);
            } else {
                // climb until we come up from a left child
                Node* parent = node->parent;
                if (parent == nullptr) {
                    node = nullptr;
                } else if (parent->left == node) {
                    node = parent;
                } else {
                    // climb while node is a right child
                    Node* p = parent;
                    while (p != nullptr && p->right == node) {
                        node = p;
                        p = p->parent;
                    }
                    node = node->parent;
                }
            }
        }
    }
}
