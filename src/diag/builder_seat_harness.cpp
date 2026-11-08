// src/diag/builder_seat_harness.cpp
//
// BANISHED_BUILDERS=<n>[,<settleFrame>] — seat N citizens as BUILDERS, the way the
// town-hall Professions spinner does.
//
// WHY THIS EXISTS
// Road construction never happens in an unattended town, and the reason is not the
// road code. RoadTool's commit posts one BuildRoad work item per tile into the work
// scheduler (gw+0x118), and the scheduler files each item into a per-PROFESSION
// bucket chosen by the work's vtable slot +0x50. BuildRoad's slot 10 (0x1400e4960)
// returns *(*(gw+0xb0)+0x38) — the BUILDER key. A citizen's idle work search
// (CitizenAI_State_Idle -> 0x1400f7830) only ever looks in the bucket keyed by its
// OWN profession (citizen+0x1f0). So with no builder in town the items sit there
// forever: measured with BANISHED_ROAD_CENSUS, 620 tiles queued, 620 items in the
// (BUILDER) bucket, 0 ever taken.
//
// Builders are a player-set profession: retail seats them from the town-hall
// Professions spinner, which loops CitizenList::AddWorker (0x140091130) once per
// requested worker. That call is FindAvailableBuilding followed by the four-strategy
// AssignWorker cascade, and retail deliberately lets a NULL "available building"
// through it — c5f0/c4b0 only ever STORE the pointer, so the citizen's profession
// flips with no roster attach (see the note on Strat_c5f0 in
// src/game/Component/citizen_employ_cascade.cpp). That is what makes a builder
// possible in a town whose only pending work is a road: there is no building to
// attach to, and retail does not require one.
//
// So this harness is the same class of stand-in as src/diag/panel_staffing_driver.cpp
// — it presses a widget the unattended build has no player to press. The seat itself
// is retail's (0x140091130, already registered by reg_townhallui.cpp); only the
// pressing is ours.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/ProfessionList.h"          // B4: m_professionList
#include "Entity/CitizenBehavior.h"
#include "Component/CitizenComponent.h"
#include "Entity/ComponentEntity.h"   // B4: the component array by name
#include "Entity/EntityList.h"        // B4: GameUpdateLoop::m_entityList
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// workplacecomponent.cpp: the roster-only mode of WorkPlaceComponent::Attach / Detach.
extern bool g_wpcRosterOnly;

namespace {

int  s_want   = -2;    // -2 = env unread, -1 = disabled
int  s_settle = 300;
bool s_fired  = false;

void Init() {
    if (s_want != -2) return;
    s_want = -1;
    const char* e = Opt().builders;
    if (!e || !e[0]) return;
    int n = HarnessAtoi(e);
    if (n <= 0) return;
    s_want = n;
    if (const char* comma = std::strchr(e, ',')) {
        int f = HarnessAtoi(comma + 1);
        if (f > 0) s_settle = f;
    }
}

// Count citizens currently holding `key` as their profession — the same walk the
// Professions panel's "N" column does (townMgr+0xa0 array / +0xa8 count, profession
// at citizen+0x1f0).
int CountProfession(CitizenBehavior* townMgr, const Profession* key) {
    Object::InternalPtr<CitizenComponent>* arr = townMgr->citizens._data;   // +0xa0
    int    n   = townMgr->citizens._size;           // +0xa8
    if (!arr || n <= 0) return 0;
    int hits = 0;
    for (int i = 0; i < n; ++i) {
        CitizenComponent* c = arr[i].Get();
        if (!c) continue;
        if (c->profession == key) ++hits;         // +0x1f0
    }
    return hits;
}

}  // namespace

extern "C" void BuilderSeat_Tick(int frameNum) {
    Init();
    if (s_want <= 0 || s_fired) return;
    if (frameNum < s_settle) return;

    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    if (!gw) return;
    CitizenBehavior* townMgr = gw->m_citizenList;     // +0xf0
    ProfessionList*  profMgr = gw->m_professionList;  // +0xb0
    if (!townMgr || !profMgr) return;

    s_fired = true;
    Profession* const builder = profMgr->builderProfession;  // +0x38
    const uint64_t builderKey = reinterpret_cast<uintptr_t>(builder);   // the log line prints the pointer as an integer (%016llx)  // CAST-OK: ptr_int
    const int before = CountProfession(townMgr, builder);

    // The roster-only hold this harness was measured with (it lived inside AddWorker until
    // review F-088 moved it here, out of the retail body): the workplace Attach / Detach
    // skip the status-icon and model toggles while the seats are pressed.
    const bool prevRosterOnly = g_wpcRosterOnly;
    g_wpcRosterOnly = true;
    for (int i = 0; i < s_want; ++i)
        townMgr->AddWorker(builder);
    g_wpcRosterOnly = prevRosterOnly;

    const int after = CountProfession(townMgr, builder);

    // Correlation for the ValidateTask crash (BANISHED_WORKVALIDATE_TRACE): list the
    // citizens now holding the builder key, so a later [WorkValidate] BAD VTABLE obj=
    // can be matched against the set this harness actually converted.
    if (const char* t = Opt().workValidateTrace) if (t[0] && t[0] != '0') {
        Object::InternalPtr<CitizenComponent>* carr = townMgr->citizens._data;   // +0xa0
        int    cn   = townMgr->citizens._size;            // +0xa8
        std::fprintf(stderr, "[BuilderSeat] converted citizens (roster %d):", cn);
        for (int i = 0; i < cn; ++i) {
            CitizenComponent* c = carr[i].Get();
            if (!c) continue;
            if (c->profession == builder)               // +0x1f0
                std::fprintf(stderr, " %p", (void*)c);
        }
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
    }
    std::fprintf(stderr, "[BuilderSeat] f=%d requested %d builders (key=%016llx): "
                         "profession count %d -> %d\n",
                 frameNum, s_want, (unsigned long long)builderKey, before, after);
    std::fflush(stderr);
}
