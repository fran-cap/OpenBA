// src/game/Work/work_act_path.cpp
// WorkAct_GatherRawMatCtor_bf790 — 1:1 port of exe Pathfinding_FindPath @ 0x1400bf790.
//
// Despite the Ghidra name this is NOT pathfinding: it is a citizen work-action-queue
// builder (~0x835 bytes) that constructs a pickup/dropoff/move action chain and stores
// it on the citizen's action stack (CitizenComponent +0x278 -> its CharacterComponent,
// stack at +0x48). It
// is the "gather raw material" task builder: walk to a source, optional pickup, drop off
#include "Work/exe_bodies.h"
// raw material at a destination, do work, move on. SendComponentMessage is stored at
// character+0x58 (the prior one DeletePtr'd first).
//
// Instruction-faithful transcription of the decompiled exe helper; do NOT "improve"
// behavior.
//
// All disasm citations are exe addresses (base 0x140000000).

#include <new>
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"
#include "Action/PerformWork.h"   // ENDGAME B.3.8 CB3
#include "Action/exe_bodies.h"      // B3R: the action ctor bodies called by name
#include "Game/exe_constants.h"     // B3R: kRdata_*
#include "system/Value.h"           // B3R: RtSystem_Time_Ctor
#include "object/Internal.h"
#include "Component/CitizenAIComponent.h"   // CitizenAIComponent::TypeId() (the message target)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Component/CitizenComponent.h"   // ENDGAME B.3.9 D5
#include "Component/CharacterComponent.h"
#include "Action/Action.h"
#include "Entity/ComponentEntity.h"
#include "Action/SendComponentMessage.h"   // B4 P2 WORK-3: the action ctors by name
#include "Action/CompleteTask.h"
#include "Action/ShowTool.h"
#include "Action/MoveTo.h"
#include "Action/DropOffRawMaterial.h"
#include "Action/PickupStoredRawMaterial.h"

// ============================================================
// Sub-callees, called by name (B3R)
// ============================================================
// Every exe sub-callee is one of our own bodies (Action/, Work/ exe_bodies.h):
//   0x1400c5400 TaskListCleanup                     WorkBase_TaskListCleanup_c5400 (0xbf7f3)
//   0x1400c54e0 TaskListAppend                      Pf_ActStackPush_54e0 (0xbf9f5 etc.)
//   0x1400c29a0 Entity_impl_29a0 (heading, XMM0)    ActChild_Entity29a0_c29a0
//   0x1400012d0 Entity_FindComponent_Family         ActMv_FindComponentFamily_12d0
//   0x1400cb1d0 SendComponentMessage copy-ctor      Pf_SendComponentMessageCopyCtor_b1d0
//   0x1400c5da0 CompleteTask copy-ctor              Pf_CompleteTaskCopyCtor_5da0
//   0x140042890 ShowTool ctor (R8D=toolType)        ActChild_ShowToolCtor_42890
//   0x1400c7cd0 DropOffRawMaterial copy-ctor        ActChild_DropOffRawCopyCtor_c7cd0
//   0x1400ca520 PickupStoredRawMaterial copy-ctor   ActChild_PickupStoredCopyCtor_ca520
//   0x1400c9720 PerformWork_Action_Ctor7            Pf_PerformWorkCtor7_9720
//   0x1400c9170 MoveTo ctor                         Pf_MoveToCtor_9170
// Dispatch slots:
//   0x140152b60 MemAlloc             -> System::MemoryAllocator::MemAlloc
//   0x140152a90 InternalBasePtr ctor -> placement-new Object::InternalBasePtr
//   0x140152b00 System::Time(float)  -> RtSystem_Time_Ctor
//   0x140151ab8 ?DeletePtr@InternalBasePtr@Object@@QEAAXXZ -> Object::InternalBasePtr::DeletePtr
//                                    (Runtime 0x1800c0150, object/Internal.h), by name
// .rdata: 0x140172124 = 0.75f (0xbf962), 0x140172280 = -1.0f MoveTo speed (0xbfbb6).

static inline void* WAP2_Alloc(uint64_t size, uint32_t align) {
    return System::MemoryAllocator::MemAlloc(size, static_cast<int>(align));
}

// 0x1400c54e0 ActionStackPush (Action.h) on CharacterComponent::actionStack (+0x48).
static inline void  WAP2_Append(ActionStack* list, const ::Object::InternalPtr<Action>* ibp) {
    ActionStackPush(list, ibp);
}

// Read a runtime Time qword via System::Time::Time(&buf, seconds); the original reads
// the first qword of the returned Time and forwards it as the action's "time" field.
static inline int64_t WAP2_MakeTime(float seconds) {
    System::Time buf = {0};
    System::Time* r = RtSystem_Time_Ctor(&buf, seconds);
    if (r) return r->_ticks;
    return 0;
}

// ============================================================
// WorkAct_GatherRawMatCtor_bf790  (exe Pathfinding_FindPath @ 0x1400bf790)
// ============================================================
//   retval         = InternalPtr<Action>* hidden return (RCX) — written with the Pickup.
//   citizenRef     = the citizen reference (RDX)
//   pathType       = u32 (R8D, homed; reused as CompleteTask workId)
//   srcRef         = entity reference (R9 — *srcRef = source)
//   srcPos         = float* (stack; DropOff/MoveTo source position / first heading)
//   dstRef         = entity reference (stack; *dstRef = dest — Pickup target)
//   dstPos         = float* (stack; second heading / second MoveTo destination)
//   material       = ComponentDescription* (stack; DropOff/Pickup p4)
//   matCount       = int (stack; DropOff/Pickup p5)
//   completionCode = int (stack; CompleteTask completionCode)
//   useAnimal      = byte (stack; Pickup p6)
//   twoWay         = char (stack; gates pre-CompleteTask + post pickup CompleteTask)
//   flags          = int (stack; low byte gates the optional srcLoc SendComponentMessage)
//   param_14       = byte (stack; PerformWork p6 + selects path/tool variants)
extern "C" ::Object::InternalPtr<Action>* WorkAct_GatherRawMatCtor_bf790(
        ::Object::InternalPtr<Action>* retval, CitizenComponent* const* citizenRef,
        uint32_t pathType, ComponentEntity* const* srcRef,
        float* srcPos, ComponentEntity* const* dstRef, float* dstPos, ComponentDescription* material,
        int matCount, int completionCode, uint8_t useAnimal, char twoWay,
        int flags, uint8_t param_14) {

    // --- the action stack = character + 0x48 ; cleanup pending actions (0xbf7cf-0xbf7f3) ---
    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* list = &character->actionStack;
    list->Clear();

    // Temporaries (mirror the original's stack IBP scratch slots).
    ::Object::InternalPtr<Action> ibpA;
    ::Object::InternalPtr<Action> ibpB;
    ::Object::InternalPtr<Action> ibpC;
    ComponentEntity* ibpInner = nullptr;   // the null InternalBasePtr scratch slot
    void* obj = nullptr;

    // ============================================================
    // SendComponentMessage #1  → stored at character+0x58 (0xbf7f8-0xbf885)
    // ============================================================
    obj = WAP2_Alloc(0x40, 8);
    Action* sm = nullptr;
    if (obj) {
        ibpInner = nullptr;
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        sm = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &ibpInner, 0);
    }
    ibpA = sm;                                            // (0xbf86a)
    // SendComponentMessage handle slot = R14+0x10, where R14 = list = character+0x48,
    // i.e. character+0x58. DeletePtr the prior handle, then store. (0xbf870-0xbf885)
    if (character->actionStack.completionAction != nullptr)   // [0x140151ab8] = InternalBasePtr::DeletePtr
        character->actionStack.completionAction.DeletePtr();   // (0xbf877-0xbf87b)
    character->actionStack.completionAction = ibpA;                                       // (0xbf885)

    // ============================================================
    // optional SendComponentMessage #2 when (char)flags != 0  (0xbf889-0xbf914)
    //   msgId = DWORD_1401c7158 ; innerIbp slot = *srcLoc
    // ============================================================
    if (static_cast<char>(flags) != 0) {
        obj = WAP2_Alloc(0x40, 8);
        Action* sm2 = nullptr;
        if (obj) {
            ComponentEntity* innerSlot = srcRef ? *srcRef : nullptr;     // local_res8 = *srcLoc (0xbf8b0)
            uint32_t msgId = CitizenComponent::TypeId(); // DWORD_1401c7158 (0xbf8b8)
            ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
            sm2 = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerSlot, 0);
        }
        ibpC = sm2;                                       // (0xbf907)
        WAP2_Append(list, &ibpC);                                      // (0xbf914)
    }

    // ============================================================
    // headings: Entity_impl_29a0(srcPos, &*srcLoc) and (dstPos, &*dstLoc) (0xbf919-0xbf94d)
    // ============================================================
    ComponentEntity* headSlot;
    float headingSrc = 0.0f;
    headSlot = srcRef ? *srcRef : nullptr;                             // local_res8 = *srcLoc
    headingSrc = HeadingToward(srcPos, &headSlot); // XMM6 (0xbf929)
    float headingDst = 0.0f;
    headSlot = (dstRef) ? *dstRef : nullptr;                           // local_res8 = *dstLoc (0xbf931)
    headingDst = HeadingToward(dstPos, &headSlot); // XMM8 (0xbf944)

    const float kTime075 = kRdata_140172124; // DAT_140172124 = 0.75f (0xbf962)

    // ============================================================
    // inline PerformWork #1 (heading=headingSrc, actionType=7, time=0.75) (0xbf94d-0xbf9f5)
    // ============================================================
    obj = WAP2_Alloc(0x48, 8);
    Action* pw1 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;  // *(entity) (0xbf96f)
        int64_t timeVal = WAP2_MakeTime(kTime075);                      // System::Time::Time(_,0.75) (0xbf97d)
        // (0xbf989..0xbf9d4) the inlined PerformWork ctor 0x1400c9720 with useTool = 0,
        // +0x44 = -1 — the real ctor.
        pw1 = new (obj) PerformWork(&entityInner, headingSrc, 7, timeVal, 0, -1);
    }
    ibpB = pw1;
    WAP2_Append(list, &ibpB);                                          // (0xbf9f5)

    // ============================================================
    // ShowTool #1 (~Hoe = -1; OR R8D,0xffffffff) (0xbf9fa-0xbfa41)
    // ============================================================
    obj = WAP2_Alloc(0x30, 8);                                         // size 0x30 (0xbf9ff LEA ECX,[RDX+0x28]; ECX=8+0x28)
    Action* st1 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        st1 = new (obj) ShowTool(&entityInner, -1); // R8D = -1 (0xbfa18)
    }
    ibpB = st1;
    WAP2_Append(list, &ibpB);                                          // (0xbfa41)

    // ============================================================
    // pre-CompleteTask when twoWay == 0 (0xbfa46-0xbfab7)
    //   workId = pathType (homed slot), completionCode = completionCode
    // ============================================================
    if (twoWay == 0) {
        obj = WAP2_Alloc(0x38, 8);
        Action* ct = nullptr;
        if (obj) {
            ibpInner = nullptr;                          // (0xbfa6f)
            CitizenComponent* entityInner = *citizenRef; // *(entity) (0xbfa75)
            ct = new (obj) CompleteTask(pathType, &entityInner, &ibpInner,
                                        static_cast<uint32_t>(completionCode)); // (0xbfa94)
        }
        ibpC = ct;
        WAP2_Append(list, &ibpC);                                      // (0xbfab2)
    }

    // ============================================================
    // DropOffRawMaterial (0xbfab7-0xbfb1c)
    //   p2 = &*entity (citizen, 0xbfad6→RDX@0xbfaf7), p3 = &*srcLoc (dropoff
    //   target, 0xbface→R8@0xbfaf3), p4 = materialDesc, p5 = matCount, p6 = 1
    //   (disasm-verified 2026-07-20; an earlier comment here had p2/p3 swapped)
    // ============================================================
    obj = WAP2_Alloc(0x40, 8);                                         // size 0x40 (0xbfabc LEA ECX,[RDX+0x38])
    Action* drop = nullptr;
    if (obj) {
        ComponentEntity* srcInner = srcRef ? *srcRef : nullptr;        // local_res8 = *srcLoc (0xbface)
        CitizenComponent* entInner = *citizenRef;                      // local_res10 = *entity (0xbfad6)
        drop = new (obj) DropOffRawMaterial(&entInner, &srcInner, material,
                                            static_cast<uint32_t>(matCount), 1);  // p6=1 (0xbfadd), 0x1400c7cd0
    }
    ibpC = drop;
    WAP2_Append(list, &ibpC);                                          // (0xbfb1c)

    // ============================================================
    // PerformWork_Ctor7 #1 (heading=headingSrc, actionType=6, time=0.75,
    //   p6=param_14, p7=-1) (0xbfb21-0xbfb9a)
    // ============================================================
    obj = WAP2_Alloc(0x48, 8);                                         // size 0x48 (0xbfb26 LEA ECX,[RDX+0x40])
    Action* pw7a = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        int64_t timeVal = WAP2_MakeTime(kTime075);                      // (0xbfb51)
        pw7a = new (obj) PerformWork(&entityInner, headingSrc, 6 /*R9D=6*/,
                                       timeVal, param_14 != 0 /*R12B; the forwarder's ctor tested != 0*/, -1 /*[RSP+0x30]=-1*/); // (0xbfb7c)
    }
    ibpA = pw7a;
    WAP2_Append(list, &ibpA);                                          // (0xbfb9a)

    // ============================================================
    // MoveTo #1 (destination=srcPos, pathType = (param_14!=0)?5:4) (0xbfb9f-0xbfc40)
    // ============================================================
    const float kSpeed = kRdata_140172280;   // DAT_140172280 = -1.0f (0xbfbb6)
    obj = WAP2_Alloc(0x480, 8);
    Action* mv1 = nullptr;
    if (obj) {
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();             // *((*entity)+0x10) (0xbfbcc-0xbfbd7)
        int pathT = 4 + (param_14 != 0 ? 1 : 0);                       // LEA R8D,[RBX+4], RBX=SETNZ (0xbfbe7-0xbfc1b)
        CharacterComponent* family = entityInner->FindComponent<CharacterComponent>(); // 0x1400012d0 (0xbfbf5)
        mv1 = new (obj) MoveTo(&family, pathT, 0, srcPos, PathBits::Walk, 0, kSpeed); // (0xbfc22)
    }
    ibpB = mv1;
    WAP2_Append(list, &ibpB);                                          // (0xbfc40)

    // ============================================================
    // inline PerformWork #2 (heading=headingDst, actionType=7, time=0.75) (0xbfc45-0xbfce5)
    // ============================================================
    obj = WAP2_Alloc(0x48, 8);                                         // size 0x48 (0xbfc4a LEA ECX,[RDX+0x40])
    Action* pw2 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;  // *(entity) (0xbfc5f)
        int64_t timeVal = WAP2_MakeTime(kTime075);                      // (0xbfc6d)
        // (0xbfc79..0xbfcc4) the inlined PerformWork ctor 0x1400c9720 with useTool = 0,
        // +0x44 = -1 — the real ctor.
        pw2 = new (obj) PerformWork(&entityInner, headingDst, 7, timeVal, 0, -1);
    }
    ibpB = pw2;
    WAP2_Append(list, &ibpB);                                          // (0xbfce5)

    // ============================================================
    // ShowTool #2 (param_14!=0 ? 0xa : -2) (0xbfcea-0xbfd3f)
    // ============================================================
    obj = WAP2_Alloc(0x30, 8);                                         // size 0x30 (0xbfcef LEA ECX,[RDX+0x28])
    Action* st2 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        int toolType = (param_14 != 0) ? 0xa : -2;                     // MOV R8D,-2; CMOVNZ R8D,0xa (0xbfd0d-0xbfd16)
        st2 = new (obj) ShowTool(&entityInner, toolType);
    }
    ibpB = st2;
    WAP2_Append(list, &ibpB);                                          // (0xbfd3f)

    // ============================================================
    // PickupStoredRawMaterial → written to retval (0xbfd44-0xbfdb9)
    //   p2 = &*entity, p3 = &*dstLoc, p4 = materialDesc, p5 = matCount, p6 = useAnimal
    // ============================================================
    obj = WAP2_Alloc(0x48, 8);                                         // size 0x48 (0xbfd44 LEA ECX,[RDX+0x40])
    Action* pick = nullptr;
    if (obj) {
        ComponentEntity* dstInner = (dstRef) ? *dstRef : nullptr;              // *dstLoc (0xbfd5b)
        CitizenComponent* entInner = *citizenRef;                              // *(entity) (0xbfd66)
        pick = new (obj) PickupStoredRawMaterial(&entInner, &dstInner, material,
                                                 matCount, useAnimal);  // p6=useAnimal (0xbfd94), 0x1400ca520
    }
    ::Object::InternalPtr<Action>* ret = new (retval) ::Object::InternalPtr<Action>(pick);   // ctor into the hidden return slot (0xbfda4)
    ::Object::InternalPtr<Action> retInner = *ret;                     // dstLoc = *retval (0xbfdaa)
    WAP2_Append(list, &retInner);                                     // (0xbfdb9)

    // ============================================================
    // post-Pickup CompleteTask when twoWay != 0 (0xbfdbe-0xbfe2a)
    // ============================================================
    if (twoWay != 0) {
        obj = WAP2_Alloc(0x38, 8);                                     // size 0x38 (0xbfdcc LEA ECX,[RDX+0x30])
        Action* ct = nullptr;
        if (obj) {
            ibpInner = nullptr;                          // (0xbfde7)
            CitizenComponent* entityInner = *citizenRef;
            ct = new (obj) CompleteTask(pathType, &entityInner, &ibpInner,
                                        static_cast<uint32_t>(completionCode)); // R8=&entityInner@0xbfe02, R9=&ibpInner@0xbfdfe
        }
        ibpB = ct;
        WAP2_Append(list, &ibpB);                                      // (0xbfe2a)
    }

    // ============================================================
    // PerformWork_Ctor7 #2 (heading=headingDst, actionType=6, time=0.75,
    //   p6=param_14, p7=-1) (0xbfe2f-0xbfea1)
    // ============================================================
    obj = WAP2_Alloc(0x48, 8);                                         // size 0x48 (0xbfe34 LEA ECX,[RDX+0x40])
    Action* pw7b = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;  // *(entity) (0xbfe49)
        int64_t timeVal = WAP2_MakeTime(kTime075);                      // (0xbfe57)
        pw7b = new (obj) PerformWork(&entityInner, headingDst, 6 /*R9D=6*/,
                                       timeVal, param_14 != 0 /*R12B; the forwarder's ctor tested != 0*/, -1); // (0xbfe83)
    }
    ibpA = pw7b;
    WAP2_Append(list, &ibpA);                                          // (0xbfea1)

    // ============================================================
    // MoveTo #2 (destination=dstPos, pathType = (param_14!=0)?5:1) (0xbfea6-0xbff46)
    // ============================================================
    obj = WAP2_Alloc(0x480, 8);
    Action* mv2 = nullptr;
    if (obj) {
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();             // *((*entity)+0x10) (0xbfecb-0xbfed6)
        int pathT = (param_14 != 0) ? 5 : 1;                           // MOV EBX,1; MOV EAX,5; CMOVNZ (0xbfee4-0xbfef1)
        CharacterComponent* family = entityInner->FindComponent<CharacterComponent>(); // 0x1400012d0 (0xbfefc)
        mv2 = new (obj) MoveTo(&family, pathT, 0, dstPos, PathBits::Walk, 0, kSpeed); // (0xbff28)
    }
    ibpB = mv2;
    WAP2_Append(list, &ibpB);                                          // (0xbff46)

    // ============================================================
    // optional ShowTool #3 when param_14 != 0 (toolType=9) (0xbff4b-0xbff97)
    // ============================================================
    if (param_14 != 0) {
        obj = WAP2_Alloc(0x30, 8);                                     // size 0x30 (0xbff55 LEA ECX,[RDX+0x28])
        Action* st3 = nullptr;
        if (obj) {
            CitizenComponent* entityInner = *citizenRef;
            st3 = new (obj) ShowTool(&entityInner, 9); // R8D=9 (0xbff6e)
        }
        ibpB = st3;
        WAP2_Append(list, &ibpB);                                      // (0xbff97)
    }

    return retval;                                                     // RAX = R13 = retval (0xbff9c)
}
