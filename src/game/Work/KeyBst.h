// src/game/Work/KeyBst.h
//
// retail 0x14001a6e0 / 0x140025520 -- the 0x28-node AVL remove and find-remove of the exe's
// inlined 8-byte-key Set instances (`CitizenAI_impl_a6e0` / `CitizenAI_impl_5520`), as
// TEMPLATES over the node type (B8 D5; the shape of Work/CellBst.h, B7 LX). MSVC folded one
// body over every 0x28-node set the exe removes from (ICF): the Work leaves' citizen sets
// (Collection::Set<CitizenComponent*>), EntityList's two update trees
// (Collection::Set<ComponentEntity*>), the work scheduler's queue-order tree
// (AvlTreeHead<WorkSchedulerCountNode>) and Map's +0xE8 / +0x10 trees (AvlTreeHead<AvlNode28>).
// Every layout has the same links -- element +0x00, parent +0x08, left +0x10, right +0x18,
// balance +0x20, height +0x22 -- and the bodies read only those, the header's root (+0x00)
// and its dword count (+0x08): each caller instantiates them on its own node type.
//
// The retrace (0x14001a8c0) and its two rotations (0x140014c70 / 0x140014da0) are templates
// here too: the insert 0x140021a30 (Work/work_base_bst.cpp) stands on the same retrace.
//
// Retail takes the header in RCX and the node (remove) or the key's address (find-remove) in
// RDX. The node is freed with MemFree; its element is NOT destroyed (retail has no element
// destructor in this body).
#pragma once

#include <cstdint>
#include "collection/Collection.h"
#include "system/MemoryAllocator.h"
#include "io/Serializer.h"     // the set serializer (0x1400296b0)
#include "object/Internal.h"   // InternalPtr / InternalBasePtr::Serialize
#include "object/TypeInfo.h"   // Object::IsDerivedFromNullSafe
#include <type_traits>

template <class Node> struct AvlTreeHead;   // Action/PathAvl.h

// Recompute balance(+0x20) and height(+0x22) of node `n` from its children.
template <class Node>
inline void WorkBST_UpdateHB(Node* n) {
    if (n == nullptr) return;
    Node* l = n->left;
    Node* r = n->right;
    int16_t lh = (l == nullptr) ? 0 : static_cast<int16_t>(l->height + 1);
    int16_t rh = (r == nullptr) ? 0 : static_cast<int16_t>(r->height + 1);
    n->balance = static_cast<int16_t>(rh - lh);
    n->height = (rh < lh) ? lh : rh;
}

// retail 0x140014c70 -- left rotation about `node` (pivot = node->right).
template <class Node>
inline Node* WorkBST_RotateLeft(Node** root, Node* node) {
    if (node == *root) *root = node->right;
    Node* parent = node->parent;
    if (parent != nullptr) {
        if (parent->left == node)
            parent->left = node->right;
        else
            parent->right = node->right;
    }
    Node* pivot = node->right;
    pivot->parent = node->parent;
    Node* tmp = pivot->left;
    node->right = tmp;
    if (tmp != nullptr) tmp->parent = node;
    pivot->left = node;
    node->parent = pivot;
    WorkBST_UpdateHB(pivot->left);
    WorkBST_UpdateHB(pivot->right);
    WorkBST_UpdateHB(pivot);
    return pivot;
}

// retail 0x140014da0 -- right rotation about `node` (pivot = node->left).
template <class Node>
inline Node* WorkBST_RotateRight(Node** root, Node* node) {
    if (node == *root) *root = node->left;
    Node* parent = node->parent;
    if (parent != nullptr) {
        if (parent->left == node)
            parent->left = node->left;
        else
            parent->right = node->left;
    }
    Node* pivot = node->left;
    pivot->parent = node->parent;
    Node* tmp = pivot->right;
    node->left = tmp;
    if (tmp != nullptr) tmp->parent = node;
    pivot->right = node;
    node->parent = pivot;
    WorkBST_UpdateHB(pivot->left);
    WorkBST_UpdateHB(pivot->right);
    WorkBST_UpdateHB(pivot);
    return pivot;
}

// retail 0x14001a8c0 (CitizenAI_State_Leisure_sub_a8c0) -- AVL retrace: walk up the parent
// chain from *pWalk, recomputing balance/height and rotating where the balance leaves
// [-1,1]. The parent is captured before rotations so the walk follows the original ancestry.
template <class Node>
inline void WorkBST_Retrace(Node** root, Node** pWalk) {
    while (*pWalk != nullptr) {
        Node* node = *pWalk;
        Node* next = node->parent;   // parent, pre-rotation
        WorkBST_UpdateHB(node);
        node = *pWalk;
        if (node->balance > 1 && node->right != nullptr) {
            Node* rc = node->right;
            if (rc->balance < 0) {       // right-left case
                WorkBST_RotateRight(root, rc);
                node = *pWalk;
            }
            WorkBST_RotateLeft(root, node);
        }
        node = *pWalk;
        if (node->balance < -1 && node->left != nullptr) {
            Node* lc = node->left;
            if (lc->balance > 0) {       // left-right case
                WorkBST_RotateLeft(root, lc);
                node = *pWalk;
            }
            WorkBST_RotateRight(root, node);
        }
        *pWalk = next;
    }
}

// retail 0x14001a6e0 (CitizenAI_impl_a6e0) -- AVL delete of `node`.
// Handles the three classic cases (no right child; right child with a left subtree whose
// in-order predecessor is spliced in; right child but no left child), then retraces from
// the splice point.
template <class Node>
inline void WorkBST_BSTRemove_a6e0(Node*& root, int32_t& count, Node* node) {
    Node* local_res8 = node->parent;
    count -= 1;   // count--
    Node* lVar5 = node->right;

    if (lVar5 == nullptr) {
        Node* parent = node->parent;
        Node* left   = node->left;
        if (parent == nullptr) root = left;
        else if (parent->left == node) parent->left = left;
        else parent->right = left;
        lVar5 = node->left;          // surviving child = left
    } else {
        Node* lVar1 = node->left;
        if (lVar1 != nullptr) {
            // in-order predecessor = rightmost of left subtree
            Node* pred = lVar1;
            Node* walk = lVar1;
            do { pred = walk; walk = pred->right; } while (pred->right != nullptr);

            local_res8 = nullptr;
            if (pred != lVar1) {
                lVar5 = pred->left;
                local_res8 = pred->parent;
                local_res8->right = lVar5;          // pred->parent->right = pred->left
                if (lVar5 != nullptr) lVar5->parent = local_res8;
                pred->left = node->left;
                WorkBST_UpdateHB(local_res8);
            }
            lVar5 = local_res8;
            pred->right = node->right;
            if (pred->left != nullptr)
                pred->left->parent = pred;
            if (pred->right != nullptr)
                pred->right->parent = pred;
            Node* nodeParent = node->parent;
            pred->parent = nodeParent;
            Node* local_res10;
            if (nodeParent == nullptr) {
                root = pred;
                pred->parent = nullptr;
                local_res10 = root;
            } else {
                if (nodeParent->left == node)
                    nodeParent->left = pred;
                else
                    nodeParent->right = pred;
                local_res10 = pred;
            }
            System::MemoryAllocator::MemFree(node);
            if (lVar5 == nullptr) WorkBST_Retrace(&root, &local_res10);
            else WorkBST_Retrace(&root, &local_res8);
            return;
        }
        // node->left == 0, node->right != 0
        Node* parent = node->parent;
        if (parent == nullptr) root = lVar5;
        else if (parent->left == node) parent->left = lVar5;
        else parent->right = lVar5;
        lVar5 = node->right;          // surviving child = right
    }

    if (lVar5 != nullptr) lVar5->parent = node->parent;
    Node* local_res10 = lVar5;
    System::MemoryAllocator::MemFree(node);
    if (lVar5 == nullptr) WorkBST_Retrace(&root, &local_res8);
    else WorkBST_Retrace(&root, &local_res10);
}

// retail 0x140025520 (CitizenAI_impl_5520) -- find the node whose element equals *key
// (compared unsigned, as the pointer it is) and remove it.
template <class Node, class Key>
inline void WorkBST_BSTFindRemove_25520(Node*& root, int32_t& count, const Key* key) {
    Node* node = root;
    if (node == nullptr) return;
    do {
        if (*key < node->element) {
            node = node->left;
        } else {
            if (*key <= node->element) {
                WorkBST_BSTRemove_a6e0(root, count, node);
                return;
            }
            node = node->right;
        }
    } while (node != nullptr);
}

// The header each caller holds: retail's RCX, its root and count handed to the core.
template <class T>
inline void WorkBST_BSTRemove_a6e0(Collection::Set<T*>* set, typename Collection::Set<T*>::Node* node) {
    WorkBST_BSTRemove_a6e0(set->_root, set->_count, node);
}
template <class Node>
inline void WorkBST_BSTRemove_a6e0(AvlTreeHead<Node>* head, Node* node) {
    WorkBST_BSTRemove_a6e0(head->root, head->count, node);
}
template <class T>
inline void WorkBST_BSTFindRemove_25520(Collection::Set<T*>* set, T* const* key) {
    WorkBST_BSTFindRemove_25520(set->_root, set->_count, key);
}

// retail 0x140021b40 -- allocate + init a 0x28-byte leaf: the element from *key, the parent,
// both links null, balance + height 0 (one dword store in retail).
template <class Node, class Elem>
inline Node* WorkBST_AllocNode_1b40(const Elem* key, Node* parent) {
    Node* node = static_cast<Node*>(System::MemoryAllocator::MemAlloc(0x28, 8));
    if (!node) return nullptr;
    node->element = *key;
    node->parent = parent;
    node->left = nullptr;
    node->right = nullptr;
    node->balance = 0;   // balance + height: one dword store in retail
    node->height = 0;
    return node;
}

// retail 0x140021a30 -- AVL insert. key = *key (the pointer, compared unsigned); the inserted
// (or pre-existing) node is written to *out and `out` returned; the count is bumped on the
// insert arms only. The out slot is a node-pointer slot at every retail caller (a reused
// stack slot there; a typed local here).
template <class Node, class Elem>
inline Node** WorkBST_RegisterWork_1a30(Node*& root, int32_t& count, Node** out, const Elem* key) {
    if (root == nullptr) {
        Node* node = WorkBST_AllocNode_1b40(key, static_cast<Node*>(nullptr));
        root = node;
        *out = node;
    } else {
        Node* cur = root;
        Node* newLeaf = nullptr;
        for (;;) {
            // descend left while key < node->key
            for (;;) {
                if (*key < cur->element) {
                    if (cur->left == nullptr) {
                        newLeaf = WorkBST_AllocNode_1b40(key, cur);
                        cur->left = newLeaf;
                        goto done_insert;
                    }
                    cur = cur->left;
                } else {
                    break;
                }
            }
            if (*key <= cur->element) {
                *out = cur;   // key already present
                return out;
            }
            if (cur->right == nullptr) {
                newLeaf = WorkBST_AllocNode_1b40(key, cur);
                cur->right = newLeaf;
                goto done_insert;
            }
            cur = cur->right;
        }
    done_insert:
        Node* retraceFrom = cur;   // parent of the new leaf
        WorkBST_Retrace(&root, &retraceFrom);
        *out = newLeaf;
    }
    count += 1;   // count++
    return out;
}

// The header each caller holds is retail's RCX (one out-of-line body per instantiation, as
// retail's 0x140021a30 is called, never inlined: [[gnu::noipa]] keeps the call and its three
// arguments).
template <class T>
[[gnu::noipa]] typename Collection::Set<T*>::Node** WorkBST_RegisterWork_1a30(
        Collection::Set<T*>* set, typename Collection::Set<T*>::Node** out, T* const* key) {
    return WorkBST_RegisterWork_1a30(set->_root, set->_count, out, key);
}
template <class Node, class Elem>
[[gnu::noipa]] Node** WorkBST_RegisterWork_1a30(AvlTreeHead<Node>* head, Node** out, const Elem* key) {
    return WorkBST_RegisterWork_1a30(head->root, head->count, out, key);
}

// retail 0x1400296b0 -- serialize a citizen set: count(int), then `count` InternalPtrs.
// LOAD reads each key (InternalBasePtr::Serialize; a non-CitizenComponent is nulled) and
// inserts it through 0x140021a30; SAVE walks in order through the parent links, writing each
// node's element as the InternalBasePtr retail hands. `set` is the header the insert takes.
template <class Head, class Node>
inline void WorkBST_BST_Serialize_296b0(Head* set, Node*& root, int32_t& count, IO::Serializer* ser) {
    using Elem = typename std::remove_pointer<decltype(Node::element)>::type;   // CitizenComponent
    int n = count;
    ser->Serialize(n, nullptr);
    bool loading = (static_cast<int>(ser->GetDirection()) == 0);   // IO::Serializer::GetDirection, retail 0x180006210
    uint32_t citTid = Elem::TypeId();  // DAT_1401c7158

    if (loading) {
        for (int i = 0; i < n; i++) {
            // The key is the InternalPtr retail serializes through InternalBasePtr::Serialize
            // in one stack slot and hands to the insert by address (0x1400296fa .. 0x14002972b
            // LEA R8,[RSP+0x48]): the slot is the element type, viewed as the InternalBasePtr
            // it is, as the save arm below views the node's element.
            Elem* key = nullptr;
            Object::InternalBasePtr* keyIbp = reinterpret_cast<Object::InternalBasePtr*>(&key);   // the slot is the InternalPtr<CitizenComponent> retail serializes  // CAST-OK: elem_stride
            keyIbp->Serialize(*ser);
            // NOT RETAIL (F-143): null and type tests on the held Internal*, before the
            // slot is used as an Elem*; the answer is the same.
            if (!keyIbp->IsNull() && !Object::IsDerivedFromNullSafe(*keyIbp, citTid))   // 0x14002970a TEST; 0x140029715 IsDerivedFrom; CMOVZ
                key = nullptr;
            Node* out;   // the insert always writes it; retail never zeroes the slot (0x140029730)
            WorkBST_RegisterWork_1a30(set, &out, &key);
        }
        return;
    }

    // SAVE: iterative in-order traversal via parent links.
    Node* pIVar6 = root;
    if (pIVar6 == nullptr) return;
    Node* pIVar3;
    Node* pIVar1;
    do { pIVar3 = pIVar6; pIVar6 = pIVar3->left; } while (pIVar6 != nullptr);  // leftmost
joined:
    while ((pIVar6 = pIVar3) != nullptr) {
        // The key is an InternalPtr retail serializes through InternalBasePtr::Serialize.
        Object::InternalBasePtr* elemIbp = reinterpret_cast<Object::InternalBasePtr*>(&pIVar6->element);   // the element is the InternalPtr<CitizenComponent> retail serializes  // CAST-OK: elem_stride
        elemIbp->Serialize(*ser);
        // NOT RETAIL (F-143): as the load arm, the tests run on the held Internal*.
        if (!elemIbp->IsNull() && !Object::IsDerivedFromNullSafe(*elemIbp, citTid))   // 0x14002978f / 0x14002979a
            pIVar6->element = nullptr;
        pIVar1 = pIVar6->right;   // right child
        if (pIVar1 == nullptr) {
            pIVar3 = pIVar6->parent;  // parent
            if (pIVar3 != nullptr) goto succ;
            goto joined;                                         // root reached (pIVar3==0) -> terminate
        }
        do { pIVar3 = pIVar1; pIVar1 = pIVar3->left; } while (pIVar1 != nullptr);  // leftmost of right subtree
    }
    return;
succ:
    if (pIVar3->left != pIVar6) {
        for (;;) {
            pIVar1 = pIVar3;
            if (pIVar1 == nullptr || pIVar1->right != pIVar6) break;
            pIVar6 = pIVar1;
            pIVar3 = pIVar1->parent;
        }
        pIVar3 = pIVar6->parent;
    }
    goto joined;
}
template <class T>
[[gnu::noipa]] void WorkBST_BST_Serialize_296b0(Collection::Set<T*>* set, IO::Serializer* ser) {
    WorkBST_BST_Serialize_296b0(set, set->_root, set->_count, ser);
}
template <class Node>
[[gnu::noipa]] void WorkBST_BST_Serialize_296b0(AvlTreeHead<Node>* head, IO::Serializer* ser) {
    WorkBST_BST_Serialize_296b0(head, head->root, head->count, ser);
}
