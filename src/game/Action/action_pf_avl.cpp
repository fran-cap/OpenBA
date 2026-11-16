// src/game/Action/action_pf_avl.cpp
// Pathfinding/Action closure — AVL/BST node + rotation family.
//
// Shared by the pathfinder's open set, the region-pair cache, the path-cost cache
// and the trees of other modules (through the template instantiations below). Transcribed
// 1:1 from the Ghidra decompile (cross-checked against disassembly for the rotation
// pointer surgery, the insert child-pointer offsets, and the balance-factor / height
// fixups), so it stays a LITERAL transcription, not an "improvement".
//
// B.4 ACT-a: the bodies run over a NODE TYPE (src/game/Action/PathAvl.h). Retail's two
// copies of each primitive (one per element width, the link offsets baked in) are one
// template here, instantiated for every node the tree uses:
//   0x14000e450 / 0x140014c70  AvlRotateLeft      0x14000e580 / 0x140014da0  AvlRotateRight
//   0x140013480 / 0x14001a8c0  AvlRebalance
#include "Action/exe_bodies.h"
#include "Action/PathAvl.h"
#include "Action/PathSearch.h"          // PathOpenNode

#include "Component/exe_bodies.h"      // CitizenAI_Reg_helper_e360
#include "Dialog/TownHallUI.h"         // TownHallUI::ProfessionNode / AgricultureNode
#include "Entity/CitizenBehavior.h"    // PathGraph
#include "Game/WorkSchedulerNodes.h"   // WorkSchedulerIdNode, WorkCandidateNode
#include "Game/LegacyKeyIntNode.h"     // LegacyKeyIntNode
#include "Component/TradeComponent.h"  // TradeResourceNode
#include "Work/Work.h"                 // WorkEntitySetNode
#include "Work/HarvestArea.h"          // HarvestArea::Claim (B7 LX: 0x14001ca60's cell set)
#include "Work/HarvestOrchard.h"       // HarvestOrchard::Claim (likewise)
#include "Component/LeisureCandidateSet.h"   // LeisureHomeNode
#include "Player/PathTool.h"           // PathSegmentNode (PathTool::SetSelected's dedup trees)
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc
#include <cstdint>

namespace {

// A child's contribution to its parent's height: child->height + 1, or 0 for none.
template <class Node>
inline int16_t ChildHeight(const Node* child) {
    return (child == nullptr) ? 0 : static_cast<int16_t>(child->height + 1);
}

} // namespace

// ===========================================================================
// Rotations (0x14000e450 / 0x14000e580 for the 0x30 node, 0x140014c70 / 0x140014da0 for
// the 0x28 node). Each re-derives the balance / height of the promoted node's two
// children, then of the promoted node itself, in that order.
// ===========================================================================

// 0x14000e450 / 0x140014c70 — the right child is promoted. Returns it.
template <class Node>
Node* AvlRotateLeft(Node** root, Node* node)
{
    Node* pivot;
    Node* tmp;
    int16_t s3, s4, s5, s6;

    if (node == *root) {
        *root = node->right;
    }
    pivot = node->parent;
    if (pivot != nullptr) {
        if (pivot->left == node) {
            pivot->left = node->right;
        } else {
            pivot->right = node->right;
        }
    }
    pivot = node->right;                   // becomes the new subtree root
    pivot->parent = node->parent;
    tmp = pivot->left;
    node->right = tmp;
    if (tmp != nullptr) {
        tmp->parent = node;
    }
    pivot->left = node;
    node->parent = pivot;

    // height/balance fixup for pivot->left
    tmp = pivot->left;
    s5 = 0;
    if (tmp != nullptr) {
        s6 = ChildHeight(tmp->left);
        s4 = ChildHeight(tmp->right);
        s3 = s4;
        if (s4 < s6) s3 = s6;
        tmp->balance = static_cast<int16_t>(s4 - s6);
        tmp->height = s3;
    }
    // height/balance fixup for pivot->right
    tmp = pivot->right;
    if (tmp != nullptr) {
        s6 = ChildHeight(tmp->left);
        s4 = ChildHeight(tmp->right);
        s3 = s4;
        if (s4 < s6) s3 = s6;
        tmp->balance = static_cast<int16_t>(s4 - s6);
        tmp->height = s3;
    }
    // height/balance fixup for pivot itself
    s6 = ChildHeight(pivot->left);
    if (pivot->right != nullptr) {
        s5 = static_cast<int16_t>(pivot->right->height + 1);
    }
    s4 = s5;
    if (s5 < s6) s4 = s6;
    pivot->balance = static_cast<int16_t>(s5 - s6);
    pivot->height = s4;
    return pivot;
}

// 0x14000e580 / 0x140014da0 — the left child is promoted. Returns it.
template <class Node>
Node* AvlRotateRight(Node** root, Node* node)
{
    Node* pivot;
    Node* tmp;
    int16_t s3, s4, s5, s6;

    if (node == *root) {
        *root = node->left;
    }
    pivot = node->parent;
    if (pivot != nullptr) {
        if (pivot->left == node) {
            pivot->left = node->left;
        } else {
            pivot->right = node->left;
        }
    }
    pivot = node->left;                    // becomes the new subtree root
    pivot->parent = node->parent;
    tmp = pivot->right;
    node->left = tmp;
    if (tmp != nullptr) {
        tmp->parent = node;
    }
    pivot->right = node;
    node->parent = pivot;

    tmp = pivot->left;
    s5 = 0;
    if (tmp != nullptr) {
        s6 = ChildHeight(tmp->left);
        s4 = ChildHeight(tmp->right);
        s3 = s4;
        if (s4 < s6) s3 = s6;
        tmp->balance = static_cast<int16_t>(s4 - s6);
        tmp->height = s3;
    }
    tmp = pivot->right;
    if (tmp != nullptr) {
        s6 = ChildHeight(tmp->left);
        s4 = ChildHeight(tmp->right);
        s3 = s4;
        if (s4 < s6) s3 = s6;
        tmp->balance = static_cast<int16_t>(s4 - s6);
        tmp->height = s3;
    }
    s6 = ChildHeight(pivot->left);
    if (pivot->right != nullptr) {
        s5 = static_cast<int16_t>(pivot->right->height + 1);
    }
    s4 = s5;
    if (s5 < s6) s4 = s6;
    pivot->balance = static_cast<int16_t>(s5 - s6);
    pivot->height = s4;
    return pivot;
}

// ===========================================================================
// 0x140013480 / 0x14001a8c0 — rebalance driver. Walks from *cursor up to the root,
// recomputing balance/height and rotating, with the double-rotation pre-step when the
// child leans the opposite way.
// ===========================================================================
template <class Node>
void AvlRebalance(Node** root, Node** cursor)
{
    Node* up;
    Node* child;
    int16_t s3, s4, s5;
    Node* cur;

    up = *cursor;
    while (up != nullptr) {
        cur = *cursor;
        up = cur->parent;                  // next step up
        s5 = ChildHeight(cur->left);
        s4 = ChildHeight(cur->right);
        s3 = s4;
        if (s4 < s5) s3 = s5;
        cur->balance = static_cast<int16_t>(s4 - s5);
        cur->height = s3;

        cur = *cursor;
        if (1 < cur->balance) {
            child = cur->right;
            if (child != nullptr) {
                if ((child->balance < 1) && (child->balance < 0)) {
                    AvlRotateRight(root, child);
                    cur = *cursor;
                }
                AvlRotateLeft(root, cur);
            }
        }
        cur = *cursor;
        if (cur->balance < -1) {
            child = cur->left;
            if (child != nullptr) {
                if ((-1 < child->balance) && (0 < child->balance)) {
                    AvlRotateLeft(root, child);
                    cur = *cursor;
                }
                AvlRotateRight(root, cur);
            }
        }
        *cursor = up;
    }
}

// The node types the tree hands these primitives.
template PathOpenNode* AvlRotateLeft<PathOpenNode>(PathOpenNode**, PathOpenNode*);
template PathOpenNode* AvlRotateRight<PathOpenNode>(PathOpenNode**, PathOpenNode*);
template void AvlRebalance<PathOpenNode>(PathOpenNode**, PathOpenNode**);
template AvlNode30* AvlRotateLeft<AvlNode30>(AvlNode30**, AvlNode30*);
template AvlNode30* AvlRotateRight<AvlNode30>(AvlNode30**, AvlNode30*);
template void AvlRebalance<AvlNode30>(AvlNode30**, AvlNode30**);
template AvlNode28* AvlRotateLeft<AvlNode28>(AvlNode28**, AvlNode28*);
template AvlNode28* AvlRotateRight<AvlNode28>(AvlNode28**, AvlNode28*);
template void AvlRebalance<AvlNode28>(AvlNode28**, AvlNode28**);
template PathSegmentNode* AvlRotateLeft<PathSegmentNode>(PathSegmentNode**, PathSegmentNode*);
template PathSegmentNode* AvlRotateRight<PathSegmentNode>(PathSegmentNode**, PathSegmentNode*);
// The 0x30 nodes of other modules' trees (retail: the same 0x14000e450 / 0x14000e580 /
// 0x140013480 bodies, B13).
template void AvlRebalance<WorkSchedulerIdNode>(WorkSchedulerIdNode**, WorkSchedulerIdNode**);
template void AvlRebalance<WorkEntitySetNode>(WorkEntitySetNode**, WorkEntitySetNode**);
// B7 LX: the cell sets 0x14001ca60 removes from (Work/CellBst.h, one instantiation per node).
template void AvlRebalance<WorkCellSetNode>(WorkCellSetNode**, WorkCellSetNode**);
template void AvlRebalance<Collection::Set<HarvestArea::Claim>::Node>(Collection::Set<HarvestArea::Claim>::Node**, Collection::Set<HarvestArea::Claim>::Node**);
template void AvlRebalance<Collection::Set<HarvestOrchard::Claim>::Node>(Collection::Set<HarvestOrchard::Claim>::Node**, Collection::Set<HarvestOrchard::Claim>::Node**);
// The 0x28 nodes of other modules' trees (retail: the same 0x14001a8c0 body, B13): the work
// scheduler's +0x10 count tree and CitizenAI leisure's home set.
template void AvlRebalance<WorkSchedulerCountNode>(WorkSchedulerCountNode**, WorkSchedulerCountNode**);
template void AvlRebalance<LeisureHomeNode>(LeisureHomeNode**, LeisureHomeNode**);
template WorkCandidateNode* AvlRotateLeft<WorkCandidateNode>(WorkCandidateNode**, WorkCandidateNode*);
template WorkCandidateNode* AvlRotateRight<WorkCandidateNode>(WorkCandidateNode**, WorkCandidateNode*);
// The other trees of 0x14000e360's walk (CitizenAI_Reg_helper_e360): the trade stock tree
// and CitizenList's version < 2 key -> int tree.
template TradeResourceNode* AvlRotateLeft<TradeResourceNode>(TradeResourceNode**, TradeResourceNode*);
template TradeResourceNode* AvlRotateRight<TradeResourceNode>(TradeResourceNode**, TradeResourceNode*);
template LegacyKeyIntNode* AvlRotateLeft<LegacyKeyIntNode>(LegacyKeyIntNode**, LegacyKeyIntNode*);
template LegacyKeyIntNode* AvlRotateRight<LegacyKeyIntNode>(LegacyKeyIntNode**, LegacyKeyIntNode*);
// TownHallUI's profession tree (0x30 node: 0x140013480) and agriculture trees (0x28
// node: 0x1400149a0, retail's third copy of the 0x28 retrace, same rotations
// 0x140014c70 / 0x140014da0).
template void AvlRebalance<TownHallUI::ProfessionNode>(TownHallUI::ProfessionNode**, TownHallUI::ProfessionNode**);
template void AvlRebalance<TownHallUI::AgricultureNode>(TownHallUI::AgricultureNode**, TownHallUI::AgricultureNode**);

// ===========================================================================
// Node allocators
// ===========================================================================

// 0x14001a680 — alloc+init a 0x28 node. RCX is never read (retail keeps the caller's
// value); RDX -> key, R8 = parent. Returns the new node, or null on OOM.
AvlNode28* AvlNewNode28(const int32_t* key, AvlNode28* parent)
{
    AvlNode28* node = static_cast<AvlNode28*>(System::MemoryAllocator::MemAlloc(0x28, 8));
    if (node == nullptr) {
        return nullptr;
    }
    int32_t k = *key;
    node->parent = parent;                 // +0x08
    node->key = k;                         // +0x00
    node->left = nullptr;                  // +0x10
    node->right = nullptr;                 // +0x18
    node->balance = 0;                     // +0x20 / +0x22: one dword store (H7: two stores)
    node->height = 0;
    return node;
}

// 0x14001cc40 — alloc+init a 0x30 node. RCX is never read; RDX -> the 3-int key,
// R8 = parent. Returns the new node, or null on OOM.
AvlNode30* AvlNewNode30(const int32_t* key, AvlNode30* parent)
{
    AvlNode30* node = static_cast<AvlNode30*>(System::MemoryAllocator::MemAlloc(0x30, 8));
    if (node == nullptr) {
        return nullptr;
    }
    node->key0 = key[0];                   // +0x00
    node->key1 = key[1];                   // +0x04
    node->key2 = key[2];                   // +0x08
    node->parent = parent;                 // +0x10
    node->left = nullptr;                  // +0x18
    node->right = nullptr;                 // +0x20
    node->balance = 0;                     // +0x28 / +0x2a: one dword store (H7: two stores)
    node->height = 0;
    return node;
}

// ===========================================================================
// Inserts
// ===========================================================================

// 0x14001c940 — insert-or-find for a 0x30 node tree ordered by (key0, key1). tree =
// {root, count}; out = the node found or made; key = {key0, key1, key2}. Calls
// 0x14001cc40 (alloc) and 0x140013480 (rebalance).
AvlNode30** AvlInsert30(AvlTreeHead<AvlNode30>* tree, AvlNode30** out, const int32_t* key)
{
    int32_t    k;
    int32_t    curKey;
    AvlNode30* cur;
    AvlNode30* walk;
    AvlNode30* fresh;
    AvlNode30* nn;
    AvlNode30* local_res8;

    if (tree->root == nullptr) {
        fresh = static_cast<AvlNode30*>(System::MemoryAllocator::MemAlloc(0x30, 8));
        if (fresh == nullptr) {
            tree->root = nullptr;
            *out = nullptr;
        } else {
            k = key[0];
            *out = fresh;
            fresh->key0 = k;
            fresh->key1 = key[1];
            fresh->key2 = key[2];
            fresh->parent = nullptr;
            fresh->left = nullptr;
            fresh->right = nullptr;
            fresh->balance = 0;            // +0x28 / +0x2a: one dword store (H7: two stores)
            fresh->height = 0;
            tree->root = fresh;
        }
    } else {
        k = key[0];
        walk = tree->root;
        do {
            while (local_res8 = walk, cur = local_res8, curKey = local_res8->key0,
                   curKey <= k) {
                if (k <= curKey) {
                    if (key[1] < local_res8->key1) break;
                    if ((k <= curKey) &&
                        ((k < curKey) || (key[1] <= local_res8->key1))) {
                        *out = local_res8;
                        return out;
                    }
                }
                walk = local_res8->right;
                if (local_res8->right == nullptr) {
                    nn = AvlNewNode30(key, local_res8);
                    cur->right = nn;
                    goto LAB_14001ca2a;
                }
            }
            walk = local_res8->left;
        } while (local_res8->left != nullptr);
        nn = AvlNewNode30(key, local_res8);
        cur->left = nn;
LAB_14001ca2a:
        AvlRebalance(&tree->root, &local_res8);
        *out = nn;
    }
    tree->count = tree->count + 1;
    return out;
}

// 0x14001a4f0 — insert-or-find for a 0x28 node tree ordered by its int key. Calls
// 0x14001a680 (alloc) and 0x14001a8c0 (rebalance).
AvlNode28** AvlInsert28(AvlTreeHead<AvlNode28>* tree, AvlNode28** out, const int32_t* key)
{
    int32_t    k;
    AvlNode28* cur;
    AvlNode28* walk;
    AvlNode28* fresh;
    AvlNode28* nn;
    AvlNode28* local_res8;

    if (tree->root == nullptr) {
        fresh = static_cast<AvlNode28*>(System::MemoryAllocator::MemAlloc(0x28, 8));
        if (fresh == nullptr) {
            tree->root = nullptr;
            *out = nullptr;
        } else {
            fresh->key = *key;
            fresh->parent = nullptr;
            fresh->left = nullptr;
            fresh->right = nullptr;
            fresh->balance = 0;            // +0x20 / +0x22: one dword store (H7: two stores)
            fresh->height = 0;
            tree->root = fresh;
            *out = fresh;
        }
    } else {
        k = *key;
        walk = tree->root;
        do {
            while (local_res8 = walk, cur = local_res8, local_res8->key <= k) {
                if (k <= local_res8->key) {
                    *out = local_res8;
                    return out;
                }
                walk = local_res8->right;
                if (local_res8->right == nullptr) {
                    nn = AvlNewNode28(key, local_res8);
                    cur->right = nn;
                    goto LAB_14001a5b5;
                }
            }
            walk = local_res8->left;
        } while (local_res8->left != nullptr);
        nn = AvlNewNode28(key, local_res8);
        cur->left = nn;
LAB_14001a5b5:
        AvlRebalance(&tree->root, &local_res8);
        *out = nn;
    }
    tree->count = tree->count + 1;
    return out;
}

// ===========================================================================
// PathGraph cost cache (CitizenBehavior path-distance graph)
// ===========================================================================
// 0x140025a10 — insert-or-find in the region-pair cost tree at pathGraph+0x18 (0x30
// nodes). key = {regionHigh, regionLow, cost}, ordered by (key0, key1). Structurally
// 0x14001c940, but it rebalances through 0x14000e360 instead of 0x140013480.
AvlNode30** PathCostTreeInsert(AvlTreeHead<AvlNode30>* tree, AvlNode30** out, const int32_t* key)
{
    int32_t    k;
    int32_t    curKey;
    AvlNode30* cur;
    AvlNode30* walk;
    AvlNode30* fresh;
    AvlNode30* nn;
    AvlNode30* local_res8;

    if (tree->root == nullptr) {
        fresh = static_cast<AvlNode30*>(System::MemoryAllocator::MemAlloc(0x30, 8));
        if (fresh == nullptr) {
            tree->root = nullptr;
            *out = nullptr;
        } else {
            k = key[0];
            *out = fresh;
            fresh->key0 = k;
            fresh->key1 = key[1];
            fresh->key2 = key[2];
            fresh->parent = nullptr;
            fresh->left = nullptr;
            fresh->right = nullptr;
            fresh->balance = 0;            // +0x28 / +0x2a: one dword store (H7: two stores)
            fresh->height = 0;
            tree->root = fresh;
        }
    } else {
        k = key[0];
        walk = tree->root;
        do {
            while (local_res8 = walk, cur = local_res8, curKey = local_res8->key0,
                   curKey <= k) {
                if (k <= curKey) {
                    if (key[1] < local_res8->key1) break;
                    if ((k <= curKey) &&
                        ((k < curKey) || (key[1] <= local_res8->key1))) {
                        *out = local_res8;
                        return out;
                    }
                }
                walk = local_res8->right;
                if (local_res8->right == nullptr) {
                    nn = AvlNewNode30(key, local_res8);
                    cur->right = nn;
                    goto LAB_140025afa;
                }
            }
            walk = local_res8->left;
        } while (local_res8->left != nullptr);
        nn = AvlNewNode30(key, local_res8);
        cur->left = nn;
LAB_140025afa:
        CitizenAI_Reg_helper_e360(&tree->root, &local_res8);   // 0x14000e360
        *out = nn;
    }
    tree->count = tree->count + 1;
    return out;
}

// ===========================================================================
// B4 FORWARD: the C spellings other blocks' files still call, one line each.
// ===========================================================================

// 0x140025b30 — PathPairs_Clear: reset the pairs scratch array (pg+0x00 header
// {data@+0, count@+8, capacity@+0xC}) count to 0. B9 RT reading: retail is
// Array<PathCellPair>::SetSize(0) CONSTANT-FOLDED, one argument (0x140025b3a
// MOVSXD RBP,[RCX+8] / TEST EBP,EBP / JZ; MOV [RCX+8],EDI with EDI 0) -- no second
// parameter was dropped. Capacity (>=0) is never < 0, so the grow arm, the -1 fill
// loop (it runs while the index is negative) and the free are dead: the whole
// effect is the count reset (ProcessPaths @1400d70aa, BuildPathGraph @1400d7266).
extern "C" void PathPairs_Clear_25b30(PathGraph* pg)
{
    pg->m_pairCount = 0;
}
