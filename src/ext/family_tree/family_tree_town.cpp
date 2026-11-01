// src/ext/family_tree/family_tree_town.cpp -- NOT RETAIL. ext.family_tree (class C): the game side. The seams
// that write births, marriages and deaths into the tree, the binding of the tree's living people to the
// town's citizens after a load, and the sweep that catches what the seams do not see. See family_tree.h and
// docs/extensions/handins/FAMILYTREE.md.
#include "ext/family_tree/family_tree.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Game/Calendar.h"
#include "Game/GameUpdateLoop.h"
#include "Game/PlayerToolManager.h"
#include "Game/Profession.h"
#include "Game/ProfessionList.h"
#include "ui/StringTable.h"
#include "ext/registry.h"   // Ext::Ext().family_tree (the game's locked value)

namespace Ext {
namespace FamilyTree {

namespace {

constexpr float kAgeToYears = 5.0f;     // the citizen panel's age: (int)(age * 5), citizenui.cpp
constexpr int kSweepFrames = 90;        // a sweep every this many frames
// more game time than this between two sweeps is a jump (a load), not credited
constexpr double kMaxJobStepMonths = 2.0;

std::unordered_map<const CitizenComponent*, uint32_t> s_idOf;   // live citizen -> person
std::unordered_map<uint32_t, CitizenComponent*> s_citizen;      // person -> live citizen
std::unordered_map<const CitizenComponent*, uint8_t> s_cause;   // the death event a citizen was last given
bool s_needBind = false;
int s_countdown = 0;
bool s_clockSeen = false;               // s_clock is the game clock at the last sweep
double s_clock = 0.0;

std::string NameOf(const CitizenComponent* c) {
    return c->name._data != nullptr ? std::string(c->name._data) : std::string();
}

int AgeYears(const CitizenComponent* c) {
    const int y = static_cast<int>(c->age * kAgeToYears);
    return y < 0 ? 0 : y;
}

uint32_t IdOf(const CitizenComponent* c) {
    if (c == nullptr) return 0;
    const auto it = s_idOf.find(c);
    return it == s_idOf.end() ? 0 : it->second;
}

void Link(uint32_t id, CitizenComponent* c) {
    s_idOf[c] = id;
    s_citizen[id] = c;
}

void Unlink(const CitizenComponent* c) {
    const auto it = s_idOf.find(c);
    if (it == s_idOf.end()) return;
    s_citizen.erase(it->second);
    s_idOf.erase(it);
}

void ForgetAll() {
    s_idOf.clear();
    s_citizen.clear();
    s_cause.clear();
    s_clockSeen = false;
}

void ParentsOf(CitizenComponent* parent, uint32_t& mother, uint32_t& father) {
    mother = 0;
    father = 0;
    const uint32_t pid = IdOf(parent);
    if (pid == 0) return;
    CitizenComponent* sp = parent->spouse.Get();
    const uint32_t sid = IdOf(sp);
    if (parent->gender == 0) {
        mother = pid;
        father = sid;
    } else {
        father = pid;
        mother = sid;
    }
}

// A person for a citizen the tree has not seen: one that was there before the extension was on, or whose
// creation no seam saw. It starts as an arrival dated now, with the parents the live links still name.
uint32_t Ensure(CitizenComponent* c, int depth = 0) {
    if (c == nullptr) return 0;
    if (const uint32_t known = IdOf(c)) return known;
    uint32_t mother = 0, father = 0;
    CitizenComponent* par = c->_parent.Get();
    if (par != nullptr && depth < 8) {
        Ensure(par, depth + 1);
        ParentsOf(par, mother, father);
    }
    const uint32_t id = Town().Add(NameOf(c), c->gender == 0 ? 0 : 1, false, NowStamp(), AgeYears(c), mother,
                                   father);
    if (id != 0) Link(id, c);
    return id;
}

int32_t LiveHint(const Person& p) {
    const auto it = s_citizen.find(p.id);
    if (it == s_citizen.end() || it->second == nullptr) return -1;
    ComponentEntity* e = it->second->_entity.Get();
    return e != nullptr ? e->_entityId : -1;
}

// After a load: find each living person's citizen again. First by the entity index the save recorded,
// accepted only when the name and the gender agree; then, for the rest, by name and gender when exactly one
// person and one citizen are left with them. A person with no citizen is no longer in the town: recorded as
// gone.
void Bind() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_citizenList == nullptr) return;
    ForgetAll();
    Tree& tree = Town();
    std::vector<CitizenComponent*> citizens;
    const auto& list = g->m_citizenList->citizens;
    for (int i = 0; i < list._size; ++i) {
        CitizenComponent* c = list._data[i].Get();
        if (c != nullptr) citizens.push_back(c);
    }
    std::unordered_map<int32_t, CitizenComponent*> byEntity;
    for (CitizenComponent* c : citizens) {
        ComponentEntity* e = c->_entity.Get();
        if (e != nullptr) byEntity[e->_entityId] = c;
    }
    std::vector<uint32_t> loose;
    for (const Person& p : tree.People()) {
        if (!p.Alive()) continue;
        const auto it = byEntity.find(p.live);
        if (p.live >= 0 && it != byEntity.end() && IdOf(it->second) == 0 && NameOf(it->second) == p.name &&
            (it->second->gender == 0 ? 0 : 1) == p.gender) {
            Link(p.id, it->second);
        } else {
            loose.push_back(p.id);
        }
    }
    std::vector<CitizenComponent*> unbound;
    for (CitizenComponent* c : citizens)
        if (IdOf(c) == 0) unbound.push_back(c);
    std::vector<uint32_t> gone;
    for (const uint32_t id : loose) {
        const Person* p = tree.Find(id);
        int people = 0, matches = 0;
        CitizenComponent* match = nullptr;
        for (const uint32_t other : loose) {
            const Person* q = tree.Find(other);
            if (q->name == p->name && q->gender == p->gender) ++people;
        }
        for (CitizenComponent* c : unbound) {
            if (NameOf(c) == p->name && (c->gender == 0 ? 0 : 1) == p->gender) {
                ++matches;
                match = c;
            }
        }
        if (people == 1 && matches == 1) {
            Link(id, match);
            unbound.erase(std::remove(unbound.begin(), unbound.end(), match), unbound.end());
        } else {
            gone.push_back(id);
        }
    }
    const int32_t now = NowStamp();
    for (const uint32_t id : gone) {
        if (s_citizen.count(id) == 0) tree.Died(id, now, 0, kOther);
    }
}

// The game clock in months (year * 12 + the month so far, with its fraction); -1 when there is no game.
double ClockMonths() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_dayCounter == nullptr) return -1.0;
    const Calendar* cal = g->m_dayCounter;
    const double length = cal->yearLength.AsSeconds();
    if (!(length > 0.0)) return -1.0;
    return cal->year * 12.0 + cal->yearTime.AsSeconds() / length * 12.0;
}

// What the citizen panel titles a citizen: a child is a child, one at school a student, and everyone else has
// their profession (Laborer when none was given). Null when there is none.
const Profession* TitleOf(const CitizenComponent* c, const ProfessionList* list) {
    if (c == nullptr || list == nullptr) return nullptr;
    const bool child = c->description != nullptr && c->age < c->description->adultAge;
    if (child || c->school != nullptr)
        return c->school == nullptr ? list->childProfession : list->studentProfession;
    return c->profession;
}

// The marriages and the parents the live links show and the tree does not. Runs every kSweepFrames frames.
void Sweep() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_citizenList == nullptr) return;
    Tree& tree = Town();
    const auto& list = g->m_citizenList->citizens;
    std::vector<CitizenComponent*> citizens;
    for (int i = 0; i < list._size; ++i) {
        CitizenComponent* c = list._data[i].Get();
        if (c != nullptr) citizens.push_back(c);
    }
    for (CitizenComponent* c : citizens) Ensure(c);
    const int32_t now = NowStamp();
    // the game time since the last sweep, for the jobs: none while paused, and none across a jump of the
    // clock
    const double clock = ClockMonths();
    double elapsed = 0.0;
    if (clock >= 0.0 && s_clockSeen && clock > s_clock
        && clock - s_clock <= kMaxJobStepMonths) elapsed = clock - s_clock;
    s_clockSeen = clock >= 0.0;
    s_clock = clock;
    const ProfessionList* professions = g->m_professionList;
    for (CitizenComponent* c : citizens) {
        const uint32_t id = IdOf(c);
        if (id == 0) continue;
        CitizenComponent* sp = c->spouse.Get();
        const uint32_t sid = IdOf(sp);
        if (sid != 0 && sp->spouse.Get() == c) tree.Marry(id, sid, now);
        if (elapsed > 0.0) {
            const Profession* title = TitleOf(c, professions);
            if (title != nullptr && title->stringName._data != nullptr) {
                const uint8_t job = tree.JobIndex(title->stringName._data);
                if (job != kNoJob) tree.AddJobTime(id, job, static_cast<float>(elapsed));
            }
        }
        Person* me = tree.FindMutable(id);
        CitizenComponent* par = c->_parent.Get();
        if (me != nullptr && par != nullptr && me->mother == 0 && me->father == 0) {
            uint32_t m = 0, f = 0;
            ParentsOf(par, m, f);
            if (tree.Find(m) != nullptr && m != id) me->mother = m;
            if (tree.Find(f) != nullptr && f != id) me->father = f;
        }
    }
}

}  // namespace

int32_t NowStamp() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_dayCounter == nullptr) return -1;
    Calendar* cal = g->m_dayCounter;
    // EventLog::SeasonString's month
    int month = static_cast<int>(cal->yearTime.AsSeconds() / 3600.0f * 12.0f);
    if (month < 0) month = 0;
    if (month > 11) month = 11;
    return cal->year * 12 + month;
}

void BeginFrame() {
    const bool was = g_on;
    g_on = ::Ext::Ext().family_tree;
    if (!g_on) {
        if (was) {
            ForgetAll();
            CitizenButtonsForget();
        }
        return;
    }
    if (!was) {
        g_liveHint = LiveHint;
        g_onTreeReplaced = [] { ForgetAll(); s_needBind = true; };
        s_needBind = true;
    }
    if (s_needBind) {
        s_needBind = false;
        Bind();
        Sweep();
        s_countdown = kSweepFrames;
        return;
    }
    if (--s_countdown <= 0) {
        s_countdown = kSweepFrames;
        Sweep();
    }
}

void OnCreatedSlow(CitizenComponent* citizen, CitizenComponent* parent) {
    if (citizen == nullptr || IdOf(citizen) != 0) return;
    uint32_t mother = 0, father = 0;
    const bool born = parent != nullptr && citizen->age <= 0.0001f;
    if (parent != nullptr) {
        Ensure(parent);
        ParentsOf(parent, mother, father);
    }
    const uint32_t id = Town().Add(NameOf(citizen), citizen->gender == 0 ? 0 : 1, born, NowStamp(),
                                   born ? 0 : AgeYears(citizen), mother, father);
    if (id != 0) Link(id, citizen);
}

void OnMarriedSlow(CitizenComponent* a, CitizenComponent* b) {
    if (a == nullptr || b == nullptr) return;
    const uint32_t ia = Ensure(a);
    const uint32_t ib = Ensure(b);
    if (ia != 0 && ib != 0) Town().Marry(ia, ib, NowStamp());
}

void OnLifeEventSlow(CitizenComponent* citizen, int eventType) {
    uint8_t cause = kAlive;
    switch (eventType) {
        case 0: cause = kHunger; break;
        case 1: cause = kCold; break;
        case 2: cause = kOldAge; break;
        case 3: cause = kDisease; break;
        case 4: cause = kOrphaned; break;
        case 6: cause = kChildbirth; break;
        default: return;
    }
    if (citizen != nullptr) s_cause[citizen] = cause;
}

void OnRemovedSlow(CitizenComponent* citizen) {
    const uint32_t id = IdOf(citizen);
    if (id == 0) return;
    const auto it = s_cause.find(citizen);
    const uint8_t cause = it != s_cause.end() ? it->second : static_cast<uint8_t>(kOther);
    Town().Died(id, NowStamp(), AgeYears(citizen), cause);
    s_cause.erase(citizen);
    Unlink(citizen);
}

uint32_t PersonOfCitizen(const CitizenComponent* citizen) { return IdOf(citizen); }

bool CanShowInTown(uint32_t id) {
    const auto it = s_citizen.find(id);
    if (it == s_citizen.end() || it->second == nullptr || it->second->_entity.Get() == nullptr) return false;
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    return g != nullptr && g->m_toolManager != nullptr && g->m_playerController != nullptr &&
           g->m_game != nullptr;
}

bool ShowInTown(uint32_t id) {
    if (!CanShowInTown(id)) return false;
    ComponentEntity* entity = s_citizen[id]->_entity.Get();
    // no panel is open on a pin to keep: the camera moves and the citizen is selected
    UI::DialogInstance* noDialog = nullptr;
    GameUpdateLoop::_singleton->m_toolManager->CenterOnEntity(&noDialog, &entity);
    return true;
}

std::string JobText(const std::string& key) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    const ProfessionList* list = g != nullptr ? g->m_professionList : nullptr;
    if (list == nullptr) return key;
    std::vector<const Profession*> all;
    for (int i = 0; i < list->professions.GetSize(); ++i) all.push_back(list->professions.GetData()[i]);
    all.push_back(list->field_0x30);
    all.push_back(list->field_0x38);
    all.push_back(list->childProfession);
    all.push_back(list->studentProfession);
    for (const Profession* prof : all) {
        if (prof == nullptr || prof->stringName._data == nullptr || key != prof->stringName._data) continue;
        UI::StringTable* table = prof->stringTable;
        if (table == nullptr) break;
        const int32_t index = table->GetIndexFromName(prof->stringName);
        if (index < 0 || index >= table->GetCount()) break;
        const System::String text = table->GetText(index);
        if (text._data != nullptr && text._data[0] != 0) return text._data;
        break;
    }
    return key;
}

bool LiveViewOf(uint32_t id, LiveView& out) {
    const auto it = s_citizen.find(id);
    if (it == s_citizen.end() || it->second == nullptr) return false;
    const CitizenComponent* c = it->second;
    out.ageYears = AgeYears(c);
    out.hasHome = c->home.Get() != nullptr;
    out.hasWork = c->workplace.Get() != nullptr;
    return true;
}

}  // namespace FamilyTree
}  // namespace Ext
