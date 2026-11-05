// src/diag/orchard_growth_harness.cpp
// BANISHED_ORCHARD_GROW=<n> — fast-forward orchard tree growth so the HARVEST half of the
// state machine can be exercised in a single run instead of a multi-hour soak.
//
// WHY THIS EXISTS: the orchard chain is now live end to end up to tending — 12 trees plant,
// bind to their tiles, and mature. But maturity climbs about 0.01 per ~200 diag ticks
// (measured 2026-08-10: 0.000 -> 0.0085 -> 0.0192 -> 0.0299), so a tree reaching 1.0 takes
// game-YEARS. Nothing downstream of maturity can be tested at that cadence: fruit ripening,
// TryHarvest's 0.99 threshold, the event-4 handler, state 11, HarvestOrchard.
//
// WHAT IT DOES *NOT* DO: it does not touch, rewrite or bypass any ported game code, and it
// does not fake a harvest. It advances exactly the two simulation VALUES the real code reads,
// in the real order, and then gets out of the way — every gate downstream still has to pass
// on its own:
//   1. tree maturity at NaturalResourceComponent(slot+0x10) + 0x28, up to 1.0. This is the
//      gate FruitComponent::Update tests as `growthRate >= 1.0f` before it will accumulate
//      ANY ripeness — which is why a young orchard correctly bears nothing.
//   2. only once a tree is mature, fruit ripeness at FruitComponent(slot+0x18) + 0x30, the
//      field OrchardComponent_GetGrowth averages and TryHarvest compares against 0.99.
// So a harvest driven by this harness is still a real harvest: TryHarvest still has to fire,
// the year check at +0x4c still has to pass, and state 11 still has to do its job.
//
// Env:
//   BANISHED_ORCHARD_GROW      unset / 0 / false  -> disabled (default; this is a test tool,
//                                                    never a default-on feature)
//                              1                  -> default step, 0.05 per tick
//                              <N> for N > 1      -> step = N/100 per tick (e.g. 25 -> 0.25)
//
// Cadence: every kTickFrames frames from kStartFrame. Slots are only advanced while their
// slot state (+0x04) is 1 (planted and bound) — a hole or an unplanted slot is left alone,
// so the harness cannot paper over a planting failure.
//
// Offsets, all already used by orchard_diag.cpp / orchardcomponent.cpp:
//   gameWorld+0xe8 EntityList {+0x08 data, +0x10 count}; entity+0x30/+0x38 component array
//   OrchardComponent +0x50 slot array, +0x58 slot count; slot stride 0x28
//   slot +0x04 state, +0x10 NaturalResourceComponent, +0x18 FruitComponent
//   natRes +0x28 maturity; fruit +0x30 ripeness

#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Component/OrchardComponent.h"
#include "Component/NaturalResourceComponent.h"   // B4: OrchardSlot::resource
#include "Component/FruitComponent.h"             // B4: OrchardSlot::fruit
#include "Entity/ComponentEntity.h"               // B4: the component array by name
#include "Entity/EntityList.h"                    // B4: GameUpdateLoop::m_entityList

namespace {

constexpr int   kStartFrame = 120;
constexpr int   kTickFrames = 60;
constexpr float kDefaultStep = 0.05f;
constexpr float kMature      = 1.0f;

// -1 = unchecked, 0 = off, 1 = on
int   s_state = -1;
float s_step  = kDefaultStep;

bool Enabled() {
    if (s_state >= 0) return s_state == 1;
    const char* e = Opt().orchardGrow;
    if (!e || !e[0] || std::strcmp(e, "0") == 0 || std::strcmp(e, "false") == 0) {
        s_state = 0;
        return false;
    }
    const double v = ClampedAtof(e);
    if (v > 1.0) s_step = static_cast<float>(v / 100.0);
    s_state = 1;
    std::fprintf(stderr, "[OrchardGrow] ENABLED (BANISHED_ORCHARD_GROW=\"%s\") — advancing tree "
                 "maturity then fruit ripeness by %.3f every %d frames. This is a TEST HARNESS: "
                 "it moves two simulation values, it does not skip any gate downstream of them "
                 "(FruitComponent's maturity gate, TryHarvest's 0.99 threshold and its "
                 "once-per-year check all still apply).\n", e, s_step, kTickFrames);
    std::fflush(stderr);
    return true;
}

}  // namespace

extern "C" void OrchardGrowthHarness_Tick(int frameNum) {
    if (!Enabled()) return;
    if (frameNum < kStartFrame || (frameNum % kTickFrames) != 0) return;
    if (GameUpdateLoop::_singleton == nullptr) return;

    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    EntityList* entityList = gw->m_entityList;                   // +0xe8
    if (!entityList) return;
    Object::InternalPtr<ComponentEntity>* arr = entityList->masterArray._data;        // +0x08
    int       n   = entityList->masterArray._size;               // +0x10
    const uint32_t tidOrchardComp = OrchardComponent::TypeId();
    if (!arr || n <= 0 || !tidOrchardComp) return;

    int orchards = 0, grown = 0, ripened = 0, mature = 0, slotsSeen = 0;
    float matMax = 0.0f, ripeMax = 0.0f;

    for (int i = 0; i < n; ++i) {
        ComponentEntity* ent = arr[i].Get();
        if (!ent) continue;

        int       cN = ent->_components._size;                     // +0x38
        Object::InternalPtr<Component>* cA = ent->_components._data;   // +0x30
        OrchardComponent* orchComp = nullptr;
        for (int c = 0; c < cN && cA; ++c) {
            Component* comp = cA[c].Get();
            if (!comp) continue;
            if (comp->Type() == tidOrchardComp) {
                orchComp = static_cast<OrchardComponent*>(comp);   // exact type id
                break;
            }
        }
        if (!orchComp) continue;
        ++orchards;

        int       count = orchComp->slotCount;                   // +0x58
        OrchardSlot* slots = orchComp->slots;                    // +0x50
        if (!slots || count <= 0) continue;

        for (int s = 0; s < count; ++s) {
            OrchardSlot& slot = slots[s];                        // stride 0x28
            if (slot.state != 1) continue;  // only planted+bound   (+0x04)
            ++slotsSeen;

            NaturalResourceComponent* natRes = slot.resource.Get();   // +0x10
            FruitComponent* fruit  = slot.fruit.Get();                 // +0x18
            if (!natRes) continue;

            float* pMat = &natRes->resourceScale;                // +0x28
            if (*pMat < kMature) {
                *pMat += s_step;
                if (*pMat > kMature) *pMat = kMature;
                ++grown;
            } else {
                ++mature;
                // Mature: let ripeness move. FruitComponent::Update also advances this field
                // on its own now that its gate passes; we simply add to it, so the two
                // sources compose rather than fight.
                if (fruit) {
                    float* pRipe = &fruit->ripeness;             // +0x30
                    if (*pRipe < kMature) {
                        *pRipe += s_step;
                        if (*pRipe > kMature) *pRipe = kMature;
                        ++ripened;
                    }
                    if (*pRipe > ripeMax) ripeMax = *pRipe;
                }
            }
            if (*pMat > matMax) matMax = *pMat;
        }
    }

    // Report OUTCOMES, not intent: "the harness ran" is worthless, "it moved N maturities and
    // M ripenesses and the maxima are here" is what tells you whether harvest should be close.
    static int s_reports = 0;
    if (slotsSeen && (++s_reports <= 6 || (s_reports % 20) == 0)) {
        std::fprintf(stderr, "[OrchardGrow] frame=%d orchards=%d plantedSlots=%d | advanced "
                     "maturity=%d (max=%.3f) mature=%d | advanced ripeness=%d (max=%.3f)\n",
                     frameNum, orchards, slotsSeen, grown, matMax, mature, ripened, ripeMax);
        std::fflush(stderr);
    }
}
