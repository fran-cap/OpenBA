// src/game/Work/work_act_consume.cpp
// Work action-queue builder for the Consume/Produce work task.
//
// 1:1 reverse-engineering port of FUN_1400bef70 @ 0x1400bef70 (Application-x64.exe)
// -> WorkAct_ConsumeProduceCtor_bef70. Referenced from consumeproduce.cpp
// (s_fn_SetupWorkChain). Transcribed literally from the Ghidra disassembly; no
#include "Work/exe_bodies.h"
// refactoring / "improvement" of behavior.
//
// Builds the citizen's action chain on its CharacterComponent's action stack
// (CitizenComponent +0x278 -> character, stack at +0x48). A SendComponentMessage action
// is stored at character+0x58 (DeletePtr of the prior one first). The rest are appended
// to the stack via TaskListAppend (0x1400c54e0) or the inlined array-append the
// original compiler emitted for the last two entries.
//
// LEFT: the caller (ConsumeProduce, WORK-b) hands the citizen / entity references as
// void* and the description as an integer; the action ctors (ACT's C bodies) take
// their references as uint64_t*.
//
// Every non-obvious offset/constant cites its disasm address.

#include <new>
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"
#include "Action/Action.h"   // ENDGAME B.3.8 CB3
#include "Action/exe_bodies.h"      // B3R: the action ctor bodies called by name
#include "Game/exe_constants.h"     // B3R: kRdata_*
#include "system/Value.h"           // B3R: RtSystem_Time_Ctor
#include "Component/CitizenAIComponent.h"   // CitizenAIComponent::TypeId() (the message target)
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/ConsumeProduceDescription.h"   // ENDGAME B.3.9 D5
#include "Component/ConsumeProduceComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/StorageComponent.h"
#include "Entity/ComponentEntity.h"
#include "Action/SendComponentMessage.h"   // B4 P2 WORK-3: the action ctors by name
#include "Action/CompleteTask.h"
#include "Action/ShowTool.h"
#include "Action/MoveTo.h"
#include "Action/PerformWork.h"
#include "Action/CreateRawMaterial.h"
#include "Action/ConsumeRawMaterial.h"

// ============================================================
// Sub-callees, called by name (B3R)
// ============================================================
// Every exe sub-callee is one of our own bodies (Action/, Work/, Component/ exe_bodies.h):
//   0x1400c5400 TaskListCleanup                       WorkBase_TaskListCleanup_c5400
//   0x1400c54e0 TaskListAppend                        Pf_ActStackPush_54e0
//   0x1400cb1d0 SendComponentMessage copy-ctor        Pf_SendComponentMessageCopyCtor_b1d0
//               (RCX=self, RDX=&entityInner, R8=&msgId, R9D=p4, [RSP+0x20]=&innerIbp, [RSP+0x28]=p6)
//   0x1400c5da0 CompleteTask copy-ctor                Pf_CompleteTaskCopyCtor_5da0
//   0x140042890 ShowTool ctor (R8D=toolType)          ActChild_ShowToolCtor_42890
//   0x1400c71a0 CreateRawMaterial copy-ctor           Pf_CreateRawMaterialCopyCtor_71a0
//               (disasm 0x1400bf12f..0x1400bf1a2: RCX=self, RDX=p2, XMM2=p3(float), R9=p4,
//                [RSP+0x20]=p5, [RSP+0x28]=p6, [RSP+0x30]=p7, [RSP+0x38]=p8, [RSP+0x40]=p9(u8),
//                [RSP+0x48]=p10, [RSP+0x50]=p11)
//   0x1400c9720 PerformWork_Action_Ctor7              Pf_PerformWorkCtor7_9720
//   0x1400c9170 MoveTo ctor                           Pf_MoveToCtor_9170
//   0x1400c6440 ConsumeRawMaterial_Action_Ctor3       ActChild_ConsumeRawCtor3_c6440
//   0x1400c64b0 consume-list append                   Entity_ConsumeListAppend_64b0
//   0x140006270 FindComponent<StorageComponent>
//   0x1400012d0 Entity_FindComponent_Family           ActMv_FindComponentFamily_12d0
//   0x14000b6f0 FindComponent<ConsumeProduceComponent>
//   0x140119470 WeightedRandomIndex                   WeightedRandomIndex_119470
// 0x1400032e0 (task-list array grow) is Collection::Array<InternalPtr>::SetSize: the
// member call on ActionStack::actions (kSpec_PtrZero).
// Dispatch slots: 0x140152b60 MemAlloc -> System::MemoryAllocator::MemAlloc;
// 0x140152b00 System::Time(float) -> RtSystem_Time_Ctor; 0x140152a98 IsDerivedFrom ->
// the member Object::Base::IsDerivedFrom.
// .rdata: 0x14017214c = 1.0f (CreateRawMaterial XMM2), 0x140172280 = -1.0f (MoveTo speed).


// ---- small WA-style helpers ----
static inline void* WCP_Alloc(uint64_t size) {
    // disasm: MOV EDX,8 (align); ECX=size; CALL [0x140152b60].
    return System::MemoryAllocator::MemAlloc(size, 8);
}
// 0x1400c54e0 ActionStackPush (Action.h) on CharacterComponent::actionStack (+0x48).
static inline void WCP_Append(ActionStack* list, const ::Object::InternalPtr<Action>* ibp) {
    ActionStackPush(list, ibp);
}

// Search a ComponentDescription's sub-descriptions for the one whose Type() (slot 2)
// is ConsumeProduceDescription's, then IsDerivedFrom; returns it or null. Mirrors the
// two identical loops at 0x1400bf2a7.. and 0x1400bf3a4..
static ConsumeProduceDescription* WCP_FindComponentDerived(ComponentDescription* desc) {
    uint32_t typeId = ConsumeProduceDescription::TypeId();  // DWORD_1401c7388
    ConsumeProduceDescription* result = nullptr;
    int count = desc->_subDescCount;                           // [desc+0x30]
    if (count > 0) {
        int idx = 0;
        for (;;) {
            Description* elem = desc->_subDescs[idx];
            if (elem->Type() == typeId) {
                Description* comp = desc->_subDescs[idx];
                if (comp->IsDerivedFrom(typeId))   // slot 0x140152a98
                    result = static_cast<ConsumeProduceDescription*>(comp);  // CMOVNZ RCX,RBX
                break;
            }
            ++idx;
            if (idx >= count) break;
        }
    }
    return result;
}

// Inlined action-stack append the compiler emitted for the last two entries
// (0x1400bf5be.. and 0x1400bf69c..). Functionally identical to TaskListAppend: the
// Array's count (+0x50) is the stack's size and currentAction (+0x60) its top.
static void WCP_AppendInline(CharacterComponent* character, Action* ibpVal) {
    // FUN_1400032e0 == Collection::Array<InternalPtr>::SetSize (kSpec_PtrZero); it only
    // touches the array header at +0x48..+0x58.
    int cap = character->actionStack.actions._size;                  // [RSI+0x8]
    if (cap == 0) {
        cap = 0x10;
        character->actionStack.actions.SetSize(cap);
    } else {
        int n = character->actionStack.current + 1;                  // [RSI+0x18]+1
        if (n == cap) {
            cap *= 2;
            character->actionStack.actions.SetSize(cap);
        }
    }

    // Cleanup walk over the chain at actions[top] (clear the status byte, call Exit).
    int top = character->actionStack.current;                        // [RSI+0x18] (signed)
    if (top >= 0) {
        Action* node = character->actionStack.actions[top].Get();
        while (node != nullptr) {
            if (node->_status) {
                node->_status = false;
                node->Exit(&character->actionStack);               // slot 7 (vtable +0x38)
            }
            node = node->_next.Get();   // Action::_next (+0x10), the chained action
        }
    }

    // top++; actions[top] = ibpVal.
    character->actionStack.current += 1;                             // INC [RSI+0x18]
    character->actionStack.actions[character->actionStack.current] = ibpVal;
}

// ============================================================
// WorkAct_ConsumeProduceCtor_bef70 (FUN_1400bef70)
// ============================================================
// Parameters (Win64 + stack), matching consumeproduce.cpp's s_fn_SetupWorkChain_t:
//   param_1  RCX   citizen InternalBasePtr*
//   param_2  EDX   priority/workId (u32)
//   param_3  R8    entity InternalBasePtr* (the work target / raw-material source)
//   param_4  R9    float* destPos (MoveTo destination)
//   param_5  [+0x28] float  heading (PerformWork XMM2)
//   param_6  [+0x30] longlong  the ComponentDescription for the 2nd search loop
//   param_7  [+0x38] u64    productType (CreateRawMaterial p2)
//   param_8  [+0x40] u32    flags (low byte = "haul/produce" enable)
//   param_9  [+0x48] int*   -> if *param_9 != 0, emit optional SendComponentMessage #2
//   param_10 [+0x50] u32    SendComponentMessage #2 p4
//   param_11 [+0x58] u32    CreateRawMaterial p8
//   param_12 [+0x60] float  produceTime (Time::Time for PerformWork)
//   param_13 [+0x68] u32    CompleteTask p5
void WorkAct_ConsumeProduceCtor_bef70(CitizenComponent* const* citizenRef, uint32_t param_2,
                                      ComponentEntity* const* entityRef,
                                      float* param_4, float param_5, ComponentDescription* param_6,
                                      ComponentDescription* rawMatDesc, uint32_t param_8, int* param_9,
                                      uint32_t param_10, uint32_t param_11, float param_12,
                                      uint32_t param_13) {

    // the action stack = character + 0x48  (0x1400befaf..0x1400befd1)
    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* list = &character->actionStack;          // RSI / plVar15
    list->Clear();

    void* obj;

    // --- SendComponentMessage #1 -> stored at character+0x58 (DeletePtr old first) (0x1400befd6..) ---
    obj = WCP_Alloc(0x40);                    // MOV EDX,8; LEA ECX,[RDX+0x38] => 0x40
    Action* sm1 = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        sm1 = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0);
    }
    ::Object::InternalPtr<Action> ibpSm1;
    ibpSm1 = sm1;                // ctor(&local_60, sm1)
    if (character->actionStack.field_0x10 != nullptr)     // [lVar2+0x58]
        character->actionStack.field_0x10.DeletePtr();
    character->actionStack.field_0x10 = ibpSm1;

    // --- CompleteTask action (0x1400bf06b..) ---
    obj = WCP_Alloc(0x38);                    // LEA ECX,[RDX+0x30] => 0x38
    Action* ct = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        CitizenComponent* entityInner = *citizenRef;     // *param_1
        ct = new (obj) CompleteTask(param_2, &entityInner, &innerIbp, param_13);
    }
    ::Object::InternalPtr<Action> ibpCt;
    ibpCt = ct;
    WCP_Append(list, &ibpCt);                 // FUN_1400c54e0(plVar15, &local_70)

    // cVar4 = (char)param_8  (0x1400bf0d2)
    uint8_t cVar4 = static_cast<uint8_t>(param_8);

    if (cVar4 != 0) {
        // --- CreateRawMaterial action gated on the storage slot (0x1400bf0e0..) ---
        StorageComponent* storage = (*citizenRef)->_entity->FindComponent<StorageComponent>();   // 0x140006270
        uint32_t uVar1 = static_cast<uint32_t>(storage->capacity);   // *(*pIVar11+0x38) -> EDI

        obj = WCP_Alloc(0x58);                // LEA ECX,[RDX+0x50] => 0x58
        Action* crm = nullptr;
        if (obj) {
            ComponentEntity* local60 = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot   // ctor(&local_60, 0)  (p10)
            CitizenComponent* local70 = *citizenRef;           // *param_1  -> p4
            ComponentEntity* local68 = local70->workplace.Get();     // *(*param_1+0x248) -> p5
            float xmm2 = kRdata_14017214c;                    // [0x14017214c]==1.0f (g_fUVScale)
            // p6 = leftover 0 in [RSP+0x28]; p7=uVar1; p8=param_11; p9=0; p10=&local60; p11=4.
            crm = new (obj) CreateRawMaterial(rawMatDesc, xmm2, &local70, &local68,
                                              0, uVar1, param_11, 0, &local60, 4);   // 0x1400c71a0
        }
        ::Object::InternalPtr<Action> ibpCrm;
        ibpCrm = crm;
        WCP_Append(list, &ibpCrm);

        // --- optional SendComponentMessage #2 when *param_9 != 0 (0x1400bf1c5..) ---
        // B6 (14d G27): 0x1400bf1c5 MOV RDI,[RBP+0x87]; 0x1400bf1cc CMP dword ptr [RDI],0 --
        // param_9 itself is not tested.
        if (static_cast<uint32_t>(*param_9) != 0) {  // CMP [RDI],0; JBE (unsigned)
            obj = WCP_Alloc(0x40);            // LEA ECX,[RDX+0x38] => 0x40
            Action* sm2 = nullptr;
            if (obj) {
                ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
                uint32_t msgId = static_cast<uint32_t>(*param_9);                       // *param_9
                ComponentEntity* e2 = *entityRef;                                       // *param_3 (R13)
                sm2 = new (obj) SendComponentMessage(&e2, &msgId, param_10, &innerIbp, 0);
            }
            ::Object::InternalPtr<Action> ibpSm2;
            ibpSm2 = sm2;
            WCP_Append(list, &ibpSm2);
        }
    }

    // --- ShowTool (~Hoe = -1) action (0x1400bf247..) ---
    obj = WCP_Alloc(0x30);                    // LEA ECX,[RDX+0x28] => 0x30
    Action* st1 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;  // *param_1
        // disasm: OR R8D,0xffffffff => toolType = -1 (~Hoe, Hoe==0).
        st1 = new (obj) ShowTool(&entityInner, -1);
    }
    ::Object::InternalPtr<Action> ibpSt1;
    ibpSt1 = st1;
    WCP_Append(list, &ibpSt1);

    // --- 1st search over the entity's description; derive toolType (0x1400bf293..) ---
    ConsumeProduceDescription* foundComp1 = WCP_FindComponentDerived((*entityRef)->_description);
    // toolType = found->_u50. When none is found retail reads [0 + 0x50] (RCX = R14 = 0):
    // a read through the null receiver that faults exactly as retail's does. Not guarded.
    // The null-receiver read is spelled as the address it faults on.
    uint32_t toolType = foundComp1 ? foundComp1->_u50
                                   : *reinterpret_cast<uint32_t*>(0x50);   // the null-receiver read is spelled as the address it faults on.  // CAST-OK: null_receiver
    // actionType = (toolType <= 0xa) ? toolType+9 : 8   (0x1400bf307..0x1400bf312)
    int actionType = 8;
    if (toolType <= 0xa) actionType = static_cast<int>(toolType) + 9;

    // --- PerformWork_Ctor7 (0x1400bf316..) ---
    obj = WCP_Alloc(0x48);                    // LEA ECX,[RDX+0x40] => 0x48
    Action* pw7 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;  // *param_1
        System::Time timeRes = {0};
        RtSystem_Time_Ctor(&timeRes, param_12);        // Time::Time(&local_70, param_12)
        // heading=param_5 (XMM2), actionType=EDI (R9D), p6=1, p7=-1.
        pw7 = new (obj) PerformWork(&entityInner, param_5, actionType, timeRes._ticks, 1, -1);
    }
    ::Object::InternalPtr<Action> ibpPw7;
    ibpPw7 = pw7;
    WCP_Append(list, &ibpPw7);

    // --- consume-list build (0x1400bf394..) gated on cVar4 (R12B) ---
    if (cVar4 != 0) {
        // 2nd search over param_6 (the caller's ComponentDescription).
        ConsumeProduceDescription* foundComp2 =
            WCP_FindComponentDerived(param_6);

        // the ConsumeProduceComponent + weighted random recipe index.
        ConsumeProduceComponent* cp = (*entityRef)->FindComponent<ConsumeProduceComponent>();   // 0x14000b6f0
        uint64_t widx = cp->PickConsumeRecipe();   // 0x140119470
        int64_t idx = static_cast<int64_t>(static_cast<int>(widx));   // LEA R13,[RDX+RDX*4]: idx*0x28

        // the recipe array (*(foundComp2+0x28)); when none is found retail reads [0 + 0x28]:
        // the second read through the null receiver, faulting as retail's does. Not guarded.
        // The null-receiver read is spelled as the address it faults on.
        ConsumeProduceRecipe* recBase = foundComp2 ? foundComp2->_recipes
                                                   : *reinterpret_cast<ConsumeProduceRecipe**>(0x28);   // the null-receiver read is spelled as the address it faults on.  // CAST-OK: null_receiver
        if (recBase[idx].inputCount > 0) {
            // ConsumeRawMaterial_Action_Ctor3(self, &(*param_3)) (0x1400bf446..)
            obj = WCP_Alloc(sizeof(ConsumeRawMaterial));   // MOV EDX,8; LEA ECX,[RDX+0x38] => 0x40
            ConsumeRawMaterial* crmAct = nullptr;
            if (obj) {
                ComponentEntity* inner = *entityRef;   // *param_3
                crmAct = new (obj) ConsumeRawMaterial(&inner);   // 0x1400c6440
            }
            ConsumeRawMaterial* consumeIbp = crmAct;   // stored at [RBP+0x57]
            ::Object::InternalPtr<Action> appendCopy = consumeIbp;  // local_60 = local_res18
            WCP_Append(list, &appendCopy);

            // inner loop: append each (resType,count) pair to the consume list (0x1400bf496..)
            int count = recBase[idx].inputCount;
            if (count > 0) {
                ConsumeProduceInput* subArr = recBase[idx].inputs;   // *(recBase + idx*0x28)
                for (int j = 0; j < count; ++j) {
                    consumeIbp->AddMaterial(subArr[j].material, subArr[j].count);   // 0x1400c64b0
                    // re-read count each iter (CMP [RSI+R13*8+8]); recBase==RSI here.
                    count = recBase[idx].inputCount;
                }
            }
        }
        // toolType reload (MOV R15D,[RBP+0x7f]) — value unchanged from above.
    }

    // --- MoveTo action with path-type switch on (toolType+1) (0x1400bf4ed..) ---
    obj = WCP_Alloc(0x480);                   // MOV ECX,0x480 => 0x480
    Action* mv = nullptr;
    if (obj) {
        // Jump table @0x1400bf758 (read_memory verified), index = toolType+1,
        // default(>8 unsigned)=Slow(1). Each entry (base 0x140000000):
        //   idx0->0x1400bf53a(EBX=1,Slow) idx1->bf52c(2,Fast) idx2->bf52c(2)
        //   idx3->bf533(3,Fastest) idx4->bf533(3) idx5->bf52c(2) idx6->bf52c(2)
        //   idx7->bf52c(2) idx8->bf52c(2)
        int pathType;
        uint32_t sw = toolType + 1;
        if (sw > 8) {
            pathType = 1;                     // default Slow (CMP ECX,8; JA -> MOV EBX,1)
        } else {
            switch (sw) {
                case 1: case 2: case 5: case 6: case 7: case 8: pathType = 2; break;  // Fast
                case 3: case 4:                                 pathType = 3; break;  // Fastest
                default:                                        pathType = 1; break;  // case 0 -> Slow
            }
        }
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();  // *(*param_1+0x10)
        CharacterComponent* family = entityInner->FindComponent<CharacterComponent>();   // 0x1400012d0
        float speed = kRdata_140172280;          // [0x140172280]==-1.0f
        mv = new (obj) MoveTo(&family, pathType, 0, param_4, 0xe1f, 0, speed);   // 0x1400c9170
    }
    ::Object::InternalPtr<Action> ibpMv;
    ibpMv = mv;                   // ctor(&local_58, mv)
    WCP_AppendInline(character, ibpMv.Get());        // inlined append (0x1400bf5be..)

    // --- final ShowTool (toolType) action (0x1400bf65c..) ---
    obj = WCP_Alloc(0x30);                     // LEA ECX,[RDX+0x28] => 0x30
    Action* st2 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;  // *param_1 (R12)
        st2 = new (obj) ShowTool(&entityInner, static_cast<int>(toolType));
    }
    ::Object::InternalPtr<Action> ibpSt2;
    ibpSt2 = st2;                 // ctor(&local_res18, st2)
    WCP_AppendInline(character, ibpSt2.Get());       // inlined append (0x1400bf69c..)
}
