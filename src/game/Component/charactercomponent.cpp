// src/game/Component/charactercomponent.cpp
// Clean reimplementation of CharacterComponent::Update from Application-x64.exe.
//
// CharacterComponent (0x80 bytes) manages character animation state.
// Update checks if a character index (currentAction) is valid (>= 0),
// looks up a character object from an array at actions, and calls
// Character_UpdateAnimation to process the character's animation state.
//
// Vtable at 0x140162370. One non-trivial override:
//   Update (0x140109480) — conditional animation delegate
//
// Decompiled from Application-x64.exe using Ghidra.

#include "options.h"
#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Action/Action.h"     // B.3.x W0: Action::Enter by name
#include "Component/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Component/CharacterComponent.h"
#include "Component/CharacterDescription.h"
#include "Component/CharacterGrid.h"
#include "Component/ModelComponent.h"
#include "Component/PickingComponent.h"
#include "Entity/ComponentEntity.h"

// Character_UpdateAnimation(arrayBase*, InternalBasePtr*, Time)
// Located at 0x1400c4f80 in Application-x64.exe; Character_UpdateAnimation_Reimpl below,
// called directly.
// Iterates a linked list of character objects from the InternalBasePtr,
// calling each object's vtable[6] (Draw/UpdateAnimation) method.
// RCX = arrayBase (this+0x48), RDX = &localPtr (InternalBasePtr*), R8 = Time (int64)

// ============================================================
// CharacterComponent::Update (vtable slot [9], 0x140109480)
// ============================================================
// If currentAction >= 0, look up object from *(actions)[currentAction] and call
// Character_UpdateAnimation with (&this->actions, &obj, deltaTime).
//
// Original disassembly:
//   SUB RSP,0x28
//   MOVSXD RAX,[RCX+0x60]    ; sign-extend currentAction
//   ADD RCX,0x48              ; arrayBase = this+0x48
//   TEST EAX,EAX
//   JS skip                   ; skip if index < 0
//   MOV RDX,[RCX]             ; objArray = *(this+0x48)
//   LEA RDX,[RDX+RAX*8]      ; &objArray[index]
//   MOV R8,R9                 ; deltaTime (forwarded from caller)
//   CALL Character_UpdateAnimation
// skip:
//   ADD RSP,0x28
//   RET

// ============================================================
// Reimpl of Character_UpdateAnimation (0x1400c4f80) — Component reg wave "people"
// Walks the linked list of character objects starting at *param_2; for each node
// whose enabled-flag (+0x18) is set, invokes vtable slot +0x30 (Draw/UpdateAnimation)
// with (node, arrayBase, deltaTime); advances via node+0x10. Non-static for registration.
// ============================================================
void ActionStack::UpdateAnimation(Action** ptrRef, const System::Time* deltaTime) {
    Action* node = *ptrRef;
    while (node != nullptr) {
        if ((*ptrRef)->_status) {
            // Action slot 6 (the list is the action stack's node chain: +0x18 status,
            // +0x10 next). `deltaTime` is the Time* CharacterComponent::Update received
            // (0x1401094a8 `MOV R8,RDX`), passed through as retail's R8.
            (*ptrRef)->Enter(this, deltaTime);   // slot 6
        }
        node = (*ptrRef)->_next.Get();   // Action::_next (+0x10), the next node
        *ptrRef = node;
    }
}

// ===== Standalone factory (Foundation D — component instantiation) =====
#include "Object/factory_registration.h"

// ------------------------------------------------------------------
// Phase 3 / Keystone D — CharacterComponent Startup-only live-wiring (Brick 4a).
//
// The citizen position/geometry pipeline: CharacterComponent::Startup
// (0x140109a30, slot +0x20) finds the sibling ModelComponent (+0x68, whose
// transform at +0x158 IS the citizen world position) + PickingComponent (+0x70)
// + adopts the CharacterDescription (+0x78). The Brick-2 CanFindHousing guard
// bails while CharacterComponent+0x68 is null; this brick populates it so the
// CitizenAI loop can actually evaluate spatial decisions (housing).
//
// Startup-ONLY (the heavy move/anim tick + action executor at 0x1401094c0 stays
// inert — that is Brick 4b): we set +0x60 = -1 (no actions) so even a future
// move-tick would skip the executor on the empty stack. Default (env off) =>
// inert => byte-unchanged. Finds are self-contained over the class's TypeId()
// (ENDGAME B.3.9 TR: was a by-name lookup of the same record).
// ------------------------------------------------------------------
// The tree's null-entity, bad-id and null-slot guards are its own (retail's
// FindComponent<T> instances have none); kept, not extended.
static Component* CharComp_FindSiblingByTypeId(ComponentEntity* ownerEntity, uint32_t want) {
    if (!ownerEntity) return nullptr;
    if (want == 0 || want == 0xFFFFFFFF) return nullptr;
    int count = ownerEntity->_components._size;
    for (int i = 0; i < count; i++) {
        Component* comp = ownerEntity->_components[i].Get();
        if (!comp) continue;
        uint32_t tid = comp->Type();
        if (tid == want)
            return ::Object::IsDerivedFromNullSafe(comp, want) ? comp : nullptr;
    }
    return nullptr;
}

// Char spatial-hash manager registrar (charmanager.cpp) — allocates the char render-index.

// ------------------------------------------------------------------
// Brick 4b — per-tick move/anim tick + action executor (0x1401094c0).
// The Update slot (+0x48 / vtable slot 9) routes to the move-tick, which runs the
// action executor over the stack at CharacterComponent+0x48 when it is non-empty
// (+0x60 >= 0), then early-returns when the char render-index (+0x40) is < 0 —
// which it is until OnAdded registers the character, so the inert-ModelComponent
// transform tail is skipped (crash-safe). With no actions pushed yet the move-tick
// is a per-frame no-op; it begins executing actions once the CitizenAI state
// handlers push them.
//
// ------------------------------------------------------------------
extern "C" void CharacterComponent_MoveTick_1094c0(CharacterComponent* self, const System::Time* time);   // citizenai_executor.cpp

// RETAIL SLOT WIRING (ENDGAME B.3.8 CIT: the class's compiler vtable carries it —
// CharacterComponent.h; the members are at the bottom of this file).
// Retail vtable 0x140162370:
//   slot 8 (+0x40) = 0x1401094c0 move-tick — dispatched EVERY FRAME by the
//                    EntityList update-TREE walk (component flag bit 2);
//   slot 9 (+0x48) = 0x140109480 Update — dispatched at the THROTTLED 5-bucket
//                    round-robin rate (component flag bit 3, processCount quotas).
// The original port installed the move-tick at slot 9, so every character
// integrated movement at the BUCKET rate: citizens at roughly half speed, and
// Fifth-priority animals (quota 2/frame, ~120 animals) at ~1/60 speed —
// visually frozen. Slot 8 full-rate dispatch is the fix (2026-07-30).
// Slot 9's faithful body (CharacterComponent_Update -> action +0x30 tick) is
// GRADUATED (2026-08-18): BANISHED_LIVE_CHARACTER_UPDATE9 is in the default-on table and
// the env var is now a kill-switch (=0). It is the ONLY dispatcher of an action's
// +0x30 slot, so while it was opt-in every action's Enter was dead code — most
// visibly HuntAnimal::Enter, which is the kill step: hunters chased deer forever and
// the hunting cabin's production panel read 0. A/B evidence is in the default-on table.
// ------------------------------------------------------------------
// Slot 7 (+0x38) — CharacterComponent::OnRemove (retail 0x140109980), the RELEASE half
// of the char-index allocation OnAdded (slot 6) makes. Retail body is exactly:
//     FUN_1400c5400(self + 0x48);                       // tear down the action stack
//     CharMgr_impl_8560(*(gw + 0x88), *(int*)(self+0x40));  // unlink from its cell list
//     *(int*)(self + 0x40) = -1;
//
// Without it we allocate a char-index for every character and never give one back, so a
// dead character's slot keeps a dangling InternalBasePtr AND stays linked in its spatial
// cell. Nothing walked those lists until the action +0x30 tick went live (both callers of
// the proximity query 0x140092260 are +0x30 slots), which is why this was invisible: the
// query then reads the stale slot's entity +0x68 -> +0x160 and faults on freed memory.
// Gated with the same flag as the allocation so the two halves can never be on separately.
// ------------------------------------------------------------------

// ============================================================================
// ENDGAME B.3.8 CIT — CharacterComponent as a REAL class (CharacterComponent.h).
//
// Before: a hand-rolled 48-slot CharacterComponent_LiveVtable filled at every
// create (slots 3 Serialize, 4 Startup, 7 OnRemove, 8 move-tick, 9 Update),
// Inert_ScalarDtor in slot 0. After: the compiler's 16-slot vtable and a real
// ctor/dtor chain (B38_RULES D1, D2).
// ============================================================================
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"     // ModelComponent::TypeId() (ENDGAME B.3.9 TR)
#include "Component/PickingComponent.h"   // PickingComponent::TypeId() (ENDGAME B.3.9 TR)
#include "object/class_vptr.h"   // Object::ClassVptr (the ctor records the vptr hint)


// 0x140109d20. Component() is the CALL 0x1401188b0; the 0x140162370 stamp is
// the compiler's; flags |= 0x8f.
// +0x40 = -1 (char index, 0x140109d59) and +0x60 = -1 (action-stack top,
// 0x140109d74) are the ctor's own stores; Startup still re-writes both, as it
// did when the inert factory zero-filled them.
CharacterComponent::CharacterComponent()
    : field_0x28(false), _pad29{}, field_0x2C(), field_0x38(0), field_0x3C(0),
      characterIndex(-1), _pad44{},
      actionStack(),
      modelComponent(nullptr), pickingComponent(nullptr), characterDescription(nullptr) {
    OrFlagsInCtor(0x8f);
    Object::ClassVptr<CharacterComponent>::Record(this);   // NOT RETAIL: the vptr hint (object/class_vptr.h)
}


// [4] 0x140109a30 — sibling finds + description adoption: +0x68 = the
// ModelComponent (0x140003f60), +0x70 = the PickingComponent (0x140026a30), +0x78 =
// the description through IsDerivedFrom CharacterDescription. The char-index
// allocation is OnAdded's (slot 6, below), as in retail.
void CharacterComponent::Startup() {
    // ctor (0x140109d20) sentinels: +0x40 grid-index = -1, +0x60 action-stack top
    // = -1 (EMPTY stack — a freshly spawned citizen has no queued actions). This is
    // faithful AND required for Brick 3: the CitizenAI Phase-8 check reads
    // CharacterComponent+0x60 + 1 > 0 as "has actions => busy this tick"; with the
    // empty -1 it is 0 (not >0), so Phase 8 runs the state-selection path
    // (CheckEducation/CheckAge/TransitionState) to assign a state. (Brick 3 ports
    // that path + guards it crash-safe; the move-tick/executor is still inert, so
    // -1 never drives the executor on an empty stack.)
    characterIndex = -1;
    actionStack.current = -1;

    // Startup (0x140109a30): find sibling components + adopt the description.
    ComponentEntity* owner = _entity.Get();
    if (!owner) return;

    // +0x68 = ModelComponent (geometry; +0x158 = world position).
    modelComponent = static_cast<ModelComponent*>(CharComp_FindSiblingByTypeId(owner, ModelComponent::TypeId()));
    // +0x70 = PickingComponent.
    pickingComponent = static_cast<PickingComponent*>(CharComp_FindSiblingByTypeId(owner, PickingComponent::TypeId()));
    // +0x78 = CharacterDescription (trust comp+0x18).
    Description* description = _description;
    characterDescription = static_cast<CharacterDescription*>(description);
}

// [6] 0x1401099c0 (98 B) — dispatched by ComponentEntity::OnAddedPass at
// EntityList::RegisterEntity time (component flag bit 0; the ctor ORs 0x8f).
// Facing init: +0x3C = the ModelComponent's yaw (+0x168), then wrap this in an
// InternalBasePtr (import 0x140152a90) and register it with the character grid
// (GameUpdateLoop +0x88, 0x1400183f0): +0x40 = the char index the move-tick
// needs before it writes the model transform. Retail reads +0x68, the singleton
// and the grid unguarded; Startup has seated +0x68 before the pass runs. The
// load route re-registers in Persist (charactercomponent_serialize.cpp).
void CharacterComponent::OnAdded() {
    ModelComponent* model = modelComponent.Get();
    field_0x3C = model->yaw;
    Object::InternalPtr<CharacterComponent> self(this);
    CharacterGrid* charMgr = GameUpdateLoop::_singleton->m_characterGrid;
    characterIndex = static_cast<int32_t>(charMgr->Register(&self));
    // BANISHED_MOVETICK_TRACE (options.h): one line per char-index allocation.
    if (Opt().movetickTrace) {
        fprintf(stderr, "[CharIdx] idx=%d char=%p entity=%p model=%p\n",
                characterIndex, static_cast<void*>(this),
                static_cast<void*>(_entity.Get()), static_cast<void*>(model));
    }
}

// [7] 0x140109980 — release the char index (the other half of the allocation).
void CharacterComponent::OnRemove() {
    actionStack.Clear();   // 0x1400c5400, the action-stack clear

    // GameUpdateLoop::m_characterGrid (+0x88)
    CharacterGrid* charMgr = GameUpdateLoop::_singleton
                                 ? GameUpdateLoop::_singleton->m_characterGrid
                                 : nullptr;
    int idx = characterIndex;
    // DEVIATION (ours, not retail's): retail passes +0x40 through unguarded, and
    // impl_8560 writes `slots + idx*0x18` and pushes idx onto the free list without
    // checking it -- so a -1 corrupts memory ahead of the slot array. Retail can do that
    // because OnAdded (slot 6) always allocated an index; since OnAdded is ported (not
    // folded into Startup) the -1 is left only for a character that was never added
    // to an EntityList. The guard is kept for that case; a character that never
    // registered has nothing to release.
    if (charMgr && idx >= 0) charMgr->Release(idx);

    characterIndex = -1;
}

// [8] 0x1401094c0 — the move-tick, EVERY FRAME from the EntityList tree walk
// (flag bit 2; 0x1400c2e5d passes the update's time pointer through).
void CharacterComponent::Tick(const System::Time* time) {
    CharacterComponent_MoveTick_1094c0(this, time);
}

// [9] 0x140109480 — the bucket-rate Update. Retail forwards the Time operand RAW:
// slot 9 does MOV R8,RDX at 0x1401094a8 and Character_UpdateAnimation hands that
// same value to the action's +0x30 as param_3, so it is passed through, not
// dereferenced.
void CharacterComponent::Update(const System::Time* time) {
    if (actionStack.current >= 0) {
        // actions is the array of action pointers, currentAction the index into it
        Action* localPtr = actionStack.actions[actionStack.current].Get();
        actionStack.UpdateAnimation(&localPtr, time);
    }
}

// create 0x14002f960: MemAlloc(0x80, 8), then the ctor.
::Object::Base* CharacterComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CharacterComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CharacterComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* CharacterComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CharacterComponent());
}

// [PREFETCH] the ctor records the vptr (Object::ClassVptr<CharacterComponent>): the staged walk
// prefetch (entitylist.cpp, kWalkPrefetch == 2) tells this class by it.

DEFINE_EXE_TYPE(CharacterComponent, "Component")
