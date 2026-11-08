// src/game/Game/gameupdateloop_teardown.cpp
// GameUpdateLoop::Shutdown teardown chain.
//
// GameUpdateLoop::Shutdown (0x1400977C0) runs whenever a live game goes away:
// quitting to the menu, and — the reason this file exists — loading a save from
// inside a running game, which tears the current loop down before deserializing
// the new one. Our port of Shutdown was complete, but five of the functions it
// calls unguarded had no body anywhere in the tree, so ResolveAddr handed back 0
// and the first one reached died with EXEC-of-0:
//
//   0x1400c3d30  EntityList::Shutdown
//   0x1400c3e60  EntityList::~EntityList   (since B.4 X4-COMP: the class dtor, entitylist.cpp)
//   0x1400907b0  CitizenList::~CitizenList  (since B.4 X5-COMP: CitizenBehavior::~CitizenBehavior, citizenbehavior.cpp)
//   0x1401024d0  EntityUI::~EntityUI
//   0x1400f8ff0  Tutorial::~Tutorial
//
// Everything here is teardown: array frees, BST frees, and one entity-removal
// drain. The container-free leaves are shared with other subsystems, so they are
// registered at their own retail addresses too rather than kept file-local.
//
// Already ported elsewhere, called by name (B3R):
//   0x1400c2ea0  MarkEntityForRemoval              (Action/exe_bodies.h)
//   0x1400c38e0  EntityList::RemoveEntity          (Entity/EntityList.h)
//   0x14000e320  WorkBase_TreeNodeFree_e320, kids +0x18/+0x20   (Work/exe_bodies.h)
//   0x140014c30  WorkBase_TreeNodeFree_14c30, kids +0x10/+0x18  (Work/exe_bodies.h)
//   0x140014b00  TradeWork_helper_14b00, kids +0x20/+0x28       (Player/exe_bodies.h)

// ENDGAME B.3.2 step 2 (COLL-GAME sweep): every `MemFree(data); data = 0; one
// qword over size+capacity = 0` triple below was a hand-inlined
// `Collection::Array<T>::~Array` for a trivially-destructible T
// (`rtcoll::ArrayDtorImpl`, byte for byte). Each is now `->~Array()` on the
// header, spelled `rtcoll::ArrayHdr` (= Array<uint8_t>) where the element type
// is not pinned in this file and `Array<void*>` where it is a pointer list.
// `ArrayOfArrays_Clear_175d0` is `Array<Array<Vector2>>::~Array` (WorldGeometry
// +0x1e0's release): each inner header ascending, as retail's loop does, then
// the outer block. Trees ({root,count} pairs, 4-byte count store),
// node chains and IBP payloads are NOT arrays and are untouched.
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Action/exe_bodies.h"          // MarkEntityForRemoval
#include "Player/exe_bodies.h"          // TradeWork_helper_14b00
#include "Work/exe_bodies.h"            // WorkBase_TreeNodeFree_e320 / _14c30
#include "options.h"         // Opt().teardownHeapCheck
#include "redzone_alloc.h"   // RedzoneAlloc_FindCorruptBlockNative
#include "collection/Collection.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Player/Tool.h"       // B.3.x W0: ~EntityUI shuts the active tool down by name
#include "Game/exe_bodies.h"
#include "Game/WorldGeometry.h"       // ArrayOfArrays_Clear_175d0 (WorldGeometry +0x1e0)
#include "Entity/EntityList.h"          // EntityList (0xE8), its trees and buckets
#include "Entity/ComponentEntity.h"     // ComponentEntity::_entityFlags
#include "Entity/CitizenBehavior.h"     // CitizenBehavior (0x1C0), PathGraph (0x28)
#include "Game/PlayerToolManager.h"     // PlayerToolManager (0xB0), TrackedEntityNode
#include "Game/WorkSchedulerNodes.h"    // WorkScheduler (0x138) and its three node types
#include "Dialog/ToolRegistryNode.h"    // the tool registry node (PlayerToolManager +0x10)
#include "Entity/path_cost_index.h"    // PathCostIndex::Clear (HITCH_PAIRSPREAD)

// B.4 P2 GAME-2: the five teardown bodies take their object typed and read it by
// name (B.4 Z CODEA: the last two offset reads, PathGraph's pair array and the
// tool registry's count, are named fields now).
namespace {

inline void MemFree(void* p) { if (p) System::MemoryAllocator::MemFree(p); }   // LEFT: void* is a raw heap block (System::MemoryAllocator::MemFree takes void*); kept as a wrapper for its null test

// MSVC's __ehvec_dtor_iter: destroy the elements of an embedded array in
// REVERSE order. Retail reaches these through the CRT helper at 0x14003ad30;
// the iteration is the whole content, so it is inlined here rather than ported.
template <typename T, int N, typename Dtor>
void VectorDtorIter(T (&elements)[N], Dtor dtor) {
    for (int i = N - 1; i >= 0; --i) dtor(&elements[i]);
}

} // namespace

// (0x1400174e0 / 0x1400173d0, the bucket trees' subtree free and Clear, are
// Collection::Set's DestroySubtree / Clear since B.4 X5-COMP: CitizenBehavior's
// +0x100 / +0x160 are Set members, cleared by ~CitizenBehavior.)

// ============================================================
// 0x1400175d0 — array of {ptr,count} sub-arrays: free each, then the array
// ============================================================
void ArrayOfArrays_Clear_175d0(RawArray<RawArray<Math::Vector2>>& outlines) {
    // Array<Array<Vector2>>::~Array: each inner header freed and zeroed in
    // ascending order (retail's loop: MemFree, then the two zero qwords), then
    // the outer block freed and its fields zeroed.
    for (int32_t i = 0; i < outlines.size; ++i)
        RawArrayFree(outlines.data[i]);
    RawArrayFree(outlines);
}

// ============================================================
// 0x1400171b0 — PathGraph's members: the distance-cache tree (+0x18), then the
// pair array (+0x00)
// ============================================================
extern "C" void ArrayPlusTree_Clear_171b0(PathGraph* self) {
    PathCostIndex::Clear(self);   // NOT RETAIL (HITCH_PAIRSPREAD): the m_bst map's storage
    PathCostNode* root = self->m_bst.root;
    if (root) {
        WorkBase_TreeNodeFree(&self->m_bst.root, root->left);
        WorkBase_TreeNodeFree(&self->m_bst.root, root->right);
        MemFree(root);
    }
    self->m_bst.root = nullptr;
    self->m_bst.count = 0;
    // +0x00: the pair array's inlined ~Array (0x14001720a..0x140017216: MemFree,
    // then two zero qwords; three named stores here, H7).
    System::MemoryAllocator::MemFree(self->m_pairs);
    self->m_pairs = nullptr;
    self->m_pairCount = 0;
    self->m_pairCapacity = 0;
}

// ============================================================
// 0x14000dcb0 — {root,count} tree clear, children +0x18/+0x20 (WorkScheduler's
// fifteen id trees)
// ============================================================
extern "C" void Tree_Clear_dcb0(WorkSchedulerTree* tree) {
    WorkSchedulerIdNode* root = tree->root;
    if (root) {
        WorkBase_TreeNodeFree(tree, root->left);
        WorkBase_TreeNodeFree(tree, root->right);
        MemFree(root);
    }
    tree->root = nullptr;
    tree->count = 0;
}

// ============================================================
// 0x14002cf30 — node-owned sub-tree clear: the bucket's own id tree (node+0x08)
// ============================================================
extern "C" void NodeSubTree_Clear_2cf30(WorkSchedulerBucketNode* node) {
    WorkSchedulerTree* subTree = &node->bucket.entries;
    WorkSchedulerIdNode* root = subTree->root;
    if (root) {
        WorkBase_TreeNodeFree(subTree, root->left);
        WorkBase_TreeNodeFree(subTree, root->right);
        MemFree(root);
    }
    subTree->root = nullptr;
    node->bucket.entries.count = 0;
}

// ============================================================
// 0x14002ce60 — recursive node free (children +0x20/+0x28), each node owning a
// sub-tree cleared by NodeSubTree_Clear_2cf30
// ============================================================
extern "C" void TreeNodeFree_2ce60(WorkSchedulerBucketNode** hdrV, WorkSchedulerBucketNode* node) {
    if (!node) return;
    TreeNodeFree_2ce60(hdrV, node->left);
    TreeNodeFree_2ce60(hdrV, node->right);
    NodeSubTree_Clear_2cf30(node);
    MemFree(node);
}

// ============================================================
// 0x14002df90 — recursive node free (children +0x20/+0x28), IBP payload at +0x10
// ============================================================
// (PlayerToolManager +0x10's tool registry: the node is Dialog/ToolRegistryNode.h.)
extern "C" void TreeNodeFree_2df90(AvlTreeHead<ToolRegistryNode>* hdrV, ToolRegistryNode* node) {
    if (!node) return;
    TreeNodeFree_2df90(hdrV, node->left);
    TreeNodeFree_2df90(hdrV, node->right);
    if (node->toolSetting != nullptr)
        node->toolSetting.DeletePtr();
    MemFree(node);
}

// ============================================================
// 0x14002de90 — clear a {root,count} tree of IBP-payload nodes
// ============================================================
extern "C" void IbpTree_Clear_2de90(AvlTreeHead<ToolRegistryNode>* tree) {
    ToolRegistryNode* root = tree->root;
    if (root) {
        TreeNodeFree_2df90(tree, root->left);
        TreeNodeFree_2df90(tree, root->right);
        if (root->toolSetting != nullptr)
            root->toolSetting.DeletePtr();
        MemFree(root);
    }
    tree->root = nullptr;
    tree->count = 0;
}

// ============================================================
// 0x1400c3d30 — EntityList::Shutdown
// ============================================================
// Sets the "list closed" flag at +0xE0 (which makes entity registration DELETE
// instead of file — see spawncomponent.cpp), marks every live entity for removal,
// drains the pending-removal array, then removes whatever is left in the master
// array until it is empty. The flag is cleared again on the way out.
void EntityList::Shutdown() {
    shuttingDown = true;

    // BANISHED_TEARDOWN_HEAPCHECK brackets the mark loop. The stress town's
    // teardown crash (docs/history/runtime_port/TEARDOWN_WILD_WRITE.md) is ONE stray
    // 4-byte store into an allocator boundary tag, laid down inside a single
    // MarkForRemoval cascade and only tripped over hundreds of entities later,
    // in the sweep. A hardware watchpoint cannot be armed on a victim whose
    // address is unknown until after it has been written, so the boundary tags
    // are scanned after every step instead: the first step whose scan comes back
    // dirty IS the step that wrote. This is the instrument that pinned the writer
    // to mark step 13; it costs nothing when the flag is off.
    const bool bracket = Opt().teardownHeapCheck;
    bool bracketFired = false;
    if (bracket) {
        std::fprintf(stderr, "[TeardownBracket] ARMED; heap at entry is %s\n",
                     RedzoneAlloc_FindCorruptBlockNative() ? "ALREADY CORRUPT" : "clean");
        std::fflush(stderr);
    }

    // Mark every entity not already flagged deleted (entity+0x40 bit 10).
    for (int32_t i = 0; i < masterArray._size; ++i) {
        Object::InternalPtr<ComponentEntity>* slot = &masterArray._data[i];
        ComponentEntity* entity = slot->Get();
        if ((entity->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) == 0) {
            slot->Get()->MarkForRemoval();   // 0x1400c2ea0
        }
        if (bracket && !bracketFired) {
            if (void* bad = RedzoneAlloc_FindCorruptBlockNative()) {
                bracketFired = true;
                std::fprintf(stderr,
                    "[TeardownBracket] *** HEAP WENT CORRUPT *** mark step=%d "
                    "corruptBlock=%p tag=0x%016llx | the step that just ran is the "
                    "writer: entity=%p entityVtable=0x%016llx\n",
                    (int)i, bad, (unsigned long long)*reinterpret_cast<uint64_t*>(bad),  // CAST-OK: crash_walk
                    entity,
                    (unsigned long long)(entity ? *reinterpret_cast<uint64_t*>(entity) : 0));  // CAST-OK: crash_walk
                std::fflush(stderr);
                // Name the owner of the block written past, repair the tag, arm a
                // watchpoint on the dword (TEARDOWN_WILD_WRITE.md section 3).
                RedzoneAlloc_ReportWildWrite(bad);
            }
        }
    }

    // Drain the pending-removal array (+0xD0 data / +0xD8 count).
    for (int32_t i = 0; i < removalList._size; ++i) {
        ComponentEntity* ibp = removalList._data[i];
        RemoveEntity(&ibp);   // 0x1400c38e0
    }
    removalList.~Array();   // retail inlines the Array dtor here (free, then the two zero qwords); ~EntityList runs it again on the empty array

    // Whatever is still in the master array goes now. RemoveEntity shrinks the
    // array, so retail re-reads the count every pass and always takes slot 0.
    while (masterArray._size > 0) {
        ComponentEntity* ibp = masterArray._data[0].Get();
        RemoveEntity(&ibp);   // 0x1400c38e0
    }

    shuttingDown = false;
}

// (0x1400907b0, CitizenList::~CitizenList, is CitizenBehavior::~CitizenBehavior
// since B.4 X5-COMP: Entity/citizenbehavior.cpp; Shutdown calls it.)

// ============================================================
// 0x1401024d0 — EntityUI::~EntityUI
// ============================================================
// (The receiver is the 0xB0 PlayerToolManager at GameUpdateLoop+0x120; the
// name is the tree's.)
void PlayerToolManager::Teardown() {
    // Active tool (+0x28): Tool slot 5 Shutdown (0x140102505: RCX only), then
    // drop the IBP.
    if (currentTool != nullptr) {
        currentTool->Shutdown();   // slot 5
        currentTool.DeletePtr();   // 0x14010250c
    }

    cameraViews.~Array();   // retail's inlined ~Array, here in the body's order (no C++ dtor runs: Shutdown MemFrees the block)

    // Two trees with kids at +0x20/+0x28 (+0x60 then +0x50).
    // (B.4 Y GAMEB: the two sets have their own node types now; the same
    // body runs on each, +0x60 first.)
    auto freeSet = [](auto& set) {
        auto* root = set.root;
        if (root) {
            Avl38FreeSubtree(&set.root, root->left);    // 0x140014b00 (was its TradeWork_helper_14b00 forwarder)
            Avl38FreeSubtree(&set.root, root->right);
            MemFree(root);
        }
        set.root = nullptr;
        set.count = 0;
    };
    freeSet(trackedEntities);
    freeSet(pinnedToolDialogs);

    // Tool registry tree (+0x10): nodes carry an IBP payload at +0x10.
    IbpTree_Clear_2de90(&toolRegistry);
}

// ============================================================
// 0x1400f8ff0 — Tutorial::~Tutorial
// ============================================================
// (The receiver is the 0x138 WorkScheduler at GameUpdateLoop+0x118; the name
// is the tree's.)
void WorkScheduler::Teardown() {
    // In-order walk of the id tree trees[0] (+0x30), dropping each node's Work
    // (+0x08). The nodes themselves are freed by the vector-dtor pass further
    // down, not here.
    {
        WorkSchedulerIdNode* node = trees[0].root;
        while (node && node->left) node = node->left;
        while (node) {
            node->work.DeletePtr();
            WorkSchedulerIdNode* right = node->right;
            if (right) {
                node = right;
                while (node->left) node = node->left;
            } else {
                WorkSchedulerIdNode* parent = node->parent;
                while (parent && parent->right == node) {
                    node = parent;
                    parent = node->parent;
                }
                node = parent;
            }
        }
    }

    // Tree at +0x120 whose nodes each own a sub-tree (kids +0x20/+0x28).
    {
        WorkSchedulerBucketNode* root = professionBucketRoot;
        if (root) {
            TreeNodeFree_2ce60(&professionBucketRoot, root->left);
            TreeNodeFree_2ce60(&professionBucketRoot, root->right);
            NodeSubTree_Clear_2cf30(root);
            MemFree(root);
        }
        professionBucketRoot = nullptr;
        professionBucketCount = 0;
    }

    // 15 tree headers of 0x10 bytes at +0x30, cleared back to front.
    VectorDtorIter(trees, [](WorkSchedulerTree* e) { Tree_Clear_dcb0(e); });

    // Tree at +0x20 (kids +0x18/+0x20).
    {
        WorkSchedulerNode20* root = unusedTreeRoot;
        if (root) {
            WorkBase_TreeNodeFree(&unusedTreeRoot, root->left);
            WorkBase_TreeNodeFree(&unusedTreeRoot, root->right);
            MemFree(root);
        }
        unusedTreeRoot = nullptr;
        unusedTreeCount = 0;
    }

    // Tree at +0x10 (kids +0x10/+0x18).
    {
        WorkSchedulerCountNode* root = queueOrderTree.root;
        if (root) {
            WorkBase_TreeNodeFree(&queueOrderTree.root, root->left);
            WorkBase_TreeNodeFree(&queueOrderTree.root, root->right);
            MemFree(root);
        }
        queueOrderTree.count = 0;
        queueOrderTree.root = nullptr;
    }
}
