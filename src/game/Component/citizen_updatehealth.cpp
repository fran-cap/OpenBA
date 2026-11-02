// src/game/Component/citizen_updatehealth.cpp
// CitizenComponent::UpdateHealth (0x1401127d0) + the complete disease subsystem it
// closes over. This was the last unported branch of the step-2.5 citizen lifecycle
// backlog ("deep closure" defer) — the closure turns out to be BOUNDED: 7 unported
// callees, everything below them already registered.
//
// Ported functions (all disasm-verified 2026-07-19):
//   0x1401127d0 UpdateHealth        — healthy: immunity tick + catch-disease roll;
//                                     sick: duration/spread-tick timers, recover-or-die
//   0x1401123b0 RecoverOrDie        — death roll (1/disease+0x40)/herbMult vs rand;
//                                     survive → clear disease + immunity from desc+0x9c
//   0x140112470 SpreadDisease       — contagion roll (disease+0x34), radius query
//                                     (disease+0x38) for citizens, infect 1st healthy
//   0x140112620 CatchDisease        — weighted pick (weight disease+0x3c) over the
//                                     disease ExternalList (gw+0xa8), then infect
//   0x140116b90 Infect              — guards, event-log post (type 0xb sev 3 + disease
//                                     name), set +0x68/+0x70/+0x78, FindWorkplace
//   0x1400893c0 AnyCitizenSick      — scan mgr(+0xa0/+0xa8) IBP array for +0x68 != 0
//   0x1400305e0 IntArrayResize      — generic 4-byte-elem grow-array resize+fill
//   0x140092990 Map_RadiusQuery     — quadtree walk (18dd0/18880, registered) collecting
//                                     entity links within radius, optional type-id filter
//
// CitizenComponent fields touched:
//   +0x50 health(float)  +0x60 time-since-roll(Time)  +0x68 Disease*  +0x70 immunity/
//   duration timer(Time) +0x78 spread-tick timer(Time) +0x228 herbalist-treatment link
//   +0x278 model-entity link (->+0x68 ->+0x158 pos)   +0x288 CitizenDescription
// CitizenDescription: +0x70 maxHealth, +0x7c/0x80..0x8c/0x90 sick-chance lerp table,
//   +0x94/+0x98 duration base/spread (months), +0x9c immunity months, +0x128 herb mult
// Disease resource (src/game/Game/disease.cpp): +0x20 StringTable, +0x28 name String,
//   +0x30 spread-tick months, +0x34 contagion, +0x38 radius, +0x3c weight, +0x40 1/death
// GameWorld: +0x88 spatial map  +0xa8 disease ExternalList  +0xd8 Random  +0xf0 town
//   mgr (+0x1a8 health scale, +0xa0/+0xa8 citizen IBP array)  +0x138 event log
// Spatial map: +0x00 root size(float)  +0x10 entity records (stride 0x18: +0 link IBP
//   value, +8 next index — circular)  +0x30 per-node first-index[]  +0x50 per-node count[]
//
// Constants (read_memory-verified): 0x1401721b8=12.0f (months/yr, registered
// reg_component_fruit), 0x140172234=300.0f (secs/month, reg_citizen_needsupdate),
// 0x14017214c=1.0f, DAT_140172174=2.0f, DAT_140172280=-1.0f (inlined bit-exact).

#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/WorkPlaceComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/CitizenComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/CitizenDescription.h"  // B.4 W1 COMP-b: description fields by name
#include "Component/CharacterComponent.h"
#include "Component/CharacterGrid.h"
#include "Component/ModelComponent.h"
#include "Component/AttendanceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"
#include "Action/PathSearch.h"            // CharacterGridWalker, RawArray<T>
#include "Game/EventLog.h"
#include "Game/Profession.h"
#include "Game/Disease.h"                 // B.3.9 EXA: Disease::TypeId()
#include "options.h"
#include "Object/factory_registration.h"   // Object::GetTypeIdByName
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "math/Math.h"
#include "system/String.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "ui/StringTable.h"    // ENDGAME B.3.6 UI callers (K4)
#include <new>
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Game/exe_constants.h"
#include "volume/Volume.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

namespace {


// Exe bodies called by name: CitizenComp_FindCareBuilding 0x140111590,
// AttendanceComponent_ReleaseLink59b0 0x1401159b0 (Entity_impl_59b0), the quadtree
// walker CharacterGridWalker::Init 0x140018dd0 / ::Subdivide 0x140018880 (Action/PathSearch.h:
// the 0x24-byte walker record is CharacterGridWalkerNode), EventLog::AddCitizenEvent 0x140094e00.
// Runtime calls the original makes through exe import slots: V3 ctor3 0x140152a30,
// V3 splat 0x140151320, Dot 0x140151298 (all direct Math::Vector3 calls),
// Box::Intersect 0x140151e80 (RtVolume_Box_Intersect), StringTable::GetText(String&)
// 0x140151718.

// Disease type id. The .rdata global (0x1401c5948) is env-gated / unregistered
// in standalone — resolve the class's own record instead (D5, ENDGAME B.3.9 CG3;
// citizen's own 0x1401c7158 now reads CitizenComponent::TypeId() directly).
static uint32_t s_tidDisease = 0;

static bool s_init = false;

// Resolves no door: it only caches the Disease type id.
static void EnsureInit() {
    if (s_init) return;
    s_init = true;

    s_tidDisease = Disease::TypeId();   // B.3.9 EXA: the class's own record (was by name)
}

static inline void* GameWorld() {
    return GameUpdateLoop::_singleton;
}
// 0x1401721b8 = 12.0f, stored as its bit pattern (exe_constants.h).
static inline float MonthsPerYear() {
    float f;
    const uint32_t bits = kRdata_1401721b8;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
static inline float SecsPerMonth()  { return kRdata_140172234; }   // 0x140172234 = 300.0f

// Time(float seconds) -> int64 ticks, via the Runtime ctor.
static inline int64_t TimeFromSeconds(float secs) {
    System::Time t = { 0 };
    RtSystem_Time_Ctor(&t, secs);
    return t._ticks;
}

// ============================================================
// [Plague] soak instrumentation (DIAG ONLY — no behaviour change)
// ============================================================
// Every line reports an OUTCOME, printed from inside the guard it describes, with
// both arms covered. Default OFF; BANISHED_PLAGUE_TRACE=1 turns it on (the census and
// the CARE lines run on every citizen update, so they never ship on by default).
static bool PlagueTraceOn() {
    static int en = -1;
    if (en < 0) {
        const char* e = Opt().plagueTrace;
        en = (e && e[0] != '\0' && e[0] != '0' && std::strcmp(e, "false") != 0) ? 1 : 0;
    }
    return en == 1;
}

// Catch-roll census. Printed every kCensusPeriod rolls so a soak gets a steady
// pulse whether or not anyone ever gets ill — a silent log would otherwise be
// indistinguishable from "the roll never runs".
static const int32_t kCensusPeriod = 4000;
static int32_t s_rollCalls = 0, s_rollSkippedNoWindow = 0, s_rollFired = 0;
static int32_t s_rollEpidemicBlocked = 0;
static double s_rollChanceSum = 0.0;
static float  s_rollChanceMax = 0.0f;

// Periodic census of the catch roll — the "not fired" arm. Without this a soak in
// which nobody ever falls ill produces an empty log, which reads identically to the
// roll never running at all.
static void PlagueCensusTick() {
    if (!PlagueTraceOn()) return;
    if (s_rollCalls % kCensusPeriod != 0) return;
    fprintf(stderr,
            "[Plague] CENSUS rolls=%d fired=%d noWindow(B<=0)=%d epidemicBlocked=%d "
            "meanChance=%.8f maxChance=%.8f\n",
            s_rollCalls, s_rollFired, s_rollSkippedNoWindow, s_rollEpidemicBlocked,
            (s_rollCalls > s_rollSkippedNoWindow)
                ? s_rollChanceSum / (double)(s_rollCalls - s_rollSkippedNoWindow) : 0.0,
            s_rollChanceMax);
    fflush(stderr);
}

// One-shot tuning report: the numbers the cure arm depends on. Valid on the first
// UpdateHealth call — the CitizenDescription is fully populated by then.
static bool s_bootReported = false;

static void PlagueBootReport(CitizenComponent* citizen) {
    if (!PlagueTraceOn()) return;

    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    CitizenDescription* desc = citizen->description;
    if (!desc) {
        if (!s_bootReported) {
            s_bootReported = true;
            fprintf(stderr, "[Plague] BOOT: citizen+0x288 (CitizenDescription) is NULL — "
                            "no tuning available\n");
            fflush(stderr);
        }
        return;
    }

    Profession* healerProf = desc->_professions[2];
    void* diseaseList = nullptr;
    if (gw) diseaseList = gw->m_diseaseList;

    if (!s_bootReported) {
        s_bootReported = true;
        float careMult = desc->_f128;
        fprintf(stderr,
                "[Plague] BOOT: careMult(desc+0x128)=%.4f%s healerProf(desc+0x30)=%p "
                "maxHealth(+0x70)=%.3f durBase(+0x94)=%.3f durSpread(+0x98)=%.3f "
                "immunityMonths(+0x9c)=%.3f diseaseList(gw+0xa8)=%p tidDisease=%u\n",
                careMult,
                (careMult == 0.0f) ? "  <== ZERO: (1/recipMortality)/0 is INFINITE, every"
                                     " treated citizen dies instantly" : "",
                healerProf, desc->maxHealth, desc->_f94,
                desc->_f98, desc->_f9c,
                diseaseList, s_tidDisease);
        fflush(stderr);
    }

}

} // namespace

// ============================================================
// 0x1400893c0 — any citizen currently diseased?
// ============================================================
// mgr = *(gw+0xf0): IBP array at +0xa0, count at +0xa8. Returns 1 on the first
// entry whose +0x68 (Disease*) is set — the game allows one epidemic at a time.
// The citizen roster is CitizenBehavior +0xa0 / +0xa8 (the null-manager test is the tree's).
extern "C" char CitizenComp_AnyCitizenSick(const CitizenBehavior* mgr) {
    EnsureInit();
    if (!mgr) return 0;
    int count = mgr->citizens._size;
    const Object::InternalPtr<CitizenComponent>* arr = mgr->citizens._data;
    for (int i = 0; i < count; i++) {
        const CitizenComponent* citizen = arr[i].Get();
        if (citizen->disease != nullptr) return 1;
    }
    return 0;
}

// ============================================================
// 0x1400305e0 — generic 4-byte-element grow-array resize + fill
// ============================================================
// arr = {data* @+0, count @+8, capacity @+0xc}. Resize to newCount: triple-capacity
// heuristic on a single-element grow, copy preserved elements, fill new slots with
// *fill. (The original's unrolled copy loop is semantically this.) Its one caller,
// CatchDisease, grows an array of float running totals: the element is a float.
extern "C" void CitizenComp_IntArrayResize(RawArray<float>* arr, int newCount, const float* fill) {
    EnsureInit();
    int oldCount = arr->size;
    if (newCount == oldCount) return;

    float* oldData = arr->data;
    arr->size = newCount;
    if (arr->capacity < newCount) {
        arr->capacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) arr->capacity = newCount * 3;
        arr->data = static_cast<float*>(System::MemoryAllocator::MemAlloc((uint64_t)arr->capacity * 4, 4));
    }
    float* newData = arr->data;

    int start = (oldData == newData) ? oldCount : 0;
    for (int i = start; i < newCount; i++) {
        newData[i] = (i < oldCount) ? oldData[i] : *fill;
    }
    if (oldData != newData && oldData) System::MemoryAllocator::MemFree(oldData);
}

// ============================================================
// 0x140092990 — quadtree radius query
// ============================================================
// Collects entity-link IBP values within `radius` of `pos` into the 8-byte-element
// grow-array outArr {data,count,cap}. typeId != 0 additionally requires the entity to
// own a component whose GetTypeId() == typeId (found component only FILTERS — the
// pushed value is always the entity link). Drives the registered quadtree walker
// 0x140018dd0/0x140018880; walker record = {pos.xyz @+0, ext.xyz @+0xc, nodeId @+0x18},
// stride 0x24, base walker+0x24, top index at walker+0x8. Node box =
// [pos - (2ex,2ey,ez) + (0,0,-1), pos + (2ex,2ey,ez) + (0,0,+1)]  (the (0,0,∓1) z pads
// are the walker's +0x0c/+0x18 vectors, seeded from the init's -1.0f/+1.0f args).
void* CharacterGrid::RadiusQuery(void** out, float* pos, float* radius,
                                       int typeId, RawArray<CharacterComponent*>* arr) const {
    EnsureInit();
    // B6 row 331: no test of any argument -- 0x1400929f0 the walker init on the grid,
    // 0x1400929fa MOVSS XMM12,[RBX] (the radius), 0x140092a02 MOV qword ptr [RDI],0 (*out),
    // 0x140092a39.. [R15] (pos); the tree's null returns are gone.

    // The 0x2430-byte stack walker (retail: at RBP+0x10; top at walker+0x08, node r at
    // walker+0x24 + r*0x24, cell at node+0x18, grid at walker+0x2428 — 0x140092a82,
    // 0x140092b3b, 0x140092ad2), seeded with the -1.0f / +1.0f z pads.
    CharacterGridWalker walker;
    walker.Init(this, -1.0f, 1.0f);   // 0x140018dd0

    float r  = *radius;
    *out = nullptr;   // 0x140092a02, after the radius read
    float r2 = r * r;

    // Query box {min, max} around pos.
    Box qbox = { Math::Vector3(pos[0] - r, pos[1] - r, pos[2] - r),
                 Math::Vector3(pos[0] + r, pos[1] + r, pos[2] + r) };

    while (walker.top >= 0) {
        const CharacterGridWalkerNode& rec = walker.nodes[walker.top];
        int nodeId = rec.cell;
        int nodeCount = cellOccupancy[nodeId];
        if (nodeCount <= 0) { walker.top--; continue; }

        const float* rp = rec.center;   // pos.xyz
        const float* re = rec.extent;   // ext.xyz
        float sx = 2.0f * re[0], sy = 2.0f * re[1], sz = 1.0f * re[2];
        const float* zlo = walker.padLo;   // (0, 0, -1)
        const float* zhi = walker.padHi;   // (0, 0, +1)
        Box nbox = { Math::Vector3(rp[0] - sx + zlo[0], rp[1] - sy + zlo[1], rp[2] - sz + zlo[2]),
                     Math::Vector3(rp[0] + sx + zhi[0], rp[1] + sy + zhi[1], rp[2] + sz + zhi[2]) };
        if (!RtVolume_Box_Intersect(&qbox, &nbox)) { walker.top--; continue; }

        int first = cellHead[nodeId];
        if (first >= 0) {
            const CharacterGridSlot* recs = slots._data;
            int idx = first;
            do {
                CharacterComponent* linkIBP = recs[idx].character.Get();

                bool accept = true;
                if (typeId != 0) {
                    accept = false;
                    ComponentEntity* entity = linkIBP->_entity.Get();
                    int compCount = entity->_components._size;
                    Object::InternalPtr<Component>* comps = entity->_components._data;
                    for (int c = 0; c < compCount; c++) {
                        Component* comp = comps[c].Get();
                        if (static_cast<int>(comp->Type()) == typeId) { accept = true; break; }   // slot 2
                    }
                }

                if (accept) {
                    CharacterComponent* link = linkIBP;
                    const Math::Vector3& at = link->modelComponent->transform.translation;   // +0x68 -> +0x158
                    Math::Vector3 diff(at.x - pos[0],
                                       at.y - pos[1],
                                       at.z - pos[2]);
                    float dist2 = diff.Dot(diff).v;
                    if (dist2 < r2) {
                        // inline 8-byte grow-array push (×3 capacity rule)
                        int count = arr->size;
                        CharacterComponent** oldData = arr->data;
                        arr->size = count + 1;
                        if (arr->capacity < count + 1) {
                            arr->capacity = count + 1;
                            if (count != 0) arr->capacity = (count + 1) * 3;
                            arr->data = static_cast<CharacterComponent**>(
                                System::MemoryAllocator::MemAlloc((uint64_t)arr->capacity * 8, 8));
                        }
                        CharacterComponent** newData = arr->data;
                        int start = (oldData == newData) ? count : 0;
                        for (int i = start; i <= count; i++) {
                            newData[i] = (i < count) ? oldData[i] : linkIBP;
                        }
                        if (oldData != newData && oldData) System::MemoryAllocator::MemFree(oldData);
                    }
                }

                // The ring link at slot +0x08 (0x140092ec7), which CharacterGrid.h names `prev`.
                idx = recs[idx].prev;
            } while (idx != first);
        }

        walker.Subdivide();   // 0x140018880: pop node / push its children
    }
    return out;
}

// ============================================================
// 0x140116b90 — infect a citizen with `disease`
// ============================================================
// Skips if already sick, still immune (+0x70 > 0), or (profession field4D set AND
// employed +0x248 != 0). Posts the type-0xb (sev 3) event UNLESS an epidemic is already
// active (AnyCitizenSick) — the text formats {0}=season {1}=year {2}=disease name from
// the disease's own StringTable (+0x20) keyed by its name String (+0x28). Then:
// +0x68 = disease, +0x70 = Time((RandFloatTol*desc+0x98 + desc+0x94) * 300),
// +0x78 = Time(disease+0x30 * 300), FindCareBuilding (sick citizens seek care).
void CitizenComponent::Infect(Disease* disease) {
    EnsureInit();
    void* self = this;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!disease || !gw) {
        if (PlagueTraceOn())
            fprintf(stderr, "[Plague] INFECT skip=nullarg citizen=%p disease=%p gw=%p\n",
                    self, (void*)disease, (void*)gw), fflush(stderr);
        return;
    }

    if (this->disease != nullptr) {
        if (PlagueTraceOn())
            fprintf(stderr, "[Plague] INFECT skip=alreadySick citizen=%p\n", self), fflush(stderr);
        return;
    }
    if (field_0x70._ticks > 0) {
        if (PlagueTraceOn())
            fprintf(stderr, "[Plague] INFECT skip=immune citizen=%p immunityTicks=%lld\n",
                    self, (long long)field_0x70._ticks), fflush(stderr);
        return;
    }
    Profession* p1f0 = profession;
    if (p1f0 && p1f0->field4D && workplace != nullptr) {
        if (PlagueTraceOn())
            fprintf(stderr, "[Plague] INFECT skip=exemptProfessionAndHoused citizen=%p\n",
                    self), fflush(stderr);
        return;
    }

    System::Random* rng = gw->m_random;

    if (!CitizenComp_AnyCitizenSick(gw->m_citizenList)) {
        // Post "outbreak of {disease}" via the registered AddCitizenEvent
        // (0x140094e00): identical entry to the original inline block — color
        // 0xffff6060 (sev 3), type 0xb, icon 1, popup -1, tile -1 (null entity) —
        // including the drop-to-1x-speed tail on the settings +0x17 byte.
        EventLog* eventLog = gw->m_eventNotifier;
        const UI::StringTable* nameTable = disease->_stringTable;
        System::String diseaseName;
        bool haveName = false;
        if (nameTable) {
            ::new (&diseaseName) System::String(
                nameTable->GetText(disease->_name));
            haveName = true;
        }
        if (eventLog) {
            // {3}/{4} unused by the type-0xb template; AddCitizenEvent maps null -> "".
            // 0x140094e00, called by the member (the forwarder's null test is this one).
            eventLog->AddCitizenEvent(0xb, 3, nullptr,
                                      haveName ? &diseaseName : nullptr, nullptr, nullptr);
        }
    }

    this->disease = disease;
    CitizenDescription* desc = description;
    float tol = rng->RandFloatTol();   // ?RandFloatTol@Random@System@@ 0x1800c8000
    field_0x70._ticks = TimeFromSeconds(
        (tol * desc->_f98 + desc->_f94) * SecsPerMonth());
    field_0x78._ticks = TimeFromSeconds(
        disease->_field30 * SecsPerMonth());
    FindCareBuilding();   // 0x140111590

    // Both arms: whether the care link actually got made. +0x228 == 0 here means
    // FindCareBuilding (0x140111590 -> FindAttendance 0x140111930) found no building
    // whose WorkPlaceComponent+0x48 matches CitizenDescription+0x30 — no hospital, or
    // the profession ref is null. Without it RecoverOrDie uses careMult 1.0.
    if (PlagueTraceOn()) {
        fprintf(stderr,
                "[Plague] INFECT ok citizen=%p disease=%p durationTicks=%lld "
                "spreadTicks=%lld care(+0x228)=%s finderResolved=%d\n",
                self, (void*)disease, (long long)field_0x70._ticks,
                (long long)field_0x78._ticks,
                (careBuilding != nullptr) ? "LINKED" : "NOT-LINKED",
                1);   // the finder is a direct call: always resolved
        fflush(stderr);
    }
}

// ============================================================
// 0x140112620 — catch a disease (weighted random pick)
// ============================================================
// No-op while an epidemic is active. Builds a cumulative-weight array over the
// disease ExternalList (gw+0xa8, weight = disease+0x3c), rolls rand*total, picks the
// bucket, infects self. Non-Disease entries contribute a NULL deref in the original;
// here the type-id gate skips the whole body if "Disease" is unresolved.
void CitizenComponent::CatchDisease() {
    EnsureInit();
    void* self = this;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw || s_tidDisease == 0) {
        // One-shot: this is a structural condition, true for every citizen every tick.
        static bool once = false;
        if (PlagueTraceOn() && !once && (once = true))
            fprintf(stderr, "[Plague] CATCH bail=unresolved citizen=%p gw=%p "
                    "tidDisease=%u\n",
                    self, (void*)gw, s_tidDisease), fflush(stderr);
        return;
    }
    if (CitizenComp_AnyCitizenSick(gw->m_citizenList)) {
        ++s_rollEpidemicBlocked;
        return;   // censused, not logged per-call: happens for every citizen every tick
    }

    System::Random* rng = gw->m_random;
    ::Object::ExternalList* list = gw->m_diseaseList;
    if (!list) {
        // One-shot: structural, true for every citizen every tick while the flag is off.
        static bool once = false;
        if (PlagueTraceOn() && !once && (once = true))
            fprintf(stderr, "[Plague] CATCH bail=noDiseaseList (gw+0xa8 NULL — set "
                    "BANISHED_LIVE_DISEASELIST=1) citizen=%p\n", self), fflush(stderr);
        return;
    }
    int count = list->GetResourceCount();

    RawArray<float> cum = { nullptr, 0, 0 };   // the running weight totals
    float total = 0.0f;
    for (int i = 0; i < count; i++) {
        ::Object::External* entry = list->_resources[i];
        Disease* d = Object::IsDerivedFromNullSafe(entry, s_tidDisease) ? static_cast<Disease*>(entry) : nullptr;
        total += d->_field3C;   // faithful: NULL deref if not Disease
        CitizenComp_IntArrayResize(&cum, cum.size + 1, &total);
    }

    float target = rng->RandFloat() * total;
    float prev = 0.0f;
    int picked = -1;
    for (int i = 0; i < count; i++) {
        float c = cum.data[i];
        if (prev <= target && target < c) {
            ::Object::External* entry = list->_resources[i];
            Disease* d = Object::IsDerivedFromNullSafe(entry, s_tidDisease) ? static_cast<Disease*>(entry) : nullptr;
            picked = i;
            if (PlagueTraceOn()) {
                fprintf(stderr, "[Plague] CATCH draw citizen=%p entries=%d totalWeight=%.4f "
                        "target=%.4f -> idx=%d disease=%p\n",
                        self, count, total, target, i, (void*)d);
                fflush(stderr);
            }
            Infect(d);
            break;
        }
        prev = c;
    }
    // Other arm: no bucket matched (total weight 0, or an empty list).
    if (picked < 0 && PlagueTraceOn()) {
        fprintf(stderr, "[Plague] CATCH draw citizen=%p entries=%d totalWeight=%.4f "
                "target=%.4f -> NO BUCKET (nothing drawn)\n", self, count, total, target);
        fflush(stderr);
    }
    if (cum.data) System::MemoryAllocator::MemFree(cum.data);
}

// ============================================================
// 0x140112470 — try to spread the disease to a nearby citizen
// ============================================================
// Contagion roll (rand <= disease+0x34), then a radius query (disease+0x38) around
// this citizen's model position for entities owning a CitizenComponent; the first hit
// that is not self and not already sick gets infected with the same disease.
void CitizenComponent::SpreadDisease() {
    EnsureInit();
    void* self = this;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw) return;
    Disease* disease = this->disease;
    if (!disease) return;

    float contagionRoll = gw->m_random->RandFloat();
    float contagion = disease->_field34;
    if (contagionRoll > contagion) {
        if (PlagueTraceOn())
            fprintf(stderr, "[Plague] SPREAD citizen=%p roll=FAILED (%.4f > contagion %.4f)\n",
                    self, contagionRoll, contagion), fflush(stderr);
        return;
    }

    float radius = disease->_field38;
    uint32_t typeId = CitizenComponent::TypeId();   // cell 0x1401c7158 (D5)
    if (typeId == 0) return;

    // The model's translation (ModelComponent +0x148 transform, +0x158 translation).
    const Math::Vector3& at = character->modelComponent->transform.translation;
    float pos[3] = { at.x, at.y, at.z };

    RawArray<CharacterComponent*> found = { nullptr, 0, 0 };
    void* victim = nullptr;   // diag only
    void* scratch = nullptr;
    gw->m_characterGrid->RadiusQuery(&scratch, pos, &radius,
                          (int)typeId, &found);

    for (int i = 0; i < found.size; i++) {
        ComponentEntity* entity = found.data[i]->_entity.Get();
        if (entity == _entity.Get()) continue;   // skip self

        CitizenComponent* citizenIBP = entity->FindComponent<CitizenComponent>();   // typeId is CitizenComponent's (above)
        if (citizenIBP && citizenIBP->disease == nullptr) {
            victim = citizenIBP;
            if (PlagueTraceOn()) {
                fprintf(stderr, "[Plague] SPREAD citizen=%p roll=PASSED (%.4f <= %.4f) "
                        "radius=%.2f hits=%d -> INFECTING victim=%p\n",
                        self, contagionRoll, contagion, radius, found.size, victim);
                fflush(stderr);
            }
            citizenIBP->Infect(disease);
            break;
        }
    }
    // Other arm: the roll passed but nobody in range was eligible.
    if (!victim && PlagueTraceOn()) {
        fprintf(stderr, "[Plague] SPREAD citizen=%p roll=PASSED (%.4f <= %.4f) "
                "radius=%.2f hits=%d -> NO ELIGIBLE VICTIM (all self or already sick)\n",
                self, contagionRoll, contagion, radius, found.size);
        fflush(stderr);
    }
    System::MemoryAllocator::MemFree(found.data);   // original frees even when null
}

// ============================================================
// 0x1401123b0 — disease has run its course: recover or die
// ============================================================
// deathChance = (1 / disease+0x40) / (treated(+0x228) ? desc+0x128 : 1). rand < chance
// → die (release attendance link 59b0, return 0). Else survive: +0x78 = 0, +0x70 =
// immunity Time(desc+0x9c * 300), +0x68 = 0, release link, return 1.
char CitizenComponent::RecoverOrDie() {
    EnsureInit();
    void* self = this;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw) return 1;

    float r = gw->m_random->RandFloat();
    CitizenDescription* desc = description;
    float mult = (careBuilding != nullptr) ? desc->_f128 : 1.0f;
    Disease* disease = this->disease;
    float deathChance = (1.0f / disease->_field40) / mult;

    bool treated = (careBuilding != nullptr);
    if (deathChance >= r) {                     // COMISS deathChance,r; !JC
        if (PlagueTraceOn()) {
            fprintf(stderr, "[Plague] RESOLVE citizen=%p outcome=DIED deathChance=%.5f "
                    "roll=%.5f recipMortality(+0x40)=%.3f careMult=%.4f care(+0x228)=%s\n",
                    self, deathChance, r, disease->_field40, mult,
                    treated ? "LINKED" : "NOT-LINKED");
            fflush(stderr);
        }
        ReleaseCareBuilding();   // 0x1401159b0
        return 0;                               // dead
    }
    field_0x78._ticks = 0;
    field_0x70._ticks = TimeFromSeconds(desc->_f9c * SecsPerMonth());
    this->disease = nullptr;
    if (PlagueTraceOn()) {
        fprintf(stderr, "[Plague] RESOLVE citizen=%p outcome=RECOVERED deathChance=%.5f "
                "roll=%.5f recipMortality(+0x40)=%.3f careMult=%.4f care(+0x228)=%s "
                "immunityTicks=%lld\n",
                self, deathChance, r, disease->_field40, mult,
                treated ? "LINKED" : "NOT-LINKED", (long long)field_0x70._ticks);
        fflush(stderr);
    }
    ReleaseCareBuilding();   // 0x1401159b0
    return 1;                                   // recovered
}

// ============================================================
// 0x1401127d0 — CitizenComponent::UpdateDisease (tree: "UpdateHealth")
// ============================================================
// Returns 0 only when the citizen dies of disease (caller removes them).
// Healthy: tick down immunity (+0x70, clamp 0); lerp the catch-chance window from
// desc+0x7c..0x90 by (1 - health/maxHealth) then by the town health scale
// (*(gw+0xf0)+0x1a8); if the upper value B <= 0 just reset the roll timer; else roll
// elapsed(+0x60)/(A * 12 * 300 * B) vs rand and maybe catch a disease.
// Sick: tick +0x70 (duration) and +0x78 (spread); spread-tick expiry → SpreadDisease
// + re-arm from disease+0x30; duration expiry → RecoverOrDie.
char CitizenComponent::UpdateDisease(const System::Time* dtTime) {
    EnsureInit();
    void* self = this;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!dtTime || !gw) return 1;
    const int64_t* dt = &dtTime->_ticks;

    PlagueBootReport(this);

    if (disease == nullptr) {
        field_0x70._ticks -= *dt;
        if (field_0x70._ticks < 0) field_0x70._ticks = 0;

        CitizenDescription* desc = description;
        // The town health scale is CitizenBehavior +0x1a8.
        CitizenBehavior* mgr = gw->m_citizenList;
        System::Random* rng = gw->m_random;

        float t = 1.0f - health / desc->maxHealth;
        float a = (desc->_f84 - desc->_f7c) * t + desc->_f7c;
        float b = (desc->_f88 - desc->_f80) * t + desc->_f80;
        float m = mgr->field_0x1A8;
        float A = (desc->_f8c - a) * m + a;
        float B = (desc->_f90 - b) * m + b;

        ++s_rollCalls;
        if (!(B > 0.0f)) {                       // COMISS 0,B; !JC (B <= 0 or NaN)
            ++s_rollSkippedNoWindow;
            field_0x60._ticks = 0;
            PlagueCensusTick();
            return 1;
        }
        float elapsed = field_0x60.AsSeconds();
        field_0x60._ticks = 0;
        float denom = A * MonthsPerYear() * SecsPerMonth() * B;
        float ratio = elapsed / denom;
        float r = rng->RandFloat();
        s_rollChanceSum += (double)ratio;
        if (ratio > s_rollChanceMax) s_rollChanceMax = ratio;
        // [Plague] ROLL-INPUTS -- diagnostic only. The census reported meanChance and
        // maxChance as exactly 0.00000000 over 200k rolls with the disease list loaded,
        // which the aggregate alone cannot explain: it is either elapsed==0 (the +0x60
        // accumulator never reaching this read) or a denom so large the ratio underflows
        // the 8-decimal print. Those need different fixes, so print the raw terms of the
        // first few rolls instead of guessing.
        {
            static int nInputs = 0;
            if (PlagueTraceOn() && nInputs < 6) {
                ++nInputs;
                fprintf(stderr, "[Plague] ROLL-INPUTS citizen=%p elapsed=%.9fs A=%.6f "
                        "B=%.6f monthsPerYear=%.2f secsPerMonth=%.2f denom=%.6f "
                        "ratio=%.9f roll=%.6f health(+0x50)=%.3f maxHealth(desc+0x70)=%.3f "
                        "townMult(mgr+0x1a8)=%.6f\n",
                        self, elapsed, A, B, MonthsPerYear(), SecsPerMonth(), denom,
                        ratio, r, health, desc->maxHealth, m);
                fflush(stderr);
            }
        }
        if (ratio > r) {
            ++s_rollFired;
            if (PlagueTraceOn()) {
                fprintf(stderr, "[Plague] ROLL citizen=%p FIRED chance=%.6f roll=%.6f "
                        "health=%.2f/%.2f A=%.4f B=%.4f elapsed=%.2fs\n",
                        self, ratio, r, health, desc->maxHealth,
                        A, B, elapsed);
                fflush(stderr);
            }
            CatchDisease();
        }
        PlagueCensusTick();
        return 1;
    }

    // sick
    field_0x70._ticks -= *dt;
    field_0x78._ticks -= *dt;
    if (field_0x78._ticks <= 0) {
        SpreadDisease();
        field_0x78._ticks = TimeFromSeconds(this->disease->_field30 * SecsPerMonth());
    }
    if (field_0x70._ticks > 0) return 1;
    return RecoverOrDie();      // original tail-jumps here
}
