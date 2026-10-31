// src/game/Component/immigrationcomponent_states.cpp
// The IMMIGRATION (nomad arrival) state machine of the town hall — ported 2026-09-05
// for the Immigrants achievement (Ach27, stat 28 = nomads admitted).
//
// ImmigrationComponent (0x68, a ControllerComponent on Template\TownHall.rsc) is the
// town hall's nomad-arrival controller. In retail its behaviour is the state table
// registered once by ImmigrationDescription_Setup -> helper_e6c0 (0x14004e6c0):
//
//   state 2    IDLE        enter d290 / tick d220 : if CanAccept -> 0xf
//                          transitions -> {3, 0xf}
//   state 0xf  WAITING     enter d1a0 : timer = (24 + rand*48) months
//                          tick  d0d0 : timer -= dt; when expired AND month in
//                                       [desc+0x50, desc+0x54] (spring) -> 0x10 if
//                                       CanAccept else back to 2
//                          transitions -> {3, 0x10, 2}
//   state 0x10 ARRIVING    enter d090 : reset + Spawn the group at a map edge
//                          tick  d070 : re-spawn if the group is empty
//                          transitions -> {3 (dismiss on the way out), 0x11}
//                          event 6  cff0 : one nomad reached the hall; all there -> 0x11
//   state 0x11 WAITING-FOR-ANSWER
//                          enter ceb0 : event-log "nomads have arrived" (type 0xe)
//                          tick  ce00 : month >= desc+0x58 (autumn) -> dismiss -> 2
//                          transitions -> {3 (dismiss), 2}
//                          event 7  cd90 : ALLOW  = Accept (convert to citizens) -> 2
//                          event 8  cd20 : DENY   = Dismiss (send them home)     -> 2
//
// The Allow/Deny buttons of the town hall's "Nomads" tab (ImmigrationUI_Slot9,
// immigrationui.cpp) post events 7/8 through the StateMachineComponent node — the
// same node-vtable slot 2 (PostEvent) the Dispatcher below implements.
//
// Component layout (retail ctor 0x14004e640): ImmigrationComponent.h.
// ImmigrationDescription: ImmigrationDescription.h (fields named from their use here).
//
// Opt-in: BANISHED_LIVE_IMMIGRATION (or the global live-components switch). Default off keeps
// the component inert exactly as before this port.
//
// Test fixtures (all env-gated, byte-unchanged when unset — NOT retail behaviour):
//   BANISHED_NOMAD_TRACE=1            stderr trace of every state/event/spawn/accept
//   BANISHED_NOMAD_TIMER_SCALE=<f>    multiply the 2..6-year wait timer (0.001 = now)
//   BANISHED_NOMAD_ANY_SEASON=1       ignore the spring window and the autumn leave
//   BANISHED_NOMAD_AUTOACCEPT=<n>     n ticks after the group is waiting, post the
//                                     ALLOW event (7) through the SM node, exactly
//                                     as the Allow button does; a NEGATIVE n posts
//                                     DENY (event 8) after |n| ticks instead
//
// Faithful offset-by-offset port of 0x14004cd00 .. 0x14004e4b0 and 0x140050030 /
// 0x1400501a0 (nomad -> citizen conversion).
//
// LEFT (B.4 P2 COMP-45): the Pf_ / ModelComponent bodies owned by other blocks take
// int64_t / uintptr_t receivers (wave X); the controller hands the tick its time as void*.

// graphics/*.h FIRST: they pull src/engine/volume/Volume.h, whose global-scope
// `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once <windows.h> has
// been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
// EXT SEAM (NOT RETAIL, docs/extensions/handins/FAMILYTREE.md): ext.family_tree (class C)
#include "ext/family_tree/family_tree.h"
#include "graphics/Mesh.h"
#include "Component/ResidenceComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/NomadDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/NomadComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/InteractDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/InteractComponent.h"
#include "Component/ImmigrationDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/ImmigrationComponent.h"
#include "Component/CitizenDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/CitizenComponent.h"
#include "Component/ModelComponent.h"
#include "Component/StateMachineComponent.h"
#include "Component/ComponentDescription.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Game/ProfessionList.h"
#include <new>
#include "object/External.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Game/Calendar.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Action/exe_bodies.h"            // Pf_*, ActCNR_NR_SetVariant_1253d0
#include "Component/exe_bodies.h"
#include "Game/exe_bodies.h"              // Reimpl_MapGetTerrainPos, EventLog_AddEvent
#include "Player/exe_bodies.h"            // ToolTile_SetTransformFromYaw_3870
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "Object/factory_registration.h"
#include "Component/controller_state_table.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Game/EventLog.h"   // EventLog::AddEvent (0x140095580)

namespace {

// ---------------------------------------------------------------- constants
constexpr float kSecondsPerMonth = 300.0f;   // DAT_140172234
constexpr float kSecondsPerYear  = 3600.0f;  // DAT_14017225c
constexpr float kMonthsPerYear   = 12.0f;    // DAT_1401721b8
constexpr float kHalfCell        = 0.5f;     // DAT_140172114
constexpr float kYawRange        = 180.0f;   // DAT_140172220

// ---------------------------------------------------------------- helpers, by name (B3R)
//   0x140003ea0 FindComponent<StateMachineComponent>
//   0x140122e20 Pf_EntityResolvePos_2e20 (Entity_impl_2e20)   0x140123070 Pf_RecursiveChildSum_123070
//   0x1400c3720 ComponentEntity(ComponentDescription*)
//   0x1400c48a0 EntityList_RegisterEntity_Impl                 0x14009c480 Pf_ZoneRefresh_9c480
//   0x140003870 ToolTile_SetTransformFromYaw_3870              0x1401253d0 ActCNR_NR_SetVariant_1253d0
//   0x140117080 CitizenComponent::Initialize
//   0x140002ed0 FindComponent<InteractComponent> (the engine_funcs global that bound it
//               was misnamed fn_Entity_FindComponent_Position)
// 0x140003110 was Collection::Array<void*>::SetSize(n, fill) monomorphised
// into the exe (ENDGAME B.3.2 step 2) — converted at its call site below.

// Type ids by name, resolved lazily from the live factory tree (the exe .rdata
// globals are not all registered in standalone; see the ImmigrationUI stub note).
uint32_t TidImmDesc()   { static uint32_t t = 0; if (!t) t = ImmigrationDescription::TypeId(); return t; }
uint32_t TidNomadDesc() { static uint32_t t = 0; if (!t) t = NomadDescription::TypeId();       return t; }
uint32_t TidCitDesc()   { static uint32_t t = 0; if (!t) t = CitizenDescription::TypeId();     return t; }
// cell 0x1401c7938 (TypeId_InteractDescription_FC) always fell back to
// InteractDescription::TypeId() unconditionally once filled; converting by cell
// (D5 exception) is mechanically equivalent.
uint32_t TidInteractDesc() { static uint32_t t = 0; if (!t) t = InteractDescription::TypeId(); return t; }

// ---------------------------------------------------------------- small accessors
inline GameUpdateLoop* GW() { return GameUpdateLoop::_singleton; }
inline CitizenBehavior* TownMgr(GameUpdateLoop* gw) { return gw->m_citizenList; }   // +0xF0
inline Map*     Grid(GameUpdateLoop* gw) { return gw->m_game; }   // +0xF8

// desc = comp->_description if IsDerivedFrom(ImmigrationDescription) else null — retail
// dereferences the null; a town hall without its description is a broken template.
ImmigrationDescription* ImmDesc(ImmigrationComponent* comp) {
    // IsDerivedFrom on the unguarded description, CMOVNZ (e.g. 0x14004d0f7..0x14004d120).
    Description* d = comp->_description;
    return d->IsDerivedFrom(TidImmDesc()) ? static_cast<ImmigrationDescription*>(d) : nullptr;
}

// The month of the in-game year: the clock at gw+0x128 holds {year int, Time-in-year}.
int MonthOfYear(GameUpdateLoop* gw) {
    Calendar* clock = gw->m_dayCounter;   // +0x128, read unguarded (0x14004d119)
    float secs = clock->yearTime.AsSeconds();   // +0x08
    int m = static_cast<int>((secs / kSecondsPerYear) * kMonthsPerYear);
    if (m > 11) m = 0;
    return m;
}

// Post an event to an entity's StateMachineComponent through its node vtable slot 2,
// the exact call shape retail uses: node->PostEvent(node, &sm, curIdx, eventId).
// Retail tests none of the entity, the component or the node (Dismiss 0x14004d394, Accept
// 0x14004d615): a group member always carries its StateMachineComponent.
void PostEventToEntity(ComponentEntity* entity, int eventId) {
    StateMachineComponent* smc = entity->FindComponent<StateMachineComponent>();   // 0x140003ea0
    ControllerComponent::Controller* node = smc->stateMachine.current;
    node->PostEvent(   // slot 2
        &smc->stateMachine, smc->stateMachine.activeState, eventId);
}

}  // namespace

// ============================================================
// CanAccept — port of 0x14004e3f0
// population >= desc+0x28 AND every required template in desc+0x30[0..+0x38) has a
// built instance (townMgr+0x160 BST: key@+0, count@+0x10, left@+0x20, right@+0x28).
// ============================================================
bool ImmigrationComponent::CanAccept() {
    GameUpdateLoop* gw = GW();
    ImmigrationDescription* desc = ImmDesc(this);
    CitizenBehavior* tm = TownMgr(gw);
    if (tm->citizens._size < desc->_minPopulation) return false;
    int req = desc->_requiredBuildingCount;
    if (req < 1) return true;
    // The +0x160 template bucket tree (key compared as an unsigned qword, 0x1400172f0).
    TemplateBucket* root = tm->field_0x160._root;
    ComponentDescription** keys = desc->_requiredBuildings;
    for (int i = 0; i < req; ++i) {
        ComponentDescription* key = keys[i];
        TemplateBucket* n = root;
        while (n) {
            if (key < n->element.key)       n = n->left;    // +0x20
            else if (key <= n->element.key) break;
            else                    n = n->right;   // +0x28
        }
        if (!n || n->element.entities._size == 0) return false;   // +0x10
    }
    return true;
}

// ============================================================
// FindSpawnCell — port of 0x14004e270
// Up to 50 tries: a random cell within desc+0x5c of (x, y) that is walkable
// (flag bit in 0xe07) and — after a zone refresh — in the same pathing zone
// (tile+0x26) as the town hall's cell, or on a zone-less bridge/road class (0x1e0).
// ============================================================
int ImmigrationComponent::FindSpawnCell(int x, int y, int hallCell) {
    GameUpdateLoop* gw = GW();
    ImmigrationDescription* desc = ImmDesc(this);   // read unguarded (0x14004e2c4 / 0x14004e2d0)
    System::Random* rng = gw->m_random;
    Map* grid = Grid(gw);
    const int W = grid->size;
    const int reach = desc->_spawnEdgeDistance;
    for (int t = 0; t < 50; ++t) {
        int cx = x + rng->RandTol(static_cast<int32_t>(reach));
        int cy = y + rng->RandTol(static_cast<int32_t>(reach));
        if (cx < 0) cx = 0; else if (cx >= W) cx = W - 1;
        if (cy < 0) cy = 0; else if (cy >= W) cy = W - 1;
        int c = W * cy + cx;
        const MapCell* tiles = grid->cells.GetData();   // Map +0x80
        uint32_t fc = 1u << (tiles[static_cast<uintptr_t>(c)].terrainType & 0x1f);
        if ((fc & 0xe07u) == 0) continue;
        grid->FillZones();   // 0x14009c480 (RCX only)
        tiles = grid->cells.GetData();
        const MapCell* tc = &tiles[static_cast<uintptr_t>(c)];
        const MapCell* th = &tiles[static_cast<uintptr_t>(hallCell)];
        uint32_t mh = 1u << (th->terrainType & 0x1f);
        uint32_t mc = 1u << (tc->terrainType & 0x1f);
        uint32_t u = mc | mh;
        uint32_t u8 = u | 0xe1fu;
        if ((u8 & mc) && (u8 & mh)) {
            if (u & 0x1e0u) return c;
            int16_t zc = static_cast<int16_t>(tc->zone);   // +0x26
            int16_t zh = static_cast<int16_t>(th->zone);
            if (zc != 0 && zh != 0 && zc == zh) return c;
        }
    }
    return -1;
}

// ============================================================
// NomadComponent::Init — port of 0x140050030
// Bind a freshly cloned nomad to its town hall: target (+0x28), spawn position
// (+0x30), meeting position (+0x3c), gender (+0x50); place the model at the spawn
// point with a random yaw, pick the mesh variant and material for the gender.
// ============================================================
void NomadComponent::Init(const Object::InternalPtr<ComponentEntity>* hallEntity, const Math::Vector3* spawn,
                          const Math::Vector3* meet) {
    GameUpdateLoop* gw = GW();
    // The description through CMOVNZ, the model and the material read with no test
    // (0x140050078, 0x1400500dd, 0x140050138 / 0x14005013e).
    Description* nd = _description;
    NomadDescription* ndesc = nd->IsDerivedFrom(TidNomadDesc()) ? static_cast<NomadDescription*>(nd) : nullptr;
    hall = *hallEntity;
    spawnPoint = *spawn;
    meetPoint = *meet;
    System::Random* rng = gw->m_random;
    gender = static_cast<int32_t>(rng->Rand(2u));

    ModelComponent* model = _entity->FindComponent<ModelComponent>();   // DAT_1401c7a50
    float yaw = rng->RandFloatTol() * kYawRange;
    model->SetTransformFromYaw(&spawnPoint, yaw);   // 0x140003870
    model->SelectMesh(// 0x1401253d0
                    static_cast<uint16_t>(gender), 0);
    Graphics::MaterialInstance* mat = (gender == 0) ? ndesc->_materialGender0 : ndesc->_materialGender1;
    model->meshInstance
        .SetMaterialInstance(0, Object::ExternalPtr<Graphics::MaterialInstance>(mat));
}

// ============================================================
// Spawn — port of 0x14004dbe0
// Pick a map edge, find a walkable edge cell in the hall's pathing zone, size the
// group from the population (5..15%) plus a random share of the empty house slots
// (minimum 2), then clone the nomad template once per member at that edge and
// register each into the world.
// ============================================================
void ImmigrationComponent::Spawn() {
    GameUpdateLoop* gw = GW();
    ImmigrationDescription* desc = ImmDesc(this);   // read unguarded (0x14004dbe0, the first +0x5c read)
    System::Random* rng = gw->m_random;
    Map* grid = Grid(gw);
    const int W = grid->size;
    const int edge = desc->_spawnEdgeDistance;

    InteractComponent* pc = _entity->FindComponent<InteractComponent>();   // 0x140002ed0

    int x = 0, y = 0, hallCell = 0, cell = -1;
    for (int t = 0; t < 10; ++t) {
        uint32_t r = rng->Rand(4u);
        if (static_cast<int>(r) < 2) {
            x = (r == 0) ? edge : (W - edge) - 1;
            y = static_cast<int>(rng->Rand(static_cast<uint32_t>(W)));
        } else {
            x = static_cast<int>(rng->Rand(static_cast<uint32_t>(W)));
            y = (r == 2) ? edge : (W - edge) - 1;
        }
        Math::Vector3 hp;
        pc->NodePosition(&hp, 0, 0, nullptr);
        int cx = static_cast<int>(hp.x + static_cast<float>(W >> 1));
        int cy = static_cast<int>(hp.y + static_cast<float>(W >> 1));
        if (cx < 0) cx = 0;
        if (cx >= W) cx = W - 1;
        if (cy < 0) cy = 0;
        if (cy >= W) cy = W - 1;
        hallCell = cy * W + cx;
        cell = FindSpawnCell(x, y, hallCell);
        if (cell >= 0) break;
    }
    if (cell < 0) {
        return;
    }

    CitizenBehavior* tm = TownMgr(gw);
    const int pop = tm->citizens._size;
    const float fLo = desc->_groupFractionMin;
    const float fRand = rng->RandFloat();
    const float fHi = desc->_groupFractionMax;

    // Empty household slots across every registered house (townMgr+0x20/+0x28).
    int emptySlots = 0;
    {
        int nh = tm->field_0x20._size;
        Object::InternalPtr<ComponentEntity>* houses = tm->field_0x20._data;
        for (int i = 0; i < nh; ++i) {
            ComponentEntity* h = houses[i].Get();
            // Neither the house nor its ResidenceComponent is tested (0x14004de75, the +0x58 read).
            ResidenceComponent* rc = h->FindComponent<ResidenceComponent>();
            int slots = rc->residentSlots._size;
            const ResidenceComponent::Family* arr = rc->residentSlots._data;   // +0x50, stride 0x10
            for (int s = 0; s < slots; ++s)
                if (arr[static_cast<uintptr_t>(s)]._size == 0) ++emptySlots;   // Family +0x08
        }
    }
    // Retail calls Rand(emptySlots*2) unguarded; Rand(0) returns 0 without drawing.
    int extra = static_cast<int>(rng->Rand(static_cast<uint32_t>(emptySlots * 2)));
    int n = extra + static_cast<int>(static_cast<float>(pop) * (fRand * (fHi - fLo) + fLo));
    if (n < 2) n = 2;


    for (int k = 0; k < n; ++k) {
        int c = FindSpawnCell(x, y, hallCell);
        if (c < 0) c = cell;

        // A random "use point" on the town hall for this nomad to stand at.
        Description* idesc = pc->_description;
        InteractDescription* id = idesc->IsDerivedFrom(TidInteractDesc())
                                      ? static_cast<InteractDescription*>(idesc) : nullptr;
        uint32_t nPts = static_cast<uint32_t>(id->_createNodes.count);   // +0x50, unguarded
        int nChildren = pc->useLocations._size;
        for (int ci = 0; ci < nChildren; ++ci) {
            nPts += static_cast<uint32_t>(pc->useLocations[ci].Get()->NodeCount(2));   // 0x140123070
        }
        uint32_t ptIdx = rng->Rand(static_cast<uint32_t>(nPts));
        Math::Vector3 meet(0.0f, 0.0f, 0.0f);
        pc->NodePosition(&meet, 2, static_cast<int>(ptIdx), nullptr);

        // Clone the nomad template.
        void* mem = System::MemoryAllocator::MemAlloc(0x48, 8);
        ComponentEntity* ent = mem ? new (mem) ComponentEntity(desc->_nomad) : nullptr;   // 0x1400c3720

        // World position of the spawn cell, snapped to the terrain.
        float half = static_cast<float>(W >> 1) - kHalfCell;
        float cellPos[3] = { static_cast<float>(c % W) - half, static_cast<float>(c / W) - half, 0.0f };
        Math::Vector3 spawn(0.0f, 0.0f, 0.0f);
        grid->GetTerrainPos(&spawn.x, cellPos, '\0', nullptr);

        // Bind its NomadComponent to this hall and drop it into the world.
        // Neither the clone nor its NomadComponent is tested (0x14004e127).
        NomadComponent* nomad = ent->FindComponent<NomadComponent>();
        nomad->Init(&_entity, &spawn, &meet);   // 0x140050030
        int category = ent->_description->_category;
        Object::InternalPtr<ComponentEntity> regIBP = ent;
        gw->m_entityList->RegisterEntity(&regIBP, category);
        // Append to the group array (+0x58/+0x60/+0x64) — retail inlines this
        // DynArray8 push; the registered resize does the same growth policy.
        int count = group._size;
        ComponentEntity* pushIBP = ent;
        // exe 0x140003110 IS Collection::Array<void*>::SetSize(n, fill) (kSpec_PtrZero).
        group.SetSizeFill(count + 1, &pushIBP);
    }
    arrivedCount = 0;
}

// ============================================================
// Dismiss — port of 0x14004d300: event 1 (LEAVE) to every nomad, drop the group.
// ============================================================
void ImmigrationComponent::Dismiss() {
    int n = group._size;
    Object::InternalPtr<ComponentEntity>* arr = group._data;
    for (int i = 0; i < n; ++i) {
        PostEventToEntity(arr[i].Get(), 1);
    }
    if (arr) System::MemoryAllocator::MemFree(arr);
    group._data = nullptr;
    group._size = 0;
    group._capacity = 0;
    arrivedCount = 0;
}

// ============================================================
// NomadComponent::ConvertToCitizen — port of 0x1400501a0: clone the citizen template
// for one nomad at the nomad's current position and run the per-citizen deferred init
// (name, age, gender, family link). Returns the new CitizenComponent (out IBP).
// ============================================================
CitizenComponent* NomadComponent::ConvertToCitizen(CitizenComponent** out, CitizenComponent* const* parent,
                                                   int genderArg, char isChild) {
    GameUpdateLoop* gw = GW();
    // Retail tests none of the nomad description, the citizen, its description, the
    // nomad's model or the parent slot (0x1400501a0: +0x20, +0x18, +0x50 / +0x54, +0x158
    // and *param_3 all read unguarded).
    Description* nd = _description;
    NomadDescription* ndesc = nd->IsDerivedFrom(TidNomadDesc()) ? static_cast<NomadDescription*>(nd) : nullptr;
    System::Random* rng = gw->m_random;

    void* mem = System::MemoryAllocator::MemAlloc(0x48, 8);
    ComponentEntity* ent = mem ? new (mem) ComponentEntity(ndesc->_citizen) : nullptr;   // 0x1400c3720
    *out = ent->FindComponent<CitizenComponent>();
    CitizenComponent* cit = *out;
    Description* cd = cit->_description;
    CitizenDescription* cdesc = cd->IsDerivedFrom(TidCitDesc()) ? static_cast<CitizenDescription*>(cd) : nullptr;

    // Spawn the citizen where the nomad stands (its model transform translation).
    ModelComponent* model = _entity->FindComponent<ModelComponent>();
    Math::Vector3 pos = model->transform.translation;
    float age;
    float aLo = cdesc->minPregnancyAge;
    float aHi = cdesc->maxPregnancyAge;
    if (isChild == 0) age = rng->RandFloat() * (aHi - aLo) + aLo;
    else              age = rng->RandFloat() * aLo;
    uint32_t r = rng->Rand(1u);
    char flag = (r == 0 || isChild != 0) ? 0 : 1;
    Profession* key = gw->m_professionList->field_0x30;
    cit->Initialize(&pos, static_cast<uint32_t>(genderArg), isChild, age, flag,
                    *parent, key);   // 0x140117080

    Object::InternalPtr<ComponentEntity> regIBP = ent;
    {
        int category = ndesc->_citizen->_category;
        gw->m_entityList->RegisterEntity(&regIBP, category);
    }
    return *out;
}

// ============================================================
// Accept — port of 0x14004d450 (the ALLOW handler's body).
// Seeds the town's disease clock (townMgr+0x1a8 chance, +0x1a0 duration), adds the
// group to the NOMADS-ADMITTED counter at townMgr+0x1b0 (achievement stat 28), then
// converts the group in fours: adult (gender 1), adult (gender 0) married to it,
// and two children of the second adult; every converted nomad gets event 0
// (DESTROY). Finally frees the group array.
// ============================================================
void ImmigrationComponent::Accept() {
    GameUpdateLoop* gw = GW();
    ImmigrationDescription* desc = ImmDesc(this);   // read unguarded (0x14004d450: +0x60 / +0x64)
    CitizenBehavior* tm = TownMgr(gw);

    float chance = desc->_diseaseChance;
    if (tm->field_0x1A8 < chance) tm->field_0x1A8 = chance;
    System::Time dur;
    dur._ticks = 0;
    RtSystem_Time_Ctor(&dur, static_cast<float>(desc->_diseaseMonths) * kSecondsPerMonth);
    if (tm->field_0x1A0._ticks < dur._ticks) tm->field_0x1A0._ticks = dur._ticks;

    const int n = group._size;
    TownMgr(gw)->field_0x1B0 += n;

    Object::InternalPtr<ComponentEntity>* arr = group._data;
    auto nomadAt = [&](int i) -> ComponentEntity* {
        return arr[i].Get();
    };
    auto convert = [&](int i, CitizenComponent* const* parent, int gender, char child) -> CitizenComponent* {
        ComponentEntity* ent = nomadAt(i);
        NomadComponent* nomad = ent->FindComponent<NomadComponent>();
        CitizenComponent* cit = nullptr;
        nomad->ConvertToCitizen(&cit, parent, gender, child);   // 0x1400501a0 (unguarded, as the tree's)
        PostEventToEntity(nomadAt(i), 0);
        return cit;
    };

    int genderC = 0;
    for (int i = 0; i < n; ) {
        CitizenComponent* nullIBP = nullptr;
        CitizenComponent* a = convert(i, &nullIBP, 1, 0);
        if (i + 1 >= n) break;
        CitizenComponent* nullIBP2 = nullptr;
        CitizenComponent* b = convert(i + 1, &nullIBP2, 0, 0);
        b->spouse = a;   // both written unguarded (after 0x14004d745)
        a->spouse = b;
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/FAMILYTREE.md): ext.family_tree (class C), an
        // immigrant couple
        Ext::FamilyTree::OnMarried(a, b);
        if (i + 2 >= n) break;
        CitizenComponent* parent = b;
        convert(i + 2, &parent, genderC, 1);
        if (i + 3 >= n) break;
        int genderD = (genderC == 0) ? 1 : 0;
        CitizenComponent* parent2 = b;
        convert(i + 3, &parent2, genderD, 1);
        genderC = (genderD == 0) ? 1 : 0;
        i += 4;
    }
    if (arr) System::MemoryAllocator::MemFree(arr);
    group._data = nullptr;
    group._size = 0;
    group._capacity = 0;
    arrivedCount = 0;
}

// ============================================================
// State handlers (the controller hands them this component and its StateMachine)
// ============================================================

// state 2 IDLE — enter 0x14004d290 (returns 0), tick 0x14004d220 (returns 1)
char ImmigrationComponent::EnterIdle(StateMachine* sm) {
    if (CanAccept()) ::ControllerLive::RequestStateTransition(sm, 0xf);
    return 0;
}
char ImmigrationComponent::TickIdle(StateMachine* sm) {
    if (CanAccept()) ::ControllerLive::RequestStateTransition(sm, 0xf);
    return 1;
}

// state 0xf WAITING — enter 0x14004d1a0: timer = (base + rand*range) months
char ImmigrationComponent::EnterWaiting() {
    GameUpdateLoop* gw = GW();
    ImmigrationDescription* desc = ImmDesc(this);
    float r = gw->m_random->RandFloat();
    float months = r * desc->_waitRandomMonths + desc->_waitMonths;
    float secs = months * kSecondsPerMonth;
    System::Time t;
    RtSystem_Time_Ctor(&t, secs);
    waitTimer = t;
    return 0;
}
// tick 0x14004d0d0: countdown; in season -> 0x10 if CanAccept else 2
char ImmigrationComponent::TickWaiting(StateMachine* sm, const System::Time* time) {
    GameUpdateLoop* gw = GW();
    ImmigrationDescription* desc = ImmDesc(this);
    waitTimer._ticks -= time->_ticks;   // 0x14004d0ea / 0x14004d0f3
    int month = MonthOfYear(gw);
    bool inSeason = (desc->_arrivalMonthFirst <= month && month <= desc->_arrivalMonthLast);
    if (waitTimer._ticks < 1 && inSeason) {
        bool ok = CanAccept();
        ::ControllerLive::RequestStateTransition(sm, ok ? 0x10 : 2);
    }
    return 1;
}

// state 0x10 ARRIVING — enter 0x14004d090: reset the group and spawn it
char ImmigrationComponent::EnterArriving() {
    arrivedCount = 0;
    Object::InternalPtr<ComponentEntity>* old = group._data;
    if (old) System::MemoryAllocator::MemFree(old);
    group._data = nullptr;
    group._size = 0;
    group._capacity = 0;
    Spawn();   // 0x14004dbe0
    return 1;
}
// tick 0x14004d070: spawn again if the group is empty
char ImmigrationComponent::TickArriving() {
    if (group._size == 0) Spawn();   // 0x14004dbe0
    return 1;
}
// event 6 0x14004cff0: a nomad reached the hall
char ImmigrationComponent::OnArrived(StateMachine* sm) {
    int32_t arrived = ++arrivedCount;
    if (arrived == group._size) ::ControllerLive::RequestStateTransition(sm, 0x11);
    return 1;
}
// transition 0x10/0x11 -> 3 handler 0x14004cd00: dismiss on the way out
void ImmigrationComponent::OnLeaveGroup() {
    Dismiss();   // 0x14004d300
}

// state 0x11 WAITING FOR AN ANSWER — enter 0x14004ceb0: the event-log line
char ImmigrationComponent::EnterAnswer() {
    GameUpdateLoop* gw = GW();
    Object::InternalPtr<ComponentEntity> ownerIBP(_entity);
    gw->m_eventNotifier->AddEvent(0xe, 2, &ownerIBP);   // retail inlines it (0x14004cece..)
    return 1;
}
// tick 0x14004ce00: past the leave month -> dismiss -> 2
char ImmigrationComponent::TickAnswer(StateMachine* sm) {
    GameUpdateLoop* gw = GW();
    ImmigrationDescription* desc = ImmDesc(this);
    if (desc->_leaveMonth <= MonthOfYear(gw)) {
        Dismiss();   // 0x14004d300
        ::ControllerLive::RequestStateTransition(sm, 2);
    }
    return 1;
}
// event 7 0x14004cd90: ALLOW
char ImmigrationComponent::OnAllow(StateMachine* sm) {
    Accept();   // 0x14004d450
    ::ControllerLive::RequestStateTransition(sm, 2);
    return 1;
}
// event 8 0x14004cd20: DENY
char ImmigrationComponent::OnDeny(StateMachine* sm) {
    Dismiss();   // 0x14004d300
    ::ControllerLive::RequestStateTransition(sm, 2);
    return 1;
}

// 0x1400a9ac0 — request a transition on a StateMachine (shared idiom).
extern "C" void Imm_RequestStateTransition_9ac0(StateMachine* sm, int stateId) {
    ::ControllerLive::RequestStateTransition(sm, stateId);
}

// ============================================================
// Dispatcher + table (replicates ImmigrationDescription_helper_e6c0, 0x14004e6c0)
// ============================================================
// ENDGAME B.3.8 MI-A: the tag lives with the class (its controller member is
// ControllerLive::TableController<ImmigrationCtrlTag>, ImmigrationComponent.h).
#include "Component/ImmigrationComponent.h"
using ImmigrationDispatch = ::ControllerLive::Dispatcher<ImmigrationCtrlTag>;

// The table (controller_state_table.h) holds plain handler addresses called as
// handler([node+0x18], sm[, time]): [node+0x18] is the controller's back-pointer, this
// component, so each entry is a one-line adapter that calls the member.
namespace {
char ImmEnterIdle(ImmigrationComponent* c, StateMachine* sm)                { return c->EnterIdle(sm); }        // 0x14004d290
char ImmTickIdle(ImmigrationComponent* c, StateMachine* sm, const System::Time*)          { return c->TickIdle(sm); }         // 0x14004d220
char ImmEnterWaiting(ImmigrationComponent* c, StateMachine*)                { return c->EnterWaiting(); }       // 0x14004d1a0
char ImmTickWaiting(ImmigrationComponent* c, StateMachine* sm, const System::Time* time)  { return c->TickWaiting(sm, time); } // 0x14004d0d0
char ImmEnterArriving(ImmigrationComponent* c, StateMachine*)               { return c->EnterArriving(); }      // 0x14004d090
char ImmTickArriving(ImmigrationComponent* c, StateMachine*, const System::Time*)         { return c->TickArriving(); }       // 0x14004d070
char ImmOnArrived(ImmigrationComponent* c, StateMachine* sm)                { return c->OnArrived(sm); }        // 0x14004cff0
void ImmOnLeaveGroup(ImmigrationComponent* c)                       { c->OnLeaveGroup(); }              // 0x14004cd00
char ImmEnterAnswer(ImmigrationComponent* c, StateMachine*)                 { return c->EnterAnswer(); }        // 0x14004ceb0
char ImmTickAnswer(ImmigrationComponent* c, StateMachine* sm, const System::Time*)        { return c->TickAnswer(sm); }       // 0x14004ce00
char ImmOnAllow(ImmigrationComponent* c, StateMachine* sm)                  { return c->OnAllow(sm); }          // 0x14004cd90
char ImmOnDeny(ImmigrationComponent* c, StateMachine* sm)                   { return c->OnDeny(sm); }           // 0x14004cd20
}  // namespace

extern "C" void ImmigrationCtrl_PopulateTable() {
    using namespace ::ControllerLive;
    auto& t = ImmigrationDispatch::table;
    if (t.count != 0) return;
    // sub_7ba0(id, ENTER, p3, p4, TICK) -> {id, enter@+0x08, trans38@+0x18}
    TableRegisterState(t, 2, &ImmEnterIdle, nullptr,
                       &ImmTickIdle, nullptr);
    TableAddTransition(t, 3, nullptr);
    TableAddTransition(t, 0xf, nullptr);

    TableRegisterState(t, 0xf, &ImmEnterWaiting, nullptr,
                       &ImmTickWaiting, nullptr);
    TableAddTransition(t, 3, nullptr);
    TableAddTransition(t, 0x10, nullptr);
    TableAddTransition(t, 2, nullptr);

    TableRegisterState(t, 0x10, &ImmEnterArriving, nullptr,
                       &ImmTickArriving, nullptr);
    TableAddTransition(t, 3, &ImmOnLeaveGroup);
    TableAddTransition(t, 0x11, nullptr);
    TableAddEvent(t, 6, &ImmOnArrived);

    TableRegisterState(t, 0x11, &ImmEnterAnswer, nullptr,
                       &ImmTickAnswer, nullptr);
    TableAddTransition(t, 3, &ImmOnLeaveGroup);
    TableAddTransition(t, 2, nullptr);
    TableAddEvent(t, 7, &ImmOnAllow);
    TableAddEvent(t, 8, &ImmOnDeny);
}
