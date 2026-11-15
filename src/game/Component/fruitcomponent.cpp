// src/game/Component/fruitcomponent.cpp
// Clean reimplementation of FruitComponent::Update from Application-x64.exe.
//
// Fruit ripeness simulation: accumulates growth progress each tick based on
// temperature sensitivity and seasonal growth factor. Uses a quadratic
// temperature curve -- growth peaks at the optimal temp and falls off as the
// temperature diverges. Result is clamped to [0, ~1] (the upper clamp uses the
// engine's g_fUVScale 0.98 threshold, snapping to exactly 1.0).
//
// Key formula (faithful to 0x140049de0):
//   resData       = IsDerivedFrom(self->_description, FruitDescription) ? self->_description : 0
//   ripeningSpeed = g_fUVScale / (resData->+0x20 * SECS_PER_SEASON)
//   tempDelta     = |currentTemp - resData->+0x24| / resData->+0x28
//   if fruitFlag clear && (*(self->+0x28))->+0x28 >= g_fUVScale:
//     if tempDelta in [0, g_fUVScale]: growthFactor = g_fUVScale - tempDelta^2
//     else if currentTemp <= FREEZE_THRESHOLD: growthFactor = FROST_DAMAGE
//     else: growthFactor = 0.0
//   else: growthFactor = 0.0
//   effRate = min((*(self->+0x28))->+0x28, g_fUVScale)
//   ripeness += dtSeconds * effRate * ripeningSpeed * growthFactor
//   clamp ripeness to [0.0, 1.0]
//
// Original: 0x140049de0 (vtable slot 9, +0x48; vtable @ 0x140159450)
//
// Decompiled from Application-x64.exe using Ghidra.

#include <new>
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Calendar.h"
#include "Game/WeatherSimulation.h"
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/FruitDescription.h"   // ENDGAME B.3.9 C1: 0x1401c4288 cell class
#include "Work/exe_bodies.h"
#include "Component/NaturalResourceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/exe_constants.h"

// ============================================================
// .rdata constants (Game/exe_constants.h) and helpers
// ============================================================

static inline float BitsF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }

// g_fUVScale (DAT_140172140 = 0.98f). Used as the "near-1.0" comparison/clamp
// value throughout Update AND as the ripeningSpeed numerator. (NOT a literal
// 1.0f -- that was the prior port's bug.)
static const float kUVScale         = BitsF(kRdata_140172140);   // 0.98f
static const float kSecsPerSeason   = kRdata_140172234;          // 300.0f
static const float kSecsPerYear     = BitsF(kRdata_14017225c);   // 3600.0f
static const float kMonthsPerYear   = BitsF(kRdata_1401721b8);   // 12.0f
static const float kFreezeThreshold = BitsF(kRdata_1401721d0);   // 32.0f
static const float kFrostDamage     = BitsF(kRdata_140172288);   // -10.0f

// Game world singleton (DAT_1401c3dd8 == g_pGameWorldSingleton)
//   +0x128: pointer to the season-time object (+8 = Time value)
//   +0x130: pointer to the weather/environment data (+0x18 = current temperature)

// FUN_1400052c0 -- find the sibling NaturalResourceComponent IBP on the owner
// entity (the slot-4 OnCreate accessor): ComponentEntity::FindComponent<NaturalResourceComponent>,
// called directly.

// fabsf via the double-abs bitmask the original uses (DAT_1401722b0, matches bitwise pattern).
static inline float AbsViaMask(float v) {
    double d = static_cast<double>(v);
    uint64_t bits;
    __builtin_memcpy(&bits, &d, sizeof(bits));
    bits &= kRdata_1401722b0;
    __builtin_memcpy(&d, &bits, sizeof(d));
    return static_cast<float>(d);
}

// ============================================================
// FruitComponent::Update (vtable slot [9], +0x48, 0x140049de0)
// ============================================================
// Fruit ripening simulation with temperature-dependent growth.

// ===== ENDGAME B.3.8 CIT — FruitComponent as a REAL class (FruitComponent.h) =====
// Before: a 48-slot hand-built FruitComponent_LiveVtable filled at every create
// (slots 3 Serialize, 4 Startup, 9 Update), Inert_ScalarDtor in slot 0. After:
// the compiler's 16-slot vtable and a real ctor/dtor chain (B38_RULES D1, D2).
#include "Object/factory_registration.h"
#include "Component/FruitComponent.h"


// 0x14004a160. Component() is the CALL 0x1401188b0; the 0x140159450 stamp is
// the compiler's; +0x28 = 0, +0x30 (dword) = 0, +0x34 (byte) = 0; flags |= 0x88.
FruitComponent::FruitComponent()
    : resource(nullptr), ripeness(0.0f), fruitFlag(false), _pad35{} {
    OrFlagsInCtor(0x88);
}

// ============================================================
// FruitComponent::Startup / OnCreate (vtable slot [4], +0x20, original 0x14004a030)
// ============================================================
// Body-init: populate resource (+0x28) with the sibling
// NaturalResourceComponent's InternalBasePtr from the owning entity (+0x10).
//
//   CheckPtr(self+0x10);
//   ibp = FUN_1400052c0(*(self+0x10), &local);   // first sibling NatResComp
//   self+0x28 = *(void**)ibp;                     // the wrapped component ptr
//
// Called at spawn by ComponentEntity_StartupComponents (vtable+0x20). _description
// (+0x18, the FruitDescription) is set earlier by the base Component copy-ctor
// (ComponentEntity_CtorFromDescription).
void FruitComponent::Startup() {
    if (!_entity) return;   // not yet wired into an entity (the tree's test)

    // FUN_1400052c0 = FindComponent<NaturalResourceComponent>.
    resource = Object::InternalPtr<NaturalResourceComponent>(
        _entity->FindComponent<NaturalResourceComponent>());
}

// [9] 0x140049de0. RDX is a pointer to the elapsed System::Time (EntityList
// bucket tick 0x1400c42af).
void FruitComponent::Update(const System::Time* time) {
    const System::Time* pDeltaTime = time;

    // [LIVE-COMPONENT BRING-UP GUARD] On an inert-spawned standalone component the
    // body is zeroed unless Startup ran. _description(+0x18) is the FruitDescription set
    // by the copy-ctor; resource(+0x28) is the sibling NaturalResourceComponent IBP
    // populated by FruitComponent_Startup (slot 4), which the spawn pipeline
    // invokes via vtable+0x20 in ComponentEntity_StartupComponents. Belt-and-
    // suspenders: if +0x28 is still null but the description(+0x18) is present, run
    // the body-init now (idempotent); if still incomplete, bail safely -- the math
    // below divides by resData->+0x20 and derefs *(this+0x28). See growthcomponent.cpp
    // for the worked example / docs/history/notes/STANDALONE_REG_WAVES_PLAN.md.
    if (resource.Get() == nullptr) {
        if (_description != nullptr)
            FruitComponent::Startup();
        if (resource.Get() == nullptr)
            return;
    }

    const float fUV = kUVScale;  // ~1.0 comparison/clamp

    // resData = IsDerivedFrom(_description, FruitDescription) ? _description : 0
    Description* baseObj = _description;
    FruitDescription* resData = nullptr;
    if (baseObj) {
        if (::Object::IsDerivedFromNullSafe(baseObj, FruitDescription::TypeId()))
            resData = static_cast<FruitDescription*>(baseObj);
    }
    // [STANDALONE TRUST] The factory/type tree can be incomplete in standalone, so
    // IsDerivedFrom on a description is unreliable (see growthcomponent.cpp / the
    // SpawnComponent precedent). _description is the FruitDescription the copy-ctor stored
    // at +0x18 — trust it as a fallback so a present fruit entity still simulates.
    if (!resData && baseObj) resData = static_cast<FruitDescription*>(baseObj);
    if (!resData) return;  // original derefs resData->+0x20/+0x24/+0x28 below

    // ripeningSpeed = g_fUVScale / (resData->+0x20 * SECS_PER_SEASON)
    float secsPerSeason = kSecsPerSeason;
    float ripeningSpeed = fUV / (resData->_ripenSeasons * secsPerSeason);

    // Seasonal flag-clear: when fruitFlag(+0x34) set, clear it once the month index
    // leaves the fruit-bearing window.  month = (seasonSecs/3600)*12 - 7; clear if
    // (uint)month > 4.
    if (fruitFlag != 0) {
        if (GameUpdateLoop::_singleton != nullptr) {
            const Calendar* dayCounter = GameUpdateLoop::_singleton->m_dayCounter;   // +0x128
            float seasonSecs = dayCounter->yearTime.AsSeconds();   // +0x08
            float secsPerYear   = kSecsPerYear;
            float monthsPerYear = kMonthsPerYear;
            unsigned int monthIdx =
                (unsigned int)((int)((seasonSecs / secsPerYear) * monthsPerYear) - 7);
            if (monthIdx > 4)
                fruitFlag = false;
        }
    }

    // currentTemp = *(*(world+0x130)+0x18)
    float currentTemp = 0.0f;
    if (GameUpdateLoop::_singleton != nullptr) {
        const WeatherSimulation* weather = GameUpdateLoop::_singleton->m_weather;   // +0x130
        currentTemp = weather->temperature;   // +0x18
    }

    // tempDelta = |currentTemp - resData->+0x24| / resData->+0x28
    float optimalTemp = resData->_optimalTemp;
    float tempRange   = resData->_tempRange;
    float tempDelta   = AbsViaMask(currentTemp - optimalTemp) / tempRange;

    float growthFactor = 0.0f;
    if (!fruitFlag) {
        // growth rate is the linked NaturalResourceComponent's resourceScale (+0x28).
        float growthRate = resource.Get()->resourceScale;
        if (growthRate >= fUV) {
            if (tempDelta >= 0.0f && tempDelta <= fUV) {
                // Quadratic falloff: g_fUVScale - delta^2
                growthFactor = fUV - tempDelta * tempDelta;
            } else {
                growthFactor = 0.0f;
                float freezeTemp = kFreezeThreshold;
                if (currentTemp <= freezeTemp)
                    growthFactor = kFrostDamage;
            }
        }
        // growthRate < g_fUVScale -> growthFactor stays 0.0
    }

    // effRate = min((*(this+0x28))->+0x28, g_fUVScale)
    float effRate = resource.Get()->resourceScale;
    if (effRate >= fUV) effRate = fUV;

    // ripeness(+0x30) += dtSeconds * effRate * ripeningSpeed * growthFactor
    float dtSeconds = pDeltaTime->AsSeconds();
    float newRipeness = dtSeconds * effRate * ripeningSpeed * growthFactor + ripeness;
    ripeness = newRipeness;

    // Clamp to [0.0, 1.0]: < 0 -> 0; > g_fUVScale -> exactly 1.0.
    if (ripeness < 0.0f)
        ripeness = 0.0f;
    if (ripeness > fUV)
        ripeness = 1.0f;
}

// create 0x140005920: MemAlloc(0x38, 8), then the ctor.
::Object::Base* FruitComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FruitComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) FruitComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* FruitComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) FruitComponent());
}

DEFINE_EXE_TYPE(FruitComponent, "Component")
