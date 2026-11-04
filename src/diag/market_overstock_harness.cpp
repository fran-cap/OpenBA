// src/diag/market_overstock_harness.cpp
// TEST RIG ONLY — no retail function is ported here.
//
// Purpose: exercise the ONE arm of MarketWork slot 4 (0x1400eff80) that has never
// executed — stage 3's SURPLUS SHED (retail 0x1400f09eb..0x1400f0be0, our
// marketwork.cpp "gate=SURPLUS_SHED"). That arm fires only when
//
//     want * 2.0  <  inMarket * (1/capacity)              [0x140172174 = 2.0]
//
// i.e. the market holds MORE THAN TWICE its target share of one resource, and it
// then hands the excess to the hauler to carry AWAY to some other storage.
// Haulers stock the market TO its target and stop, so in a normal game the test
// can never be true — the only way to see the arm run is to put the goods there
// by force. That is all this file does.
//
// ⚠ Two ceilings have to be respected at once, or the arm still never runs:
//   1. Stage 1's STOCKED gate (0x1400f01dd): if
//      QueryCount(marketStorage)/capacity >= MarketDescription+0x20 the function
//      returns 1 long BEFORE stage 3. So the injected volume must keep the
//      market's TOTAL fill under that threshold while pushing ONE resource over
//      2x its own share. Overstocking too hard is just as invisible as not
//      overstocking at all.
//   2. inMarket is VOLUME, not item count: loop A accumulates
//      RawMaterialDescription+0x48 (unit size) * available (0x1400f04f?), so the
//      item count to add is desiredVolume / unitSize.
// The harness picks the description entry with the SMALLEST `want` precisely
// because that is the cheapest one to push over 2x while staying under (1).
//
// Injection uses the same faithful primitive the retail resource seeder uses,
// AddItem 0x14012d000 (registered) — which, as town_storage_seed.cpp records,
// checks nothing about capacity. The market's own accept mask (+0x3c) IS
// honoured here so we never park a material in a slot the market would refuse.
//
// Env:
//   BANISHED_MARKET_OVERSTOCK        enable; optional value = target satisfaction
//                                    x100 (default 260 => aim for 2.6x, clear of
//                                    the 2.0 trigger)
//   BANISHED_MARKET_OVERSTOCK_FRAME  frame to inject on (default 2400 — after the
//                                    market is built, staffed and hauling)
//   BANISHED_MARKET_OVERSTOCK_PERIOD re-inject every N frames (default 0 = one
//                                    shot). The shed arm carries the surplus off,
//                                    so a period lets the arm fire repeatedly.
//
// Trace lines are [MarketOverstock]; pair with BANISHED_MARKETBST_TRACE=1 to see
// the resulting "gate=SURPLUS_SHED" from marketwork.cpp.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include "Object/factory_registration.h"
#include "Component/MarketComponent.h"
#include "Component/MarketDescription.h"
#include "Component/RawMaterialDescription.h"
#include "Component/StorageComponent.h"
#include "Action/exe_bodies.h"   // StorageAdd (0x14012d000)
#include "Component/ComponentDescription.h"   // B4: the template's sub-descriptions
#include "Entity/CitizenBehavior.h"           // B4: the town's material templates
#include "Entity/ComponentEntity.h"           // B4: the component array by name
#include "Entity/EntityList.h"                // B4: GameUpdateLoop::m_entityList
#include "object/TypeInfo.h"     // Object::IsDerivedFromNullSafe
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ============================================================
// Env
// ============================================================
static int OverstockPct() {          // -1 = disabled
    static int v = -2;
    if (v == -2) {
        const char* e = Opt().marketOverstock;
        if (!e || !*e || e[0] == '0') { v = -1; }
        else { int n = HarnessAtoi(e); v = (n > 100) ? n : 260; }
        // At most 1000x the want: the need computation casts targetSat * want *
        // capacity to int, out of range (undefined) for a huge percent (review F-284).
        if (v > 100000) v = 100000;
    }
    return v;
}
static int OverstockFrame() {
    static int v = -1;
    if (v < 0) { const char* e = Opt().marketOverstockFrame;
                 v = (e && *e) ? HarnessAtoi(e) : 2400; if (v < 1) v = 1; }
    return v;
}
static int OverstockPeriod() {
    static int v = -1;
    if (v < 0) { const char* e = Opt().marketOverstockPeriod;
                 v = (e && *e) ? HarnessAtoi(e) : 0; if (v < 0) v = 0; }
    return v;
}

#define MO_LOG(...)                                            \
    do { fprintf(stderr, "[MarketOverstock] " __VA_ARGS__);     \
         fprintf(stderr, "\n"); fflush(stderr); } while (0)

// Type-ids: each class's own record (real classes, ENDGAME B.3.8) -- the same value
// the .rdata cells 0x1401c44f0 / 0x1401c4528 / 0x1401c7e78 / 0x1401c7cb8 are filled with.
// Every object scanned below is an entity's component or a template's description
// (Object::Base), so slot 2 is Type().
// Component scan over a LIVE entity (+0x30 IBP array / +0x38 count).
static Component* FindComponent(ComponentEntity* entity, uint32_t tid) {
    if (!entity || !tid) return nullptr;
    const int count = entity->_components._size;                   // +0x38
    for (int i = 0; i < count; ++i) {
        Component* comp = entity->_components[i].Get();          // +0x30
        if (!comp) continue;
        if (comp->Type() == tid)
            return Object::IsDerivedFromNullSafe(comp, tid) ? comp : nullptr;
    }
    return nullptr;
}

// Description scan over a TEMPLATE entity (+0x28 raw array / +0x30 count).
static Description* FindDescOnTemplate(const ComponentDescription* tmpl, uint32_t tid) {
    if (!tmpl || !tid) return nullptr;
    const int count = tmpl->_subDescCount;                       // +0x30
    Description** arr = tmpl->_subDescs;                         // +0x28
    for (int i = 0; i < count; ++i) {
        Description* d = arr[i];
        if (d && d->Type() == tid)
            return Object::IsDerivedFromNullSafe(d, tid) ? d : nullptr;
    }
    return nullptr;
}

// StorageComponent::QueryCount (0x14012cf10) — volume-weighted total.
static int QueryCount(StorageComponent* sc, uint32_t tidRawMat) {
    int total = 0;
    const int slots = sc->storageSlots._size;                             // +0x30
    const StorageComponent::Item* arr = sc->storageSlots._data;        // +0x28 (stride 0x10)
    for (int i = 0; i < slots; ++i) {
        const StorageComponent::Item& slot = arr[i];
        // Found by IsDerivedFrom(RawMaterialDescription).
        RawMaterialDescription* d = static_cast<RawMaterialDescription*>(
            FindDescOnTemplate(slot._resource, tidRawMat));
        const int unitVol = d ? d->_weight : 0;                  // +0x48
        total += unitVol * slot._count;                          // +0x08
    }
    return total;
}

// The same accumulation stage 2's LOOP A performs for one record mask:
// sum of unitSize * (slot total - slot reserved) over the market's own slots.
static int InMarketVolumeForMask(StorageComponent* sc, uint32_t mask, uint32_t tidRawMat) {
    int total = 0;
    const int slots = sc->storageSlots._size;                             // +0x30
    const StorageComponent::Item* arr = sc->storageSlots._data;        // +0x28 (stride 0x10)
    for (int i = 0; i < slots; ++i) {
        const StorageComponent::Item& slot = arr[i];
        // Found by IsDerivedFrom(RawMaterialDescription).
        RawMaterialDescription* d = static_cast<RawMaterialDescription*>(
            FindDescOnTemplate(slot._resource, tidRawMat));
        if (!d) continue;
        if ((d->_flags & mask) == 0) continue;                   // +0x5c
        const int available = slot._count - slot._reserve;       // +0x08 - +0x0c
        total += d->_weight * available;                         // +0x48
    }
    return total;
}

static inline CitizenBehavior* TownMgr() {
    if (GameUpdateLoop::_singleton == nullptr) return nullptr;
    return GameUpdateLoop::_singleton->m_citizenList;            // +0xf0
}

// ============================================================
// The injection
// ============================================================
static void DoOverstock(int frameNum) {
    CitizenBehavior* town = TownMgr();
    if (!town) { MO_LOG("no town manager yet (frame %d)", frameNum); return; }

    const uint32_t tidMarketComp   = MarketComponent::TypeId();
    const uint32_t tidMarketDesc   = MarketDescription::TypeId();
    const uint32_t tidStorageComp  = StorageComponent::TypeId();
    const uint32_t tidRawMat       = RawMaterialDescription::TypeId();
    if (!tidMarketComp || !tidMarketDesc || !tidStorageComp || !tidRawMat) {
        MO_LOG("type-id resolve failed (market=%u desc=%u storage=%u rawmat=%u)",
               tidMarketComp, tidMarketDesc, tidStorageComp, tidRawMat);
        return;
    }

    // ---- find the first market in the WORLD entity list (gw+0xe8: array +0x08,
    // int32 count +0x10) ----------------------------------------------------
    // ⚠ NOT townMgr+0x00. That list is the STORAGE-LOCATION registry (see
    // town_storage_seed.cpp); a market is not in it, which is exactly why the
    // first cut of this harness reported "no MarketComponent in town" while the
    // MarketBST trace showed slot 4 servicing that very market on the same frame.
    ComponentEntity* marketEntity = nullptr;
    Component* marketComp = nullptr;
    {
        GameUpdateLoop* gw = GameUpdateLoop::_singleton;
        EntityList* el = gw->m_entityList;                                // +0xe8
        Object::InternalPtr<ComponentEntity>* arr = el ? el->masterArray._data : nullptr;      // +0x08
        const int entCount = el ? el->masterArray._size : 0;              // +0x10
        for (int e = 0; e < entCount && arr; ++e) {
            ComponentEntity* ent = arr[e].Get();
            if (!ent) continue;
            Component* mc = FindComponent(ent, tidMarketComp);
            if (mc) { marketEntity = ent; marketComp = mc; break; }
        }
    }
    if (!marketComp) { MO_LOG("no MarketComponent in the world entity list (frame %d)", frameNum); return; }

    Description* descRaw = marketComp->_description;                      // +0x18
    MarketDescription* marketDesc = (descRaw &&
                        Object::IsDerivedFromNullSafe(descRaw, tidMarketDesc))
                        ? static_cast<MarketDescription*>(descRaw) : nullptr;
    // FindComponent checked IsDerivedFrom(StorageComponent).
    StorageComponent* storageComp =
        static_cast<StorageComponent*>(FindComponent(marketEntity, tidStorageComp));
    if (!marketDesc || !storageComp) {
        MO_LOG("market %p incomplete (desc=%p storage=%p)",
               static_cast<void*>(marketEntity), static_cast<void*>(marketDesc),
               static_cast<void*>(storageComp));
        return;
    }
    const MarketDescription* dp = marketDesc;
    StorageComponent*        sc = storageComp;

    const int   capacity  = sc->capacity;                                 // +0x38
    const float threshold = dp->_fillRatio;   // stage 1's STOCKED gate      +0x20
    const uint32_t acceptMask = sc->rawMaterialFlags;                     // +0x3c
    const int   entryCount = dp->_ratioCount;                             // +0x30
    const MarketRatio* entries = dp->_ratios;                             // +0x28 (stride 8)
    if (capacity <= 0 || entryCount <= 0) {
        MO_LOG("market %p capacity=%d entries=%d — cannot overstock",
               static_cast<void*>(marketEntity), capacity, entryCount);
        return;
    }

    const int  currentTotal = QueryCount(sc, tidRawMat);
    const float targetSat   = static_cast<float>(OverstockPct()) / 100.0f;
    // Stage 1 declines at fill >= threshold, so this is the volume head-room the
    // injection must stay inside. One volume unit of slack keeps the compare strict.
    const int  headroom = static_cast<int>(threshold * static_cast<float>(capacity))
                        - currentTotal - 1;

    MO_LOG("frame %d market=%p storage=%p capacity=%d filled=%d fillThreshold=%.3f "
           "headroom=%d acceptMask=0x%08x entries=%d targetSat=%.2f",
           frameNum, static_cast<void*>(marketEntity), static_cast<void*>(storageComp),
           capacity, currentTotal, threshold, headroom, acceptMask, entryCount, targetSat);
    if (headroom <= 0) {
        MO_LOG("market already at/over stage 1's STOCKED gate — slot 4 returns 1 before "
               "stage 3 ever runs; nothing injected");
        return;
    }

    // ---- choose the entry: smallest non-zero want that we can actually push ----
    int   bestEntry = -1;
    float bestWant  = 0.0f;
    int   bestNeed  = 0;      // volume still to add
    for (int r = 0; r < entryCount; ++r) {
        const uint32_t mask = entries[r].resourceMask;                    // +0x00
        const float    want = entries[r].ratio;                           // +0x04
        if (!(want > 0.0f)) continue;
        if ((mask & acceptMask) == 0) continue;   // the market would refuse the goods
        const int have = InMarketVolumeForMask(sc, mask, tidRawMat);
        const int need = static_cast<int>(targetSat * want * static_cast<float>(capacity)) - have;
        if (need <= 0) continue;                  // already past target for this one
        if (need > headroom) continue;            // would trip stage 1's gate instead
        if (bestEntry < 0 || want < bestWant) { bestEntry = r; bestWant = want; bestNeed = need; }
    }
    if (bestEntry < 0) {
        MO_LOG("no description entry can be pushed over %.2fx want within the %d volume "
               "of headroom — raise BANISHED_MARKET_OVERSTOCK? nothing injected", targetSat, headroom);
        return;
    }
    const uint32_t mask = entries[bestEntry].resourceMask;

    // ---- pick a material template carrying that mask (town+0x110 / +0x118) ----
    ComponentDescription* tmpl = nullptr;
    int unitSize = 0;
    {
        const int tc = town->field_0x110._size;                                  // +0x118
        ComponentDescription** ta = town->field_0x110._data;                     // +0x110
        for (int t = 0; t < tc; ++t) {
            ComponentDescription* cand = ta[t];
            // Found by IsDerivedFrom(RawMaterialDescription).
            RawMaterialDescription* dd = static_cast<RawMaterialDescription*>(
                FindDescOnTemplate(cand, tidRawMat));
            if (!dd) continue;
            if ((dd->_flags & mask) == 0) continue;                        // +0x5c
            const int us = dd->_weight;                                    // +0x48
            if (us <= 0) continue;
            tmpl = cand; unitSize = us; break;
        }
    }
    if (!tmpl) {
        MO_LOG("entry %d mask=0x%08x want=%.4f: no raw-material template carries that mask; "
               "nothing injected", bestEntry, mask, bestWant);
        return;
    }

    const int units = (bestNeed + unitSize - 1) / unitSize;   // round UP past the trigger
    storageComp->AddItem(tmpl, units);   // 0x14012d000

    const int afterTotal = QueryCount(sc, tidRawMat);
    const int afterMask  = InMarketVolumeForMask(sc, mask, tidRawMat);
    const float sat  = static_cast<float>(afterMask) / (static_cast<float>(capacity) * bestWant);
    const float fill = static_cast<float>(afterTotal) / static_cast<float>(capacity);
    MO_LOG("INJECTED entry=%d mask=0x%08x want=%.4f unitSize=%d units=%d volume=%d "
           "-> inMarket=%d satisfaction=%.3f (trigger >2.0) | fill=%.3f (gate %.3f) %s",
           bestEntry, mask, bestWant, unitSize, units, units * unitSize,
           afterMask, sat, fill, threshold,
           (sat > 2.0f && fill < threshold) ? "ARMED" : "NOT ARMED — see the two numbers above");
}

// ============================================================
// Tick — called from GameUpdateLoop::Update
// ============================================================
extern "C" void MarketOverstock_Tick(int frameNum) {
    if (OverstockPct() < 0) return;
    const int first = OverstockFrame();
    if (frameNum < first) return;
    const int period = OverstockPeriod();
    if (frameNum == first) { DoOverstock(frameNum); return; }
    if (period > 0 && ((frameNum - first) % period) == 0) DoOverstock(frameNum);
}
