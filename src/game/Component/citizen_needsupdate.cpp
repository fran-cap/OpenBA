// src/game/Component/citizen_needsupdate.cpp
// Economy Brick M-D (citizen/eat-message half) — CitizenComponent message
// dispatcher (vtable slot +0x78 / index 15) + the msg-0 warm-house helper.
//
// Faithful ports (Ghidra-verified against Application-x64.exe disassembly):
//   0x140112260  CitizenComponent_NeedsUpdate   — the switch(msg) dispatcher the
//                SendComponentMessage action executes via component vtable+0x78.
//   0x1401121e0  CitizenComponent_helper_21e0   — msg 0: if the target entity has
//                a ResidenceComponent (finder 0x140008090, type DAT_1401c7cf0)
//                that passes the warm-house check FUN_14012ac30, zero the
//                citizen's thirst (+0x48).
//
// msg 2 (EAT) delegates to CitizenComp_ReleaseWorkplace (0x1401152e0) in
// citizencomponent.cpp — NOTE the retail Ghidra label "ReleaseWorkplace" is a
// MISNOMER: the function is EatFromStorage (drains hunger +0x44 against the
// target's StorageComponent, one unit per g_fUVScale). A named alias
// CitizenComp_EatFromStorage is provided below.
//
// Ported here + registered by standalone/reg_citizen_25.cpp (openfuncs step 2.5c):
//   0x14010fde0  CitizenComp_helper_fde0 (msg 1: happiness credit)
//   0x1401117a0  CitizenComp_helper_17a0 (msg 4: attendance/schedule budget)
//   0x140111540  CitizenComp_helper_1540 (msg 5: countdown decay)
//   The dispatcher resolves each via ResolveAddr (now non-NULL) and calls it like
//   every other msg; the msg-1 inline duplicate was retired in favour of the port.
// Already ported/registered elsewhere (NOT duplicated):
//   0x1401152e0  CitizenComp_ReleaseWorkplace   (citizencomponent.cpp, reg_component_people)
//   0x140088f90  GetResourceCategory            (action_base_rawmat.cpp, reg_action_rawmat)
//   0x14012ac30  ResidenceCheck_2ac30           (citizenai_findhelpers.cpp, reg_citizenai_findhelpers)
//   0x140008090  Entity_FindComponent_Residence (citizenaicomponent.cpp, reg_component_people)
//
// Disasm notes (0x140112260):
//   - Signature: RCX=this, EDX=msg (0..8, JA out-of-range returns), R8=ptr to the
//     target-entity IBP VALUE (msg 0/2 copy *R8 to a stack local and pass its
//     address), R9D=int param (only msg 1 consumes it: MOV EDX,R9D).
//   - msg 3: +0x54 |= 0x200; +0x50 += DAT_140172114 (=0.5f in retail .rdata).
//   - msg 6/7: +0x1e0/+0x1e4 = 1/1 (2/2), then the shared tail:
//     Time ctor (dispatch slot [0x140152b00], RCX=&out XMM1=secs) of
//     desc(+0x288)+0x130 * DAT_140172234 (=300.0f), then +0x88 |= 0x800 and the
//     8-byte Time stored at +0x1e8.
//   - msg 8: +0x88 |= 0x40000.
//

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"    // B.4 W1 COMP-b: description fields by name
#include "Component/ResidenceComponent.h"
#include "Component/StatusIconComponent.h"   // statusIcon->SetFlag (0x14012c3f0)
#include "Entity/ComponentEntity.h"           // FindComponent<T>
#include "Component/exe_bodies.h"
#include "Game/exe_constants.h"

// A .rdata constant stored as its bit pattern (exe_constants.h), read as the float.
static inline float RdataF(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// ============================================================
// CitizenComponent::AddHappiness (0x14010fde0) — msg 1: happiness credit.
// Refresh the citizen's StatusIconComponent (+0x280, mode 4 = happiness icon) then
// accumulate the credit: +0xac is the running happiness total, +0xa8 the last delta.
// 0x14010fe01-0x14010fe0f: +0x280 handed to SetFlag with no test (B8 RES, row 310).
// 0x14012c3f0 is StatusIconComponent::SetFlag.
// ============================================================
void CitizenComponent::AddHappiness(int param) {
    statusIcon->SetFlag(4, 0);
    field_0xAC += param;
    field_0xA8  = param;
}

// ============================================================
// CitizenComponent::AdvanceSchooling (0x1401117a0) — msg 4: a school lesson.
// +0x210 (education) is bumped every call. If it is still under the descriptor's
// maximum (desc+0x118) AND the time spent in school (+0x218, AsSeconds) is still
// within budget (desc+0x11c × 3600s/year), do nothing. On overrun: education = the
// maximum, flag +0x88 |= 0x400, leave the school (no reroll), and post life-event 9
// via PostLifeEvent. (Retail derefs +0x288 unguarded.)
// ============================================================
void CitizenComponent::AdvanceSchooling() {
    int32_t* counter = &education;
    *counter += 1;
    CitizenDescription* desc = description;   // 0x1401117b0, no test (B8 RES, row 310)
    int32_t threshold  = desc->maxEducation;
    float   perUnitSec = desc->_f11c;
    float budgetSecs = perUnitSec * RdataF(kRdata_14017225c);   // 3600.0f
    float elapsed = schoolTime.AsSeconds();
    if (*counter < threshold && elapsed <= budgetSecs) return;

    *counter = threshold;
    happinessFlags |= 0x400;
    LeaveSchool(0);
    PostLifeEvent(9, 0, nullptr);
}

// ============================================================
// CitizenComponent::ShortenDisease (0x140111540) — msg 5: countdown decay.
// Gated on the disease (+0x68). When set, build a Time of desc+0x12c × 300s and
// subtract its tick value from the +0x70 disease timer.
// ============================================================
void CitizenComponent::ShortenDisease() {
    if (disease == nullptr) return;
    CitizenDescription* desc = description;   // 0x140111550, no test (B8 RES, row 310)
    float base = desc->_f12c;
    System::Time t{};
    RtSystem_Time_Ctor(&t, base * kRdata_140172234);
    field_0x70._ticks -= t._ticks;
}

// ============================================================
// CitizenComponent::WarmAt (0x1401121e0) — msg 0 (warm house).
// If the target entity has a ResidenceComponent (finder 0x140008090) whose heat
// passes FUN_14012ac30 (warm-house check), zero the citizen's coldness (+0x48).
// Faithful: the finder is called TWICE (retail re-runs it after the first
// null-check). ResidenceCheck_2ac30 is ResidenceComponent's body (LEFT free).
// ============================================================
void CitizenComponent::WarmAt(ComponentEntity* target) {
    if (target == nullptr) return;

    ResidenceComponent* r1 = target->FindComponent<ResidenceComponent>();
    if (r1 == nullptr) return;

    ResidenceComponent* r2 = target->FindComponent<ResidenceComponent>();
    if (r2->CheckHeating()) {
        coldness = 0.0f;
    }
}

// ============================================================
// CitizenComponent::OnMessage (0x140112260) — the component message handler
// dispatched by SendComponentMessage::AbstractImpl via vtable+0x78.
//   this, msg, data = ptr to the target-entity IBP value,
//   arg0 = int extra (+0x38 of the action), arg1 = +0x3c (unused in retail).
// ============================================================

// [15] 0x140112260 — the component message handler SendComponentMessage
// (0x1400caef0) dispatches: EDX = message id, R8 = &target entity IBP, R9D =
// extra (read from the retail body's jump table).
void CitizenComponent::OnMessage(int message, void* data, uint32_t arg0, uint32_t /*arg1*/) {
    ComponentEntity* const* param_3 = static_cast<ComponentEntity* const*>(data);   // Component::OnMessage (void* data, Component.h)  // CAST-OK: user_data
    int param_4 = static_cast<int>(arg0);
    if (static_cast<unsigned>(message) > 8) return;   // retail: CMP EDX,8 / JA ret

    switch (message) {
    case 0: {   // warm-house: zero coldness if the target residence passes 2ac30
        WarmAt(*param_3);
        return;
    }
    case 1: {   // happiness credit (0x14010fde0)
        AddHappiness(param_4);
        return;
    }
    case 2: {   // EAT — drain hunger against the target's storage (0x1401152e0)
        EatFrom(*param_3);
        return;
    }
    case 3: {   // +0x54 |= 0x200 ; +0x50 += DAT_140172114 (0.5f)
        statusFlags |= Bits(CitizenStatus::UsedHealthItem);
        health += kRdata_140172114;   // 0.5f
        return;
    }
    case 4: {   // 0x1401117a0
        AdvanceSchooling();
        return;
    }
    case 5: {   // 0x140111540
        ShortenDisease();
        return;
    }
    case 6:
        field_0x1E0 = 1;
        clothingLevel = 1;
        break;      // falls through to the shared Time tail
    case 7:
        field_0x1E0 = 2;
        clothingLevel = 2;
        break;      // falls through to the shared Time tail
    case 8:
        happinessFlags |= 0x40000;
        return;
    default:
        return;
    }

    // Shared tail for message 6/7 (new clothing):
    //   Time t(desc(+0x288)->+0x130 * DAT_140172234 [300.0f]); +0x88 |= 0x800;
    //   clothingTimer (+0x1e8) = t.
    // 0x14011231d: +0x288 read with no test (B8 RES, row 310).
    System::Time t{};
    CitizenDescription* desc = description;
    float base = desc->_f130;
    RtSystem_Time_Ctor(&t, base * kRdata_140172234);
    happinessFlags |= 0x800;
    clothingTimer = t;
}
