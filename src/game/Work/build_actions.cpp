// src/game/Work/build_actions.cpp
// Faithful 1:1 port of FUN_1400c0690 — BuildStructure::SetupBuildActions: builds the
// citizen build-labor action chain and pushes it onto the CharacterComponent action
// stack. Called from BuildStructure::Execute (buildstructure.cpp, via the engine_funcs
// global fn_SetupBuildActions == ResolveAddr(Exe, 0x1400c0690)). Unported/NULL in
// standalone was the EXEC-of-0 crash blocking organic build labor.
//
#include "Work/exe_bodies.h"
// Signature (matches buildstructure.cpp's call = fn_SetupBuildActions(&citizenEntityVal,
// self->field_20, (void*)(bs+0x10), bestPos, chosenIndex, (uint8_t)self->field_48)):
//   param_1 = citizen entity InternalBasePtr* (the builder)
//   param_2 = priority / task index (CompleteTask +0x30)
//   param_3 = building entity InternalBasePtr* (the construction site = BuildStructure+0x10)
//   param_4 = target world position (float[3]) — MoveTo destination + heading source
//   param_5 = task param (CompleteTask +0x34)
//   param_6 = altAnim byte — selects the state-8 SendComponentMessage id + tool/anim variant
//
// The chain (order of PUSH via TaskListAppend 0x1400c54e0; the stack is LIFO so it runs
// bottom-to-top): a LEAD SendComponentMessage (target type-id 70e8 = CitizenAIComponent) is stored at CharacterComponent
// +0x58 (NOT pushed — it fires when the chain completes), then pushed in order:
// CompleteTask -> SendComponentMessage(c7008 idle / c75f0 alt) -> ShowTool(-1) ->
// PerformWork(3.0s, heading anim) -> MoveTo(Fast) -> ShowTool(Knife 7 / Saw 4). When the
// timed PerformWork fires, the lead message posts back to the BuildComponent and credits
// build progress (BuildComponent+0x48).
//
// Offset-exact against the 0x1400c0690 disassembly (Ghidra): float consts
// DAT_140172180=3.0f (PerformWork time), DAT_140172280=-1.0f (MoveTo speed sentinel =
// "randomize default"); MoveTo pathType=2 (Fast); ShowTool#1 tool=-1; ShowTool#2 tool=
// uVar2?4(Saw):7(Knife); PerformWork actionType=uVar2?0xd:0x10, p6=1, p7=-1, animIndex=
// Entity_impl_29a0(pos,&buildingEntity) heading; uVar2=Random::Rand(*(gw+0xd8),2). The
// msg-id .rdata globals (c70e8/c7008/c75f0) are 0 in the static image; in standalone the
// BuildComponent type-id (c7008) is healed via GetTypeIdByName so the state-8 message
// targets the BuildComponent (the others carry 0, matching citizenai_actionctors).
//
// fn_SetupBuildActions corresponds to the exe helper at c0690.

#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"
#include "Action/exe_bodies.h"   // B3R: the action ctor bodies called by name
#include "Component/CitizenAIComponent.h"   // CitizenAIComponent::TypeId() (the message target)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Component/BuildComponent.h"   // ENDGAME B.3.8 CB3: BuildComponent::TypeId()
#include "Component/DestroyComponent.h"   // ENDGAME B.3.8 CB3: DestroyComponent::TypeId()
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Entity/ComponentEntity.h"
#include "Action/SendComponentMessage.h"   // 0x1400cb1d0, by name
#include "Action/CompleteTask.h"           // 0x1400c5da0, by name
#include "Action/MoveTo.h"                 // 0x1400c9170, by name
#include "Action/ShowTool.h"               // 0x140042890, by name
#include "Action/PerformWork.h"            // 0x1400c9720, by name

namespace {

// Already-ported action ctors + stack ops, called by name (B3R; Action/ and Work/
// exe_bodies.h): 0x1400c5400 WorkBase_TaskListCleanup_c5400, 0x1400c54e0
// Pf_ActStackPush_54e0, 0x1400c29a0 ActChild_Entity29a0_c29a0, 0x1400cb1d0
// Pf_SendComponentMessageCopyCtor_b1d0, 0x1400c5da0 Pf_CompleteTaskCopyCtor_5da0,
// 0x140042890 ActChild_ShowToolCtor_42890, 0x1400c9720 Pf_PerformWorkCtor7_9720,
// 0x1400c9170 Pf_MoveToCtor_9170; 0x1400012d0 (the CharacterComponent lookup) is
// ComponentEntity::FindComponent<CharacterComponent>.
//
// The action ctors take their InternalPtr arguments as `uint64_t*` / `int64_t*`
// (Action/exe_bodies.h, not this block's header): those casts are LEFT.
uint32_t s_msg7008 = 0, s_msg75f0 = 0;
bool s_init = false;
// ENDGAME B.3.9 (B39_RULES.md D5): 0x1401c7008 and 0x1401c75f0 are registered type-id
// cells (BuildComponent, DestroyComponent) that always hold that class's id once
// Fill* runs, so reading them via the cell is behaviour-neutral with a direct
// Class::TypeId() call. The lead message's 0x1401c70e8 is CitizenAIComponent::TypeId()
// (its OnMessage 0x14010f340 is the immediate re-decision, CitizenAIComponent.h).

void EnsureInit() {
    if (s_init) return;
    // c7008 = BuildComponent type-id (the state-8 message target).
    s_msg7008 = BuildComponent::TypeId();
    // c75f0 = DestroyComponent type-id -- the altAnim/demolition variant of the same
    // state-8 message.
    s_msg75f0 = DestroyComponent::TypeId();
    s_init = true;
}


inline void* Alloc(uint64_t sz)          { return System::MemoryAllocator::MemAlloc(sz, 8); }

} // namespace

extern "C" void BuildStructure_SetupBuildActions(CitizenComponent** citizenRef, uint32_t priority,
                    const Object::InternalPtr<ComponentEntity>* building, float* position, uint32_t taskParam, uint32_t altAnim) {
    EnsureInit();
    if (!citizenRef) return;

    CitizenComponent* citizen = *citizenRef;
    if (!citizen) return;
    CharacterComponent* character = citizen->character.Get();
    if (!character) return;
    // The action stack (base +0x48, top +0x60), as the stack ops take it.
    ActionStack* stack = &character->actionStack;

    // heading = Entity_impl_29a0(position, &buildingEntityValue) — used as PerformWork anim.
    ComponentEntity* buildingEntity = building ? building->Get() : nullptr;
    float heading = 0.0f;
    heading = HeadingToward(position, &buildingEntity);

    // WIPE the action stack (start the new chain fresh).
    stack->Clear();

    // --- LEAD SendComponentMessage (target type-id 70e8 = CitizenAIComponent) -> stored at CharacterComponent+0x58 (NOT pushed) ---
    {
        void* mem = Alloc(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* outIBP = nullptr;
            uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
            ComponentEntity* targetEntity = citizen->_entity.Get();
            act = ::new (mem) SendComponentMessage(&targetEntity, &msgId, 0, &outIBP, 0);   // 0x1400cb1d0
        }
        if (character->actionStack.field_0x10 != nullptr)
            character->actionStack.field_0x10.DeletePtr();
        character->actionStack.field_0x10 = act;
    }

    // uVar2 = Random::Rand(*(gw+0xd8), 2) — tool/anim variant (0 or 1).
    uint32_t uVar2 = 0;
    {
        GameUpdateLoop* gw = GameUpdateLoop::_singleton;
        System::Random* rng = gw ? gw->m_random : nullptr;
        if (rng) uVar2 = rng->Rand(static_cast<uint32_t>(2));
    }

    // --- CompleteTask (0x38) -> PUSH ---
    {
        void* mem = Alloc(0x38);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* outIBP = nullptr;
            CitizenComponent* ent = *citizenRef;   // citizen
            act = ::new (mem) CompleteTask(priority, &ent, &outIBP, taskParam);   // 0x1400c5da0
        }
        ::Object::InternalPtr<Action> ibp = act;
        ActionStackPush(stack, &ibp);
    }

    // --- SendComponentMessage #2 (msg c7008 idle / c75f0 alt, target=building) -> PUSH ---
    {
        void* mem = Alloc(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* outIBP = nullptr;
            uint32_t msgId = (altAnim & 0xff) ? s_msg75f0 : s_msg7008;
            ComponentEntity* bldg = building ? building->Get() : nullptr;   // building entity
            act = ::new (mem) SendComponentMessage(&bldg, &msgId, 0, &outIBP, 0);   // 0x1400cb1d0
        }
        ::Object::InternalPtr<Action> ibp = act;
        ActionStackPush(stack, &ibp);
    }

    // --- ShowTool #1 (tool = -1) -> PUSH ---
    {
        void* mem = Alloc(0x30);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* ent = *citizenRef;
            act = ::new (mem) ShowTool(&ent, -1);   // 0x140042890
        }
        ::Object::InternalPtr<Action> ibp = act;
        ActionStackPush(stack, &ibp);
    }

    // --- PerformWork (0x48, timed 3.0s, heading anim) -> PUSH ---
    {
        void* mem = Alloc(0x48);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* ent = *citizenRef;
            System::Time workTime;
            RtSystem_Time_Ctor(&workTime, 3.0f);   // DAT_140172180
            uint32_t actionType = (uVar2 != 0) ? 0xd : 0x10;
            act = ::new (mem) PerformWork(&ent, heading, actionType, workTime._ticks, 1, -1);   // 0x1400c9720
        }
        ::Object::InternalPtr<Action> ibp = act;
        ActionStackPush(stack, &ibp);
    }

    // --- MoveTo (0x480, Fast, to the citizen entity's CharacterComponent) -> PUSH ---
    {
        void* mem = Alloc(0x480);
        Action* act = nullptr;
        if (mem) {
            CharacterComponent* mover = citizen->_entity->FindComponent<CharacterComponent>();
            act = ::new (mem) MoveTo(&mover, 2 /*pathType Fast*/, 0, position, PathBits::Walk, 0, -1.0f /*DAT_140172280*/);   // 0x1400c9170
        }
        ::Object::InternalPtr<Action> ibp = act;
        ActionStackPush(stack, &ibp);
    }

    // --- ShowTool #2 (tool = uVar2 ? 4(Saw) : 7(Knife)) -> PUSH ---
    {
        void* mem = Alloc(0x30);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* ent = *citizenRef;
            int toolType = (uVar2 != 0) ? 4 : 7;
            act = ::new (mem) ShowTool(&ent, toolType);   // 0x140042890
        }
        ::Object::InternalPtr<Action> ibp = act;
        ActionStackPush(stack, &ibp);
    }

}
