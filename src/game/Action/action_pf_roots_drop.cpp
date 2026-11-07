// src/game/Action/action_pf_roots_drop.cpp
// Wave 3 (Action) — DropOff / RemoveAnimal-ctor ROOT callers.
//
// Faithful 1:1 ports of the 4 root-caller exe helpers in the raw-material drop-off /
// RemoveAnimal-action closure. Each is transcribed literally from the Ghidra decompile,
// cross-checked against disassembly for the call-site arg ABI. Do NOT "improve" — none
// of this is exercised on the autostart path, so a wrong port is undetectable latent
// corruption.
#include "Action/exe_bodies.h"
//
// Registration: RegisterPfRootsDropBatch() in standalone/reg_pf_roots_drop.cpp.
//
// Sub-callees (called by name, B3R — bodies NOT duplicated):
//   0x14009bd40 CreateRawMaterial_FindPlacementCell — ported by another agent THIS wave
//   0x1400c3720 ComponentEntity_CtorFromDescription (CopyCtor)
//   0x1400223c0 FindComponent (rawmat batch)
//   0x140129760 PositionEntity / helper_9760 (rawmat batch)
//   0x1400c48a0 EntityList_RegisterEntity
//   0x140006270 Entity_FindComponent_Storage (work_base)
//   0x1400012d0 Entity_FindComponent_Family (action_move)
//   0x14012d150 StorageComponent::SubtractItem (rawmat)
//   0x1401167c0 storage count-delta (action_pf_ctors)
//   0x140116760 storage drop-off (action_pf_ctors)
//   0x14012cd80 StorageComponent::CanAdd (Pf_StorageCanAccept_cd80)
//   0x14012d000 StorageComponent::AddItem (rawmat)
//   0x14008d590 FindStorageForResource (Pf_PickBestDropTarget_8d590)
//   0x1400c9170 MoveTo_Action_Ctor (action_pf_ctors)
//   0x1400c9720 PerformWork_Action_Ctor7 (action_pf_ctors)
//   0x1400c5da0 CompleteTask_Action_CopyCtor (action_pf_ctors)
//   0x1400c71a0 CreateRawMaterial_Action_CopyCtor (action_pf_ctors)
//   0x1400cb1d0 SendComponentMessage_Action_CopyCtor (action_pf_ctors)
//   0x1400c54e0 action-stack push (action_pf_ctors) — bbea0/c7620 inline this; call helper
//   0x1400c5400 TaskListCleanup (work_base)
//   0x140107260 set-flag/refresh/Update == RemoveAnimal_helper_7260 (action_pf_ctors)
// Engine members: Object::InternalBasePtr, System::MemoryAllocator::MemAlloc,
//   RtSystem_Time_Ctor, System::Random::RandTol.
//
// bbea0 inlines the RemoveAnimal ctor; ENDGAME B.3.8 ACT: `new (p) RemoveAnimal(animal)`.

#include "Action/RemoveAnimal.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Work/exe_bodies.h"              // WorkBase_FindComponent_6270, WorkBase_TaskListCleanup_c5400
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Game/exe_constants.h"           // kRdata_*
#include "system/MemoryAllocator.h"       // System::MemoryAllocator::MemAlloc
#include <cstring>
#include "Component/CitizenAIComponent.h"   // CitizenAIComponent::TypeId() (the message target)
#include <cstdint>
#include <new>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Action/Action.h"                 // ActionStack
#include "Action/CompleteTask.h"
#include "Action/CreateRawMaterial.h"
#include "Action/DropOffRawMaterial.h"
#include "Action/MoveTo.h"
#include "Action/PerformWork.h"
#include "Action/SendComponentMessage.h"
#include "Action/PathSearch.h"             // RawArray (the drop-target lists)
#include "Component/AnimalDescription.h"
#include "Component/CharacterComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/ModelComponent.h"
#include "Component/RawMaterialComponent.h"
#include "Component/StorageComponent.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"
#include "math/Math.h"

namespace {

// .rdata constants (Game/exe_constants.h): 0x140172204 (c78c0 RandTol(2) scale),
// 0x140172280 (MoveTo speed, c7620 / bbea0), 0x140172194 (PerformWork Time seconds, bbea0),
// 0x1401722a8 (PerformWork animIndex float, bbea0; a bit pattern), 0x14017214c (g_fUVScale,
// the CreateRawMaterial CopyCtor float arg).
inline float RdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }

// 0x1400032e0 (array-grow) was resolved here but never called anywhere in this file
// -- dead resolved-pointer machinery, removed rather than converted (B.3.2 step 2).

// MemAlloc shim matching the original `System::MemoryAllocator::MemAlloc(size, align)`.
static void* MemAlloc(int64_t size, int64_t align) {
    return System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(size), static_cast<int>(align));
}
} // namespace

// ============================================================
// FUN_1400c78c0 — spawn a raw-material pile near the citizen, then take the amount out of
// the citizen's storage.
// If count != 0: pick a placement cell near the citizen's model position (0x14009bd40); on
// success build an entity from the material template (0x1400c3720), find its
// RawMaterialComponent (0x1400223c0), position it (0x140129760) with a small random yaw,
// set quantity = count / profession = 0 and register the entity in the world entity list
// (0x1400c48a0). Finally subtract `count` from the citizen's storage (0x14012d150).
// ============================================================
void DropOffRawMaterial::DropOnGround(ComponentDescription* material, int count) {
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    if (count == 0) return;
    CitizenComponent* citizen = _field30.Get();   // 0x1400c7916 [R14+0x30] (RCX = the action)

    Map* map = world->m_game;
    // 0x14009bd40's allow slot (retail's local_res20, a null InternalBasePtr; the same stack
    // slot is then reused as the find-component out slots below).
    ComponentEntity* allow = nullptr;
    const Math::Vector3& position = citizen->character->modelComponent->transform.translation;
    int size = map->size;
    int x = static_cast<int>(position.x + static_cast<float>(size >> 1));
    int y = static_cast<int>(position.y + static_cast<float>(size >> 1));
    if (x < 0) x = 0;
    if (size <= x) x = size - 1;
    if (y < 0) y = 0;
    if (size <= y) y = size - 1;
    int cell = map->FindPlacementCell(size * y + x, '\0', &allow, PathBits{});   // 0x14009bd40
    if (-1 < cell) {
        ComponentEntity* created = nullptr;
        ComponentEntity* mem = static_cast<ComponentEntity*>(MemAlloc(0x48, 8));
        if (mem != nullptr) {
            created = ::new (mem) ComponentEntity(material);   // 0x1400c3720
        }
        RawMaterialComponent* pile = created->FindComponent<RawMaterialComponent>();   // 0x1400223c0
        // B6 (14d G63): 0x1400c7a13 MOV RBX,[R15+0xd8] and RandTol(2) on it with no
        // test.
        int randv = world->m_random->RandTol(2);
        pile->PositionRawMaterial(cell, static_cast<float>(randv) * kRdata_140172204);   // 0x140129760
        pile->quantity = count;
        pile->profession = nullptr;
        Object::InternalPtr<ComponentEntity> registered = created;
        world->m_entityList->RegisterEntity(&registered,
                                       static_cast<int>(material->_category));   // 0x1400c48a0
    }
    StorageComponent* storage = citizen->_entity->FindComponent<StorageComponent>();   // 0x140006270
    storage->SubtractItem(material, count);   // 0x14012d150
}

// ============================================================
// FUN_1400c7570 — DropOffRawMaterial::CheckEntity's body: spill every stack of the
// citizen's storage onto the ground via 78c0. Re-reads the storage count each iteration
// because 78c0 mutates it (12d150 compacts the array).
// ============================================================
void DropOffRawMaterial::SpillAllStorage() {
    CitizenComponent* citizen = _field30.Get();   // InternalPtr<CitizenComponent> (B.4 ACT-b)
    StorageComponent* storage = citizen->_entity->FindComponent<StorageComponent>();   // 0x140006270
    int n = storage->storageSlots._size;
    while (0 < n) {
        const StorageComponent::Item* items = storage->storageSlots._data;
        n = items[0]._count;   // the first stack's count
        if (0 < n) {
            DropOnGround(items[0]._resource, n);
        }
        n = storage->storageSlots._size;
    }
}

// ============================================================
// FUN_1400c7620 — DropOffRawMaterial executor (deposit-or-move branch).
// The deposit amount = min(self->_field24 cap, the free count from 1167c0); if it fits
// (StorageCanAdd 0x14012cd80) deposit it (d000 AddItem + 116760 source subtract) and return
// 1. Otherwise, if self->_field20 is set, search for a storage destination (8d590); if none,
// spill via 78c0 and return 2; else stash the found entity at self->_field38, build a
// MoveTo toward it and push it (c54e0). Returns 1.
// ============================================================
uint64_t DropOffRawMaterial::Deposit(ActionStack* stack, StorageComponent* const* target,
                                     ComponentDescription* material) {
    CitizenComponent* citizen = _field30.Get();   // InternalPtr<CitizenComponent> (B.4 ACT-b)
    int iVar5 = citizen->StorageCountDelta(material);   // 0x1401167c0
    int iVar8 = _field24;
    int iVar9 = iVar5;
    if ((-1 < iVar8) && (iVar9 = iVar8, iVar5 < iVar8)) {
        iVar9 = iVar5;
    }
    if (0 < iVar9) {
        if ((*target)->CanAdd(material, iVar9)) {   // 0x14012cd80
            (*target)->AddItem(material, iVar9);   // 0x14012d000
            citizen->StorageDropOff(material, iVar9);   // 0x140116760
            return 1;
        }
    }
    if (_field20 != 0) {
        GameUpdateLoop* world = GameUpdateLoop::_singleton;
        Map* map = world->m_game;
        float dropPos[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};   // local_50
        Action* ptr = nullptr;
        Object::InternalPtr<ComponentEntity> exclude = nullptr;   // local_res8
        ComponentEntity* found = nullptr;     // local_70

        const Math::Vector3& position = citizen->character->modelComponent->transform.translation;
        int size = map->size;
        iVar8 = static_cast<int>(position.x + static_cast<float>(size >> 1));
        iVar5 = static_cast<int>(position.y + static_cast<float>(size >> 1));
        if (iVar8 < 0) iVar8 = 0;
        if (size <= iVar8) iVar8 = size - 1;
        if (iVar5 < 0) iVar5 = 0;
        if (size <= iVar5) iVar5 = size - 1;
        world->m_citizenList->PickBestDropTarget(
                           &found, static_cast<uint32_t>(size * iVar5 + iVar8), material, iVar9, '\x01',
                           dropPos, &exclude);   // 0x14008d590
        if (found == nullptr) {
            DropOnGround(material, iVar9);
            return 2;
        }
        _field38 = found;
        void* mem = MemAlloc(0x480, 8);
        if (mem != nullptr) {
            CharacterComponent* character =
                citizen->_entity->FindComponent<CharacterComponent>();   // 0x1400012d0
            ptr = new (mem) MoveTo(&character, /*Slow*/1, 0, dropPos, PathBits::Walk, 0,   // 0x1400c9170
                                   kRdata_140172280);
        }
        ::Object::InternalPtr<Action> pushed(ptr);
        ActionStackPush(stack, &pushed);   // 0x1400c54e0
    }
    return 1;
}

// ============================================================
// FUN_1400bbea0 — RemoveAnimal_Action Ctor. Builds a child-action sub-tree on the citizen's
// action stack (citizen->character, +0x48). `task` = a CompleteTask arg; `destination` =
// the MoveTo destination; `createFlag` = the CreateRawMaterial flag arg; `animal` = the
// animal's component IBP; `description` = the animal's AnimalDescription (its _refs are
// the raw-material templates it leaves); `gate` (low byte). If gated, installs a
// SendComponentMessage and pushes a CompleteTask. Then for each template pushes a
// CreateRawMaterial action; appends a RemoveAnimal, a PerformWork, and a MoveTo; finally
// calls 107260 on the animal's component.
// The original inlined the action-stack push (c54e0) at each append site.
// ============================================================
void RemoveAnimalActions(CitizenComponent* const* citizenIBP, uint32_t task, float* destination,
                         uint32_t createFlag, Component* const* animal,
                         const AnimalDescription* description, uint32_t gate) {
    CitizenComponent* citizen = *citizenIBP;
    CharacterComponent* character = citizen->character.Get();
    // CharacterComponent's +0x48..+0x67 action list (Action/Action.h ActionStack).
    ActionStack* stack = &character->actionStack;

    if (static_cast<char>(gate) != '\0') {
        stack->Clear();   // 0x1400c5400
        // --- SendComponentMessage child action ---
        void* mem = MemAlloc(0x40, 8);
        Action* message = nullptr;
        if (mem != nullptr) {
            ComponentEntity* nullIBP = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
            uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
            ComponentEntity* entity = citizen->_entity.Get();
            message = new (mem) SendComponentMessage(&entity, &msgId, 0, &nullIBP, 0);   // 0x1400cb1d0
        }
        if (character->actionStack.field_0x10 != nullptr) {
            character->actionStack.field_0x10.DeletePtr();
        }
        character->actionStack.field_0x10 = message;
        // --- CompleteTask child action ---
        void* mem2 = MemAlloc(0x38, 8);
        Action* complete = nullptr;
        if (mem2 != nullptr) {
            ComponentEntity* animalEntity = (*animal)->_entity.Get();
            CitizenComponent* self = *citizenIBP;
            complete = new (mem2) CompleteTask(task, &self, &animalEntity, 0);   // 0x1400c5da0
        }
        ::Object::InternalPtr<Action> pushed(complete);
        ActionStackPush(stack, &pushed);   // 0x1400c54e0
    }

    // --- the citizen's storage, for the CreateRawMaterial loop ---
    StorageComponent* storage = citizen->_entity->FindComponent<StorageComponent>();   // 0x140006270
    float fVar6 = kRdata_14017214c;  // g_fUVScale, 0x14017214c
    // StorageComponent::capacity is NOT a dead load: retail keeps it in R12D (1400bc048) and
    // passes it as arg7 of EVERY CreateRawMaterial_Action_CopyCtor below (stored to
    // RSP+0x30 at 1400bc0d8). R12D is only zeroed at 1400bc294, AFTER the loop.
    uint32_t capacity = static_cast<uint32_t>(storage->capacity);

    // --- CreateRawMaterial action per template ---
    // AnimalDescription::_refs: the raw-material ComponentDescription templates.
    ComponentDescription* const* templates = description->_refs;
    if (0 < description->_refCount) {
        int i = 0;
        do {
            void* mem = MemAlloc(0x58, 8);
            Action* create = nullptr;
            if (mem != nullptr) {
                // Disasm-verified arg setup (Ghidra's bbea0 decompile aliased the integer
                // args; the disassembly is ground truth):
                //   arg2 = the template, arg3 = fVar6 (XMM2/g_fUVScale float, ctor +0x4c)
                //   arg4 = &citizen, arg5 = &workplace
                //   arg6 = <unwritten by caller — uninitialized, ctor never reads it>
                //   arg7 = capacity (R12D), arg8 = createFlag (R13D), arg9 = 1
                //   arg10 = &workplace (a 2nd copy), arg11 = 0xc
                ComponentEntity* workplace2 = citizen->workplace.Get();
                CitizenComponent* self = citizen;
                ComponentEntity* workplace = citizen->workplace.Get();
                create = new (mem) CreateRawMaterial(   // 0x1400c71a0
                    templates[i],
                    fVar6,   // float arg3 -> XMM2 (ctor MOVSS to +0x4c); pass the value, not its bits
                    &self,
                    &workplace,
                    0 /*arg6 uninitialized — unused by ctor*/,
                    capacity /*arg7 = R12D*/, createFlag /*arg8 = R13D*/, true,
                    &workplace2, 0xc);
            }
            ::Object::InternalPtr<Action> pushed(create);
            ActionStackPush(stack, &pushed);   // 0x1400c54e0
            i = i + 1;
        } while (i < description->_refCount);
    }

    // --- RemoveAnimal action (raw 0x28-byte, no dedicated ctor) ---
    {
        RemoveAnimal* remove = static_cast<RemoveAnimal*>(MemAlloc(0x28, 8));
        if (remove != nullptr) {
            // ENDGAME B.3.8 ACT: the real ctor (RemoveAnimal.h) — retail inlines it here.
            new (remove) RemoveAnimal((*animal)->_entity.Get());
        }
        ::Object::InternalPtr<Action> pushed(remove);
        ActionStackPush(stack, &pushed);   // 0x1400c54e0
    }

    // --- PerformWork action ---
    {
        void* mem = MemAlloc(0x48, 8);
        Action* perform = nullptr;
        if (mem != nullptr) {
            CitizenComponent* self = *citizenIBP;
            System::Time time = {};
            RtSystem_Time_Ctor(&time, kRdata_140172194);   // 0x140172194
            perform = new (mem) PerformWork(&self,   // 0x1400c9720
                                            RdataF(kRdata_1401722a8),   // 0x1401722a8
                                            0xb /*Fast|ShowGroup*/, time._ticks,
                                            true, 2);
        }
        ::Object::InternalPtr<Action> pushed(perform);
        ActionStackPush(stack, &pushed);   // 0x1400c54e0
    }

    // --- MoveTo action toward the destination ---
    {
        void* mem = MemAlloc(0x480, 8);
        Action* moveTo = nullptr;
        if (mem != nullptr) {
            CharacterComponent* characterIBP =
                citizen->_entity->FindComponent<CharacterComponent>();   // 0x1400012d0
            moveTo = new (mem) MoveTo(&characterIBP, /*Slow*/1, 0,   // 0x1400c9170
                                      destination, PathBits::Walk, 0,
                                      kRdata_140172280);
        }
        ::Object::InternalPtr<Action> pushed(moveTo);
        ActionStackPush(stack, &pushed);   // 0x1400c54e0
    }

    // --- finalise on the animal's component ---
    // MUST be the animal (param_5), NOT the storage find's out slot: retail loads param_5's
    // home slot into R14 at 1400bc267, BEFORE that stack slot is recycled as a loop cursor
    // at 1400bc71f, and calls 0x140107260 with *R14 at 1400bc755. Ghidra's decompile prints
    // the storage-find result here because it reassigns its alias to param_5 mid-function.
    // Semantic proof it is the animal: 0x140107260 sets bit 0x100000 in the component's
    // _flags, and pasturework.cpp tests exactly that bit on the NatRes component to skip
    // animals already flagged for removal. A null guard here would be WRONG — this is the
    // only writer of that flag, so guarding would leave the culled animal unflagged and the
    // pasture would re-select it every tick: a silent livelock instead of a crash.
    (*animal)->FlagForRemoval();   // 0x140107260
}

// ===========================================================================
// B4 FORWARD: the C spellings other blocks' files still call, one line each.
// ===========================================================================

