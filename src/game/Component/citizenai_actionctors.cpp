// src/game/Component/citizenai_actionctors.cpp
//
// CitizenAI "action-ctor family" (Brick 4b) — the multi-action sequence BUILDERS that
// the CitizenAI state handlers (citizenai_handlers / the State_* dispatchers) call to
// assemble the citizen's Action/Work queue and push it onto the action stack at
// CharacterComponent+0x48. Faithful 1:1 ports, transcribed from the Ghidra decompile and
// cross-checked against the disassembly (NavigateTo @0x1400bda50 + WalkTowards
// @0x1400bb490 disasm verified offset-by-offset; the six PerformWork builders are the
// same shape).
//
// VERIFIED real addresses (reconciled vs each calling handler's get_function_callees):
//   0x1400c1070  PerformWork_Action_Ctor4   <- State_LongTermPlan  (the big chop/build seq)
//   0x1400bd070  PerformWork_Action_Ctor5   <- State_SeekHappiness
//   0x1400bd420  PerformWork_Action_Ctor6   <- State_AdultTransition
//   0x1400bd790  PerformWork_Action_Ctor8   <- State_ElderTransition
//   0x1400bccb0  PerformWork_Action_Ctor10  <- State_Education
//   0x1400bc790  PerformWork_Action_Ctor11  <- State_GrowUp
//   0x1400bda50  Pathfinding_NavigateTo     <- State_Work
//   0x1400bb490  Pathfinding_WalkTowards    <- State_Work / State_FindWork
//   0x1400ba6b0  PlayAnimation_Action_Ctor4 <- (sibling of the already-ported Ctor5 c26c0)
//
// Each builder follows the same skeleton:
//   self    = *param_1 (a CitizenComponent) ; char = *(self+0x278) ; stack = char+0x48
//   FUN_1400c5400(stack)                       — clear/cleanup the action stack
//   alloc a SendComponentMessage action (0x40) -> CharacterComponent+0x58 (DeletePtr old)
//   ... alloc & CopyCtor each per-step Action (SendComponentMessage / ConsumeRaw /
//       DropOffRaw / PickupStored / ShowTool / CompleteTask / a raw inline PerformWork /
//       PlayAnimation), wrap in an InternalBasePtr, FUN_1400c54e0(stack, &ibp) — push.
//   ... alloc a MoveTo action (0x480) via MoveTo_Action_Ctor, push.
//
// SELF-CONTAINED-BRICK NOTE: the SendComponentMessage message-id globals
// DWORD_1401c70e8 / DWORD_1401c7158 and the HappinessComponent id DAT_1401c42c0 are NOT
// registered in standalone (read 0). We null-guard them exactly like the existing
// citizenai_handlers.cpp PlayAnimationActionCtor5 — the message actions then carry id 0.
// The RawMaterialDescription id DAT_1401c7cb8 IS registered+filled, so the Ctor5/Ctor10
// storage-limit component scan reads the real CRC.
//
// ALL exe sub-callees are already registered (resolved via ResolveAddr) — bodies NOT
// duplicated here:
//   0x1400c5400  FUN_1400c5400  action-stack cleanup       (reg_pf_ctors / reg_work_base)
//   0x1400c54e0  FUN_1400c54e0  action-stack push          (reg_pf_ctors)
//   0x1400cb1d0  SendComponentMessage_Action_CopyCtor      (reg_pf_ctors)
//   0x1400c9170  MoveTo_Action_Ctor                        (reg_pf_ctors)
//   0x1400c5da0  CompleteTask_Action_CopyCtor              (reg_pf_ctors)
//   0x1400012d0  Entity_FindComponent_Family               (reg_action_move / reg_pf_ctors)
//   0x1400c29a0  Entity_impl_29a0 (returns float anim-idx) (reg_action_child_ctors)
//   0x1400c7cd0  DropOffRawMaterial_Action_CopyCtor        (reg_action_child_ctors2)
//   0x1400c63b0  ConsumeRawMaterial_Action_CopyCtor        (reg_action_child_ctors2)
//   0x1400ca520  PickupStoredRawMaterial_Action_CopyCtor   (reg_action_child_ctors2)
//   0x140042890  ShowTool_Action_Ctor                      (reg_action_child_ctors)
//
// Engine helpers (engine_funcs.h): fn_ObjectInternal_Ctor, fn_InternalBasePtr_Ctor,
//   fn_InternalBasePtr_CheckPtr, fn_InternalBasePtr_DeletePtr, fn_Time_Ctor_Float,
//   System::MemoryAllocator::MemAlloc(size, 8), g_pGameWorldSingleton.
//
// Action vtables (exe .rdata): vtable_Action 0x14015faa0, vtable_PerformWork 0x14015fea0,
//   vtable_PlayAnimation 0x140158950 (all confirmed from the ctor disasm `LEA RAX,[...]`).
//
// Float consts (exe .rdata, read with read_memory):
//   0x140172124 = 0.75   (Ctor4 PerformWork Time)
//   0x140172174 = 2.0    (Ctor6 PerformWork Time)
//   0x140172194 = 4.0    (Ctor8 NavigateTo's "g_pAgeMultiplier" sibling Time)
//   0x140172198 = 5.0    (NavigateTo PerformWork Time; "g_pAgeMultiplier" in decompile)
//   0x14017214c = 1.0    (Ctor5/Ctor10/Ctor11 "g_fUVScale" Time)
//   0x140172280 = -1.0   (MoveTo randomized-speed sentinel)
//


#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"              // Map::cells (MapCell::terrainType)
#include "Action/PlayAnimation.h"   // B.3.8 CB1a (Vtable)
#include "Action/PerformWork.h"   // B.3.8 CB1a (Vtable)
#include "Action/Action.h"   // B.3.8 CB1a (Vtable); ActionStack, ActionStackPush
#include "Action/SendComponentMessage.h"
#include "Action/MoveTo.h"
#include "Action/ShowTool.h"
#include "Action/CompleteTask.h"
#include "Action/DropOffRawMaterial.h"
#include "Action/ConsumeRawMaterial.h"
#include "Action/PickupStoredRawMaterial.h"
#include "Component/HappinessComponent.h"   // HappinessComponent::TypeId() (the message target)
#include "Component/CitizenAIComponent.h"   // CitizenAIComponent::TypeId() (the message target)
#include "Component/CharacterComponent.h"
#include "Component/ComponentDescription.h"
#include "Object/factory_registration.h"   // Object::GetTypeIdByName (c7158 heal)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/CitizenComponent.h"   // ENDGAME B.3.8 CIT (TypeId)
#include "Component/RawMaterialDescription.h"   // ENDGAME B.3.9 C1: 0x1401c7cb8 cell class
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Game/exe_constants.h"
// EXT SEAM (NOT RETAIL, docs/extensions/handins/TOOLNAMES.md): ext.tool_names (class C)
#include "ext/tool_names/tool_names.h"
#include "ext/registry.h"
#include "system/Value.h"   // RtSystem_Time_Ctor
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

namespace {

// Exe sub-callees, called by name (B4 P2 CAI-1: the action ctors are called directly in
// place of their C forwarders): WorkBase_TaskListCleanup_c5400 0x1400c5400,
// ActionStackPush 0x1400c54e0, SendComponentMessage 0x1400cb1d0, MoveTo 0x1400c9170,
// CompleteTask 0x1400c5da0, FindCharacterComponent 0x1400012d0, HeadingToward 0x1400c29a0,
// DropOffRawMaterial 0x1400c7cd0, ConsumeRawMaterial 0x1400c63b0, PickupStoredRawMaterial
// 0x1400ca520, ShowTool 0x140042890. Message targets: 70e8 (CitizenAIComponent) /
// 42c0 (HappinessComponent) are those classes' TypeId(). The .rdata floats are exe_constants.h's kRdata_140172124 (0.75),
// _140172174 (2.0), _140172194 (4.0), _140172198 (5.0), _14017214c (1.0), _140172280 (-1.0).

// ENDGAME B.3.9 (B39_RULES.md D5): 0x1401c7158 is CitizenComponent's registered
// type-id cell (INVENTORY_C Table 1); it always holds CitizenComponent's id after
// Fill*, so the previous "heal to the class static when the raw read is 0" fallback
// (kept as history above) is now mechanically equivalent to calling the static
// directly. Unambiguous here: every c7158 use in this file targets CitizenComponent.
static inline uint32_t MsgId7158() {
    return CitizenComponent::TypeId();
}
static inline void*    MemA(uint64_t sz) {
    return System::MemoryAllocator::MemAlloc(sz, 8);
}

// The inline PerformWork actions below are placed at `mem` (0x48 bytes). animIdx is the facing
// float stored at +0x28 (-1000.0f where the original used the literal 0xc47a0000).
// time = the System::Time for +0x30, type = +0x2c work type.
// ENDGAME B.3.9 EXA: the real class — PerformWork's parameterised ctor
// (0x1400c9720) with useTool = 0 and field44 = -1 writes exactly the stores this
// helper wrote by hand after the `??0Internal` door and the two stamps (the
// citizen IBP is copied raw at +0x20, as retail's inlined ctor does).

// Resolve the citizen's character (+0x278) and its +0x48 action stack; return the stack
// and store the character via `outChar`. `doCleanup` runs FUN_1400c5400 cleanup (all
// builders clean unconditionally at the top EXCEPT Ctor4, which gates it behind its
// build flag).
static ActionStack* PrologueResolveStack(CitizenComponent* const* citizenRef, CharacterComponent** outChar,
                                         bool doCleanup = true) {
    CharacterComponent* ch = (*citizenRef)->character.Get();
    *outChar = ch;
    ActionStack* stack = &ch->actionStack;
    if (doCleanup) stack->Clear();
    return stack;
}

// Build + install the leading SendComponentMessage action into CharacterComponent+0x58
// (msgId = DWORD_1401c70e8, the standard "begin work" message).
static void InstallLeadMessage(CitizenComponent* const* citizenRef, CharacterComponent* ch) {
    void* mem = MemA(0x40);
    Action* act = nullptr;
    if (mem) {
        ComponentEntity* destIBP = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
        uint32_t msgId = CitizenAIComponent::TypeId();   // [0x1401c70e8] -> OnMessage 0x14010f340
        ComponentEntity* entityIBP = (*citizenRef)->_entity.Get();
        act = new (mem) SendComponentMessage(&entityIBP, &msgId, 0, &destIBP, 0);   // 0x1400cb1d0
    }
    if (ch->actionStack.field_0x10 != nullptr) {
        ch->actionStack.field_0x10.DeletePtr();
    }
    ch->actionStack.field_0x10 = act;
}

// Push an action (null when its allocation failed) on the stack: the InternalPtr retail
// builds in a stack slot, then 0x1400c54e0.
static void Push(ActionStack* stack, Action* act) {
    ::Object::InternalPtr<Action> ibp(act);
    ActionStackPush(stack, &ibp);   // 0x1400c54e0
}

// Build + push the trailing MoveTo action toward `dest`, mask = the pathfinding flags.
static void PushMoveTo(CitizenComponent* const* citizenRef, ActionStack* stack, uint32_t pathType,
                       float* dest, int mask) {
    void* mem = MemA(0x480);
    Action* act = nullptr;
    if (mem) {
        CharacterComponent* fam = ((*citizenRef)->_entity.Get())->FindComponent<CharacterComponent>();   // 0x1400012d0
        float speed = kRdata_140172280;
        act = new (mem) MoveTo(&fam, pathType, 0, dest, static_cast<uint32_t>(mask), 0, speed);   // 0x1400c9170
    }
    Push(stack, act);
}

// Locate the material's RawMaterialDescription (GetTypeId == DWORD_1401c7cb8) among its
// sub-descriptions (_subDescs +0x28, _subDescCount +0x30, stride 8). Returns the match
// or null. Used by Ctor5/Ctor10 (the original's pIVar6 scan).
static RawMaterialDescription* FindLimitComponent(ComponentDescription* material) {
    RawMaterialDescription* found = nullptr;
    uint32_t want = RawMaterialDescription::TypeId();
    int n = material->_subDescCount;
    if (0 < n) {
        int idx = 0;
        for (;;) {
            Description* child = material->_subDescs[idx];
            uint32_t tid = child->Type();   // slot 2
            if (tid == want) {
                Description* match = material->_subDescs[idx];
                if (Object::IsDerivedFromNullSafe(match, want))
                    found = static_cast<RawMaterialDescription*>(match);
                break;
            }
            idx++;
            if (idx >= n) break;
        }
    }
    return found;
}

} // namespace

// ============================================================
// 0x1400c1070 — PerformWork_Action_Ctor4  (called by State_LongTermPlan)
// The full "fetch axe / chop / drop raw / build" sequence. Only runs when param_6 (the
// build flag) is set. Pushes: SendComponentMessage(lead)+DropOffRaw(into +0x58); then
// PerformWork(type 7) + ShowTool(~Axe=-? see note) + DropOffRaw + PerformWork(type 6) +
// MoveTo + ShowTool. animIdx = HeadingToward(dest). The work-mask bit comes from the
// terrain cell flag byte at grid+0x80 + param_4*0x30 + 0x29.
//   param_1 = CitizenComponent IBP ; param_2 = &targetEntityIBP ; param_3 = float* dest
//   param_4 = cell index ; param_5 = drop-raw flag byte ; param_6 = build flag.
// NOTE: ShowTool takes ~Axe (the original passes the bitwise-NOT of the Axe tool enum).
// Axe's enum value isn't separately known here; the original literal is reproduced as the
// disasm immediate. We pass the same immediate ShowTool received (see s_showToolAxeArg).
// ============================================================
// ShowTool's 3rd arg is the immediate 0xfffffffe (-2), verified from the Ctor4 disasm
// (`MOV R8D,0xfffffffe` before both CALL 0x140042890). The decompile rendered it as "~Axe".
namespace { static const int kShowToolAxeArg = static_cast<int>(0xfffffffe); }

void CitizenAI_PerformWorkCtor4_c1070(CitizenComponent* const* citizenRef, ComponentEntity* const* targetRef,
                                      float* param_3, int param_4, uint8_t param_5, uint32_t param_6) {
    CharacterComponent* ch = nullptr;
    // Ctor4 gates BOTH the stack cleanup and the lead block behind param_6 (disasm:
    // `CMP byte[RBP+0x68],DIL; JZ` skips cleanup + lead to the cell-flags math).
    ActionStack* stack = PrologueResolveStack(citizenRef, &ch, /*doCleanup=*/false);

    if (static_cast<char>(param_6) != '\0') {
        stack->Clear();
        // lead SendComponentMessage -> CharacterComponent+0x58
        InstallLeadMessage(citizenRef, ch);
        // DropOffRaw action (also stored at +0x58, replacing the message) — faithful: the
        // original re-derives local_68 and OVERWRITES char+0x58 with the drop-raw IBP.
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* destIBP = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
            CitizenComponent* entityIBP = *citizenRef;  // *(undefined8*)param_1
            act = new (mem) DropOffRawMaterial(&entityIBP, &destIBP, nullptr, 0xffffffff, param_5);   // 0x1400c7cd0
        }
        Push(stack, act);
    }

    // work mask bit from the cell flags byte
    uint8_t bit = GameUpdateLoop::_singleton->m_game->cells.GetData()[param_4].terrainType;

    ComponentEntity* target = *targetRef;
    if (target == nullptr) return;

    float animIdx = 0;
    {
        ComponentEntity* targetIBP = target;
        animIdx = HeadingToward(param_3, &targetIBP);   // 0x1400c29a0
    }

    // PerformWork (type 7) @0.75s
    {
        void* mem = MemA(0x48);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityVal = *citizenRef;
            System::Time t = {}; RtSystem_Time_Ctor(&t, kRdata_140172124);
            act = ::new (mem) PerformWork(&entityVal, animIdx, 7, t._ticks, /*useTool=*/false, /*field44=*/-1);
        }
        Push(stack, act);
    }
    // ShowTool (equip)
    {
        void* mem = MemA(0x30);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityIBP = *citizenRef;
            act = new (mem) ShowTool(&entityIBP, kShowToolAxeArg);   // 0x140042890
        }
        Push(stack, act);
    }
    // DropOffRaw
    {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* targetIBP = *targetRef;
            CitizenComponent* entityIBP = *citizenRef;
            act = new (mem) DropOffRawMaterial(&entityIBP, &targetIBP, nullptr, 0xffffffff, param_5);   // 0x1400c7cd0
        }
        Push(stack, act);
    }
    // PerformWork (type 6) @0.75s
    {
        void* mem = MemA(0x48);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityVal = *citizenRef;
            System::Time t = {}; RtSystem_Time_Ctor(&t, kRdata_140172124);
            act = ::new (mem) PerformWork(&entityVal, animIdx, 6, t._ticks, /*useTool=*/false, /*field44=*/-1);
        }
        Push(stack, act);
    }
    // MoveTo (Faster=4, disasm `LEA R8D,[R9+0x4]`) with mask = (1<<bit)|0xe1f
    PushMoveTo(citizenRef, stack, 4 /*Faster*/, param_3, (1 << (bit & 0x1f)) | 0xe1f);
    // trailing ShowTool (unequip)
    {
        void* mem = MemA(0x30);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityIBP = *citizenRef;
            act = new (mem) ShowTool(&entityIBP, kShowToolAxeArg);   // 0x140042890
        }
        Push(stack, act);
    }
}

// ============================================================
// 0x1400bd070 — PerformWork_Action_Ctor5  (called by State_SeekHappiness)
// lead SendComponentMessage(70e8) -> +0x58 ; scan for limit component (7cb8) -> pIVar6 ;
// SendComponentMessage(7158, a4=1, a6 = pIVar6->+0x54) ; ConsumeRaw ; PerformWork(type 8,
// time 1.0) ; MoveTo(Slow). animIdx = HeadingToward(dest).
//   param_1 = CitizenComponent IBP ; param_2 = &srcEntityIBP ; param_3 = float* dest ;
//   param_4 = the material ComponentDescription (for the limit scan + ConsumeRaw).
// ============================================================
void CitizenAI_PerformWorkCtor5_bd070(CitizenComponent* const* citizenRef, ComponentEntity* const* srcRef,
                                      float* param_3, ComponentDescription* material) {
    CharacterComponent* ch = nullptr;
    ActionStack* stack = PrologueResolveStack(citizenRef, &ch);
    InstallLeadMessage(citizenRef, ch);

    float animIdx;
    {
        ComponentEntity* srcIBP = *srcRef;
        animIdx = HeadingToward(param_3, &srcIBP);   // 0x1400c29a0
    }

    RawMaterialDescription* limit = FindLimitComponent(material);

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/TOOLNAMES.md): ext.tool_names (class C) notes which item
    // this citizen is on its way to take and the use count it posts, so the citizen panel can name it.
    if (::Ext::Ext().tool_names && limit != nullptr && citizenRef != nullptr) {
        ::Ext::ToolNames::RememberPickup(*citizenRef, limit, limit->_i54);
    }

    // SendComponentMessage(7158, a4=1, a6 = limit->+0x54)
    {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* destIBP = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
            uint32_t msgId = MsgId7158();
            ComponentEntity* entityIBP = (*citizenRef)->_entity.Get();
            uint32_t a6 = static_cast<uint32_t>(limit->_i54);
            act = new (mem) SendComponentMessage(&entityIBP, &msgId, 1, &destIBP, a6);   // 0x1400cb1d0
        }
        Push(stack, act);
    }
    // ConsumeRaw(resType list = *param_2, list = param_4, flag a4=1?  see disasm: param_4(arg) is `param_4=1` byte)
    {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* resType = *srcRef;  // local_res10 = *local_res10
            act = new (mem) ConsumeRawMaterial(&resType, material, 1);   // 0x1400c63b0 (R9 = 1 unused)
        }
        Push(stack, act);
    }
    // PerformWork (type 8) @1.0s
    {
        void* mem = MemA(0x48);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityVal = *citizenRef;
            System::Time t = {}; RtSystem_Time_Ctor(&t, kRdata_14017214c);
            act = ::new (mem) PerformWork(&entityVal, animIdx, 8, t._ticks, /*useTool=*/false, /*field44=*/-1);
        }
        Push(stack, act);
    }
    // MoveTo (Slow=1) mask 0xe1f
    PushMoveTo(citizenRef, stack, 1 /*Slow*/, param_3, 0xe1f);
}

// ============================================================
// 0x1400bd420 — PerformWork_Action_Ctor6  (called by State_AdultTransition)
// lead SendComponentMessage(70e8) -> +0x58 ; animIdx = HeadingToward(dest) ; if param_4:
// SendComponentMessage(7158, a4=0) ; SendComponentMessage(7158, a4=2) ; PerformWork(type 8,
// time 2.0) ; MoveTo(Slow).
//   param_1 = CitizenComponent IBP ; param_2 = &targetEntityIBP ; param_3 = float* dest ;
//   param_4 = "first message" flag byte.
// ============================================================
void CitizenAI_PerformWorkCtor6_bd420(CitizenComponent* const* citizenRef, ComponentEntity* const* targetRef,
                                      float* param_3, char param_4) {
    CharacterComponent* ch = nullptr;
    ActionStack* stack = PrologueResolveStack(citizenRef, &ch);
    InstallLeadMessage(citizenRef, ch);

    float animIdx;
    {
        ComponentEntity* targetIBP = *targetRef;
        animIdx = HeadingToward(param_3, &targetIBP);   // 0x1400c29a0
    }

    // helper to push a SendComponentMessage(7158, a4) toward *param_2
    auto pushMsg7158 = [&](uint32_t a4) {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* destIBP = *targetRef;  // local_res8 = *param_2
            uint32_t msgId = MsgId7158();
            ComponentEntity* entityIBP = (*citizenRef)->_entity.Get();
            act = new (mem) SendComponentMessage(&entityIBP, &msgId, a4, &destIBP, 0);   // 0x1400cb1d0
        }
        Push(stack, act);
    };

    if (param_4 != '\0') pushMsg7158(0);
    pushMsg7158(2);

    // PerformWork (type 8) @2.0s
    {
        void* mem = MemA(0x48);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityVal = *citizenRef;
            System::Time t = {}; RtSystem_Time_Ctor(&t, kRdata_140172174);
            act = ::new (mem) PerformWork(&entityVal, animIdx, 8, t._ticks, /*useTool=*/false, /*field44=*/-1);
        }
        Push(stack, act);
    }
    PushMoveTo(citizenRef, stack, 1 /*Slow*/, param_3, 0xe1f);
}

// ============================================================
// 0x1400bd790 — PerformWork_Action_Ctor8  (called by State_ElderTransition)
// lead SendComponentMessage(70e8) -> +0x58 ; SendComponentMessage(7158, a4=0, dest=*param_2)
// ; PerformWork(type 8, animIdx literal -1000.0f, time 4.0) ; MoveTo(Slow).
//   param_1 = CitizenComponent IBP ; param_2 = &targetEntityIBP ; param_3 = float* dest.
// ============================================================
void CitizenAI_PerformWorkCtor8_bd790(CitizenComponent* const* citizenRef, ComponentEntity* const* targetRef,
                                      float* param_3) {
    CharacterComponent* ch = nullptr;
    ActionStack* stack = PrologueResolveStack(citizenRef, &ch);
    InstallLeadMessage(citizenRef, ch);

    // SendComponentMessage(7158, a4=0) toward *param_2
    {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* destIBP = *targetRef;
            uint32_t msgId = MsgId7158();
            ComponentEntity* entityIBP = (*citizenRef)->_entity.Get();
            act = new (mem) SendComponentMessage(&entityIBP, &msgId, 0, &destIBP, 0);   // 0x1400cb1d0
        }
        Push(stack, act);
    }
    // PerformWork (type 8) animIdx = 0xc47a0000 (-1000.0f) literal, time 4.0
    {
        void* mem = MemA(0x48);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityVal = *citizenRef;
            System::Time t = {}; RtSystem_Time_Ctor(&t, kRdata_140172194);
            act = ::new (mem) PerformWork(&entityVal, -1000.0f, 8, t._ticks, /*useTool=*/false, /*field44=*/-1);
        }
        Push(stack, act);
    }
    PushMoveTo(citizenRef, stack, 1 /*Slow*/, param_3, 0xe1f);
}

// ============================================================
// 0x1400bccb0 — PerformWork_Action_Ctor10  (called by State_Education)
// lead SendComponentMessage(70e8) -> +0x58 ; animIdx = HeadingToward(dest) ; scan for
// limit component (7cb8) -> pIVar7, read flags pIVar7->+0x5c ; SendComponentMessage(7158,
// a4 = 6 + ((flags>>15)&1)) ; ConsumeRaw(resType=*param_2, list=param_4, flag = that a4) ;
// PerformWork(type 8, time 1.0) ; MoveTo(Slow).
//   param_1 = CitizenComponent IBP ; param_2 = &srcEntityIBP ; param_3 = float* dest ;
//   param_4 = the material ComponentDescription.
// ============================================================
void CitizenAI_PerformWorkCtor10_bccb0(CitizenComponent* const* citizenRef, ComponentEntity* const* srcRef,
                                       float* param_3, ComponentDescription* material) {
    CharacterComponent* ch = nullptr;
    ActionStack* stack = PrologueResolveStack(citizenRef, &ch);
    InstallLeadMessage(citizenRef, ch);

    float animIdx;
    {
        ComponentEntity* srcIBP = *srcRef;
        animIdx = HeadingToward(param_3, &srcIBP);   // 0x1400c29a0
    }

    RawMaterialDescription* limit = FindLimitComponent(material);
    uint32_t flags = limit->_flags;
    uint32_t a4 = ((flags >> 0xf) & 1) + 6;

    // SendComponentMessage(7158, a4)
    {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* destIBP = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
            uint32_t msgId = MsgId7158();
            ComponentEntity* entityIBP = (*citizenRef)->_entity.Get();
            act = new (mem) SendComponentMessage(&entityIBP, &msgId, a4, &destIBP, 0);   // 0x1400cb1d0
        }
        Push(stack, act);
    }
    // ConsumeRaw(resType=*param_2, list=param_4, flag=a4)
    {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* resType = *srcRef;
            act = new (mem) ConsumeRawMaterial(&resType, material, 1);   // 0x1400c63b0 (R9 = a4 unused)
        }
        Push(stack, act);
    }
    // PerformWork (type 8) @1.0s
    {
        void* mem = MemA(0x48);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityVal = *citizenRef;
            System::Time t = {}; RtSystem_Time_Ctor(&t, kRdata_14017214c);
            act = ::new (mem) PerformWork(&entityVal, animIdx, 8, t._ticks, /*useTool=*/false, /*field44=*/-1);
        }
        Push(stack, act);
    }
    PushMoveTo(citizenRef, stack, 1 /*Slow*/, param_3, 0xe1f);
}

// ============================================================
// 0x1400bc790 — PerformWork_Action_Ctor11  (called by State_GrowUp)
// The longest builder. lead SendComponentMessage(70e8) -> +0x58 ;
//   animIdx_a = HeadingToward(param_4) ; animIdx_b = HeadingToward(param_6) ;
//   SendComponentMessage(7158, a4=3) ; ConsumeRaw(resType=*param_1.entity, list=param_2, flag=3,reserve=0) ;
//   PerformWork(type 8, animIdx_b, time 5.0) ; MoveTo(Slow, dest=param_6) ;
//   PickupStored(src=*param_3, list=param_2, requested=1, flag=0) ;
//   PerformWork(type 8, animIdx_a, time 1.0) ; MoveTo(Slow, dest=param_4).
//   param_1 = CitizenComponent IBP ; param_2 = the material ComponentDescription ;
//   param_3 = &srcEntityIBP ; param_4 = float* destA ; param_5 = &dstEntityIBP ;
//   param_6 = float* destB.
// ============================================================
void CitizenAI_PerformWorkCtor11_bc790(CitizenComponent* const* citizenRef, ComponentDescription* material,
                                       ComponentEntity* const* srcRef, float* param_4,
                                       ComponentEntity* const* dstRef, float* param_6) {
    CharacterComponent* ch = nullptr;
    ActionStack* stack = PrologueResolveStack(citizenRef, &ch);
    InstallLeadMessage(citizenRef, ch);

    float animIdxA, animIdxB;
    {
        ComponentEntity* srcIBP = *srcRef;
        animIdxA = HeadingToward(param_4, &srcIBP);   // 0x1400c29a0
    }
    {
        ComponentEntity* dstIBP = *dstRef;
        animIdxB = HeadingToward(param_6, &dstIBP);   // 0x1400c29a0
    }

    // SendComponentMessage(7158, a4=3)
    {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* destIBP = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
            uint32_t msgId = MsgId7158();
            ComponentEntity* entityIBP = (*citizenRef)->_entity.Get();
            act = new (mem) SendComponentMessage(&entityIBP, &msgId, 3, &destIBP, 0);   // 0x1400cb1d0
        }
        Push(stack, act);
    }
    // ConsumeRaw(entity=*param_1.entity, list=param_2, flag=3, reserve=0)
    {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* entityIBP = (*citizenRef)->_entity.Get();
            act = new (mem) ConsumeRawMaterial(&entityIBP, material, 0);   // 0x1400c63b0 (R9 = 3 unused)
        }
        Push(stack, act);
    }
    // PerformWork (type 8) animIdxB @5.0s
    {
        void* mem = MemA(0x48);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityVal = *citizenRef;
            System::Time t = {}; RtSystem_Time_Ctor(&t, kRdata_140172198);
            act = ::new (mem) PerformWork(&entityVal, animIdxB, 8, t._ticks, /*useTool=*/false, /*field44=*/-1);
        }
        Push(stack, act);
    }
    // MoveTo(Slow) dest = param_6
    PushMoveTo(citizenRef, stack, 1 /*Slow*/, param_6, 0xe1f);
    // PickupStored(src=*param_3, list=param_2, requested=1, flag=0)
    {
        void* mem = MemA(0x48);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* srcIBP = *srcRef;
            CitizenComponent* entityIBP = *citizenRef;
            act = new (mem) PickupStoredRawMaterial(&entityIBP, &srcIBP, material, 1, 0);   // 0x1400ca520
        }
        Push(stack, act);
    }
    // PerformWork (type 8) animIdxA @1.0s
    {
        void* mem = MemA(0x48);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityVal = *citizenRef;
            System::Time t = {}; RtSystem_Time_Ctor(&t, kRdata_14017214c);
            act = ::new (mem) PerformWork(&entityVal, animIdxA, 8, t._ticks, /*useTool=*/false, /*field44=*/-1);
        }
        Push(stack, act);
    }
    // MoveTo(Slow) dest = param_4
    PushMoveTo(citizenRef, stack, 1 /*Slow*/, param_4, 0xe1f);
}

// ============================================================
// 0x1400bda50 — Pathfinding_NavigateTo  (called by State_Work) — disasm-verified.
// lead SendComponentMessage(70e8) -> +0x58 ; PerformWork(type 8, animIdx literal -1000.0f,
// time 5.0 @0x140172198) ; SendComponentMessage(7158, a4=0, dest=*param_2) ; MoveTo(Slow).
//   param_1 = CitizenComponent IBP ; param_2 = &targetEntityIBP ; param_3 = u32* dest(=float*) ;
//   param_4 = byte (unused after the local_res20 store).
// ============================================================
void CitizenAI_Pathfinding_NavigateTo_bda50(CitizenComponent* const* citizenRef, ComponentEntity* const* targetRef,
                                            float* param_3, uint8_t /*param_4*/) {
    CharacterComponent* ch = nullptr;
    ActionStack* stack = PrologueResolveStack(citizenRef, &ch);
    InstallLeadMessage(citizenRef, ch);

    // PerformWork (type 8) animIdx 0xc47a0000 (-1000.0f), time 5.0
    {
        void* mem = MemA(0x48);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityVal = *citizenRef;
            System::Time t = {}; RtSystem_Time_Ctor(&t, kRdata_140172198);
            act = ::new (mem) PerformWork(&entityVal, -1000.0f, 8, t._ticks, /*useTool=*/false, /*field44=*/-1);
        }
        Push(stack, act);
    }
    // SendComponentMessage(7158, a4=0) toward *param_2
    {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* destIBP = *targetRef;
            uint32_t msgId = MsgId7158();
            ComponentEntity* entityIBP = (*citizenRef)->_entity.Get();
            act = new (mem) SendComponentMessage(&entityIBP, &msgId, 0, &destIBP, 0);   // 0x1400cb1d0
        }
        Push(stack, act);
    }
    PushMoveTo(citizenRef, stack, 1 /*Slow*/, param_3, 0xe1f);
}

// ============================================================
// 0x1400bb490 — Pathfinding_WalkTowards  (called by State_Work / State_FindWork) — disasm-verified.
// lead SendComponentMessage(70e8) -> +0x58 ; then if param_6==0: CompleteTask(param_2,...)
// else: SendComponentMessage(7158, a4=param_4) ; PerformWork(type 8, animIdx literal -1000.0f,
// time = param_5) ; MoveTo(Slow).
//   param_1 = CitizenComponent IBP ; param_2 = u32 (CompleteTask first arg) ; param_3 = float* dest ;
//   param_4 = u32 (msg a4) ; param_5 = float (PerformWork time) ; param_6 = byte flag.
// ============================================================
void CitizenAI_Pathfinding_WalkTowards_bb490(CitizenComponent* const* citizenRef, uint32_t param_2, float* param_3,
                                             uint32_t param_4, float param_5, uint32_t param_6) {
    CharacterComponent* ch = nullptr;
    ActionStack* stack = PrologueResolveStack(citizenRef, &ch);
    InstallLeadMessage(citizenRef, ch);

    {
        Action* act = nullptr;
        if (static_cast<char>(param_6) == '\0') {
            void* mem = MemA(0x38);
            if (mem) {
                ComponentEntity* destIBP = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
                CitizenComponent* entityIBP = *citizenRef;  // local_48 = *param_1
                act = new (mem) CompleteTask(param_2, &entityIBP, &destIBP, 0);   // 0x1400c5da0
            }
        } else {
            void* mem = MemA(0x40);
            if (mem) {
                ComponentEntity* destIBP = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
                uint32_t msgId = MsgId7158();
                ComponentEntity* entityIBP = (*citizenRef)->_entity.Get();
                act = new (mem) SendComponentMessage(&entityIBP, &msgId, param_4, &destIBP, 0);   // 0x1400cb1d0
            }
        }
        Push(stack, act);
    }
    // PerformWork (type 8) animIdx 0xc47a0000 (-1000.0f), time = param_5
    {
        void* mem = MemA(0x48);
        Action* act = nullptr;
        if (mem) {
            CitizenComponent* entityVal = *citizenRef;
            System::Time t = {}; RtSystem_Time_Ctor(&t, param_5);
            act = ::new (mem) PerformWork(&entityVal, -1000.0f, 8, t._ticks, /*useTool=*/false, /*field44=*/-1);
        }
        Push(stack, act);
    }
    PushMoveTo(citizenRef, stack, 1 /*Slow*/, param_3, 0xe1f);
}

// ============================================================
// 0x1400ba6b0 — PlayAnimation_Action_Ctor4  (sibling of the ported Ctor5 c26c0)
// lead SendComponentMessage(70e8) -> +0x58 ; if param_5: SendComponentMessage(7158, a4=8) ;
// if *param_2: SendComponentMessage(DAT_1401c42c0, dest=*param_2, a4=0) ; a raw inline
// PlayAnimation action (vtable_PlayAnimation, +0x20=entity, +0x28=0, +0x30=Time(param_4)) ;
// MoveTo(Slow, dest=param_3).
//   param_1 = CitizenComponent IBP ; param_2 = &optionalTargetIBP ; param_3 = u32* dest(=float*) ;
//   param_4 = float anim time ; param_5 = byte flag.
// ============================================================
void CitizenAI_PlayAnimationCtor4_ba6b0(CitizenComponent* const* citizenRef, ComponentEntity* const* targetRef,
                                        float* param_3, float param_4, uint32_t param_5) {
    CharacterComponent* ch = nullptr;
    ActionStack* stack = PrologueResolveStack(citizenRef, &ch);
    InstallLeadMessage(citizenRef, ch);

    // optional SendComponentMessage(7158, a4=8)
    if (static_cast<char>(param_5) != '\0') {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* destIBP = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
            uint32_t msgId = MsgId7158();
            ComponentEntity* entityIBP = (*citizenRef)->_entity.Get();
            act = new (mem) SendComponentMessage(&entityIBP, &msgId, 8, &destIBP, 0);   // 0x1400cb1d0
        }
        Push(stack, act);
    }
    // optional SendComponentMessage(DAT_1401c42c0, dest=*param_2)
    if (*targetRef != nullptr) {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            ComponentEntity* destIBP = nullptr;   // retail: InternalBasePtr(nullptr) in a stack slot
            uint32_t msgId = HappinessComponent::TypeId();   // [0x1401c42c0] -> OnMessage 0x14004a780
            ComponentEntity* entityIBP = *targetRef;  // local_res10 = *param_2
            act = new (mem) SendComponentMessage(&entityIBP, &msgId, 0, &destIBP, 0);   // 0x1400cb1d0
        }
        Push(stack, act);
    }
    // raw inline PlayAnimation action (0x40 bytes)
    {
        void* mem = MemA(0x40);
        Action* act = nullptr;
        if (mem) {
            System::Time t = {}; RtSystem_Time_Ctor(&t, param_4);
            ComponentEntity* entityIBP = (*citizenRef)->_entity.Get();
            // ENDGAME B.3.9 EXA: the real class (was the `??0Internal` door and
            // two stamps); then retail's inlined leaf stores.
            PlayAnimation* a = ::new (mem) PlayAnimation();
            a->_field20         = entityIBP;   // raw IBP copy, as retail
            a->animation        = 0;
            a->_field30         = t;
            a->_field38._ticks  = 0;
            act = a;
        }
        Push(stack, act);
    }
    PushMoveTo(citizenRef, stack, 1 /*Slow*/, param_3, 0xe1f);
}
