// src/diag/death_harness.cpp
// Diagnostic driver: kill N citizens on a chosen frame so the removal path
// (HandleLifeEvent -> MarkForRemoval -> the +0x38 broadcast -> CitizenComponent::
// OnRemove) can be exercised deterministically. Natural deaths are decades of game
// time away in a fresh start-town, which makes the citizen-removal teardown
// effectively untestable otherwise.
//
// Same shape as tool_harness.cpp: env-gated, one-shot, ticked from
// GameUpdateLoop::Update. No-op unless BANISHED_KILL_CITIZENS is set.
//
//   BANISHED_KILL_CITIZENS=<n>   how many citizens to kill (0 / unset = disabled)
//   BANISHED_KILL_AT_FRAME=<f>   frame to fire on (default 900)
//   BANISHED_FAMILY_CENSUS=<f>   every f frames, report how many citizens carry family
//                                links — spouse +0x250, ring head +0x260, partner +0x208
//   BANISHED_KILL_FAMILY=<n>     poll until citizens with a family link EXIST, then kill
//                                up to n of them. This is the only practical way to reach
//                                CitizenComponent::OnRemove's step-4 family branch: the
//                                start town begins with every link null, so an untargeted
//                                kill (or a natural death) exercises the empty path.
//
// Pair it with BANISHED_LIVE_CITIZEN_ONREMOVE=1 and BANISHED_ONREMOVE_TRACE=1 to see
// each teardown, and with BANISHED_OBJAUDIT=1 to watch the object tally.
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "options.h"
#include "Game/exe_constants.h"
#include "Entity/CitizenBehavior.h"           // B4: the town lists
#include "Entity/ComponentEntity.h"           // B4: FindComponent<ResidenceComponent>
#include "Component/CitizenComponent.h"       // B4: the citizen fields and members
#include "Component/CitizenDescription.h"     // B4: the description thresholds
#include "Component/ResidenceComponent.h"     // B4: IsFamilyFull
#include "Component/ResidenceDescription.h"   // B4: _maxResidentsPerFamily
#include "Component/CemeteryComponent.h"      // B4: plots._size
#include "Component/ImmigrationComponent.h"   // the nomad harness: the town hall's controller
#include "Component/ImmigrationDescription.h" //   its thresholds and month window
#include "Component/StateMachineComponent.h"  //   the node the answer events go through
#include "Entity/EntityList.h"                //   the master entity array (town hall scan)
#include "Game/Calendar.h"                    //   the month of the year
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <vector>

// CitizenComponent_HandleLifeEvent (0x140116dd0) — Component/exe_bodies.h.
// CitizenComponent_sub_1fa0 (0x140111fa0) — re-parent a citizen: detach from the current
// guardian's family ring (+0x258 / the host's +0x260 ring) and attach to the new one.
// Passing a null IBP is exactly what CitizenComponent::Update does when a grown child
// finds its own home and moves out. Declared in Component/exe_bodies.h.
// CitizenComponent_AssignWorkplace (0x1401163d0) — MISNAMED in retail: this is the
// RESIDENCE assigner (sets +0x238 = home and rosters the citizen into the house's
// ResidenceComponent). Update calls it right after the move-out with the house sub_0e70
// found; the harness calls it with the house the citizen already lived in.
// Declared in Component/exe_bodies.h.

namespace {
int  s_count       = -1;   // -1 = not yet read from the environment
int  s_atFrame     = 900;
int  s_censusEvery = 0;
int  s_killFamily  = 0;
int  s_marryHarness = 0;
int  s_birthTrace   = 0;   // BANISHED_BIRTH_TRACE — per-citizen detail lines for the birth gate
int  s_moved        = 0;   // citizens the marriage harness has made independent so far
int  s_forceBirth      = 0;   // BANISHED_FORCE_BIRTH — see the force-birth harness below
int  s_forceBirthFrame = 1200;
bool s_firedBirth      = false;
int  s_spouseProbe     = 0;   // BANISHED_SPOUSE_PROBE — frame of the FindSpouse probe (0 = off)
bool s_firedProbe      = false;
int  s_nomadPeriod     = 0;   // BANISHED_NOMAD_HARNESS — poll period of the nomad harness (0 = off)
bool s_fired       = false;
bool s_firedFamily = false;
bool s_firedMarry  = false;

void Init() {
    if (s_count >= 0) return;
    const char* n = Opt().killCitizens;
    s_count = (n && n[0]) ? std::atoi(n) : 0;
    const char* f = Opt().killAtFrame;
    if (f && f[0]) s_atFrame = std::atoi(f);
    const char* c = Opt().familyCensus;
    s_censusEvery = (c && c[0]) ? std::atoi(c) : 0;
    const char* kf = Opt().killFamily;
    s_killFamily = (kf && kf[0]) ? std::atoi(kf) : 0;
    const char* mh = Opt().marryHarness;
    s_marryHarness = (mh && mh[0]) ? std::atoi(mh) : 0;
    const char* bt = Opt().birthTrace;
    s_birthTrace = (bt && bt[0]) ? std::atoi(bt) : 0;
    const char* fb = Opt().forceBirth;
    s_forceBirth = (fb && fb[0]) ? std::atoi(fb) : 0;
    const char* fbf = Opt().forceBirthFrame;
    if (fbf && fbf[0]) s_forceBirthFrame = std::atoi(fbf);
    const char* sp = Opt().spouseProbe;
    s_spouseProbe = (sp && sp[0]) ? std::atoi(sp) : 0;
    const char* nh = Opt().nomadHarness;
    s_nomadPeriod = (nh && nh[0]) ? std::atoi(nh) : 0;
    if (s_spouseProbe > 0)
        std::fprintf(stderr, "[SpouseProbe] ENABLED — probing FindSpouse's age window at frame %d\n",
                     s_spouseProbe);
    if (s_nomadPeriod > 0)
        std::fprintf(stderr, "[NomadHarness] ENABLED — polling the town hall every %d frames\n",
                     s_nomadPeriod);
    if (s_forceBirth > 0)
        std::fprintf(stderr, "[BirthHarness] ENABLED — delivering %d baby(ies) at frame %d\n",
                     s_forceBirth, s_forceBirthFrame);
    if (s_birthTrace > 0)
        std::fprintf(stderr, "[BirthGate] ENABLED — walking the reproduction gate at each "
                     "census, up to %d per-citizen detail line(s)\n", s_birthTrace);
    if (s_count > 0) {
        std::fprintf(stderr, "[DeathHarness] ENABLED — killing %d citizen(s) at frame %d\n",
                     s_count, s_atFrame);
    }
    if (s_censusEvery > 0)
        std::fprintf(stderr, "[DeathHarness] family census every %d frames\n", s_censusEvery);
    if (s_killFamily > 0)
        std::fprintf(stderr, "[DeathHarness] will kill %d citizen(s) WITH family links "
                     "once any exist\n", s_killFamily);
    if (s_marryHarness > 0)
        std::fprintf(stderr, "[MarryHarness] ENABLED — will move up to %d grown, unmarried "
                     "citizen(s) out of their guardian's household once any are old enough\n",
                     s_marryHarness);
    std::fflush(stderr);
}

// The town citizen list: array at townMgr+0xa0, count at +0xa8.
bool TownList(Object::InternalPtr<CitizenComponent>** outList, int* outCount, CitizenBehavior** outTownMgr) {
    if (GameUpdateLoop::_singleton == nullptr) return false;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    CitizenBehavior* townMgr = gw->m_citizenList;      // +0xf0
    if (!townMgr) return false;
    *outTownMgr = townMgr;
    *outList  = townMgr->citizens._data;                     // +0xa0
    *outCount = townMgr->citizens._size;                 // +0xa8
    return *outList != nullptr && *outCount > 0;
}

inline bool HasFamily(CitizenComponent* c) {
    return c && (c->spouse != nullptr ||               // +0x250
                 c->_firstChild != nullptr);           // +0x260
}

// ---- Birth-gate walk (BANISHED_BIRTH_TRACE=<n>) ------------------------------------
// Open A of the next-session brief: the town has couples but zero births, and the
// reproduction timer (+0x4C) only ever counts down — never gets seeded. The seed lives in
// CitizenComponent::Update behind a chain of tests, and "no births" alone does not say
// which one nobody passes. This walks the SAME chain, in the same order, read-only, and
// reports the first test each female fails. It calls nothing that mutates state: the two
// resolved helpers (FindComponent_Residence, helper_a4f0) are both pure queries.
//
// The chain, from Update's LAB_140113873 tail:
//   1. +0x38 == 0            female
//   2. +0x3c >= desc+0x44    adult
//   3. +0x208 == 0           not a student/partner-in-training
//   4. NOT hungry/cold       hunger(+0x44) <= warn, warmth(+0x48) <= warn — a citizen over
//                            either warn level has its +0x4C ZEROED every tick, so the
//                            timer can never run down to fire
//   5. +0x250 != 0           married
//   6. *(+0x238) != 0        housed
//   7. +0x4C == 0            timer idle (a NEGATIVE +0x4C is the post-birth cooldown)
//   8. desc+0x50 <= age <= desc+0x54    within the childbearing window
//   9. residence component found off the home entity
//  10. !helper_a4f0          household not full: desc+0x24 > slot[+0x240].count
// Passing all ten is what seeds +0x4C = desc+0x58 and starts the countdown to a birth.
//
// n = how many per-citizen detail lines to print (the aggregate always prints when set).

const char* const kGateName[12] = {
    "PASSES-ALL",       // 0
    "male",             // 1
    "child",            // 2
    "student(+0x208)",  // 3
    "hungry/cold",      // 4
    "unmarried",        // 5
    "homeless",         // 6
    "timerBusy",        // 7
    "ageWindow",        // 8
    "noResidence",      // 9
    "householdFull",    // 10
    "noDesc",           // 11
};

void BirthGate(int frameNum, Object::InternalPtr<CitizenComponent>* list, int n) {
    if (s_birthTrace <= 0) return;
    // DAT_140172114 / DAT_140172100 (.rdata).
    const float hungerFrac = kRdata_140172114;
    const float warmthFrac = kRdata_140172100;

    int tally[12] = {0};
    int printed = 0;
    // Thresholds read off the first citizen with a description, so the numbers have a scale.
    float hWarn = 0.0f, wWarn = 0.0f, bearMin = 0.0f, bearMax = 0.0f;
    float timerSeed = 0.0f, occupantCap = 0.0f;
    bool  haveDesc = false;

    for (int i = 0; i < n; i++) {
        CitizenComponent* c = list[i].Get();
        if (!c) continue;
        const CitizenDescription* desc = c->description;          // +0x288
        if (!desc) { tally[11]++; continue; }

        const float hungerWarn = (desc->maxHunger - desc->_fcc) * hungerFrac +    // +0xD0, +0xCC
                                 desc->_fcc;
        const float warmthWarn = (desc->maxColdness - desc->_fec) * warmthFrac +  // +0xF0, +0xEC
                                 desc->_fec;
        if (!haveDesc) {
            haveDesc = true;
            hWarn = hungerWarn; wWarn = warmthWarn;
            bearMin = desc->minPregnancyAge;                       // +0x50
            bearMax = desc->maxPregnancyAge;                       // +0x54
            timerSeed = desc->pregnancyTime;                       // +0x58
            // The occupant cap lives on the RESIDENCE description, not the citizen's —
            // filled in below from the first residence we successfully resolve.
        }

        const float age    = c->age;                               // +0x3c
        const float hunger = c->hunger;                            // +0x44
        const float warmth = c->coldness;                          // +0x48
        const float timer  = c->pregnancyTimer;                    // +0x4C
        ComponentEntity* const home = c->home.Get();                     // +0x238
        const int   slotIdx = c->homeSlot;                         // +0x240

        int gate = 0;
        ResidenceComponent* residence = nullptr;
        if (c->gender != 0)                                            gate = 1;
        else if (age < desc->adultAge)                                 gate = 2;
        else if (c->school != nullptr)                                 gate = 3;
        else if (hungerWarn < hunger || warmthWarn < warmth)           gate = 4;
        else if (c->spouse == nullptr)                                 gate = 5;
        else if (home == nullptr)                                      gate = 6;
        else if (timer != 0.0f)                                        gate = 7;
        else if (age < desc->minPregnancyAge ||
                 age > desc->maxPregnancyAge)                          gate = 8;
        else {
            // 0x140008090 is Entity::FindComponent<ResidenceComponent> (NOT storage — 0x140006270
            // is the storage one; the two are a standing naming trap in this codebase).
            residence = home->FindComponent<ResidenceComponent>();
            if (!residence)                                            gate = 9;
            else if (residence->IsFamilyFull(slotIdx))                 gate = 10;   // 0x14012a4f0
        }
        tally[gate]++;

        // Detail lines: the interesting citizens are the ones deep in the chain, so print
        // anything past the married gate first — a wall of "male"/"child" says nothing.
        if (printed < s_birthTrace && (gate == 0 || gate >= 4)) {
            printed++;
            float cap = -1.0f;
            if (residence) {
                // A ResidenceComponent's description is its ResidenceDescription.
                if (Description* rdesc = residence->_description) {    // +0x18
                    cap = static_cast<float>(
                        static_cast<ResidenceDescription*>(rdesc)->_maxResidentsPerFamily);   // +0x24
                    if (occupantCap == 0.0f) occupantCap = cap;
                }
            }
            std::fprintf(stderr,
                "[BirthGate]   c=%p BLOCKED-AT=%s age=%.2f hunger=%.1f/%.1f warmth=%.1f/%.1f "
                "timer(+0x4C)=%.2f score(+0x50)=%.2f spouse=%d home=%d slot=%d occupantCap=%.0f\n",
                static_cast<void*>(c), kGateName[gate], age, hunger, hungerWarn,
                warmth, warmthWarn, timer, c->health,             // +0x50
                c->spouse != nullptr ? 1 : 0,
                home != nullptr ? 1 : 0, slotIdx, cap);
        }
    }

    std::fprintf(stderr,
        "[BirthGate] frame %d: PASSES-ALL=%d | male=%d child=%d student=%d hungry/cold=%d "
        "unmarried=%d homeless=%d timerBusy=%d ageWindow=%d noResidence=%d householdFull=%d\n",
        frameNum, tally[0], tally[1], tally[2], tally[3], tally[4], tally[5], tally[6],
        tally[7], tally[8], tally[9], tally[10]);
    std::fprintf(stderr,
        "[BirthGate]   thresholds: hungerWarn=%.1f warmthWarn=%.1f (frac %.2f/%.2f) "
        "bearingAge=[%.2f,%.2f] timerSeed(desc+0x58)=%.2f occupantCap(rdesc+0x24)=%.0f\n",
        hWarn, wWarn, hungerFrac, warmthFrac, bearMin, bearMax, timerSeed, occupantCap);
    // The roll itself, so a failed attempt is interpretable. Update computes
    //   pSuccess = (score(+0x50) / desc+0x70) * (desc+0x64 - desc+0x68) + desc+0x68
    // and a birth happens when RandFloat() < pSuccess; otherwise +0x4C is reseeded to
    // -(rand * desc+0x60 + desc+0x5C), the post-attempt cooldown.
    if (haveDesc) {
        for (int i = 0; i < n; i++) {
            CitizenComponent* c = list[i].Get();
            const CitizenDescription* d = c ? c->description : nullptr;   // +0x288
            if (!d) continue;
            std::fprintf(stderr,
                "[BirthGate]   roll consts: chanceLow(desc+0x68)=%.4f chanceHigh(desc+0x64)=%.4f "
                "scoreMax(desc+0x70)=%.2f cooldownBase(desc+0x5C)=%.1f range(desc+0x60)=%.1f\n",
                d->_f68, d->_f64, d->maxHealth, d->_f5c, d->_f60);
            break;
        }
    }
    std::fflush(stderr);
}

// ---- Couples (part of the census) ---------------------------------------------------
// CitizenComponent::FindSpouse (0x14010fe40) pairs a seeker with the first unmarried
// citizen of the other gender older than the marriage age (+0x48) whose age differs from
// the seeker's by at most the description's window (+0x4c). A couple whose partners the
// census saw single at an earlier census is one FindSpouse formed (the founding families
// and an accepted nomad group arrive married), so each such couple is checked once
// against that window: [Couple] PASS / FAIL. Both partners age at the same rate, so the
// gap read now is the gap at the marriage.
std::unordered_set<const CitizenComponent*> s_seenSingle;
std::unordered_set<const CitizenComponent*> s_checkedCouple;

void Couples(int frameNum, Object::InternalPtr<CitizenComponent>* list, int n) {
    for (int i = 0; i < n; i++) {
        CitizenComponent* c = list[i].Get();
        if (!c) continue;
        CitizenComponent* s = c->spouse.Get();                     // +0x250
        if (s == nullptr) {
            s_seenSingle.insert(c);
            continue;
        }
        if (c > s) continue;                                       // each pair once
        if (s_checkedCouple.count(c) != 0) continue;
        if (s_seenSingle.count(c) == 0 || s_seenSingle.count(s) == 0) continue;
        s_checkedCouple.insert(c);
        const CitizenDescription* d = c->description;              // +0x288
        const float gap = std::fabs(c->age - s->age);              // +0x3c
        const bool ok = c->gender != s->gender && s->spouse.Get() == c && gap <= d->_f4c;
        std::fprintf(stderr, "[Couple] %s frame %d: %p (gender %d, age %.2f) + %p (gender %d, "
                     "age %.2f) gap %.3f window(+0x4c) %.2f marriageAge(+0x48) %.2f\n",
                     ok ? "PASS" : "FAIL", frameNum, static_cast<void*>(c), c->gender, c->age,
                     static_cast<void*>(s), s->gender, s->age, gap, d->_f4c, d->marriageAge);
    }
    std::fflush(stderr);
}

// Known_port_issues row 990: every citizen carries a profession (+0x1f0) -- retail never
// creates one without (every birth / immigration / founding path passes one). Counted
// on every census, so a newborn or a converted nomad with a null one shows up.
void ProfessionCensus(int frameNum, Object::InternalPtr<CitizenComponent>* list, int n) {
    int nulls = 0;
    for (int i = 0; i < n; i++) {
        CitizenComponent* c = list[i].Get();
        if (c && c->profession == nullptr) {
            if (nulls < 4)
                std::fprintf(stderr, "[ProfessionCensus]   null profession: %p age %.2f\n",
                             static_cast<void*>(c), c->age);
            nulls++;
        }
    }
    std::fprintf(stderr, "[ProfessionCensus] frame %d: %d citizens, profession(+0x1f0) null=%d\n",
                 frameNum, n, nulls);
    std::fflush(stderr);
}

void Census(int frameNum) {
    Object::InternalPtr<CitizenComponent>* list; int n; CitizenBehavior* townMgr;
    if (!TownList(&list, &n, &townMgr)) {
        // TownList fails when the citizen array is null OR the count is 0 — i.e. an EXTINCT
        // town looks exactly like "no census at all". Silence then reads as a hung process
        // (it did, 2026-07-25: a FORCE_SPEED=40 soak went quiet and three gdb samples inside
        // the DX11 Present path were misread as a stall, when the town had simply died out —
        // RTSS's frame limiter blocks in Present, so that is where an IDLE run always samples).
        // Say it out loud instead.
        std::fprintf(stderr, "[FamilyCensus] frame %d: NO CITIZENS — town list is empty or "
                     "unavailable (extinct town, or the world is not built yet)\n", frameNum);
        std::fflush(stderr);
        return;
    }
    int spouse = 0, ring = 0, partner = 0, host = 0;
    // Gender (+0x38): 0 = female, non-zero = male. Retail's per-citizen init writes it for
    // every colonist; the founding spawner reproduces it from the mesh variant. A town that
    // reports female == citizens is one where nobody can marry (the opposite-gender test in
    // helper_fe40 can never pass) and every adult passes the reproduction gate — the exact
    // symptom of the gender write being skipped. Tracked here so the A/B is measurable.
    int female = 0, male = 0;
    // Marriage-gate breakdown. CitizenComponent::Update's partner block (the tail at
    // 0x140113827) only calls the finder when ALL of these hold:
    //     +0x258 (guardian/host citizen) == 0
    //     +0x238 (home) != 0
    //     +0x250 (spouse) == 0
    //     +0x3c (age) >= desc+0x48 (marriage age)
    // A soak that reports "0 marriages" is uninformative without knowing which of the
    // four is the one nobody passes, so count them separately. `eligible` is the number
    // of citizens that actually reach the finder on a given tick.
    int adults = 0, children = 0;
    int overMarryAge = 0, unmarried = 0, noGuardian = 0, hasHome = 0, eligible = 0;
    float youngest = 1e9f, oldest = -1e9f, marryAge = -1.0f, adultAge = -1.0f;
    // Survival state. The standalone town has no food economy: houses are seeded with a
    // fixed stock and nothing replaces it, so hunger (+0x44) and thirst (+0x48) climb until
    // the whole cohort starves at once. Without these, a soak just ends and the census goes
    // quiet — which is exactly how the 2026-07-25 die-off was first misread as a hang.
    float hungerMax = 0.0f, hungerSum = 0.0f, thirstMax = 0.0f, thirstSum = 0.0f;
    int   sick = 0;
    // Lifespan audit. CitizenComponent::Update kills on `*(p+0x40) <= *(p+0x3c)`
    // (maxAge <= age) via HandleLifeEvent(self, 2), and that handler ALWAYS returns 1
    // and ALWAYS queues the entity for removal — so an "overdue" citizen (age past its
    // own maxAge) that is still in this list means the old-age branch is not being
    // reached, not that it declined to fire. The founding spawn rolls
    // maxAge = RandFloat * desc+0x3c + desc+0x38, i.e. [15,17) with the retail
    // constants, so once the oldest passes 15 some deaths are due. Counting `overdue`
    // separates "nobody is old enough yet" from "the check never runs" — two states
    // that look identical on the population curve alone.
    int   overdue = 0, noMaxAge = 0;
    float maxAgeMin = 1e9f, maxAgeMax = -1e9f;
    for (int i = 0; i < n; i++) {
        CitizenComponent* c = list[i].Get();
        if (!c) continue;
        bool noSpouse = c->spouse == nullptr;                  // +0x250
        if (!noSpouse) spouse++; else unmarried++;
        if (c->_firstChild != nullptr) ring++;                 // +0x260
        if (c->school != nullptr) partner++;                   // +0x208
        bool guardianFree = c->_parent == nullptr;             // +0x258
        if (!guardianFree) host++;
        if (c->gender == 0) female++; else male++;             // +0x38

        float hunger = c->hunger;                              // +0x44
        float thirst = c->coldness;                            // +0x48 (row 121)
        hungerSum += hunger; thirstSum += thirst;
        if (hunger > hungerMax) hungerMax = hunger;
        if (thirst > thirstMax) thirstMax = thirst;
        if (c->disease != nullptr) sick++;                     // +0x68

        const float cAge   = c->age;                           // +0x3c
        const float cMaxAge = c->maxAge;                       // +0x40
        if (cMaxAge <= 0.0f) noMaxAge++;
        else {
            if (cMaxAge < maxAgeMin) maxAgeMin = cMaxAge;
            if (cMaxAge > maxAgeMax) maxAgeMax = cMaxAge;
            if (cMaxAge <= cAge) overdue++;
        }

        const CitizenDescription* desc = c->description;      // +0x288
        if (!desc) continue;
        float age = c->age;                                    // +0x3c
        adultAge  = desc->adultAge;                            // +0x44
        marryAge  = desc->marriageAge;                         // +0x48
        if (age < youngest) youngest = age;
        if (age > oldest)   oldest   = age;
        if (age >= adultAge) adults++; else children++;
        if (!noSpouse) continue;                 // already married: not a candidate
        bool old_enough = age >= marryAge;
        bool home       = c->home != nullptr;                  // +0x238
        if (old_enough)   overMarryAge++;
        if (guardianFree) noGuardian++;
        if (home)         hasHome++;
        if (old_enough && guardianFree && home) eligible++;
    }
    static int s_prevN = -1;
    int delta = (s_prevN < 0) ? 0 : n - s_prevN;
    s_prevN = n;
    std::fprintf(stderr, "[FamilyCensus] frame %d: %d citizens (%+d) — spouse(+0x250)=%d "
                 "ring(+0x260)=%d partner(+0x208)=%d host(+0x258)=%d "
                 "gender(+0x38) female=%d male=%d\n",
                 frameNum, n, delta, spouse, ring, partner, host, female, male);
    // A grown child can only stop being someone's dependant (+0x258) by finding a home of
    // its OWN: CitizenComponent::Update -> sub_0e70 -> sub_1fa0(self, null). sub_0e70 scans
    // the town building registry at townMgr+0x20/+0x28 and the profession/category BST at
    // townMgr+0x100. Both are populated by WorkPlaceComponent::OnCreate in retail and are
    // empty in standalone's start town, so report them next to the gate counts — an
    // "ELIGIBLE=0 / noGuardian=0" census means nothing without knowing whether the finder
    // it depends on had anything to find.
    Object::InternalPtr<ComponentEntity>* reg = townMgr->field_0x20._data;           // +0x20
    int     regN  = townMgr->field_0x20._size;                   // +0x28
    void*   bst   = townMgr->field_0x100._root;                  // +0x100
    int     bldgN = townMgr->attendanceComponents._size;     // +0x68
    std::fprintf(stderr, "[FamilyCensus]   townMgr=%p buildingRegistry(+0x20)=%p count(+0x28)=%d "
                 "profBST(+0x100)=%p bldgCount(+0x68)=%d\n",
                 static_cast<void*>(townMgr), static_cast<void*>(reg), regN,
                 bst, bldgN);
    std::fprintf(stderr, "[FamilyCensus]   ages: %d adult / %d child (min=%.2f max=%.2f "
                 "adultAge=%.2f marryAge=%.2f) — unmarried=%d of which overMarryAge=%d "
                 "noGuardian=%d hasHome=%d ELIGIBLE=%d\n",
                 adults, children, (youngest > 1e8f ? 0.0f : youngest),
                 (oldest < -1e8f ? 0.0f : oldest), adultAge, marryAge,
                 unmarried, overMarryAge, noGuardian, hasHome, eligible);
    // desc+0xcc/+0xd0 are the hunger warn/death thresholds, +0xec/+0xf0 the thirst ones —
    // read them off the last citizen's description so the numbers have a scale.
    float hungerDeath = 0.0f, thirstDeath = 0.0f;
    for (int i = 0; i < n; i++) {
        CitizenComponent* c = list[i].Get();
        const CitizenDescription* d = c ? c->description : nullptr;   // +0x288
        if (d) { hungerDeath = d->maxHunger;                          // +0xd0
                 thirstDeath = d->maxColdness; break; }               // +0xf0
    }
    std::fprintf(stderr, "[FamilyCensus]   survival: hunger avg=%.1f max=%.1f (death %.1f) "
                 "thirst avg=%.1f max=%.1f (death %.1f) sick(+0x68)=%d\n",
                 n ? hungerSum / n : 0.0f, hungerMax, hungerDeath,
                 n ? thirstSum / n : 0.0f, thirstMax, thirstDeath, sick);
    // ⚠`thirst` above is really WARMTH/COLD (+0x48) — a town can freeze at 20.0 while
    // hunger sits comfortably at 5. See project_population_sustainability_soak.
    std::fprintf(stderr, "[FamilyCensus]   lifespan(+0x40): OVERDUE(age>=maxAge)=%d "
                 "maxAge min=%.2f max=%.2f zero/unset=%d\n",
                 overdue, (maxAgeMin > 1e8f ? 0.0f : maxAgeMin),
                 (maxAgeMax < -1e8f ? 0.0f : maxAgeMax), noMaxAge);
    std::fflush(stderr);
    Couples(frameNum, list, n);
    ProfessionCensus(frameNum, list, n);
    BirthGate(frameNum, list, n);
}
}  // namespace

// ---- Marriage harness (BANISHED_MARRY_HARNESS=<n>) --------------------------------
// The marriage block in CitizenComponent::Update only runs for a citizen with
//   +0x258 (guardian) == 0, +0x238 (home) != 0, +0x250 (spouse) == 0, age >= desc+0x48.
// A citizen born into a family clears the FIRST of those only by finding a home of its
// own — Update -> sub_0e70 -> sub_1fa0(self, null). sub_0e70 scans the town building
// registry (townMgr+0x20/+0x28) and the profession BST (townMgr+0x100).
//
// HISTORY: when this harness was written, both registries were empty in the standalone
// start town, so no citizen could ever move out and the marriage code was untestable.
// The town-registry wave (07155bb, 2026-07-26) fixed that — the residence state-2 enter
// now fills townMgr+0x20 and ORGANIC move-out + marriage are proven in the standing run
// (spouse 12 -> 14 by frame 3000, given a free house via BANISHED_BUILD_DEMO_HOUSE=1).
// The harness stays for tests that need a couple NOW rather than after an organic
// move-out, or without a spare house: it needs neither.
//
// This harness performs the move-out the retail way: sub_1fa0(citizen, nullIBP), the
// same call Update and the marriage block itself make. Leaving the household also drops
// the citizen out of its guardian's house (+0x238 goes to 0), and a homeless citizen
// still fails the marriage gate — so, exactly as Update does one line later, the harness
// re-assigns a home immediately: the very house the citizen already lived in, which
// keeps the residence roster count unchanged. It does NOT touch +0x250 or the age gate;
// everything after this is the game's own code path.
void MoveOutGrownChildren(int frameNum) {
    Object::InternalPtr<CitizenComponent>* list; int n; CitizenBehavior* townMgr;
    if (!TownList(&list, &n, &townMgr)) return;
    for (int i = 0; i < n && s_moved < s_marryHarness; i++) {
        CitizenComponent* c = list[i].Get();
        if (!c) continue;
        if (c->spouse != nullptr) continue;                          // already married (+0x250)
        if (c->_parent == nullptr) continue;                         // already independent (+0x258)
        ComponentEntity* home = c->home.Get();                             // +0x238
        if (home == nullptr) continue;                               // no home to move back into
        const CitizenDescription* desc = c->description;            // +0x288
        if (!desc) continue;
        float age = c->age;                                          // +0x3c
        if (age < desc->marriageAge) continue;                       // not old enough yet (+0x48)

        int slot = c->homeSlot;                                      // +0x240
        c->SetParent(nullptr);                                       // 0x140111fa0 (null IBP)
        CitizenComponent* guardian = c->_parent.Get();                     // +0x258
        c->SetHome(home, slot);                                      // 0x1401163d0
        ComponentEntity* homeAfter = c->home.Get();                        // +0x238
        std::fprintf(stderr, "[MarryHarness] frame %d: moved out comp=%p age=%.2f gender=%d "
                     "— guardian(+0x258) %s, home(+0x238) %p -> %p\n", frameNum,
                     static_cast<void*>(c), age, c->gender,
                     guardian == nullptr ? "cleared" : "STILL SET",
                     static_cast<void*>(home), static_cast<void*>(homeAfter));
        std::fflush(stderr);
        if (guardian == nullptr && homeAfter != nullptr) ++s_moved;
    }
    if (s_moved >= s_marryHarness) {
        s_firedMarry = true;
        std::fprintf(stderr, "[MarryHarness] %d citizen(s) independent and housed — done\n",
                     s_moved);
        std::fflush(stderr);
    }
}

// ---- Force-birth harness (BANISHED_FORCE_BIRTH=<n>) -------------------------------
// An organic birth is unreachable in a test-length run: conception needs a married,
// housed, fed, in-window female with an idle +0x4C timer, and then desc+0x58 = 900
// seconds of gestation on top. An 11-minute soak at FORCE_SPEED=10 produced zero
// deliveries — the founding cohort ages out first.
//
// So call the DELIVERY leaf directly. This is not a shortcut around the code under
// test: CitizenComp_helper_1b90 (0x140111b90) is exactly what CitizenComponent::Update
// invokes when the gestation timer fires, and it owns the whole newborn path —
// allocate + copy-construct the child entity, DeferredInit_117080, and
// EntityList_RegisterEntity (0x1400c48a0), whose tail is the OnAddedPass that
// broadcasts component vtable slot 6. Everything the birth arm needs to exercise is
// inside it; only the gestation countdown is skipped.
//
// Requirements on the "mother": a description (+0x288) and a ModelComponent (+0x278,
// for the spawn position). A spouse is optional — helper_1b90 tests +0x250 before
// flagging it. Any founding adult qualifies.
//
//   BANISHED_FORCE_BIRTH=<n>        deliver n babies (0 / unset = disabled)
//   BANISHED_FORCE_BIRTH_FRAME=<f>  frame to fire on (default 1200)
//
// Pair with BANISHED_BIRTH_TRACE=1 to get the [Birth] BABY BORN line and the newborn's
// carry capacity, which is the point of the exercise.
// CitizenComp_helper_1b90 is declared in Component/exe_bodies.h.

namespace {
// The newborn check: the citizens before the deliveries, and the frame the town list is
// read again (a newborn joins it through its own OnAdd, so give the world a few frames).
std::unordered_set<const CitizenComponent*> s_beforeBirth;
int s_birthsDelivered = 0;
int s_birthCheckFrame = -1;

void CheckNewborns(int frameNum) {
    Object::InternalPtr<CitizenComponent>* list; int n; CitizenBehavior* townMgr;
    if (!TownList(&list, &n, &townMgr)) {
        std::fprintf(stderr, "[BirthCheck] FAIL frame %d: no town citizen list\n", frameNum);
        std::fflush(stderr);
        return;
    }
    int newborns = 0, nullProfession = 0;
    for (int i = 0; i < n; i++) {
        CitizenComponent* c = list[i].Get();
        if (!c || s_beforeBirth.count(c) != 0) continue;
        newborns++;
        if (c->profession == nullptr) nullProfession++;           // +0x1f0
        std::fprintf(stderr, "[BirthCheck]   newborn %p age %.2f gender %d profession(+0x1f0)=%p "
                     "parent(+0x258)=%p home(+0x238)=%p\n", static_cast<void*>(c), c->age,
                     c->gender, static_cast<void*>(c->profession),
                     static_cast<void*>(c->_parent.Get()), static_cast<void*>(c->home.Get()));
    }
    const bool ok = newborns == s_birthsDelivered && nullProfession == 0;
    std::fprintf(stderr, "[BirthCheck] %s frame %d: %d delivered, %d newborn(s) in the town list, "
                 "%d with a null profession\n", ok ? "PASS" : "FAIL", frameNum,
                 s_birthsDelivered, newborns, nullProfession);
    std::fflush(stderr);
}

void ForceBirths(int frameNum) {
    Object::InternalPtr<CitizenComponent>* list; int n; CitizenBehavior* townMgr;
    if (!TownList(&list, &n, &townMgr)) {
        std::fprintf(stderr, "[BirthHarness] frame %d: no town citizen list — nothing to do\n",
                     frameNum);
        std::fflush(stderr);
        return;
    }
    // Snapshot the mothers first: each birth appends to this very list.
    CitizenComponent* mothers[16];
    int want = 0;
    int skippedNoDesc = 0, skippedNoModel = 0;
    for (int i = 0; i < n && want < s_forceBirth && want < 16; i++) {
        CitizenComponent* c = list[i].Get();
        if (!c) continue;
        if (c->description == nullptr) { skippedNoDesc++;  continue; }   // +0x288
        if (c->character == nullptr)   { skippedNoModel++; continue; }   // +0x278
        mothers[want++] = c;
    }
    std::fprintf(stderr, "[BirthHarness] frame %d: %d mother(s) selected from %d citizens "
                 "(skipped: noDesc=%d noModel=%d)\n",
                 frameNum, want, n, skippedNoDesc, skippedNoModel);
    std::fflush(stderr);
    if (want == 0) return;

    int before = townMgr->citizens._size;                    // +0xa8
    for (int i = 0; i < n; i++)
        s_beforeBirth.insert(list[i].Get());
    s_birthsDelivered = want;
    s_birthCheckFrame = frameNum + 120;
    for (int i = 0; i < want; i++) {
        std::fprintf(stderr, "[BirthHarness] delivering %d/%d from mother=%p\n",
                     i + 1, want, static_cast<void*>(mothers[i]));
        std::fflush(stderr);
        mothers[i]->GiveBirth();                           // 0x140111b90
    }
    std::fprintf(stderr, "[BirthHarness] done — town citizen list %d -> %d "
                 "(+%d expected if CitizenComponent::OnAdd is live)\n",
                 before, townMgr->citizens._size, want);
    std::fflush(stderr);
}
// ---- Spouse probe (BANISHED_SPOUSE_PROBE=<frame>) ---------------------------------
// FindSpouse (0x14010fe40) is a pure query, so it can be asked directly. The probe picks a
// seeker S and, as the candidate C, the FIRST unmarried citizen of the other gender in
// the town list: every citizen before C is then of S's gender or married, so whenever C
// is eligible FindSpouse must return C. For one call at a time it sets S to
// marriageAge + window + 2 years and C to an age just past the window older, just past
// it younger, inside it older and inside it younger, then restores both ages. Beyond the
// window C must not come back (either direction); inside it C must. Any citizen returned
// must be unmarried, of the other gender, older than the marriage age and within the
// window of S.
bool ProbeCase(const char* label, CitizenComponent* seeker, CitizenComponent* cand, float candAge,
               bool expectCand) {
    const CitizenDescription* d = seeker->description;            // +0x288
    const float saved = cand->age;
    cand->age = candAge;
    CitizenComponent* got = seeker->FindSpouse();                 // 0x14010fe40
    bool ok = expectCand ? got == cand : got != cand;
    if (got != nullptr) {
        const float gap = std::fabs(got->age - seeker->age);
        if (got->gender == seeker->gender || got->spouse != nullptr || got->age <= d->marriageAge ||
            gap > d->_f4c)
            ok = false;
    }
    std::fprintf(stderr, "[SpouseProbe]   %-14s seeker age %.2f, candidate age %.2f (diff %+.2f): "
                 "FindSpouse -> %s%p (age %.2f) %s\n", label, seeker->age, candAge,
                 candAge - seeker->age, got == cand ? "THE CANDIDATE " : "", static_cast<void*>(got),
                 got ? got->age : 0.0f, ok ? "ok" : "WRONG");
    cand->age = saved;
    return ok;
}

void SpouseProbe(int frameNum) {
    Object::InternalPtr<CitizenComponent>* list; int n; CitizenBehavior* townMgr;
    if (!TownList(&list, &n, &townMgr)) return;
    CitizenComponent* seeker = nullptr;
    CitizenComponent* cand = nullptr;
    for (int i = 0; i < n && !seeker; i++) {
        CitizenComponent* c = list[i].Get();
        if (c && c->spouse == nullptr && c->description != nullptr) seeker = c;
    }
    for (int i = 0; i < n && seeker && !cand; i++) {
        CitizenComponent* c = list[i].Get();
        if (c && c->spouse == nullptr && c->gender != seeker->gender) cand = c;
    }
    if (!seeker || !cand) {
        std::fprintf(stderr, "[SpouseProbe] FAIL frame %d: no unmarried pair of opposite gender "
                     "(seeker %p, candidate %p)\n", frameNum, static_cast<void*>(seeker),
                     static_cast<void*>(cand));
        std::fflush(stderr);
        return;
    }
    const CitizenDescription* d = seeker->description;
    const float w = d->_f4c;
    const float savedSeeker = seeker->age;
    seeker->age = d->marriageAge + w + 2.0f;
    const float a = seeker->age;
    std::fprintf(stderr, "[SpouseProbe] frame %d: seeker %p (gender %d) candidate %p (gender %d); "
                 "marriageAge(+0x48) %.2f window(+0x4c) %.2f\n", frameNum, static_cast<void*>(seeker),
                 seeker->gender, static_cast<void*>(cand), cand->gender, d->marriageAge, w);
    bool ok = true;
    ok = ProbeCase("older, beyond", seeker, cand, a + w + 0.25f, false) && ok;
    ok = ProbeCase("younger, beyond", seeker, cand, a - w - 0.25f, false) && ok;
    ok = ProbeCase("older, inside", seeker, cand, a + w * 0.5f, true) && ok;
    ok = ProbeCase("younger, inside", seeker, cand, a - w * 0.5f, true) && ok;
    seeker->age = savedSeeker;
    std::fprintf(stderr, "[SpouseProbe] %s frame %d: the age window holds in both directions\n",
                 ok ? "PASS" : "FAIL", frameNum);
    std::fflush(stderr);
}

// ---- The organic stage of the spouse probe (600 frames after the probe) ----------
// The founding town's only unmarried citizens are children, decades of game time from
// the marriage age, so no couple forms on its own in a test-length run. The stage makes
// one pair that CitizenComponent::Update's partner block can reach: it widows a man of
// one founding couple and a woman of another (HandleLifeEvent 3 on each one's spouse,
// the death path), so the survivors are single, housed, guardian-free adults. It then
// sets the woman's age to the man's + window + 1: for 1800 frames neither one's Update
// may marry them (the candidate is beyond the window older for him, younger for her).
// Then it sets her age inside the window, and the pair must marry within 1800 frames.
// Every marriage itself is the game's own Update -> FindSpouse.
int s_organicStage = 0;       // 0 not started, 1 apart, 2 inside the window, 3 done
int s_organicFrame = 0;
CitizenComponent* s_widower = nullptr;
CitizenComponent* s_widow = nullptr;

void SpouseOrganic(int frameNum) {
    Object::InternalPtr<CitizenComponent>* list; int n; CitizenBehavior* townMgr;
    if (!TownList(&list, &n, &townMgr)) return;
    if (s_organicStage == 0) {
        CitizenComponent* wifeOfA = nullptr;
        CitizenComponent* husbandOfB = nullptr;
        for (int i = 0; i < n; i++) {
            CitizenComponent* c = list[i].Get();
            if (!c || c->spouse == nullptr || c->_parent != nullptr || c->home == nullptr) continue;
            CitizenComponent* s = c->spouse.Get();
            if (s->_parent != nullptr || s->home == nullptr) continue;
            if (c->gender == 1 && !s_widower && c != husbandOfB) {
                s_widower = c;
                wifeOfA = s;
            } else if (c->gender == 0 && !s_widow && c != wifeOfA) {
                s_widow = c;
                husbandOfB = s;
            }
        }
        if (!s_widower || !s_widow) {
            std::fprintf(stderr, "[SpouseOrganic] FAIL frame %d: no two founding couples to widow\n",
                         frameNum);
            std::fflush(stderr);
            s_organicStage = 3;
            return;
        }
        wifeOfA->HandleLifeEvent(3, nullptr);                      // 0x140116dd0
        husbandOfB->HandleLifeEvent(3, nullptr);
        const float w = s_widower->description->_f4c;
        s_widow->age = s_widower->age + w + 1.0f;
        std::fprintf(stderr, "[SpouseOrganic] frame %d: widowed %p (gender 1, age %.2f) and %p "
                     "(gender 0, age %.2f -- set to the window %.2f + 1 above him); spouses now %p / %p\n",
                     frameNum, static_cast<void*>(s_widower), s_widower->age,
                     static_cast<void*>(s_widow), s_widow->age, w,
                     static_cast<void*>(s_widower->spouse.Get()), static_cast<void*>(s_widow->spouse.Get()));
        s_organicStage = 1;
        s_organicFrame = frameNum;
    } else if (s_organicStage == 1) {
        if (s_widower->spouse != nullptr || s_widow->spouse != nullptr) {
            std::fprintf(stderr, "[SpouseOrganic] FAIL frame %d: married beyond the window (spouses "
                         "%p / %p, ages %.2f / %.2f)\n", frameNum,
                         static_cast<void*>(s_widower->spouse.Get()), static_cast<void*>(s_widow->spouse.Get()),
                         s_widower->age, s_widow->age);
            s_organicStage = 3;
        } else if (frameNum - s_organicFrame >= 1800) {
            const float w = s_widower->description->_f4c;
            std::fprintf(stderr, "[SpouseOrganic] beyond ok frame %d: no marriage in %d frames at a gap "
                         "of %.2f (window %.2f); setting her age inside it\n", frameNum,
                         frameNum - s_organicFrame, s_widow->age - s_widower->age, w);
            s_widow->age = s_widower->age + w * 0.5f;
            s_organicStage = 2;
            s_organicFrame = frameNum;
        }
    } else if (s_organicStage == 2) {
        if (s_widower->spouse.Get() == s_widow && s_widow->spouse.Get() == s_widower) {
            std::fprintf(stderr, "[SpouseOrganic] PASS frame %d: married inside the window after %d "
                         "frames (ages %.2f / %.2f)\n", frameNum, frameNum - s_organicFrame,
                         s_widower->age, s_widow->age);
            s_organicStage = 3;
        } else if (frameNum - s_organicFrame >= 1800) {
            std::fprintf(stderr, "[SpouseOrganic] FAIL frame %d: not married inside the window "
                         "(spouses %p / %p)\n", frameNum, static_cast<void*>(s_widower->spouse.Get()),
                         static_cast<void*>(s_widow->spouse.Get()));
            s_organicStage = 3;
        }
    }
    std::fflush(stderr);
}

// ---- Nomad harness (BANISHED_NOMAD_HARNESS=<period>) ------------------------------
// Drives the town hall's ImmigrationComponent state machine (immigrationcomponent_states.cpp)
// end to end in a test-length run. Its one lever is the wait: on every poll it cuts the
// 24..72-month WAITING countdown (+0x48) to one tick, so TickWaiting fires in the next
// arrival month. Everything after that is the game's own code: CanAccept, Spawn at a map
// edge, the nomads' walk to the hall (event 6 each), the answer state 0x11. The harness
// then answers as the Nomads tab's buttons do, through the town hall's
// StateMachineComponent node: ALLOW (event 7) for the first group, DENY (event 8) for the
// second. Checked on the next poll: ALLOW adds the group to the admitted counter
// (townMgr+0x1b0) and to the town list, every new citizen with a profession; DENY adds
// nobody. Both answers clear the group (+0x58 / +0x60).
// Needs a town CanAccept passes: population >= desc+0x28 (8 in the shipped data) and the
// required buildings, and a town hall to carry the ImmigrationComponent: the stress town is
// the one town that founds one (tools/harness_route.sh immigration).
ImmigrationComponent* s_hall = nullptr;
int  s_answered     = 0;      // groups answered so far (0: none, 1: allowed, 2: denied)
int  s_arrivedPolls = 0;      // consecutive polls with the whole group at the hall
int  s_answerGroup  = 0;      // the group size the last answer was given for
int  s_admittedBefore = 0;    // townMgr+0x1b0 before the last answer
std::unordered_set<const CitizenComponent*> s_beforeAnswer;
bool s_verifyPending = false;

ImmigrationComponent* FindTownHall(GameUpdateLoop* gw) {
    EntityList* el = gw->m_entityList;                            // +0xe8
    Object::InternalPtr<ComponentEntity>* arr = el->masterArray._data;
    const int n = el->masterArray._size;
    for (int i = 0; i < n; i++) {
        ComponentEntity* ent = arr[i].Get();
        if (!ent) continue;
        if (ImmigrationComponent* imm = ent->FindComponent<ImmigrationComponent>()) return imm;
    }
    return nullptr;
}

void PostHallEvent(ImmigrationComponent* imm, int eventId) {
    StateMachineComponent* smc = imm->_entity->FindComponent<StateMachineComponent>();
    StateMachine* sm = &smc->stateMachine;
    sm->current->PostEvent(sm, sm->activeState, eventId);
}

void VerifyAnswer(int frameNum, ImmigrationComponent* imm, CitizenBehavior* townMgr) {
    s_verifyPending = false;
    const int admitted = townMgr->field_0x1B0 - s_admittedBefore;
    int added = 0, nullProfession = 0;
    for (int i = 0; i < townMgr->citizens._size; i++) {
        CitizenComponent* c = townMgr->citizens._data[i].Get();
        if (!c || s_beforeAnswer.count(c) != 0) continue;
        added++;
        if (c->profession == nullptr) nullProfession++;           // +0x1f0
    }
    bool ok;
    if (s_answered == 1) {
        ok = imm->group._size == 0 && admitted == s_answerGroup && added >= s_answerGroup &&
             nullProfession == 0;
        std::fprintf(stderr, "[NomadHarness] %s frame %d: ALLOW of %d -> admitted(+0x1b0) +%d, "
                     "%d new citizen(s), %d with a null profession, group now %d\n",
                     ok ? "allow ok" : "FAIL", frameNum, s_answerGroup, admitted, added,
                     nullProfession, imm->group._size);
    } else {
        ok = imm->group._size == 0 && admitted == 0;
        std::fprintf(stderr, "[NomadHarness] %s frame %d: DENY of %d -> admitted(+0x1b0) +%d, "
                     "%d new citizen(s) (births count here too), group now %d\n",
                     ok ? "deny ok" : "FAIL", frameNum, s_answerGroup, admitted, added,
                     imm->group._size);
        if (ok)
            std::fprintf(stderr, "[NomadHarness] PASS frame %d: one group allowed, one denied\n",
                         frameNum);
    }
    if (!ok) s_answered = 3;                                      // stop driving
    std::fflush(stderr);
}

void NomadHarness(int frameNum) {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw || !gw->m_citizenList || !gw->m_entityList) return;
    if (s_answered >= 3) return;
    CitizenBehavior* townMgr = gw->m_citizenList;
    if (!s_hall) {
        s_hall = FindTownHall(gw);
        if (!s_hall) {
            std::fprintf(stderr, "[NomadHarness] frame %d: no town hall in the world yet\n", frameNum);
            std::fflush(stderr);
            return;
        }
    }
    ImmigrationComponent* imm = s_hall;
    const ImmigrationDescription* desc = static_cast<const ImmigrationDescription*>(imm->_description);
    StateMachineComponent* smc = imm->_entity->FindComponent<StateMachineComponent>();
    const float secs = gw->m_dayCounter->yearTime.AsSeconds();   // +0x128
    const int month = static_cast<int>((secs / 3600.0f) * 12.0f);
    if (s_verifyPending) VerifyAnswer(frameNum, imm, townMgr);
    if (s_answered >= 2) return;
    const bool arrived = imm->group._size > 0 && imm->arrivedCount == imm->group._size;
    std::fprintf(stderr, "[NomadHarness] frame %d: month %d (arrive %d..%d, leave %d) pop %d "
                 "(min %d) canAccept %d state[%d] pending %d wait(+0x48) %lld group %d arrived %d\n",
                 frameNum, month, desc->_arrivalMonthFirst, desc->_arrivalMonthLast, desc->_leaveMonth,
                 townMgr->citizens._size, desc->_minPopulation, imm->CanAccept() ? 1 : 0,
                 smc->stateMachine.activeState, smc->stateMachine.pendingState,
                 static_cast<long long>(imm->waitTimer._ticks), imm->group._size, imm->arrivedCount);
    if (imm->group._size == 0 && imm->waitTimer._ticks > 1) {
        imm->waitTimer._ticks = 1;
        std::fprintf(stderr, "[NomadHarness]   cut the wait to one tick\n");
    }
    // The answer goes in once the whole group has stood at the hall for a full poll, so
    // the 0x10 -> 0x11 transition OnArrived requested has run.
    s_arrivedPolls = arrived ? s_arrivedPolls + 1 : 0;
    if (s_arrivedPolls >= 2 && smc->stateMachine.pendingState < 0) {
        s_beforeAnswer.clear();
        for (int i = 0; i < townMgr->citizens._size; i++)
            s_beforeAnswer.insert(townMgr->citizens._data[i].Get());
        s_answerGroup = imm->group._size;
        s_admittedBefore = townMgr->field_0x1B0;
        s_answered++;
        const int eventId = (s_answered == 1) ? 7 : 8;
        std::fprintf(stderr, "[NomadHarness] frame %d: group of %d at the hall -> %s (event %d)\n",
                     frameNum, s_answerGroup, eventId == 7 ? "ALLOW" : "DENY", eventId);
        PostHallEvent(imm, eventId);
        s_arrivedPolls = 0;
        s_verifyPending = true;
    }
    std::fflush(stderr);
}
}  // namespace

extern "C" void CitizenDeathHarness_Tick(int frameNum) {
    Init();

    if (s_spouseProbe > 0 && !s_firedProbe && frameNum >= s_spouseProbe) {
        s_firedProbe = true;
        SpouseProbe(frameNum);
    }
    if (s_firedProbe && s_organicStage < 3 && frameNum >= s_spouseProbe + 600 && (frameNum % 60) == 0)
        SpouseOrganic(frameNum);

    if (s_nomadPeriod > 0 && frameNum > 0 && (frameNum % s_nomadPeriod) == 0)
        NomadHarness(frameNum);

    if (s_birthCheckFrame >= 0 && frameNum >= s_birthCheckFrame) {
        s_birthCheckFrame = -1;
        CheckNewborns(frameNum);
    }

    if (s_forceBirth > 0 && !s_firedBirth && frameNum >= s_forceBirthFrame) {
        s_firedBirth = true;
        ForceBirths(frameNum);
    }

    if (s_censusEvery > 0 && frameNum > 0 && (frameNum % s_censusEvery) == 0)
        Census(frameNum);

    // Poll until enough citizens have aged past the marriage age, then move them out once.
    if (s_marryHarness > 0 && !s_firedMarry && frameNum > 600 && (frameNum % 600) == 0)
        MoveOutGrownChildren(frameNum);

    // Targeted kill: wait for family links to exist, then take those citizens.
    if (s_killFamily > 0 && !s_firedFamily && frameNum > 300 && (frameNum % 300) == 0) {
        Object::InternalPtr<CitizenComponent>* list; int n; CitizenBehavior* townMgr;
        if (TownList(&list, &n, &townMgr)) {
            CitizenComponent* victims[16];
            int want = 0;
            for (int i = 0; i < n && want < s_killFamily && want < 16; i++)
                if (HasFamily(list[i].Get())) victims[want++] = list[i].Get();
            if (want > 0) {
                s_firedFamily = true;
                std::fprintf(stderr, "[DeathHarness] frame %d: %d citizen(s) WITH family "
                             "links found — killing them (list=%d entries)\n",
                             frameNum, want, n);
                std::fflush(stderr);
                for (int i = 0; i < want; i++) {
                    CitizenComponent* v = victims[i];
                    std::fprintf(stderr, "[DeathHarness] killing family citizen comp=%p "
                                 "spouse=%p ring=%p\n", static_cast<void*>(v),
                                 static_cast<void*>(v->spouse.Get()),          // +0x250
                                 static_cast<void*>(v->_firstChild.Get()));    // +0x260
                    std::fflush(stderr);
                    v->HandleLifeEvent(3, nullptr);                      // 0x140116dd0
                }
                std::fprintf(stderr, "[DeathHarness] family kill done — list = %d entries "
                             "(was %d)\n", townMgr->citizens._size, n);    // +0xa8
                std::fflush(stderr);
            }
        }
    }

    if (s_count <= 0 || s_fired || frameNum < s_atFrame) return;

    // BANISHED_KILL_WHEN_CEMETERY_READY=1 -- hold the kill until the town actually has a
    // graveyard that can accept a burial, instead of racing a fixed frame number.
    //
    // Needed because the ONLY way to get a cemetery with a usable plot array is to build
    // one with the real tool: the plot grid is derived from the MultiObjectComponent's
    // child entities (the gate tile), and the ringPlace demo spawner creates a bare
    // entity with no children, so its plot array stays empty forever. A fixed
    // BANISHED_KILL_AT_FRAME therefore fires either before the player has placed one or
    // long after, and a "nobody was buried" result proves nothing either way.
    //
    // Ready = at least one cemetery in the town list at townMgr+0x40 (count +0x48) whose
    // own plot count (cemComp+0x50) is non-zero.
    {
        static int s_waitCemetery = -1;
        if (s_waitCemetery < 0) {
            const char* e = Opt().killWhenCemeteryReady;
            s_waitCemetery = (e && e[0] && std::strcmp(e, "0") != 0) ? 1 : 0;
            if (s_waitCemetery)
                std::fprintf(stderr, "[DeathHarness] kill DEFERRED until a cemetery with a "
                             "non-empty plot array exists (build one with the real tool)\n"),
                std::fflush(stderr);
        }
        if (s_waitCemetery) {
            CitizenBehavior* tm = nullptr; Object::InternalPtr<CitizenComponent>* l; int c;
            int readyPlots = 0, cemN = 0;
            if (TownList(&l, &c, &tm) && tm) {
                cemN = tm->cemeteries._size;                        // +0x48
                Object::InternalPtr<CemeteryComponent>* arr = tm->cemeteries._data;        // +0x40
                for (int i = 0; i < cemN && arr; ++i) {
                    CemeteryComponent* comp = arr[i].Get();
                    if (!comp) continue;
                    const int plots = comp->plots._size;          // +0x50
                    if (plots > readyPlots) readyPlots = plots;
                }
            }
            if (readyPlots <= 0) {
                static int s_nag = 0;
                if ((frameNum % 600) == 0 && s_nag < 20) { ++s_nag;
                    std::fprintf(stderr, "[DeathHarness] waiting: %d cemetery(ies) registered, "
                                 "best plot count = %d\n", cemN, readyPlots);
                    std::fflush(stderr);
                }
                return;
            }
            std::fprintf(stderr, "[DeathHarness] cemetery READY at frame %d: %d registered, "
                         "%d plots -- proceeding with the kill\n", frameNum, cemN, readyPlots);
            std::fflush(stderr);
        }
    }

    s_fired = true;

    Object::InternalPtr<CitizenComponent>* list; int n; CitizenBehavior* townMgr;
    if (!TownList(&list, &n, &townMgr)) return;
    std::fprintf(stderr, "[DeathHarness] frame %d: town citizen list = %d entries\n",
                 frameNum, n);

    // Snapshot first: each kill tears the victim out of this very list.
    //
    // This used to snapshot into `int64_t victims[64]` and clamp `want` to 64, so
    // BANISHED_KILL_CITIZENS=500 silently killed 64. That cap is invisible in the log
    // except as the "N/64" in the per-kill line, and it makes any test that needs a
    // LARGE death toll -- the Tombstone achievement wants 400 graves -- quietly
    // unreachable. Heap-allocate instead; the snapshot is transient and one qword per
    // citizen even for a whole town.
    int want = (s_count < n) ? s_count : n;
    std::vector<CitizenComponent*> victims((size_t)(want > 0 ? want : 0));
    for (int i = 0; i < want; i++) victims[(size_t)i] = list[i].Get();

    for (int i = 0; i < want; i++) {
        if (!victims[i]) continue;
        std::fprintf(stderr, "[DeathHarness] killing citizen %d/%d comp=%p\n",
                     i + 1, want, static_cast<void*>(victims[i]));
        std::fflush(stderr);
        victims[i]->HandleLifeEvent(3, nullptr);           // 0x140116dd0
    }

    std::fprintf(stderr, "[DeathHarness] done — town citizen list = %d entries "
                 "(was %d; expect %d if OnRemove is live, unchanged if it is not)\n",
                 townMgr->citizens._size, n, n - want);      // +0xa8
    std::fflush(stderr);
}
