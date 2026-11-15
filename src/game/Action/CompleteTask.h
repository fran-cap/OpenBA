#pragma once
// ============================================================================
// src/game/Action/CompleteTask.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `CompleteTask`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CompleteTask", parent "Action", ti 0x1401c5e50
//   create 0x140021fc0: MemAlloc(0x38, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x14015fbd0, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class CitizenComponent;   // Component/CitizenComponent.h
class ComponentEntity;    // Entity/ComponentEntity.h

class CompleteTask final : public Action {
    DECLARE_EXE_TYPE(CompleteTask)

public:
    CompleteTask();                                                   // create 0x140021fc0
    CompleteTask(uint32_t taskIndex, CitizenComponent* const* workEntity,
                 ComponentEntity* const* targetEntity, uint32_t taskParam);   // 0x1400c5da0
    ~CompleteTask() override = default;                        // [0] 0x140022030
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400c5be0
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400c5b40
    void CheckEntity() override;                               // [8] 0x1400c5ac0
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400c5a50

    // ---- fields, retail order (PUBLIC) ----
    Object::InternalPtr<CitizenComponent> workEntity;   // +0x20 the working citizen (Persist checks CitizenComponent)
    Object::InternalPtr<ComponentEntity>  targetEntity; // +0x28 target entity ref (Persist checks ComponentEntity)
    int32_t   taskIndex;         // +0x30 task index (0x7FFFFFFF = unresolved)
    int32_t   taskParam;         // +0x34 task parameter
};

static_assert(sizeof(CompleteTask) == 0x38, "CompleteTask is 0x38 (create 0x140021fc0 MemAlloc)");
static_assert(alignof(CompleteTask) == 8, "CompleteTask is 8-aligned");
static_assert(offsetof(CompleteTask, workEntity) == 0x20, "CompleteTask +0x20");
static_assert(offsetof(CompleteTask, targetEntity) == 0x28, "CompleteTask +0x28");
static_assert(offsetof(CompleteTask, taskIndex) == 0x30, "CompleteTask +0x30");
static_assert(offsetof(CompleteTask, taskParam) == 0x34, "CompleteTask +0x34");
