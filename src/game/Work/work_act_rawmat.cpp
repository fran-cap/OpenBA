// src/game/Work/work_act_rawmat.cpp
// Raw-material work-action constructors (Work action-ctor sub-batch).
//
// Two exe helpers that build the action sequence for a citizen "gather raw
// material" / "fishing" work task. Each allocates a chain of Action objects
#include "Work/exe_bodies.h"
// (SendComponentMessage / ShowTool / CompleteTask / CreateRawMaterial /
// PerformWork / MoveTo) and appends InternalBasePtr wrappers to the citizen's
// action stack (CitizenComponent +0x278 -> its CharacterComponent, stack at +0x48).
// The SendComponentMessage action is stored at +0x58 (DeletePtr the prior one
// first). 1:1 instruction-faithful ports of
//   FUN_1400bb7b0 (WorkAct_GatherWorkCtor_bb7b0)
//   FUN_1400beae0 (WorkAct_FishWorkCtor_beae0)
//
// LEFT: the callers (GatherRawMaterials, FishingWork in WORK-a / WORK-b) hand the
// citizen reference as void* and the work entity as int64_t*; the action ctors (ACT's
// C bodies) take their references as uint64_t*.
//
// Do NOT "improve" — every offset/constant cites its disasm address.

#include <new>
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"
#include "Action/exe_bodies.h"      // B3R: the action ctor bodies called by name
#include "Game/exe_constants.h"     // B3R: kRdata_*
#include "system/Value.h"           // B3R: RtSystem_Time_Ctor
#include "object/Internal.h"
#include "Component/CitizenAIComponent.h"   // CitizenAIComponent::TypeId() (the message target)
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/OrchardComponent.h"   // ENDGAME B.3.9 D5
#include "Component/FishingComponent.h"   // ENDGAME B.3.9 D5
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/StorageComponent.h"
#include "Action/Action.h"
#include "Entity/ComponentEntity.h"
#include "Action/SendComponentMessage.h"   // B4 P2 WORK-3: the action ctors by name
#include "Action/CompleteTask.h"
#include "Action/ShowTool.h"
#include "Action/MoveTo.h"
#include "Action/CreateRawMaterial.h"
#include "Action/PerformWork.h"   // B4 P2 WORK-3: the action ctors by name

// ============================================================
// Sub-callees, called by name (B3R)
// ============================================================
// Every exe sub-callee is one of our own bodies (Action/, Work/ exe_bodies.h):
//   0x1400c5400 TaskListCleanup                     WorkBase_TaskListCleanup_c5400
//   0x1400c54e0 TaskListAppend                      Pf_ActStackPush_54e0
//   0x1400012d0 Entity_FindComponent_Family         ActMv_FindComponentFamily_12d0
//   0x140006270 FindComponent<StorageComponent>
//   0x1400cb1d0 SendComponentMessage copy-ctor      Pf_SendComponentMessageCopyCtor_b1d0
//   0x1400c5da0 CompleteTask copy-ctor              Pf_CompleteTaskCopyCtor_5da0
//   0x140042890 ShowTool ctor (R8D=toolType)        ActChild_ShowToolCtor_42890
//   0x1400c71a0 CreateRawMaterial copy-ctor         Pf_CreateRawMaterialCopyCtor_71a0
//   0x1400c9720 PerformWork_Action_Ctor7            Pf_PerformWorkCtor7_9720
//   0x1400c9170 MoveTo ctor                         Pf_MoveToCtor_9170
// Dispatch slots:
//   0x140152b60 MemAlloc             -> System::MemoryAllocator::MemAlloc
//   0x140152a90 InternalBasePtr ctor -> placement-new Object::InternalBasePtr
//   0x140152b00 System::Time(float)  -> RtSystem_Time_Ctor
//   0x140151ab8 ?DeletePtr@InternalBasePtr@Object@@QEAAXXZ -> Object::InternalBasePtr::DeletePtr
//                                    (Runtime 0x1800c0150, object/Internal.h), by name
// .rdata: 0x140172280 = -1.0f (MoveTo speed, both fns).

static inline void* WAR_Alloc(uint64_t size, uint32_t align) {
    return System::MemoryAllocator::MemAlloc(size, static_cast<int>(align));
}
// 0x1400c54e0 ActionStackPush (Action.h) on CharacterComponent::actionStack (+0x48).
static inline void WAR_Append(ActionStack* list, const ::Object::InternalPtr<Action>* ibp) {
    ActionStackPush(list, ibp);
}

// Bit-exact float constants (read_memory). Used where the disasm loads an immediate
// from .rdata that holds a real value in the static image (Time-ctor args).
static const uint32_t kWar_f_1p0_14017214c   = 0x3f800000u; // [0x14017214c] = 1.0f  (bb7b0 PerformWork Time arg)
static const uint32_t kWar_f_n1000_1401722a8 = 0xc47a0000u; // [0x1401722a8] = -1000.0f (bb7b0 PerformWork heading)
static const uint32_t kWar_f_30p0_1401721cc  = 0x41f00000u; // [0x1401721cc] = 30.0f  (beae0 PerformWork Time arg)
static inline float WAR_FloatBits(uint32_t bits) { float f; __builtin_memcpy(&f, &bits, 4); return f; }

// ============================================================
// WorkAct_GatherWorkCtor_bb7b0 (FUN_1400bb7b0)
// ============================================================
// Build the gather-raw-material work-action chain (forester/gatherer-style) and
// store it on the citizen's action stack (character +0x48).
//   param_1 = InternalPtr<CitizenComponent>* citizenRef
//   param_2 = workId (u32)               -> CompleteTask param_2
//   param_3 = InternalPtr<ComponentEntity>* workRef -> *param_3 used as SendComponentMessage #2 entity inner
//   param_4 = float* worldPos            -> MoveTo destination
//   param_5 = ComponentDescription* rawMatDesc -> CreateRawMaterial param_2 (RDX, [RBP+0x67])
//   param_6 = char flag                  -> gates the CreateRawMaterial block ([RBP+0x6f])
//   param_7 = float (raw-material p3)    -> CreateRawMaterial XMM2 ([RBP+0x77])
//   param_8 = u32 (raw-material p8)      -> CreateRawMaterial p8 ([RBP+0x7f])
//   param_9 = u32                        -> CompleteTask p5 / SendComponentMessage #2 p6 ([RBP+0x87])
void WorkAct_GatherWorkCtor_bb7b0(CitizenComponent* const* citizenRef, uint32_t workId, ComponentEntity* const* workRef,
                                  float* worldPos, ComponentDescription* rawMatDesc, char flag6,
                                  float rawMatP3, uint32_t rawMatP8, uint32_t param9) {

    // the action stack = character + 0x48 (disasm 0x1400bb7eb..0x1400bb802)
    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* list = &character->actionStack;
    list->Clear();

    void* obj;
    ::Object::InternalPtr<Action> ibpA;
    ::Object::InternalPtr<Action> ibpB;
    ::Object::InternalPtr<Action> ibpC;

    // --- SendComponentMessage #1 -> character+0x58 (replaces prior, DeletePtr) (0x1400bb80e..0x1400bb89b) ---
    obj = WAR_Alloc(0x40, 8);
    Action* sm = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;            // [RBP-0x29]   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get(); // [RBP-0x21]
        sm = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0);
    }
    ibpA = sm;              // [RBP-0x19]
    // disasm 0x1400bb886: CMP [RSI+0x10],0 ; RSI = character+0x48 -> +0x10 = character+0x58.
    if (character->actionStack.completionAction != nullptr)   // [0x140151ab8] = InternalBasePtr::DeletePtr
        character->actionStack.completionAction.DeletePtr();
    character->actionStack.completionAction = ibpA; // disasm 0x1400bb89b: MOV [RSI+0x10],RAX

    // --- CompleteTask action (0x1400bb89f..0x1400bb904) ---
    obj = WAR_Alloc(0x38, 8);
    Action* ct = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;   // [RBP-0x19]
        ComponentEntity* innerIbp = nullptr;                                              // [RBP+0x47] re-init via IbpCtor? — disasm reuses [RBP+0x47] as 0-init IBP   // retail: InternalBasePtr(nullptr) in a stack slot
        ct = new (obj) CompleteTask(workId, &entityInner, &innerIbp, param9);
    }
    ibpB = ct;              // [RBP-0x21]
    WAR_Append(list, &ibpB);

    // --- SendComponentMessage #2 (entity = *param_3, targetTypeId = [0x1401c46b0]=OrchardComponent, p4=1, p6=param_9) (0x1400bb909..0x1400bb97d) ---
    obj = WAR_Alloc(0x40, 8);
    Action* sm2 = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;          // [RBP-0x19]   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = OrchardComponent::TypeId();     // [0x1401c46b0] -> [RBP+0x47]
        ComponentEntity* entityInner = *workRef;        // *param_3 -> [RBP-0x21]
        sm2 = new (obj) SendComponentMessage(&entityInner, &msgId, 1, &innerIbp, param9);   // 0x1400cb1d0
    }
    ibpC = sm2;             // [RBP-0x29]
    WAR_Append(list, &ibpC);

    // --- CreateRawMaterial action, gated on param_6 (0x1400bb982..0x1400bba73) ---
    if (flag6 != '\0') {
        // storage = Entity_FindComponent_Storage(citizen entity inner)
        StorageComponent* storage = (*citizenRef)->_entity->FindComponent<StorageComponent>();   // 0x140006270
        uint32_t p7 = static_cast<uint32_t>(storage->capacity);   // R15D = +0x38

        obj = WAR_Alloc(0x58, 8);
        Action* crm = nullptr;
        if (obj) {
            ComponentEntity* entField248 = (*citizenRef)->workplace.Get();  // [RBP+0x47] (p10 target)
            CitizenComponent* entObj = *citizenRef;                   // RCX -> [RBP-0x21] (p4)
            ComponentEntity* ent248Slot = entObj->workplace.Get();          // [RBP-0x19] (p5)
            // p2=param_5 (RDX), p3=param_7 (XMM2 float), p4=&entObj, p5=&ent248Slot, p6=0 (leftover stack),
            // p7=*(*(storage)+0x38), p8=param_8, p9=1, p10=&entField248, p11=0xc (disasm 0x1400bba1a..0x1400bba55)
            crm = new (obj) CreateRawMaterial(rawMatDesc, rawMatP3, &entObj, &ent248Slot, 0,
                                              p7, rawMatP8, 0x1, &entField248, 0xc);   // 0x1400c71a0
        }
        ibpC = crm;         // [RBP-0x29]
        WAR_Append(list, &ibpC);
    }

    // --- PerformWork action (0x1400bba78..0x1400bbaf3) ---
    // disasm: XMM2 = [0x1401722a8] (-1000.0f heading), R9D = 0x8 (actionType),
    //         time = System::Time::Time(&t, 1.0f [0x14017214c]); p6 = 1; p7 = -1.
    obj = WAR_Alloc(0x48, 8);
    Action* pw = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef;  // [RBP+0x47]
        System::Time timeVal = {0};                                              // [RBP-0x21]
        RtSystem_Time_Ctor(&timeVal, WAR_FloatBits(kWar_f_1p0_14017214c));
        float heading = WAR_FloatBits(kWar_f_n1000_1401722a8);
        pw = new (obj) PerformWork(&entityInner, heading, 0x8, timeVal._ticks, 1, -1);
    }
    ibpB = pw;              // [RBP-0x19]
    WAR_Append(list, &ibpB);

    // --- MoveTo action (Slow=1, destination=param_4) (0x1400bbaf8..0x1400bbb8f) ---
    obj = WAR_Alloc(0x480, 8);
    Action* mv = nullptr;
    if (obj) {
        ComponentEntity* entInner = (*citizenRef)->_entity.Get(); // [RBP+0x47]
        CharacterComponent* family = entInner->FindComponent<CharacterComponent>();   // 0x1400012d0            // [RBP-0x21]
        float speed = kRdata_140172280;          // [0x140172280] = -1.0f
        // disasm: R8D=1 (Slow), R9D=0, [RSP+0x20]=param_4, [RSP+0x28]=0xe1f, [RSP+0x30]=0, [RSP+0x38]=speed
        mv = new (obj) MoveTo(&family, 1 /*Slow; disasm LEA R8D,[R9+1]*/, 0, worldPos, PathBits::Walk, 0, speed);
    }
    ibpB = mv;              // [RBP-0x19]
    WAR_Append(list, &ibpB);
}

// ============================================================
// WorkAct_FishWorkCtor_beae0 (FUN_1400beae0)
// ============================================================
// Build the fishing work-action chain and store it on the citizen's
// action stack (character +0x48).
//   param_1 = citizen InternalBasePtr*
//   param_2 = workId (u32)               -> CompleteTask param_2
//   param_3 = InternalPtr<ComponentEntity>* (workRef) -> *param_3 gates + feeds SendComponentMessage #2
//   param_4 = float* position            -> MoveTo destination
//   param_5 = (unused in body; param-area only)
//   param_6 = u64 (raw-material p2)      -> CreateRawMaterial param_2 (RDX, [RBP+0x68])
//   param_7 = float (raw-material p3)    -> CreateRawMaterial XMM2 ([RBP+0x70])
//   param_8 = u32                        -> SendComponentMessage #2 p6 / CreateRawMaterial p8 ([RBP+0x78])
//   param_9 = u32                        -> CompleteTask p5 ([RBP+0x80])
void WorkAct_FishWorkCtor_beae0(CitizenComponent* const* citizenRef, uint32_t workId, ComponentEntity* const* workRef,
                                float* position, float param5_heading, ComponentDescription* rawMatDesc,
                                float rawMatP3, uint32_t param8, uint32_t param9) {

    // the action stack = character + 0x48 (disasm 0x1400beb18..0x1400beb2f)
    CharacterComponent* character = (*citizenRef)->character.Get();
    ActionStack* list = &character->actionStack;
    list->Clear();

    void* obj;
    ::Object::InternalPtr<Action> ibpA;
    ::Object::InternalPtr<Action> ibpB;
    ::Object::InternalPtr<Action> ibpC;

    // --- SendComponentMessage #1 -> character+0x58 (0x1400beb3b..0x1400bebc8) ---
    obj = WAR_Alloc(0x40, 8);
    Action* sm = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;          // [RBP+0x50]   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityInner = (*citizenRef)->_entity.Get(); // [RBP-0x20]
        sm = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, 0);
    }
    ibpA = sm;              // [RBP-0x18]
    // disasm 0x1400bebb3: CMP [RSI+0x10],0 ; RSI = character+0x48 -> +0x10 = character+0x58.
    if (character->actionStack.completionAction != nullptr)   // [0x140151ab8] = InternalBasePtr::DeletePtr
        character->actionStack.completionAction.DeletePtr();
    character->actionStack.completionAction = ibpA; // disasm 0x1400bebc8: MOV [RSI+0x10],RAX

    // --- ShowTool (~Hoe = -1) action (0x1400bebcc..0x1400bec13) ---
    obj = WAR_Alloc(0x30, 8);
    Action* st = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef; // [RBP+0x40]
        // disasm: OR R8D,0xFFFFFFFF -> toolType = -1 (~Hoe, Hoe==0)
        st = new (obj) ShowTool(&entityInner, -1);
    }
    ibpB = st;              // [RBP+0x50]
    WAR_Append(list, &ibpB);

    // --- CompleteTask action (param_5 = param_9) (0x1400bec18..0x1400bec7b) ---
    obj = WAR_Alloc(0x38, 8);
    Action* ct = nullptr;
    if (obj) {
        ComponentEntity* innerIbp = nullptr;          // [RBP+0x40]   // retail: InternalBasePtr(nullptr) in a stack slot
        CitizenComponent* entityInner = *citizenRef; // [RBP+0x50]
        ct = new (obj) CompleteTask(workId, &entityInner, &innerIbp, param9);
    }
    ibpA = ct;              // [RBP-0x18]
    WAR_Append(list, &ibpA);

    // --- optional SendComponentMessage #2 when *param_3 != 0 (0x1400bec80..0x1400becfc) ---
    if (*workRef != nullptr) {
        obj = WAR_Alloc(0x40, 8);
        Action* sm2 = nullptr;
        if (obj) {
            ComponentEntity* innerIbp = nullptr;      // [RBP+0x50]   // retail: InternalBasePtr(nullptr) in a stack slot
            uint32_t msgId = FishingComponent::TypeId(); // [0x1401c7660] -> [RBP+0x40]
            ComponentEntity* entityInner = *workRef;    // *param_3 -> [RBP-0x18]
            // disasm: R9D=0 (p4), [RSP+0x28]=R15D = param_8 ([RBP+0x78]) -> p6
            sm2 = new (obj) SendComponentMessage(&entityInner, &msgId, 0, &innerIbp, param8);   // 0x1400cb1d0
        }
        ibpC = sm2;         // [RBP-0x20]
        WAR_Append(list, &ibpC);
    }

    // --- CreateRawMaterial action (always; 0x1400bed01..0x1400beddb) ---
    {
        // storage = Entity_FindComponent_Storage(citizen entity inner)
        StorageComponent* storage = (*citizenRef)->_entity->FindComponent<StorageComponent>();   // 0x140006270
        uint32_t p7 = static_cast<uint32_t>(storage->capacity);   // R12D = +0x38

        obj = WAR_Alloc(0x58, 8);
        Action* crm = nullptr;
        if (obj) {
            ComponentEntity* innerIbp = nullptr;      // [RBP+0x40] (IbpCtor(&,0))   // retail: InternalBasePtr(nullptr) in a stack slot
            CitizenComponent* entObj = *citizenRef;                   // RDX -> [RBP-0x18] (p4); RCX side
            ComponentEntity* ent248Slot = entObj->workplace.Get();          // [RBP+0x50] (p5)
            // p2=param_6 (RDX), p3=param_7 (XMM2), p4=&ent248Slot([RBP+0x50]) , p5=... — order per disasm:
            //   R9 = &[RBP-0x18] (= entObj, p4); [RSP+0x20] = &[RBP+0x50] (= ent248Slot, p5);
            //   [RSP+0x28]=0 (p6); [RSP+0x30]=R12D=p7; [RSP+0x38]=R15D=param_8 (p8);
            //   [RSP+0x40]=0 (p9 byte); [RSP+0x48]=&[RBP+0x40] (p10); [RSP+0x50]=4 (p11)
            crm = new (obj) CreateRawMaterial(rawMatDesc, rawMatP3, &entObj, &ent248Slot, 0,
                                              p7, param8, 0x0, &innerIbp, 0x4);   // 0x1400c71a0
        }
        ibpC = crm;         // [RBP-0x20]
        WAR_Append(list, &ibpC);
    }

    // --- PerformWork action (0x1400bede0..0x1400bee58) ---
    // disasm: R9D = 0xe (actionType), XMM2 = [RBP+0x60] (param area float — the heading
    //         arg; carried from caller), time = System::Time::Time(&t, 30.0f [0x1401721cc]),
    //         p6 = 1, p7 = -1.
    obj = WAR_Alloc(0x48, 8);
    Action* pw = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef; // [RBP+0x40]
        System::Time timeVal = {0};                                             // [RBP-0x18]
        RtSystem_Time_Ctor(&timeVal, WAR_FloatBits(kWar_f_30p0_1401721cc));
        // XMM2 = [RBP+0x60] = param_5 (5th incoming arg, the caller-supplied heading float;
        // [RBP+0x60] is the 5th-arg home slot, one below param_6@[RBP+0x68]).
        // (disasm 0x1400bee2e MOVSS XMM2,[RBP+0x60])
        float heading = param5_heading;
        pw = new (obj) PerformWork(&entityInner, heading, 0xe, timeVal._ticks, 1, -1);
    }
    ibpC = pw;              // [RBP+0x50]
    WAR_Append(list, &ibpC);

    // --- MoveTo action (Fast=2, destination=param_4) (0x1400bee5d..0x1400beef2) ---
    obj = WAR_Alloc(0x480, 8);
    Action* mv = nullptr;
    if (obj) {
        ComponentEntity* entInner = (*citizenRef)->_entity.Get(); // [RBP+0x40]
        CharacterComponent* family = entInner->FindComponent<CharacterComponent>();   // 0x1400012d0            // [RBP-0x18]
        float speed = kRdata_140172280;          // [0x140172280] = -1.0f
        // disasm: R8D = 2 (Fast; LEA R8D,[R9+2]), R9D=0, [RSP+0x20]=R13(=param_4),
        //         [RSP+0x28]=0xe1f, [RSP+0x30]=0, [RSP+0x38]=speed
        mv = new (obj) MoveTo(&family, 2 /*Fast*/, 0, position, PathBits::Walk, 0, speed);
    }
    ibpC = mv;              // [RBP+0x50]
    WAR_Append(list, &ibpC);

    // --- ShowTool (Hammer = 5) action (0x1400beef7..0x1400bef3e) ---
    obj = WAR_Alloc(0x30, 8);
    Action* st2 = nullptr;
    if (obj) {
        CitizenComponent* entityInner = *citizenRef; // [RBP+0x40]
        // disasm: MOV R8D,5 -> toolType = 5 (Hammer)
        st2 = new (obj) ShowTool(&entityInner, 5);
    }
    ibpC = st2;             // [RBP+0x50]
    WAR_Append(list, &ibpC);
}
