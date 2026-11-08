// src/game/Game/town_storage_seed.cpp
// Economy Brick M-D (WORLD/STORAGE half) — town storage-location registry +
// retail starting-resource seeding, so the start-town StorageBarn/StorageYard
// become REAL stocked storage the citizen eat chain can find.
//
// Faithful ports (Ghidra, Application-x64.exe):
//   * 0x14012d230 StorageLocationDescription_helper_d230
//       -> TownRegistry_RegisterStorageLocation(comp)
//       Appends *(comp+0x10) (the owning ENTITY pointer) to the town
//       storage-location list at townMgr+0x00 (count +0x08, cap +0x0c) via the
//       grow fn FUN_140003110 (registered). townMgr = *(gameWorld+0xf0).
//       NOT an OnAdded vtable slot (an earlier note here claimed that; corrected
//       2026-07-27). 0x14012d230 appears in NO vtable — its only three xrefs are
//       the .pdata entry plus StorageLocationDescription_SetupCrossRefs
//       (0x14014fc70) and its inlined twin impl_d410, both of which register it as
//       the ENTER handler of StorageLocationComponent state 2. So retail registers
//       a storage location exactly when that component's state machine ENTERS
//       state 2, and nowhere else. It is now wired as exactly that (state-2 enter in
//       storagelocationcomponent.cpp, with helper_d1f0 as the matching state-3 erase),
//       and the StartTown_WireStorage stand-in stands down when the controller can
//       beat the resource seeder to it (BANISHED_LIVE_STORAGECTRL + LIVE_SM_ONADDED).
//   * 0x140107ff0 BuildDescription_helper_7ff0
//       -> TownStorage_BuildCompleteInit_7ff0(componentWithEntityAt0x10)
//       The BUILD-COMPLETE storage init: finds the entity's StorageComponent,
//       credits any construction-slot contents to the town stock table
//       (townMgr+0x170, index via FUN_140088f90), then re-initializes from the
//       StorageDescription: slots cleared (FUN_14000f090), +0x3c = _storageFlags
//       (desc+0x28), +0x38 = _volumeLimit (desc+0x24) * (desc+0x20 _areaBasedLimit
//       ? mapW*mapH : 1). NOTE: it does NOT test desc+0x21 (_available) — that
//       byte only gates the OnAdded slot-rebuild (StorageComponent_Draw
//       0x14012c640), i.e. storage while still a construction site. This is how
//       a completed barn (_available=false, _volumeLimit=6000) gets capacity.
//   * 0x140091660 Game_SpawnResources(townMgr, materialFlagMask, count)
//       Phase A: filter the raw-material TEMPLATE entity list at townMgr+0x110
//       (count +0x118) by RawMaterialDescription(+0x5c) & mask (descriptions on a
//       template live at +0x28 array / +0x30 count, raw pointers).
//       Phase B: round-robin the matched templates over the storage-location
//       entities at townMgr+0x00, one unit at a time: accept-check
//       StorageComponent_impl_cd80 then AddItem FUN_14012d000 (registered),
//       until `count` units placed or no storage accepts.
//   * 0x1400a1f00 Game_impl_1f00 -> StartTown_SeedStartingResources(startConfig)
//       Seven Game_SpawnResources calls with masks 0x400/0x800/0x1000/0x200/
//       0x20/0x40/0x4000 and counts from startConfig+0xfc..+0x114. Verified at
//       the FUN_1400a3300 call site 0x1400a342b: RCX = RDI = a3300's param_1 =
//       the SAME startConfig object StartTown_SpawnEntities receives.
//
// Row 98 (B7 RES): the private copies of 0x140006270 (FindComponent<StorageComponent>),
// 0x14012cf10 (StorageComponent::QueryCount), 0x14012c560 and 0x14012cd80 that stood here
// (their reason -- type ids read from raw .rdata cells resolving null -- went with the
// cells) are gone: the bodies call ComponentEntity::FindComponent<T>() and StorageCanAdd
// (0x14012cd80) by name. Every "standalone-inert" skip / bail went with them: retail reads
// the town manager, the found components, the narrowed descriptions and the template's
// RawMaterialDescription with no test (0x14012d234-0x14012d250, 0x140107ff0's finds and
// +0x20 / +0x24 / +0x28 reads, 0x140091660's +0x5c read and its cd80 call, 0x1400a1f0a).
//
// Trace: BANISHED_STORAGE_TRACE=1 prints [StorageSeed] lines.

#include <new>
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"      // ActHunt_DynArraySetCount_3110, ActRm_StorageAdd_d000, ActRm_GetResourceCategory_88f90
#include "Component/exe_bodies.h"   // StorageComponent_DynArrayResize
#include "object/TypeInfo.h"                     // caller batch CB6: Object::Base::Type()
#include "Game/GameUpdateLoop.h"                // caller batch CB6: the world's named fields
#include "Component/RawMaterialDescription.h"   // caller batch CB6: TypeId()
#include "Component/StorageComponent.h"
#include "Component/StorageDescription.h"
#include "Component/MapComponent.h"
#include "collection/Collection.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Entity/CitizenBehavior.h"        // the town manager (GameUpdateLoop +0xF0)
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Game/MapGenerator.h"             // the start config (GameUpdateLoop::m_mapConfig)
#include "Game/exe_bodies.h"               // the declarations of this file's bodies

// ============================================================
// Type-ids: 0x1401c7cb8/7e78/7eb0/79e0 are these four classes' cells (Table 1).
// ============================================================
static uint32_t TidRawMaterialDescription() { return RawMaterialDescription::TypeId(); }
static uint32_t TidStorageDescription()     { return StorageDescription::TypeId(); }

// ============================================================
// Exe helpers, called by name (all registered in standalone):
//   0x140003110 ActHunt_DynArraySetCount_3110     (Action/exe_bodies.h)
//   0x14012d000 ActRm_StorageAdd_d000             (Action/exe_bodies.h)
//   0x14000f090 StorageComponent_DynArrayResize   (Component/exe_bodies.h)
//   0x140088f90 ActRm_GetResourceCategory_88f90   (Action/exe_bodies.h)
// ============================================================

static inline CitizenBehavior* TownMgr() {
    return GameUpdateLoop::_singleton->m_citizenList;   // [[0x1401c3dd8]+0xf0], no test
}

// Slot 2. Every caller passes an element of an entity's component array or a
// template's description array (Object::Base-derived exe objects).
static inline uint32_t GetTypeIdOf(const Object::Base* obj) {
    return obj->Type();
}

// ============================================================
// Private 1:1 helper transcriptions
// ============================================================

// FUN_140002ff0-family description scan: find the RawMaterialDescription on a
// TEMPLATE (a ComponentDescription: sub-descriptions at +0x28, count +0x30) — the
// exact inline loop of Game_SpawnResources / cd80 / c560 / QueryCount.
static RawMaterialDescription* FindRawMatDescOnTemplate(const ComponentDescription* tmpl) {
    const uint32_t tid = TidRawMaterialDescription();
    int count = tmpl->_subDescCount;
    Description** arr = tmpl->_subDescs;
    for (int i = 0; i < count; i++) {
        Description* desc = arr[i];
        if (GetTypeIdOf(desc) == tid) {
            // Original: IsDerivedFrom-narrow, break either way.
            return desc->IsDerivedFrom(tid) ? static_cast<RawMaterialDescription*>(desc) : nullptr;
        }
    }
    return nullptr;
}

// ============================================================
// 0x14012d230 — TownRegistry_RegisterStorageLocation
// ============================================================
// 1:1: appends the component's owning ENTITY pointer (*(comp+0x10)) to the
// town storage-location list (townMgr+0x00 base / +0x08 count / +0x0c cap)
// via FUN_140003110. Retail does NOT dedup and does NOT CheckPtr here.
bool Component::RegisterStorageLocation() const {
    CitizenBehavior* townMgr = TownMgr();
    ComponentEntity* entityRef = _entity.Get();   // the component's +0x10 entity
    int newCount = townMgr->storageLocations._size + 1;
    townMgr->storageLocations.SetSizeFill(newCount, &entityRef);   // 0x140003110
    // Retail does not dedup here, so EVERY caller is an append. On a LOADED world the
    // tutorial's single starting stockpile came back registered TWICE (count=2, same
    // entity at [0] and [1]), which put goalstoragebarnfinished permanently out of
    // reach — that goal has no `_greaterEqual`, so it needs EXACTLY 3 and saw 4.
    return false;   // 0x14012d25a `XOR AL,AL`: the enter walk goes on (row 1065)
}

// ============================================================
// 0x140107ff0 — TownStorage_BuildCompleteInit_7ff0
// ============================================================
// param = any component whose +0x10 holds the building's entity IBP (retail
// passes the BuildComponent; the StorageComponent itself works — same layout).
void Component::BuildCompleteStorageInit() {
    const Component* p = this;

    ComponentEntity* entity = p->_entity.Get();
    StorageComponent* sc = entity->FindComponent<StorageComponent>();   // 0x140006270
    CitizenBehavior* townMgr = TownMgr();

    // Credit existing (construction-delivery) slot contents to the town stock
    // table at townMgr+0x170 (the currentCount... +0xc field per 0x20-stride row,
    // index FUN_140088f90). No-op for a freshly spawned building (0 slots).
    int slots = sc->storageSlots._size;
    if (slots > 0) {
        StorageComponent::Item* arr = sc->storageSlots._data;
        for (int i = 0; i < slots; i++) {
            StorageComponent::Item* slot = &arr[i];
            int cnt = slot->_count;
            int idx = ResourceCategory(slot->_resource);   // 0x140088f90
            if (idx >= 0) {
                int* cell = &townMgr->professionResources._data[idx].consumedCount;
                *cell += cnt;
            }
        }
    }

    // StorageDescription (comp+0x18, IsDerivedFrom-narrowed).
    // IsDerivedFrom on +0x18 (the member, called on the pointer as is), CMOVNZ over 0;
    // +0x20 / +0x24 / +0x28 are read through the result with no test.
    Description* descObj = sc->_description;
    StorageDescription* desc = nullptr;
    if (descObj->IsDerivedFrom(TidStorageDescription()))
        desc = static_cast<StorageDescription*>(descObj);

    // _areaBasedLimit (+0x20): multiply _volumeLimit by the MapComponent footprint.
    int tileArea = 1;
    if (desc->_flagA) {
        ComponentEntity* ent2 = sc->_entity.Get();
        // The inline FindComponent<MapComponent> (cell 0x1401c79e0), wrapped in an IBP and
        // read (+0x2c / +0x2e) with no test.
        ::Object::InternalPtr<MapComponent> mapIBP(ent2->FindComponent<MapComponent>());
        tileArea = static_cast<int>(mapIBP->tileY1) *
                   static_cast<int>(mapIBP->tileX1);
    }

    int volumeLimit    = desc->_pickupValue;
    uint32_t storageFlags = desc->_rawMaterialFlags;
    sc->storageSlots.SetSize(0);   // 0x14000f090
    sc->rawMaterialFlags = storageFlags;
    sc->capacity = volumeLimit * tileArea;

}

// ============================================================
// 0x140091660 — Game_SpawnResources(townMgr, mask, count)
// ============================================================
extern "C" void TownStorage_SpawnResources_91660(CitizenBehavior* townMgr, uint32_t mask, int count) {

    // ---- Phase A: filter the raw-material template list (+0x110/+0x118) ----
    // ENDGAME B.3.2 step 2: retail's local_48/local_40 {data,count,cap} triple is
    // a real `Collection::Array<int64_t>` with RAII (trait-derived, trait-
    // equivalent: 8-byte copy, no destroy, the 2-argument init never reached by
    // a fill call). Exactly one destruction per path: the function has a single
    // exit after phase B, where the manual free used to be. The grow-append
    // below was compared arm for arm against rtcoll::SetSizeCore: the capacity
    // rule `matchedCount != 0 ? newCount*3 : newCount` IS the reference's rule
    // because newCount = matchedCount + 1 always; the exact-equal early return
    // is vacuous for the same reason; prefix copied only when the block moved,
    // fill at the old count, old block freed only when it moved. Only the
    // `if (!matched) break` out-of-memory bail is gone.
    Collection::Array<ComponentDescription*> matched;

    int tmplCount = townMgr->knownRawMaterials._size;
    ComponentDescription** tmplArr = townMgr->knownRawMaterials._data;
    for (int t = 0; t < tmplCount; t++) {
        ComponentDescription* tmpl = tmplArr[t];
        RawMaterialDescription* desc = FindRawMatDescOnTemplate(tmpl);
        // 0x14009171a: [desc+0x5c] read with no test.
        if ((desc->_flags & mask) == 0)
            continue;
        // Faithful grow-append (8-byte entries; cap = n+1, tripled when growing
        // a non-empty array; copy survivors; free the old buffer).
        matched.SetSize(matched.GetSize() + 1, tmpl);
    }
    const int matchedCount = matched.GetSize();

    // ---- Phase B: round-robin the matched templates over townMgr+0x00 ----
    int placed = 0;
    if (matchedCount > 0 && count > 0) {
        for (;;) {
            bool progress = false;
            int locCount = townMgr->storageLocations._size;   // +0x08 (retail: the low dword of the qword)
            if (locCount < 1) break;
            for (int i = 0; i < locCount; i++) {
                ComponentEntity* entityRef = townMgr->storageLocations._data[i].Get();
                // The inline FindComponent<StorageComponent> (cell 0x1401c7e78), wrapped in
                // an IBP; cd80 is called on it with no test (0x1400918cf onward).
                ::Object::InternalPtr<StorageComponent> compIBP(entityRef->FindComponent<StorageComponent>());
                if (placed < count) {
                    ComponentDescription* tmpl = matched[placed % matchedCount];
                    if (compIBP.Get()->CanAdd(tmpl, 1)) {   // 0x14012cd80
                        compIBP.Get()->AddItem(tmpl, 1);   // 0x14012d000
                        placed++;
                        progress = true;
                    }
                }
            }
            if (!progress || placed >= count) break;
        }
    }
    // (matched: freed by its destructor here, where the manual free was)
}

// ============================================================
// 0x1400a1f00 — StartTown_SeedStartingResources(startConfig)
// ============================================================
void MapGenerator::SeedStartingResources() {
    CitizenBehavior* townMgr = TownMgr();
    // The seven counts are MapGenerator +0xFC..+0x114.
    const MapGenerator* sc = this;
    // Mask labels corrected 2026-07-19 (binary-verified flag values, see
    // engine/types/RawMaterialDescription.h): 0x20 = EDIBLE food, 0x40 = FUEL
    // firewood. The 0x400/0x800/0x1000/0x200/0x4000 class names below are the
    // best current guesses for the remaining start-resource classes
    // (logs/stone/iron/tools/clothing) — amounts and masks are exe-faithful
    // regardless of naming.
    TownStorage_SpawnResources_91660(townMgr, 0x400,  sc->initialWoodCount);
    TownStorage_SpawnResources_91660(townMgr, 0x800,  sc->initialStoneCount);
    TownStorage_SpawnResources_91660(townMgr, 0x1000, sc->initialIronCount);
    TownStorage_SpawnResources_91660(townMgr, 0x200,  sc->initialToolCount);
    TownStorage_SpawnResources_91660(townMgr, 0x20,   sc->initialFoodCount);
    TownStorage_SpawnResources_91660(townMgr, 0x40,   sc->initialFuelCount);
    TownStorage_SpawnResources_91660(townMgr, 0x4000, sc->initialClothingCount);
}
