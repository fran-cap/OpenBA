// src/diag/disaster_harness.cpp
// TEST RIG ONLY — no retail function is ported here.
//
// Fires each of the four disasters on demand, so the fire / citizen-disease /
// crop-infestation / tornado lanes can be exercised in a `make sa-run` session.
//
// WHY THE ORGANIC ARM CANNOT BE TESTED WITHOUT THIS
// -------------------------------------------------
// Every disaster rolls its own dice inside its own per-tick function, each gated
// on its own bit of gameWorld+0xD0 and each using the same probability primitive
// FUN_1400a9290:
//
//   fire         FireComponent::Update            0x140048fb0   bit 6 (noFire)
//   crop disease DiseaseComponent::InfectionTick  0x140048610   bit 5 (noCropDisease)
//   tornado      Weather::ResetTime spawn block   0x1400947e3   bit 7 (noTornado)
//   citizen sick (bit 4, noSick)
//
// There is NO central disaster scheduler. Game_TriggerDisasterByType (0x140060110)
// looks like one and is not: it walks the external-factory list and calls
// Game_RemoveEntity on matches. It REMOVES entities. Do not use it (ids 321-323).
//
// The fire chances are authored in ToolKit/resource/Template/Common.rsc as
// FireDescription._fireChance = RandomSelection { float _years; float _population; }
// — the float pair at FireDescription+0x44 that FireComponent::Update hands to
// SpawnChance:
//
//   The five fire-chance rows (fireWoodHouse .. fireStoneBuilding) and the building lists that use them are in
//   Template/Common.rsc of the install; the values are not reproduced here. Some rows are zero (see below).
//
// Two consequences drive this file's design:
//   1. The organic arm is denominated in YEARS. It is unreachable in a session-length
//      run. Forcing the trigger is the only way to test the downstream behaviour.
//   2. HALF THE BUILDINGS CAN NEVER IGNITE. A _years=0 template has no chance to roll,
//      so igniting one is a wasted test — and worse, a false negative, because it looks
//      like the trigger worked and nothing happened. Hence the TARGETED fire pick below
//      rather than the debug button's random-building roll.
//
// SHAPE
// -----
// Follows src/diag/death_harness.cpp's discipline on src/diag/market_overstock_harness.cpp's
// skeleton: env read once into file-scope statics, one-shot latch (or _PERIOD poll),
// targets snapshotted before any mutation, and every arm REPORTS ITS OUTCOME — including
// the negative ones. A silent log means the env var was never set.
//
// Each trigger goes through the SAME resolved pointer the debug UI uses, so when the
// port lanes land their RegisterExeAddr entries this file starts working with no change
// here. Where a leaf is still null we say so as a distinct outcome rather than failing
// silently — "Ignite unresolved — port not landed" is a different fact from "no house
// qualified", and the two must never look alike in the log.
//
// ENV
// ---
//   BANISHED_FORCE_FIRE=<n>        ignite n house(s), nearest the house-cluster centroid
//   BANISHED_FORCE_TORNADO=<n>     spawn n tornado(es)
//   BANISHED_FORCE_SICK=<n>        infect n citizen(s)
//   BANISHED_FORCE_INFEST=<n>      infest n crop field(s)
//   <EACH>_FRAME=<f>               frame to fire on (default 2400 — after the town is
//                                  built, staffed and hauling)
//   <EACH>_PERIOD=<f>              re-fire every f frames (default 0 = one shot)
//   BANISHED_FORCE_SICK_DISEASE=<i> which DiseaseDescription to use (default 0 = first)
//
// All opt-in. NOT in the default-on table. Unset ⇒ byte-unchanged run.
//
// EACH ARM ALSO DEPENDS ON ITS LANE'S LIVE GATE — but NOT all in the same way, and the
// difference decides which failures a gate can and cannot explain.
//
// A COMPONENT_LIVE_* gate does NOT control whether the component EXISTS. The factory
// calls MakeInertTyped unconditionally and the gate only reaches FinishLiveVtable
// (firecomponent.cpp:209-215 is the worked example; component_factory_base.h:113 is the
// generic macro). So a gate can explain "the component did not REACT"; it can never
// explain "no entity carries the component". Two of the four arms nonetheless cannot
// find a target with their gate off — because what they read is filled by something the
// gate DOES control:
//
//   Fire     BANISHED_LIVE_FIRE         DOWNSTREAM ONLY. FireComponents exist either way,
//                                       so this arm always finds its targets and always
//                                       ignites. Without the gate the ignition has no
//                                       live slot 9 to burn, spread or extinguish with.
//                                       It can NOT cause "no structure carries a
//                                       FireComponent" — that means no such template.
//   Tornado  BANISHED_LIVE_TORNADO      DOWNSTREAM ONLY. The funnel spawns either way;
//                                       without the gate it never moves.
//   Sick     BANISHED_LIVE_DISEASELIST  REQUIRED FOR A TARGET. It gates the Diseases.rsc
//                                       LOAD into gameWorld+0xA8 (GameUpdateLoop::Load
//                                       step 1) — the list this arm picks its
//                                       DiseaseDescription from. Off ⇒ no target at all.
//   Infest   BANISHED_LIVE_CROPDISEASE  REQUIRED FOR A TARGET. It gates DiseaseComponent's
//                                       slot 6 OnAdded, the only thing that fills the
//                                       townMgr+0xF0 registry this arm walks.
//                                       Off ⇒ empty registry ⇒ no target at all.
//
// Every "nothing happened" message below names the gate it ACTUALLY depends on — and the
// Fire one says outright that setting BANISHED_LIVE_FIRE will not change it. That is the
// whole point: "the gate is off" and "the feature is broken" produce the same silence,
// and a message naming a gate that cannot produce the symptom is WORSE than none — it
// sends the reader to set a flag, re-run, and see the identical line.
//
// Grep tags: [Disaster] [Fire] [Tornado] [Sick] [Infest]

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include "Object/factory_registration.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "Dialog/ui_panels.h"          // DebugUI, DebugUI_Slot9
#include "Component/diseasecomponent_funcs.h"  // DiseaseComp_CheckCure (0x1400481b0), DiseaseComp_Infect
#include "Component/firecomponent_funcs.h"     // FireComponent_Ignite_610 (0x140049610)
#include "Component/exe_bodies.h"              // CitizenComp_Infect (0x140116b90)
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "object/TypeInfo.h"                   // Object::IsDerivedFromNullSafe
#include "Game/Disease.h"
#include "Entity/CitizenBehavior.h"         // B4: the town registries
#include "Entity/ComponentEntity.h"         // B4: the component array by name
#include "Entity/EntityList.h"              // B4: GameUpdateLoop::m_entityList
#include "Component/MapComponent.h"         // B4: the tile rect
#include "Component/FireComponent.h"        // B4: fireTimer
#include "Component/DiseaseComponent.h"     // B4: duration
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Component/CitizenComponent.h"   // B4 S4 RT2: InternalPtr<CitizenComponent>::Get() needs the class complete

namespace {

// ============================================================
// Env — read once, cached in file-scope statics
// ============================================================

struct Arm {
    const char* name;       // grep tag / log name
    int  armIndex;          // FIRE=0, TORNADO=1, SICK=2, INFEST=3 — indexes Opt().disaster*[]
    int  count;             // 0 = disabled
    int  frame;
    int  period;
    bool fired;             // one-shot latch (period == 0)
    int  lastFireFrame;
};

Arm s_arms[] = {
    { "Fire",    0, 0, 2400, 0, false, -1 },
    { "Tornado", 1, 0, 2400, 0, false, -1 },
    { "Sick",    2, 0, 2400, 0, false, -1 },
    { "Infest",  3, 0, 2400, 0, false, -1 },
};
enum { ARM_FIRE = 0, ARM_TORNADO = 1, ARM_SICK = 2, ARM_INFEST = 3, ARM_COUNT = 4 };

int  s_sickDiseaseIdx = 0;
bool s_initialized    = false;

#define DLOG(tag, ...)                                             \
    do { std::fprintf(stderr, "[" tag "] " __VA_ARGS__);           \
         std::fflush(stderr); } while (0)

int ParseInt(const char* v, int dflt) {
    if (!v || !v[0]) return dflt;
    return HarnessAtoi(v);
}

void Init() {
    if (s_initialized) return;
    s_initialized = true;

    bool any = false;
    for (int i = 0; i < ARM_COUNT; ++i) {
        Arm& a = s_arms[i];
        const char* e = Opt().disasterCount[a.armIndex];
        a.count = (e && e[0] && e[0] != '0') ? HarnessAtoi(e) : 0;
        if (a.count <= 0) { a.count = 0; continue; }
        a.frame  = ParseInt(Opt().disasterFrame[a.armIndex],  2400);
        if (a.frame  < 1) a.frame  = 1;
        a.period = ParseInt(Opt().disasterPeriod[a.armIndex], 0);
        if (a.period < 0) a.period = 0;
        any = true;
        DLOG("Disaster", "%s ENABLED — %d target(s) at frame %d%s\n",
             a.name, a.count, a.frame,
             a.period ? " (repeating)" : " (one shot)");
        if (a.period)
            DLOG("Disaster", "%s repeat period = %d frames\n", a.name, a.period);
    }
    s_sickDiseaseIdx = ParseInt(Opt().forceSickDisease, 0);
    if (s_sickDiseaseIdx < 0) s_sickDiseaseIdx = 0;

    if (any)
        DLOG("Disaster", "disaster bits at gameWorld+0xD0 are 0 on the new-game path, "
             "so all four disasters are ENABLED; these arms force the trigger, they do "
             "not bypass any gate\n");
}

// ============================================================
// World access
// ============================================================

// The town manager at gameWorld+0xF0 carries the registries the debug triggers walk:
//   +0x20 / +0x28   structure entities (Game_StartRandomFire, 0x14005feb0)
//   +0xA0 / +0xA8   citizens          (also death_harness.cpp TownList)
//   +0xF0 / +0xF8   DiseaseComponents (Game_InfectRandomCitizen, 0x14005fe10 —
//                   MISNAMED in Ghidra: this registry is the CROP/livestock one)
GameUpdateLoop* GameWorld() {
    if (GameUpdateLoop::_singleton == nullptr) return nullptr;
    return GameUpdateLoop::_singleton;
}
CitizenBehavior* TownMgr() {
    GameUpdateLoop* gw = GameWorld();
    if (!gw) return nullptr;
    return gw->m_citizenList;                                   // +0xf0
}

// Read one registry: the town's typed array and its count (the three registries
// below: +0x20/+0x28, +0xA0/+0xA8, +0xF0/+0xF8).
template <class T>
bool Registry(CitizenIbpList<T> CitizenBehavior::*list,
              Object::InternalPtr<T>** outArr, int* outCount) {
    CitizenBehavior* town = TownMgr();
    if (!town) return false;
    *outArr   = (town->*list)._data;
    *outCount = (town->*list)._size;
    return *outArr != nullptr && *outCount > 0;
}

// Find a component on an entity by REGISTERED NAME rather than by one of the
// fn_FindComponent_* inlines. Deliberate: several of those are misnamed aliases
// (engine_funcs.h:819 `fn_FindComponent_Residence` is literally Storage's type id),
// and a wrong finder here would silently pick the wrong buildings. Walking the
// entity's own component array and asking each component's vtable +0x10 GetTypeId
// is the same thing tool_harness.cpp:2000 does, and it cannot be misnamed.
//   entity+0x30 = component IBP array, entity+0x38 = count (retail FUN_140048840).
// A null return has TWO causes — the entity does not carry that component, and the type
// name never resolved — and they demand opposite responses. An unresolved name means the
// type was never registered (or Object::Factory::Startup has not run yet), which makes
// EVERY lookup of that name fail and turns each caller's census into a silent zero. Say
// so once per name, so "no structure carries a FireComponent" is never reported when the
// truth is "nothing could have been found".
bool WarnedTypeId(const char* name) {
    static const char* seen[8] = {};
    for (int i = 0; i < 8; ++i) {
        if (seen[i] == name) return true;          // literals: pointer identity is enough
        if (!seen[i]) { seen[i] = name; return false; }
    }
    return true;                                   // table full — stop warning
}

Component* ComponentByName(ComponentEntity* entity, const char* typeName) {
    if (!entity || !typeName) return nullptr;
    uint32_t want = ::Object::GetTypeIdByName(typeName);
    if (!want || want == ::Object::kInvalidTypeId) {
        if (!WarnedTypeId(typeName))
            DLOG("Disaster", "TYPE ID UNRESOLVED: GetTypeIdByName(\"%s\") returned %s. "
                 "EVERY lookup of that component will report absent, so any census below "
                 "counting it is meaningless — this is a registration problem, not a "
                 "world-contents one.\n",
                 typeName, want ? "the 0xFFFFFFFF pre-Startup sentinel" : "0 (never registered)");
        return nullptr;
    }

    const int count = entity->_components._size;                   // +0x38
    Object::InternalPtr<Component>* arr = entity->_components._data;   // +0x30
    if (!arr || count <= 0) return nullptr;

    for (int i = 0; i < count; ++i) {
        Component* comp = arr[i].Get();
        if (!comp) continue;
        if (comp->Type() == want) return comp;
    }
    return nullptr;
}

// Tile of an entity, from its MapComponent (+0x28 int16 x, +0x2a int16 y — the
// offsets StartTown writes at starttown.cpp:1420). Returns false when the entity
// has no MapComponent, which is normal for non-placed entities.
bool EntityTile(ComponentEntity* entity, int* outX, int* outY) {
    Component* mc = ComponentByName(entity, "MapComponent");
    if (!mc) return false;
    MapComponent* m = static_cast<MapComponent*>(mc);   // exact type id ("MapComponent")
    *outX = m->tileX0;                                  // +0x28
    *outY = m->tileY0;                                  // +0x2a
    return true;
}

// ============================================================
// FIRE — targeted, not random
// ============================================================
// The debug button (DebugUI_Slot9 id 328 -> Game_StartRandomFire, 0x14005feb0)
// picks a UNIFORMLY RANDOM structure. Per the Common.rsc table above that is a coin
// flip on whether the test means anything: a Market or StorageBarn carries
// fireWoodBuilding, which can never start a fire on its own and usually sits with no
// flammable neighbour, so the ignition has nowhere to spread and the run proves nothing.
//
// So: pick HOUSES (entities carrying both a FireComponent and a ResidenceComponent),
// and among those pick the one nearest the centroid of all of them — the middle of
// the residential cluster, which is exactly where FireComponent::SpreadToNeighbors
// (0x140049130) has the most neighbours to reach. Pair with BANISHED_EXTRA_HOUSES to
// make that cluster big enough to be worth burning.
//
// The ignition itself is the retail leaf, through the same pointer the debug button
// uses: fn_FireComponent_Ignite -> ResolveAddr(Exe, 0x140049610).

struct Cand { ComponentEntity* entity; FireComponent* fire; int x; int y; int32_t dist2; };

void ForceFire(int frameNum, int want) {
    Object::InternalPtr<ComponentEntity>* arr = nullptr; int n = 0;
    if (!Registry(&CitizenBehavior::field_0x20, &arr, &n)) {
        DLOG("Fire", "frame %d: structure registry (townMgr+0x20/+0x28) empty or absent "
             "— nothing to ignite\n", frameNum);
        return;
    }

    // Census FIRST, so the negative outcomes are distinguishable from each other.
    Cand cands[64];
    int  nCand = 0;
    int  withFire = 0, withResidence = 0, noTile = 0;

    for (int i = 0; i < n && nCand < 64; ++i) {
        ComponentEntity* ent = arr[i].Get();
        if (!ent) continue;
        // Exact type id ("FireComponent").
        FireComponent* fire = static_cast<FireComponent*>(ComponentByName(ent, "FireComponent"));
        Component* res  = ComponentByName(ent, "ResidenceComponent");
        if (fire) ++withFire;
        if (res)  ++withResidence;
        if (!fire || !res) continue;
        int x = 0, y = 0;
        if (!EntityTile(ent, &x, &y)) { ++noTile; continue; }
        cands[nCand].entity = ent;
        cands[nCand].fire   = fire;
        cands[nCand].x = x; cands[nCand].y = y;
        ++nCand;
    }

    DLOG("Fire", "frame %d: %d structure(s) scanned — withFireComponent=%d "
         "withResidenceComponent=%d houses(both)=%d skippedNoMapComponent=%d\n",
         frameNum, n, withFire, withResidence, nCand, noTile);

    if (nCand == 0) {
        if (withFire == 0)
            DLOG("Fire", "NO structure carries a FireComponent. NOTE: the component is "
                 "CREATED regardless of BANISHED_LIVE_FIRE — FireComponent_Factory calls "
                 "MakeInertTyped unconditionally and the gate only reaches "
                 "FinishLiveVtable (firecomponent.cpp:209-215) — so setting that gate will "
                 "NOT change this line. It means no template in this town has a "
                 "FireComponent: Well, Pasture, CropField, Orchard, Quarry, Mine, Cemetery "
                 "and AnimalShelter carry none. Set BANISHED_EXTRA_HOUSES=<n>. "
                 "Nothing ignited.\n");
        else
            DLOG("Fire", "%d structure(s) are flammable but none is a house. Igniting a "
                 "fireWoodBuilding/fireStoneBuilding template is a wasted test (Common.rsc "
                 "_years=0). Set BANISHED_EXTRA_HOUSES=<n> and retry. Nothing ignited.\n",
                 withFire);
        return;
    }

    // Centroid of the house cluster, then nearest-first ordering.
    int32_t sx = 0, sy = 0;
    for (int i = 0; i < nCand; ++i) { sx += cands[i].x; sy += cands[i].y; }
    const int cx = static_cast<int>(sx / nCand), cy = static_cast<int>(sy / nCand);
    for (int i = 0; i < nCand; ++i) {
        const int32_t dx = cands[i].x - cx, dy = cands[i].y - cy;
        cands[i].dist2 = dx * dx + dy * dy;
    }
    // Selection sort — nCand is tiny and a stable, obvious order beats a clever one.
    for (int i = 0; i < nCand; ++i) {
        int best = i;
        for (int j = i + 1; j < nCand; ++j)
            if (cands[j].dist2 < cands[best].dist2) best = j;
        if (best != i) { Cand t = cands[i]; cands[i] = cands[best]; cands[best] = t; }
    }

    DLOG("Fire", "house cluster centroid=(%d,%d); nearest house at (%d,%d) d2=%d\n",
         cx, cy, cands[0].x, cands[0].y, cands[0].dist2);

    // Snapshot before igniting: FireComponent::Ignite can register the entity into
    // other lists, and SpreadToNeighbors may add more burning entities on later ticks.
    const int take = (want < nCand) ? want : nCand;
    FireComponent* victims[64];
    for (int i = 0; i < take; ++i) victims[i] = cands[i].fire;

    for (int i = 0; i < take; ++i) {
        DLOG("Fire", "igniting %d/%d house entity=%p fireComp=%p tile=(%d,%d)\n",
             i + 1, take, static_cast<void*>(cands[i].entity), static_cast<void*>(victims[i]),
             cands[i].x, cands[i].y);
        victims[i]->Ignite();
    }

    // Outcome: FireComponent::Update treats +0x30 (burn timer) != 0 as "burning"
    // (retail 0x140048fb0 opens `if (*(longlong *)(param_1 + 0x30) == 0)`). Read it
    // back — a zero here means Ignite ran but did not take, which is a different
    // failure from Ignite never having been called.
    int burning = 0;
    for (int i = 0; i < take; ++i)
        if (victims[i]->fireTimer._ticks != 0)              // +0x30
            ++burning;
    DLOG("Fire", "done — %d/%d house(s) now report a non-zero burn timer (+0x30). "
         "%d expected if Ignite is live; 0 means Ignite resolved but did not take.\n",
         burning, take, take);
}

// ============================================================
// TORNADO
// ============================================================
// Retail has no natural tornado on the exe side — 0x14005fd20 (the debug button) is
// the only spawner. Go through DebugUI_Slot9 id 329 rather than re-deriving it: that
// path never touches `self` (it only derefs *g_pGameWorldSingleton), so a null self is
// legal, and routing through the ported dispatcher means the harness exercises the
// same code a real click would.

void ForceTornado(int frameNum, int want) {
    GameUpdateLoop* gw = GameWorld();
    if (!gw) { DLOG("Tornado", "frame %d: gameWorld null\n", frameNum); return; }

    // Preconditions, named individually — "nothing appeared" has three distinct causes
    // and they must not look alike in the log.
    EntityList* entityList = gw->m_entityList;                  // +0xe8
    DLOG("Tornado", "frame %d: preconditions — entityList=%p\n", frameNum,
         static_cast<void*>(entityList));
    if (!entityList) {
        DLOG("Tornado", "entityList null. Nothing spawned.\n");
        return;
    }

    // Approximate entity count (the same field gameupdateloop.cpp:886 reads for its
    // FPS line). Approximate is enough: we only need a before/after delta.
    // +0x30 is buckets[1].count in EntityList's header (row 124).
    const int before = entityList->buckets[1]._size;

    for (int i = 0; i < want; ++i) {
        DLOG("Tornado", "spawning %d/%d via DebugUI_Slot9(id=329)\n", i + 1, want);
        DebugUI_Slot9(nullptr, 0, /*msg=buttonClick*/UI::EventId::Activated, /*id=spawnTornado*/329);
    }

    const int after = entityList->buckets[1]._size;             // +0x30
    DLOG("Tornado", "done — entityList count %d -> %d (+%d expected if "
         "Template/Tornado.rsc resolved and RegisterEntity is live; +0 means the "
         "template did not resolve). A funnel that spawns but never MOVES means "
         "BANISHED_LIVE_TORNADO is off — spawning is this arm's job, moving is the "
         "component's.\n", before, after, want);
}

// ============================================================
// SICK — citizen disease
// ============================================================
// This is retail DebugUI_TriggerDisasterOnBuilding (0x14005ff30, button id 327),
// whose Ghidra name is wrong: the registry it walks is townMgr+0xA0/+0xA8, which
// death_harness.cpp:99 independently establishes as the CITIZEN list, and the leaf it
// calls is fn_DisasterComponent_Infect -> 0x140116b90 = CitizenComponent::Infect
// (citizen, Disease*).
//
// We do NOT dispatch through id 327: that is the one disaster button that reads
// `self` (the disease combo at DebugUI+0x30, debugui.cpp:715), so it needs a live
// DebugUI instance. Resolve the DiseaseDescription here instead, exactly the way
// DebugUI::Slot4 populates that combo (debugui.cpp:1253-1262) — walk the external
// resource list at gameWorld+0xA8 and keep the entries derived from
// the type of the DISEASE EXTERNAL.
//
// TWO DIFFERENT SYSTEMS SHARE THE WORD "disease", and this arm read the wrong handle.
// `TypeId_DiseaseDescription` is a misnomer twice over:
//   * the id it holds is registered under the name "Disease" (the citizen-illness
//     external authored in Game/Disease/*.rsc), NOT the crop lane DiseaseDescription
//     (the CropField/Orchard/Pasture component description); and
//   * it is filled by exactly one thing - debugui_sa::EnsureGlobals(), the DebugUI
//     lazy heal - because ResolveAddr(Exe, 0x1401c5948) returns 0 in standalone.
// So in a run that never builds a DebugUI the global is NULL, FindDiseaseDescription
// returned early with found=0, and the log blamed BANISHED_LIVE_DISEASELIST for a
// resource list that had in fact loaded fine. Read Disease's own type id here instead:
// same id the DebugUI heal would install, minus the ordering dependency.

// The "Disease" external type id (the real class's record, ENDGAME B.3.8), so this arm
// does not depend on a DebugUI ever existing. 0 = never registered, 0xFFFFFFFF = the
// pre-Startup sentinel; both are reported as distinct outcomes by the caller, never
// silently filtered on.
uint32_t DiseaseExternalTypeId() {
    return Disease::TypeId();
}

Disease* FindDiseaseDescription(int wantIdx, int* outFound, int* outScanned,
                             uint32_t* outTypeId, bool* outListPresent) {
    *outFound = 0; *outScanned = 0;
    *outTypeId = DiseaseExternalTypeId();
    *outListPresent = false;
    GameUpdateLoop* gw = GameWorld();
    if (!gw) return nullptr;
    ::Object::ExternalList* externalList = gw->m_diseaseList;   // +0xa8
    if (!externalList) return nullptr;
    *outListPresent = true;
    if (!*outTypeId || *outTypeId == ::Object::kInvalidTypeId) return nullptr;

    const int count = externalList->GetResourceCount();
    *outScanned = count;
    ::Object::External** arr = externalList->_resources;        // +0x20
    if (!arr) return nullptr;

    Disease* chosen = nullptr;
    for (int i = 0; i < count; ++i) {
        ::Object::External* res = arr[i];
        if (!res) continue;
        if (!Object::IsDerivedFromNullSafe(res, *outTypeId)) continue;
        if (*outFound == wantIdx) chosen = static_cast<Disease*>(res);
        ++(*outFound);
    }
    return chosen;
}

void ForceSick(int frameNum, int want) {
    int found = 0, scanned = 0;
    uint32_t diseaseTid = 0;
    bool listPresent = false;
    Disease* disease = FindDiseaseDescription(s_sickDiseaseIdx, &found, &scanned,
                                           &diseaseTid, &listPresent);
    DLOG("Sick", "frame %d: gameWorld+0xA8 list=%s scanned=%d Disease typeId=%u "
         "matches=%d chosenIdx=%d chosen=%p\n",
         frameNum, listPresent ? "present" : "ABSENT", scanned, diseaseTid,
         found, s_sickDiseaseIdx, static_cast<void*>(disease));

    if (!disease) {
        // Four distinct causes, never collapsed into one message.
        if (!listPresent)
            DLOG("Sick", "gameWorld+0xA8 is NULL - the external list was never loaded. "
                 "GameUpdateLoop::Load step 1 loads Game/Disease/Diseases.rsc only under "
                 "BANISHED_LIVE_DISEASELIST. Nobody infected.\n");
        else if (!diseaseTid || diseaseTid == ::Object::kInvalidTypeId)
            DLOG("Sick", "the Disease type id is %s, so IsDerivedFrom can never match and "
                 "the scan of %d resource(s) is meaningless. This is a type-REGISTRATION "
                 "problem, not an empty list. Nobody infected.\n",
                 diseaseTid ? "the 0xFFFFFFFF pre-Startup sentinel" : "0 (never registered)",
                 scanned);
        else if (found == 0)
            DLOG("Sick", "the list loaded (%d resource(s)) and the Disease type id is live "
                 "(%u), but NOTHING in it derives from that type. Nobody infected.\n",
                 scanned, diseaseTid);
        else
            DLOG("Sick", "BANISHED_FORCE_SICK_DISEASE=%d is out of range (%d available). "
                 "Nobody infected.\n", s_sickDiseaseIdx, found);
        return;
    }

    Object::InternalPtr<CitizenComponent>* arr = nullptr; int n = 0;
    if (!Registry(&CitizenBehavior::citizens, &arr, &n)) {
        DLOG("Sick", "frame %d: citizen registry (townMgr+0xA0/+0xA8) empty — "
             "nobody infected\n", frameNum);
        return;
    }

    // Snapshot: infecting can move a citizen between town lists.
    const int take = (want < n) ? want : n;
    CitizenComponent* victims[64];
    const int cap = (take < 64) ? take : 64;
    int picked = 0;
    for (int i = 0; i < n && picked < cap; ++i) {
        CitizenComponent* c = arr[i].Get();
        if (c) victims[picked++] = c;
    }
    DLOG("Sick", "%d citizen(s) selected from a registry of %d\n", picked, n);
    if (picked == 0) {
        DLOG("Sick", "every registry slot resolved null — nobody infected.\n");
        return;
    }

    for (int i = 0; i < picked; ++i) {
        DLOG("Sick", "infecting %d/%d citizen=%p disease=%p\n",
             i + 1, picked, static_cast<void*>(victims[i]), static_cast<void*>(disease));
        victims[i]->Infect(disease);
    }
    DLOG("Sick", "done — %d infection call(s) made. Pair with the citizen-disease lane's "
         "trace to confirm each took; a silent downstream means that lane's live gate is "
         "off, so the component has no live vtable slots to react with.\n", picked);
}

// ============================================================
// INFEST — crop disease
// ============================================================
// Retail Game_InfectRandomCitizen (0x14005fe10, button id 330) is likewise misnamed:
// it walks townMgr+0xF0/+0xF8, a registry of DiseaseComponents (crops / livestock),
// and calls fn_DiseaseComponent_Infect -> 0x1400483a0, whose `self` IS a
// DiseaseComponent. Nothing about it touches a citizen.
//
// We do not use id 330 either, because it picks a UNIFORMLY RANDOM registry entry.
// A random pick makes a failure unattributable — you cannot tell "the infestation
// logic is dead" from "it landed on a livestock pen, not a crop field". So walk the
// registry, climb each DiseaseComponent to its owning entity (comp+0x10 is the owner
// entity IBP — retail FUN_140048840 reads exactly that, then the entity's +0x30/+0x38
// component array), and take the FIRST entry whose owner carries a
// CropFieldComponent. Deterministic and nameable.
//
// ⚠ AND ON THE FIELD ACTUALLY HAVING PLANTS. Retail CheckCure (0x1400481b0) reports a
// field cured when NO NaturalResource entity remains on its tiles, and Infect
// (0x1400483a0) opens by calling it: an empty or freshly-placed field is infected and
// cured in the same call, logging INFECT-SKIPPED reason=already-cureable. That is
// CORRECT retail behaviour, not a defect - but it makes for a worthless test, because
// "the infestation logic is dead" and "the crop had not sprouted yet" look identical in
// the outcome count. So this arm PRE-FILTERS on CheckCure and reports the two
// separately. Crops take in-game weeks to sprout: pair BANISHED_FORCE_INFEST with
// BANISHED_FORCE_INFEST_FRAME set late (or _PERIOD set, so the arm re-arms and keeps
// polling) and let a farmer plant the field first.
//
// ⚠ HARD DEPENDENCY ON THE CROP LANE'S GATE. That registry is populated by exactly
// one thing: DiseaseComponent's OnAdded (vtable slot 6), which is only installed when
// BANISHED_LIVE_CROPDISEASE is set. With the gate off the registry is EMPTY and this
// arm has nothing to infest — which is a fact about the gate, not about the
// infestation logic. Both empty-outcome messages below name the gate for that reason:
// "registry empty" and "infestation is broken" must never look alike in the log.

void ForceInfest(int frameNum, int want) {
    Object::InternalPtr<DiseaseComponent>* arr = nullptr; int n = 0;
    if (!Registry(&CitizenBehavior::diseases, &arr, &n)) {
        DLOG("Infest", "frame %d: DiseaseComponent registry (townMgr+0xF0/+0xF8) empty "
             "or absent — is BANISHED_LIVE_CROPDISEASE set? DiseaseComponent's slot 6 "
             "OnAdded is the ONLY thing that fills that registry, and it is installed "
             "only under that gate. This is NOT evidence that infestation is broken. "
             "Nothing infested.\n", frameNum);
        return;
    }

    DiseaseComponent* picks[64];
    int   pickTiles[64][2];
    int   nPick = 0, ownerless = 0, notCropField = 0, barren = 0;

    for (int i = 0; i < n && nPick < 64; ++i) {
        DiseaseComponent* comp = arr[i].Get();
        if (!comp) continue;
        // comp+0x10 = owner entity IBP.
        ComponentEntity* owner = comp->_entity.Get();
        if (!owner) { ++ownerless; continue; }
        if (!ComponentByName(owner, "CropFieldComponent")) { ++notCropField; continue; }
        // Nothing growing on it yet -> Infect would cure it in the same call. Count it
        // as its own outcome instead of letting it dilute the infestation result.
        if (comp->CheckCure()) {
            ++barren;
            continue;
        }
        int x = 0, y = 0;
        EntityTile(owner, &x, &y);          // best effort; tile is for the log only
        picks[nPick] = comp;
        pickTiles[nPick][0] = x; pickTiles[nPick][1] = y;
        ++nPick;
    }

    DLOG("Infest", "frame %d: %d DiseaseComponent(s) in the registry - cropFields=%d "
         "skippedNoOwner=%d skippedNotCropField=%d skippedNoLivingPlants=%d\n",
         frameNum, n, nPick, ownerless, notCropField, barren);

    if (nPick == 0) {
        if (barren > 0)
            DLOG("Infest", "%d crop field(s) ARE registered but every one of them is "
                 "already cureable - no NaturalResource plant remains on its tiles, so "
                 "retail Infect would cure it in the same call (INFECT-SKIPPED "
                 "reason=already-cureable). This is CORRECT retail behaviour and says "
                 "NOTHING about the infestation logic: the field has not been sown yet. "
                 "Let a farmer plant it, then re-arm (BANISHED_FORCE_INFEST_PERIOD keeps "
                 "this arm polling). Nothing infested.\n", barren);
        else
            DLOG("Infest", "%d DiseaseComponent(s) ARE registered (so BANISHED_LIVE_CROPDISEASE "
                 "is on and slot 6 ran) but none belongs to a crop field. Set "
                 "BANISHED_BUILD_DEMO_CROPFIELD=1 so there is a field to infest. "
                 "Nothing infested.\n", n);
        return;
    }

    const int take = (want < nPick) ? want : nPick;
    for (int i = 0; i < take; ++i) {
        DLOG("Infest", "infesting %d/%d diseaseComp=%p field tile=(%d,%d)\n",
             i + 1, take, static_cast<void*>(picks[i]), pickTiles[i][0], pickTiles[i][1]);
        picks[i]->Infect();
    }

    // Outcome: retail FUN_140048840 (the active-disease tick) treats comp+0x30 > 0 as
    // "currently diseased" and routes to Cure when it drops below zero. Read it back.
    int active = 0;
    for (int i = 0; i < take; ++i)
        if (picks[i]->duration._ticks > 0)                  // +0x30
            ++active;
    DLOG("Infest", "done — %d/%d field(s) now report a positive disease timer (+0x30). "
         "%d expected if Infect is live; 0 means Infect resolved but did not take.\n",
         active, take, take);
}

// ============================================================
// Dispatch
// ============================================================

bool Due(Arm& a, int frameNum) {
    if (a.count <= 0) return false;
    if (frameNum < a.frame) return false;
    if (a.period == 0) {
        if (a.fired) return false;
        a.fired = true;
        return true;
    }
    if (a.lastFireFrame >= 0 && frameNum - a.lastFireFrame < a.period) return false;
    a.lastFireFrame = frameNum;
    return true;
}

}  // namespace

// ============================================================
// Entry point — ticked from GameUpdateLoop::Update
// ============================================================
// No-op unless one of the BANISHED_FORCE_* vars is set.
extern "C" void DisasterHarness_Tick(int frameNum) {
    Init();

    if (Due(s_arms[ARM_FIRE],    frameNum)) ForceFire   (frameNum, s_arms[ARM_FIRE].count);
    if (Due(s_arms[ARM_TORNADO], frameNum)) ForceTornado(frameNum, s_arms[ARM_TORNADO].count);
    if (Due(s_arms[ARM_SICK],    frameNum)) ForceSick   (frameNum, s_arms[ARM_SICK].count);
    if (Due(s_arms[ARM_INFEST],  frameNum)) ForceInfest (frameNum, s_arms[ARM_INFEST].count);
}
