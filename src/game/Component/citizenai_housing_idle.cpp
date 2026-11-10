// src/game/Component/citizenai_housing_idle.cpp
//
// Brick 3b handler wave (FindHousing + Idle) — faithful 1:1 ports transcribed from the
// Ghidra decompile, offset-by-offset, of the two "non-pushing" CitizenAIComponent state
// handlers and their entire shared deep AVL/BST helper subtree:
//
//   0x14010b340  CitizenAI_State_FindHousing  (state id 2)  -> helper_7550
//   0x14010b2d0  CitizenAI_State_Idle         (state id 9)  -> helper_7830
//   0x1400f7550  CitizenAI_helper_7550        (candidate-roll over the per-state remembered-
//                                              target trees; calls CanFindHousing, 3f010,
//                                              79b0, Random::Rand)
//   0x1400f7830  CitizenAI_helper_7830        (Idle: probe one specific tree key, else 7550)
//   0x1400f79b0  CitizenAI_helper_sub_79b0    (the AVL walker — builds a transient result tree
//                                              via 7c90/8000, walks it via 7b60, frees via e320)
//   0x1400f7b60  CitizenAI_helper_sub_79b0_sub_7b60  (in-order walk of the result tree;
//                                              dispatches each payload's vtable +0x48 / +0x20)
//   0x1400f7c90  CitizenAI_helper_sub_79b0_sub_7c90  (cost-keyed AVL insert, slope-aware cost)
//   0x1400f8000  CitizenAI_helper_sub_79b0_sub_8000  (cost-keyed AVL insert, simple cost field)
//   0x14003f010  FUN_14003f010                (memset-0xFF leaf; pure)
//   0x14000e360  TradeDescription_impl_e360   (AVL rebalance walker; calls registered rotations
//                                              e450/e580) — UNREGISTERED elsewhere, ported here.
//
// FindHousing/Idle are "non-pushing": they do NOT build/push an Action onto the citizen action
// stack themselves. They invoke the helper (which may itself push via the payload-object virtual
// at vtable+0x20), then return CONCAT71(citizen>>8, 0 < (*(citizen+0x278)+0x60) + 1). i.e. the
// boolean low byte = "the character-component action-stack-depth field at +0x60 is >= 0" — which
// is essentially always true (it is -1 only when uninitialised). They do NOT stamp cooldowns.
//
// SELF CHAIN: param_1 = CitizenAIComponent. self/+0x38 = sibling CitizenComponent IBP.
//   citizen = *self. citizen+0x278 = CharacterComponent (its +0x60 (int) = action depth used by
//   the return expr). citizen+0x1f0 = the citizen's "home/owner key" (a building-instance ptr,
//   compared against tree keys and against gameWorld+0xb0 +0x30/+0x38). The trees searched live
//   on the GameWorld-derived object passed as helper param_1 = *(g_pGameWorldSingleton + 0x118).
//
// TREE-ROOT ARRAY (helper param_1, base = *(g_pGameWorldSingleton+0x118)):
//   a flat array of {key, root}-ish 0x10-byte records. helper_7550 indexes it as
//   *(param_1 + (stateByte+3)*0x10) to fetch the AVL root for a candidate-state code, then
//   follows the node +0x18 (left) chain to its leftmost node before walking. helper_7830 reads
//   the secondary BST at param_1+0x120 (a {key@+0, valueRoot@+8, left@+0x20, right@+0x28}-style
//   ordered tree) keyed by the citizen's +0x1f0 owner key.
//
// (B4 P2 CAI-4: the structs below are now Game/WorkSchedulerNodes.h WorkCandidateNode /
//  WorkCandidateTree, and the source trees WorkSchedulerIdNode / WorkSchedulerBucketNode.)
// === RECOVERED AVL NODE STRUCT (the transient result tree built by 7c90/8000, walked by 7b60,
//     freed by 79b0 via e320, rebalanced by e360) — 0x30 bytes, MemAlloc(0x30,8):
//     +0x00  void*   payload         (entity IBP / candidate object; node+0)
//     +0x08  int32   costKey         (sort key: path-cost / id; node[1] low dword)
//     +0x10  void*   parent
//     +0x18  void*   left
//     +0x20  void*   right
//     +0x28  int16   balance         (right.height - left.height)
//     +0x2a  int16   height          (max(childHeights)+1)
//   7c90 secondary tiebreak compares (payload+0x20) int field; 8000 uses costKey only.
//   7b60 reads (payload+0x2c) as a per-pass visit-stamp vs (helperCtx+0x8); dispatches the
//   payload object's vtable: (+0x48)() [refresh], then (+0x20)(payload, &citizenIBP, ctx+0x130)
//   returning int {0 skip, 1 mark-visited, 2 success-stop}.
//
// SOURCE TREE node layout (the per-state remembered-target trees at param_1[(b+3)*0x10] and the
//   param_1+0x120 BST) — read-only here:
//     +0x00 key ; +0x08 valueSubtreeRoot ; +0x10 ??? ; +0x18 left ; +0x20 right
//   (7550 walks +0x18 to leftmost; 7830 BST: <key -> +0x20(node[4]), >key -> +0x28(node[5]),
//    ==key -> value at +0x08(node[1]) then +0x18(node[3]) to leftmost.)
//
// REGISTERED exe sub-callees (resolved via ResolveAddr; bodies NOT duplicated):
//   0x140115be0  CitizenComponent_CanFindHousing   (reg_component_gap_A.cpp)
//   0x14000e320  WorkBase_TreeNodeFree_e320         (reg_work_base.cpp)  — frees node +0x18/+0x20
//   0x14009f810  Pf_IsDstReachable_f810/BuildStructure_impl_f810 (reg_pf_roots_route.cpp)
//   0x140132010  WorkImpl_Sqrtf_132010              (reg_work_impl2.cpp) — sqrtf
//   0x14000e450  Pf_AvlRotateB_e450                 (reg_pf_avl.cpp)     — called by e360
//   0x14000e580  Pf_AvlRotateB_e580                 (reg_pf_avl.cpp)     — called by e360
//
// e320 REG VERDICT: REGISTERED (standalone/reg_work_base.cpp:42). Confirmed by grep. Not re-ported.
//
// Engine externals (engine_funcs.h): fn_InternalBasePtr_CheckPtr, fn_InternalBasePtr_Ctor,
//   fn_MemAlloc(+g_pMemAllocSingleton), fn_MemFreeStatic, fn_Random_Rand, g_pGameWorldSingleton.
//


#include <new>
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Work/Work.h"      // B.3.x W0 G-B: Work::Validate / Execute by name
#include "Component/CitizenAIComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Action/PathAvl.h"   // AvlRotateLeft / AvlRotateRight (0x14000e450 / 0x14000e580)
#include "Work/exe_bodies.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors
#include "Game/WorkScheduler.h"        // WorkScheduler (GameUpdateLoop +0x118)
#include "Game/WorkSchedulerNodes.h"   // WorkSchedulerIdNode / BucketNode, WorkCandidateNode / Tree
#include "Game/LegacyKeyIntNode.h"     // LegacyKeyIntNode (a tree of the e360 walk)
#include "Component/TradeComponent.h"  // TradeResourceNode (a tree of the e360 walk)
#include "Game/Map.h"                  // Map::size
#include "Game/ProfessionList.h"
#include "Game/Profession.h"
#include "Component/ModelComponent.h"
#include "Entity/CitizenBehavior.h"    // ProfessionResourceEntry
#include "ext/registry.h"              // EXT SEAM (NOT RETAIL, WORKCAND.md): Ext::Ext().work_cand
#include "ext/work_cand/work_cand.h"   // EXT SEAM (NOT RETAIL, WORKCAND.md): the sorted flat candidate list
#include "stress_harness.h"            // g_verifyOn / StressHarness_Verify (WORKCAND's COMPPROF=4 shadow)
#include <memory>
#include <vector>

// 2c-B probe: the last BuildStructure work spawned by State8Enter (buildcomponent_state8.cpp).
extern "C" void* g_pBuildStructureWork;
// Same probe idiom, pointed at the crop field's PlantArea. The field posts its work and
// the scheduler assigns it an id, yet PlantArea::Execute is never called — so the open
// question is whether the citizen claim walk ever REACHES that work, or whether it is
// reached and rejected. Those need different fixes and are indistinguishable from
// outside. Set by CF_PostWork (cropfieldcomponent_states.cpp).
extern "C" void* g_pCropPlantWork;

namespace {

// --- registered exe sub-callees: called by name (Component/Action/Work exe_bodies.h) ---


static inline WorkCandidateNode* NodeAlloc() {
    return static_cast<WorkCandidateNode*>(System::MemoryAllocator::MemAlloc(0x30, 8));
}

// Idle work-search counters, read by standalone/road_probe.cpp. The road wave needs to
// tell "no builder ever searched" from "a builder searched and its bucket had nothing it
// would take" -- those look identical from the map.
extern "C" {
long long g_idleSearchCalls = 0;
long long g_idleSearchByBuilder = 0;
long long g_idleBuilderBucketHit = 0;
long long g_idleBuilderTookWork = 0;
}

// Forward decls for the helper subtree.
void          Helper_sub_8000(CitizenComponent* p1, ComponentEntity* const* keyHolder,
                              WorkSchedulerIdNode* const* srcRootSlot, WorkCandidateTree* dst);       // 0x1400f8000
void          Helper_sub_7c90(CitizenComponent* p1, CitizenComponent* const* citizenHolder, ComponentEntity* const* keyHolder,
                              WorkSchedulerIdNode* const* srcRootSlot, WorkCandidateTree* dst);       // 0x1400f7c90
uint8_t       Helper_sub_7b60(WorkScheduler* ctx, CitizenComponent* const* citizenHolder, WorkCandidateTree* dst); // 0x1400f7b60
uint64_t      Helper_sub_79b0(WorkScheduler* param_1, CitizenComponent* const* param_2,
                              WorkSchedulerIdNode* const* param_3, Profession* param_4,
                              char param_5, char param_6);                                 // 0x1400f79b0
uint64_t      Helper_7550(WorkScheduler* param_1, CitizenComponent* const* param_2);      // 0x1400f7550
// EXT BODY (NOT RETAIL, docs/extensions/handins/WORKCAND.md): ext.work_cand's plain-cost walk (below 79b0).
uint64_t      Helper_sub_79b0_Flat(WorkScheduler* param_1, CitizenComponent* const* param_2,
                                   WorkSchedulerIdNode* const* param_3, Profession* param_4, char param_6);

// ============================================================
// 0x14003f010 — memset(dst, byteVal, count). Pure leaf; just call memset.
// ============================================================
void* Helper_3f010(unsigned char* dst, uint64_t byteVal, uint64_t count) {
    // Faithful to behaviour: fills `count` bytes with (byte)byteVal. The exe variant has
    // hand-unrolled 64-byte and 8-byte loops + a LOCK fence for large fills; the observable
    // effect is identical to memset.
    unsigned char* p = dst;
    unsigned char b = static_cast<unsigned char>(byteVal);
    for (uint64_t i = 0; i < count; ++i) p[i] = b;
    return dst;
}

// ============================================================
// 0x1400f8000 — insert each source-tree node into the dst AVL keyed by (Work +0x24).
//   param_1 unused-ish holder; param_2 = &(skip-key) (when non-null, only inserts the node
//   whose Work +0x10 matches *param_2); param_3 = &srcRoot; param_4 = the dst tree.
//   Source tree traversed in-order via node +0x10(parent)/+0x18(left)/+0x20(right).
// ============================================================
void Helper_sub_8000(CitizenComponent* /*param_1: the citizen, unread*/, ComponentEntity* const* param_2, WorkSchedulerIdNode* const* param_3,
                     WorkCandidateTree* param_4) {
    if (*param_3 == nullptr) return;
    WorkSchedulerIdNode* cur = *param_3;
    // retail's RCX home slot (0x1400f8000: the citizen), re-used as the descent node; the
    // citizen it holds on entry is never read (every e360 call follows a store below).
    WorkCandidateNode* local_res8 = nullptr;
    do {
        bool doInsert = false;
        if (*param_2 == nullptr) {
            doInsert = true;
        } else {
            Work* payload = cur->work.Get();
            ComponentEntity* k = payload->_entity.Get();
            if (k == *param_2) doInsert = true;
        }

        if (doInsert) {
            Work* payload = cur->work.Get();   // local_28
            int costKey = payload->_queueOrder;   // iVar6 / local_20
            bool inserted = true;   // false on duplicate (JG/JGE 0x81fc -> skip INC + rebalance)
            WorkCandidateNode* existing = param_4->root;
            if (existing == nullptr) {
                WorkCandidateNode* nn = NodeAlloc();
                if (nn == nullptr) { param_4->root = nullptr; }
                else {
                    nn->work = payload;
                    nn->cost = costKey;
                    nn->parent = nullptr; nn->left = nullptr; nn->right = nullptr;
                    nn->balance = 0; nn->height = 0;   // retail: one dword store (0x1400f80bb)
                    param_4->root = nn;
                }
            } else {
                bool descendLeft = false;
                for (;;) {
                    WorkCandidateNode* self = existing;
                    // Track the descent node for the post-insert rebalance. The originals
                    // (0x1400f8000 / 0x1400f7c90) do `local_res8 = pIVar3` at the top of this
                    // loop, so CitizenAI_Reg_helper_e360 walks UP from the insertion node. The port left
                    // local_res8 pinned at param_1 (the citizen), so e360 read citizen fields
                    // as AVL links and faulted on the -1 they held (READ of -1 crash once an
                    // idle citizen's candidate tree grew past its root). Restore the update.
                    local_res8 = self;
                    if (self->cost <= costKey) {
                        // self.key <= costKey
                        if (costKey <= self->cost) {
                            // equal key: tiebreak on Work +0x20
                            int iv = payload->_workId;
                            int sv = self->work->_workId;
                            if (iv < sv) { descendLeft = true; }
                        }
                        if (!descendLeft) {
                            if (costKey <= self->cost) {
                                if (costKey < self->cost) { inserted = false; break; }
                                int sv2 = self->work->_workId;
                                int iv2 = payload->_workId;
                                if (iv2 <= sv2) { inserted = false; break; }
                            }
                            // go right (+0x20)
                            WorkCandidateNode* rc = self->right;
                            if (rc == nullptr) {
                                WorkCandidateNode* nn = NodeAlloc();
                                if (nn == nullptr) { self->right = nullptr; }
                                else {
                                    nn->work = payload;
                                    nn->cost = costKey;
                                    nn->parent = self; nn->left = nullptr; nn->right = nullptr;
                                    nn->balance = 0; nn->height = 0;   // retail: one dword store (0x1400f81a0)
                                    self->right = nn;
                                }
                                CitizenAI_Reg_helper_e360(&param_4->root, &local_res8);
                                break;
                            }
                            existing = rc;
                            continue;
                        }
                    }
                    // go left (+0x18)
                    {
                        WorkCandidateNode* lc = self->left;
                        if (lc == nullptr) {
                            WorkCandidateNode* nn = NodeAlloc();
                            if (nn != nullptr) {
                                nn->work = payload;
                                nn->cost = costKey;
                                nn->parent = self; nn->left = nullptr; nn->right = nullptr;
                                nn->balance = 0; nn->height = 0;   // retail: one dword store (0x1400f81e3)
                            }
                            self->left = nn;
                            CitizenAI_Reg_helper_e360(&param_4->root, &local_res8);
                            break;
                        }
                        existing = lc;
                        descendLeft = false;
                        continue;
                    }
                }
            }
            if (inserted) param_4->count = param_4->count + 1;
        }
        // (else: skip-key set and key didn't match -> no insert; matches the decompile's
        //  `if (key == *param_2) goto LAB_805c` which doInsert already captured above.)

        // in-order successor in the source tree (parent=+0x10, left=+0x18, right=+0x20)
        WorkSchedulerIdNode* rc = cur->right;
        WorkSchedulerIdNode* next;
        if (rc == nullptr) {
            WorkSchedulerIdNode* up = cur->parent;
            if (up != nullptr) {
                if (up->left == cur) { next = up; goto advance; }
                while (up != nullptr && up->right == cur) {
                    cur = up;
                    up = up->parent;
                }
            }
            next = cur->parent;
        } else {
            WorkSchedulerIdNode* n2 = rc;
            while (n2->left != nullptr) n2 = n2->left;
            next = n2;
        }
    advance:
        cur = next;
    } while (cur != nullptr);
}

// ============================================================
// 0x1400f7c90 — like 8000 but cost is computed (slope/distance via f810+sqrtf) and the source
//   node is gated on (Work +0x10 == *param_3 skip-key) OR (param_3 holds null). param_1 holder,
//   param_2 = &citizen (for the citizen model cell), param_3 = &skipKey, param_4 = &srcRoot,
//   param_5 = the dst tree.
// ============================================================
void Helper_sub_7c90(CitizenComponent* /*param_1: the citizen, unread*/, CitizenComponent* const* param_2,
                     ComponentEntity* const* param_3, WorkSchedulerIdNode* const* param_4,
                     WorkCandidateTree* param_5) {
    Map* map = GameUpdateLoop::_singleton->m_game;
    int* mapGrid = &map->size;
    // retail's RCX home slot (0x1400f7c95), re-used as the descent node (see 8000)
    WorkCandidateNode* local_res8 = nullptr;

    // citizen model cell (clamped row*w+col) -> uVar11
    CitizenComponent* cz = *param_2;
    CharacterComponent* ch = cz->character.Get();
    ModelComponent* model = ch->modelComponent.Get();
    int w = *mapGrid;
    int col = static_cast<int>(model->transform.translation.x + static_cast<float>(w >> 1));
    int row = static_cast<int>(model->transform.translation.y + static_cast<float>(w >> 1));
    if (col < 0) col = 0; if (w <= col) col = w - 1;
    if (row < 0) row = 0; if (w <= row) row = w - 1;
    uint32_t fromCell = static_cast<uint32_t>(w * row + col);

    WorkSchedulerIdNode* cur = *param_4;
    while (cur != nullptr) {
        bool doInsert = false;
        if (*param_3 == nullptr) {
            doInsert = true;
        } else {
            ComponentEntity* key = cur->work->_entity.Get();
            if (key == *param_3) doInsert = true;
        }

        if (doInsert) {
            uint32_t toCell = static_cast<uint32_t>(cur->work->_cell);
            int cost = map->PathDistance(fromCell, toCell, '\x01');
            if (cost < 0) {
                // unreachable via path graph -> euclidean fallback on grid coords
                int gw = *mapGrid;
                int dr = static_cast<int>(fromCell) / gw - static_cast<int>(toCell) / gw;
                int dc = static_cast<int>(fromCell) % gw - static_cast<int>(toCell) % gw;
                float d = WorkImpl_Sqrtf_132010(static_cast<float>(dr * dr + dc * dc));
                cost = static_cast<int>(d) + gw;
            }
            Work* payload = cur->work.Get();   // local_38
            int costKey = cost;                // local_30
            bool inserted = true;    // false on duplicate -> skip the count INC + rebalance
            WorkCandidateNode* existing = param_5->root;
            if (existing == nullptr) {
                WorkCandidateNode* nn = NodeAlloc();
                if (nn == nullptr) { param_5->root = nullptr; }
                else {
                    nn->work = payload; nn->cost = costKey;
                    nn->parent = nullptr; nn->left = nullptr; nn->right = nullptr;
                    nn->balance = 0; nn->height = 0;   // retail: one dword store
                    param_5->root = nn;
                }
            } else {
                bool descendLeft = false;
                for (;;) {
                    WorkCandidateNode* self = existing;
                    // Track the descent node for the post-insert rebalance. The originals
                    // (0x1400f8000 / 0x1400f7c90) do `local_res8 = pIVar3` at the top of this
                    // loop, so CitizenAI_Reg_helper_e360 walks UP from the insertion node. The port left
                    // local_res8 pinned at param_1 (the citizen), so e360 read citizen fields
                    // as AVL links and faulted on the -1 they held (READ of -1 crash once an
                    // idle citizen's candidate tree grew past its root). Restore the update.
                    local_res8 = self;
                    if (self->cost <= costKey) {
                        if (costKey <= self->cost) {
                            int iv = payload->_workId;
                            int sv = self->work->_workId;
                            if (iv < sv) descendLeft = true;
                        }
                        if (!descendLeft) {
                            if (costKey <= self->cost) {
                                if (costKey < self->cost) { inserted = false; break; }
                                int sv2 = self->work->_workId;
                                int iv2 = payload->_workId;
                                if (iv2 <= sv2) { inserted = false; break; }
                            }
                            WorkCandidateNode* rc = self->right;
                            if (rc == nullptr) {
                                WorkCandidateNode* nn = NodeAlloc();
                                if (nn == nullptr) { self->right = nullptr; }
                                else {
                                    nn->work = payload; nn->cost = costKey;
                                    nn->parent = self; nn->left = nullptr; nn->right = nullptr;
                                    nn->balance = 0; nn->height = 0;   // retail: one dword store
                                    self->right = nn;
                                }
                                CitizenAI_Reg_helper_e360(&param_5->root, &local_res8);
                                break;
                            }
                            existing = rc;
                            continue;
                        }
                    }
                    {
                        WorkCandidateNode* self2 = existing;
                        WorkCandidateNode* lc = self2->left;
                        if (lc == nullptr) {
                            WorkCandidateNode* nn = NodeAlloc();
                            if (nn != nullptr) {
                                nn->work = payload; nn->cost = costKey;
                                nn->parent = self2; nn->left = nullptr; nn->right = nullptr;
                                nn->balance = 0; nn->height = 0;   // retail: one dword store
                            }
                            self2->left = nn;
                            CitizenAI_Reg_helper_e360(&param_5->root, &local_res8);
                            break;
                        }
                        existing = lc;
                        descendLeft = false;
                        continue;
                    }
                }
            }
            if (inserted) param_5->count = param_5->count + 1;
        }

        // in-order successor in src tree
        WorkSchedulerIdNode* rc = cur->right;
        WorkSchedulerIdNode* next;
        if (rc == nullptr) {
            next = cur->parent;
            if (next != nullptr) {
                if (next->left != cur) {
                    WorkSchedulerIdNode* up = next;
                    while (up != nullptr && up->right == cur) {
                        cur = up;
                        up = up->parent;
                    }
                    next = cur->parent;
                }
            }
        } else {
            WorkSchedulerIdNode* n2 = rc;
            while (n2->left != nullptr) n2 = n2->left;
            next = n2;
        }
        cur = next;
    }
}

// ============================================================
// 0x1400f7b60 — in-order walk of the dst result tree (param_3). For each node whose
//   Work visit-stamp (Work +0x2c) != the scheduler's step count (param_1 +8): call Work slot 9
//   Validate() (refresh), then slot 4 Execute(&citizen, &scheduler->time) -> int. 2 => stop/success
//   (return 1). 1 => mark visited (Work +0x2c = step count). 0 => skip. Walk uses node +0x10
//   parent / +0x18 left / +0x20 right.
// ============================================================
uint8_t Helper_sub_7b60(WorkScheduler* param_1, CitizenComponent* const* param_2, WorkCandidateTree* param_3) {
    WorkCandidateNode* cur = param_3->root;
    if (cur == nullptr) return 0;
    // descend to leftmost (+0x18)
    while (cur->left != nullptr) cur = cur->left;

    while (cur != nullptr) {
        if (cur->work->_lastRejectStep != static_cast<int32_t>(param_1->stepCount)) {
            // vtable +0x48 refresh
            cur->work->Validate();   // slot 9; 0x1400f7bc3 RCX only
            CitizenComponent* citizen = *param_2;  // local_res18[0] = *param_2 (citizen)
            uint32_t r = static_cast<uint32_t>(cur->work->Execute(&citizen, &param_1->time));   // slot 4; 0x1400f7be9
            if (static_cast<int>(r) == 2) return 1;
            if (static_cast<int>(r) == 1) {
                cur->work->_lastRejectStep = static_cast<int32_t>(param_1->stepCount);
            }
        }
        // in-order successor: node +0x20 (right) subtree's leftmost, else climb +0x10 parents
        WorkCandidateNode* rc = cur->right;
        if (rc == nullptr) {
            WorkCandidateNode* up = cur->parent;
            if (up == nullptr) { cur = nullptr; continue; }
            if (up->left == cur) { cur = up; continue; }
            // climb while cur is the right child
            while (up != nullptr && up->right == cur) {
                cur = up;
                up = up->parent;
            }
            cur = cur->parent;
        } else {
            WorkCandidateNode* n2 = rc;
            while (n2->left != nullptr) n2 = n2->left;
            cur = n2;
        }
    }
    return 0;
}

// ============================================================
// 0x1400f79b0 — the AVL walker driver. Builds a transient cost-keyed result tree (local_40)
//   from a source subtree (param_3), then walks it (7b60). param_5 selects the cost mode
//   (7c90 slope-aware vs 8000 plain). param_6 forces the "whole-list" mode (fresh empty key
//   holder). On finish, frees the result tree via e320 + MemFree.
//
//   param_1 = the work scheduler (+8 = step count, +0x130 = the clock handed to Work slot 4)
//   param_2 = &citizen
//   param_3 = &srcSubtreeRoot (a node of a scheduler id tree)
//   param_4 = the citizen's profession (citizen+0x1f0)
//   param_5 = cost mode (1 => 7c90, 0 => 8000)
//   param_6 = "all" flag
// ============================================================
// VERIFIED against disassembly (offsets 45-95): srcRoot passed to 8000/7c90 is ALWAYS *param_3
// (the walked source node, [RBP+0x38]); the skip-key holder is the +0x248 gate object on the
// key-match path and a FRESH empty IBP on the common path; the citizen (*param_2) is 7c90's
// param_2 and 7b60's citizen. local_50=[RBP-0x28], local_58=[RBP-0x30].
//   8000(citizen, &skipKeyHolder, &srcRoot, &dstRoot)
//   7c90(citizen, &citizen,      &skipKeyHolder, &srcRoot, &dstRoot)
uint64_t Helper_sub_79b0(WorkScheduler* param_1, CitizenComponent* const* param_2,
                         WorkSchedulerIdNode* const* param_3, Profession* param_4,
                         char param_5, char param_6) {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/WORKCAND.md): ext.work_cand -- the plain-cost build
    // (0x1400f8000) into a sorted flat array instead of this transient AVL; the COMPPROF=4 shadow builds both.
    if (param_5 == '\0' && (::Ext::Ext().work_cand || g_verifyOn != 0)) {
        return Helper_sub_79b0_Flat(param_1, param_2, param_3, param_4, param_6);
    }
    Profession* local_res20 = param_4;
    CitizenComponent* citizen0 = *param_2;
    ComponentEntity* gate = citizen0->workplace.Get();   // pIVar5 = *(citizen+0x248)

    if (param_6 != '\0') {
        ::Object::InternalPtr<ComponentEntity> freshGate(nullptr);   // the IBP ctor 0x1400f79fb
        gate = freshGate.Get();   // fresh empty IBP -> 0
    }

    WorkCandidateTree resultPair = {};   // local_40 (root) + local_38 (count)
    WorkSchedulerIdNode* srcRoot = nullptr;   // [RBP+0x38] = *param_3 (walked source node)
    ComponentEntity* local_50 = nullptr;      // [RBP-0x28]
    CitizenComponent* local_58 = nullptr;     // [RBP-0x30]
    CitizenComponent* citHolder = nullptr;    // local_res10: 7b60's citizen holder

    // BuildAndWalk(skipKeyHolder): srcRoot = *param_3; build the cost-keyed dst tree via 7c90
    // (slope cost) or 8000 (plain), then walk it via 7b60. citizen for 7b60 = *param_2.
    auto BuildAndWalk = [&](ComponentEntity* skipKeyHolder) -> uint8_t {
        srcRoot = *param_3;
        if (param_5 != '\0') {
            local_58 = *param_2;            // citizen (RDX for 7c90)
            local_50 = skipKeyHolder;       // skip-key gate (R8 for 7c90)
            Helper_sub_7c90(citizen0, &local_58, &local_50, &srcRoot, &resultPair);
        } else {
            local_50 = skipKeyHolder;       // skip-key gate (RDX for 8000)
            Helper_sub_8000(citizen0, &local_50, &srcRoot, &resultPair);
        }
        citHolder = *param_2;               // citizen for 7b60
        return Helper_sub_7b60(param_1, &citHolder, &resultPair);
    };

    uint64_t ret = 0;
    bool tryCommon = true;

    if (gate != nullptr) {
        CitizenComponent* citizen = *param_2;
        if (local_res20 == citizen->profession) {
            // key match: build with the +0x248 gate object as the skip-key holder
            uint8_t hit = BuildAndWalk(gate);
            if (hit != 0) { ret = 1; tryCommon = false; }
        }
        // (key mismatch -> falls through to the common path, LAB_1400f7a89)
    }

    if (tryCommon && ret == 0) {
        // LAB_1400f7a89: build with a FRESH empty IBP as the skip-key holder (no skip filter),
        // but only when the citizen's "remember" flag (local_res20+0x4c) is set OR param_6.
        // B6 row 211: 0x1400f7a89 MOV RAX,[RBP+0x48]; 0x1400f7a8d CMP byte ptr [RAX+0x4c],0 --
        // the key's byte is read with no test (every citizen carries a profession: B6_RULES
        // C5, row 990's fixture re-cut, the root of the old "citizen+0x1f0 == 0" null).
        if (local_res20->transient || (param_6 != '\0')) {
            ::Object::InternalPtr<ComponentEntity> freshIbp(nullptr);   // the IBP ctor 0x1400f7aa6
            uint8_t hit = BuildAndWalk(freshIbp.Get());
            ret = (hit != 0) ? 1 : 0;
        }
    }
    (void)local_58;

    // free the transient result tree
    WorkCandidateNode* r = resultPair.root;
    if (r != nullptr) {
        WorkBase_TreeNodeFree(&resultPair.root, r->left);
        WorkBase_TreeNodeFree(&resultPair.root, r->right);
        System::MemoryAllocator::MemFree(r);
    }
    return ret;
}

// ============================================================
// EXT BODY (NOT RETAIL, docs/extensions/handins/WORKCAND.md): ext.work_cand, class B.
//   Helper_sub_79b0 above for its plain-cost builder (param_5 == 0, 0x1400f8000): the candidates go into a
//   sorted flat array (Ext::WorkCand::CandidateList, src/ext/work_cand/work_cand.h: retail's
//   (Work +0x24, Work +0x20) order and duplicate rule) instead of the transient AVL, and the walk
//   (0x1400f7b60) visits that array: no MemAlloc / MemFree per job. The gate test, the two builds into one
//   set, the visit (Validate, Execute, the reject stamp) and the return are 79b0's and 7b60's.
//   Class B because the arena allocations it skips move later heap addresses, which pointer-keyed sets
//   (EntityList's update tree) order by (WORKCAND.md section 2); the walk itself is retail's.
//   Under the COMPPROF=4 shadow (g_verifyOn) retail's AVL is built too, from the same source tree, and its
//   in-order sequence is compared with the array after each build ("WorkCandidates", once per build). The
//   walk then follows the row: the array with ext.work_cand on, retail's tree (7b60) with it off, so a
//   faithful shadow run allocates and plays as retail.
// ============================================================
struct WorkIdOf {
    int32_t operator()(const Work* w) const { return w->_workId; }   // Work +0x20, read at each insert
};
using WorkCandidateList = ::Ext::WorkCand::CandidateList<Work, WorkIdOf>;

// One list per nesting level of the walk (a job's Execute is game code; a nested walk gets its own list).
// The lists live for the process: no allocation per walk once they have grown.
std::vector<std::unique_ptr<WorkCandidateList>> g_workCandLists;
size_t g_workCandDepth = 0;

struct WorkCandidateLease {
    WorkCandidateList* list;
    WorkCandidateLease() {
        if (g_workCandDepth == g_workCandLists.size()) {
            g_workCandLists.push_back(std::make_unique<WorkCandidateList>());
        }
        list = g_workCandLists[g_workCandDepth].get();
        ++g_workCandDepth;
        list->Clear();
    }
    ~WorkCandidateLease() { --g_workCandDepth; }
    WorkCandidateLease(const WorkCandidateLease&) = delete;
    WorkCandidateLease& operator=(const WorkCandidateLease&) = delete;
};

// 0x1400f8000's source walk and filter into the list: the source tree in order (parent +0x10, left +0x18,
// right +0x20), each job whose Work +0x10 entity is `skipKey` when it is set (every job when it is null), at
// cost Work +0x24.
void BuildFlat(WorkCandidateList& list, ComponentEntity* skipKey, WorkSchedulerIdNode* cur) {
    while (cur != nullptr) {
        Work* payload = cur->work.Get();
        if (skipKey == nullptr || payload->_entity.Get() == skipKey) {
            list.Stage(payload, payload->_queueOrder);
        }
        WorkSchedulerIdNode* rc = cur->right;
        if (rc != nullptr) {
            while (rc->left != nullptr) rc = rc->left;
            cur = rc;
            continue;
        }
        // no right subtree: climb while cur is a right child, then its parent is next (null: the end)
        WorkSchedulerIdNode* up = cur->parent;
        while (up != nullptr && up->right == cur) {
            cur = up;
            up = up->parent;
        }
        cur = up;
    }
    list.Commit();
}

// 0x1400f7b60 over the list: the same visit per job, in the same order.
uint8_t WalkFlat(WorkScheduler* param_1, CitizenComponent* const* param_2, const WorkCandidateList& list) {
    for (size_t i = 0; i < list.Size(); ++i) {
        Work* work = list.At(i).item;
        if (work->_lastRejectStep != static_cast<int32_t>(param_1->stepCount)) {
            work->Validate();   // slot 9
            CitizenComponent* citizen = *param_2;
            const int r = work->Execute(&citizen, &param_1->time);   // slot 4
            if (r == 2) return 1;
            if (r == 1) {
                work->_lastRejectStep = static_cast<int32_t>(param_1->stepCount);
            }
        }
    }
    return 0;
}

// The shadow's compare: retail's tree in order against the list (the job and the stored cost of each entry)
// and its insert count against the list's size. `detail` = the entries matched << 32 | the tree's count.
void VerifyCandidates(const WorkCandidateList& list, const WorkCandidateTree& tree) {
    WorkCandidateNode* cur = tree.root;
    while (cur != nullptr && cur->left != nullptr) cur = cur->left;
    size_t matched = 0;
    bool ok = static_cast<size_t>(tree.count) == list.Size();
    while (ok && cur != nullptr) {
        if (matched >= list.Size() || cur->work.Get() != list.At(matched).item ||
            cur->cost != list.At(matched).cost) {
            ok = false;
            break;
        }
        ++matched;
        WorkCandidateNode* rc = cur->right;
        if (rc != nullptr) {
            while (rc->left != nullptr) rc = rc->left;
            cur = rc;
            continue;
        }
        WorkCandidateNode* up = cur->parent;
        while (up != nullptr && up->right == cur) {
            cur = up;
            up = up->parent;
        }
        cur = up;
    }
    if (ok && matched != list.Size()) ok = false;
    StressHarness_Verify("WorkCandidates", ok,
                         (static_cast<long long>(matched) << 32) | static_cast<uint32_t>(tree.count));
}

uint64_t Helper_sub_79b0_Flat(WorkScheduler* param_1, CitizenComponent* const* param_2,
                              WorkSchedulerIdNode* const* param_3, Profession* param_4, char param_6) {
    CitizenComponent* citizen0 = *param_2;
    ComponentEntity* gate = citizen0->workplace.Get();   // *(citizen+0x248)
    if (param_6 != '\0') {
        gate = nullptr;   // 79b0's fresh empty IBP
    }
    const bool verify = g_verifyOn != 0;
    const bool walkFlat = ::Ext::Ext().work_cand;
    WorkCandidateLease lease;
    WorkCandidateList& list = *lease.list;
    WorkCandidateTree shadow = {};   // retail's tree: built under the shadow only

    // 79b0's BuildAndWalk for the plain cost: both builds go into the one list (and the one shadow tree).
    auto BuildAndWalk = [&](ComponentEntity* skipKey) -> uint8_t {
        BuildFlat(list, skipKey, *param_3);
        if (verify) {
            ComponentEntity* skipHolder = skipKey;
            WorkSchedulerIdNode* srcRoot = *param_3;
            Helper_sub_8000(citizen0, &skipHolder, &srcRoot, &shadow);
            VerifyCandidates(list, shadow);
        }
        CitizenComponent* citHolder = *param_2;
        if (walkFlat) return WalkFlat(param_1, &citHolder, list);
        return Helper_sub_7b60(param_1, &citHolder, &shadow);
    };

    uint64_t ret = 0;
    bool tryCommon = true;
    if (gate != nullptr && param_4 == (*param_2)->profession) {
        // the key matches: the workplace's jobs only
        if (BuildAndWalk(gate) != 0) {
            ret = 1;
            tryCommon = false;
        }
    }
    if (tryCommon && ret == 0 && (param_4->transient || param_6 != '\0')) {
        // LAB_1400f7a89: the whole bucket
        ret = BuildAndWalk(nullptr) != 0 ? 1 : 0;
    }

    // the shadow's tree is freed as 79b0 frees its own
    WorkCandidateNode* r = shadow.root;
    if (r != nullptr) {
        WorkBase_TreeNodeFree(&shadow.root, r->left);
        WorkBase_TreeNodeFree(&shadow.root, r->right);
        System::MemoryAllocator::MemFree(r);
    }
    return ret;
}

// ============================================================
// 0x1400f7550 — roll a randomised list of candidate states, then for each (until success) walk
//   the corresponding remembered-target subtree via 79b0.
//   param_1 = the work scheduler (GameUpdateLoop +0x118)
//   param_2 = &citizen
//   Returns CONCAT71(.., walkSucceeded).
// ============================================================
uint64_t Helper_7550(WorkScheduler* param_1, CitizenComponent* const* param_2) {
    // 0x78-byte candidate buffer, slots at +i*8, low byte = state code, init 0xFF (terminator).
    unsigned char buf[0x78];
    Helper_3f010(buf, 0xffffffff, 0x78);
    char allFlag = 0;        // cVar9 / R15B

    char canHouse = (*param_2)->CanFindHousing();

    uint32_t n;  // candidate count -> Rand range
    if (canHouse != 0) {
        allFlag = 1;
        buf[0] = 0x0e;
        n = 1;
    } else {
        // the town's per-resource counters: CitizenBehavior (+0xF0) professionResources (+0x170)
        CitizenBehavior* listEnt = GameUpdateLoop::_singleton->m_citizenList;
        bool homeless = ((*param_2)->profession
                         == GameUpdateLoop::_singleton->m_professionList->builderProfession);
        if (homeless) buf[0] = 0x0d;
        uint32_t idx = homeless ? 1u : 0u;
        ProfessionResourceEntry* p = listEnt->professionResources._data;

        // Each resource category below threshold -> emit a gather/work state code into the next
        // slot. The packed multi-byte writes mirror the exe (slot low byte = primary state id).
        auto emit1 = [&](unsigned char b0) {
            buf[idx * 8 + 0] = b0; idx++;
        };
        auto emit2 = [&](unsigned char b0, unsigned char b1) {
            buf[idx * 8 + 0] = b0; buf[idx * 8 + 1] = b1; idx++;
        };
        auto emit4 = [&](unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3) {
            buf[idx * 8 + 0] = b0; buf[idx * 8 + 1] = b1; buf[idx * 8 + 2] = b2; buf[idx * 8 + 3] = b3; idx++;
        };

        // (entry k's currentCount is int [k*8], its maxCount int [k*8+2] of the 0x20 stride)
        if (p[5].currentCount  <= p[5].maxCount  / 10) emit1(1);
        if (p[3].currentCount  <= p[3].maxCount  / 10) { emit2(0x02, 0x04); buf[(idx - 1) * 8 + 2] = 10; }
        if (p[4].currentCount  <= p[4].maxCount  / 10) { emit4(0x03, 0x06, 0x04, 0x0a); buf[(idx - 1) * 8 + 4] = 0x0c; }
        if (p[0].currentCount  <= p[0].maxCount  / 10) emit2(0x04, 0x0a);
        if (p[2].currentCount  <= p[2].maxCount  / 10) emit2(0x06, 0x0c);
        if (p[1].currentCount  <= p[1].maxCount  / 10) emit2(0x05, 0x0b);
        if (p[10].currentCount <= p[10].maxCount / 10) emit1(9);
        if (p[7].currentCount  <= p[7].maxCount  / 10) emit2(0x07, 0x09);
        if (p[6].currentCount  <= p[6].maxCount  / 10) emit1(8);
        n = idx;
    }

    // Random start index in [0, n)
    System::Random* rng = GameUpdateLoop::_singleton->m_random;
    uint32_t r = rng->Rand(static_cast<uint32_t>(n));

    // walk from buf[r*8] byte-by-byte until a negative (0xFF) terminator
    unsigned char* pc = buf + static_cast<int>(r) * 8;
    while (static_cast<signed char>(*pc) >= 0) {
        CitizenComponent* citizen = *param_2;  // local_res18[0]
        Profession* ownerKey = citizen->profession;

        // tree root for this state code: param_1 +0x30 + byte*0x10, then follow +0x18 to leftmost
        int64_t stateByte = static_cast<int64_t>(static_cast<signed char>(*pc));
        WorkSchedulerIdNode* node = param_1->trees[stateByte].root;
        WorkSchedulerIdNode* walked = node;  // local_res10
        while (node != nullptr) { walked = node; node = node->left; }

        CitizenComponent* citHolder = citizen;  // &local_res18
        WorkSchedulerIdNode* walkedHolder = walked;
        uint64_t hit = Helper_sub_79b0(param_1, &citHolder, &walkedHolder, ownerKey, '\x01', allFlag);
        if (static_cast<char>(hit) != '\0') {
            return (static_cast<uint64_t>(hit) & 0xffffffffffffff00ULL) | 1;
        }
        pc++;
    }
    return 0;
}

// ============================================================
// 0x1400f7830 — Idle: try the citizen's current profession's bucket in the scheduler's +0x120
//   tree first (cost mode 8000, all=0). If that misses, fall back to 7550; if THAT also misses,
//   try the ProfessionList +0x30 profession's bucket once.
// ============================================================
void Helper_7830(WorkScheduler* param_1, CitizenComponent* const* param_2) {
    if (param_1->trees[0].count == 0) return;  // tree-array not populated
    ++g_idleSearchCalls;

    ProfessionList* homeRoot = GameUpdateLoop::_singleton->m_professionList;
    Profession* homeKey = homeRoot->laborerProfession;

    Profession* ownerKey = (*param_2)->profession;

    // (1) probe the param_1+0x120 tree for ownerKey
    if (ownerKey == homeRoot->builderProfession) ++g_idleSearchByBuilder;
    if (ownerKey != homeRoot->laborerProfession) {
        WorkSchedulerBucketNode* node = param_1->professionBucketRoot;
        while (node != nullptr) {
            if (ownerKey < node->bucket.key) {
                node = node->left;          // +0x20 left
            } else if (ownerKey <= node->bucket.key) {
                // equal: the bucket's id tree (+0x08), descend +0x18 to leftmost
                WorkSchedulerIdNode* v = node->bucket.entries.root;
                WorkSchedulerIdNode* walked = v;
                while (v != nullptr) { walked = v; v = v->left; }
                CitizenComponent* citizen = *param_2;
                CitizenComponent* citHolder = citizen;
                WorkSchedulerIdNode* walkedHolder = walked;
                uint64_t hit = Helper_sub_79b0(param_1, &citHolder, &walkedHolder,
                                               ownerKey, '\0', '\0');
                if (ownerKey == homeRoot->builderProfession) {
                    ++g_idleBuilderBucketHit;
                    if (static_cast<char>(hit) != '\0') ++g_idleBuilderTookWork;
                }
                if (static_cast<char>(hit) != '\0') return;
                break;
            } else {
                node = node->right;         // +0x28 right
            }
        }
    }

    // (2) fall back to the full 7550 candidate roll
    CitizenComponent* citHolder2 = *param_2;
    uint64_t hit2 = Helper_7550(param_1, &citHolder2);
    if (static_cast<char>(hit2) != '\0') return;

    // (3) try the home-key subtree once
    WorkSchedulerBucketNode* node = param_1->professionBucketRoot;
    while (node != nullptr) {
        if (homeKey < node->bucket.key) {
            node = node->left;
        } else if (homeKey <= node->bucket.key) {
            WorkSchedulerIdNode* v = node->bucket.entries.root;
            WorkSchedulerIdNode* walked = v;
            while (v != nullptr) { walked = v; v = v->left; }
            CitizenComponent* citizen = *param_2;
            CitizenComponent* citHolder = citizen;
            WorkSchedulerIdNode* walkedHolder = walked;
            Helper_sub_79b0(param_1, &citHolder, &walkedHolder, homeKey, '\0', '\0');
            return;
        } else {
            node = node->right;
        }
    }
}

} // namespace

// return expression shared by both handlers:
//   CONCAT71(character>>8, 0 < character->currentAction + 1)
// (the caller reads the low byte)
static uint64_t HandlerEpilogue(CitizenComponent* citizen) {
    CharacterComponent* ch = citizen->character.Get();
    int truth = (0 < ch->actionStack.current + 1) ? 1 : 0;
    return static_cast<uint64_t>(truth);
}

// ============================================================
// 0x14010b340 — CitizenAIComponent::State_FindHousing  (state id 2)
// ============================================================
uint64_t CitizenAIComponent::State_FindHousing() {
    WorkScheduler* treeBase = GameUpdateLoop::_singleton->m_tutorial;
    CitizenComponent* holder = citizen.Get();   // local_res8[0]
    Helper_7550(treeBase, &holder);
    return HandlerEpilogue(citizen.Get());
}

// ============================================================
// 0x14010b2d0 — CitizenAIComponent::State_Idle  (state id 9)
// ============================================================
uint64_t CitizenAIComponent::State_Idle() {
    WorkScheduler* treeBase = GameUpdateLoop::_singleton->m_tutorial;
    CitizenComponent* holder = citizen.Get();   // local_res8[0]
    Helper_7830(treeBase, &holder);
    return HandlerEpilogue(citizen.Get());
}

// ============================================================
// 0x14000e360 — AVL rebalance walk up the right-spine of an insertion path.
//   param_2 = &(node ptr); walks node = node->parent (+0x10) updating height/balance and
//   rotating via e450/e580 until null. param_1 is the tree-root slot passed to the rotations.
//   Retail's body serves every 0x30 node with links at +0x10/+0x18/+0x20 and int16 balance /
//   height at +0x28 / +0x2A (ICF, B13): one template, instantiated below for each tree.
// ============================================================
// B6 row 210: retail tests each link for NULL only (0x14000e36a the start node,
// 0x14000e38e / 0x14000e3a5 the two children, 0x14000e3d7 / 0x14000e40b the rotation
// children, 0x14000e434 the parent); the tree's ValidNode range fence (which also rejected
// the -1 IBP sentinel) is gone, and a -1 link is dereferenced as retail does.
template <class Node>
void CitizenAI_Reg_helper_e360(Node** rootSlot, Node** param_2) {
    if (*param_2 == nullptr) return;
    Node* parent;
    do {
        Node* n = *param_2;
        parent = n->parent;  // next up
        Node* left  = n->left;
        Node* right = n->right;
        short hL = left  ? static_cast<short>(left->height + 1) : 0;
        short hR = right ? static_cast<short>(right->height + 1) : 0;
        short h = (hR < hL) ? hL : hR;
        n->height = h;
        n->balance = static_cast<short>(hR - hL);

        if (static_cast<short>(hR - hL) > 1) {
            Node* rc = n->right;
            if (rc) {
                // right-heavy: if right child is left-heavy, rotate it first (e580), then e450.
                if (rc->balance < 1 && rc->balance < 0) {
                    AvlRotateRight(rootSlot, rc);   // 0x14000e580
                    n = *param_2;
                }
                AvlRotateLeft(rootSlot, n);   // 0x14000e450
            }
        }
        n = *param_2;
        if (n->balance < -1) {
            Node* lc = n->left;
            if (lc) {
                // left-heavy: if left child is right-heavy, rotate it first (e450), then e580.
                if (lc->balance > -1 && lc->balance > 0) {
                    AvlRotateLeft(rootSlot, lc);   // 0x14000e450
                    n = *param_2;
                }
                AvlRotateRight(rootSlot, n);   // 0x14000e580
            }
        }
        *param_2 = parent;
    } while (parent != nullptr);
}

// The trees that rebalance through 0x14000e360.
template void CitizenAI_Reg_helper_e360<WorkCandidateNode>(WorkCandidateNode**, WorkCandidateNode**);
template void CitizenAI_Reg_helper_e360<AvlNode30>(AvlNode30**, AvlNode30**);                          // PathCostTreeInsert
template void CitizenAI_Reg_helper_e360<TradeResourceNode>(TradeResourceNode**, TradeResourceNode**);  // the trade stock tree
template void CitizenAI_Reg_helper_e360<LegacyKeyIntNode>(LegacyKeyIntNode**, LegacyKeyIntNode**);     // citizenlist_serialize.cpp
