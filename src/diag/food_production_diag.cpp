// src/diag/food_production_diag.cpp
// Behavioural-equivalence probe for the six food-production classes.
//
// BANISHED_FOOD_DIAG=<periodFrames>  (0/unset = off, "1" = default 600)
//
// Every period it prints, for each of CropFieldComponent / OrchardComponent /
// PastureComponent / FishingComponent / HuntComponent / HerbalistComponent:
//   * how many instances exist,
//   * each one's state-machine state id and its workplace staffing/accepting bits,
//   * for the fishing dock, its computed rate (+0x54) and registered latch (+0x50),
// and then the TOWN STORAGE totals broken down by resource type.
//
// The storage total is the only number that answers "does this building feed the
// town". Counting works posted, or piles on the ground, does not: a work nobody
// claims and a pile nobody hauls are both a yield of zero. This deliberately reports
// zero counts too — a class with no instances prints "0 instance(s)" rather than
// nothing, so a silent section can never be mistaken for a passing one.
//
// HARNESS ONLY: reads published fields, writes nothing. Default off.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include "Object/factory_registration.h"
#include "object/TypeInfo.h"   // Object::IsDerivedFromNullSafe
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Component/CropFieldComponent.h"
#include "Component/FishingComponent.h"
#include "Component/HerbalistComponent.h"
#include "Component/HerbalistDescription.h"
#include "Component/HuntComponent.h"
#include "Component/MapComponent.h"
#include "Component/OrchardComponent.h"
#include "Component/PastureComponent.h"
#include "Component/RadiusDescription.h"
#include "Component/StateMachineComponent.h"
#include "Component/StorageComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/ComponentDescription.h"   // B4: the entity's sub-description list
#include "Entity/ComponentEntity.h"           // B4: the component array by name
#include "Entity/EntityList.h"                // B4: GameUpdateLoop::m_entityList
#include "Game/Map.h"                         // B4: Map::size / cells, MapCell::occupant

namespace {

int  s_period  = 0;
bool s_init    = false;

void Init() {
    if (s_init) return;
    s_init = true;
    const char* e = Opt().foodDiag;
    if (!e || !e[0] || std::strcmp(e, "0") == 0) { s_period = 0; return; }
    s_period = HarnessAtoi(e);
    if (s_period <= 1) s_period = 600;
    std::fprintf(stderr, "[FoodDiag] ENABLED — dumping every %d frames\n", s_period);
    std::fflush(stderr);
}

struct ClassRow {
    const char* name;
    uint32_t    tid;
    int         count;
};

// The component's embedded state node lives at comp+0x28 for every class in this
// wave (ControllerComponent layout); the owning StateMachine carries the current
// state id in its +0x0a byte. We read the StateMachineComponent instead, which is
// where the id is actually published — comp+0x28 is only the node.
// Find a component of type `tid` on an entity (the +0x30 array / +0x38 count pair).
Component* FindComp(ComponentEntity* ent, uint32_t tid) {
    if (!ent || !tid) return nullptr;
    const int n = ent->_components._size;                          // +0x38
    Object::InternalPtr<Component>* a = ent->_components._data;        // +0x30
    for (int i = 0; i < n && a; ++i) {
        Component* c = a[i].Get();
        if (!c) continue;
        if (c->Type() == tid) return c;
    }
    return nullptr;
}

void DumpStorage(int frame, Object::InternalPtr<ComponentEntity>* arr, int n) {
    const uint32_t tidStorage = StorageComponent::TypeId();
    if (!tidStorage || !arr || n <= 0) {
        std::fprintf(stderr, "[FoodDiag] frame=%d storage: CANNOT SCAN "
                     "(tid=%u entities=%d)\n", frame, tidStorage, n);
        return;
    }
    struct Stack { const ComponentDescription* rt; long long ct; };
    Stack byType[32] = {};
    int types = 0, storages = 0, nonEmpty = 0;
    long long grand = 0;

    for (int i = 0; i < n; ++i) {
        ComponentEntity* ent = arr[i].Get();
        if (!ent) continue;
        const int cN = ent->_components._size;                     // +0x38
        Object::InternalPtr<Component>* cA = ent->_components._data;   // +0x30
        for (int c = 0; c < cN && cA; ++c) {
            Component* comp = cA[c].Get();
            if (!comp) continue;
            if (comp->Type() != tidStorage) continue;
            ++storages;
            StorageComponent* st = static_cast<StorageComponent*>(comp);   // exact type id
            const int stacks = st->storageSlots._size;                    // +0x30
            const StorageComponent::Item* inv = st->storageSlots._data;   // +0x28 (stride 0x10)
            if (stacks > 0) ++nonEmpty;
            for (int k = 0; inv && k < stacks; ++k) {
                const StorageComponent::Item& e = inv[k];
                const ComponentDescription* rt = e._resource;    // +0x00
                const int ct = e._count;                         // +0x08
                grand += ct;
                int f = -1;
                for (int t = 0; t < types; ++t) if (byType[t].rt == rt) { f = t; break; }
                if (f < 0 && types < 32) { f = types++; byType[f].rt = rt; byType[f].ct = 0; }
                if (f >= 0) byType[f].ct += ct;
            }
        }
    }
    std::fprintf(stderr, "[FoodDiag] frame=%d storage: %d component(s), %d holding, "
                 "%d type(s), TOTAL ITEMS=%lld\n",
                 frame, storages, nonEmpty, types, grand);
    for (int t = 0; t < types; ++t)
        std::fprintf(stderr, "[FoodDiag]     resType=%p count=%lld\n",
                     static_cast<const void*>(byType[t].rt), byType[t].ct);
}

void DumpAll(int frame) {
    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    if (!gw) {
        std::fprintf(stderr, "[FoodDiag] frame=%d: gameWorld null\n", frame);
        return;
    }
    EntityList* entityList = gw->m_entityList;                  // +0xe8
    if (!entityList) {
        std::fprintf(stderr, "[FoodDiag] frame=%d: EntityList (gw+0xe8) null\n", frame);
        return;
    }
    Object::InternalPtr<ComponentEntity>* arr = entityList->masterArray._data;       // +0x08
    int       n   = entityList->masterArray._size;              // +0x10

    ClassRow rows[] = {
        { "CropFieldComponent", CropFieldComponent::TypeId(), 0 },
        { "OrchardComponent",   OrchardComponent::TypeId(),   0 },
        { "PastureComponent",   PastureComponent::TypeId(),   0 },
        { "FishingComponent",   FishingComponent::TypeId(),   0 },
        { "HuntComponent",      HuntComponent::TypeId(),      0 },
        { "HerbalistComponent", HerbalistComponent::TypeId(), 0 },
    };
    const int nRows = (int)(sizeof(rows) / sizeof(rows[0]));
    const uint32_t tidSM  = StateMachineComponent::TypeId();
    const uint32_t tidWPC = WorkPlaceComponent::TypeId();

    for (int i = 0; i < n && arr; ++i) {
        ComponentEntity* ent = arr[i].Get();
        if (!ent) continue;

        for (int r = 0; r < nRows; ++r) {
            if (!rows[r].tid) continue;
            Component* comp = FindComp(ent, rows[r].tid);
            if (!comp) continue;
            ++rows[r].count;

            // StateMachineComponent: the live state id is the +0x51 byte of the
            // component (the node-local index the orchard panel path already reads).
            int stateId = -1;
            if (Component* smc = FindComp(ent, tidSM))   // exact type id
                stateId = static_cast<StateMachineComponent*>(smc)->stateMachine.activeState;   // +0x51

            int staffed = -1, accepting = -1;
            if (Component* wpcBase = FindComp(ent, tidWPC)) {
                WorkPlaceComponent* w = static_cast<WorkPlaceComponent*>(wpcBase);   // exact type id
                staffed   = w->workers._size;                  // +0x60
                accepting = w->isAccepting;                  // +0x50
            }

            std::fprintf(stderr, "[FoodDiag] frame=%d %-19s #%d comp=%p smStateIdx=%d "
                         "staffed=%d accepting=%d",
                         frame, rows[r].name, rows[r].count, static_cast<void*>(comp),
                         stateId, staffed, accepting);
            if (std::strcmp(rows[r].name, "FishingComponent") == 0) {
                // The row's type id is FishingComponent's.
                FishingComponent* f = static_cast<FishingComponent*>(comp);
                std::fprintf(stderr, " registered(+0x50)=%u rate(+0x54)=%.3f",
                             (unsigned)f->fishingFlag, f->fishingRate);
            }
            // Gatherer's Hut and Hunter Lodge both search their RADIUS for something
            // that already exists in the world: HerbalistWork::Slot4 walks random cells
            // in radius looking for a NaturalResource entity (cell+8) whose description
            // is in the hut's herb list and whose growth clears the threshold; the hunt
            // work looks for game. A building on cleared ground therefore executes its
            // work, finds nothing, and produces nothing — which is indistinguishable
            // from a broken port unless the resource count in range is reported.
            if (std::strcmp(rows[r].name, "HerbalistComponent") == 0 ||
                std::strcmp(rows[r].name, "HuntComponent") == 0) {
                int resInRange = -1;
                GameUpdateLoop* gwp = (GameUpdateLoop::_singleton != nullptr)
                          ? GameUpdateLoop::_singleton : nullptr;
                Map* mapObj = gwp ? gwp->m_game : nullptr;           // +0xf8
                MapCell* grid = mapObj ? mapObj->cells.GetData() : nullptr;   // +0x80 (stride 0x30)
                const int mw = mapObj ? mapObj->size : 0;           // +0x00
                // Radius lives on the RadiusDescription in the entity's description
                // list (entity+0x28), at +0x28 — the same read the work does.
                int radius = 0;
                const uint32_t tidRadius = RadiusDescription::TypeId();
                ComponentDescription* dlist = ent->_description;    // +0x28
                if (dlist && tidRadius) {
                    const int dn = dlist->_subDescCount;            // +0x30
                    Description** da = dlist->_subDescs;            // +0x28
                    for (int d = 0; d < dn && da; ++d)
                        if (da[d] && Object::IsDerivedFromNullSafe(da[d], tidRadius)) {
                            radius = static_cast<RadiusDescription*>(da[d])->_radius;   // +0x28
                            break;
                        }
                }
                if (grid && mw > 0 && radius > 0) {
                    // Building tile centre, as the works compute it.
                    const uint32_t tidMap = MapComponent::TypeId();
                    Component* mcBase = FindComp(ent, tidMap);
                    if (mcBase) {
                        MapComponent* m = static_cast<MapComponent*>(mcBase);   // exact type id
                        const int cx = (m->tileX1 >> 1) + m->tileX0;        // +0x2c, +0x28
                        const int cy = (m->tileY1 >> 1) + m->tileY0;        // +0x2e, +0x2a
                        resInRange = 0;
                        for (int dy = -radius; dy < radius; ++dy)
                            for (int dx = -radius; dx < radius; ++dx) {
                                if (dx * dx + dy * dy > radius * radius) continue;
                                const int x = cx + dx, y = cy + dy;
                                if (x < 0 || y < 0 || x >= mw || y >= mw) continue;
                                if (grid[y * mw + x].occupant != nullptr)   // cell+0x08
                                    ++resInRange;
                            }
                    }
                }
                std::fprintf(stderr, " radius=%d resourceCellsInRange=%d", radius, resInRange);

                // resourceCellsInRange counts ANY entity parked in a cell — overwhelmingly
                // TREES. HerbalistWork::Slot4 only accepts a resource whose entity+0x28
                // matches an entry in the hut's own accept list (HerbalistDescription+0x20,
                // count +0x28), so the all-resources number says nothing about whether the
                // hut can forage. Count the MATCHING ones — that is the number the work
                // actually gates on, and the one worth siting the building by.
                if (std::strcmp(rows[r].name, "HerbalistComponent") == 0 &&
                    grid && mw > 0 && radius > 0) {
                    // Retail resolves the list off the HERBALIST component's own
                    // description (comp+0x18), NOT the WorkPlaceComponent's — the work's
                    // finder uses type id 0x1401c43a0, which is HerbalistComponent despite
                    // the port naming its local `s_typeId_WorkplaceComponent`. `comp` here
                    // is already that component.
                    const uint32_t tidHerbDesc = HerbalistDescription::TypeId();
                    HerbalistDescription* hdesc = nullptr;
                    {
                        Description* d = comp->_description;               // +0x18
                        if (d && tidHerbDesc &&
                            Object::IsDerivedFromNullSafe(d, tidHerbDesc))
                            hdesc = static_cast<HerbalistDescription*>(d);
                    }
                    int accepted = -1, listN = 0;
                    if (hdesc) {
                        listN = hdesc->_herbCount;                         // +0x28
                        ComponentDescription** list = hdesc->_herbs;       // +0x20
                        const uint32_t tidMap2 = MapComponent::TypeId();
                        Component* mc2 = FindComp(ent, tidMap2);
                        if (list && listN > 0 && mc2) {
                            MapComponent* m2 = static_cast<MapComponent*>(mc2);   // exact type id
                            const int cx = (m2->tileX1 >> 1) + m2->tileX0;   // +0x2c, +0x28
                            const int cy = (m2->tileY1 >> 1) + m2->tileY0;   // +0x2e, +0x2a
                            accepted = 0;
                            for (int dy = -radius; dy < radius; ++dy)
                                for (int dx = -radius; dx < radius; ++dx) {
                                    if (dx * dx + dy * dy > radius * radius) continue;
                                    const int x = cx + dx, y = cy + dy;
                                    if (x < 0 || y < 0 || x >= mw || y >= mw) continue;
                                    ComponentEntity* re = grid[y * mw + x].occupant.Get();   // cell+0x08
                                    if (!re) continue;
                                    const Object::External* rd = re->_description;    // +0x28
                                    for (int t = 0; t < listN; ++t)
                                        if (list[t] == rd) { ++accepted; break; }
                                }
                        }
                    }
                    std::fprintf(stderr, " acceptList=%d GATHERABLE=%d", listN, accepted);
                }
            }
            if (std::strcmp(rows[r].name, "HuntComponent") == 0) {
                // The row's type id is HuntComponent's.
                HuntComponent* h = static_cast<HuntComponent*>(comp);
                std::fprintf(stderr, " progress(+0x50)=%lld",
                             (long long)h->huntProgress._ticks);

            }
            std::fprintf(stderr, "\n");
        }
    }

    // Report the empty classes too — "no line" must never read as "fine".
    for (int r = 0; r < nRows; ++r)
        if (rows[r].count == 0)
            std::fprintf(stderr, "[FoodDiag] frame=%d %-19s 0 instance(s)%s\n",
                         frame, rows[r].name,
                         rows[r].tid ? "" : "  (TYPE ID UNRESOLVED)");

    DumpStorage(frame, arr, n);
    std::fflush(stderr);
}

}  // namespace

extern "C" void FoodDiag_Tick(int frameNum) {
    Init();
    if (s_period <= 0) return;
    if (frameNum <= 0 || (frameNum % s_period) != 0) return;
    DumpAll(frameNum);
}
