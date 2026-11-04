// src/game/Action/completetask.cpp
// Clean C++ reimplementation of CompleteTask action methods.
// Source: Game/Action/CompleteTask.cpp (from profile build debug strings)
//
// CompleteTask — 0x38 bytes. Completes a task/work order on a building.
// Inherits: Object::Internal -> Action (0x20) -> CompleteTask
// Ctor: 0x140021fc0 (factory), Vtable: 0x14015fbd0
//
// Decompiled from Application-x64.exe using Ghidra.

#include "CompleteTask.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"   // ActCt_* task helpers
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include "Component/CitizenComponent.h"   // ENDGAME B.3.8 CB3: CitizenComponent::TypeId()
#include "Entity/ComponentEntity.h"   // ENDGAME B.3.8 CB3: ComponentEntity::TypeId()

#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName (heal .rdata type-ids)
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Game/WorkScheduler.h"   // WorkScheduler::TryCompleteTask / TaskReassign / TaskCleanup


// ============================================================
// Helpers, called by name (B3R)
//   0x1400f8760 ActCt_TryCompleteTask_8760 -- try complete task
//   0x1400f8e30 ActCt_TaskCleanup_8e30     -- cleanup after task completion
//   0x1400f8800 ActCt_TaskReassign_8800    -- task reassignment (used by CheckEntity)
// DAT_1401c3dd8 (the game state singleton; the task manager is *(it+0x118)) is read
// through g_pGameWorldSingleton, the engine_funcs global bound to the same cell.
// ============================================================

// ============================================================
// Serialize (0x1400c5be0, vtable slot 3)
// Serializes status, entity refs with type checks, task fields.
// ============================================================
void CompleteTask::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Serialize entity refs + task fields (version 1)
    IO::Serializer ser(stream, 1);

    // Work citizen ref (+0x20) with type validation.
    workEntity.InternalBasePtr::Serialize(ser);
    if (!workEntity.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(workEntity, CitizenComponent::TypeId())) {
            workEntity = nullptr;
        }
    }

    // Target entity ref (+0x28) with type validation
    targetEntity.InternalBasePtr::Serialize(ser);
    if (!targetEntity.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(targetEntity, ComponentEntity::TypeId())) {
            targetEntity = nullptr;
        }
    }

    ser.Serialize(taskParam, nullptr);   // +0x34

    // Task index (+0x30) — versioned, only in version >= 1
    int version = ser.GetVersion();
    if (version > 0) {
        ser.Serialize(taskIndex, nullptr);
    }
}

// ============================================================
// AbstractImpl (0x1400c5b40, vtable slot 4)
// Resolves task index from work entity if needed, calls task completion.
// Returns 0 (action complete).
// ============================================================

int  CompleteTask::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;
    // Retail 0x1400c5b40 reads the scheduler (g_pGameWorldSingleton + 0x118) with no test of
    // the singleton or of +0x118 (B6 CLOSE-B, the 14d ACT-b bullet): the old "inert in
    // standalone" guard went with CheckEntity's (row 13, TRADE).

    // Lazy-resolve task index from the work citizen
    if (this->taskIndex == 0x7FFFFFFF) {
        this->taskIndex = workEntity->field_0x30;
    }

    // taskIdx < 0 is EXPECTED, not a gap. The build-material haul tree
    // (GatherRawMaterials_Serialize 0x1400ec406) stamps CompleteTask+0x30 from the
    // Work item's +0x20 field ("pathType", reused as workId), which is -1 for hauls
    // that are NOT registered as a task-manager BST node — i.e. a building's ongoing
    // "needs N stone" that any number of builders may service. Retail runs the very
    // same no-op here (its own epilogue guards `-1 < Work+0x20` before setting the
    // citizen +0x88 flag, and citizenai_gather stamps citizen+0x30 = -1). Delivery is
    // credited via the BUILDCTRL MsgReceive path, not this manager completion. Verified
    // 2026-07-22 against the live BUILD_TRACE (taskIdx=-1, taskMgr live) — do not "fix".
    int32_t taskIdx = this->taskIndex;
    if (taskIdx >= 0) {
        ComponentEntity* entityRef28 = this->targetEntity.Get();
        CitizenComponent* entityRef20 = this->workEntity.Get();
        WorkScheduler* taskMgr = GameUpdateLoop::_singleton->m_tutorial;

        bool completed = taskMgr->TryCompleteTask(&entityRef20, taskIdx, &entityRef28, (uint32_t)this->taskParam);   // 0x1400f8760
        if (completed) {
            taskMgr->TaskCleanup(taskIdx);   // 0x1400f8e30
        }
    }
    return 0;
}

// ============================================================
// CheckEntity (0x1400c5ac0, vtable slot 8)
// Resolves task index, calls task reassignment. Retail reads the singleton and its
// +0x118 scheduler unguarded (0x1400c5af4 / 0x1400c5aff): no null test (B6 TRADE, row 13).
// This is the removal half of every Work that tracks a citizen through its CompleteTask
// (TradeWork::OnCancel, slot 7): the action-stack clear 0x1400c5400 reaches it.
// ============================================================
void CompleteTask::CheckEntity() {
    // Lazy-resolve task index
    if (this->taskIndex == 0x7FFFFFFF) {
        this->taskIndex = workEntity->field_0x30;
    }

    ComponentEntity* entityRef28 = this->targetEntity.Get();
    CitizenComponent* entityRef20 = this->workEntity.Get();
    WorkScheduler* taskMgr = GameUpdateLoop::_singleton->m_tutorial;

    taskMgr->TaskReassign(&entityRef20, this->taskIndex, &entityRef28, (uint32_t)this->taskParam);   // 0x1400f8800
}

// ============================================================
// GetTarget (0x1400c5a50, vtable slot 9)
// Returns true if param matches entity at +0x28 or parent entity at +0x20.
// ============================================================
bool CompleteTask::GetTarget(ComponentEntity** param) {
    void* paramVal = *param;

    // Check direct match with target entity
    if (this->targetEntity.Get() == paramVal) {
        return true;
    }

    // Check the work citizen's owning entity (Component +0x10)
    return workEntity->_entity.Get() == paramVal;
}

// ============================================================
// class CompleteTask (CompleteTask.h) — ENDGAME B.3.8 ACT.
// The hand-built g_CompleteTask_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x140021fc0 (inlined ctor): +0x20 / +0x28 = 0, then one qword store at +0x30
// (140022019 MOV qword ptr [RBX + 0x30],0x7fffffff): taskIndex = 0x7FFFFFFF, the
// "unresolved" value AbstractImpl replaces with the citizen's task, and taskParam = 0.
// A version-0 record (Persist reads taskIndex only at version > 0) keeps it.
CompleteTask::CompleteTask()
    : workEntity(nullptr),
      targetEntity(nullptr),
      taskIndex(0x7FFFFFFF),
      taskParam(0) {}

// [3] 0x1400c5be0
// [4] 0x1400c5b40
// [8] 0x1400c5ac0
// [9] 0x1400c5a50

::Object::Base* CompleteTask::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CompleteTask), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CompleteTask());
}
::Object::Base* CompleteTask::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CompleteTask());
}


// ============================================================
// FUN_1400c5da0 — CompleteTask_Action CopyCtor
// Base Internal ctor, then Action vtable -> CompleteTask vtable; copies two IBP fields
// (+0x20 from param_3, +0x28 from param_4) and two int fields (+0x30=param_2, +0x34=param_5).
// ============================================================

// 0x1400c5da0: +0x20 = *param_3 ; +0x28 = *param_4 ; +0x30 = param_2 ; +0x34 = param_5.
CompleteTask::CompleteTask(uint32_t taskIndexArg, CitizenComponent* const* workEntityArg,
                           ComponentEntity* const* targetEntityArg,
                           uint32_t taskParamArg)
    : workEntity(*workEntityArg),
      targetEntity(*targetEntityArg),
      taskIndex(static_cast<int32_t>(taskIndexArg)),
      taskParam(static_cast<int32_t>(taskParamArg)) {}

DEFINE_EXE_TYPE(CompleteTask, "Action")
