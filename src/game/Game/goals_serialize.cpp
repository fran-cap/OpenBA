// src/game/Game/goals_serialize.cpp
//
// Port of exe 0x1400f9240 (Ghidra: TradeComponent_helper_9240): WorkScheduler::Serialize,
// the serializer of the work scheduler at GameUpdateLoop+0x118 (Game/WorkScheduler.h; the
// member the tree calls m_tutorial), called from the master GameUpdateLoop serialize
// (0x140095a90) during SAVE/LOAD. Despite the Ghidra "Trade*" auto-naming (an artifact of
// decompiler similarity heuristics, not a real namespace), the tree this function walks at
// +0x30 is the SAME work-id tree WorkComponent::CheckWorkSlot walks.
//
// Layout (Game/WorkScheduler.h; node types Game/WorkSchedulerNodes.h):
//   +0x00 / +0x04 int, +0x08 uint   serialized directly
//   +0x10  the 0x28-node "work count" tree (WorkSchedulerCountNode), count +0x18: never itself
//          serialized, rebuilt on LOAD from version-2 saves
//   +0x30  fifteen {root, count} trees of 0x30 nodes (WorkSchedulerIdNode: work id -> Work);
//          trees[0] is the id tree WorkComponent::CheckWorkSlot walks; the other 14 are only
//          touched here by the dead-id removal
//   +0x120 the 0x38-node bucket tree (WorkSchedulerBucketNode: Profession* -> its own id tree)
//
// Sub-callees:
//   0x1400f8270  WorkScheduler::ValidateAll  — SAVE-side pre-pass: slot 9 on every Work.
//   0x14002d060  SerializeIdTrees            — the 14/15-tree array serializer.
//   0x14002d340  SerializeIdTree             — one id tree (0x14002d060 inlines the same body
//                                              per tree).
//   0x14002d5f0  SerializeBucketTree         — the bucket tree.
//   0x14002ccf0  InsertBucket                — bucket-tree insert-or-find.
//   0x14002ceb0  CopyBucketNode              — bucket node copy-ctor.
//   0x14002cfb0  CopyIdSubtree               — recursive id-subtree deep copy, used by ceb0.
//   0x140018020  Reimpl_ExternalPtr_Serialize_StringTable (ExternalPtr<Profession>)
//   0x14001a8c0 / 0x140013480 / 0x140014b40  the three rebalance drivers (C forwarders)
//   0x14001ca60  WorkCellBST_Remove_ca60     — 0x30-node erase
//   0x14001a6e0  WorkBST_BSTRemove_a6e0      — 0x28-node erase
//   0x140014c30 / 0x14000e320  WorkBase_TreeNodeFree_14c30 / _e320 — post-order frees
//
// Byte-faithful port — every branch, walk, and rebuild step mirrors the decompile. In-order BST
// walks (Ghidra's coalesced goto-successor idiom) are rewritten as clean iterator loops via the
// Leftmost / Next templates below; this is a structural cleanup only (the classic parent-pointer
// in-order successor algorithm), not a logic change.
//
// B.4 GAME-b: every node and the scheduler are read by NAME. The casts left are the arguments of
// the C declarations other blocks own (the rebalance drivers, Action/exe_bodies.h and
// Player/exe_bodies.h; the two erases, Work/exe_bodies.h): each takes the tree as an integer
// pointer. They are funnelled through the four Rebalance / Erase wrappers below.

#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // GameUpdateLoop::_singleton
#include "Game/WorkScheduler.h"
#include "Game/WorkSchedulerNodes.h"
#include "Game/Profession.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Object/factory_registration.h"
#include "Work/Work.h"                // Work::TypeId() -- cell 0x1401c6cf8 (ENDGAME B.3.9 RF)
#include <cstdint>
#include <cstdio>
#include "ext/registry.h"                                   // Ext::Fix().priority_after_load
#include "ext/priority_after_load/priority_after_load.h"   // fix.priority_after_load (docs/retail_bugs.md section 34)
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include "io/Stream.h"      // ENDGAME B.3.4 callers batch H: IO::Stream::GetDirection
#include "Component/WorkComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include <new>
#include "Game/exe_bodies.h"
#include "Action/exe_bodies.h"   // Pf_AvlRebalanceA_a8c0
#include "Action/PathAvl.h"      // AvlRebalance (0x140013480)
#include "Player/exe_bodies.h"   // Avl38Retrace (0x140014b40)
#include "Work/exe_bodies.h"     // WorkBST / WorkCellBST removes, WorkBase_TreeNodeFree_*


namespace {

// ============================================================
// The C declarations of other blocks' tree primitives take the tree header and the cursor as
// integers. LEFT: their headers (Action/exe_bodies.h ACT-a, Player/exe_bodies.h, Work/
// exe_bodies.h WORK-a) are not this block's.
// ============================================================
void RebalanceIdTree(WorkSchedulerIdNode** root, WorkSchedulerIdNode* from) {        // 0x140013480
    AvlRebalance(root, &from);
}
void RebalanceCountTree(WorkSchedulerCountNode** root, WorkSchedulerCountNode* from) { // 0x14001a8c0
    AvlRebalance(root, &from);
}
void RebalanceBucketTree(WorkSchedulerBucketNode** root, WorkSchedulerBucketNode* from) { // 0x140014b40
    Avl38Retrace(root, &from);
}
void EraseIdNode(WorkSchedulerTree* tree, WorkSchedulerIdNode* node) {               // 0x14001ca60
    WorkCellBST_Remove_ca60(tree, node);   // the template on WorkSchedulerIdNode (Work/CellBst.h)
}
// 0x14001a6e0 takes the tree's {root, count} header: it unlinks the node, rewrites the root
// when it changes, and decrements the count dword at +8 (known_port_issues 796, fixed).
void EraseCountNode(AvlTreeHead<WorkSchedulerCountNode>* tree, WorkSchedulerCountNode* node) {    // 0x14001a6e0
    WorkBST_BSTRemove_a6e0(tree, node);
}

// ============================================================
// Generic in-order BST walk (parent-pointer successor algorithm) and search, over a node
// type that names `parent`, `left`, `right`.
// ============================================================
template <class Node>
Node* Leftmost(Node* node) {
    if (!node) return nullptr;
    while (node->left) node = node->left;
    return node;
}
template <class Node>
Node* Next(Node* node) {
    if (node->right) return Leftmost(node->right);
    Node* parent = node->parent;
    Node* cur = node;
    while (parent && parent->right == cur) {
        cur = parent;
        parent = parent->parent;
    }
    return parent;
}

// Search by int32 key (the id and count nodes).
template <class Node>
Node* FindByKey(Node* root, int32_t key) {
    Node* node = root;
    while (node != nullptr) {
        if (key < node->key) {
            node = node->left;
        } else if (node->key < key) {
            node = node->right;
        } else {
            return node;
        }
    }
    return nullptr;
}

// ============================================================
// 0x28-node insert: the element is one qword {key, workId}. *treeRoot is the tree's root
// field. Returns the (found-or-inserted) node; *outInserted reports whether a NEW node was
// made (retail's per-slot counter increments only on genuine insert, not on found-existing).
// ============================================================
WorkSchedulerCountNode* CountTreeInsert(WorkSchedulerCountNode** treeRoot, int32_t key,
                                        uint32_t workId, bool* outInserted) {
    *outInserted = false;
    WorkSchedulerCountNode* root = *treeRoot;
    if (root == nullptr) {
        WorkSchedulerCountNode* node = static_cast<WorkSchedulerCountNode*>(
            System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerCountNode), 8));
        if (node) {
            node->key = key;
            node->workId = workId;
            node->parent = nullptr; node->left = nullptr; node->right = nullptr;
            node->balance = 0; node->height = 0;
        }
        *treeRoot = node;
        *outInserted = true;
        return node;
    }
    WorkSchedulerCountNode* parent = root;
    WorkSchedulerCountNode* node = nullptr;
    for (;;) {
        if (parent->key <= key) {
            if (key <= parent->key) {
                return parent; // found existing — no insert
            }
            WorkSchedulerCountNode* right = parent->right;
            if (right == nullptr) {
                node = static_cast<WorkSchedulerCountNode*>(
                    System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerCountNode), 8));
                if (node) {
                    node->key = key;
                    node->workId = workId;
                    node->parent = parent;
                    node->left = nullptr; node->right = nullptr;
                    node->balance = 0; node->height = 0;
                }
                parent->right = node;
                break;
            }
            parent = right;
        } else {
            WorkSchedulerCountNode* left = parent->left;
            if (left == nullptr) {
                node = static_cast<WorkSchedulerCountNode*>(
                    System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerCountNode), 8));
                if (node) {
                    node->key = key;
                    node->workId = workId;
                    node->parent = parent;
                    node->left = nullptr; node->right = nullptr;
                    node->balance = 0; node->height = 0;
                }
                parent->left = node;
                break;
            }
            parent = left;
        }
    }
    RebalanceCountTree(treeRoot, parent);
    *outInserted = true;
    return node;
}

// A fresh 0x30 id node {key, work} under `parent` (null for a new root).
WorkSchedulerIdNode* NewIdNode(int32_t key, Object::InternalPtr<Work> work,
                               WorkSchedulerIdNode* parent) {
    WorkSchedulerIdNode* node = static_cast<WorkSchedulerIdNode*>(
        System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerIdNode), 8));
    if (node) {
        node->key = key;
        node->work = work;
        node->parent = parent;
        node->left = nullptr; node->right = nullptr;
        node->balance = 0; node->height = 0;
    }
    return node;
}

// ============================================================
// 0x14002cfb0 — recursive deep-copy of a 0x30-node subtree. (Retail's first argument is only
// forwarded to the recursion.)
// ============================================================
WorkSchedulerIdNode* CopyIdSubtree(const WorkSchedulerIdNode* src) {
    WorkSchedulerIdNode* dst = static_cast<WorkSchedulerIdNode*>(
        System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerIdNode), 8));
    if (dst != nullptr) {
        dst->key = src->key;
        dst->work = src->work;   // payload @0x08
        dst->parent = nullptr;
        dst->left = nullptr;
        dst->right = nullptr;
        dst->balance = 0; dst->height = 0;
    }
    // Faithful: retail derefs `dst` unconditionally here even on OOM (dst==null) — no guard added.
    dst->height = src->height;
    dst->balance = src->balance;

    if (src->left != nullptr) {
        WorkSchedulerIdNode* leftCopy = CopyIdSubtree(src->left);
        dst->left = leftCopy;
        leftCopy->parent = dst;
    }
    if (src->right != nullptr) {
        WorkSchedulerIdNode* rightCopy = CopyIdSubtree(src->right);
        dst->right = rightCopy;
        rightCopy->parent = dst;
    }
    return dst;
}

// ============================================================
// 0x14002ceb0 — bucket node copy-ctor: `dst` is a fresh 0x38 buffer, `src` the record, `parent`
// the new node's parent.
// ============================================================
WorkSchedulerBucketNode* CopyBucketNode(WorkSchedulerBucketNode* dst, const WorkSchedulerBucket* src,
                                        WorkSchedulerBucketNode* parent) {
    dst->bucket.key = src->key;
    dst->bucket.entries.root = nullptr;
    dst->bucket.entries.count = 0;
    if (&src->entries != &dst->bucket.entries) { // always true at our call sites; kept for fidelity
        if (src->entries.root != nullptr) {
            dst->bucket.entries.root = CopyIdSubtree(src->entries.root);
        }
        dst->bucket.entries.count = src->entries.count;
    }
    dst->parent = parent;
    dst->left = nullptr;
    dst->right = nullptr;
    dst->balance = 0; dst->height = 0;
    return dst;
}

// ============================================================
// 0x14002ccf0 — bucket-tree insert. On an existing key: writes the existing node to *pOut.
// Otherwise allocates via ceb0, links, rebalances (0x140014b40).
// ============================================================
WorkSchedulerBucketNode** InsertBucket(WorkSchedulerBucketNode** treeRoot, int32_t* treeCount,
                                       WorkSchedulerBucketNode** pOut, const WorkSchedulerBucket* rec) {
    Profession* const key = rec->key;   // compared as unsigned qwords (pointer order)
    WorkSchedulerBucketNode* root = *treeRoot;
    WorkSchedulerBucketNode* result;
    if (root == nullptr) {
        WorkSchedulerBucketNode* buf = static_cast<WorkSchedulerBucketNode*>(
            System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerBucketNode), 8));
        result = buf ? CopyBucketNode(buf, rec, nullptr) : nullptr;
        *treeRoot = result;
    } else {
        WorkSchedulerBucketNode* cur = root;
        WorkSchedulerBucketNode* parent = cur;
        for (;;) {
            parent = cur;
            Profession* const nodeKey = parent->bucket.key;
            if (nodeKey <= key) {
                if (key <= nodeKey) {
                    *pOut = parent;
                    return pOut;
                }
                cur = parent->right;
                if (cur == nullptr) {
                    WorkSchedulerBucketNode* buf = static_cast<WorkSchedulerBucketNode*>(
                        System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerBucketNode), 8));
                    WorkSchedulerBucketNode* node = buf ? CopyBucketNode(buf, rec, parent) : nullptr;
                    parent->right = node;
                    result = node;
                    break;
                }
            } else {
                cur = parent->left;
                if (cur == nullptr) {
                    WorkSchedulerBucketNode* buf = static_cast<WorkSchedulerBucketNode*>(
                        System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerBucketNode), 8));
                    WorkSchedulerBucketNode* node = buf ? CopyBucketNode(buf, rec, parent) : nullptr;
                    parent->left = node;
                    result = node;
                    break;
                }
            }
        }
        RebalanceBucketTree(treeRoot, parent);
    }
    *treeCount += 1;
    *pOut = result;
    return pOut;
}

// ============================================================
// 0x14002d340 — one id tree. On LOAD, rebuilds the tree from the stream; on SAVE, walks it
// in-order. Each Work is IsDerivedFrom-filtered against Work::TypeId() (non-Work -> null).
// ============================================================
void SerializeIdTree(WorkSchedulerTree* tree, IO::Serializer& ser) {
    int32_t count = tree->count;
    ser.Serialize(count, nullptr);

    bool loading = (static_cast<int>(ser.GetDirection()) == 0);   // IO::Serializer::GetDirection, retail 0x180006210
    if (loading) {
        for (int32_t i = 0; i < count; ++i) {
            // Retail re-reads the root on every iteration — the previous insertion
            // may have installed (or rebalanced) the root. Hoisting this read out of
            // the loop (as an earlier copy of the d060 body did) leaves `root` pinned
            // at the entry value, so every key takes the "empty tree" path and
            // overwrites the single root node instead of building the tree.
            WorkSchedulerIdNode* root = tree->root;
            int32_t key = 0;
            ser.Serialize(key, nullptr);
            Object::InternalPtr<Work> payload;
            payload.Serialize(ser, Work::TypeId());   // nulls a non-Work
            WorkSchedulerIdNode* node = FindByKey(root, key);
            if (node == nullptr) {
                if (root == nullptr) {
                    tree->root = NewIdNode(key, payload, nullptr);
                } else {
                    WorkSchedulerIdNode* parent = root;
                    for (;;) {
                        if (parent->key <= key) {
                            if (parent->right == nullptr) {
                                parent->right = NewIdNode(key, payload, parent);
                                break;
                            }
                            parent = parent->right;
                        } else {
                            if (parent->left == nullptr) {
                                parent->left = NewIdNode(key, payload, parent);
                                break;
                            }
                            parent = parent->left;
                        }
                    }
                    RebalanceIdTree(&tree->root, parent);
                }
                tree->count += 1;
            }
        }
    } else {
        for (WorkSchedulerIdNode* node = Leftmost(tree->root); node; node = Next(node)) {
            ser.Serialize(node->key, nullptr);
            node->work.Serialize(ser, Work::TypeId());   // nulls a non-Work
        }
    }
}

// ============================================================
// 0x14002d060 — the 14/15-tree array serializer. count = number of trees (0xe or 0xf,
// version-gated by the caller). Retail inlines 0x14002d340's body for each tree (the stream
// direction is read once; it does not change between trees). Its fourth register (R9) is
// spilled to its home slot and never read; 0x1400f9240 leaves it unset.
// ============================================================
void SerializeIdTrees(IO::Serializer& ser, int32_t count, WorkSchedulerTree* trees) {
    for (int32_t s = 0; s < count; ++s) {
        SerializeIdTree(&trees[s], ser);
    }
}

// ============================================================
// 0x14002d5f0 — the bucket tree serializer.
// ============================================================
void SerializeBucketTree(WorkSchedulerBucketNode** treeRoot, int32_t* treeCount, IO::Serializer& ser) {
    int32_t count = *treeCount;
    ser.Serialize(count, nullptr);

    bool loading = (static_cast<int>(ser.GetDirection()) == 0);   // IO::Serializer::GetDirection, retail 0x180006210
    if (loading) {
        for (int32_t i = 0; i < count; ++i) {
            // Record layout (retail's local_40/local_38/local_30 triple): the key and the
            // {root,count} header d340 fills — retail passes &local_38, not a separate pair.
            // Building the subtree somewhere else and then inserting the record loses every
            // Work subtree on load.
            WorkSchedulerBucket rec = {};
            ::Object::SerializeExternal<Profession>(ser, rec.key);   // ExternalPtr<Profession> (0x140018020)
            SerializeIdTree(&rec.entries, ser);

            WorkSchedulerBucketNode* outNode = nullptr;
            InsertBucket(treeRoot, treeCount, &outNode, &rec);
            // The insert deep-copies the record, so the subtree we just built is ours to
            // release — retail's FUN_14000e320 free-then-MemFree pair on the same header.
            if (rec.entries.root != nullptr) {
                WorkSchedulerIdNode* subRoot = rec.entries.root;
                WorkBase_TreeNodeFree(&rec.entries, subRoot->left);
                WorkBase_TreeNodeFree(&rec.entries, subRoot->right);
                System::MemoryAllocator::MemFree(subRoot);
            }
        }
    } else {
        for (WorkSchedulerBucketNode* cNode = Leftmost(*treeRoot); cNode; cNode = Next(cNode)) {
            ::Object::SerializeExternal<Profession>(ser, cNode->bucket.key);
            // Retail passes the node's OWN {root, count} header. Copying the root into a
            // stack pair with a zeroed count made d340 write "0 entries" and then walk the
            // real subtree anyway, so the save stream carried N records the load never read
            // back: the reader stopped at count 0 and hit those records as the next record's key.
            SerializeIdTree(&cNode->bucket.entries, ser);
        }
    }
}

} // namespace

// ============================================================
// 0x1400f8270 — the SAVE-side pre-pass. In-order walk of the id tree (+0x30), slot 9 on each
// Work (CheckPtr, a bare RET, first).
// ============================================================
void WorkScheduler::ValidateAll() {
    for (WorkSchedulerIdNode* node = Leftmost(trees[0].root); node; node = Next(node)) {
        node->work->Validate();   // slot 9; 0x1400f82b1 RCX only
    }
}

// ============================================================
// 0x1400f9240 — the scheduler's serializer (version 3).
// ============================================================
void WorkScheduler::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 3);

    bool saving = (static_cast<int>(stream.GetDirection()) == 1);   // IO::Stream::GetDirection, retail 0x180006200
    if (saving) {
        ValidateAll();
    }

    int32_t version = ser.GetVersion();
    int32_t slotCount = (version > 2) ? 0xf : 0xe;

    SerializeIdTrees(ser, slotCount, trees);
    SerializeBucketTree(&professionBucketRoot, &professionBucketCount, ser);

    ser.Serialize(lastWorkId, nullptr);
    ser.Serialize(nextQueueOrder, nullptr);
    ser.Serialize(stepCount, nullptr);

    bool loading = (static_cast<int>(stream.GetDirection()) == 0);   // IO::Stream::GetDirection, retail 0x180006200
    if (loading) {
        WorkSchedulerCountNode* workCountRoot = queueOrderTree.root;
        // FIX priority_after_load: decided once, above the walk.
        const bool rebuildFromSavedKeys =
            Ext::PriorityAfterLoad::RebuildsFromSavedKeys(Ext::Fix().priority_after_load, ser.GetVersion());

        // --- Stage 1: walk the id tree (+0x30), notify each Work (slot 12), and for
        // version==2 entries build the +0x10 tree keyed by the running counter at +0x18. ---
        {
            for (WorkSchedulerIdNode* node = Leftmost(trees[0].root); node; node = Next(node)) {
                Work* work = node->work.Get();

                // 0x1400f9335..0x1400f9340: the version is passed as EDX to slot 12.
                const int32_t slot12Ver = ser.GetVersion();
                work->Slot12(slot12Ver);

                int32_t ver = ser.GetVersion();
                if (ver == 2) {
                    int32_t counter = queueOrderTree.count;
                    uint32_t workId = static_cast<uint32_t>(node->work->_workId);
                    bool inserted = false;
                    CountTreeInsert(&workCountRoot, counter, workId, &inserted);
                    if (inserted) {
                        queueOrderTree.count = counter + 1;
                    }
                } else if (rebuildFromSavedKeys) {
                    // FIX priority_after_load (NOT RETAIL, docs/retail_bugs.md section 34): the job goes
                    // back into the queue-order tree under the key it was saved with.
                    bool inserted = false;
                    CountTreeInsert(&workCountRoot, node->work->_queueOrder,
                                    static_cast<uint32_t>(node->work->_workId), &inserted);
                    if (inserted) {
                        queueOrderTree.count += 1;
                    }
                }
            }
            queueOrderTree.root = workCountRoot;
            if (rebuildFromSavedKeys) {
                fprintf(stderr, "[PriorityAfterLoad] queue-order tree rebuilt: %d key(s)\n",
                        static_cast<int>(queueOrderTree.count));
            }
        }

        // --- Stage 2: re-walk the id tree; for Works whose entity (+0x10) is set, collect the
        // bare id into a fresh 0x28-node set (retail's local_60). ---
        // Retail's local Set header: root [RBP-0x28], count [RBP-0x20], both zeroed at
        // 0x1400f94cd / 0x1400f94d1; the count increments only on a genuine insert (0x1400f9609,
        // which the found-existing arm 0x1400f956a skips).
        AvlTreeHead<WorkSchedulerCountNode> localFound;
        localFound.root = nullptr;
        localFound.count = 0;
        {
            for (WorkSchedulerIdNode* node = Leftmost(trees[0].root); node; node = Next(node)) {
                Work* work = node->work.Get();
                if (work->_entity != nullptr) {
                    bool inserted = false;
                    CountTreeInsert(&localFound.root, node->key, 0, &inserted); // key only; upper 32 unused by retail
                    if (inserted) {
                        localFound.count++;
                    }
                }
            }
        }

        // --- Stage 3: scan the live entity list; for each entity, find its WorkComponent,
        // look its workId up in the id tree (the world's m_tutorial +0x30 == this tree), and if
        // present, erase the matching id from localFound (it's still alive). ---
        if (GameUpdateLoop::_singleton != nullptr) {
            GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
            EntityList* entityList = gameWorld->m_entityList;
            int32_t entityCount = entityList->masterArray._size;

            for (int32_t i = 0; i < entityCount; ++i) {
                ComponentEntity* entity = entityList->masterArray._data[i].Get();
                WorkComponent* workComponent = entity->FindComponent<WorkComponent>();
                if (workComponent != nullptr) {
                    int32_t workId = workComponent->workId;

                    WorkSchedulerIdNode* slotNode = FindByKey(gameWorld->m_tutorial->trees[0].root, workId);
                    if (slotNode != nullptr) {
                        WorkSchedulerCountNode* found = FindByKey(localFound.root, workId);
                        if (found != nullptr) {
                            EraseCountNode(&localFound, found);   // RCX = RBP-0x28 (0x1400f97c1 / 0x1400f97c5)
                        }
                    }
                }
            }
        }

        // --- Stage 4: for every id remaining in localFound (genuinely dead), remove it
        // from the 14 other trees, the +0x10 tree and the bucket tree, then delete the Work
        // and drop it from the id tree itself. ---
        if (localFound.root != nullptr) {
            for (WorkSchedulerCountNode* idNode = Leftmost(localFound.root); idNode; ) {
                int32_t deadId = idNode->key;
                WorkSchedulerCountNode* nextIdNode = Next(idNode); // capture successor before any mutation below

                WorkSchedulerIdNode* idTreeNode = FindByKey(trees[0].root, deadId);
                if (idTreeNode != nullptr) {
                    // Erase the id from the 14 remaining trees (+0x40..+0x11F).
                    for (int32_t s = 1; s < 15; ++s) {
                        WorkSchedulerIdNode* match = FindByKey(trees[s].root, deadId);
                        if (match != nullptr) {
                            EraseIdNode(&trees[s], match);
                        }
                    }

                    Work* work = idTreeNode->work.Get();
                    int32_t orderKey = work->_queueOrder;

                    WorkSchedulerCountNode* wcMatch = FindByKey(queueOrderTree.root, orderKey);
                    if (wcMatch != nullptr) {
                        EraseCountNode(&queueOrderTree, wcMatch);   // RCX = R13+0x10 (0x1400f9896 / 0x1400f989a)
                    }

                    // Slot 10 writes the Work's target Profession into the return slot (every
                    // Work's GetTarget returns the slot it was handed).
                    Profession* target[4] = {};
                    work->GetTarget(target);

                    // The keys compare as unsigned qwords (pointer order).
                    Profession* const targetKey = target[0];
                    WorkSchedulerBucketNode* cNode = professionBucketRoot;
                    while (cNode != nullptr) {
                        Profession* const nodeKey = cNode->bucket.key;
                        if (targetKey < nodeKey) {
                            cNode = cNode->left;
                        } else if (nodeKey < targetKey) {
                            cNode = cNode->right;
                        } else {
                            break;
                        }
                    }
                    if (cNode != nullptr) {
                        WorkSchedulerIdNode* subMatch = FindByKey(cNode->bucket.entries.root, deadId);
                        if (subMatch != nullptr) {
                            EraseIdNode(&cNode->bucket.entries, subMatch);
                        }
                    }

                    idTreeNode->work.DeletePtr();
                    EraseIdNode(&trees[0], idTreeNode);
                }

                idNode = nextIdNode;
            }
        }

        // --- Stage 5: final walk over the id tree calling slot 9 (same shape as the SAVE-side
        // pre-pass). ---
        {
            for (WorkSchedulerIdNode* node = Leftmost(trees[0].root); node; node = Next(node)) {
                node->work->Validate();   // slot 9; 0x1400f99a4 RCX only
            }
        }

        // --- Stage 6: free localFound (post-order free of the children, then the root
        // node itself). ---
        if (localFound.root != nullptr) {
            WorkSchedulerCountNode* rootNode = localFound.root;
            WorkBase_TreeNodeFree(&localFound, rootNode->left);    // RCX = RBP-0x28 (0x1400f99ff)
            WorkBase_TreeNodeFree(&localFound, rootNode->right);   // 0x1400f9a0c
            System::MemoryAllocator::MemFree(localFound.root);
        }
    }
}
