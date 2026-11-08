// src/game/Component/citizen_employ_cascade.cpp
//
// Brick M-C — the ProfessionUI worker-count SEAT cascade + the town citizen-list
// registrar. Faithful 1:1 ports of:
//   0x140114000  CitizenComponent_OnAdd          — townMgr+0xa0 citizen-list ADD
//   0x1400896a0  FUN_1400896a0                   — Fisher-Yates shuffle of the house list
//   0x14008cbf0  CitizenComponent_impl_sub_cbf0  — strategy 1 (distance-optimized relocate)
//   0x14008ca20  CitizenComponent_impl_sub_ca20  — strategy 2 (nearest employed laborer)
//   0x14008c5f0  CitizenComponent_impl_c5f0      — strategy 3 (most-laborer house)
//   0x14008c4b0  CitizenComponent_impl_c4b0      — strategy 4 (first idle laborer)
// plus a headless driver CitizenEmploy_SeatWorkers that mirrors the per-worker inner
// loop of CitizenComponent_impl_dd10 (0x14007dd10) — dd10 reads a NumberEditElement UI
// widget we don't have, so we drive the FindAvailableBuilding + cbf0->ca20->c5f0->c4b0
// fallback directly (as if the player set the worker count).
//
// CORRECTED field semantics (reference_employ_cascade_field_semantics): each strategy
// seats a worker by setting citizen+0x1f0 = professionKey and calling AssignHousing
// (0x140111ee0), which sets citizen+0x248 (the WorkPlaceComponent-roster / workplace
// link) — NOT +0x238 (that is the c7cf0 residence/household link, set by AssignWorkplace).
//
// Deps already ported+registered elsewhere:
//   FindAvailableBuilding 0x14008d100  (town_workplace_registry.cpp)
//   CitizenComp_AssignHousing 0x140111ee0  (citizencomponent.cpp) -> +0x248
//   CitizenComp_Entity_FindComponent_Storage 0x140008090  (citizenaicomponent.cpp) -> c7cf0
//   Entity_impl_7fb0 0x140087fb0, CalcDistance 0x140008830, arrayAdd 0x140003110

#include "Component/ResidenceComponent.h"   // B.3.8 CB1a (TypeId)
#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"   // adultAge (+0x44)
#include "Entity/CitizenBehavior.h"         // the town manager's arrays (+0x20, +0xA0)
#include "Entity/ComponentEntity.h"         // _components / FindComponent<T>
#include "Game/ProfessionList.h"            // laborerProfession (the laborer profession)
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"   // Pf_EntityAnchorCell_7fb0 (0x140087fb0)
#include "Work/exe_bodies.h"     // WorkBase_CalcDistance_8830 (0x140008830)
#include "Game/exe_bodies.h"     // FindAvailableBuilding (0x14008d100)
#include "Game/Map.h"           // Map::CalcDistance (0x140008830)

// Deps ported elsewhere, called by name through their headers:
//   0x140087fb0 Pf_EntityAnchorCell_7fb0 — an entity IBP's anchor cell (used for distance).
//   0x140008830 WorkBase_CalcDistance_8830 — distance between two cells (game = *(gw+0xf8)).
//   0x1401163d0 CitizenComponent_AssignWorkplace(self, workplaceIBP, slotIndex).
//     Passing &nullIBP,-1 detaches the citizen from any workplace worker list and clears
//     citizen+0x238 (workplace) / +0x240 (slot) — the "become an unassigned laborer" clear.
//   0x140003110 was Collection::Array<void*>::SetSize(n, fill) monomorphised
//   into the exe (ENDGAME B.3.2 step 2) — converted at its call sites below.

namespace {

uint32_t          s_tidResidence = 0;   // ResidenceComponent (c7cf0) — self-resolved
bool              s_init         = false;

// Resolves no door: it only caches the ResidenceComponent type id.
void EnsureFns() {
    if (s_init) return;
    if (s_tidResidence == 0) s_tidResidence = ResidenceComponent::TypeId();
    s_init = true;
}

// The default (laborer) profession: *(gw+0xb0)+0x30, re-read at every use.
inline Profession* LaborerKey()  { return GameUpdateLoop::_singleton->m_professionList->laborerProfession; }

// citizen adult (desc+0x44 <= cz+0x3c) && idle (cz+0x208 == 0). No CheckPtr (caller
// already CheckPtr'd; the exe reads these two fields before the mid-check CheckPtr).
inline bool AdultIdle(CitizenComponent* cz) {
    return (cz->description->adultAge <= cz->age)
        && (cz->school == nullptr);
}

// Find the ResidenceComponent (c7cf0) on an entity — mirrors the exe's inline scan
// (stop at first GetTypeId match; return it only if IsDerivedFrom too, else null).
ResidenceComponent* FindResidenceComp(ComponentEntity* entity) {
    int i = 0;
    Object::InternalPtr<Component>* arr = entity->_components._data;   // +0x30
    int cnt = entity->_components._size;                            // +0x38
    while (i < cnt) {
        Component* comp = arr[i].Get();
        uint32_t tid = comp->Type();   // slot 2
        if (tid == s_tidResidence) {
            Component* compVal = arr[i].Get();
            return Object::IsDerivedFromNullSafe(compVal, s_tidResidence)
                       ? static_cast<ResidenceComponent*>(compVal) : nullptr;
        }
        ++i;
        cnt = entity->_components._size;   // faithful re-read
    }
    return nullptr;
}

// Seat epilogue shared by all four strategies: if the citizen isn't already this
// profession at this building, AssignHousing(citizen,&building) (-> +0x248) and set
// +0x1f0 = profKey. `avail` = the workplace building IBP value.
inline void SeatWorker(CitizenComponent* citizen, ComponentEntity* avail, Profession* profKey) {
    ComponentEntity* bldg = avail;
    if ((citizen->profession != profKey) ||
        (citizen->workplace != bldg)) {
        citizen->SetWorkplace(bldg);   // 0x140111ee0
        citizen->profession = profKey;
    }
}

// ============================================================================
// 0x1400896a0 — Fisher-Yates shuffle of the town house list (townMgr+0x20 array,
// townMgr+0x28 count). Uses System::Random::Rand(*(gw+0xd8), n).
// ============================================================================
void Shuffle896a0(CitizenBehavior* townMgr) {
    uint32_t n = townMgr->residences._size;
    System::Random* rng = GameUpdateLoop::_singleton->m_random;   // +0xd8
    if ((int)n <= 1) return;
    for (int64_t i = 0; (int64_t)i < (int)n; ++i) {
        uint32_t j = rng->Rand(static_cast<uint32_t>(n));
        if ((uint32_t)i != j) {
            Object::InternalPtr<ComponentEntity>* base = townMgr->residences._data;
            ComponentEntity* tmp = base[i].Get();
            base[i] = base[(int)j];
            base[(int)j] = tmp;
        }
    }
}

// ============================================================================
// 0x14008c4b0 — strategy 4: first adult+idle+laborer in townMgr+0xa0 -> seat.
// ============================================================================
CitizenComponent** Strat_c4b0(CitizenBehavior* townMgr, CitizenComponent** out,
                              ComponentEntity** availPtr, Profession* profKey) {
    int cnt = townMgr->citizens._size;
    for (int i = 0; i < cnt; ++i) {
        CitizenComponent* cz = townMgr->citizens._data[i].Get();
        if (AdultIdle(cz)) {
            if (cz->profession == LaborerKey()) {
                SeatWorker(cz, *availPtr, profKey);
                *out = cz;
                return out;
            }
        }
    }
    *out = nullptr;
    return out;
}

// ============================================================================
// 0x14008ca20 — strategy 2: nearest (to avail) adult+idle laborer that already HAS
// a residence (+0x238) -> seat.
// ============================================================================
CitizenComponent** Strat_ca20(CitizenBehavior* townMgr, CitizenComponent** out,
                              ComponentEntity** availPtr, Profession* profKey) {
    Map* game = GameUpdateLoop::_singleton->m_game;
    ComponentEntity* availAnchorArg = *availPtr;
    uint32_t availCell = (uint32_t)EntityAnchorCell(&availAnchorArg, nullptr);   // 0x140087fb0
    int bestDist = 0x7ffffff;
    CitizenComponent* best = nullptr;
    int cnt = townMgr->citizens._size;
    for (int i = 0; i < cnt; ++i) {
        CitizenComponent* cz = townMgr->citizens._data[i].Get();
        if (AdultIdle(cz)) {
            if (cz->profession == LaborerKey()) {
                if (cz->home != nullptr) {
                    ComponentEntity* resAnchor = cz->home.Get();   // +0x238
                    uint32_t czCell = (uint32_t)EntityAnchorCell(&resAnchor, nullptr);
                    int d = game->CalcDistance(czCell, availCell);
                    if (d <= bestDist) { best = cz; bestDist = d; }
                }
            }
        }
    }
    if (best != nullptr) {
        SeatWorker(best, *availPtr, profKey);
        *out = best;
        return out;
    }
    *out = nullptr;
    return out;
}

// ============================================================================
// 0x14008cbf0 — strategy 1: distance-optimized relocate. Scan townMgr+0xa0 for a
// citizen already of THIS profession with a residence (+0x238) whose c7cf0 roster
// holds an idle laborer; pick the nearest such donor; seat one of its laborers.
// (If *availPtr==0 seats immediately during the scan.)
// ============================================================================
CitizenComponent** Strat_cbf0(CitizenBehavior* townMgr, CitizenComponent** out,
                              ComponentEntity** availPtr, Profession* profKey) {
    Map* game = GameUpdateLoop::_singleton->m_game;
    int bestRecordIdx = -1;               // local_74
    uint32_t availCell = 0xffffffff;      // local_78
    if (*availPtr != nullptr) {
        ComponentEntity* availAnchorArg = *availPtr;
        availCell = (uint32_t)EntityAnchorCell(&availAnchorArg, nullptr);   // 0x140087fb0
    }
    int bestDist = 0x7ffffff;             // iVar14
    ComponentEntity* bestRes = nullptr;   // local_70

    int cnt = townMgr->citizens._size;
    for (int i = 0; i < cnt; ++i) {       // local_60 = i * 8
        CitizenComponent* cz = townMgr->citizens._data[i].Get();   // local_80
        if (!AdultIdle(cz)) continue;
        if (cz->profession != profKey) continue;

        ComponentEntity* res = cz->home.Get();  // local_90 = their residence (+0x238)
        if (res == nullptr) continue;
        ComponentEntity* resAnchor = res;                        // local_68
        uint32_t resCell = (uint32_t)EntityAnchorCell(&resAnchor, nullptr);
        int d = bestDist;
        if ((int)availCell >= 0) {
            d = game->CalcDistance(resCell, availCell);
            if (d > bestDist) continue;
        }

        // walk res's c7cf0 roster for an idle laborer. Entity_FindComponent_Storage
        // (0x140008090) is the ResidenceComponent type scan.
        ResidenceComponent* c7cf0 = res->FindComponent<ResidenceComponent>();   // local_98 (IBP)
        int recCount = c7cf0->residentSlots._size;                  // +0x58
        ResidenceComponent::Family* recBase = nullptr;
        for (int r = 0; r < recCount; ++r) {
            recBase = c7cf0->residentSlots._data;                       // +0x50
            int slotCount = recBase[r]._size;
            bool matched = false;
            Object::InternalPtr<CitizenComponent>* memberData = recBase[r]._data;
            for (int m = 0; m < slotCount; ++m) {
                recBase = c7cf0->residentSlots._data;
                memberData = recBase[r]._data;
                CitizenComponent* member = memberData[m].Get();
                if (AdultIdle(member)) {
                    if (member->profession == LaborerKey()) {
                        if (*availPtr == nullptr) {
                            SeatWorker(member, *availPtr, profKey);
                            *out = member;
                            return out;
                        }
                        bestRes = res; bestDist = d; bestRecordIdx = r;
                        matched = true;
                        break;
                    }
                }
                slotCount = c7cf0->residentSlots[r]._size;
            }
            (void)matched;
            recCount = c7cf0->residentSlots._size;
        }
    }

    // Post-scan: seat from the best donor residence's record bestRecordIdx.
    if (bestRes != nullptr) {
        ResidenceComponent* c7cf0 = FindResidenceComp(bestRes);
        ResidenceComponent::Family* recAddr = &c7cf0->residentSlots[bestRecordIdx];
        int slotCount = recAddr->_size;
        for (int m = 0; m < slotCount; ++m) {
            Object::InternalPtr<CitizenComponent>* memberData = c7cf0->residentSlots[bestRecordIdx]._data;
            CitizenComponent* member = memberData[m].Get();
            if (member->profession == LaborerKey()) {
                if (AdultIdle(member)) {
                    SeatWorker(member, *availPtr, profKey);
                    *out = member;
                    return out;
                }
            }
            slotCount = c7cf0->residentSlots[bestRecordIdx]._size;
        }
    }

    *out = nullptr;
    return out;
}

// ============================================================================
// 0x14008c5f0 — strategy 3: shuffle houses, pick the house whose c7cf0 roster record
// has the most idle laborers, then seat one from that record.
//
// A NULL `*availPtr` here is FAITHFUL — verified against retail 2026-08-15, do not
// "fix" it with a guard. Retail's cascade 0x14008c3d0 guards exactly one of the four
// strategies: `if (avail != 0)` wraps ca20 ALONE. ca20 needs the guard because it
// DEREFS avail on entry (Entity_impl_7fb0(&avail) to get its position for the
// nearest-residence sort); c5f0 and c4b0 only ever STORE the pointer
// (AssignHousing(cz,&avail); cz+0x1f0 = profKey), so retail lets a null through on
// purpose. The asymmetry in one 20-line function is the proof of intent.
//
// Consequence, also faithful: when FindAvailableBuilding returns null the citizen's
// profession still flips with no roster attach, so WPC+0x60 does not move and the
// panel redraws the same "N / M" (trace signature `via=idle roster 0->0`). That is a
// symptom of an upstream registration miss — the workplace absent from the
// townMgr+0x100 profession BST, roster >= capacity, or WPC+0x50 == 0 — NOT of a
// missing null check here.
// ============================================================================
CitizenComponent** Strat_c5f0(CitizenBehavior* townMgr, CitizenComponent** out,
                              ComponentEntity** availPtr, Profession* profKey) {
    ComponentEntity* bestHouse = nullptr;   // local_58
    int bestCount = 0;       // iVar9
    int bestRecordIdx = -1;  // iVar13

    Shuffle896a0(townMgr);

    int houseCnt = townMgr->residences._size;
    for (int h = 0; h < houseCnt; ++h) {   // local_50[0] = h * 8
        ComponentEntity* house = townMgr->residences._data[h].Get();   // local_60
        ResidenceComponent* c7cf0 = FindResidenceComp(house);   // local_70
        int recCount = c7cf0->residentSlots._size;
        for (int r = 0; r < recCount; ++r) {
            int laborerN = 0;   // iVar6
            int slotCount = c7cf0->residentSlots[r]._size;
            for (int m = 0; m < slotCount; ++m) {
                Object::InternalPtr<CitizenComponent>* memberData = c7cf0->residentSlots[r]._data;
                CitizenComponent* member = memberData[m].Get();
                if (AdultIdle(member)) {
                    if (member->profession == LaborerKey()) laborerN++;
                }
                slotCount = c7cf0->residentSlots[r]._size;
            }
            if (bestCount < laborerN) { bestHouse = house; bestCount = laborerN; bestRecordIdx = r; }
            recCount = c7cf0->residentSlots._size;
        }
    }

    if (bestHouse != nullptr) {
        ResidenceComponent* c7cf0 = FindResidenceComp(bestHouse);
        int slotCount = c7cf0->residentSlots[bestRecordIdx]._size;
        for (int m = 0; m < slotCount; ++m) {
            Object::InternalPtr<CitizenComponent>* memberData = c7cf0->residentSlots[bestRecordIdx]._data;
            CitizenComponent* member = memberData[m].Get();
            if (AdultIdle(member)) {
                if (member->profession == LaborerKey()) {
                    SeatWorker(member, *availPtr, profKey);
                    *out = member;
                    return out;
                }
            }
            slotCount = c7cf0->residentSlots[bestRecordIdx]._size;
        }
    }

    *out = nullptr;
    return out;
}

} // namespace

// ============================================================================
// 0x140114000 — CitizenComponent_OnAdd: append the citizen to townMgr+0xa0.
// ============================================================================

// ============================================================================
// Headless driver — mirrors CitizenComponent_impl_dd10's per-worker inner loop.
// Seats up to `count` workers of profession `profKey`; returns the number actually
// seated. (dd10 derives profKey from a UI profession index via *(*(gw+0xb0)+0x20 +
// idx*8); here the caller already holds the key — e.g. g_foresterProfKey from the
// lodge's WorkPlaceDescription — so we take it directly.) Faithful fallback order:
// FindAvailableBuilding then cbf0 -> (if avail!=0) ca20 -> c5f0 -> c4b0.
// ============================================================================
extern "C" int CitizenEmploy_SeatWorkers(CitizenBehavior* town, Profession* profKey, int count) {
    EnsureFns();
    if (town == nullptr || GameUpdateLoop::_singleton == nullptr) return 0;
    if (profKey == nullptr) return 0;

    int seated = 0;
    for (int i = 0; i < count; ++i) {
        ComponentEntity* availOut[2] = { nullptr, nullptr };
        town->FindAvailableBuilding(availOut, profKey);   // 0x14008d100
        ComponentEntity* avail = availOut[0];

        CitizenComponent* result[2];
        ComponentEntity* availArg;

        result[0] = nullptr; availArg = avail;
        Strat_cbf0(town, result, &availArg, profKey);
        CitizenComponent* seatedCz = result[0];
        if (seatedCz == nullptr) {
            if (avail != nullptr) {
                result[0] = nullptr; availArg = avail;
                Strat_ca20(town, result, &availArg, profKey);
                seatedCz = result[0];
            }
            if (seatedCz == nullptr) {
                result[0] = nullptr; availArg = avail;
                Strat_c5f0(town, result, &availArg, profKey);
                seatedCz = result[0];
                if (seatedCz == nullptr) {
                    result[0] = nullptr; availArg = avail;
                    Strat_c4b0(town, result, &availArg, profKey);
                    seatedCz = result[0];
                }
            }
        }

        if (seatedCz != nullptr) {
            ++seated;
        } else {
            break;   // no candidate this round -> stop
        }
    }
    return seated;
}

// ============================================================================
// Registerable exports of the four seat strategies. The exe's ONLY dispatcher of
// these addresses is TownHallUI's profession spinner (CitizenComponent_impl_dd10,
// 0x14007dd10 -> fn_CitizenList_FindIdle/FindWorking/FindStudent/FindAny). Now that
// the town-hall panel is promoted in standalone, that path is live, so expose the
// file-static strategies for RegisterExeAddr (standalone/reg_townhallui.cpp).
// ============================================================================
// The C signatures stay for their outside callers (professionui.cpp, townhallui.cpp,
// workplaceui.cpp); each argument is typed once here.
CitizenComponent** CitizenBehavior::FindWorkerToRelocate(CitizenComponent** out,
                                                       ComponentEntity** availPtr, Profession* profKey) {
    EnsureFns();
    return Strat_cbf0(this, out, availPtr, profKey);
}
CitizenComponent** CitizenBehavior::FindNearestHousedLaborer(CitizenComponent** out,
                                                       ComponentEntity** availPtr, Profession* profKey) {
    EnsureFns();
    return Strat_ca20(this, out, availPtr, profKey);
}
CitizenComponent** CitizenBehavior::FindLaborerFromFullestHouse(CitizenComponent** out,
                                                       ComponentEntity** availPtr, Profession* profKey) {
    EnsureFns();
    return Strat_c5f0(this, out, availPtr, profKey);
}
CitizenComponent** CitizenBehavior::FindIdleLaborer(CitizenComponent** out,
                                                       ComponentEntity** availPtr, Profession* profKey) {
    EnsureFns();
    return Strat_c4b0(this, out, availPtr, profKey);
}

// CitizenList::AssignWorker (0x14008c3d0) — the top-level seat cascade the exe's
// worker-add UI calls (fn_CitizenList_AssignWorker, workplaceui.cpp id 0x1450).
// Tries the four strategies in order on a FRESH copy of *availPtr each time:
// cbf0 (distance-optimized relocate) -> ca20 (nearest employed laborer, skipped if
// *availPtr is 0) -> c5f0 (most-laborer house) -> c4b0 (first idle laborer, writes
// `out` itself). First nonzero winner's 8-byte result is copied into out+0x0 only,
// exactly like the original (out+0x8 is left untouched on those paths).
// Defined below (declared in Component/exe_bodies.h); AddWorker (0x140091130) calls it.

// ============================================================================
// CitizenList::AddWorker (0x140091130) — the public one-call seat: find a building
// with an open slot for `professionKey`, then run the four-strategy cascade against
// it. Retail is literally two calls (the result is discarded; the seat happens as a
// side effect inside the strategies):
//
//     FUN_14008d100(townMgr, &avail, profKey);        // FindAvailableBuilding
//     local_res20 = avail;                            // fresh copy — c3d0 mutates it
//     CitizenComponent_impl_c3d0(townMgr, out, &local_res20, profKey);
//
// The copy matters: c3d0 takes `availPtr` by pointer and each strategy anchors
// through it, so the caller's value must not be handed over directly.
//
// No roster-only hold, as in retail (review F-088). Its one caller,
// src/diag/builder_seat_harness.cpp, takes the hold around its own call instead.
void CitizenBehavior::AddWorker(Profession* profession) {
    // Row 1141: no test of the profession (0x14009113a passes RDX straight to
    // 0x14008d100 as R8, and 0x14009114f reloads it for 0x14008c3d0's R9).
    EnsureFns();

    ComponentEntity* avail[2] = { nullptr, nullptr };
    FindAvailableBuilding(avail, profession);   // 0x14008d100

    ComponentEntity* availArg = avail[0];        // fresh copy — c3d0 anchors through it
    CitizenComponent* out[2] = { nullptr, nullptr };   // local_18[16] in the original
    AssignWorker(out, &availArg, profession);
}

CitizenComponent** CitizenBehavior::AssignWorker(CitizenComponent** out,
                                                           ComponentEntity** availPtr, Profession* profKey) {
    EnsureFns();
    alignas(8) CitizenComponent* local[2] = { nullptr, nullptr };
    ComponentEntity* avail = *availPtr;
    CitizenComponent** r = Strat_cbf0(this, local, &avail, profKey);
    CitizenComponent* won = *r;
    if (won == nullptr) {
        avail = *availPtr;
        if (avail != nullptr) {
            r = Strat_ca20(this, local, &avail, profKey);
            won = *r;
        }
        if (won == nullptr) {
            avail = *availPtr;
            r = Strat_c5f0(this, local, &avail, profKey);
            won = *r;
            if (won == nullptr) {
                avail = *availPtr;
                Strat_c4b0(this, out, &avail, profKey);
                return out;
            }
        }
    }
    *out = won;
    return out;
}

// [6] 0x140114000 — the ADDED-to-world broadcast (ComponentEntity_OnAddedPass,
// flags bit 0): append to the town citizen list townMgr+0xa0, the list the
// census, the town-hall stats and every population system read.
void CitizenComponent::OnAdded() {
    EnsureFns();
    if (GameUpdateLoop::_singleton == nullptr) return;
    CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;
    if (townMgr == nullptr) return;
    CitizenComponent* ref[2] = { nullptr, nullptr };
    ref[0] = this;   // the InternalBasePtr ctor's one store
    townMgr->citizens
        .SetSizeFill(townMgr->citizens._size + 1, ref);
}
