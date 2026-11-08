// src/game/Component/citizenai_work_scheduler.cpp
// Clean C++ port of the CitizenAI work-scheduler priority-AVL INSERT family. This is the
// chokepoint both construction gather (2b) and build labor (2c) post through:
//
//   CitizenAI_impl_ec60 (0x14012ec60)  — post wrapper: stop existing work slot, then
//   CitizenAI_impl_88a0 (0x1400f88a0)  — the scheduler insert: a primary AVL keyed by
//                                        queue order + a per-id map + 14 category-bitmask
//                                        sub-trees + the per-PROFESSION bucket tree.
//   TradeWork_impl_ccf0 (0x14002ccf0)  — BST insert into a per-entity work tree (0x38 node)
//   CitizenAI_helper_cb50 (0x14002cb50)— insert-with-temp-copy wrapper over ccf0
//   TradeWork_impl_ccf0_sub_ceb0 (0x14002ceb0) — 0x38-node copy-ctor
//   FUN_14002cfb0 (0x14002cfb0)        — recursive 0x30-node subtree deep-copy
//
// The remaining callees are already registered in standalone:
//   a8c0 (0x14001a8c0) primary-AVL rebalance, 3480 (0x140013480) category-AVL rebalance,
//   9a40 (0x140029a40) id-map insert, 4b40 (0x140014b40) entity-tree rebalance,
//   e320 (0x14000e320) node-free, ead0 (0x14012ead0)/eb50 (0x14012eb50) work-slot ops.
//
// Node layouts: Game/WorkSchedulerNodes.h (WorkSchedulerIdNode 0x30, WorkSchedulerCountNode
// 0x28, WorkSchedulerBucketNode 0x38); the scheduler itself: Game/WorkScheduler.h.
//
// Faithful port; every node field is read by name.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/WorkScheduler.h"
#include "Game/WorkSchedulerNodes.h"
#include "Work/Work.h"   // B.3.8 CB1a (Work::GetTarget)
#include "Component/WorkComponent.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Component/exe_bodies.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Action/exe_bodies.h"
#include "Action/PathAvl.h"   // AvlRebalance (0x140013480)
#include "Work/exe_bodies.h"
#include "Player/exe_bodies.h"

// ==================================================================================
// FUN_14002cfb0 — recursive deep-copy of a 0x30-node subtree. ctx = the tree header
// being filled (unused beyond being threaded through recursion); src = source node.
// Returns the copy.
// ==================================================================================
static WorkSchedulerIdNode* WS_cfb0(WorkSchedulerTree* ctx, const WorkSchedulerIdNode* src) {
    WorkSchedulerIdNode* dst = static_cast<WorkSchedulerIdNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerIdNode), 8));
    WorkSchedulerIdNode* out = nullptr;
    if (dst) {
        dst->key = src->key;
        dst->work = Object::InternalPtr<Work>();
        dst->work = src->work;
        dst->parent = nullptr;
        dst->left = nullptr;
        dst->right = nullptr;
        dst->balance = 0;
        dst->height = 0;
        out = dst;
    }
    if (!out) return nullptr;
    out->height = src->height;
    out->balance = src->balance;
    WorkSchedulerIdNode* left = src->left;
    if (left) {
        WorkSchedulerIdNode* c = WS_cfb0(ctx, left);
        out->left = c;
        if (c) c->parent = out;
    }
    WorkSchedulerIdNode* right = src->right;
    if (right) {
        WorkSchedulerIdNode* c = WS_cfb0(ctx, right);
        out->right = c;
        if (c) c->parent = out;
    }
    return out;
}

// ==================================================================================
// TradeWork_impl_ccf0_sub_ceb0 — 0x38-node copy-ctor. dst = fresh 0x38 buffer,
// src = the bucket record to copy, parent = parent to record at dst+0x18.
// ==================================================================================
static WorkSchedulerBucketNode* WS_ceb0(WorkSchedulerBucketNode* dst, const WorkSchedulerBucket* src,
                                        WorkSchedulerBucketNode* parent) {
    dst->bucket.key = src->key;                  // key
    dst->bucket.entries.root = nullptr;          // subtree
    dst->bucket.entries.count = 0;
    // The original guards `src+1 != dst+1` (self-copy); for distinct nodes copy subtree+int.
    if (&src->entries != &dst->bucket.entries) {
        WorkSchedulerIdNode* sub = src->entries.root;
        if (sub) {
            WorkSchedulerIdNode* c = WS_cfb0(&dst->bucket.entries, sub);
            dst->bucket.entries.root = c;
        }
        dst->bucket.entries.count = src->entries.count;
    }
    dst->parent = parent;                        // parent
    dst->left = nullptr;                         // left
    dst->right = nullptr;                        // right
    dst->balance = 0;                            // balance
    dst->height = 0;
    return dst;
}

// ==================================================================================
// TradeWork_impl_ccf0 — BST insert (0x38 nodes) into the scheduler's +0x120 tree
// (root professionBucketRoot, count professionBucketCount). key = pKey->key. On existing key: writes node
// to *pOut and returns. Otherwise allocates via ceb0, links, rebalances (4b40).
// ==================================================================================
static WorkSchedulerBucketNode** WS_ccf0(WorkScheduler* ws, WorkSchedulerBucketNode** pOut,
                                         const WorkSchedulerBucket* pKey) {
    WorkSchedulerBucketNode* root = ws->professionBucketRoot;
    if (root == nullptr) {
        WorkSchedulerBucketNode* n = static_cast<WorkSchedulerBucketNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerBucketNode), 8));
        WorkSchedulerBucketNode* node = nullptr;
        if (n) node = WS_ceb0(n, pKey, nullptr);
        ws->professionBucketRoot = node;
        *pOut = node;
    } else {
        WorkSchedulerBucketNode* cur = root;
        WorkSchedulerBucketNode* parent = cur;
        for (;;) {
            parent = cur;
            Profession* nodeKey = cur->bucket.key;
            if (nodeKey <= pKey->key) {
                if (pKey->key <= nodeKey) {       // equal -> found existing
                    *pOut = cur;
                    return pOut;
                }
                // key > nodeKey -> go right (+0x28)
                cur = parent->right;
                if (cur == nullptr) {
                    WorkSchedulerBucketNode* buf = static_cast<WorkSchedulerBucketNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerBucketNode), 8));
                    WorkSchedulerBucketNode* node = buf ? WS_ceb0(buf, pKey, parent) : nullptr;
                    parent->right = node;
                    cur = node;
                    break;
                }
            } else {
                // key < nodeKey -> go left (+0x20)
                cur = parent->left;
                if (cur == nullptr) {
                    WorkSchedulerBucketNode* buf = static_cast<WorkSchedulerBucketNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerBucketNode), 8));
                    WorkSchedulerBucketNode* node = buf ? WS_ceb0(buf, pKey, parent) : nullptr;
                    parent->left = node;
                    cur = node;
                    break;
                }
            }
        }
        WorkSchedulerBucketNode* localTmp = parent;   // &local_res8 holds `parent` for the rebalance
        Avl38Retrace(&ws->professionBucketRoot, &localTmp);   // 0x140014b40 (TradeWork_impl_4b40's body)
        *pOut = cur;
    }
    ws->professionBucketCount += 1;   // count++
    return pOut;
}

// ==================================================================================
// CitizenAI_helper_cb50 — build a temp bucket record (copying its id tree via cfb0 from
// pSrc), insert it into the +0x120 tree via ccf0, then free the temp copy.
//   pKey = the bucket key, pSrc = optional {root, count} whose tree is deep-copied.
// ==================================================================================
static WorkSchedulerBucketNode** WS_cb50(WorkScheduler* ws, WorkSchedulerBucketNode** pOut,
                                         Profession* const* pKey, const WorkSchedulerTree* pSrc) {
    Profession* local_key = *pKey;
    WorkSchedulerIdNode* local_sub = nullptr;
    int32_t  local_int = 0;
    // Build a temp {key@0, sub@8, int@0x10} on the stack mirroring the node prefix.
    WorkSchedulerBucket tmp{};   // root, count and the padding zeroed
    tmp.key = local_key;
    if (pSrc != &tmp.entries) {
        if (pSrc && pSrc->root != nullptr) {
            local_sub = WS_cfb0(&tmp.entries, pSrc->root);
            tmp.entries.root = local_sub;
        }
        local_int = pSrc ? pSrc->count : 0;
        tmp.entries.count = local_int;
    }
    WS_ccf0(ws, pOut, &tmp);
    // free the temp deep-copied subtree
    if (tmp.entries.root) {
        WorkSchedulerIdNode* sub = tmp.entries.root;
        {
            WorkBase_TreeNodeFree(&tmp.entries.root, sub->left);
            WorkBase_TreeNodeFree(&tmp.entries.root, sub->right);
        }
        System::MemoryAllocator::MemFree(sub);
    }
    return pOut;
}

// ==================================================================================
// CitizenAI_impl_88a0 — the scheduler insert.
//   sched (int*) = gw+0x118. pWorkIBP = InternalBasePtr* to the work. code = category
//   bitmask (bits 1..14). fromExisting = param_4. prio = param_5.
// `prio` is a QUEUE-ORDER key, not an importance level: when fromExisting == 0 the caller's
// value is thrown away and replaced by sched[1]++ (a monotonic counter), so a caller passing
// 0 and a caller passing -1 behave identically. It is only honoured when fromExisting != 0,
// which is exactly the "re-post at the position I already had" case — the wrapper (ec60)
// sets that flag only when workComp+0x2c holds a previously assigned order (!= INT_MIN).
// Retail's only direct caller, FUN_14009c960, passes ('\0', -1): the value is dead there.
// Returns the assigned id.
// ==================================================================================
int WorkScheduler::Insert(Object::InternalPtr<Work>* workRef, uint32_t code,
                                              char fromExisting, int prio) {

    // --- assign id (wraps at 0x7ffffffe) ---
    lastWorkId += 1;
    int id = lastWorkId;
    if (id == 0x7ffffffe) lastWorkId = 0;

    // --- assign priority ---
    if (fromExisting == '\0') {
        prio = nextQueueOrder;
        nextQueueOrder = prio + 1;
    } else {
        // dedup: while a primary node already holds `prio`, decrement and restart search
        WorkSchedulerCountNode* p = queueOrderTree.root;
        while (p != nullptr) {
            int nodeKey = p->key;
            if (prio < nodeKey)      p = p->left;
            else if (nodeKey < prio) p = p->right;
            else { prio -= 1; p = queueOrderTree.root; }
        }
    }

    // --- stamp the work IBP: +0x20 = id, +0x24 = prio ---
    Work* work = workRef->Get();
    work->_workId = id;
    work = workRef->Get();
    work->_queueOrder = prio;

    // --- insert into the primary AVL (0x28 nodes) at sched+0x10, keyed by prio ---
    // key node scratch = {prio, id}
    struct { int prio; int id; } keyNode = { prio, id };
    WorkSchedulerCountNode* proot = queueOrderTree.root;
    if (proot == nullptr) {
        WorkSchedulerCountNode* n = static_cast<WorkSchedulerCountNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerCountNode), 8));
        if (n) {
            n->key = keyNode.prio;
            n->workId = keyNode.id;
            n->parent = nullptr;
            n->left = nullptr;
            n->right = nullptr;
            n->balance = 0;
            n->height = 0;
        }
        queueOrderTree.root = n;
    } else {
        WorkSchedulerCountNode* cur = proot;
        WorkSchedulerCountNode* parent = cur;
        bool linked = false;
        for (;;) {
            parent = cur;
            int nodeKey = cur->key;
            if (nodeKey <= prio) {
                if (prio <= nodeKey) break;   // equal (shouldn't happen post-dedup)
                cur = parent->right;
                if (cur == nullptr) {
                    WorkSchedulerCountNode* n = static_cast<WorkSchedulerCountNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerCountNode), 8));
                    if (n) {
                        n->key = keyNode.prio;
                        n->workId = keyNode.id;
                        n->parent = parent;
                        n->left = nullptr;
                        n->right = nullptr;
                        n->balance = 0;
                        n->height = 0;
                    }
                    parent->right = n;
                    linked = true; break;
                }
            } else {
                cur = parent->left;
                if (cur == nullptr) {
                    WorkSchedulerCountNode* n = static_cast<WorkSchedulerCountNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerCountNode), 8));
                    if (n) {
                        n->key = keyNode.prio;
                        n->workId = keyNode.id;
                        n->parent = parent;
                        n->left = nullptr;
                        n->right = nullptr;
                        n->balance = 0;
                        n->height = 0;
                    }
                    parent->left = n;
                    linked = true; break;
                }
            }
        }
        if (linked) {
            WorkSchedulerCountNode* tmp = parent;
            AvlRebalance(&queueOrderTree.root, &tmp);   // 0x14001a8c0
            queueOrderTree.count += 1;   // +0x18 primary count
        }
    }

    // --- id-map insert (9a40) at sched+0x30. arg2 = &{prio,id} key node; arg3 =
    //     &{id, pad, work} (the mapped value; ec60 later reads node+0x08 = the work). ---
    struct { int id; int pad; Object::InternalPtr<Work> work; } mapVal;
    mapVal.id = id; mapVal.pad = 0; mapVal.work = *workRef;
    WorkCellBST_Insert_9a40(&trees[0], &keyNode, &mapVal);

    // --- 14 category sub-trees at sched+0x40 (trees[1..14]), driven by `code` bits ---
    const Object::InternalPtr<Work> workPtr = *workRef;
    uint32_t mask = 2;
    for (int t = 0; t < 14; ++t) {
        if (code & mask) {
            WorkSchedulerTree* tree = &trees[1 + t];
            WorkSchedulerIdNode* croot = tree->root;
            if (croot == nullptr) {
                WorkSchedulerIdNode* n = static_cast<WorkSchedulerIdNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerIdNode), 8));
                if (n) {
                    n->key = id;
                    n->work = workPtr;
                    n->parent = nullptr;
                    n->left = nullptr;
                    n->right = nullptr;
                    n->balance = 0;
                    n->height = 0;
                }
                tree->root = n;
            } else {
                WorkSchedulerIdNode* cur = croot;
                WorkSchedulerIdNode* parent = cur;
                bool linked = false;
                for (;;) {
                    parent = cur;
                    int nodeKey = cur->key;
                    if (nodeKey <= id) {
                        if (id <= nodeKey) break;
                        cur = parent->right;
                        if (cur == nullptr) {
                            WorkSchedulerIdNode* n = static_cast<WorkSchedulerIdNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerIdNode), 8));
                            if (n) {
                                n->key = id;
                                n->work = workPtr;
                                n->parent = parent;
                                n->left = nullptr;
                                n->right = nullptr;
                                n->balance = 0;
                                n->height = 0;
                            }
                            parent->right = n;
                            linked = true; break;
                        }
                    } else {
                        cur = parent->left;
                        if (cur == nullptr) {
                            WorkSchedulerIdNode* n = static_cast<WorkSchedulerIdNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerIdNode), 8));
                            if (n) {
                                n->key = id;
                                n->work = workPtr;
                                n->parent = parent;
                                n->left = nullptr;
                                n->right = nullptr;
                                n->balance = 0;
                                n->height = 0;
                            }
                            parent->left = n;
                            linked = true; break;
                        }
                    }
                }
                if (linked) {
                    WorkSchedulerIdNode* tmp = parent;
                    AvlRebalance(&tree->root, &tmp);   // 0x140013480
                }
            }
            tree->count += 1;   // sub-tree count++
        }
        // signed-shift replicate: uVar17 = uVar17<<1 | (uVar17<0)
        mask = (mask << 1) | ((int)mask < 0 ? 1u : 0u);
    }

    // --- per-PROFESSION bucket tree at sched+0x120 (0x38 nodes), keyed by the Profession
    //     the work's vtable slot +0x50 returns. That slot is the work's "who can do this"
    //     getter, NOT an entity id: Pickup_Slot10 (0x1400f3a50) and
    //     GatherRawMaterials_Slot10 (0x1400ec510) both return *(*(gw+0xb0)+0x30), the
    //     Laborer key. It is the same key space the re-key (f8400) moves items between —
    //     its callers pass wpc+0x48 / *(*(gw+0xb0)+0x38) (Builder) / desc+0x20. Each bucket
    //     node holds its own id-keyed inner tree at +0x08 with a count at +0x10.
    //     If the bucket is absent, cb50 inserts a fresh one. ---
    Work* wobj = workRef->Get();
    Profession* slot50Buf[2] = {nullptr, nullptr};
    Profession** targetKey = wobj->GetTarget(slot50Buf);   // Work slot 10, retslot

    WorkSchedulerBucketNode* froot = professionBucketRoot;
    WorkSchedulerBucketNode* found = nullptr;
    if (froot != nullptr) {
        WorkSchedulerBucketNode* p = froot;
        while (p != nullptr) {
            Profession* nk = p->bucket.key;
            if (*targetKey < nk)       p = p->left;
            else if (*targetKey == nk) { found = p; break; }
            else                       p = p->right;
        }
    }
    if (found == nullptr) {
        // cb50 builds+inserts a fresh bucket node whose key = the work's profession.
        Work* wobj2 = workRef->Get();
        Profession* keyBuf[2] = {nullptr, nullptr};
        Profession** freshKey = wobj2->GetTarget(keyBuf);   // Work slot 10, retslot
        WorkSchedulerBucketNode* outNode = nullptr;
        const WorkSchedulerTree nullSrc{};   // {root=0, count=0}: no subtree to copy
        WS_cb50(this, &outNode, freshKey, &nullSrc);
        found = outNode;
    }

    // --- inner tree hanging off the found profession bucket (+0x08), 0x30 nodes keyed by id ---
    if (found) {
        WorkSchedulerTree* innerField = &found->bucket.entries;
        const Object::InternalPtr<Work> innerKeyPayload = *workRef;
        WorkSchedulerIdNode* iroot = innerField->root;
        if (iroot == nullptr) {
            WorkSchedulerIdNode* n = static_cast<WorkSchedulerIdNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerIdNode), 8));
            if (n) {
                n->key = id;
                n->work = innerKeyPayload;
                n->parent = nullptr;
                n->left = nullptr;
                n->right = nullptr;
                n->balance = 0;
                n->height = 0;
            }
            innerField->root = n;
        } else {
            WorkSchedulerIdNode* cur = iroot;
            WorkSchedulerIdNode* parent = cur;
            bool linked = false;
            for (;;) {
                parent = cur;
                int nodeKey = cur->key;
                if (nodeKey <= id) {
                    if (id <= nodeKey) return id;   // already present
                    cur = parent->right;
                    if (cur == nullptr) {
                        WorkSchedulerIdNode* n = static_cast<WorkSchedulerIdNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerIdNode), 8));
                        if (n) {
                            n->key = id;
                            n->work = innerKeyPayload;
                            n->parent = parent;
                            n->left = nullptr;
                            n->right = nullptr;
                            n->balance = 0;
                            n->height = 0;
                        }
                        parent->right = n;
                        linked = true; break;
                    }
                } else {
                    cur = parent->left;
                    if (cur == nullptr) {
                        WorkSchedulerIdNode* n = static_cast<WorkSchedulerIdNode*>(System::MemoryAllocator::MemAlloc(sizeof(WorkSchedulerIdNode), 8));
                        if (n) {
                            n->key = id;
                            n->work = innerKeyPayload;
                            n->parent = parent;
                            n->left = nullptr;
                            n->right = nullptr;
                            n->balance = 0;
                            n->height = 0;
                        }
                        parent->left = n;
                        linked = true; break;
                    }
                }
            }
            if (linked) {
                WorkSchedulerIdNode* tmp = parent;
                AvlRebalance(&innerField->root, &tmp);   // 0x140013480
            }
        }
        found->bucket.entries.count += 1;   // profession bucket count++
    }

    // The flat id-map at sched+0x30 is filed here (the 9a40 call above) and is the thing
    // f8400 needs to find a work item by id. It was recorded as "never populates"; report its
    // root and count right where it is written so that claim is measured at the source rather
    // than inferred from the re-key's downstream "NOT in the sched+0x30 id-map" miss. NB most
    // posts reach this function DIRECTLY (exe 0x1400f88a0), not through the ec60 wrapper — a
    // trace placed only in the wrapper sees nothing.
    return id;
}

// ==================================================================================
// CitizenAI_impl_ec60 — post wrapper. workComp = the WorkComponent, code = 0x2001,
// flag = param_3, pWorkIBP = the work IBP.
// ==================================================================================
void WorkComponent::PostWork(uint32_t code, char flag, Object::InternalPtr<Work>* pWorkIBP) {
    if (IsWorkPosted()) {          // 0x14012ead0
        CancelWork();              // 0x14012eb50
    }
    char prioFlag;
    if (flag == '\0' || workFlags == WorkComponent::kWorkFlagsNone)
        prioFlag = '\0';
    else
        prioFlag = '\x01';

    // B6 row 200: 0x14012ecb7 / 0x14012ecbe read [0x1401c3dd8]+0x118 with no test.
    WorkScheduler* sched = GameUpdateLoop::_singleton->m_tutorial;

    // Faithful to ec60: pass a pointer to a local copy of the work-ptr (88a0 only reads it).
    Object::InternalPtr<Work> localWork = *pWorkIBP;
    int assignedId = sched->Insert(&localWork, code, prioFlag,
                                                    workFlags);
    workId = assignedId;

    // Walk the sched+0x30 id-map (trees[0]) for the assigned id; on match write back
    // workComp+0x2c = the work's stored priority (Work +0x24). On miss, 0.
    WorkSchedulerIdNode* p = sched->trees[0].root;
    Work* payload = nullptr;
    while (p != nullptr) {
        int nk = p->key;
        if (nk <= assignedId) {
            if (assignedId <= nk) { payload = p->work.Get(); break; }
            p = p->right;
        } else {
            p = p->left;
        }
    }
    // On a miss retail builds a NULL InternalPtr (0x14012ed09..0x14012ed1b) and reads
    // [RAX+0x24] through it (0x14012ed2b): no test, a null read, not a 0 (B6 row 200).
    workFlags = payload->_queueOrder;
    // The id-map half of the post is the one nothing downstream can see going wrong: a miss
    // here silently writes priority 0 into workComp+0x2c and makes the f8400 re-key a no-op
    // ("NOT in the sched+0x30 id-map"). Report the map's own state next to the lookup result
    // so "the id-map never populates" is a measured fact and not an inference from a
    // downstream symptom.
}

// ==================================================================================
// FUN_1400f8400 — re-key a pending work item between profession buckets.
// ==================================================================================
// The scheduler files every posted work item twice: once in the flat id-map at sched+0x30
// (0x30 nodes keyed by the int work id, payload = the work object), and once inside a
// per-PROFESSION bucket tree at sched+0x120 (0x38 nodes keyed by the u64 profession key,
// each holding its own id-keyed inner tree at +0x08 with a count at +0x10). Citizens of a
// profession only ever see the items in their own bucket.
//
// When a building changes profession — a demolition or upgrade re-keys its WorkPlaceComponent
// to Builder (0x14012f520), a completed building hands it back to its natural profession
// (0x14012f670) — any work item still in flight has to move buckets with it. Without this,
// the item keeps its pre-demolition key and gets offered to the wrong profession's citizens.
//
//   sched  = *(gameWorld+0x118)
//   workId = WorkComponent+0x28 (>= 0 means an item is in flight; -1 means nothing to move)
//   oldKey / newKey = profession keys
//
// Faithful 1:1 port; the erase (ca60) and insert (9a40) helpers maintain the bucket counts
// themselves, which is why retail never touches +0x10 here.
void WorkScheduler::Rekey(uint32_t workId,
                                              Profession* const oldProf, Profession* const newProf) {
    // B6 row 200: retail reads [RCX+0x30] with no test (0x1400f842a).
    const int key = static_cast<int>(workId);

    // 1. The id-map entry (sched+0x30) — its +0x08 payload is what gets re-filed. Retail
    //    bails when the id is unknown, before touching either bucket.
    WorkSchedulerIdNode* idNode = trees[0].root;
    while (idNode) {
        int nk = idNode->key;
        if (nk > key)      idNode = idNode->left;
        else if (key > nk) idNode = idNode->right;
        else break;
    }
    if (!idNode) {
        // FAITHFUL NO-OP, not a failure (open D closed 2026-07-26): retail never
        // resets workComp+0x28 when work completes (ctor 0x14012e820 seeds -1 and
        // ec60 is the only writer), so the edf0/f670 completion site calls this
        // re-key with a stale id and retail ALSO finds nothing — the sched+0x30
        // lookup IS the engine's liveness test (same early-out in ead0).
        return;
    }

    // 2. The OLD profession's bucket (the +0x120 tree).
    WorkSchedulerBucketNode* oldBucket = professionBucketRoot;
    while (oldBucket) {
        Profession* bk = oldBucket->bucket.key;
        if (oldProf < bk)      oldBucket = oldBucket->left;
        else if (bk < oldProf) oldBucket = oldBucket->right;
        else break;
    }
    if (!oldBucket) {
        // Retail still materialises the (empty) bucket for oldKey and then returns: with no
        // bucket there is nothing filed to move. cb50's temp node is freed inside cb50.
        WorkSchedulerBucketNode* out = nullptr;
        const WorkSchedulerTree noSubtree{};
        Profession* k = oldProf;
        WS_cb50(this, &out, &k, &noSubtree);
        return;
    }

    // 3. The item itself, in that bucket's inner id-keyed tree (bucket+0x08).
    WorkSchedulerIdNode* item = oldBucket->bucket.entries.root;
    while (item) {
        int nk = item->key;
        if (nk > key)      item = item->left;
        else if (key > nk) item = item->right;
        else break;
    }
    if (!item) {
        return;
    }

    // 4. Unfile it (ca60 frees the node and decrements bucket+0x10).
    WorkCellBST_Remove_ca60(&oldBucket->bucket.entries, item);   // 0x14001ca60

    // 5. The NEW profession's bucket, created if this is its first item.
    WorkSchedulerBucketNode* newBucket = nullptr;
    for (WorkSchedulerBucketNode* p = professionBucketRoot; p; ) {
        Profession* bk = p->bucket.key;
        if (newProf < bk)      p = p->left;
        else if (bk < newProf) p = p->right;
        else { newBucket = p; break; }
    }
    if (!newBucket) {
        WorkSchedulerBucketNode* out = nullptr;
        const WorkSchedulerTree noSubtree{};
        Profession* k = newProf;
        WS_cb50(this, &out, &k, &noSubtree);
        newBucket = out;
    }
    // Retail derefs the new bucket with no test (`puVar6 = *puVar4`, then puVar6 + 1 to
    // 0x140029a40's insert): B6 row 200 dropped the tree's allocator-failure return.

    // 6. Re-file under the new key. The pair is {id, pad, payload}; 9a40 reads the u32 at +0
    //    and the u64 at +8, and bumps newBucket+0x10.
    struct { uint32_t id; uint32_t pad; Object::InternalPtr<Work> payload; } pair;
    pair.id      = workId;
    pair.pad     = 0;
    pair.payload = idNode->work;
    void* outSlot = nullptr;
    WorkCellBST_Insert_9a40(&newBucket->bucket.entries, &outSlot, &pair);
}
