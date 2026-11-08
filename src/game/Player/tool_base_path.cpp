// src/game/Player/tool_base_path.cpp
// Player registration wave (batch "path") — shared exe sub-helpers referenced
// from src/game/Player/removaltool.cpp (RemovalTool::Draw / per-tile checks).
//
// These are exe sub-helpers resolved via ResolveAddr(Module::Exe, 0xADDR) from
// the RemovalTool reimpl. They resolve to 0 (silently skipped) unless registered.
// This file provides faithful 1:1 reimplementations and a single registration point
// (RegisterPlayerPathBatch in standalone/reg_player_path.cpp) so the call sites
// fire real logic.
//
// Each function is a 1:1 port of the decompiled exe helper; do not "improve"
// behavior.
//
// Closure note: every exe sub-callee reached by the bodies here is either
// (a) registered elsewhere (FUN_1400052c0 area-self find, FUN_14009f3e0
//     CharComp height query, dispatch slot fn_InternalBasePtr_*),
// (b) registered in this batch bottom-up (the FindComponent leaves 5780/3ea0,
//     FUN_14011e710, CharComp f5d0),
// (c) a Runtime export resolved via engine_funcs.h.
//
// DEFERRED (NOT here): PathTool helper 0x1400dd710 (closure pulls in the
// 520-line AVL builder d770 + ~12 unregistered exe sub-callees), RemovalTool
// CalcPath 0x1401035e0 (~770 lines + ~15 unregistered sub-fns), and
// QuarryComponent_impl_a530 0x1400aa530 RaycastTerrain (closure IS satisfied but
// its float-march pointer-aliased body is too risky to port faithfully in this
// wave; a wrong port is silent corruption). See reg_player_path.cpp.

#include <new>
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/DestroyComponent.h"
#include "Component/BuildingState.h"
#include "Component/StateMachineComponent.h"
#include "Component/CreateDragDescription.h"
#include "Player/exe_bodies.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Component/NaturalResourceComponent.h"
#include "Component/CreateRemovalDescription.h"
#include "Component/ComponentDescription.h"
#include "Player/RemovalTool.h"
#include "Game/Map.h"

// ============================================================
// FindComponent instances: ComponentEntity::FindComponent<T>() (Entity/ComponentEntity.h).
// These names stay as B4 FORWARD forwarders until their callers hold a typed entity.
// ============================================================
// (0x140005780 PlayerPath_FindComponent_5780, the FindComponent<DestroyComponent>
// forwarder, had no caller left: deleted, B4 wave X.)

// (0x140003ea0 PlayerPath_FindComponent_3ea0, FindComponent<StateMachineComponent>
// behind an InternalPtr, had no caller left: deleted, B4 wave X round 3.)

// The state a StateMachine reports: the pending state, else the input (+0x52,
// fallback +0x50 when negative) — the read 0x14011e710 / 0x140104670 inline.
static int8_t RequestedState(const StateMachineComponent* machine) {
    int8_t s = machine->stateMachine.pendingState;
    if (s < 0) s = machine->stateMachine.currentInput;
    return s;
}

// ============================================================
// FUN_14011e710 — DestroyComponent::InRemovalState ("is removable child" check).
// CheckPtr(this+0x10), FindComponent<StateMachineComponent> (0x140003ea0) on the
// owning entity, then reads the state byte (+0x52, fallback +0x50 when negative)
// and returns 1 iff it is 3, 4, or 5 (Demolish, HaulAway, Deconstruct). No null check on
// the found component: retail has none. B4 PLAYER: a DestroyComponent member (it reads the component's
// owner, +0x10).
// ============================================================
bool DestroyComponent::InRemovalState() {
    StateMachineComponent* machine = _entity->FindComponent<StateMachineComponent>();

    if (RequestedState(machine) != StateByte(BuildingState::Demolish)) {
        if (RequestedState(machine) != StateByte(BuildingState::HaulAway)) {
            if (RequestedState(machine) != StateByte(BuildingState::Deconstruct)) return false;
        }
    }
    return true;
}


// ============================================================
// CharacterComponent_impl_f5d0 (0x14009f5d0) — single-cell terrain/height test.
// Validates cell (param_2,param_3) against pass-mask param_4, ownership param_10
// (the blocker's description), override flags param_8/param_9, then runs the
// 0x14009f3e0 terrain query and compares the returned height to
// param_5/param_6/param_7.
//
// Faithful 1:1 port. DAT_140172114 = 0.5f. Math::Scalar::Abs(x) == fabsf(x).
// f3e0 writes its result Vector3 (x,y,z) into outPos; 'local_28' (compared to
// param_5) is the 5th-argument vector's z term.
// The map's header (Map.h) is block GAME-b's: a free function over the typed map.
// ============================================================
bool Map::TestPlacement(int param_2, int param_3, uint32_t param_4,
                              float param_5, float param_6, float param_7,
                              char param_8, char param_9, const ComponentDescription* param_10) {
    const float kHalf = 0.5f;  // DAT_140172114

    if (!((-1 < param_2) && (param_2 < size) && (-1 < param_3) && (param_3 < size)))
        return false;

    int iVar2 = size * param_3 + param_2;
    const MapCell* cell = &cells.GetData()[iVar2];

    uint8_t passByte = static_cast<uint8_t>(cell->terrainType);
    if ((~param_4 & (1u << (passByte & 0x1f))) != 0)
        return false;

    const ComponentEntity* owner = cell->blocker.Get();
    bool ownOk;
    if (param_10 != nullptr) {
        if (owner == nullptr) {
            ownOk = true;
        } else {
            owner = cell->blocker.Get();
            ownOk = (owner->_description == param_10);
        }
    } else {
        ownOk = false;
    }
    if (!(ownOk || (param_8 != 0) || (cell->blocker == nullptr)))
        return false;

    // the footprint count byte at cell+0x28
    char cellFlag = static_cast<char>(cell->footprintCount);
    if (!((param_9 != 0) || (cellFlag == 0)))
        return false;

    // Contiguous Vector3 locals. f3e0 writes a Vector3 to BOTH the main output
    // (2nd arg, = decompile local_20) AND the 5th-arg buffer (= local_30); the
    // 5th buffer must be a full Vector3 (3 floats). Verified from disasm
    // 0x14009f6ea-0x14009f725.
    float outPos[3]   = {0.0f, 0.0f, 0.0f};   // main output vec (local_20)
    float queryPos[2];                         // input pos (local_40/local_3c)
    float extraOut[3] = {0.0f, 0.0f, 0.0f};    // 5th-arg output vec (local_30)

    float base = (float)(size >> 1) - kHalf;
    queryPos[0] = (float)(iVar2 % size) - base;
    queryPos[1] = (float)(iVar2 / size) - base;

    GetTerrainPos(outPos, queryPos, '\0', extraOut);

    // param_5 ([RSP+0xa0]) <= extraOut.z ([RSP+0x50]); then outPos.z ([RSP+0x60])
    // - param_7, Abs, <= param_6 ([RSP+0xa8]).
    if (param_5 <= extraOut[2]) {
        float diff = outPos[2] - param_7;
        return fabsf(diff) <= param_6;
    }
    return false;
}


// ============================================================
// RemovalTool::CanRemoveBuilding (0x140104670) — building-removal eligibility.
// this+0x38 -> CreateRemovalDescription; checks flag +0x41; if the entity is
// present and its description differs from the description's +0x38 template,
// finds the DestroyComponent (0x140005780) and the StateMachineComponent
// (0x140003ea0), then reads the state byte (+0x52 / +0x50 fallback) and returns 1
// when the state is not 3/4/5 (Demolish / HaulAway / Deconstruct).
// ============================================================
uint64_t RemovalTool::CanRemoveBuilding(ComponentEntity* const* param_2) {
    if (removalDesc->_removeBuildings && (*param_2 != nullptr)) {
        ComponentEntity* entity = *param_2;
        if (entity->_description != removalDesc->_remover) {
            DestroyComponent* destroy = (*param_2)->FindComponent<DestroyComponent>();

            StateMachineComponent* machine = (*param_2)->FindComponent<StateMachineComponent>();

            if (destroy != nullptr) {
                if (RequestedState(machine) != StateByte(BuildingState::Demolish)) {
                    if (RequestedState(machine) != StateByte(BuildingState::HaulAway)) {
                        if (RequestedState(machine) != StateByte(BuildingState::Deconstruct)) {
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

// ============================================================
// RemovalTool::CanRemoveRoad (0x140104770) — road/path-removal eligibility.
// this+0x38 -> CreateRemovalDescription; checks flag +0x42; template compare; if
// the entity's DestroyComponent (0x140005780) is present, runs 0x14011e710 on it
// and returns 1 when that says removable (or the template matches).
// ============================================================
uint64_t RemovalTool::CanRemoveRoad(ComponentEntity* const* param_2) {
    if (removalDesc->_cancelClear && (*param_2 != nullptr)) {
        ComponentEntity* entity = *param_2;
        if (entity->_description == removalDesc->_remover) {
            return 1;  // LAB_1401047aa
        }
        DestroyComponent* comp = (*param_2)->FindComponent<DestroyComponent>();
        if (comp != nullptr) {
            DestroyComponent* comp2 = (*param_2)->FindComponent<DestroyComponent>();
            if (comp2->InRemovalState()) return 1;  // goto LAB_1401047aa
        }
    }
    return 0;
}

// ============================================================
// RemovalTool::CanRemoveField (0x1401048a0) — crop/field-area removability check.
// Finds the CreateDragDescription (cell 0x1401c7430) among the sub-descriptions of
// the removal description's +0x38 template; if present, the description's flag
// (+0x20) set, and the target entity (param_2) carries a NaturalResourceComponent
// (0x1400052c0) and its description is one of the removal description's +0x28
// list (or that list is empty), it tests the single cell (param_3,param_4) via
// 0x14009f5d0. Returns 1 when the cell passes.
//
// Faithful 1:1 port. DAT_140172208 = 100.0f. The original's string-cleanup
// branch operates on zero-initialized locals; reproduced as a null free.
// ============================================================
uint64_t RemovalTool::CanRemoveField(ComponentEntity* const* param_2, int param_3, int param_4) {
    const uint32_t tidDrag = CreateDragDescription::TypeId();
    const float kHundred = 100.0f;  // DAT_140172208

    Map* local_78 = GameUpdateLoop::_singleton->m_game;

    // Scan the +0x38 template's sub-descriptions for the CreateDragDescription.
    CreateDragDescription* matched = nullptr;  // pvVar9
    {
        const ComponentDescription* lst = removalDesc->_remover;
        if (lst != nullptr) {
            int n = lst->_subDescCount;
            int idx = 0;
            while (idx < n) {
                Description* comp = lst->_subDescs[idx];
                if (comp->Type() == tidDrag) {
                    Description* c = lst->_subDescs[idx];
                    if (Object::IsDerivedFromNullSafe(c, tidDrag))
                        matched = static_cast<CreateDragDescription*>(c);
                    break;
                }
                idx++;
            }
        }
    }

    if (removalDesc->_removeResources && (matched != nullptr) && (*param_2 != nullptr)) {
        NaturalResourceComponent* resource = (*param_2)->FindComponent<NaturalResourceComponent>();
        if (resource != nullptr) {
            int compCount = removalDesc->_removeTypes._size;
            int iVar12 = compCount;  // captured before the loop (decompile semantics)

            // Determine whether param_2's entity belongs to this field.
            bool atWorkBlock = false;
            {
                int idx = 0;
                while (idx < compCount) {
                    const ComponentDescription* lv =
                        removalDesc->_removeTypes[idx];
                    if (lv == (*param_2)->_description) {
                        atWorkBlock = true;  // LAB_140104a0f
                        break;
                    }
                    idx++;
                    compCount = removalDesc->_removeTypes._size;
                }
            }
            if (!atWorkBlock && iVar12 == 0) atWorkBlock = true;  // 'if (iVar12 == 0) goto LAB_140104a0f'

            bool ok = false;
            if (atWorkBlock) {
                char cVar1 = static_cast<char>(matched->_ignoreZones);   // override flag
                uint32_t passMask = matched->_placeBits;

                // The y/x loops each run exactly once: y in [param_4,param_4],
                // x in [param_3,param_3].
                bool all = true;
                for (int y = param_4; y < param_4 + 1; y++) {
                    for (int x = param_3; x < param_3 + 1; x++) {
                        // param_9 (string '.' check) is always false: local_90==0.
                        bool cell = local_78->TestPlacement(x, y, passMask, 0.0f, kHundred, 0.0f, cVar1, '\0', nullptr);
                        if (!cell) { all = false; break; }
                    }
                    if (!all) break;
                }
                ok = all;
            }

            // Original cleanup branch (string state) runs on zero locals.
            System::MemoryAllocator::MemFree(nullptr);

            if (ok) return 1;
        }
    }
    return 0;
}