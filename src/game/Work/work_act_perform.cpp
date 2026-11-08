// src/game/Work/work_act_perform.cpp
// PerformWork work-action-queue builders (Work action-ctor sub-batch).
//
// Faithful 1:1 ports of two exe helpers that build a citizen's work-action chain
// and append it to the citizen's action stack (CitizenComponent +0x278 -> its
// CharacterComponent, stack at +0x48; the SendComponentMessage action is stored at +0x58
#include "Work/exe_bodies.h"
// after DeletePtr-ing the prior one). Each appended sub-action is allocated via the
// MemAlloc static slot (0x140152b60) and wrapped in an InternalBasePtr before being
// appended (FUN_1400c54e0) — exactly as the disassembly does.
//
//   1. PerformWork_Action_CopyCtor (0x1400ba9f0) -> WorkAct_PerformWorkCopyCtor_ba9f0
//   2. PerformWork_Action_Ctor     (0x1400bffd0) -> WorkAct_PerformWorkCtor_bffd0
//
// This is a REVERSE-ENGINEERING transcription of the disassembly; do NOT "improve"
// behavior. Every offset/size/immediate below is annotated with its disasm address.
// LEFT: the callers (BuildRoad, ClearArea, FireWork in WORK-a / WORK-b) hand the
// citizen reference as void* and the road destination as uint32_t*; the action ctors
// (ACT's C bodies) take their references as uint64_t*.
//

#include <new>
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"
#include "Action/PerformWork.h"   // ENDGAME B.3.8 CB3
#include "math/Math.h"
#include "Action/exe_bodies.h"      // B3R: the action ctor bodies called by name
#include "Component/exe_bodies.h"   // B3R: (unused since B6 block D: the QueryCount callers call the member)
#include "Game/exe_constants.h"     // B3R: kRdata_*
#include "system/Value.h"           // B3R: RtSystem_Time_Ctor
#include "object/Internal.h"
#include <cstring>
#include "Component/CitizenAIComponent.h"   // CitizenAIComponent::TypeId() (the message target)
#include <cstdint>
#include "Component/FireComponent.h"   // ENDGAME B.3.9 D5
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/StorageComponent.h"
#include "Action/Action.h"
#include "Entity/ComponentEntity.h"
#include "Action/SendComponentMessage.h"   // B4 P2 WORK-3: the action ctors by name
#include "Action/CompleteTask.h"
#include "Action/ShowTool.h"
#include "Action/MoveTo.h"
#include "Action/CompleteRoad.h"
#include "Action/ConsumeRawMaterial.h"
#include "Action/PickupStoredRawMaterial.h"

// ============================================================
// Sub-callees, called by name (B3R)
// ============================================================
// Every exe sub-callee is one of our own bodies (Action/, Work/, Component/ exe_bodies.h):
//   0x1400c29a0 Entity_impl_29a0 (heading, XMM0)      ActChild_Entity29a0_c29a0
//   0x1400c5400 TaskListCleanup                       WorkBase_TaskListCleanup_c5400
//   0x1400c54e0 TaskListAppend                        Pf_ActStackPush_54e0
//   0x1400012d0 Entity_FindComponent_Family           ActMv_FindComponentFamily_12d0
//   0x140006270 FindComponent<StorageComponent>
//   0x14012cf10 StorageComponent_QueryCount           StorageComponent::QueryCount (the member)
//   0x1400cb1d0 SendComponentMessage copy-ctor        Pf_SendComponentMessageCopyCtor_b1d0
//   0x1400c5da0 CompleteTask copy-ctor                Pf_CompleteTaskCopyCtor_5da0
//   0x140042890 ShowTool ctor (R8D=toolType)          ActChild_ShowToolCtor_42890
//   0x1400c59d0 CompleteRoad copy-ctor                ActChild_CompleteRoadCopyCtor_c59d0
//   0x1400c63b0 ConsumeRawMaterial copy-ctor (5 args) ActChild_ConsumeRawCopyCtor_c63b0
//   0x1400ca520 PickupStoredRawMaterial copy-ctor     ActChild_PickupStoredCopyCtor_ca520
//   0x1400c9720 PerformWork_Action_Ctor7              Pf_PerformWorkCtor7_9720
//               (RCX=self, RDX=&entityInner, XMM2=heading, R9D=actionType, [stack]=time,
//                byte, int -- verified via disassembly)
//   0x1400c9170 MoveTo ctor                           Pf_MoveToCtor_9170
// Dispatch slots:
//   0x140152a90 InternalBasePtr ctor -> placement-new Object::InternalBasePtr
//   0x140152b60 MemAlloc             -> System::MemoryAllocator::MemAlloc
//   0x140152b00 System::Time(float)  -> RtSystem_Time_Ctor
//   0x140151ab8 ?DeletePtr@InternalBasePtr@Object@@QEAAXXZ -> Object::InternalBasePtr::DeletePtr
//                                    (Runtime 0x1800c0150, object/Internal.h), by name
// 0x1401512c0 Normalize / 0x1401512d8 Heading / 0x140152a30 Vector3 ctor are direct
// Math::Vector3 calls (B.3.1 step 2; see reference_byvalue_return_hidden_rdx).
// .rdata (src/game/Game/exe_constants.h): 0x140172124 = 0.75f (PerformWork Time #1),
// 0x140172188 = 3.5f (ba9f0 MoveTo speed), 0x140172194 = 4.0f (bffd0 Ctor7 Time),
// 0x140172280 = -1.0f (bffd0 MoveTo speed), 0x1401722a8 = -1000.0f (bffd0 Ctor7 heading).
// 0x1401c41e0 = FireComponent (ENDGAME B.3.9 D5: read directly below).

static inline float WAP_RdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }

// Small idiom wrappers matching the disasm's call shapes.
static inline void* WAP_Alloc(uint64_t size, uint32_t align) {
    return System::MemoryAllocator::MemAlloc(size, static_cast<int>(align));
}
// 0x1400c54e0 ActionStackPush (Action.h) on CharacterComponent::actionStack (+0x48).
static inline void  WAP_Append(ActionStack* list, const ::Object::InternalPtr<Action>* ibp) {
    ActionStackPush(list, ibp);
}

// The inline PerformWork actions below are built with the real ctor 0x1400c9720 (useTool = 0,
// +0x44 = -1) placed in their 0x48-byte MemAlloc block (already non-null).
// Mirrors the repeated inline block at 0x1400babb1.. / 0x1400c0408.. etc.
//   entityInner  -> [o+0x20]    (citizen inner)
//   heading      -> [o+0x28]    (float)
//   actionType   -> [o+0x2c]    (int, 0x6 or 0x7)
//   timeVal      -> [o+0x30]    (int64 from System::Time::Time)
//   [o+0x44]     = 0xffffffff

// ============================================================
// PerformWork_Action_CopyCtor (0x1400ba9f0) -> WorkAct_PerformWorkCopyCtor_ba9f0
// ============================================================
// Builds the forester/clear-area work-action chain for a citizen and stores it on the
// citizen's action stack (CharacterComponent +0x48).
//   citizenIBP = InternalBasePtr*           (RCX)
//   workId     = u32                         (EDX -> CompleteTask workId)
//   pos3       = float*                      (R8, param_3: Vector3 minuend src + 2nd MoveTo dest)
//   pos4       = float*                      (R9, param_4: Vector3 subtrahend)
//   pos5       = float*                      (stack, param_5: Entity_impl_29a0 worldPos + 1st MoveTo dest)
//   srcEntity6 = longlong*                   (stack, param_6: 2nd SendComponentMessage entity inner)
void WorkAct_PerformWorkCopyCtor_ba9f0(CitizenComponent* const* citizenRef, uint32_t workId,
                                       float* pos3, float* pos4, float* pos5,
                                       ComponentEntity* const* srcRef6) {

    // the action stack = character + 0x48 ; cleanup (0x1400baa2f..0x1400baa53)
    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* list = &character->actionStack;
    list->Clear();

    ::Object::InternalPtr<Action> ibpA;
    ::Object::InternalPtr<Action> ibpB;
    ::Object::InternalPtr<Action> ibpC;

    // --- SendComponentMessage #1 → character+0x58 (DeletePtr old) (0x1400baa58..0x1400baae5) ---
    void* obj = WAP_Alloc(0x40, 8);  // 0x1400baa5d: ECX=0x38+8=0x40
    Action* sm = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        sm = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0); // 0x1400cb1d0
    }
    ibpA = sm;
    // 0x1400baad0: CMP [R15+0x10] — R15 = character+0x48, so this is (character+0x48)+0x10 = character+0x58.
    if (character->actionStack.completionAction != nullptr)   // [0x140151ab8] = InternalBasePtr::DeletePtr
        character->actionStack.completionAction.DeletePtr();
    character->actionStack.completionAction = ibpA; // 0x1400baae5: MOV [R15+0x10],RAX

    // --- heading via Entity_impl_29a0(pos5, &{*param_6}) (0x1400baae9..0x1400bab02; XMM6) ---
    ComponentEntity* srcInner6 = srcRef6 ? *srcRef6 : nullptr; // 0x1400baaed: MOV RAX,[R13]; [RBP+0x7f]=RAX
    float headingA = 0.0f; // XMM6
    headingA = HeadingToward(pos5, &srcInner6);

    // --- headingB via Vector3(pos4-pos3) → Normalize → Heading (0x1400bab05..0x1400bab46; XMM8) ---
    float headingB = 0.0f; // XMM8
    if (pos3 && pos4) {
        // 0x1400bab05: XMM = [pos4+k] - [pos3+k]  (k = 8,4,0 → z,y,x)
        Math::Vector3 v3(pos4[0] - pos3[0], pos4[1] - pos3[1], pos4[2] - pos3[2]);
        Math::Vector3 nv = v3.Normalize();   // was the hidden RDX return slot
        headingB = nv.Heading();
    }

    float timeC = kRdata_140172124; // XMM7 = DAT_140172124 = 0.75f

    // --- inline PerformWork #1: heading=headingA(XMM6), actionType=7, time=Time(0.75) (0x1400bab4a..) ---
    obj = WAP_Alloc(0x48, 8); // 0x1400bab4f: ECX=0x40+8=0x48
    Action* pw1 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef; // 0x1400bab6c: [R14]
        System::Time timeVal = {0};
        { System::Time* t = RtSystem_Time_Ctor(&timeVal, timeC); if (t) timeVal = *t; }
        pw1 = new (obj) PerformWork(&entityInner, headingA, 0x7, timeVal._ticks, 0, -1); // 0x2c=7
    }
    ibpA = pw1;
    WAP_Append(list, &ibpA);

    // --- ShowTool (~Hoe = -1) (0x1400babf2..0x1400bac39) ---
    obj = WAP_Alloc(0x30, 8); // 0x1400babf7: ECX=0x28+8=0x30
    Action* st1 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        st1 = new (obj) ShowTool(&entityInner, ~0 /* 0x1400bac10: OR R8D,0xffffffff = -1 */);
    }
    ibpA = st1;
    WAP_Append(list, &ibpA);

    // --- CompleteTask (workId) (0x1400bac3e..0x1400bac9b) ---
    obj = WAP_Alloc(0x38, 8); // 0x1400bac43: ECX=0x30+8=0x38
    Action* ct = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        CitizenComponent* entityInner = *citizenRef; // 0x1400bac64: [R14]
        ct = new (obj) CompleteTask(workId, &entityInner, &innerIbp, 0); // EDX=R12D=workId, [RSP+0x20]=0
    }
    ibpB = ct;
    WAP_Append(list, &ibpB);

    // --- SendComponentMessage #2 (msgId=DWORD_1401c41e0, entity=*param_6) (0x1400baca0..0x1400bad10) ---
    obj = WAP_Alloc(0x40, 8); // 0x1400baca5: ECX=0x38+8=0x40
    Action* sm2 = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = FireComponent::TypeId(); // 0x1401c41e0
        ComponentEntity* entityInner = srcRef6 ? *srcRef6 : nullptr; // 0x1400baccf: MOV RCX,[R13]
        sm2 = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0); // 0x1400cb1d0
    }
    ibpC = sm2;
    WAP_Append(list, &ibpC);

    // --- inline PerformWork #2: heading=headingA(XMM6), actionType=6, time=Time(0.75) (0x1400bad15..) ---
    obj = WAP_Alloc(0x48, 8); // 0x1400bad1a: ECX=0x40+8=0x48
    Action* pw2 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        System::Time timeVal = {0};
        { System::Time* t = RtSystem_Time_Ctor(&timeVal, timeC); if (t) timeVal = *t; }
        pw2 = new (obj) PerformWork(&entityInner, headingA, 0x6, timeVal._ticks, 0, -1); // 0x1400bad7d: 0x2c=6
    }
    ibpA = pw2;
    WAP_Append(list, &ibpA);

    float speed = WAP_RdataF(kRdata_140172188); // XMM6 = DAT_140172188 = 3.5f

    // --- MoveTo #1 (Faster=4, dest=pos5) (0x1400badc3..0x1400bae5c) ---
    obj = WAP_Alloc(0x480, 8); // 0x1400badc8: ECX=0x480
    Action* mv1 = nullptr;
    if (obj) {
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        CharacterComponent* family = entityInner->FindComponent<CharacterComponent>();   // 0x1400012d0 // 0x1400012d0
        // 0x1400bae37: LEA R8D,[R9+4] (R9=0) → pathType=4 (Faster); [RSP+0x28]=0xe1f; [RSP+0x30]=0; XMM=speed
        mv1 = new (obj) MoveTo(&family, 4, 0, pos5, PathBits::Walk, 0, speed);
    }
    ibpA = mv1;
    WAP_Append(list, &ibpA);

    // --- inline PerformWork #3: heading=headingB(XMM8), actionType=7, time=Time(0.75) (0x1400bae61..) ---
    obj = WAP_Alloc(0x48, 8); // 0x1400bae66: ECX=0x40+8=0x48
    Action* pw3 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        System::Time timeVal = {0};
        { System::Time* t = RtSystem_Time_Ctor(&timeVal, timeC); if (t) timeVal = *t; }
        pw3 = new (obj) PerformWork(&entityInner, headingB, 0x7, timeVal._ticks, 0, -1); // 0x1400baebc: 0x2c=7, XMM8
    }
    ibpA = pw3;
    WAP_Append(list, &ibpA);

    // --- ShowTool (Barrow = 8) (0x1400baef4..0x1400baf3d) ---
    obj = WAP_Alloc(0x30, 8); // 0x1400baef9: ECX=0x28+8=0x30
    Action* st2 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        st2 = new (obj) ShowTool(&entityInner, 8 /* 0x1400baf12: MOV R8D,8 (Barrow) */);
    }
    ibpA = st2;
    WAP_Append(list, &ibpA);

    // --- inline PerformWork #4: heading=headingB(XMM8), actionType=6, time=Time(0.75) (0x1400baf42..) ---
    obj = WAP_Alloc(0x48, 8); // 0x1400baf47: ECX=0x40+8=0x48
    Action* pw4 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        System::Time timeVal = {0};
        { System::Time* t = RtSystem_Time_Ctor(&timeVal, timeC); if (t) timeVal = *t; }
        pw4 = new (obj) PerformWork(&entityInner, headingB, 0x6, timeVal._ticks, 0, -1); // 0x1400baf9d: 0x2c=6, XMM8
    }
    ibpA = pw4;
    WAP_Append(list, &ibpA);

    // --- MoveTo #2 (Slow=1, dest=pos3) (0x1400bafd5..0x1400bb064) ---
    obj = WAP_Alloc(0x480, 8); // 0x1400bafda: ECX=0x480
    Action* mv2 = nullptr;
    if (obj) {
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        CharacterComponent* family = entityInner->FindComponent<CharacterComponent>();   // 0x1400012d0
        // 0x1400bb041: LEA R8D,[R9+1] (R9=0) → pathType=1 (Slow); dest=pos3 ([RBP+0x67]); XMM6=speed
        mv2 = new (obj) MoveTo(&family, 1, 0, pos3, PathBits::Walk, 0, speed);
    }
    ibpA = mv2;
    WAP_Append(list, &ibpA);

    (void)ibpB; (void)ibpC; // appended above
}

// ============================================================
// PerformWork_Action_Ctor (0x1400bffd0) -> WorkAct_PerformWorkCtor_bffd0
// ============================================================
// Builds the road/raw-material work-action chain for a citizen and stores it on the
// citizen's action stack (CharacterComponent +0x48).
//   citizenRef = InternalPtr<CitizenComponent>* (RCX) param_1
//   workId     = u32   (EDX = R15D)                 param_2  -> CompleteTask workId
//   destination = float* (R8)                       param_3  -> MoveTo#1 destination
//   roadP4     = int64*/int (R9D = R12D)            param_4  -> CompleteRoad p3 / ConsumeRawMaterial p4 byte chain
//   completeRoadP5 = u32                            param_5  -> CompleteTask p5
//   roadByte6  = byte                               param_6  -> CompleteRoad p4 byte
//   material   = ComponentDescription*              param_8  -> ConsumeRawMaterial cond + arg
//   loopCount9 = int                                param_9  -> optional pickup-chain count gate
//   pos10      = float*                             param_10 -> Entity_impl_29a0 worldPos + MoveTo#2 dest
//   srcRef11   = InternalPtr<ComponentEntity>*      param_11 -> pickup chain entity inner
//
// NOTE: param_3 is the home [RBP+0x57] (DWORD read as msg/road id) and ALSO reused as the
// MoveTo#1 destination float* (passed via the RCX home pointer). param_7 = completeRoadP5
// is the CompleteTask p5; in the disasm CompleteTask reads its p5 from [RBP+0x67] (param_7).
void WorkAct_PerformWorkCtor_bffd0(CitizenComponent* const* citizenRef, uint32_t workId, float* destination,
                                   int roadP4, uint32_t completeRoadP5, uint8_t roadByte6,
                                   uint32_t completeTaskP7, ComponentDescription* consumeMaterial, int loopCount9,
                                   float* pos10, ComponentEntity* const* srcRef11) {

    // the action stack = character + 0x48 ; cleanup (0x1400c0010..0x1400c0034)
    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* list = &character->actionStack;  // RSI = character+0x48
    list->Clear();

    ::Object::InternalPtr<Action> ibpA;
    ::Object::InternalPtr<Action> ibpB;
    ::Object::InternalPtr<Action> ibpC;

    // --- SendComponentMessage #1 → character+0x58 (DeletePtr old) (0x1400c0039..0x1400c00c6) ---
    void* obj = WAP_Alloc(0x40, 8); // 0x1400c003e: ECX=0x38+8=0x40
    Action* sm = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        sm = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0); // 0x1400cb1d0
    }
    ibpA = sm;
    // 0x1400c00b1: CMP [RSI+0x10] — RSI = character+0x48 → character+0x58.
    if (character->actionStack.completionAction != nullptr)   // [0x140151ab8] = InternalBasePtr::DeletePtr
        character->actionStack.completionAction.DeletePtr();
    character->actionStack.completionAction = ibpA; // 0x1400c00c6: MOV [RSI+0x10],RAX

    // --- CompleteTask (workId, p5=completeTaskP7) (0x1400c00ca..0x1400c012a) ---
    obj = WAP_Alloc(0x38, 8); // 0x1400c00cf: ECX=0x30+8=0x38
    Action* ct = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        CitizenComponent* entityInner = *citizenRef; // 0x1400c00f0: [R14]
        // 0x1400c00f7: ECX=[RBP+0x67] (param_7) → [RSP+0x20]=p5 ; EDX=R15D=workId
        ct = new (obj) CompleteTask(workId, &entityInner, &innerIbp, completeTaskP7);
    }
    ibpB = ct;
    WAP_Append(list, &ibpB);

    // --- CompleteRoad (p2=param_5, p3=param_4(int), p4=param_6 byte) (0x1400c012f..0x1400c0172) ---
    obj = WAP_Alloc(0x30, 8); // 0x1400c0134: ECX=0x28+8=0x30
    Action* cr = nullptr;
    if (obj) {
        // 0x1400c0146: R9D=byte[RBP+0x5f]=param_6 ; R8D=R12D=param_4 ; EDX=[RBP+0x57]=param_5
        cr = new (obj) CompleteRoad(completeRoadP5, static_cast<uint32_t>(roadP4), roadByte6); // 0x1400c59d0
    }
    ibpA = cr;
    WAP_Append(list, &ibpA);

    // --- ShowTool (~Hoe = -1) (0x1400c0177..0x1400c01be) ---
    obj = WAP_Alloc(0x30, 8); // 0x1400c017c: ECX=0x28+8=0x30
    Action* st1 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef; // 0x1400c018e: [R14]
        st1 = new (obj) ShowTool(&entityInner, ~0 /* 0x1400c0195: OR R8D,0xffffffff = -1 */);
    }
    ibpC = st1;
    WAP_Append(list, &ibpC);

    // --- optional ConsumeRawMaterial when param_8 != 0 (0x1400c01c3..0x1400c0226) ---
    if (consumeMaterial != nullptr) {
        obj = WAP_Alloc(0x40, 8); // 0x1400c01cf: MOV EDX,8; LEA ECX,[RDX+0x38] = 0x40
                                  // (was mis-transcribed 0x38 — same LEA-operand misread as
                                  // the Ctor7 0x40/0x48 bug. The ConsumeRawMaterial ctor
                                  // (0x1400c63b0) writes byte +0x38, so 0x38 overflowed by
                                  // 1 byte. Only stone roads construct this child (material
                                  // != null), hence dirt roads never crashed. Redzone
                                  // fingerprint: canary head 0x01, reqSize 0x38, alloc site
                                  // bffd0+0x3ff, 2026-07-26.)
        Action* crm = nullptr;
        if (obj) {
            ComponentEntity* entityInner = (*citizenRef)->_entity.Get(); // 0x1400c01ed
            // 0x1400c01f8: byte[RSP+0x20]=0 (p5, the FIFTH argument) ; R8=[RBP+0x6f]=param_8 (p3) ;
            // RDX=&entityInner ; R9 not written (p4, unused by the body).
            crm = new (obj) ConsumeRawMaterial(&entityInner, consumeMaterial, 0); // 0x1400c63b0
        }
        ibpA = crm;
        WAP_Append(list, &ibpA);
    }

    // --- PerformWork_Ctor7: heading(XMM2)=-1000(DAT_1722a8), actionType(R9)=0xA,
    //     time=Time(4.0=DAT_172194), p6=1, p7=-1 (0x1400c022b..0x1400c02a6) ---
    obj = WAP_Alloc(0x48, 8); // 0x1400c0230: MOV EDX,8; LEA ECX,[RDX+0x40] = 0x48
                              // (was mis-transcribed as 0x40; Ctor7 writes byte +0x40 and
                              // int +0x44, so 0x40 overflowed the block by 8 bytes — the
                              // road-build heap corruption, caught by redzone PAD+SCAN)
    Action* pw = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef; // 0x1400c0245: [R14]
        float timeF = kRdata_140172194; // 0x1400c024c: XMM1=DAT_140172194=4.0f
        System::Time timeVal = {0};
        { System::Time* t = RtSystem_Time_Ctor(&timeVal, timeF); if (t) timeVal = *t; }
        float headingC = WAP_RdataF(kRdata_1401722a8); // 0x1400c0279: XMM2=DAT_1401722a8=-1000.0f
        // 0x1400c0273: R9D=0xa ; [RSP+0x30]=0xffffffff (p7=-1) ; byte[RSP+0x28]=1 (p6) ; [RSP+0x20]=time
        pw = new (obj) PerformWork(&entityInner, headingC, 0xA, timeVal._ticks, 1, -1);
    }
    ibpA = pw;
    WAP_Append(list, &ibpA);

    float speed = kRdata_140172280; // 0x1400c02c2: XMM8=DAT_140172280=-1.0f

    // --- MoveTo #1 (Fast=2, dest=param_3) (0x1400c02ab..0x1400c0342) ---
    obj = WAP_Alloc(0x480, 8); // 0x1400c02b0: ECX=0x480
    Action* mv1 = nullptr;
    if (obj) {
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
        CharacterComponent* family = entityInner->FindComponent<CharacterComponent>();   // 0x1400012d0
        // 0x1400c031d: LEA R8D,[R9+2] (R9=0) → pathType=2 (Fast); dest=param_3 (R13 at entry); XMM8=speed
        mv1 = new (obj) MoveTo(&family, 2, 0, destination, PathBits::Walk, 0, speed);
    }
    ibpA = mv1;
    WAP_Append(list, &ibpA);

    // --- ShowTool (Axe = 1) (0x1400c0347..0x1400c0393) ---
    obj = WAP_Alloc(0x30, 8); // 0x1400c034c: ECX=0x28+8=0x30
    Action* st2 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;
        // 0x1400c0359: R13D=1 ; 0x1400c036b: R8D=R13D=1 (Axe)
        st2 = new (obj) ShowTool(&entityInner, 1 /* Axe */);
    }
    ibpA = st2;
    WAP_Append(list, &ibpA);

    // --- optional pickup chain when param_9 > 0 (0x1400c0398..0x1400c0653) ---
    if (loopCount9 > 0) {
        // heading via Entity_impl_29a0(param_10, &{*param_11}) (0x1400c03a2..0x1400c03be; XMM6)
        ComponentEntity* srcInner11 = srcRef11 ? *srcRef11 : nullptr; // 0x1400c03a9: MOV RAX,[R12]
        float headingP = 0.0f;
        headingP = HeadingToward(pos10, &srcInner11);

        float timeC = kRdata_140172124; // 0x1400c03e2: XMM1=DAT_140172124=0.75f

        // inline PerformWork A: heading=headingP(XMM6), actionType=7, time=Time(0.75) (0x1400c03c1..)
        obj = WAP_Alloc(0x48, 8); // 0x1400c03c6: ECX=0x40+8=0x48
        Action* pwA = nullptr;
        if (obj) {
            CitizenComponent* entityInner = *citizenRef;
            System::Time timeVal = {0};
            { System::Time* t = RtSystem_Time_Ctor(&timeVal, timeC); if (t) timeVal = *t; }
            pwA = new (obj) PerformWork(&entityInner, headingP, 0x7, timeVal._ticks, 0, -1); // 0x1400c0430: 0x2c=7
        }
        ibpA = pwA;
        WAP_Append(list, &ibpA);

        // PickupStoredRawMaterial (entity, *param_11, param_8, -1, 1) (0x1400c046b..0x1400c04cb)
        obj = WAP_Alloc(0x48, 8); // 0x1400c0470: ECX=0x40+8=0x48
        Action* pickup = nullptr;
        if (obj) {
            ComponentEntity* srcInner = srcRef11 ? *srcRef11 : nullptr; // 0x1400c0482: MOV RCX,[R12]
            CitizenComponent* entityInner = *citizenRef; // 0x1400c048a: [R14]
            // 0x1400c0491: byte[RSP+0x28]=1 (p6) ; [RSP+0x20]=0xffffffff (p5=-1) ; R9=[RBP+0x6f]=param_8 (p4) ;
            //   R8=&srcInner ; RDX=&entityInner.
            pickup = new (obj) PickupStoredRawMaterial(&entityInner, &srcInner, consumeMaterial, -1, 1); // 0x1400ca520
        }
        ibpB = pickup;
        WAP_Append(list, &ibpB);

        // inline PerformWork B: heading=headingP(XMM6), actionType=6, time=Time(0.75) (0x1400c04d0..)
        obj = WAP_Alloc(0x48, 8); // 0x1400c04d5: ECX=0x40+8=0x48
        Action* pwB = nullptr;
        if (obj) {
            CitizenComponent* entityInner = *citizenRef;
            System::Time timeVal = {0};
            { System::Time* t = RtSystem_Time_Ctor(&timeVal, timeC); if (t) timeVal = *t; }
            pwB = new (obj) PerformWork(&entityInner, headingP, 0x6, timeVal._ticks, 0, -1); // 0x1400c053f: 0x2c=6
        }
        ibpA = pwB;
        WAP_Append(list, &ibpA);

        // MoveTo #2: pathType = (StorageComponent::QueryCount > 0) ? Faster(4) : Slow(1); dest=param_10 (0x1400c057a..0x1400c0653)
        obj = WAP_Alloc(0x480, 8); // 0x1400c057f: ECX=0x480
        Action* mv2 = nullptr;
        if (obj) {
            // QueryCount(entity storage) → BL=(count>0) (0x1400c059a..0x1400c05d2)
            CitizenComponent* entObj = *citizenRef;                            // 0x1400c05a3: [R14]
            StorageComponent* storage = entObj->_entity->FindComponent<StorageComponent>();   // 0x140006270 (local_78)
            int count = 0;
            // LEFT: 0x14012cf10's C signature (COMP-b) takes the component as an integer.
            count = storage->QueryCount(); // 0x14012cf10
            bool countPositive = (count > 0); // 0x1400c05d0: TEST EAX,EAX; SETG BL

            ComponentEntity* entityInner = (*citizenRef)->_entity.Get();
            int pathType = 1; // R13D = Slow (default); 0x1400c05fc: CMOVNZ R13D, 4 if BL
            if (countPositive) pathType = 4; // Faster
            CharacterComponent* family = entityInner->FindComponent<CharacterComponent>();   // 0x1400012d0
            // 0x1400c0631: R8D=R13D=pathType ; dest=[RBP+0x7f]=param_10 ; XMM8=speed ; [RSP+0x28]=0xe1f
            mv2 = new (obj) MoveTo(&family, pathType, 0, pos10, PathBits::Walk, 0, speed);
        }
        ibpA = mv2;
        WAP_Append(list, &ibpA);
    }

    (void)ibpB; (void)ibpC; // appended above
}
