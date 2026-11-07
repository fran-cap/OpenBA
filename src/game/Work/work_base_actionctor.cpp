// src/game/Work/work_base_actionctor.cpp
// Work action/animation constructors (Wave 2 — Work action-ctor sub-batch).
//
// These exe helpers build the action sequence for a citizen work task: each
// allocates a chain of Action objects (SendComponentMessage / CompleteTask /
// ShowTool / PerformWork / MoveTo / *NaturalResource / inline PerformWork or
#include "Work/exe_bodies.h"
// PlayAnimation) and appends them to the citizen's action stack (CitizenComponent
// +0x278 = its CharacterComponent, whose +0x48 is the stack; the SendComponentMessage
// goes into CharacterComponent::field_0x58). Called by name from the Work .cpp call
// sites (cleararea / foresterwork / herbalistwork / pasturework / tendarea /
// plantorchard / fishingwork / pickup).
//
// Each function is an instruction-faithful port of the decompiled exe helper; do
// NOT "improve" behavior. Every exe sub-callee these ctors invoke is one of our own
// bodies, called by name (B3R; the table is below).
//
// LEFT (every body): the callers in other blocks hand the citizen / target references
// as void*, and the action ctors (ACT's C bodies) take their entity references as
// uint64_t* / uintptr_t and return void*; each call keeps the one conversion its
// signature asks for. CharacterComponent declares its +0x58 InternalPtr as Action*
// (COMP-a), so DeletePtr is reached through the base view.
//

#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "system/MemoryAllocator.h"
#include "Action/Action.h"
#include "Action/PerformWork.h"   // ENDGAME B.3.8 CB3
#include "Action/PickupRawMaterial.h"   // ENDGAME B.3.8 CB3
#include "Action/PlayAnimation.h"   // ENDGAME B.3.8 CB3
#include "Action/SendComponentMessage.h"     // B4 P2 WORK-3: the action ctors by name
#include "Action/CompleteTask.h"
#include "Action/ShowTool.h"
#include "Action/RemoveNaturalResource.h"
#include "Action/AnimateNaturalResource.h"
#include "Action/CreateNaturalResource.h"
#include "Action/MoveTo.h"
#include "math/Math.h"
#include "Action/exe_bodies.h"      // B3R: the action ctor bodies called by name
#include "Component/exe_bodies.h"   // B3R: (unused since B6 block D: the QueryCount callers call the member)
#include "Game/exe_constants.h"     // B3R: kRdata_*
#include "system/Value.h"           // B3R: RtSystem_Time_Ctor
#include <cstring>
#include "Component/CitizenAIComponent.h"   // CitizenAIComponent::TypeId() (the message target)
#include "Component/CitizenComponent.h"
#include "Component/NaturalResourceComponent.h"   // WorkAct_PerformWorkCtor_c1eb0 target (0x1400c1eb0)
#include "Component/CharacterComponent.h"
#include "Component/StorageComponent.h"
#include "Entity/ComponentEntity.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/NaturalResourceDescription.h"   // ENDGAME B.3.9 D5

// ============================================================
// Sub-callees, called by name (B3R)
// ============================================================
// Engine base helpers (InternalBasePtr ctor/deleteptr, IsDerivedFrom) are the real
// Object classes. The exe sub-callees are our own bodies, declared in the module
// exe_bodies.h headers:
//   0x1400c29a0 Entity_impl_29a0 (heading, XMM0)   ActChild_Entity29a0_c29a0
//   0x1400c5400 TaskListCleanup                    WorkBase_TaskListCleanup_c5400
//   0x1400c54e0 TaskListAppend                     Pf_ActStackPush_54e0
//   0x1400c3470 Work-target detach (c1eb0 tail)    ActHunt_NotifyEntityChanged_c3470
//   0x1400012d0 Entity_FindComponent_Family        ActMv_FindComponentFamily_12d0
//   0x1400cb1d0 SendComponentMessage copy-ctor     Pf_SendComponentMessageCopyCtor_b1d0
//   0x1400c5da0 CompleteTask copy-ctor             Pf_CompleteTaskCopyCtor_5da0
//   0x140042890 ShowTool ctor                      ActChild_ShowToolCtor_42890
//   0x1400cadf0 RemoveNaturalResource copy-ctor    ActChild_RemoveNRCopyCtor_cadf0
//   0x1400c57d0 AnimateNaturalResource copy-ctor   ActChild_AnimateNRCopyCtor_c57d0
//   0x1400c6a20 CreateNaturalResource copy-ctor    ActChild_CreateNRCopyCtor_c6a20
//   0x1400c9720 PerformWork_Action_Ctor7           Pf_PerformWorkCtor7_9720
//               (RCX=self, RDX=entity, XMM2=heading, R9D=actionType, [stack]=time,
//                byte p6, int p7 -- verified via disassembly)
//   0x1400c9170 MoveTo ctor                        Pf_MoveToCtor_9170
//   0x1400c9b70 PickupRawMaterial copy-ctor        WorkAct_PickupRawMaterialCopyCtor_c9b70 (below)
//   0x140006270 FindComponent<StorageComponent>
//   0x14012cf10 StorageComponent::QueryCount       (the member)
//   slot 0x140152b60 MemoryAllocator::MemAlloc     System::MemoryAllocator::MemAlloc
//   slot 0x140152b00 System::Time::Time(float)     RtSystem_Time_Ctor
// Math::Vector3 Normalize / Heading are direct Math::Vector3 member calls (B.3.1 step 2).
//
// .rdata floats (src/game/Game/exe_constants.h):
//   0x140172124 = 0.75f  (c0ad0 PerformWork time)
//   0x140172280 = -1.0f  (MoveTo speed sentinel)
//   0x140172100 = 0.25f  (PlayAnimation time)
//   0x1401721b0 = 10.0f  (grazing PerformWork time)
//   0x140172178 = 2.1f   (plant PerformWork time)
//   0x1401722a8 = -1000  (plant PerformWork heading const; stored as a bit pattern)
// 0x1401c7b68 = NaturalResourceDescription (ENDGAME B.3.9 D5: read directly below).

static inline float WA_RdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }

// 0x1400c54e0 ActionStackPush (Action.h) on CharacterComponent::actionStack (+0x48).
static inline void WA_Append(ActionStack* list, const ::Object::InternalPtr<Action>* ibp) {
    ActionStackPush(list, ibp);
}

static inline float WA_C_172124() { return kRdata_140172124; }
static inline float WA_MoveSpeed() { return kRdata_140172280; }

// c0ad0 repeats this exact idiom three times (@0x1400c0d06, @0x1400c0f04, @0x1400c0fdc):
//   CheckPtr(citizenIBP); e = *citizenIBP; CheckPtr(e+0x10);
//   Entity_FindComponent_Storage(*(e+0x10), &ibp); CheckPtr(&ibp);
//   StorageComponent_QueryCount(ibp)
// i.e. "how much is the citizen carrying right now". Re-evaluated each time in the
// original (not cached), so this mirrors that.
static int WA_CitizenCarryCount(CitizenComponent* const* citizenRef) {
    CitizenComponent* e = *citizenRef;
    StorageComponent* storage = e->_entity->FindComponent<StorageComponent>();   // 0x140006270
    return storage->QueryCount();
}

// ============================================================
// PerformWork_Action_Ctor3 (0x1400c1eb0)
// ============================================================
// Build the work-action chain for a resource-clearing / performwork task and store it
// on the citizen's action stack.
//   param_1 = citizen InternalBasePtr*
//   param_2 = workId (u32)
//   param_3 = target InternalBasePtr* (a component: its entity and its
//             NaturalResourceDescription)
//   param_4 = float* worldPos
//   param_5 = int (consumed/recomputed internally)
//   param_6 = byte (RemoveNaturalResource flag)
void WorkAct_PerformWorkCtor_c1eb0(CitizenComponent* const* citizenRef, uint32_t workId,
                                   NaturalResourceComponent* const* targetRef,
                                   float* worldPos, int /*zero*/, uint8_t zeroByte) {

    // --- derive the NaturalResourceDescription off the target (param_3) ---
    // IsDerivedFrom(+0x18, cell 0x1401c7b68), CMOVNZ to 0; every later read through it
    // (+0x80 at 0x1400c228d / 0x1400c22f6 / 0x1400c23d3, +0x64, +0x68, +0x70) has no test
    // (B6 14d G23).
    Description* targetComp = (*targetRef)->_description;
    NaturalResourceDescription* derivedTarget = nullptr;
    if (targetComp->IsDerivedFrom(NaturalResourceDescription::TypeId()))
        derivedTarget = static_cast<NaturalResourceDescription*>(targetComp);

    ComponentEntity* targetInner = (*targetRef)->_entity.Get();

    // heading = Entity_impl_29a0(worldPos, &{target inner}) (XMM0 float)
    ComponentEntity* headingArg = targetInner;
    float heading = 0.0f;
    heading = HeadingToward(worldPos, &headingArg);

    // the action stack = character + 0x48
    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* list = &character->actionStack;
    list->Clear();

    void* obj;
    ::Object::InternalPtr<Action> ibpA;
    ::Object::InternalPtr<Action> ibpB;

    // --- SendComponentMessage action → stored at character+0x58 (replaces prior, DeletePtr) ---
    obj = System::MemoryAllocator::MemAlloc(0x40, 8);
    Action* sm = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        sm = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0);
    }
    ibpA = sm;
    if (character->actionStack.field_0x10 != nullptr)
        character->actionStack.field_0x10.DeletePtr();
    character->actionStack.field_0x10 = ibpA;

    // --- CompleteTask action ---
    obj = System::MemoryAllocator::MemAlloc(0x38, 8);
    Action* ct = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        ComponentEntity* targetInnerSlot = targetInner;
        ct = new (obj) CompleteTask(workId, &entityInner,
                                          &targetInnerSlot, 0);
    }
    ibpB = ct;
    WA_Append(list, &ibpB);

    // --- ShowTool (~Hoe = -2) action ---
    obj = System::MemoryAllocator::MemAlloc(0x30, 8);
    Action* st = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        st = new (obj) ShowTool(&entityInner, ~0 /* ~Hoe = -1 (Hoe==0); disasm OR R8D,0xFFFFFFFF */);
    }
    ibpA = st;
    WA_Append(list, &ibpA);

    // --- RemoveNaturalResource action ---
    // ABI (Ghidra-verified vs RemoveNaturalResource_Action_CopyCtor @0x1400cadf0 and its
    // caller c1eb0): the ctor stores *param_2 → self+0x28 (resourceEntity/TREE) and
    // *param_3 → self+0x20 (citizenEntity). c1eb0 calls it as
    //   RemoveNaturalResource_Action_CopyCtor(self, &tree(lVar2), &citizen(*param_1), flag)
    // i.e. param_2 = &TREE, param_3 = &CITIZEN. A prior port swapped these (passed citizen
    // as param_2 → +0x28), which made RemoveNaturalResource::Main FindNRComp(citizen) return
    // null (citizen has no NR component) and the organic produce got skipped. Pass tree first.
    obj = System::MemoryAllocator::MemAlloc(0x38, 8);
    Action* rnr = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        ComponentEntity* targetInnerSlot = targetInner;
        rnr = new (obj) RemoveNaturalResource(&targetInnerSlot, &entityInner, zeroByte);   // 0x1400cadf0
    }
    ibpB = rnr;
    WA_Append(list, &ibpB);

    // --- compute performWork param_5 from the description's +0x80 ---
    int pwParam = 8;
    if (derivedTarget->_u80 < 0xb)
        pwParam = static_cast<int>(derivedTarget->_u80) + 9;

    // --- if the description's +0x64 (byte) is set: inline PerformWork + two AnimateNaturalResource ---
    if (derivedTarget->_b64) {
        void* inlineObj = System::MemoryAllocator::MemAlloc(0x48, 8);
        Action* pw = nullptr;
        if (inlineObj) {
            uint32_t resKind = derivedTarget->_u80;
            int64_t res70   = derivedTarget->_time70._ticks;
            // the inlined PerformWork ctor 0x1400c9720 (useTool = 0, +0x44 = resKind): the real ctor.
            pw = new (inlineObj) PerformWork(citizenRef, heading, static_cast<uint32_t>(pwParam),
                                             res70, 0, static_cast<int>(resKind));
        }
        ibpA = pw;
        WA_Append(list, &ibpA);

        // AnimateNaturalResource(targetInner, 1, 0)
        obj = System::MemoryAllocator::MemAlloc(0x30, 8);
        Action* anr1 = nullptr;
        if (obj) {
            ComponentEntity* targetInnerSlot = targetInner;
            anr1 = new (obj) AnimateNaturalResource(&targetInnerSlot, 1, 0);   // 0x1400c57d0
        }
        ibpA = anr1;
        WA_Append(list, &ibpA);

        // AnimateNaturalResource(targetInner, 0, 1)
        obj = System::MemoryAllocator::MemAlloc(0x30, 8);
        Action* anr2 = nullptr;
        if (obj) {
            ComponentEntity* targetInnerSlot = targetInner;
            anr2 = new (obj) AnimateNaturalResource(&targetInnerSlot, 0, 1);   // 0x1400c57d0
        }
        ibpA = anr2;
        WA_Append(list, &ibpA);
    }

    // --- PerformWork_Ctor7: heading(XMM2)=heading, actionType(R9)=pwParam,
    //     time=description+0x68, p6=1, p7=description+0x80 (resKind) ---
    obj = System::MemoryAllocator::MemAlloc(0x48, 8);
    Action* pw7 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        int resKind  = static_cast<int>(derivedTarget->_u80);
        int64_t time = derivedTarget->_time68._ticks;
        pw7 = new (obj) PerformWork(&entityInner, heading, pwParam, time, 1, resKind);
    }
    ibpA = pw7;
    WA_Append(list, &ibpA);

    // --- MoveTo (path type from the description's +0x80 switch) ---
    obj = System::MemoryAllocator::MemAlloc(0x480, 8);
    Action* mv = nullptr;
    if (obj) {
        int pathType = 1; // Slow default (disasm: EBX=1 at 0x1400c2123)
        switch (derivedTarget->_u80) {
            case 0: case 1: case 4: case 5: case 6: case 7: pathType = 2; break; // Fast
            case 2: case 3: pathType = 3; break;                                  // Fastest
            case 0xffffffff: pathType = 1; break;                                 // Slow
            default: pwParam = 1; break;                                          // resKind>=8: pathType stays 1
        }
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        CharacterComponent* family = entityInner->FindComponent<CharacterComponent>();   // 0x1400012d0
        float speed = kRdata_140172280;
        mv = new (obj) MoveTo(&family, pathType, 0, worldPos, PathBits::Walk, 0, speed);
    }
    ibpA = mv;
    WA_Append(list, &ibpA);

    // --- ShowTool (the description's +0x80 tool type) action ---
    obj = System::MemoryAllocator::MemAlloc(0x30, 8);
    Action* st2 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        int toolType = static_cast<int>(derivedTarget->_u80);
        st2 = new (obj) ShowTool(&entityInner, toolType);
    }
    ibpA = st2;
    WA_Append(list, &ibpA);

    // --- tail: set bit 0x14 (0x100000) on the target's flags; on newly-set bit
    //     (low 16 changed) notify its entity via FUN_1400c3470 if it has one ---
    Component* targetObj = *targetRef;
    uint32_t oldFlags = targetObj->_flags;
    uint32_t newFlags = oldFlags | 0x100000u;
    targetObj->_flags = newFlags;
    if (static_cast<uint16_t>(oldFlags ^ newFlags) != 0 &&
        targetObj->_entity != nullptr) {
        ComponentEntity* detach = targetObj->_entity.Get();
        detach->RefreshEntityFlags();   // 0x1400c3470
    }
}

// ============================================================
// PickupRawMaterial_Action_CopyCtor (0x1400c9b70)
// ============================================================
// Trivial field-copy ctor for the PickupRawMaterial action.
// Disassembly-verified: RCX=self, RDX=p2, R8=p3.
//   Object::Internal::Internal(self)
//   self+0x00 = vtable_Action (0x14015faa0)   [immediately overwritten]
//   self+0x10 = 0 ; self+0x18 = 0
//   self+0x00 = vtable_PickupRawMaterial (0x14015ff10)
//   self+0x20 = 0 then *p2   (citizen inner)
//   self+0x28 = 0 then *p3   (target/pile inner)
// Returns self (RAX).
// B8: PickupRawMaterial's two-argument constructor (its one caller is c0ad0 below, which
// placement-news it on the MemAlloc it tested). 0x1400c9bbe `MOV RAX,[RBX]` and 0x1400c9bc9
// `MOV RAX,[RDI]` read both slots with no test (row 1181).
PickupRawMaterial::PickupRawMaterial(CitizenComponent* const* p2, ComponentEntity* const* p3)
    : _field20(*p2),
      _field28(*p3) {
}

// ============================================================
// PerformWork_Action_Ctor9 (0x1400c0ad0) — THE pile-haul action builder
// ============================================================
// Called from Pickup::Execute (0x1400f2f00) once per accepted candidate. Builds the
// citizen's LIFO action stack, so the pushed order below EXECUTES IN REVERSE:
//   pushed:   ShowTool(~Hoe) → [SendComponentMessage → character+0x58, not pushed]
//             → PerformWork(kind 7) → [ShowTool(~Axe) if carrying nothing]
//             → PickupRawMaterial → CompleteTask → PerformWork(kind 6) → MoveTo
//             → [ShowTool(~Axe) if carrying something]
//   executes: [ShowTool] → MoveTo(walk to pile) → PerformWork(6) → CompleteTask
//             → PickupRawMaterial(pick it up) → [ShowTool] → PerformWork(7) → ShowTool(~Hoe)
//
//   param_1 = citizen InternalBasePtr*
//   param_2 = workId / resource type id (u32) → CompleteTask
//   param_3 = target (pile) InternalBasePtr*
//   param_4 = float* worldPos (MoveTo destination + heading source)
//   param_5 = int cell index (pile cell; indexes the map grid for the path-mask byte)
//   param_6 = byte — when non-zero, cancel any pending actions first (first candidate only)
//
// Ghidra-verified constants: ~Hoe = -1 (OR R8D,0xffffffff @0x1400c0b68),
// ~Axe = -2 (MOV R8D,0xfffffffe @0x1400c0d5b/@0x1400c1024),
// pathType = QueryCount>0 ? 4 : 1 (MOV EAX,4 / MOV EBX,1 / CMOVNZ @0x1400c0f61).
void WorkAct_PerformWorkCtor9_c0ad0(CitizenComponent* const* citizenRef, uint32_t workId, ComponentEntity* const* targetIBP,
                                    float* worldPos, int cellIdx, uint8_t cancelFirst) {

    // the action stack = character + 0x48
    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* list = &character->actionStack;
    if (cancelFirst != 0) list->Clear();

    // heading = Entity_impl_29a0(worldPos, &target inner) — returned in XMM0.
    // 0x1400c0b31 MOV RAX,[R12]: the target reference read with no test (B6 14d G25).
    // LEFT: SIG WorkAct_PerformWorkCtor9_c0ad0 (the caller hands the pile reference as void*).
    ComponentEntity* targetInner = *targetIBP;
    float heading = 0.0f;
    {
        ComponentEntity* headingArg = targetInner;
        heading = HeadingToward(worldPos, &headingArg);
    }

    void* obj;
    ::Object::InternalPtr<Action> ibpA;
    ::Object::InternalPtr<Action> ibpB;

    // --- ShowTool (~Hoe = -1) ---
    obj = System::MemoryAllocator::MemAlloc(0x30, 8);
    Action* st = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        st = new (obj) ShowTool(&entityInner, -1 /* ~Hoe */);
    }
    ibpA = st;
    WA_Append(list, &ibpA);

    // --- SendComponentMessage → character+0x58 (replaces prior, DeletePtr; NOT pushed) ---
    obj = System::MemoryAllocator::MemAlloc(0x40, 8);
    Action* sm = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        sm = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0);
    }
    ::Object::InternalPtr<Action> ibpSm;
    ibpSm = sm;
    if (character->actionStack.field_0x10 != nullptr)
        character->actionStack.field_0x10.DeletePtr();
    character->actionStack.field_0x10 = ibpSm;

    // --- path-mask byte from the pile's map cell (terrain type +0x29) ---
    // 0x1400c0c24-0x1400c0c44: [0x1401c3dd8]+0xf8 and the cell read with no test.
    Map* grid = GameUpdateLoop::_singleton->m_game;
    PathType cellFlag = grid->cells.GetData()[static_cast<int64_t>(cellIdx)].terrainType;

    // --- inline PerformWork action, kind 7 ---
    obj = System::MemoryAllocator::MemAlloc(0x48, 8);
    Action* pw7 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        System::Time timeVal = {0};
        RtSystem_Time_Ctor(&timeVal, WA_C_172124());
        // the inlined PerformWork ctor 0x1400c9720 (useTool = 0, +0x44 = -1): the real ctor.
        pw7 = new (obj) PerformWork(&entityInner, heading, 7, timeVal._ticks, 0, -1);
    }
    ibpA = pw7;
    WA_Append(list, &ibpA);

    // --- ShowTool (~Axe = -2) when the citizen is currently carrying nothing ---
    if (WA_CitizenCarryCount(citizenRef) < 1) {
        obj = System::MemoryAllocator::MemAlloc(0x30, 8);
        Action* stAxe = nullptr;
        if (obj) {
            CitizenComponent* entityInner = *citizenRef;
            stAxe = new (obj) ShowTool(&entityInner, -2 /* ~Axe */);
        }
        ibpA = stAxe;
        WA_Append(list, &ibpA);
    }

    // --- PickupRawMaterial action (self+0x20 = citizen, self+0x28 = pile) ---
    obj = System::MemoryAllocator::MemAlloc(0x30, 8);
    Action* prm = nullptr;
    if (obj) {
        ComponentEntity* targetSlot = targetInner;
        CitizenComponent* citizenSlot = *citizenRef;
        prm = new (obj) PickupRawMaterial(&citizenSlot, &targetSlot);   // 0x1400c9b70
    }
    ibpB = prm;
    WA_Append(list, &ibpB);

    // --- CompleteTask action ---
    obj = System::MemoryAllocator::MemAlloc(0x38, 8);
    Action* ct = nullptr;
    if (obj) {
        ComponentEntity* targetSlot = targetInner;
        CitizenComponent* citizenSlot = *citizenRef;
        ct = new (obj) CompleteTask(workId, &citizenSlot,
                                          &targetSlot, 0);
    }
    ibpB = ct;
    WA_Append(list, &ibpB);

    // --- inline PerformWork action, kind 6 ---
    obj = System::MemoryAllocator::MemAlloc(0x48, 8);
    Action* pw6 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        System::Time timeVal = {0};
        RtSystem_Time_Ctor(&timeVal, WA_C_172124());
        // the inlined PerformWork ctor 0x1400c9720 (useTool = 0, +0x44 = -1): the real ctor.
        pw6 = new (obj) PerformWork(&entityInner, heading, 6, timeVal._ticks, 0, -1);
    }
    ibpA = pw6;
    WA_Append(list, &ibpA);

    // --- MoveTo: walk to the pile. pathType = carrying>0 ? 4 : 1; mask = (1<<(cellFlag&0x1f)) | 0xe1f ---
    obj = System::MemoryAllocator::MemAlloc(0x480, 8);
    Action* mv = nullptr;
    if (obj) {
        int carried = WA_CitizenCarryCount(citizenRef);
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        int pathType = (carried > 0) ? 4 : 1;
        CharacterComponent* family = entityInner->FindComponent<CharacterComponent>();   // 0x1400012d0
        PathBits mask = PathBitOf(cellFlag) | PathBits::Walk;
        mv = new (obj) MoveTo(&family, pathType, 0, worldPos, mask, 0, WA_MoveSpeed());
    }
    ibpB = mv;
    WA_Append(list, &ibpB);

    // --- ShowTool (~Axe = -2) when the citizen IS carrying something ---
    if (WA_CitizenCarryCount(citizenRef) > 0) {
        obj = System::MemoryAllocator::MemAlloc(0x30, 8);
        Action* stAxe2 = nullptr;
        if (obj) {
            CitizenComponent* entityInner = *citizenRef;
            stAxe2 = new (obj) ShowTool(&entityInner, -2 /* ~Axe */);
        }
        ibpA = stAxe2;
        WA_Append(list, &ibpA);
    }
}

// ============================================================
// PlayAnimation_Action_Ctor (0x1400c2490)
// ============================================================
//   param_1 = citizen InternalBasePtr*
//   param_2 = priority/workId (u32)
//   param_3 = citizenIndex (u32, forwarded to CompleteTask param_5)
// Builds SendComponentMessage (→ character+0x58), CompleteTask, and an inline
// PlayAnimation action, appending the latter two to the action stack.
void WorkAct_PlayAnimationCtor_c2490(CitizenComponent* const* citizenRef, uint32_t priority, uint32_t citizenIndex) {

    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* list = &character->actionStack;
    list->Clear();

    ::Object::InternalPtr<Action> ibp30;
    ::Object::InternalPtr<Action> ibpRes20;

    // --- SendComponentMessage → character+0x58 ---
    void* obj = System::MemoryAllocator::MemAlloc(0x40, 8);
    Action* sm = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        sm = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0);
    }
    ibp30 = sm;
    if (character->actionStack.field_0x10 != nullptr)
        character->actionStack.field_0x10.DeletePtr();
    character->actionStack.field_0x10 = ibp30;

    // --- CompleteTask action (param_3 forwarded as param_5) ---
    obj = System::MemoryAllocator::MemAlloc(0x38, 8);
    Action* ct = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        CitizenComponent* entityInner = *citizenRef;
        ct = new (obj) CompleteTask(priority, &entityInner, &innerIbp, citizenIndex);
    }
    ibp30 = ct;
    WA_Append(list, &ibp30);

    // --- inline PlayAnimation action ---
    void* inlineObj = System::MemoryAllocator::MemAlloc(0x40, 8);
    Action* pa = nullptr;
    if (inlineObj) {
        // System::Time::Time(&t, DAT_140172100)
        System::Time timeVal = {0};
        RtSystem_Time_Ctor(&timeVal, kRdata_140172100);
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        // The real ctor (Action(), fields zeroed; +0x28 animation = 0), then the stores.
        PlayAnimation* a = new (inlineObj) PlayAnimation();
        a->_field20 = entityInner;
        a->_field30 = timeVal;   // PlayAnimation +0x30 is the System::Time (B.4 ACT-b)
        pa = a;
    }
    ibpRes20 = pa;
    WA_Append(list, &ibpRes20);
}

// ============================================================
// FUN_1400bb0a0 — grazing/tend work-action ctor (pasturework / tendarea)
// ============================================================
//   param_1 = citizen InternalBasePtr*
//   param_2 = priority (u32)
//   param_3 = workEntity InternalBasePtr* (the entity)
//   param_4 = unused (u32, used as SendComponentMessage msg id slot init)
//   param_5 = float* targetPos (MoveTo destination)
//   param_6 = float* srcPos (used to compute heading; the original calls Vector3 sub
//             + Normalize + Heading but discards the result)
//   param_7 = typeId (u32, second SendComponentMessage msg id)
//   param_8 = u32 (SendComponentMessage param_4)
//   param_9 = u32 (SendComponentMessage param_6)
void WorkAct_GrazingWorkCtor_bb0a0(CitizenComponent* const* citizenRef, uint32_t priority, ComponentEntity* const* workEntityIBP,
                                   uint32_t /*unused4*/, float* targetPos, float* srcPos,
                                   uint32_t typeId, uint32_t flag8, uint32_t cellIdx9) {

    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* list = &character->actionStack;
    list->Clear();

    ::Object::InternalPtr<Action> ibp30;
    ::Object::InternalPtr<Action> ibpResA;
    ::Object::InternalPtr<Action> ibpResB;

    // --- SendComponentMessage #1 → character+0x58 ---
    void* obj = System::MemoryAllocator::MemAlloc(0x40, 8);
    Action* sm = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        sm = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0);
    }
    ibp30 = sm;
    if (character->actionStack.field_0x10 != nullptr)
        character->actionStack.field_0x10.DeletePtr();
    character->actionStack.field_0x10 = ibp30;

    // --- CompleteTask action ---
    obj = System::MemoryAllocator::MemAlloc(0x38, 8);
    Action* ct = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        CitizenComponent* entityInner = *citizenRef;
        ct = new (obj) CompleteTask(priority, &entityInner, &innerIbp, 0);
    }
    ibp30 = ct;
    WA_Append(list, &ibp30);

    // --- heading: Vector3(srcPos-targetPos) → Normalize → Heading (XMM0 → used by Ctor7) ---
    // Disasm: dst=[param_6=srcPos], src=[param_5=targetPos]; v = srcPos - targetPos.
    float heading = 0.0f;
    if (targetPos && srcPos) {
        Math::Vector3 v3(srcPos[0] - targetPos[0], srcPos[1] - targetPos[1], srcPos[2] - targetPos[2]);
        Math::Vector3 nv = v3.Normalize();   // was the hidden RDX return slot
        heading = nv.Heading();
    }

    // --- ShowTool (~Hoe = -2) ---
    obj = System::MemoryAllocator::MemAlloc(0x30, 8);
    Action* st = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        st = new (obj) ShowTool(&entityInner, ~0 /* ~Hoe = -1 (Hoe==0); disasm OR R8D,0xFFFFFFFF */);
    }
    ibpResA = st;
    WA_Append(list, &ibpResA);

    // --- SendComponentMessage #2 (msg id = param_7, params 8/9) ---
    obj = System::MemoryAllocator::MemAlloc(0x40, 8);
    Action* sm2 = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = typeId;
        ComponentEntity* entityInner = *workEntityIBP;
        sm2 = new (obj) SendComponentMessage(&entityInner, &msgId, flag8, &innerIbp, cellIdx9);   // 0x1400cb1d0
    }
    ibpResB = sm2;
    WA_Append(list, &ibpResB);

    // --- PerformWork_Ctor7: heading(XMM2)=computed heading, actionType(R9)=0xC,
    //     time=DAT_1401721b0, p6=1, p7=3 (disasm: R9D=0xc, not the decompile's Cancel|Normal) ---
    obj = System::MemoryAllocator::MemAlloc(0x48, 8);
    Action* pw7 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        System::Time timeVal = {0};
        RtSystem_Time_Ctor(&timeVal, kRdata_1401721b0);
        pw7 = new (obj) PerformWork(&entityInner, heading, 0xC, timeVal._ticks, 1, 3);
    }
    ibpResA = pw7;
    WA_Append(list, &ibpResA);

    // --- MoveTo (Fastest=3, destination=targetPos) ---
    obj = System::MemoryAllocator::MemAlloc(0x480, 8);
    Action* mv = nullptr;
    if (obj) {
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        CharacterComponent* family = entityInner->FindComponent<CharacterComponent>();   // 0x1400012d0
        float speed = kRdata_140172280;
        mv = new (obj) MoveTo(&family, 3 /*Fastest*/, 0, targetPos, PathBits::Walk, 0, speed);
    }
    ibpResA = mv;
    WA_Append(list, &ibpResA);

    // --- ShowTool (Shovel) ---
    obj = System::MemoryAllocator::MemAlloc(0x30, 8);
    Action* st2 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        st2 = new (obj) ShowTool(&entityInner, 3 /*Shovel; disasm MOV R8D,3*/);
    }
    ibpResA = st2;
    WA_Append(list, &ibpResA);
}

// ============================================================
// FUN_1400be6e0 — plant/orchard work-action ctor (foresterwork / plantorchard)
// ============================================================
//   param_1  = citizen InternalBasePtr*
//   param_2  = priority (u32)
//   param_3  = workEntity inner (u64; CompleteTask/CreateNaturalResource param_2)
//   param_4  = unused (u32, SendComponentMessage msg id slot init)
//   param_5  = float* position (MoveTo destination)
//   param_6  = u32 (CreateNaturalResource param_4)
//   param_7  = u32 (CompleteTask param_5)
//   param_8  = longlong* — if *param_8 != 0, emit optional SendComponentMessage #2
//   param_9  = u32* (msg id for optional SendComponentMessage #2 = *param_9)
//   param_10 = u64 unused (SendComponentMessage #2 param_4 = 0 in disasm)
//   param_11 = u32 (SendComponentMessage #2 param_6)
// param_3 is the description CreateNaturalResource takes (+0x20), param_8 the parent
// entity reference (*param_8 tested, then copied).
void WorkAct_PlantWorkCtor_be6e0(CitizenComponent* const* citizenRef, uint32_t priority,
                                 ComponentDescription* plantDesc,
                                 uint32_t /*unused4*/, float* position, uint32_t createParam6,
                                 uint32_t completeParam7, ComponentEntity* const* parentRef,
                                 uint32_t* msgIdPtr9, uint64_t /*unused10*/, uint32_t smParam11) {

    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* list = &character->actionStack;
    list->Clear();

    ::Object::InternalPtr<Action> ibp40;
    ::Object::InternalPtr<Action> ibpRes8;

    // --- SendComponentMessage #1 → character+0x58 ---
    void* obj = System::MemoryAllocator::MemAlloc(0x40, 8);
    Action* sm = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        sm = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0);
    }
    ibp40 = sm;
    if (character->actionStack.field_0x10 != nullptr)
        character->actionStack.field_0x10.DeletePtr();
    character->actionStack.field_0x10 = ibp40;

    // --- CompleteTask action (param_5 = param_7) ---
    obj = System::MemoryAllocator::MemAlloc(0x38, 8);
    Action* ct = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        CitizenComponent* entityInner = *citizenRef;
        ct = new (obj) CompleteTask(priority, &entityInner, &innerIbp, completeParam7);
    }
    ibp40 = ct;
    WA_Append(list, &ibp40);

    // --- optional SendComponentMessage #2 when *param_8 != 0 ---
    // B6 (14d G26): retail tests *param_8 only (0x1400be82d MOV R15,[RBP+0x68];
    // 0x1400be831 CMP qword ptr [R15],0) and reads *param_9 with no test (0x1400be85d /
    // 0x1400be861).
    if (*parentRef != nullptr) {
        obj = System::MemoryAllocator::MemAlloc(0x40, 8);
        Action* sm2 = nullptr;
        if (obj) {
            ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
            uint32_t msgId = *msgIdPtr9;
            ComponentEntity* entityInner = *parentRef;
            sm2 = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, smParam11);   // 0x1400cb1d0
        }
        ibp40 = sm2;
        WA_Append(list, &ibp40);
    }

    // --- ShowTool (~Hoe = -2) ---
    obj = System::MemoryAllocator::MemAlloc(0x30, 8);
    Action* st = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        st = new (obj) ShowTool(&entityInner, ~0 /* ~Hoe = -1 (Hoe==0); disasm OR R8D,0xFFFFFFFF */);
    }
    ibpRes8 = st;
    WA_Append(list, &ibpRes8);

    // --- CreateNaturalResource action (param_2 = workEntity inner = *param_8, param_4 = param_6) ---
    obj = System::MemoryAllocator::MemAlloc(0x38, 8);
    Action* cnr = nullptr;
    if (obj) {
        ComponentEntity* srcInner = *parentRef;   // *param_8, no test
        cnr = new (obj) CreateNaturalResource(plantDesc, &srcInner, createParam6);   // 0x1400c6a20
    }
    ibpRes8 = cnr;
    WA_Append(list, &ibpRes8);

    // --- PerformWork_Ctor7: heading(XMM2)=const DAT_1401722a8, actionType(R9)=0xC,
    //     time=DAT_140172178, p6=1, p7=-1 (disasm: R9D=0xc, XMM2=[0x1401722a8]) ---
    obj = System::MemoryAllocator::MemAlloc(0x48, 8);
    Action* pw7 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        System::Time timeVal = {0};
        RtSystem_Time_Ctor(&timeVal, kRdata_140172178);
        float headingC = WA_RdataF(kRdata_1401722a8);
        pw7 = new (obj) PerformWork(&entityInner, headingC, 0xC, timeVal._ticks, 1, -1);
    }
    ibpRes8 = pw7;
    WA_Append(list, &ibpRes8);

    // --- MoveTo (Fastest=3, destination=position) ---
    obj = System::MemoryAllocator::MemAlloc(0x480, 8);
    Action* mv = nullptr;
    if (obj) {
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        CharacterComponent* family = entityInner->FindComponent<CharacterComponent>();   // 0x1400012d0
        float speed = kRdata_140172280;
        mv = new (obj) MoveTo(&family, 3 /*Fastest*/, 0, position, PathBits::Walk, 0, speed);
    }
    ibpRes8 = mv;
    WA_Append(list, &ibpRes8);

    // --- ShowTool (Shovel) ---
    obj = System::MemoryAllocator::MemAlloc(0x30, 8);
    Action* st2 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        st2 = new (obj) ShowTool(&entityInner, 3 /*Shovel; disasm MOV R8D,3*/);
    }
    ibpRes8 = st2;
    WA_Append(list, &ibpRes8);
}
