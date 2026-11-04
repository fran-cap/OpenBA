// src/game/Work/work_base_reimpl.cpp
// Shared Work base-class / ECS helpers referenced from many Work classes.
//
// These exe sub-helpers are resolved via ResolveAddr(Module::Exe, 0xADDR) from
// nearly every src/game/Work/*.cpp. They resolve to 0 (silently skipped) unless
// registered.
// This file provides faithful reimplementations and a single registration point
// (RegisterWorkBaseBatch in standalone/reg_work_base.cpp) so the shared call sites
// fire real logic in the standalone exe.
//
#include "Work/exe_bodies.h"
// Wave 2 (Work) — shared base-helper sub-batch. Each function is a 1:1 port of the
// decompiled exe helper; do not "improve" behavior.
//

#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"
#include "Action/exe_bodies.h"      // B3R: Pf_IsDstReachable_f810, Pf_ZoneRefresh_9c480
#include "Component/exe_bodies.h"   // B3R: FloorComponent_FindResData
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Work/Work.h"         // ENDGAME B.3.8 (WRK): Work::Persist / Work::InitFromEntity
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Component/InteractComponent.h"   // ENDGAME B.3.8 CB3: InteractComponent::TypeId()
#include "Component/WorkComponent.h"   // ENDGAME B.3.8 CB3: WorkComponent::TypeId()
#include "Component/StorageComponent.h"   // ENDGAME B.3.8 CB3: StorageComponent::TypeId()
#include "Component/TradeComponent.h"   // ENDGAME B.3.8 CB3: TradeComponent::TypeId()
#include "Component/CitizenComponent.h"   // ENDGAME B.3.8 CB3: CitizenComponent::TypeId()
#include "Action/CompleteTask.h"   // ENDGAME B.3.8 CB3: CompleteTask::TypeId()
#include "Component/StatusIconComponent.h"          // ENDGAME B.3.9 D5
#include "Component/NaturalResourceComponent.h"     // ENDGAME B.3.9 D5
#include "Entity/ComponentEntity.h"                 // ENDGAME B.3.9 D5
#include "Component/CharacterComponent.h"            // B4 WORK-c: Work::ValidateTask
#include "Component/MapComponent.h"                  // B4 WORK-c: Work::InitFromEntity
#include "Action/Action.h"
#include "Game/Map.h"                                // B4 WORK-c: Map, MapCell


// (FUN_140002ed0 = ComponentEntity::FindComponent<InteractComponent>(): its B4 FORWARD
// forwarder WorkBase_FindComponent_2ed0 had three callers left, which now call the
// member: deleted, B4 X4-WORK. 2f90 / 3050 / 52c0 went in B4 P2 WORK-3.)

// ============================================================
// Work_Ctor25 (0x1400f72e0) — the shared Work base constructor.
// ============================================================
// Object::Internal::Internal(self); vtable = vtable_Work (0x140161a80); +0x10 = entity
// ptr (from *param2); sentinels +0x20/+0x28 = -1; and when an entity is present, compute
// the work target cell (+0x28) from its MapComponent footprint (via FUN_140004680) and
// store its WorkComponent (+0x18, via FUN_140003050). Faithful 1:1 port.
//
// ENDGAME B.3.9 OBJ: `Work(void*)` (work_base_standalone.cpp) is the ONE entry point.
// The by-address door `Work_Ctor25_Impl` (0x1400f72e0: the `??0Internal` door through
// import slot 0x140152768, a stamp of Work's table, then this body) went with its last
// callers, the attendance / destroy / fire CreateWork builders, which now construct the
// real leaf (LocationWork / ClearStorage / FireWork) through `Work(void*)`.
void Work::InitFromEntity(ComponentEntity* const* pEntityIBP) {
    ComponentEntity* const* entityRef = pEntityIBP;   // LEFT: SIG Work::InitFromEntity(void*) / Work(void*): the leaves' builders (resourceproduce_creatework.cpp, tradecomponent_states.cpp, cropfieldcomponent_states.cpp ...) hand the entity reference untyped
    field_10 = nullptr;
    field_10 = *entityRef;   // 0x1400f731a MOV RAX,[RSI]: no test (B6 14d G21)
    field_18 = nullptr;
    field_20 = -1;
    field_24 = 0;
    field_28 = -1;
    field_2c = 0;
    field_30._ticks = 0;
    field_38._ticks = 0;
    field_40 = 0;

    if (field_10 != nullptr) {
        // 0x140004680 = FindComponent<MapComponent>, called twice as retail does
        // (0x1400f7361 / 0x1400f737f); the first result is tested (0x1400f7366), the
        // second and [0x1401c3dd8]+0xf8 are read with no test (0x1400f7390-0x1400f73b0).
        if (field_10->FindComponent<MapComponent>() != nullptr) {
            MapComponent* footprint = field_10->FindComponent<MapComponent>();
            Map* map = GameUpdateLoop::_singleton->m_game;
            field_28 = (((int)footprint->tileY1 >> 1) + (int)footprint->tileY0) * map->size +
                       (((int)footprint->tileX1 >> 1) + (int)footprint->tileX0);
        }
        // 0x140003050 = FindComponent<WorkComponent>, on the entity re-read from *param_2.
        field_18 = (*entityRef)->FindComponent<WorkComponent>();
    }
}

// (0x140014c30 / 0x14000e320, the recursive node free: the template WorkBase_TreeNodeFree,
// Work/exe_bodies.h, B9.)

// ============================================================
// Work base lifecycle
// ============================================================

// Reimpl of FUN_1400c5400 (TaskListCleanup) — cancel all pending work actions.
// self layout: [0]=action-list array base, +8=count(int), +0x10=IBP, +0x18=int sentinel.
// For each populated list slot, walk the singly-linked action chain (next at node+0x10),
// invoking each action's vtable +0x40 (cancel/abort); then DeletePtr the slot IBP and
// re-init it to null. Finally reset the +0x18 sentinel to -1 and clear the +0x10 IBP.
// The receiver is the action stack embedded in CharacterComponent at +0x48
// (CharacterComponent::actionStack, Action.h's ActionStack).
void ActionStack::Clear() {
    // ⚠ Retail re-reads BOTH the array pointer (*param_1) and the capacity
    // ((int)param_1[1]) on EVERY iteration — the +0x40 Cancel vcalls below can
    // push onto this same list (FUN_1400c54e0) and grow/realloc it mid-loop.
    // An earlier port cached both once; the first cancel-push that forced a
    // grow left the cached pointer dangling and the walk read freed memory
    // (crash: organic herder's first PastureWork claim, GrazingWorkCtor →
    // cleanup → MoveTo cancel → push → walk of the stale array).
    for (int i = 0; i < actions._size; i++) {
        Action* node = actions[i].Get();
        if (!node) continue;
        Action* cur = node;
        do {
            // 0x1400c5444: RCX = cur only.
            cur->CheckEntity();   // slot 8
            cur = cur->_next.Get();   // node[2] = next (Action::_next, +0x10)
        } while (cur != nullptr);
        // Slot address recomputed from the CURRENT array pointer (may have moved).
        ::Object::InternalPtr<Action>* slot = &actions[i];
        slot->DeletePtr();
        *slot = ::Object::InternalPtr<Action>(nullptr);
    }
    current = -1;
    if (field_0x10 != nullptr) {
        field_0x10.DeletePtr();
        Action* nul = nullptr;
        new (&nul) ::Object::InternalBasePtr(nullptr);
        field_0x10 = nul;
    }
}

// standalone/work_ser_probe.cpp - BANISHED_WORK_SER_PROBE=1 stale-target census.
// ⚠ NOT a null-guard on the derefs below: with the env var unset (the default) this
// returns immediately and the faithful retail deref stands untouched. It exists only
// to turn "save crashed" into a log line naming the stale Work while diagnosing.

// Reimpl of FUN_1400f7400 (Work::Serialize) — serialize a Work task (version 3 stream).
// Two task-target IBPs at +0x10 (must derive ComponentEntity) and +0x18 (must derive
// type 0x1401c8070) are post-validated and nulled on type mismatch; ints at +0x20/+0x28;
// +0x24 is version-gated (copied from +0x20 pre-v1); two Times + an int at +0x30..+0x40
// added at v3+.   Caller signature: void fn(void* self, void* stream)
void Work::Persist(IO::Stream& stream) {
    IO::Serializer ser(stream, 3);

    // +0x10 / +0x18 are InternalPtrs retail serializes through
    // InternalBasePtr::Serialize (the typed wrapper's Serialize(ser, typeId) hides
    // it; the tree validates with IsDerivedFromNullSafe below).
    field_10.Object::InternalBasePtr::Serialize(ser);
    if (field_10) {
        if (!Object::IsDerivedFromNullSafe(field_10, ComponentEntity::TypeId()))
            field_10 = nullptr;
    }
    field_18.Object::InternalBasePtr::Serialize(ser);
    if (field_18) {
        if (!Object::IsDerivedFromNullSafe(field_18, WorkComponent::TypeId()))
            field_18 = nullptr;
    }
    ser.Serialize(field_20, nullptr);
    ser.Serialize(field_28, nullptr);
    int ver = ser.GetVersion();
    if (ver < 1)
        field_24 = field_20;
    else
        ser.Serialize(field_24, nullptr);
    if (ser.GetVersion() > 2) {
        field_30.Serialize(ser);
        field_38.Serialize(ser);
        ser.Serialize(field_40, nullptr);
    }
}   // ~Serializer after the last field, as retail

// 0x1400f7100 (see Work.h). RCX = this (+0x20 read), RDX never read, R8 = the
// citizen InternalPtr, R9 = the optional out slot.
bool Work::ValidateTask(void* /*unusedRdx*/, CitizenComponent** citizen, CompleteTask** out) {
    if (*citizen == nullptr) return false;
    // DWORD_1401c7158 = CitizenComponent (ENDGAME B.3.9 D5: by cell).
    if (!(*citizen)->IsA(CitizenComponent::TypeId())) return false;

    CharacterComponent* character = (*citizen)->character.Get();
    // 0x1401c5e50 (CompleteTask's own cell) is UNREGISTERED -- the old raw-.rdata read
    // always failed and fell through to this same CompleteTask::TypeId() call, so
    // calling it directly is not a behaviour change (ENDGAME B.3.9 D5 note).
    uint32_t completeTaskTid = CompleteTask::TypeId();
    // The bound (+0x60 + 1) is re-read every pass, as retail (0x1400f7204).
    for (int i = 0; i < character->actionStack.current + 1; i++) {
        Action* action = character->actionStack.actions[i].Get();
        // The tid arm is the tree's guard; retail calls IsDerivedFrom directly.
        CompleteTask* task = (completeTaskTid && Object::IsDerivedFromNullSafe(action, completeTaskTid))
                                 ? static_cast<CompleteTask*>(action) : nullptr;
        if (task != nullptr && task->taskIndex == field_20) {
            if (out) *out = task;
            return true;
        }
    }
    return false;
}

// ============================================================
// Map distance / connectivity helpers
// ============================================================

// Reimpl of FUN_140008830 (Map_CalcDistance) — pathfinding-aware cell distance.
// Tries the cached path cost (Map_CalcPathCost, 0x14009f810); on no-path (<0) falls
// back to Euclidean grid distance (sqrtf) + gridWidth penalty. FUN_140132010 in the
// original is sqrtf (verified). Caller: int fn(Map* map, uint32_t cellA, uint32_t cellB)
int Map::CalcDistance(uint32_t cellA, uint32_t cellB) {
    int cost = PathDistance(cellA, cellB, 1);   // 0x14009f810 (was its Pf_IsDstReachable_f810 forwarder)
    if (cost < 0) {
        int gridW = size;
        int dRow = static_cast<int>(cellA) / gridW - static_cast<int>(cellB) / gridW;
        int dCol = static_cast<int>(cellA) % gridW - static_cast<int>(cellB) % gridW;
        cost = static_cast<int>(sqrtf(static_cast<float>(dRow * dRow + dCol * dCol))) + gridW;
    }
    return cost;
}

// FUN_14009c480 — connectivity/zone refresh for the (pathType, destCell, srcCell) query:
// Pf_ZoneRefresh_9c480 (Action/exe_bodies.h), called by name with all four arguments.

// Reimpl of FUN_1400a00f0 (Map::CanPathTo / WorldGrid_ValidateCell) — are two cells
// mutually reachable? Refreshes connectivity (FUN_14009c480), then ORs the two cells'
// pass-type bits with pathType; if both cell types survive the mask it returns 1 when a
// road/overlay bit (0x1e0) is set or when both cells share a non-zero region (uint16 @
// +0x26). Cell records: array at grid+0x80, stride 0x30, pass byte +0x29.
// Caller: bool fn(Map* map, uint32_t pathType, uint32_t destCell, int srcCell)
//   (low byte of the returned uint64 is the bool; the WorldGrid_ValidateCell int-variant
//    callers read the low 32 bits).
uint64_t Map::ValidateCell(uint32_t pathType, uint32_t destCell, int srcCell) {
    // 0x1400a00f0 tests neither cell: FillZones, then [RCX+0x80] indexed by both
    // (B9 RES row 1120 dropped the port's range test; its route, a FindComponent<MapComponent>
    // with an unfilled type id, no longer exists).
    FillZones();   // 0x14009c480 (was its Pf_ZoneRefresh_9c480 forwarder)

    // Both cells are sign-extended before the index (0x1400a0104 MOVSXD RSI,R9D /
    // 0x1400a0107 MOVSXD RDI,R8D).
    const MapCell& recSrc  = cells.GetData()[srcCell];
    const MapCell& recDest = cells.GetData()[static_cast<int>(destCell)];

    uint32_t typeSrc  = 1u << (recSrc.terrainType & 0x1f);
    uint32_t typeDest = 1u << (recDest.terrainType & 0x1f);
    uint32_t combined = typeDest | typeSrc;
    uint64_t ret = combined;

    uint32_t mask = pathType | combined;
    if ((mask & typeDest) && (mask & typeSrc)) {
        if (mask & 0x1e0u) return (ret & 0xffffffffffffff00ull) | 1u;
        uint16_t regionDest = recDest.zone;
        if (regionDest != 0) {
            uint16_t regionSrc = recSrc.zone;
            ret = regionSrc;
            if (regionSrc != 0 && regionDest == regionSrc)
                return (ret & 0xffffffffffffff00ull) | 1u;
        }
    }
    return ret & 0xffffffffffffff00ull;
}
