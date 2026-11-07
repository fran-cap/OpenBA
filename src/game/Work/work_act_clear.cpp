// src/game/Work/work_act_clear.cpp
// Work clear-area / harvest-area action-chain constructor.
//
// Faithful 1:1 port of ShowTool_Action_Ctor4 @ 0x1400c1530 (Application-x64.exe),
// exposed here as WorkAct_HarvestAreaCtor_c1530. Despite the (mislabeled) Ghidra
#include "Work/exe_bodies.h"
// name, this is a LARGE work-action-queue builder (~0x948 bytes): it builds the
// citizen's action chain on its CharacterComponent's action stack (CitizenComponent
// +0x278 -> character, stack at +0x48; the SendComponentMessage action is stored at
// character+0x58 after DeletePtr of the previous one).
//
// This is a sibling of WorkAct_PerformWorkCtor_c1eb0 in work_base_actionctor.cpp,
// but differs in real ways (verified against the actual disassembly):
//   * param_3 is a LOOP COUNT: it iterates param_3 times over the param_4[] array
//     (param_4 advances by 8 each iteration), and for each element builds an INLINE
//     RemoveNaturalResource action (vtable 0x140160050) + appends it + sets bit 0x14
//     (0x100000) on that target's +0x20 flags, detaching via 0x1400c3470 on change.
//   * the post-loop chain is: PerformWork_Ctor7 (0x1400c9720), MoveTo (0x1400c9170),
//     then an INLINE ShowTool action (vtable 0x1401589b0). There is NO inline-
//     PerformWork / AnimateNaturalResource block (that belongs to the c1eb0 sibling).
//   * the list "append" is INLINED in the disasm for every action except the first
//     two (CompleteTask + the first ShowTool), which use FUN_1400c54e0. We mirror
//     the inlined append faithfully via WA_AppendInline (same list-growth logic the
//     out-of-line FUN_1400c54e0 / FUN_1400032e0 perform on character+0x48..+0x60).
//
// Instruction-faithful: do NOT "improve" behavior. Every offset / constant below
// cites its disasm address.

#include <new>
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"
#include "Action/Action.h"   // ENDGAME B.3.8 CB3
#include "Action/RemoveNaturalResource.h"   // ENDGAME B.3.8 CB3
#include "Action/ShowTool.h"   // ENDGAME B.3.8 CB3
#include "Action/exe_bodies.h"      // B3R: the action ctor bodies called by name
#include "Game/exe_constants.h"     // B3R: kRdata_*
#include "object/Internal.h"
#include "Component/CitizenAIComponent.h"   // CitizenAIComponent::TypeId() (the message target)
#include "Component/NaturalResourceDescription.h"   // ENDGAME B.3.9 D5
#include "Component/Component.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Entity/ComponentEntity.h"
#include <cstdint>
#include "Action/SendComponentMessage.h"   // B4 P2 WORK-3: the action ctors by name
#include "Action/CompleteTask.h"
#include "Action/MoveTo.h"
#include "Action/PerformWork.h"
#include "Component/NaturalResourceComponent.h"   // the targets (0x1400c1530 param_4)

// ============================================================
// Sub-callees, called by name (B3R)
// ============================================================
// Every exe sub-callee is one of our own bodies (Action/, Work/ exe_bodies.h):
//   0x1400c29a0 Entity_impl_29a0 (heading, XMM0)    ActChild_Entity29a0_c29a0
//               (disasm 0x1400c1586: RCX=param_5 worldPos, RDX=&local_res8 entity inner)
//   0x1400c5400 TaskListCleanup                     WorkBase_TaskListCleanup_c5400
//   0x1400c54e0 TaskListAppend                      Pf_ActStackPush_54e0
//   0x1400c3470 Work-target detach                  ActHunt_NotifyEntityChanged_c3470
//   0x1400012d0 Entity_FindComponent_Family         ActMv_FindComponentFamily_12d0
//   0x1400cb1d0 SendComponentMessage copy-ctor      Pf_SendComponentMessageCopyCtor_b1d0
//   0x1400c5da0 CompleteTask copy-ctor              Pf_CompleteTaskCopyCtor_5da0
//   0x140042890 ShowTool ctor (R8D=toolType)        ActChild_ShowToolCtor_42890
//   0x1400c9720 PerformWork_Action_Ctor7            Pf_PerformWorkCtor7_9720
//   0x1400c9170 MoveTo ctor                         Pf_MoveToCtor_9170
// Dispatch slots:
//   0x140152a98 IsDerivedFrom        -> the member Object::Base::IsDerivedFrom
//   0x140152b60 MemAlloc             -> System::MemoryAllocator::MemAlloc
//   0x140152b68 MemFree              -> System::MemoryAllocator::MemFree
//   0x140152a90 InternalBasePtr ctor -> placement-new Object::InternalBasePtr
//   0x140151ab8 ?DeletePtr@InternalBasePtr@Object@@QEAAXXZ -> Object::InternalBasePtr::DeletePtr
//                                    (Runtime 0x1800c0150, object/Internal.h), by name
// .rdata: 0x140172280 = -1.0f (MoveTo speed).


static inline void* WAC_Alloc(uint64_t size, uint32_t align) {
    return System::MemoryAllocator::MemAlloc(size, static_cast<int>(align));
}

// Inlined action-stack append, mirroring the disasm's inlined FUN_1400c54e0 body on the
// CharacterComponent's stack (+0x48 data, +0x50 count = the stack's size, +0x54
// capacity, +0x60 top). Faithful to disasm @0x1400c17ad..0x1400c18ec (RemoveNaturalResource
// append) and the identical blocks for PerformWork / inline ShowTool.
static void WAC_AppendInline(CharacterComponent* character, Action* actionIbpVal) {
    ActionStack::ActionList& actions = character->actionStack.actions;
    int iVar16 = actions._size;
    if (iVar16 == 0) {
        // First append: initialize to capacity 0x10, copy any prior contents.
        ::Object::InternalPtr<Action>* pvVar11 = actions._data;
        actions._size = 0x10;
        if (actions._capacity < 0x10) {
            actions._capacity = 0x10;
            actions._data = static_cast<::Object::InternalPtr<Action>*>(
                System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(actions._capacity) << 3, 8));
        }
        // The original copies index 0 (iVar16==0 ⇒ start = 0), zeroing the rest.
        // Disasm: start = (RDI==data) ? old-count(EBX) : 0; here EBX(old count)=0.
        int64_t start = 0;
        if (pvVar11 == actions._data) start = iVar16; // ==0 here
        for (int64_t i = start; i < 0x10; ++i) {
            ::Object::InternalPtr<Action>* slot = &actions._data[i];
            if (i < static_cast<int64_t>(iVar16)) {
                if (slot) { *slot = nullptr; *slot = pvVar11[i]; }
            } else if (slot) {
                *slot = nullptr;
            }
        }
        if (pvVar11 != actions._data) System::MemoryAllocator::MemFree(pvVar11);
    } else if (character->actionStack.current + 1 == iVar16) {
        // FUN_1400032e0 == Collection::Array<InternalPtr>::SetSize (kSpec_PtrZero) on the
        // array header at +0x48 (data, count +0x50, capacity +0x54).
        actions.SetSize(iVar16 * 2);
    }

    // Drain any pending nodes from the current index slot (cancel/detach chain).
    if (character->actionStack.current >= 0) {
        // The step: Action::_next (+0x10), the chained action.
        for (Action* node = character->actionStack.actions[character->actionStack.current].Get();
             node != nullptr;
             node = node->_next.Get()) {
            if (node->_status) {
                node->_status = false;
                // slot 7 (vtable +0x38) Action::Exit(list): the node is an action on the
                // citizen's action stack (+0x10 next, +0x18 _status, as the walk 0x1400c4f10)
                node->Exit(&character->actionStack);
            }
        }
    }

    character->actionStack.current += 1;
    character->actionStack.actions[character->actionStack.current] = actionIbpVal;
}

// ============================================================
// WorkAct_HarvestAreaCtor_c1530 (0x1400c1530)
// ============================================================
//   param_1 (RCX) = citizen InternalBasePtr*
//   param_2 (EDX) = workId (u32, CompleteTask param_2)
//   param_3 (R8D) = count of param_4[] entries to clear (loop count; signed)
//   param_4 (R9)  = the target components (count entries; advances by 8/iter)
//   param_5 (stack, [RBP+0x60]) = float* worldPos (heading + MoveTo destination)
//   param_6 (stack, [RBP+0x68]) = u32 (CompleteTask param_5)
//   param_7 (stack, [RBP+0x70]) = dword (unused on entry; reloaded as SendComponentMessage msg id)
void WorkAct_HarvestAreaCtor_c1530(CitizenComponent* const* citizenRef, uint32_t param_2, int param_3,
                                   NaturalResourceComponent* const* targets, float* param_5, uint32_t param_6,
                                   uint32_t /*param_7*/) {

    // --- heading = Entity_impl_29a0(param_5, &{param_4[0] entity}) → XMM6 (0x1400c1563..0x1400c158b) ---
    ComponentEntity* firstEntityInner = targets[0]->_entity.Get();
    float heading = 0.0f;
    heading = HeadingToward(param_5, &firstEntityInner);

    // --- derive the description off param_4[0]; keep it if IsDerivedFrom(c7b68) (0x1400c158e..0x1400c15b6) ---
    Description* targetComp = targets[0]->_description;
    NaturalResourceDescription* local_60 = nullptr; // derived target (the "pIVar8 = local_60" used post-loop)
    bool derived = false;
    derived = targetComp->IsDerivedFrom(NaturalResourceDescription::TypeId());   // slot 0x140152a98
    if (derived) local_60 = static_cast<NaturalResourceDescription*>(targetComp);

    // --- the action stack = character + 0x48 ; cleanup (0x1400c15ba..0x1400c15e2) ---
    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* plVar15 = &character->actionStack;
    plVar15->Clear();

    ::Object::InternalPtr<Action> ibpA; // local_68 / local_res20 scratch IBP
    ::Object::InternalPtr<Action> ibpB; // second scratch IBP (local_res20 alt)

    // --- SendComponentMessage action → stored at character+0x58 (DeletePtr prior) (0x1400c15e7..0x1400c1677) ---
    {
        void* obj = WAC_Alloc(0x40, 8); // LEA EDX,[R12+8]; LEA ECX,[RDX+0x38] = 0x40
        Action* sm = nullptr;
        if (obj) {
            ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
            uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
            ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
            sm = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0);
        }
        ibpA = sm;
        if (character->actionStack.field_0x10 != nullptr)   // [0x140151ab8] = InternalBasePtr::DeletePtr
            character->actionStack.field_0x10.DeletePtr();
        character->actionStack.field_0x10 = ibpA;
    }

    // --- CompleteTask action; append via FUN_1400c54e0 (0x1400c1677..0x1400c16dc) ---
    {
        void* obj = WAC_Alloc(0x38, 8); // MOV EDX,8; LEA ECX,[RDX+0x30] = 0x38
        Action* ct = nullptr;
        if (obj) {
            CitizenComponent* entityInner = *citizenRef;
            // RDX=&entityInner, R8=&local_res8(innerIbp from prior IbpCtor), but disasm
            // reuses local_res8 (just IbpCtor'd to 0 at +0x1693). Mirror with fresh inner.
            ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
            ct = new (obj) CompleteTask(param_2, &entityInner, &innerIbp, param_6);
        }
        ibpB = ct;
        ActionStackPush(plVar15, &ibpB);
    }

    // --- ShowTool (~Hoe = -1; disasm OR R8D,0xffffffff) action; append via FUN_1400c54e0 (0x1400c16dc..0x1400c1729) ---
    {
        void* obj = WAC_Alloc(0x30, 8); // MOV EDX,8; LEA ECX,[RDX+0x28] = 0x30
        Action* st = nullptr;
        if (obj) {
            CitizenComponent* entityInner = *citizenRef;
            st = new (obj) ShowTool(&entityInner, ~0); // OR R8D,0xffffffff @0x1400c16fb
        }
        ibpA = st;
        ActionStackPush(plVar15, &ibpA);
    }

    // --- LOOP: for i in [0, param_3): inline RemoveNaturalResource action per param_4[i]
    //     (0x1400c1729..0x1400c1930). param_4 advances by 8 each iter; lVar18=param_3 counter. ---
    if (param_3 > 0) {
        int64_t loopCtr = static_cast<int64_t>(param_3); // R12=R14=param_3
        do {
            ComponentEntity* innerR4 = (*targets)->_entity.Get(); // RDI

            void* obj = WAC_Alloc(0x38, 8); // MOV EDX,8; LEA ECX,[RDX+0x30] @0x1400c174a = 8+0x30 = 0x38
            Action* actionObj = nullptr;    // R14 (the built action, or 0)
            if (obj) {
                CitizenComponent* entityInner = *citizenRef; // RBX = *(*param_1)
                ComponentEntity* targetInner = innerR4;
                // 0x1400c176d..0x1400c1796: the inlined RemoveNaturalResource ctor
                // (+0x20 citizen inner, +0x28 param_4[i] inner, +0x30 = 1) — the real ctor.
                actionObj = new (obj) RemoveNaturalResource(&targetInner, &entityInner, 1);
            }
            ibpA = actionObj; // &local_res8 → ibpA scratch

            // inline append of ibpA (0x1400c17ad..0x1400c18ec)
            WAC_AppendInline(character, ibpA.Get());

            // set bit 0x14 (0x100000) on param_4[i]'s flags; on change and an entity,
            // notify it via FUN_1400c3470 (0x1400c18f0..0x1400c1929)
            Component* targetObj = *targets;
            uint32_t oldFlags = targetObj->_flags;
            uint32_t newFlags = oldFlags | 0x100000u; // BTS EAX,0x14
            targetObj->_flags = newFlags;
            if (static_cast<uint16_t>(oldFlags ^ newFlags) != 0 &&            // TEST CX,CX
                targetObj->_entity != nullptr) {
                ComponentEntity* detach = targetObj->_entity.Get();
                detach->RefreshEntityFlags();   // 0x1400c3470
            }

            targets = targets + 1; // ADD R15,0x8
            --loopCtr;             // DEC R12
        } while (loopCtr != 0);
    }

    // --- actionType: EBX=8 default; if the description's +0x80 <= 0xa: EBX = that+9 (0x1400c1936..0x1400c194b) ---
    int actionType = 8;
    if (local_60->_u80 <= 0xa)   // read through the narrowed pointer with no test (B6 14d G24)
        actionType = static_cast<int>(local_60->_u80) + 9;

    // --- PerformWork_Ctor7: heading(XMM2)=heading, actionType(R9)=actionType,
    //     time=description+0x68, p6=1, p7=description+0x80 (0x1400c194e..0x1400c1998) ---
    // B6 (14d G24): retail tests only the MemAlloc results here and below; +0x68 / +0x80
    // are read through the narrowed description with no test.
    {
        void* obj = WAC_Alloc(0x48, 8); // MOV EDX,8; LEA ECX,[RDX+0x40] = 0x48
        Action* pwIbp = nullptr;
        if (obj) {
            CitizenComponent* entityInner = *citizenRef;
            int p7  = static_cast<int>(local_60->_u80);         // [RSP+0x30]
            int64_t time = local_60->_time68._ticks;            // [RSP+0x20]
            Action* pw = nullptr;
            pw = new (obj) PerformWork(&entityInner, heading, actionType, time, 1, p7);
            ibpA = pw;
            pwIbp = ibpA.Get();
        } else {
            ibpA = nullptr;
            pwIbp = ibpA.Get();
        }
        WAC_AppendInline(character, pwIbp);
    }

    // --- MoveTo (path type from switch on the description's +0x80) (0x1400c1aed..0x1400c1ba9) ---
    {
        void* obj = WAC_Alloc(0x480, 8); // MOV ECX,0x480
        Action* mvIbp = nullptr;
        if (obj) {
            // switch(description+0x80); ECX = that+1 (INC ECX); CMP ECX,8; JA → default Slow(1).
            // jump table @0x1400c1e7c (idx = resKind+1, 0-based):
            //   idx0→0x1b3f Slow(1); idx1,2,5,6,7,8→0x1b31 Fast(2); idx3,4→0x1b38 Fastest(3).
            int pathType;
            uint32_t resKind = local_60->_u80;
            uint32_t idx = resKind + 1; // INC ECX @0x1400c1b14
            if (idx > 8) {
                pathType = 1; // Slow (default; CMP ECX,8 / JA @0x1400c1b16)
            } else {
                switch (idx) {
                    case 0:                                  pathType = 1; break; // Slow
                    case 1: case 2: case 5: case 6:
                    case 7: case 8:                          pathType = 2; break; // Fast
                    case 3: case 4:                          pathType = 3; break; // Fastest
                    default:                                 pathType = 1; break;
                }
            }
            ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
            CharacterComponent* family = entityInner->FindComponent<CharacterComponent>();   // 0x1400012d0
            float speed = kRdata_140172280; // -1.0f
            Action* mv = nullptr;
            mv = new (obj) MoveTo(&family, pathType, 0, param_5, PathBits::Walk, 0, speed);
            ibpA = mv;
            mvIbp = ibpA.Get();
        } else {
            ibpA = nullptr;
            mvIbp = ibpA.Get();
        }
        WAC_AppendInline(character, mvIbp);
    }

    // --- inline ShowTool action (vtable 0x1401589b0); toolType = description+0x80
    //     (0x1400c1cef..0x1400c1e58) ---
    {
        void* obj = WAC_Alloc(0x30, 8); // MOV EDX,8; LEA ECX,[RDX+0x28] = 0x30
        Action* stIbp = nullptr;
        if (obj) {
            CitizenComponent* entityInner = *citizenRef;                  // RBX
            uint32_t toolType = local_60->_u80;                           // EDI = [R15+0x80]
            // 0x1400c1d1e..0x1400c1d3f: the inlined ShowTool ctor — the real ctor.
            ibpB = new (obj) ShowTool(&entityInner, static_cast<int>(toolType)); // &local_res20
            stIbp = ibpB.Get();
        } else {
            ibpB = nullptr;
            stIbp = ibpB.Get();
        }
        WAC_AppendInline(character, stIbp);
    }
}
