// src/ext/family_tree/family_tree.cpp -- NOT RETAIL. ext.family_tree (class C): the table, its relations and
// the wire format. Standard C++ only. See family_tree.h and docs/extensions/handins/FAMILYTREE.md.
#include "ext/family_tree/family_tree.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "ext/savetail.h"

namespace Ext {
namespace FamilyTree {

bool g_on = false;
std::function<int32_t(const Person&)> g_liveHint;
std::function<void()> g_onTreeReplaced;

Tree& Town() {
    static Tree t;
    return t;
}

uint32_t Tree::Add(const std::string& name, uint8_t gender, bool bornHere, int32_t joined, int32_t joinedAge,
                   uint32_t mother, uint32_t father) {
    if (people_.size() >= kMaxPeople) return 0;
    Person p;
    p.id = nextId_++;
    p.name = name.size() > kMaxNameBytes ? name.substr(0, kMaxNameBytes) : name;
    p.gender = gender ? 1 : 0;
    p.bornHere = bornHere;
    p.joined = joined;
    p.joinedAge = joinedAge < 0 ? 0 : joinedAge;
    p.mother = Find(mother) ? mother : 0;
    p.father = Find(father) ? father : 0;
    people_.push_back(std::move(p));
    return people_.back().id;
}

const Person* Tree::Find(uint32_t id) const {
    if (id == 0) return nullptr;
    // ids ascend; the common case is id == index + 1
    if (id <= people_.size() && people_[id - 1].id == id) return &people_[id - 1];
    auto it = std::lower_bound(people_.begin(), people_.end(), id,
                               [](const Person& p, uint32_t v) { return p.id < v; });
    return it != people_.end() && it->id == id ? &*it : nullptr;
}

Person* Tree::FindMutable(uint32_t id) {
    return const_cast<Person*>(static_cast<const Tree*>(this)->Find(id));
}

bool Tree::Marry(uint32_t a, uint32_t b, int32_t when) {
    if (a == b) return false;
    Person* pa = FindMutable(a);
    Person* pb = FindMutable(b);
    if (pa == nullptr || pb == nullptr) return false;
    if (!pa->marriages.empty() && pa->marriages.back().spouse == b) return false;
    if (pa->marriages.size() >= kMaxMarriages || pb->marriages.size() >= kMaxMarriages) return false;
    pa->marriages.push_back(Marriage{b, when});
    pb->marriages.push_back(Marriage{a, when});
    return true;
}

bool Tree::Died(uint32_t id, int32_t when, int32_t ageYears, uint8_t cause) {
    Person* p = FindMutable(id);
    if (p == nullptr || !p->Alive()) return false;
    p->died = when < 0 ? 0 : when;
    p->ageAtDeath = ageYears < 0 ? 0 : ageYears;
    p->cause = cause == kAlive || cause >= kCauseCount ? static_cast<uint8_t>(kOther) : cause;
    p->live = -1;
    return true;
}

void Tree::Clear() {
    people_.clear();
    jobNames_.clear();
    nextId_ = 1;
}

uint8_t Tree::JobIndex(const std::string& name) {
    if (name.empty()) return kNoJob;
    const std::string key = name.size() > kMaxNameBytes ? name.substr(0, kMaxNameBytes) : name;
    for (size_t i = 0; i < jobNames_.size(); ++i)
        if (jobNames_[i] == key) return static_cast<uint8_t>(i);
    if (jobNames_.size() >= kMaxJobNames) return kNoJob;
    jobNames_.push_back(key);
    return static_cast<uint8_t>(jobNames_.size() - 1);
}

const std::string& Tree::JobName(uint8_t index) const {
    static const std::string none;
    return index < jobNames_.size() ? jobNames_[index] : none;
}

namespace {
// Whole units onto a person's job, in the table of at most kMaxJobsPerPerson.
void CreditUnits(Person& p, uint8_t job, uint32_t units) {
    if (units == 0) return;
    for (JobTime& j : p.jobs) {
        if (j.job == job) {
            j.units = static_cast<uint16_t>(std::min<uint32_t>(kMaxJobUnits, j.units + units));
            return;
        }
    }
    JobTime t;
    t.job = job;
    t.units = static_cast<uint16_t>(std::min<uint32_t>(kMaxJobUnits, units));
    if (p.jobs.size() < kMaxJobsPerPerson) {
        p.jobs.push_back(t);
        return;
    }
    size_t least = 0;
    for (size_t i = 1; i < p.jobs.size(); ++i)
        if (p.jobs[i].units < p.jobs[least].units) least = i;
    t.units = static_cast<uint16_t>(std::min<uint32_t>(kMaxJobUnits, t.units + p.jobs[least].units));
    p.jobs[least] = t;
}
}  // namespace

bool Tree::AddJobTime(uint32_t id, uint8_t job, float months) {
    Person* p = FindMutable(id);
    if (p == nullptr || job >= jobNames_.size() || !(months > 0.0f)) return false;
    if (p->carryJob != job) {
        // the job changed: what was seen of the last one counts, to the nearest unit
        if (p->carryJob < jobNames_.size())
            CreditUnits(*p, p->carryJob, static_cast<uint32_t>(p->carryMonths / kMonthsPerJobUnit + 0.5f));
        p->carryJob = job;
        p->carryMonths = 0.0f;
    }
    p->carryMonths += months;
    // the nudge keeps 12.0 months 120 units, not 119
    const float whole = p->carryMonths / kMonthsPerJobUnit + 0.001f;
    if (whole >= 1.0f) {
        const uint32_t units =
            whole >= static_cast<float>(kMaxJobUnits) ? kMaxJobUnits : static_cast<uint32_t>(whole);
        CreditUnits(*p, job, units);
        p->carryMonths -= static_cast<float>(units) * kMonthsPerJobUnit;
        if (p->carryMonths < 0.0f) p->carryMonths = 0.0f;
    }
    return true;
}

std::vector<JobTime> Tree::JobsOf(uint32_t id) const {
    const Person* p = Find(id);
    if (p == nullptr) return {};
    std::vector<JobTime> out = p->jobs;
    std::stable_sort(out.begin(), out.end(),
                     [](const JobTime& a, const JobTime& b) { return a.units > b.units; });
    return out;
}

namespace {
bool Before(const Person* a, const Person* b) {
    if (a->joined != b->joined) return a->joined < b->joined;
    return a->id < b->id;
}
std::vector<uint32_t> Ordered(std::vector<const Person*>& v) {
    std::sort(v.begin(), v.end(), Before);
    std::vector<uint32_t> out;
    out.reserve(v.size());
    for (const Person* p : v) out.push_back(p->id);
    return out;
}
}  // namespace

std::vector<uint32_t> Tree::Children(uint32_t id) const {
    std::vector<const Person*> v;
    if (id == 0) return {};
    for (const Person& p : people_)
        if (p.mother == id || p.father == id) v.push_back(&p);
    return Ordered(v);
}

std::vector<uint32_t> Tree::Siblings(uint32_t id) const {
    const Person* me = Find(id);
    if (me == nullptr) return {};
    std::vector<const Person*> v;
    for (const Person& p : people_) {
        if (p.id == id) continue;
        const bool viaMother = me->mother != 0 && p.mother == me->mother;
        const bool viaFather = me->father != 0 && p.father == me->father;
        if (viaMother || viaFather) v.push_back(&p);
    }
    return Ordered(v);
}

std::vector<uint32_t> Tree::Spouses(uint32_t id) const {
    std::vector<uint32_t> out;
    const Person* me = Find(id);
    if (me == nullptr) return out;
    for (const Marriage& m : me->marriages) out.push_back(m.spouse);
    return out;
}

std::vector<uint32_t> Tree::DescendantIds(uint32_t id) const {
    std::vector<uint32_t> out;
    if (Find(id) == nullptr) return out;
    // one pass over the table for who is whose child, then a walk down from `id`, a generation at a time
    std::unordered_map<uint32_t, std::vector<uint32_t>> below;
    for (const Person& p : people_) {
        if (p.mother != 0) below[p.mother].push_back(p.id);
        if (p.father != 0 && p.father != p.mother) below[p.father].push_back(p.id);
    }
    std::unordered_set<uint32_t> seen;
    seen.insert(id);
    std::vector<uint32_t> level{id};
    while (!level.empty()) {
        std::vector<uint32_t> next;
        for (const uint32_t at : level) {
            const auto it = below.find(at);
            if (it == below.end()) continue;
            for (const uint32_t child : it->second) {
                if (!seen.insert(child).second) continue;
                out.push_back(child);
                next.push_back(child);
            }
        }
        level.swap(next);
    }
    return out;
}

Tree::Descendants Tree::DescendantsOf(uint32_t id) const {
    Descendants out;
    if (Find(id) == nullptr) return out;
    out.children = Children(id).size();
    for (const uint32_t d : DescendantIds(id)) {
        ++out.total;
        const Person* p = Find(d);
        if (p != nullptr && p->Alive()) ++out.living;
    }
    return out;
}

bool Tree::FullSiblings(uint32_t a, uint32_t b) const {
    const Person* pa = Find(a);
    const Person* pb = Find(b);
    return pa != nullptr && pb != nullptr && a != b && pa->mother != 0 && pa->father != 0 &&
           pa->mother == pb->mother && pa->father == pb->father;
}

std::vector<uint32_t> Tree::LivingIds() const {
    std::vector<uint32_t> out;
    for (const Person& p : people_)
        if (p.Alive()) out.push_back(p.id);
    return out;
}

int Tree::SameNameOrdinal(uint32_t id) const {
    const Person* me = Find(id);
    if (me == nullptr) return 0;
    int n = 1;
    for (const Person& p : people_) {
        if (p.id >= id) break;
        if (p.name == me->name) ++n;
    }
    return n;
}

std::string Tree::JobSpanText(uint16_t units) {
    const double months = units * static_cast<double>(kMonthsPerJobUnit);
    if (months < 0.5) return "under 1 mo";
    if (months < 12.0) return std::to_string(static_cast<int>(months + 0.5)) + " mo";
    return std::to_string(static_cast<int>(months / 12.0 + 0.5)) + " y";
}

std::string Tree::JobsText(uint32_t id, size_t maxShown,
                           const std::function<std::string(const std::string&)>& nameOf) const {
    const std::vector<JobTime> jobs = JobsOf(id);
    std::string out;
    size_t shown = 0;
    for (const JobTime& j : jobs) {
        if (shown >= maxShown) break;
        const std::string& key = JobName(j.job);
        if (key.empty()) continue;
        if (!out.empty()) out += ", ";
        out += (nameOf ? nameOf(key) : key) + " " + JobSpanText(j.units);
        ++shown;
    }
    if (shown < jobs.size() && shown > 0) out += " and " + std::to_string(jobs.size() - shown) + " more";
    return out;
}

// ---- the wire format --------------------------------------------------------------------------------------
// u32 nextId; u32 count; per person:
//   u32 id; string name; u8 gender; u8 flags (bit 0 bornHere); i32 joined; i32 joinedAge; i32 died; i32
//   ageAtDeath; u8 cause; u32 mother; u32 father; i32 live; u8 marriageCount; per marriage: u32 spouse; i32
//   when; version 2: u8 jobCount; per job: u8 name index; u16 units
// then, version 2: u8 nameCount; per name: string (the job names the jobs point into)
namespace {
// an empty name, no marriages, no jobs
constexpr size_t kMinPersonBytes = 4 + 4 + 1 + 1 + 4 * 4 + 1 + 4 + 4 + 4 + 1;
}

void Tree::Encode(std::vector<uint8_t>& out, const std::function<int32_t(const Person&)>& hint,
                  int version) const {
    SaveTail::Writer w(out);
    w.U32(nextId_);
    w.U32(static_cast<uint32_t>(people_.size()));
    for (const Person& p : people_) {
        w.U32(p.id);
        w.String(p.name);
        out.push_back(p.gender);
        out.push_back(p.bornHere ? 1 : 0);
        w.I32(p.joined);
        w.I32(p.joinedAge);
        w.I32(p.died);
        w.I32(p.ageAtDeath);
        out.push_back(p.cause);
        w.U32(p.mother);
        w.U32(p.father);
        w.I32(p.Alive() ? (hint ? hint(p) : p.live) : -1);
        const size_t nm = std::min(p.marriages.size(), kMaxMarriages);
        out.push_back(static_cast<uint8_t>(nm));
        for (size_t i = 0; i < nm; ++i) {
            w.U32(p.marriages[i].spouse);
            w.I32(p.marriages[i].when);
        }
        if (version >= 2) {
            size_t nj = 0;
            for (const JobTime& j : p.jobs)
                if (j.units > 0 && j.job < jobNames_.size()) ++nj;
            out.push_back(static_cast<uint8_t>(nj));
            for (const JobTime& j : p.jobs) {
                if (j.units == 0 || j.job >= jobNames_.size()) continue;
                out.push_back(j.job);
                out.push_back(static_cast<uint8_t>(j.units & 0xff));
                out.push_back(static_cast<uint8_t>(j.units >> 8));
            }
        }
    }
    if (version >= 2) {
        out.push_back(static_cast<uint8_t>(jobNames_.size()));
        for (const std::string& n : jobNames_) w.String(n);
    }
}

bool Tree::Decode(const uint8_t* bytes, size_t size, int version) {
    Clear();
    if (version < 1 || version > kWireVersion) return false;
    SaveTail::Reader r(bytes, size);
    uint32_t nextId = 0, count = 0;
    const size_t minPerson = kMinPersonBytes + (version >= 2 ? 1 : 0);
    if (!r.U32(nextId) || !r.U32(count) || count > kMaxPeople || count > r.left / minPerson) {
        return false;
    }
    std::vector<Person> people;
    people.reserve(count);
    uint32_t last = 0;
    for (uint32_t i = 0; i < count; ++i) {
        Person p;
        const uint8_t* b = nullptr;
        if (!r.U32(p.id) || p.id == 0 || p.id <= last || p.id >= nextId) return false;
        last = p.id;
        if (!r.String(p.name) || p.name.size() > kMaxNameBytes) return false;
        if (!r.Bytes(2, b)) return false;
        if (b[0] > 1 || b[1] > 1) return false;
        p.gender = b[0];
        p.bornHere = b[1] == 1;
        if (!r.I32(p.joined) || !r.I32(p.joinedAge) || !r.I32(p.died) || !r.I32(p.ageAtDeath)) return false;
        if (!r.Bytes(1, b) || b[0] >= kCauseCount) return false;
        p.cause = b[0];
        if ((p.cause == kAlive) != (p.died == kNoDate)) return false;
        if (!r.U32(p.mother) || !r.U32(p.father) || !r.I32(p.live)) return false;
        if (!r.Bytes(1, b)) return false;
        const size_t nm = b[0];
        if (r.left / 8 < nm) return false;
        for (size_t k = 0; k < nm; ++k) {
            Marriage m{};
            if (!r.U32(m.spouse) || !r.I32(m.when)) return false;
            p.marriages.push_back(m);
        }
        if (version >= 2) {
            if (!r.Bytes(1, b)) return false;
            const size_t nj = b[0];
            if (nj > kMaxJobsPerPerson || r.left / 3 < nj) return false;
            for (size_t k = 0; k < nj; ++k) {
                if (!r.Bytes(3, b)) return false;
                JobTime j;
                j.job = b[0];
                j.units = static_cast<uint16_t>(b[1] | (b[2] << 8));
                if (j.units == 0) return false;
                for (const JobTime& other : p.jobs)
                    if (other.job == j.job) return false;
                p.jobs.push_back(j);
            }
        }
        people.push_back(std::move(p));
    }
    std::vector<std::string> names;
    if (version >= 2) {
        const uint8_t* b = nullptr;
        if (!r.Bytes(1, b)) return false;
        const size_t nn = b[0];
        for (size_t k = 0; k < nn; ++k) {
            std::string n;
            if (!r.String(n) || n.empty() || n.size() > kMaxNameBytes) return false;
            names.push_back(std::move(n));
        }
        for (const Person& p : people)
            for (const JobTime& j : p.jobs)
                if (j.job >= names.size()) return false;
    }
    if (!r.AtEnd()) return false;
    // references to people that are not there are dropped; a person is never their own parent or spouse
    std::unordered_set<uint32_t> ids;
    for (const Person& p : people) ids.insert(p.id);
    for (Person& p : people) {
        if (p.mother == p.id || !ids.count(p.mother)) p.mother = 0;
        if (p.father == p.id || !ids.count(p.father)) p.father = 0;
        p.marriages.erase(std::remove_if(p.marriages.begin(), p.marriages.end(),
                              [&](const Marriage& m) { return m.spouse == p.id || !ids.count(m.spouse); }),
                          p.marriages.end());
    }
    people_ = std::move(people);
    jobNames_ = std::move(names);
    nextId_ = nextId;
    return true;
}

namespace {
std::string LowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}
}  // namespace

bool NameMatches(const std::string& text, const std::string& query) {
    const std::string hay = LowerAscii(text);
    const std::string q = LowerAscii(query);
    size_t at = 0;
    while (at < q.size()) {
        while (at < q.size() && q[at] == ' ') ++at;
        if (at >= q.size()) break;
        size_t end = q.find(' ', at);
        if (end == std::string::npos) end = q.size();
        if (hay.find(q.substr(at, end - at)) == std::string::npos) return false;
        at = end;
    }
    return true;
}

const char* CauseText(uint8_t cause) {
    switch (cause) {
        case kHunger: return "hunger";
        case kCold: return "cold";
        case kOldAge: return "old age";
        case kDisease: return "disease";
        case kOrphaned: return "lost their parents";
        case kChildbirth: return "childbirth";
        case kOther: return "other";
        default: return "";
    }
}

namespace {
void EncodeState(std::vector<uint8_t>& payload) { Town().Encode(payload, g_liveHint); }
bool DecodeState(const uint8_t* bytes, size_t size, int32_t savedFormatVersion) {
    if (savedFormatVersion < 1 || savedFormatVersion > kWireVersion) return false;
    const bool ok = Town().Decode(bytes, size, savedFormatVersion);
    if (g_onTreeReplaced) g_onTreeReplaced();
    return ok;
}
void ResetState() {
    Town().Clear();
    if (g_onTreeReplaced) g_onTreeReplaced();
}
}  // namespace

const SaveTail::StateCodec& Codec() {
    static const SaveTail::StateCodec c = { kId, EncodeState, DecodeState, ResetState };
    return c;
}

}  // namespace FamilyTree
}  // namespace Ext
