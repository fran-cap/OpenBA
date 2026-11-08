// src/game/Action/action_base_movedeps.cpp
// Wave 3 (Action) — closure-safe code sub-callees of the MoveTo "unsafe" helpers.
//
// These two exe helpers are the *only* members of the move-helper sub-callee set whose
// own transitive closure is fully satisfiable in standalone (engine_funcs export-resolved
#include "Action/exe_bodies.h"
// pointers + already-registered helpers + data globals). Registering them unblocks wiring
// the two closure-safe members of the deferred move-unsafe-6 set:
//   - 0x1401250f0 (this file)  ->  unblocks 0x14010a1f0 SetAnimation
//   - 0x1400a9290 (this file)  ->  unblocks 0x140116650 CitizenBreakCheck (and is also a
//                                  callee of 0x14010a1f0's helper, indirectly)
//
// The remaining move sub-callees (0x14009c480 ConnRefresh, 0x1400a68b0 / 0x1400a6a30 /
// 0x1400a6b20 / 0x1400a6c10 terrain-cost queue ops) are NOT registered here: they are
// commands to a background pathfinding worker Thread — they call System::Thread::Start and
// depend on FUN_1400a6430 -> FUN_14001ce00 (unregistered path-builder) and 9c480 ->
// FUN_14009c170 -> FUN_14001a9b0 (the deferred ZoneRefresh region flood-fill). Wiring them
// would either start an unwired background thread or build half-populated path records —
// strictly worse than the safe no-op of leaving them skipped. They stay deferred (and so do
// e550 / 9cfb0 / c8c10 that depend on them).
//
// Each function below is a 1:1 port of the decompiled exe helper (Ghidra), ABI verified
// against the disassembly. Do NOT "improve" behavior.
//

// The runtime type header comes FIRST: graphics/Mesh.h reaches
// src/engine/volume/Volume.h, whose global-scope `struct Rectangle` is hidden
// by <wingdi.h>'s `Rectangle()` once <windows.h> has been seen through resolve.h.
#include "graphics/Mesh.h"                 // ENDGAME B.3.5 batch K1
#include "object/External.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/exe_constants.h"   // kRdata_*
#include <cstring>
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include <cstdint>
#include "Component/ModelDescription.h"   // ENDGAME B.3.8 CB3: ModelDescription::TypeId()
#include "Component/ModelComponent.h"
#include "Entity/ComponentEntity.h"

// ============================================================
// FUN_1400a9290 — task-due / break probability check.
// ============================================================
// bool fn(Time* time, float* pair, Random* rng)
//   pair[1] (the weight) must be > 0, else clears *time and returns false.
//   Otherwise: denom = pair[0] * DAT_1401721b8 * DAT_140172234 * pair[1];
//   roll = AsSeconds(time) / denom; clears *time; returns RandFloat(rng) < roll.
// Verified ABI (disasm 0x1400a9290): RCX=time, RDX=pair, R8=rng; AsSeconds slot 0x140151318,
// RandFloat slot 0x1401512f8 (both are System::Time / System::Random members now);
// constants DAT_1401721b8 / DAT_140172234. The original reads BOTH pair[0] and pair[1], so
// the caller must hand it 8 bytes (two floats), not one.
// The constants are Game/exe_constants.h's (B3R): DAT_1401721b8 = 12.0f (stored as a
// bit pattern), DAT_140172234 = 300.0f.
namespace {
inline float RdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }
} // namespace

bool BreakTimeCheck(System::Time* time, const float* pair, System::Random* rng) {
    float weight = pair[1];
    if (!(0.0f < weight)) {                       // weight <= 0 (COMISS 0 < weight)
        time->_ticks = 0;
        return false;
    }
    float k1 = RdataF(kRdata_1401721b8);   // 0x1401721b8
    float k2 = kRdata_140172234;           // 0x140172234
    float denom = pair[0] * k1 * k2;
    float secs = time->AsSeconds();
    time->_ticks = 0;
    float roll = rng->RandFloat();
    return roll < secs / (denom * weight);
}


// ============================================================
// AnimalComponent_helper_50f0 (0x1401250f0) — start a mesh animation.
// ============================================================
// void fn(animObj* self, uint32_t animId, float p3, float p4, float p5)
//   obj = *(self+0x18); if IsDerivedFrom(obj, FenceDescription type) keep it, else null.
//   Time tB(p3), tA(p4).  animData = *(*(obj+0x20) + (short)self[0x164]*0x18).
//   MeshInstance(self+0x28)::PlayAnimation(animData, animId, tB, tA, p5).
//   self[0x20] |= 4; if the low-16 bits changed and self+0x10 != 0, CheckPtr(self+0x10)
//   then NotifyEntityChanged (FUN_1400c3470, already registered).
// Verified ABI (disasm 0x1401250f0): RCX=self, EDX=animId, XMM2=p3, XMM3=p4, [stack]=p5;
// IsDerivedFrom slot 0x140152a98, Time::Time(float) slot 0x140152b00, PlayAnimation slot
// 0x140152988 (?PlayAnimation@MeshInstance@Graphics@@QEAAXV?$ExternalPtr@VAnimationGroup@
// Graphics@@@Object@@HAEBVTime@System@@1M@Z), CheckPtr slot 0x140152ab0, FUN_1400c3470.
// Time is passed by const-ref (pointer); ExternalPtr<AnimationGroup> animData is a single
// pointer-sized value passed by value.
// B3R: ??0Time@System@@QEAA@M@Z is RtSystem_Time_Ctor, 0x1400c3470 is
// ActHunt_NotifyEntityChanged_c3470, both called by name.

void ModelComponent::PlayAnimation(uint32_t animId, float p3, float p4, float p5) {
    // B6 (14d G57): retail tests neither the model nor the narrowed description: it reads
    // +0x18, narrows it (IsDerivedFrom, ModelDescription cell 0x1401c7a88; CMOVNZ to 0), and
    // reads [desc+0x20] + meshGroup * 0x18 for PlayAnimation with no test.
    // The type-gate validates the ModelComponent's description is a ModelDescription
    // (DAT_1401c7a88; its DATA writer is ModelDescription_Register @ 0x14014f435). Without
    // it the gate fails → PlayAnimation skipped → the skinned bone palette stays frozen
    // (RenderDoc-verified).
    Description* obj = _description;
    const ModelDescription* animHolder = obj->IsDerivedFrom(ModelDescription::TypeId())
                                         ? static_cast<const ModelDescription*>(obj) : nullptr;

    System::Time timeA = {};  // p4
    System::Time timeB = {};  // p3
    RtSystem_Time_Ctor(&timeA, p4); RtSystem_Time_Ctor(&timeB, p3);

    short animIndex = meshGroup;
    Graphics::AnimationGroup* animData = animHolder->_meshGroups[static_cast<uintptr_t>(animIndex)].animations;
    // PlayAnimation(meshInstance, animData, animId, &tB=p3, &tA=p4, p5)
    meshInstance.PlayAnimation(Object::ExternalPtr<Graphics::AnimationGroup>(animData), static_cast<int>(animId),
                                      &timeB, &timeA, p5);

    uint32_t old = _flags;
    uint32_t nw = old | 4u;
    _flags = nw;
    if (((nw ^ old) & 0xffff) != 0 && _entity != nullptr) {
        _entity.Get()->RefreshEntityFlags();   // 0x1400c3470
    }
}

