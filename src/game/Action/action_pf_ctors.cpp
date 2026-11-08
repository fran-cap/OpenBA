// src/game/Action/action_pf_ctors.cpp
// Action-stack and entity/storage leaf sub-helpers (pathfinding/action closure batch).
//
// Faithful 1:1 ports of small exe helpers referenced from the Action closure.
// Each is transcribed literally from the Ghidra decompile (cross-checked against disassembly
// for intricate index/field math). Do NOT "improve" — none of this is exercised on the
// autostart path, so a wrong port is undetectable latent corruption.
//
// B.4 R0-EXE: the five action ctors that lived here and their by-address doors moved,
// text unchanged, to the classes' files: CompleteTask (c5da0) completetask.cpp,
// CreateRawMaterial (c71a0) createrawmaterial.cpp, MoveTo (c9170, with the file-local
// RdataF it alone used) moveto.cpp, PerformWork (c9720) performwork.cpp,
// SendComponentMessage (cb1d0) sendcomponentmessage.cpp. What stays has no settled
// class (B.4 inventory A: unattributed, or low confidence from the retail caller's class).
//
// Registration: RegisterPfCtorsBatch() in standalone/reg_pf_ctors.cpp.
//
// Sub-callees (called by name, B3R — body NOT duplicated):
//   0x1400032e0  array-grow (component_people)               — c54e0
//   0x1400c4f10  action-stack walk/clear (THIS file)         — c54e0
//   0x1400c3470  NotifyEntityChanged (action_hunt)           — 107260
//   0x1400012d0  Entity_FindComponent_Family (action_move)   — 107260
#include "Action/exe_bodies.h"
//   0x1400c5400  TaskListCleanup (work_base)                 — 107260
//   0x140006270  Entity_FindComponent_Storage (work_base)    — 116760 / 1167c0
//   0x14012d150  StorageComponent::SubtractItem (rawmat)     — 116760
//
// Engine helpers: Object::IsDerivedFromNullSafe, RtSystem_Time_Ctor
//   (??0Time@System@@QEAA@M@Z).

#include "Action/Action.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Work/exe_bodies.h"          // WorkBase_TaskListCleanup_c5400, WorkBase_FindComponent_6270
#include "Game/exe_constants.h"       // kRdata_*
#include <cstring>
#include <cstdint>
#include <new>
#include <cstdio>
#include <cstdlib>
#include "Component/CharacterDescription.h"   // ENDGAME B.3.8 CB3: CharacterDescription::TypeId()
#include "Component/CitizenComponent.h"   // ENDGAME B.3.8 CB3: CitizenComponent::TypeId()
#include "Component/InteractDescription.h"
#include "Component/RawMaterialDescription.h"
#include "Component/CharacterComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/InteractComponent.h"
#include "Component/StorageComponent.h"
#include "Entity/ComponentEntity.h"
#include "Action/PathSearch.h"   // RawArray (the drop-target lists)
#include "Game/Map.h"
// For the BANISHED_LIVE_STORAGE eat-message typeid heal in the
// SendComponentMessage copy-ctor (see Pf_SendComponentMessageCopyCtor_b1d0).
#include "Object/factory_registration.h"           // ::Object::GetTypeIdByName

// ============================================================
// FUN_1400c4f10 — action-stack: walk the chain from the entry the cursor holds; each node
// whose _status is set has it cleared and its Exit (slot 7) called with the stack; advance
// through Action::_next, writing each step back through the cursor.
// ============================================================
void ActionStackWalkClear(ActionStack* stack, ::Object::InternalPtr<Action>* cursor) {
    while (cursor->Get() != nullptr) {
        Action* node = cursor->Get();
        if (node->_status) {
            node->_status = false;
            node->Exit(stack);   // slot 7
        }
        *cursor = node->_next;
    }
}

// ============================================================
// FUN_1400c5150 — ~ActionStack (CharacterComponent's dtor 0x14002faa0 runs it on +0x48,
// 0x14002facb). DeletePtr (IAT 0x140151ab8) every non-null entry over the whole COUNT
// (+0x08, re-read each pass: 0x1400c5180..0x1400c51a0), then the non-null +0x10
// (0x1400c51a2..0x1400c51ac). The buffer free that follows (0x1400c51b3 MemFree(+0x00),
// +0x00 = +0x08 = 0) is `actions`' ~Array, run after this body.
// ============================================================
ActionStack::~ActionStack() {
    for (int i = 0; i < actions._size; ++i) {
        if (actions[i] != nullptr)
            actions[i].DeletePtr();
    }
    if (completionAction != nullptr)
        completionAction.DeletePtr();
}

// ============================================================
// FUN_1400c54e0 — action-stack push: grow the entry array when the running index is its
// last slot (0x10 on an empty array, else double), walk-clear the old top (0x1400c4f10),
// then store the new entry at ++current.
// ============================================================
void ActionStackPush(ActionStack* stack, const ::Object::InternalPtr<Action>* action) {
    int newCount = stack->actions._size;
    if (newCount == 0) {
        newCount = 0x10;
    } else {
        if (stack->current + 1 != newCount) goto LAB_550e;
        newCount = newCount * 2;
    }
    // FUN_1400032e0 == Collection::Array<InternalPtr>::SetSize (kSpec_PtrZero: stride 8,
    // zero tail) on the array header at the front of the stack (+0x00..+0x10).
    stack->actions.SetSize(newCount);
LAB_550e:
    if (-1 < stack->current) {
        ::Object::InternalPtr<Action> top = stack->actions[stack->current];
        ActionStackWalkClear(stack, &top);
    }
    stack->current = stack->current + 1;
    stack->actions[stack->current] = *action;
}

// ============================================================
// FUN_140107260 — set flag bit 0x100000 in the component's _flags, and if the low 16 bits
// changed refresh the entity's flags (0x1400c3470). Then find the entity's
// CharacterComponent (0x1400012d0); if its action stack has a running entry
// (current + 1 > 0), clear the stack (0x1400c5400) and run the component's Update
// (slot 9) with Time(0).
// ============================================================
void Component::FlagForRemoval() {
    uint32_t uVar1 = _flags;
    uint32_t uVar2 = uVar1 | 0x100000;
    _flags = uVar2;
    if ((((uVar2 ^ uVar1) & 0xffff) != 0) && (_entity != nullptr)) {
        _entity.Get()->RefreshEntityFlags();   // 0x1400c3470
    }
    CharacterComponent* character = _entity->FindComponent<CharacterComponent>();   // 0x1400012d0
    if (0 < character->actionStack.current + 1) {
        character->actionStack.Clear();   // 0x1400c5400
        System::Time time = {};
        RtSystem_Time_Ctor(&time, 0.0f);
        Update(&time);   // slot 9
    }
}

// ============================================================
// FUN_140123070 — the node count of an interact tree: the component's InteractDescription
// node array `kind` (0 use, 1 build, 2 create: the three {data, count, cap} records at
// +0x28/+0x38/+0x48, the count read at +0x30 + kind * 0x10) plus, recursively, every
// sub-location's count.
// ============================================================
int InteractComponent::NodeCount(int kind) const {
    Description* desc = _description;
    const InteractDescription* interact =
        ::Object::IsDerivedFromNullSafe(desc, InteractDescription::TypeId())
            ? static_cast<const InteractDescription*>(desc) : nullptr;
    // [STANDALONE GUARD] interact is null when the description is not an
    // InteractDescription OR the type-id global (0x1401c7938) is unfilled/unregistered in
    // standalone. The real game always resolves it; guard so the seed read can't deref
    // null+0x30 (the counter simply contributes 0). Surfaced via the BANISHED_FORCE_WORK
    // harness (Leisure/Work anchor a citizen on an inert workplace building's position comp).
    // B6 (14d G51): retail has no test (0x1401230a6 CMOVNZ R8,RBX over R8 = 0, then
    // 0x1401230ad MOV EBP,[R8+RAX*8]); KEPT under D4(a) because the comment names the
    // BANISHED_FORCE_WORK harness route.
    int total = interact ? (&interact->_useNodes)[kind].count : 0;
    for (int i = 0; i < useLocations._size; ++i) {
        total = total + useLocations[i].Get()->NodeCount(kind);
    }
    return total;
}

// ============================================================
// FUN_14012c560 — does the storage take this material: find the material template's
// RawMaterialDescription (Type() compare, then IsDerivedFrom) and test its _flags against
// the storage's rawMaterialFlags. If no match, `found` stays null and _flags reads from
// address 0x5c — faithful to the original.
// ============================================================
bool StorageComponent::AcceptsMaterial(const ComponentDescription* material) const {
    const RawMaterialDescription* found = nullptr;
    for (int i = 0; i < material->_subDescCount; ++i) {
        Description* child = material->_subDescs[i];
        if (child->Type() == RawMaterialDescription::TypeId()) {
            Description* match = material->_subDescs[i];
            if (::Object::IsDerivedFromNullSafe(match, RawMaterialDescription::TypeId()))
                found = static_cast<const RawMaterialDescription*>(match);
            break;
        }
    }
    return (rawMaterialFlags & found->_flags) != 0;
}

// ============================================================
// FUN_140116760 — storage drop-off: take `count` of `material` out of the owning entity's
// StorageComponent (0x140006270, then 0x14012d150).
// ============================================================
void CitizenComponent::StorageDropOff(ComponentDescription* material, int count) {
    StorageComponent* storage = _entity->FindComponent<StorageComponent>();   // 0x140006270
    storage->SubtractItem(material, count);   // 0x14012d150
}

// ============================================================
// FUN_1401167c0 — the free count of `material` in the owning entity's StorageComponent:
// the matching item's _count - _reserve, 0 if none.
// ============================================================
int CitizenComponent::StorageCountDelta(const ComponentDescription* material) {
    StorageComponent* storage = _entity->FindComponent<StorageComponent>();   // 0x140006270
    for (int i = 0; i < storage->storageSlots._size; ++i) {
        const StorageComponent::Item& item = storage->storageSlots[i];
        if (item._resource == material) {
            return item._count - item._reserve;
        }
    }
    return 0;
}

// ===========================================================================
// B4 FORWARD: the C spellings other blocks' files still call, one line each.
// ===========================================================================

// B4 FORWARD: outside callers in ~20 files (Component, Dialog, Entity, Game, Player, Work)

// ---- action_pf_roots_route.cpp's C spellings (kept here: that file's reinterpret column
// has no room for them).



