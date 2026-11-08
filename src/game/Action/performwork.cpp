// src/game/Action/performwork.cpp
// Clean C++ reimplementation of PerformWork action methods.
// Source: Game/Action/PerformWork.cpp (from profile build debug strings)
//
// PerformWork -- 0x48 bytes. Generic work animation action used by every
// citizen performing work. Handles tool durability, facing direction via yaw,
// elapsed vs required time tracking.
// Inherits: Object::Internal -> Action (0x20) -> PerformWork
// Vtable at 0x14015fea0 (10 slots).
//
// Vtable (10 slots):
//   [0] +0x00 ScalarDtor    = 0x140022BD0
//   [1] +0x08 VectorDtor    = 0x140022B80 (returns DAT_1401c5fe0, unhookable)
//   [2] +0x10 BaseDtor      = 0x140022B70 (returns TypeInfo, unhookable)
//   [3] +0x18 Serialize     = 0x1400C9550
//   [4] +0x20 AbstractImpl  = 0x1400C9420 (animation + tool logic)
//   [5] +0x28 Execute       = 0x1400C93C0 (time accumulation + completion check)
//   [6] +0x30 Enter         = 0x1400C4B90 (base Action, void nop)
//   [7] +0x38 Exit          = 0x1400C9370 (reset animation)
//   [8] +0x40 CheckEntity   = 0x1400C9320 (reset animation, same as Exit)
//   [9] +0x48 GetTarget     = 0x1400C92D0 (compare entity parent)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "PerformWork.h"
#include "object/TypeInfo.h"
#include "Action/exe_bodies.h"   // ActMv_* helpers
#include "Game/exe_constants.h"  // kRdata_*
#include "math/Math.h"           // Math::Vector3::FromYawPitch, Math::Scalar
#include <cstring>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "Object/factory_registration.h"   // Object::GetTypeIdByName
#include <cstdio>
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Component/CitizenComponent.h"   // ENDGAME B.3.8 CB3: CitizenComponent::TypeId()
#include "Component/CitizenDescription.h"
#include "Component/Description.h"
#include "Component/CharacterComponent.h"   // B4 S4 RT2: InternalPtr<CharacterComponent>::Get() needs the class complete
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete

// ============================================================
// Helpers, called by name (B3R)
//   0x1400012d0 ActMv_FindComponentFamily_12d0 (Entity_FindComponent_Family)
//   0x14010a1f0 ActMv_SetAnimation_a1f0 (AnimalComponent_impl_a1f0) -- SetAnimation
//               (familyComponent, animId, param3, param4)
//   0x14010a060 ActMv_SetFacingDirection_a060 (PerformWork_helper_a060) -- SetFacingDirection
//               (familyComponent, vector3)
//   import slot 0x1401512a0 ?FromYawPitch@Vector3@Math@@ -> Math::Vector3::FromYawPitch
//   0x140116650 ActMv_CitizenBreakCheck_116650 (PerformWork_helper_6650) -- citizen break
//               check (entity) -- returns bool (true = needs break)
//
// Float constants (Game/exe_constants.h): 0x14017214c = 1.0f, 0x1401722a0 = -180.0f
// (yaw lower bound), 0x140172220 = 180.0f (yaw upper bound).
//
// TypeInfo for IsDerivedFrom check in Serialize -- cell 0x1401c7158, read directly
// through CitizenComponent::TypeId() (D5, ENDGAME B.3.9 CG3). The env-gated
// TypeId_CitizenComponent_FC / by-name fallback pair this used to juggle read the
// SAME cell and always agreed (verified in the 2026-08-26 graduation A/B), so there
// is nothing left to fall back to.
//
// ENDGAME B.3.9 CB: retail reads this record field ONLY in the class's own TypeName
// slot (0x1401c5fe0 = the NAME field (+8) of PerformWork's record, read by
// PerformWork_Slot1 0x140022b80); DECLARE_EXE_TYPE answers that slot, so the tree's copy of it is gone.
// ============================================================

namespace {
// The .rdata float constants (Game/exe_constants.h) that are stored as bit patterns.
inline float RdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }
}  // namespace

// ============================================================
// PerformWork::Serialize (0x1400C9550)
// Serializes: status(+0x18), entity(+0x20) with CitizenAI type check,
//   float(+0x28), int(+0x2C), Time(+0x30), Time(+0x38), bool(+0x40), int(+0x44)
// ============================================================
void PerformWork::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Second serializer: serialize all PerformWork-specific fields
    IO::Serializer ser(stream, 0);

    // The working citizen (+0x20)
    workTarget.InternalBasePtr::Serialize(ser);
    if (!workTarget.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(workTarget, CitizenComponent::TypeId())) {
            workTarget = nullptr;
        }
    }

    ser.Serialize(facingYaw, nullptr);     // +0x28 float
    ser.Serialize(animationId, nullptr);   // +0x2C int

    // Serialize required time (+0x30) and elapsed time (+0x38) as Time
    requiredTime.Serialize(ser);
    elapsedTime.Serialize(ser);

    ser.Serialize(useTool, nullptr);   // +0x40 bool
    ser.Serialize(toolType, nullptr);   // +0x44 int
}

// ============================================================
// PerformWork::AbstractImpl (0x1400C9420) -- slot 4
// Sets animation on the citizen's CharacterComponent, sets facing direction
// if yaw is in valid range, and optionally decrements tool durability.
// Always returns 1 (action stays active, Execute checks completion).
// ============================================================
int  PerformWork::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;

    uint32_t animId = static_cast<uint32_t>(animationId);

    // The citizen's CharacterComponent (+0x278): play the work animation
    workTarget->character.Get()->SetAnimation(animId, kRdata_14017214c, 0.0f);   // 0x14010a1f0

    // If yaw is in valid range [-180, 180], set facing direction
    float yaw = facingYaw;
    if (yaw >= RdataF(kRdata_1401722a0) && yaw <= kRdata_140172220) {
        float yawVal = yaw;
        float pitch = 0.0f;

        // The CharacterComponent found on the citizen's entity
        CharacterComponent* familyPtr = workTarget->_entity.Get()->FindComponent<CharacterComponent>();   // 0x1400012d0

        // Convert yaw to direction vector and set it
        Math::Vector3 dirVec = Math::Vector3::FromYawPitch(Math::Scalar(yawVal), Math::Scalar(pitch));
        familyPtr->SetFacingDirection(dirVec);   // 0x14010a060
    }

    // If use tool flag is set, decrement tool durability
    if (useTool) {
        if (workTarget->toolUses > 0) {
            int32_t* toolDurability = &workTarget->toolUses;
            (*toolDurability)--;
            if (*toolDurability < 0) {
                *toolDurability = 0;
            }
        }
    }

    return 1;
}

// ============================================================
// PerformWork::Execute (0x1400C93C0) -- slot 5
// Accumulates elapsed time (+0x38) from delta time.
// When elapsed >= required, checks if citizen needs a break.
// Returns: 0 = done, 1 = still working, 2 = needs break
// ============================================================
int  PerformWork::Execute(ActionStack* actionStack, const System::Time* time) {
    (void)actionStack;

    int64_t elapsed = elapsedTime._ticks;
    int64_t required = requiredTime._ticks;

    if (elapsed >= required) {
        // Work time complete -- check if citizen needs a break
        uint8_t needsBreak = workTarget.Get()->BreakCheck();   // 0x140116650
        // If needs break, return 2; otherwise return 0 (done)
        return needsBreak ? 2 : 0;
    }

    // Still working -- accumulate delta time
    int64_t dt = time->_ticks;
    elapsedTime._ticks = elapsed + dt;
    return 1;
}

// ============================================================
// PerformWork::Exit (0x1400C9370) -- slot 7
// Resets character animation to idle (animation 0, rate 1.0, 1.0).
// ============================================================
void PerformWork::Exit(ActionStack* actionStack) {
    (void)actionStack;
    workTarget->character.Get()->SetAnimation(0, kRdata_14017214c, kRdata_14017214c);   // 0x14010a1f0
}

// ============================================================
// PerformWork::CheckEntity (0x1400C9320) -- slot 8
// Identical to Exit: resets character animation to idle.
// ============================================================
void PerformWork::CheckEntity() {
    workTarget->character.Get()->SetAnimation(0, kRdata_14017214c, kRdata_14017214c);   // 0x14010a1f0
}

// ============================================================
// PerformWork::GetTarget (0x1400C92D0) -- slot 9
// Returns true if param matches the citizen's owning entity (Component +0x10).
// ============================================================
bool PerformWork::GetTarget(ComponentEntity** param) {
    return workTarget->_entity.Get() == *param;
}

// ============================================================
// class PerformWork (PerformWork.h) — ENDGAME B.3.8 ACT.
// The hand-built g_PerformWork_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x140022b90. Retail's default ctor (0x1400c96b0) stores +0x44 = -1;
// the tree's factory zero-filled every field instead. B38 B8-1: the retail stores are kept; the
// fields retail leaves unwritten stay zero-filled (MemAlloc gives no guarantee).
PerformWork::PerformWork()
    : workTarget(nullptr),
      facingYaw(0.0f),
      animationId(0),
      requiredTime{0},
      elapsedTime{0},
      useTool(false),
      _pad41{},
      toolType(-1) {}

// [3] 0x1400c9550
// [4] 0x1400c9420
// [5] 0x1400c93c0
// [7] 0x1400c9370
// [8] 0x1400c9320
// [9] 0x1400c92d0

::Object::Base* PerformWork::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PerformWork), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PerformWork());
}
::Object::Base* PerformWork::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PerformWork());
}


// ============================================================
// FUN_1400c9720 — PerformWork_Action Ctor (7-field variant)
// Base Internal ctor, Action -> PerformWork vtable; copies entity IBP (+0x20), anim
// index (+0x28, float arg XMM2), action type (+0x2c), Time (+0x30), flag (+0x40), int
// (+0x44). If the flag is set and the entity's +0xac < 1, scales the Time by the
// component's +0x114 seconds (Time::Time(comp.f114); self.time = AsSeconds * AsSeconds).
// animIndex arrives in XMM2 (a float); keep it as a 32-bit store at +0x28.
// ============================================================

// 0x1400c9720 — the ctor body: after Action() the retail stores, in retail order.
PerformWork::PerformWork(CitizenComponent* const* citizen, float animIndex, uint32_t actionType,
                         int64_t time, bool useToolArg, int param_7) {
    workTarget = *citizen;   // +0x20 (the InternalPtr's own ctor stored 0 first, as retail)
    facingYaw = animIndex;
    animationId = static_cast<int32_t>(actionType);
    requiredTime._ticks = time;
    elapsedTime._ticks = 0;
    useTool = useToolArg;
    toolType = param_7;
    if (useToolArg) {
        if ((*citizen)->toolUses < 1) {
            // 0x1400c97d1-0x1400c97ee: comp = IsDerivedFrom(desc, CitizenDescription) ? desc
            // : null (CMOVNZ), then +0x114 is read with no test (a miss faults, as retail).
            Description* obj = workTarget->_description;
            const CitizenDescription* comp =
                obj->IsDerivedFrom(CitizenDescription::TypeId()) ? static_cast<const CitizenDescription*>(obj)
                                                                 : nullptr;
            // 0x1400c97fb Time(comp->toolPenalty); 0x1400c9808 / 0x1400c9814 Time::AsSeconds on
            // requiredTime and on that Time; MULSS; 0x1400c9826 Time(product) into +0x30.
            System::Time scaleTime;
            scaleTime._ticks = 0;
            RtSystem_Time_Ctor(&scaleTime, comp->toolPenalty);
            float selfSecs  = requiredTime.AsSeconds();
            float scaleSecs = scaleTime.AsSeconds();
            System::Time prod;
            prod._ticks = 0;
            RtSystem_Time_Ctor(&prod, selfSecs * scaleSecs);
            requiredTime = prod;
        }
    }
}

DEFINE_EXE_TYPE(PerformWork, "Action")
