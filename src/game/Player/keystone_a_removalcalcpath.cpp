// src/game/Player/keystone_a_removalcalcpath.cpp
// Faithful 1:1 ports of RemovalTool::Slot13 (CalcPath, 0x1401035e0) and its
// previously-unregistered exe sub-callee closure.
//
// RemovalTool::Slot13 walks the rectangular tile selection (param_2..param_3
// corner cells) and, per tile, applies the active RemovalDescription's actions:
//   - building/item removal (FUN_1400c48a0 register a fresh ComponentEntity into
//     the world entity list when sub_48a0 says the tile is removable),
//   - road removal (RemovalTool_helper_c700 -> road BST + queue worker),
//   - mark-for-removal of the resident entity (sub_4770 + FUN_1400c2ea0 +
//     FUN_140003110),
//   - state-machine kick (sub_4670 -> StateMachine_impl_dfa0),
//   - citizen-AI notify (sub_43-flag -> FUN_14012ead0 + CitizenAI_impl_9ad0).
// On finishing the rect it rebuilds the road mesh once (TradeWork_impl_f370).
//
// This file ports the 6 functions whose exe sub-callee closure was NOT yet
// registered before this wave:
//   0x14002d960  RemovalTool_helper_d960  (FindComponent-by-type, type 0x1401c7430)
//   0x1400f8d50  RemovalTool_helper_8d50  (in-order AVL successor lookup by key)
//   0x14011e4d0  DestroyUI_helper_e4d0    (StateMachine slot+0x10 dispatch)
//   0x14011dfa0  StateMachine_impl_dfa0   (full StateMachine action-tick driver)
//   0x14009c700  RemovalTool_helper_c700  (road-removal cell handler)
//   0x1400f9ad0  CitizenAI_impl_9ad0      (citizen-AI per-entity AVL update)
//   0x1401035e0  RemovalTool_Slot13       (the CalcPath itself)
//
// Every other callee reached from these bodies is already registered elsewhere
// (ComponentEntity_CopyCtor 0x1400c3720, FUN_1400c48a0, FUN_1400c2ea0,
// FUN_140003110, FUN_140003ea0, FUN_140005780, FUN_14011e710, FUN_14012ead0,
// CitizenAI_impl_8e30 0x1400f8e30, CitizenAI_impl_a6e0 0x14001a6e0,
// CitizenAI_State_Leisure_sub_a8c0 0x14001a8c0, FUN_14009f2f0 0x14009f2f0,
// FUN_1400a8c90, FUN_1400a6fe0, StateMachine_Tick 0x1400a9660,
// RemovalTool_helper_8e30 etc.), is a Runtime export (engine_funcs fn ptrs), or a
// registered dispatch slot (static MemAlloc 0x140152b60, Time::Time 0x140152b00).
//
// Faithful offset-by-offset to the Ghidra disassembly. int*-arith trap handled:
// e.g. the decompile's `param_1 + 0x3a` (int*) == byte +0xe8; `param_1 + 0x20`
// (int*) == byte +0x80; confirmed in the disassembly.
//
// NOT autostart-exercised (player RemovalTool path) — Ghidra-verified only.

#include "Player/RemovalTool.h"
#include <new>
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/terrain_sidecar.h"
#include <cstdint>
#include "Component/CreateDragDescription.h"
#include "Component/MapComponent.h"
#include "Component/DestroyComponent.h"
#include "Component/WorkComponent.h"
#include "Work/BuildRoad.h"
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Player/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Component/exe_bodies.h"
#include "Component/controller_state_table.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "system/MemoryAllocator.h"
#include "system/Value.h"
#include "object/TypeInfo.h"
#include "Component/ComponentDescription.h"
#include "Component/CreateRemovalDescription.h"
#include "Component/StateMachineComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/Map.h"
#include "Game/WorldGeometry.h"      // WorldGeometry::RebuildRoadMesh (0x1400af370)
#include "Game/PathfinderThread.h"   // PathfinderThread::SetCellTerrain (0x1400a6fe0)
#include "Game/PlayerToolManager.h"
#include "Action/PathAvl.h"
#include "Work/Work.h"
#include "Game/WorkScheduler.h"
#include "Game/WorkSchedulerNodes.h"
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::NoteWrite after a cell store
#include "cell_trace.h"   // red square: BANISHED_CELL_TRACE (src/diag/cell_trace.h)

// (The removal-eligibility checks are RemovalTool members: tool_base_path.cpp.)

// ============================================================
// Exe helpers called by name (their declaring headers):
//   0x1400c3720 ComponentEntity_CtorFromDescription, 0x1400c48a0
//   EntityList_RegisterEntity_Impl (Entity/componententity_funcs.h);
//   0x1400c2ea0 ActHunt_MarkEntityRemoval_c2ea0, 0x140003110
//   ActHunt_DynArraySetCount_3110, 0x1400af370 Pf_RoadMeshRebuild_f370,
//   0x1400f8e30 ActCt_TaskCleanup_8e30, 0x14001a8c0 Pf_AvlRebalanceA_a8c0,
//   0x1400a8c90 Pf_FineCellDirty_a8c90, 0x1400a6fe0 Pf_EnqueueCmd2_a6fe0,
//   0x14009f2f0 Pf_SetTileRoadFlag_9f2f0 (Action/exe_bodies.h);
//   0x14001a6e0 WorkBST_BSTRemove_a6e0 (Work/exe_bodies.h);
//   0x14012ead0 WorkComponent_CheckWorkSlot (Component/exe_bodies.h);
//   0x1400a9660 StateMachine_TickReimpl (Component/controller_state_table.h);
//   0x140003ea0 / 0x140005780 PlayerPath_FindComponent_*, 0x14011e710
//   PlayerPath_Sub_11e710 (Player/exe_bodies.h).
// Import slot 0x140152b60 is System::MemoryAllocator::MemAlloc; 0x140152b00 is
// System::Time::Time(float) (RtSystem_Time_Ctor).
// ============================================================

// Type-id storage: 0x1401c7430 is CreateDragDescription's cell, 0x1401c6860
// BuildRoad's (Table 1). The MapComponent / DestroyComponent / WorkComponent
// lookups are FindComponent<T> (ComponentEntity.h) since B4 PLAYER.
static uint32_t s_tid_7430_v, s_tid_6860_v;
static const uint32_t* s_tid_7430 = &s_tid_7430_v;  // 0x1401c7430 -> CreateDragDescription
static const uint32_t* s_tid_6860 = &s_tid_6860_v;  // 0x1401c6860 -> BuildRoad

static bool s_init = false;

static void InitTypeIds() {
    if (s_init) return;
    s_tid_7430_v = CreateDragDescription::TypeId();
    s_tid_6860_v = BuildRoad::TypeId();
    s_init = true;
}

// ============================================================
// 0x14002d960  RemovalTool_helper_d960
// Find-description-by-type over a ComponentDescription's sub-descriptions (count
// +0x30, array +0x28; byte offsets confirmed in disasm). Type id DAT_1401c7430
// (CreateDragDescription). Writes the match (or 0) into *param_2 and returns param_2.
// ============================================================
extern "C" void* RemovalCalc_helper_d960(const ComponentDescription* param_1, void** param_2) {
    InitTypeIds();
    void* result = nullptr;
    int n = param_1->_subDescCount;
    Description** base = param_1->_subDescs;
    for (int i = 0; i < n; i++) {
        Description* comp = base[i];
        if (s_tid_7430 && comp->Type() == *s_tid_7430) {
            Description* c = base[i];
            if (s_tid_7430 && Object::IsDerivedFromNullSafe(c, *s_tid_7430))
                result = c;
            break;
        }
    }
    *param_2 = result;
    return param_2;
}

// ============================================================
// 0x1400f8d50  WorkScheduler::FindWorkAtCell
// In-order walk of the work-id tree trees[0] (WorkSchedulerIdNode: parent +0x10,
// left +0x18, right +0x20) for the Work whose target cell (+0x28) == cell;
// null when none. Faithful to the decompile: starts at the minimum node, then
// steps to the in-order successor.
// ============================================================
Work* WorkScheduler::FindWorkAtCell(int cell) {
    InitTypeIds();
    // Descend to leftmost node (min) starting from root.
    WorkSchedulerIdNode* cur = trees[0].root;
    WorkSchedulerIdNode* lo  = trees[0].root;
    while (cur != nullptr) {
        lo = cur;
        cur = cur->left;
    }
    WorkSchedulerIdNode* node = lo;
    for (;;) {
        if (node == nullptr) {
            // The disasm calls InternalBasePtr_Ctor(ret, 0): a null result.
            return nullptr;
        }
        if (node->work->field_28 == cell) {
            return node->work.Get();
        }
        // in-order successor
        WorkSchedulerIdNode* right = node->right;
        if (right != nullptr) {
            WorkSchedulerIdNode* s = right;
            do {
                lo = s;
                s = lo->left;
            } while (s != nullptr);
            node = lo;
            continue;
        }
        // climb up while node is the right child of its parent
        WorkSchedulerIdNode* parent = node->parent;
        if (parent != nullptr && parent->left != node) {
            WorkSchedulerIdNode* p = parent;
            while (p != nullptr && p->right == node) {
                node = p;
                p = p->parent;
            }
        }
        node = node->parent;
    }
}

// ============================================================
// 0x14011e4d0  DestroyComponent::PostDestroyEvent
// FindComponent<StateMachineComponent> (0x140003ea0) on the entity, then post
// event 1 on the current node: PostEvent(&machine, active state, 1).
// ============================================================
void DestroyComponent::PostDestroyEvent() {
    InitTypeIds();
    StateMachineComponent* machine = _entity->FindComponent<StateMachineComponent>();
    StateMachine& sm = machine->stateMachine;   // no test (0x14011e4d0)
    sm.current->PostEvent(&sm, sm.activeState, SmId(BuildingEvent::Remove));
}

// ============================================================
// StateMachine action-stack tick helper. The driver runs two near-identical
// phases over the state machine; this reproduces one phase pass exactly as the
// decompile (LAB_14011e0..LAB_14011e181 and the LAB_14011e2..LAB_14011e351 copies
// are structurally identical).
// The node slots (ControllerComponent::Controller): [0] FindState, [3] Enter,
// [4] Update, [5] OnTransition, [6] Tick; +0x08 the parent, +0x10 the child.
// ============================================================
static void SM_RunPhase(StateMachine* sm, const System::Time* timeOutSlot) {
    using Controller = ControllerComponent::Controller;
    bool first = true;
    if (sm->pendingState < 0)
        goto skipInner;
    do {
        do {
            if (-1 < sm->currentInput) {
                int it = sm->activeState;
                Controller* p = sm->current;
                // FIX(lead 2026-07-20, bug-class #6): the node ABI carries the
                // state index in R8D (Ghidra drops it) — slot +0x20 is
                // Update(node, sm, idx). Without `it` the ControllerLive
                // dispatcher read garbage and bounds-rejected every dispatch.
                while (((it < 0) || (p->Update(sm, it) == 0)) &&
                       ((p = p->_data08) != nullptr)) {
                    it = p->FindState(sm->currentInput);
                }
                if (-1 < sm->transitionState) {
                    // FIX(lead-verify vs disasm 0x14011e0d7): this = the current
                    // node, arg2 = the state machine.
                    Controller* node = sm->current;
                    node->OnTransition(sm, sm->activeState, sm->transitionState);
                }
            }
            int8_t cur52 = sm->pendingState;
            // FIX(lead-verify vs disasm 0x14011e0f0 MOV RCX,[RBX]): head = the
            // current node.
            Controller* head = sm->current;
            sm->currentInput = cur52;
            // descend to the deepest child via the +0x10 chain
            Controller* deepest = head;
            for (Controller* c = head->_data10; c != nullptr; c = c->_data10) {
                deepest = c;
            }
            sm->current = deepest;
            int8_t st = static_cast<int8_t>(deepest->FindState(cur52));
            sm->activeState = st;
            while (st < 0) {
                Controller* nxt = sm->current->_data08;
                sm->current = nxt;
                st = static_cast<int8_t>(nxt->FindState(sm->currentInput));
                sm->activeState = st;
            }
            int it = sm->activeState;
            Controller* p = sm->current;
            sm->pendingState = StateByte(BuildingState::None);   // one 0xffff word store in retail (H7)
            sm->transitionState = -1;
            // FIX(lead 2026-07-20, bug-class #6): slot +0x18 is
            // Enter(node, sm, idx) — idx rides R8D, dropped by the decompile.
            while (((it < 0) || (p->Enter(sm, it) == 0)) &&
                   ((p = p->_data08) != nullptr)) {
                it = p->FindState(sm->currentInput);
            }
        } while (-1 < sm->pendingState);
    skipInner:
        if (first && (-1 < sm->currentInput)) {
            int it = sm->activeState;
            Controller* p = sm->current;
            // FIX(lead-verify vs disasm 0x14011e199 / 0x14011e369): BOTH phases' +0x30
            // call pass &outState (LEA R8,[RBP+0x20] in each).
            // FIX(lead 2026-07-20, bug-class #6): slot +0x30 is
            // Tick(node, sm, time/out, idx) — idx rides R9D, also dropped.
            while (((it < 0) || (p->Tick(sm, timeOutSlot, it) == 0)) &&
                   ((p = p->_data08) != nullptr)) {
                it = p->FindState(sm->currentInput);
            }
            first = false;
        }
    } while (-1 < sm->pendingState);
}

// ============================================================
// 0x14011dfa0  DestroyComponent::RunStateMachine
// State machine action driver. Reads the StateMachineComponent
// (FindComponent<StateMachineComponent> 0x140003ea0 on the entity),
// short-circuits when its top state byte (pending, else input) is 6 (first two
// checks) or 7. The FIRST ==6 returns with no cleanup (JZ 0x14011e3b9); the
// SECOND ==6 and the ==7 exits jump to 0x14011e3b1 which calls
// PostDestroyEvent (0x14011e4d0) first. Otherwise runs the two-phase action tick.
// Faithful to the decompile control flow.
// ============================================================
void DestroyComponent::RunStateMachine() {
    InitTypeIds();
    // Retail reuses ONE 8-byte stack slot as both the Time(0.0) buffer and the 4-byte
    // outState the transition lookup writes. The two lifetimes never overlap: phase 1
    // reads the Time, then the int is seeded and written by FindTransition and read back
    // as the pending state, and the slot is re-built as Time(0.0) before phase 2 reads
    // it again. Nothing reads the int as the Time or the Time as the int, so the port
    // keeps two typed locals (an int32 alone under System::Time::Time's 8-byte store
    // clobbered its neighbour, observed live 2026-07-26).
    System::Time timeOutSlot = {0};
    int32_t outState;   // seeded below, before FindTransition writes it

    // No test of the find's result (0x14011dfa0 reads +0x52 / +0x50 at once).
    StateMachineComponent* st0 = _entity->FindComponent<StateMachineComponent>();
    int8_t cVar5 = st0->stateMachine.pendingState;
    if (cVar5 < 0) cVar5 = st0->stateMachine.currentInput;
    if (cVar5 == StateByte(BuildingState::Collapse)) return;

    StateMachineComponent* st1 = _entity->FindComponent<StateMachineComponent>();
    cVar5 = st1->stateMachine.pendingState;
    if (cVar5 < 0) cVar5 = st1->stateMachine.currentInput;
    // FIX(lead-verify vs disasm 0x14011e01a JZ 0x14011e3b1): the 2nd ==6 exit calls e4d0.
    if (cVar5 == StateByte(BuildingState::Collapse)) { PostDestroyEvent(); return; }

    cVar5 = st1->stateMachine.pendingState;
    if (cVar5 < 0) cVar5 = st1->stateMachine.currentInput;
    // FIX(lead-verify vs disasm 0x14011e03c JZ 0x14011e3b1): the ==7 exit calls e4d0.
    if (cVar5 == StateByte(BuildingState::Rubble)) { PostDestroyEvent(); return; }

    // ---- main tick path ----
    StateMachineComponent* machine = _entity->FindComponent<StateMachineComponent>();
    // Time(0.0) into the Time slot (see the timeOutSlot note above).
    RtSystem_Time_Ctor(&timeOutSlot, 0.0f);
    StateMachine* sm = &machine->stateMachine;

    // Phase 1 (transition slot +0x30 passes the shared slot, same as phase 2)
    SM_RunPhase(sm, &timeOutSlot);

    // ---- between-phase: choose transition + kick Tick ----
    outState = SmId(BuildingState::Demolish);  // outState seed
    if (sm->activeState < 0) {
        System::Time timeBuf = {0};
        RtSystem_Time_Ctor(&timeBuf, 0.0f);
        // The door passed the Time buffer's ADDRESS where the body takes the time
        // VALUE (int64_t); the bits the door passed are kept.
        sm->Tick(&timeBuf, 0, 0);   // 0x1400a9660
    }
    {
        ControllerComponent::Controller* head = sm->current;
        int iVar8 = head->FindTransition(sm->activeState, &outState);
        if (-1 < iVar8) {
            sm->transitionState = static_cast<int8_t>(iVar8);
            sm->pendingState = static_cast<int8_t>(outState);
        }
    }
    RtSystem_Time_Ctor(&timeOutSlot, 0.0f);

    // Phase 2 (transition slot +0x30 passes the shared slot)
    SM_RunPhase(sm, &timeOutSlot);
}


// ============================================================
// 0x14009c700  RemovalTool_helper_c700
// Road-removal cell handler. param_1 = the Map. Walks the road BST (+0xE8) by cell
// id param_2; on a found road node, resolves the BuildRoad work (RemovalTool_helper
// _8d50 + IsA BuildRoad), reads its remove-kind fields (+0x60,+0x64,+0x68), notifies
// the work scheduler (0x1400f8e30) and erases the BST node (0x14001a6e0). Then
// updates the map cell (+0x80, stride 0x30): if cell+0x2a==0xff it dirties the
// region (0x1400a8c90) and queues a worker job (0x1400a6fe0); else applies the
// removal (0x14009f2f0) and rebuilds the road mesh (0x1400af370). Returns bool.
// ============================================================
uint64_t Map::RemovePendingRoad(int param_2) {
    InitTypeIds();
    const AvlNode28* root = field_0xE8.root;
    uint32_t retLow = 2;
    int removeKind = 0xff;   // iVar9 / R12D
    uint8_t ebx = 0;         // the +0x64 byte (road branch) / the IBP's low byte
    uint32_t res10 = 2;      // local_res10
    uint8_t r7 = 0;          // uVar7 byte
    bool found = false;      // bVar5
    uint8_t r13 = 1;         // bVar10 (subtract amount); EBP/R13 start = 1
    Work* res18 = nullptr;   // local_res18 (InternalPtr<Work>)

    const AvlNode28* node = root;
    while (node != nullptr) {
        if (param_2 < node->key) {
            node = node->left;
        } else if (node->key < param_2) {
            node = node->right;
        } else {
            // exact match — node found (key == param_2)
            // WorkScheduler::FindWorkAtCell 0x1400f8d50 (the work scheduler, &res18, param_2)
            res18 = GameUpdateLoop::_singleton->m_tutorial->FindWorkAtCell(param_2);
            r13 = 1;  // bVar10 = true by default
            BuildRoad* road = nullptr;
            if (res18 != nullptr) {
                // IsA / IsDerivedFrom prove the scheduler's work a BuildRoad.
                Work* work = res18;
                bool isRoad = s_tid_6860 ? work->IsA(*s_tid_6860) : false;
                if (isRoad) {
                    road = (s_tid_6860 && Object::IsDerivedFromNullSafe(work, *s_tid_6860))
                           ? static_cast<BuildRoad*>(work) : nullptr;
                    removeKind = road->field_60;
                    ebx = (uint8_t)road->field_64;   // ptr = *(uint*)(bp+100)
                    res10 = road->field_68;
                    GameUpdateLoop::_singleton->m_tutorial->TaskCleanup(work->field_20);   // 0x1400f8e30
                    r13 = (removeKind == 0xff) ? 1 : 0;  // bVar10 = (removeKind==0xff)
                } else {
                    ebx = 0;   // the low byte of the still-null IBP
                    r13 = 1;
                }
            } else {
                // LAB_14009c85e: res18 == null -> EBX = the null IBP's low byte
                ebx = 0;
            }
            r7 = ebx;  // uVar7 = the +0x64 byte for the road branch

            // second BST pass: erase the matched node (0x14001a6e0)
            AvlNode28* n2 = field_0xE8.root;
            while (n2 != nullptr) {
                if (param_2 < n2->key) {
                    n2 = n2->left;
                } else if (n2->key < param_2) {
                    n2 = n2->right;
                } else {
                    WorkBST_BSTRemove_a6e0(&field_0xE8, n2);   // 0x14001a6e0
                    break;
                }
            }
            retLow = res10;
            found = true;
            break;
        }
    }

    // ---- LAB_14009c8a0: apply to map cell ----
    MapCell* cell = &cells.GetData()[param_2];
    uint64_t result;
    if (cell->field_0x2A == MapCell::kNoRoadBatch) {
        uint8_t bVar1 = static_cast<uint8_t>(cell->terrainType);
        uint8_t setBit = ((1u << (bVar1 & 0x1f)) & Bits(PathBits::Zoneless)) != 0 ? 1 : 0;
        field_0xA2 = field_0xA2 | (setBit != 0);
        cell->terrainType = PathType::Normal;
        TerrainSidecar::NoteWrite(cell);   // [terrain sidecar] mirror
        CellStamp::NoteWrite(cell);   // [AMBMEMO] cell write clock
        if (((1u << (bVar1 & 0x1f)) & Bits(PathBits::Zoneless)) != 0) {
            int div = size;
            regionGraph->MarkCellDirty(param_2 % div, param_2 / div);   // 0x1400a8c90
        }
        // 0x1400a6fe0 reads the cell and the type (EDX, R8D) only; retail's R9 is not read.
        pathfinder->SetCellTerrain(static_cast<int>(param_2), ToUnderlying(PathType::Normal));   // 0x1400a6fe0
        result = 0;  // uVar8 & 0xff..00
    } else {
        if (found) {
            CellTrace::NoteCount(this, cell, cell->footprintCount, -r13, "RemovePendingRoad");
            cell->footprintCount = static_cast<uint8_t>(cell->footprintCount - r13);
            cell->field_0x2A = static_cast<uint8_t>(removeKind);
            cell->field_0x2B = r7;
            SetTerrainType(param_2, retLow);   // 0x14009f2f0
            if (removeKind == 0xff) {
                result = 0;
            } else {
                GameUpdateLoop::_singleton->m_worldGeometry->RebuildRoadMesh(this);   // 0x1400af370
                result = 1;
            }
        } else {
            result = 1;
        }
    }
    return result;
}

// ============================================================
// 0x1400f9ad0  WorkScheduler::PrioritizeWork
// Finds the work's node in the work-id tree trees[0] (key == workId; left +0x18,
// right +0x20). Then re-keys the count tree at +0x10 (WorkSchedulerCountNode:
// left +0x10, right +0x18) by the work's +0x24 key: erases the node keyed by the
// old value, stores (tree minimum - 1) into the work's +0x24 and inserts
// {that key, the work id +0x20}; bumps the count at +0x18.
// Faithful to the decompile.
// ============================================================
void WorkScheduler::PrioritizeWork(int workId) {
    InitTypeIds();
    WorkSchedulerIdNode* piVar4 = trees[0].root;
    if (piVar4 == nullptr) return;

    for (;;) {
        if (workId < piVar4->key) {
            piVar4 = piVar4->left;
        } else if (piVar4->key < workId) {
            piVar4 = piVar4->right;
        } else {
            // exact match
            if (piVar4 == nullptr) return;
            Object::InternalPtr<Work>& self = piVar4->work;
            WorkSchedulerCountNode* puVar7 = nullptr;
            int iVar6 = 0;
            if (field_0x10.count != 0) {
                WorkSchedulerCountNode* p3 = field_0x10.root;
                WorkSchedulerCountNode* p4 = field_0x10.root;
                while (p3 != nullptr) {
                    WorkSchedulerCountNode* p2 = p3;
                    p4 = p2;
                    p3 = p2->left;
                }
                iVar6 = p4->key + -1;
            }
            // erase any node already keyed by the work's +0x24
            WorkSchedulerCountNode* pk = field_0x10.root;
            while (pk != nullptr) {
                int entId = self->field_24;
                if (entId < pk->key) {
                    pk = pk->left;
                } else if (pk->key < entId) {
                    pk = pk->right;
                } else {
                    WorkBST_BSTRemove_a6e0(&field_0x10, pk);   // 0x14001a6e0
                    break;
                }
            }
            self->field_24 = iVar6;
            uint32_t uVar1 = static_cast<uint32_t>(self->field_20);

            WorkSchedulerCountNode* cur = field_0x10.root;
            if (cur == nullptr) {
                WorkSchedulerCountNode* nodeMem = static_cast<WorkSchedulerCountNode*>(System::MemoryAllocator::MemAlloc(0x28, 8));
                if (nodeMem != nullptr) {
                    nodeMem->parent = nullptr; nodeMem->left = nullptr; nodeMem->right = nullptr;
                    nodeMem->balance = 0;   // balance + height: one dword store in the tree
                    nodeMem->height = 0;
                    nodeMem->key = iVar6;   // key + workId: one qword store in the tree
                    nodeMem->workId = uVar1;
                    puVar7 = nodeMem;
                }
                field_0x10.root = puVar7;
            } else {
                WorkSchedulerCountNode* localRes18 = cur;
                WorkSchedulerCountNode* prev;
                for (;;) {
                    do {
                        localRes18 = cur;
                        prev = localRes18;
                        if (iVar6 < localRes18->key) {
                            cur = localRes18->left;
                            if (cur == nullptr) {
                                WorkSchedulerCountNode* nodeMem = static_cast<WorkSchedulerCountNode*>(System::MemoryAllocator::MemAlloc(0x28, 8));
                                if (nodeMem != nullptr) {
                                    nodeMem->left = nullptr; nodeMem->right = nullptr;
                                    nodeMem->balance = 0;
                                    nodeMem->height = 0;
                                    nodeMem->parent = prev;
                                    nodeMem->key = iVar6;
                                    nodeMem->workId = uVar1;
                                    puVar7 = nodeMem;
                                }
                                prev->left = puVar7;
                                goto balance;
                            }
                        } else {
                            break;
                        }
                    } while (true);
                    if (iVar6 <= localRes18->key) {
                        return;  // already present
                    }
                    cur = localRes18->right;
                    if (cur == nullptr) {
                        WorkSchedulerCountNode* nodeMem = static_cast<WorkSchedulerCountNode*>(System::MemoryAllocator::MemAlloc(0x28, 8));
                        if (nodeMem != nullptr) {
                            nodeMem->left = nullptr; nodeMem->right = nullptr;
                            nodeMem->balance = 0;
                            nodeMem->height = 0;
                            nodeMem->parent = prev;
                            nodeMem->key = iVar6;
                            nodeMem->workId = uVar1;
                            puVar7 = nodeMem;
                        }
                        prev->right = puVar7;
                        goto balance;
                    }
                }
            balance:
                {
                    WorkSchedulerCountNode* res18local = prev;
                    AvlRebalance(&field_0x10.root, &res18local);   // 0x14001a8c0
                }
            }
            field_0x10.count = field_0x10.count + 1;
            return;
        }
        if (piVar4 == nullptr) return;
    }
}


// ============================================================
// 0x1401035e0  RemovalTool::CalcPath (slot 13)
// Walks the selection rectangle (param_2 corner, param_3 corner) over the tile
// grid and, per cell, applies the active CreateRemovalDescription. See file header.
// Faithful to the decompile; B4 PLAYER reads every field by name.
// ============================================================

// A placed removal marker: a fresh entity of the description's +0x38 template whose
// MapComponent covers the one cell, registered in the world entity list
// (0x1400c3720 + FindComponent<MapComponent> 0x140004680 + 0x1400c48a0). Retail
// inlines it twice in CalcPath.
static void PlaceRemovalMarker(const CreateRemovalDescription* desc, int col, int row) {
    ComponentEntity* mem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    ComponentEntity* cloned = nullptr;
    if (mem) cloned = ::new (mem) ComponentEntity(desc->_ref38);
    MapComponent* comp = cloned->FindComponent<MapComponent>();
    comp->tileX0 = (int16_t)col;
    comp->tileY0 = (int16_t)row;
    // Retail stores ONE qword 0x10001 at +0x2c (0x140103847 / 0x1401039e1): width 1,
    // height 1 AND +0x30 (the rotation) = 0. Three named stores leave the same bytes.
    comp->tileX1 = 1;
    comp->tileY1 = 1;
    comp->extraField = 0;
    Object::InternalPtr<ComponentEntity> local_70 = cloned;
    GameUpdateLoop::_singleton->m_entityList->RegisterEntity(&local_70,
                                   static_cast<int>(desc->_ref38->_category));
}

bool RemovalTool::CalcPath(int param_2, int param_3) {
    // Decompile variable names preserved verbatim for offset-by-offset fidelity.
    Map* piVarDim = GameUpdateLoop::_singleton->m_game;
    int dim = piVarDim->size;

    int64_t uVar13 = (int64_t)param_2 / dim;   // row of corner A
    int64_t uVar26 = (int64_t)param_2 % dim;   // col of corner A
    uint32_t uVar9 = (uint32_t)uVar26;
    int64_t uVar25 = uVar13 & 0xffffffff;
    int64_t uVar16 = (int64_t)param_3 / dim;   // row of corner B
    int64_t uVar3  = (int64_t)param_3 % dim;   // col of corner B
    uint32_t local_res18 = (uint32_t)uVar3;
    int64_t uVar4 = uVar26;
    uint32_t local_res8 = uVar9;
    if ((int)local_res18 < (int)uVar9) {
        uVar4 = uVar3;
        uVar3 = uVar26;
        local_res8 = local_res18;
        local_res18 = uVar9;
    }
    int64_t uVar22 = uVar3 & 0xffffffff;       // inner-loop col end
    int64_t uVar20 = uVar4 & 0xffffffff;       // inner-loop col start
    uVar9 = (uint32_t)uVar13;
    uVar13 = uVar16 & 0xffffffff;              // outer-loop row end
    uVar26 = uVar25;                           // outer-loop row start
    uint32_t local_res20 = (uint32_t)uVar16;
    if ((int)(uint32_t)uVar16 < (int)uVar9) {
        uVar26 = uVar16 & 0xffffffff;
        uVar13 = uVar25;
        local_res20 = uVar9;
    }

    // the owner's single-rectangle constraint (+0x80 flag)
    const PlayerToolManager* lVar21 = dialogPtr;
    if (lVar21->field_0x80) {
        int iVar24 = dim;
        int64_t uVar16b = (int64_t)lVar21->field_0x9C / iVar24;
        if (((int)uVar4 != lVar21->field_0x98 % iVar24) ||
            ((int)uVar26 != lVar21->field_0x98 / iVar24) ||
            (lVar21->field_0x9C % iVar24 != (int)uVar3) ||
            ((int)uVar16b != (int)uVar13)) {
            return false;   // AL = 0 (retail: uVar16b & ~0xff in RAX; callers test AL)
        }
    }

    // RemovalTool_helper_d960 on the description's +0x38 template, if any
    if (removalDesc->_ref38 != nullptr) {
        void* tmp58 = nullptr;
        RemovalCalc_helper_d960(removalDesc->_ref38, &tmp58);
        uVar22 = (int64_t)local_res18;
    }

    Map* puVar18 = GameUpdateLoop::_singleton->m_game;
    Map* local_a0 = puVar18;
    bool bVar5 = false;          // roadMeshDirty

    if ((int)uVar26 <= (int)uVar13) {
        do {
            int iVar24 = (int)uVar26;  // current row
            if ((int)uVar20 <= (int)uVar22) {
                do {
                    int iVar23 = (int)uVar20;  // current col
                    int iRow = iVar24;
                    int iCol = iVar23;
                    int cellId = (int)(puVar18->size * iVar24 + iVar23);
                    const MapCell* cellSlot = &puVar18->cells.GetData()[cellId];

                    // resolve the entity occupying this cell (blocker, else occupant)
                    ComponentEntity* local_e8;
                    if (cellSlot->blocker == nullptr) {
                        if (cellSlot->occupant == nullptr) {
                            local_e8 = nullptr;   // the null InternalBasePtr
                        } else {
                            local_e8 = cellSlot->occupant.Get();
                        }
                    } else {
                        local_e8 = cellSlot->blocker.Get();
                    }
                    ComponentEntity* local_e0 = local_e8;

                    // ---- field removal (0x1401048a0) ----
                    if ((char)CanRemoveField(&local_e0, iCol, iRow) != 0) {
                        PlaceRemovalMarker(removalDesc, iVar23, iVar24);
                        puVar18 = local_a0;
                    }

                    // ---- road removal (flag desc+0x40) ----
                    Map* piVar15 = GameUpdateLoop::_singleton->m_game;
                    if (removalDesc->_b40) {
                        int cid = piVar15->size * iRow + iCol;
                        const MapCell* cl = &piVar15->cells.GetData()[cid];
                        uint32_t mask = Bits(PathBitOf(cl->terrainType));
                        bool hit = false;
                        if (((mask & Bits(PathBits::Fast)) == 0) && ((mask & Bits(PathBits::Faster)) == 0) &&
                            (cl->field_0x2A == MapCell::kNoRoadBatch)) {
                            const AvlNode28* bn = piVar15->field_0xE8.root;
                            while (bn != nullptr) {
                                if (cid < bn->key) {
                                    bn = bn->left;
                                } else if (bn->key < cid) {
                                    bn = bn->right;
                                } else { hit = true; break; }
                            }
                        } else {
                            hit = true;
                        }
                        if (hit) {
                            uint64_t rc = piVar15->RemovePendingRoad(piVar15->size * iRow + iCol);
                            if ((char)rc == 0) {
                                bVar5 = true;
                            } else {
                                PlaceRemovalMarker(removalDesc, iVar23, iVar24);
                            }
                        }
                    }

                    // ---- mark-for-removal of the resident entity (0x140104770) ----
                    local_e0 = local_e8;
                    if ((char)CanRemoveRoad(&local_e0) != 0) {
                        if (local_e8->_description == removalDesc->_ref38) {
                            local_e0 = local_e8;
                            EntityList* lst = GameUpdateLoop::_singleton->m_entityList;
                            if ((local_e0->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) == 0) {
                                local_e0->MarkForRemoval();
                                lst->removalList.SetSize(lst->removalList._size + 1, local_e0);   // 0x140003110
                            }
                        } else {
                            DestroyComponent* comp = local_e8->FindComponent<DestroyComponent>();
                            if (comp != nullptr) {
                                DestroyComponent* pi = local_e8->FindComponent<DestroyComponent>();
                                if (pi->InRemovalState()) {   // 0x14011e710 (the door read AL)
                                    DestroyComponent* lr = local_e8->FindComponent<DestroyComponent>();
                                    StateMachineComponent* machine =
                                        lr->_entity->FindComponent<StateMachineComponent>();
                                    // the current node's slot 2 (+0x10): PostEvent(&machine, active, 0)
                                    ControllerComponent::Controller* head = machine->stateMachine.current;
                                    head->PostEvent(&machine->stateMachine,
                                                    machine->stateMachine.activeState,
                                                    SmId(BuildingEvent::Cancel));
                                }
                            }
                        }
                    }

                    // ---- StateMachine kick (0x140104670) ----
                    ComponentEntity* local_b8 = local_e8;
                    if ((char)CanRemoveBuilding(&local_b8) != 0) {
                        DestroyComponent* comp = local_e8->FindComponent<DestroyComponent>();
                        comp->RunStateMachine();   // 0x14011dfa0
                    }

                    // ---- citizen-AI notify (flag desc+0x43) ----
                    local_e0 = local_e8;
                    if (removalDesc->_b43 && (local_e8 != nullptr)) {
                        WorkComponent* comp = local_e0->FindComponent<WorkComponent>();
                        if (comp != nullptr) {
                            if (comp->IsWorkPosted()) {   // 0x14012ead0
                                WorkComponent* comp2 = local_e8->FindComponent<WorkComponent>();
                                if (comp2 != nullptr) {
                                    // The scheduler's work-id tree trees[0] (key, work +0x08,
                                    // left +0x18, right +0x20).
                                    WorkScheduler* sched = GameUpdateLoop::_singleton->m_tutorial;
                                    WorkSchedulerIdNode* bn = sched->trees[0].root;
                                    Work* local_c8 = nullptr;
                                    int key = comp2->workId;
                                    while (bn != nullptr) {
                                        if (key < bn->key) {
                                            bn = bn->left;
                                        } else if (bn->key < key) {
                                            bn = bn->right;
                                        } else {
                                            local_c8 = bn->work.Get();
                                            break;
                                        }
                                    }
                                    if (local_c8 != nullptr) {
                                        sched->PrioritizeWork(comp2->workId);   // 0x1400f9ad0
                                    }
                                }
                            }
                        }
                    }

                    uVar20 = (int64_t)(iVar23 + 1);
                    uVar22 = (int64_t)local_res18;
                    puVar18 = local_a0;
                } while ((int)uVar20 <= (int)local_res18);  // uVar20 == iVar23+1 here
                uVar20 = (int64_t)local_res8;
                uVar13 = (int64_t)local_res20;
            }
            uVar26 = (int64_t)(iVar24 + 1);
        } while ((int)uVar26 <= (int)uVar13);  // uVar26 == iVar24+1 here

        // 0x1400af370 is void; the door read a uint64_t nobody uses.
        if (bVar5)
            GameUpdateLoop::_singleton->m_worldGeometry->RebuildRoadMesh(puVar18);
    }

    // FIX(lead-verify): faithful CONCAT71 — roadMesh return in the upper bytes, low byte 1.
    // ENDGAME B.3.8: slot 13 is `bool` (the callers test AL), so only the low byte is
    // returned.
    return true;
}
