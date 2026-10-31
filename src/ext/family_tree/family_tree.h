#pragma once
// ============================================================================
// src/ext/family_tree/family_tree.h -- ext.family_tree (class C, default OFF)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/FAMILYTREE.md. A record of who is related to whom in the town.
//
// WHY A RECORD. Retail keeps only the living links of a citizen: a spouse (pairwise), one parent and the
// children ring that hangs from that parent. A citizen who marries leaves their parent's ring; when a parent
// dies a surviving spouse inherits the children, otherwise the adult children are cut loose and the young
// ones die with them (citizen_onremove.cpp); a dead citizen's record is removed. So the game cannot say who a
// married citizen's parents were, or who a dead grandparent was. This extension writes the history down as it
// happens: a person is added when a citizen is created (a birth, with the mother and the father; or an
// arrival without parents), a marriage when the spouse links appear, a death when the citizen is removed.
//
// THE DATA. Tree is a plain table of Person (standard C++ only; tests/unit/ext_family_tree_test.cpp). An id
// is a number from 1 in the order people were added; it is the only identity a record has. Dates are month
// stamps (year * 12 + month, the game calendar's year and its twelve months), -1 for none.
//
// THE SAVE. The tree travels in the extension's own record in the save tail (class C): Encode / Decode below,
// wire format version 2 (version 1, without the jobs, still loads). A person who is alive also carries
// `live`, the index of their entity in the entity list at the moment of the save (a hint, -1 for none); the
// game side (family_tree_town.cpp) uses it after a load to find the citizen again and checks it against the
// name and the gender.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Ext { namespace SaveTail { struct StateCodec; } }
class CitizenComponent;
class CitizenUI;
namespace UI { class Element; class UserInterface; }

namespace Ext {
namespace FamilyTree {

constexpr const char* kId          = "family_tree";
constexpr const char* kPageCaption = "FamilyTreeTab";
constexpr const char* kPageRsc     = "Dialog/FamilyTreePage.rsc:pageFamilyTree";

constexpr int32_t kNoDate = -1;
constexpr uint32_t kMaxPeople = 2000000;
constexpr size_t kMaxNameBytes = 255;
constexpr size_t kMaxMarriages = 255;
constexpr int kWireVersion = 2;       // the registry row's formatVersion

// How a person's life ended. The first values follow the game's life events
// (CitizenComponent::HandleLifeEvent).
enum Cause : uint8_t {
    kAlive = 0,
    kHunger = 1,       // event 0
    kCold = 2,         // event 1
    kOldAge = 3,       // event 2
    kDisease = 4,      // event 3
    kOrphaned = 5,     // event 4: a child who died with their last parent
    kChildbirth = 6,   // event 6
    kOther = 7,        // anything else, and a citizen removed with no event (a fire, a tornado, a mod)
    kCauseCount = 8
};

struct Marriage {
    uint32_t spouse;
    int32_t when;
};

// Time spent in one job. `job` indexes the tree's table of job names (Tree::JobName); `units` counts tenths
// of a game month.
constexpr uint8_t kNoJob = 0xff;
constexpr size_t kMaxJobNames = 255;
constexpr size_t kMaxJobsPerPerson = 12;
constexpr uint16_t kMaxJobUnits = 65535;
constexpr float kMonthsPerJobUnit = 0.1f;
struct JobTime {
    uint8_t job = kNoJob;
    uint16_t units = 0;
};

struct Person {
    uint32_t id = 0;
    std::string name;
    uint8_t gender = 0;          // the game's: 0 female, 1 male
    bool bornHere = false;       // true: born in the town; false: arrived (founder or immigrant)
    int32_t joined = kNoDate;    // month stamp of the birth or of the arrival
    int32_t joinedAge = 0;       // age in years at that moment (0 for a birth)
    int32_t died = kNoDate;      // month stamp, kNoDate while alive
    int32_t ageAtDeath = 0;      // years, meaningful once died is set
    uint8_t cause = kAlive;
    uint32_t mother = 0;         // 0 unknown
    uint32_t father = 0;
    int32_t live = -1;           // entity list index when last saved (a hint); -1 none
    std::vector<Marriage> marriages;   // in order; the last is the current one while both are alive
    std::vector<JobTime> jobs;         // what they did with their time, in the order they first did it
    // Not saved: time seen in `carryJob` that is not yet a whole unit.
    uint8_t carryJob = kNoJob;
    float carryMonths = 0.0f;

    bool Alive() const { return died == kNoDate; }
};

class Tree {
public:
    // A new person. A parent id that is not in the tree is stored as 0. Returns 0 when the table is full.
    uint32_t Add(const std::string& name, uint8_t gender, bool bornHere, int32_t joined, int32_t joinedAge,
                 uint32_t mother, uint32_t father);
    // Both marry each other at `when`. False (and nothing changes) when `a` is already on record as married
    // to `b` as their latest marriage, when either is unknown, or when a is b.
    bool Marry(uint32_t a, uint32_t b, int32_t when);
    // The person dies. False when unknown or already dead.
    bool Died(uint32_t id, int32_t when, int32_t ageYears, uint8_t cause);

    const Person* Find(uint32_t id) const;
    Person* FindMutable(uint32_t id);
    const std::vector<Person>& People() const { return people_; }
    size_t Size() const { return people_.size(); }
    uint32_t NextId() const { return nextId_; }
    void Clear();

    // Relations. Each list is ordered by month stamp of joining, then id.
    std::vector<uint32_t> Children(uint32_t id) const;      // whose mother or father is id
    std::vector<uint32_t> Siblings(uint32_t id) const;      // share the mother or the father, not id itself
    std::vector<uint32_t> Spouses(uint32_t id) const;       // in marriage order
    // Everyone below a person: `children` have the person as mother or father, `total` counts children, their
    // children and so on (each person once), `living` is how many of those total are alive. Zeros for an
    // unknown id.
    struct Descendants {
        size_t children = 0;
        size_t total = 0;
        size_t living = 0;
    };
    Descendants DescendantsOf(uint32_t id) const;
    // The same people as ids, each once, nearest generations first; the person is not in the list.
    std::vector<uint32_t> DescendantIds(uint32_t id) const;
    // True when both parents are known and the same for both.
    bool FullSiblings(uint32_t a, uint32_t b) const;

    // The living, in id order.
    std::vector<uint32_t> LivingIds() const;
    // Names repeat ("Anna"), so the page shows a person's place among those with the same name: 1 for the
    // first of a name, 2 for the next, and so on. 0 for an unknown id.
    int SameNameOrdinal(uint32_t id) const;

    // Jobs. A job is named by its string table key ("Farmer"), the same in every save and every mod; the tree
    // keeps one table of the names it has seen and a person's jobs point into it. The index of a job name,
    // added when new; kNoJob when the table is full or the name is empty.
    uint8_t JobIndex(const std::string& name);
    // The name at an index; empty when there is none.
    const std::string& JobName(uint8_t index) const;
    size_t JobNameCount() const { return jobNames_.size(); }
    // Credits `months` of game time to `job` for a living or dead person. Time is kept in whole units
    // (kMonthsPerJobUnit); a part of a unit waits until the next call for the same job, or is rounded when
    // the job changes. When a person already has kMaxJobsPerPerson jobs, a new job takes the place of the one
    // with the least time (and its time). False when the person or the job is unknown, or `months` is not a
    // positive number.
    bool AddJobTime(uint32_t id, uint8_t job, float months);
    // A person's jobs, the most time first (ties: the one first done).
    std::vector<JobTime> JobsOf(uint32_t id) const;

    // The time as words: "12 y" from a year up, "5 mo" under a year, "under 1 mo" under a month. (A year is
    // twelve months.)
    static std::string JobSpanText(uint16_t units);
    // "Farmer 12 y, Woodcutter 4 y, Child 8 y": a person's jobs, the most time first, at most `maxShown` and
    // then "and 2 more". `nameOf` turns a stored job name into the words to show (null: the stored name).
    // Empty when there are none.
    std::string JobsText(uint32_t id, size_t maxShown,
                         const std::function<std::string(const std::string&)>& nameOf = nullptr) const;

    // The wire format (version 2; version 1 had no jobs). `hint` gives the `live` of a living person at the
    // moment of encoding (null: every living person keeps the live they have). Decode replaces the tree;
    // false (and an empty tree) when the payload is malformed. `version` is the format of the payload; Encode
    // can write the old one for the tests.
    void Encode(std::vector<uint8_t>& out, const std::function<int32_t(const Person&)>& hint = nullptr,
                int version = kWireVersion) const;
    bool Decode(const uint8_t* bytes, size_t size, int version = kWireVersion);

private:
    std::vector<Person> people_;       // ids ascend
    std::vector<std::string> jobNames_;
    uint32_t nextId_ = 1;
};

// The extension's one tree (the game side and the page both use it).
Tree& Town();
// Set by the game side: the `live` of a living person while the save is encoded (null: keep what they have).
extern std::function<int32_t(const Person&)> g_liveHint;
// Set by the game side: run after the tree was replaced by a load or emptied by a reset.
extern std::function<void()> g_onTreeReplaced;

// True when every word of `query` (split on spaces, upper and lower case alike) occurs in `text`; an empty
// query (or one of spaces) matches everything. The Family page's search box: `text` is a person's name with
// its number ("Anna (2)").
bool NameMatches(const std::string& text, const std::string& query);

// an English word, for the log and the tests; the page uses the string table
const char* CauseText(uint8_t cause);

const SaveTail::StateCodec& Codec();

// ---- the game side (family_tree_town.cpp) -----------------------------------------------------------------
// SELECTION (DESIGN.md hook rule 4): BeginFrame reads the row once per frame; each seam is one test of g_on.
extern bool g_on;                // this frame: ext.family_tree (the game's locked value)
// GameUpdateLoop::Update, once per frame: binds after a load, sweeps now and then
void BeginFrame();

void OnCreatedSlow(CitizenComponent* citizen, CitizenComponent* parent);
void OnMarriedSlow(CitizenComponent* a, CitizenComponent* b);
void OnLifeEventSlow(CitizenComponent* citizen, int eventType);
void OnRemovedSlow(CitizenComponent* citizen);
// CitizenComponent::Initialize, at its end: a birth (parent set) or an arrival.
inline void OnCreated(CitizenComponent* citizen,
                      CitizenComponent* parent) { if (g_on) OnCreatedSlow(citizen, parent); }
// Two citizens become married.
inline void OnMarried(CitizenComponent* a, CitizenComponent* b) { if (g_on) OnMarriedSlow(a, b); }
// CitizenComponent::HandleLifeEvent, on entry: the event that may end this citizen.
inline void OnLifeEvent(CitizenComponent* citizen,
                        int eventType) { if (g_on) OnLifeEventSlow(citizen, eventType); }
// CitizenComponent::OnRemove, on entry: the citizen leaves the town (a death).
inline void OnRemoved(CitizenComponent* citizen) { if (g_on) OnRemovedSlow(citizen); }

// The camera goes to a living person and their panel opens (the game's own CenterOnEntity, as the citizen
// panel's home and work buttons use it). CanShowInTown: false when the person is not a citizen living in the
// town now.
bool CanShowInTown(uint32_t id);
bool ShowInTown(uint32_t id);

// The page's view of a living person: false when there is no live citizen bound to the id.
struct LiveView {
    int ageYears = 0;
    bool hasHome = false;
    bool hasWork = false;
};
bool LiveViewOf(uint32_t id, LiveView& out);
// The person bound to a living citizen; 0 when none.
uint32_t PersonOfCitizen(const CitizenComponent* citizen);
// A job's name as the game shows it (its string table's text), the key itself when the game has no such
// profession.
std::string JobText(const std::string& key);

// ---- the citizen panel's Family button (citizen_button.cpp) -----------------------------------------------
// The panel (CitizenUI) gets one more button, left of Follow; pressing it opens the extension window on the
// Family page, with the drawn tree centered on that citizen. One EXT SEAM line per site in
// src/game/Dialog/citizenui.cpp, each a test of g_on.
void CitizenPanelOpenedSlow(CitizenUI* panel);            // OnCreated, at its end: adds the button
void CitizenPanelLayoutSlow(CitizenUI* panel);            // OnUpdateLayout, on entry: keeps it left of Follow
// OnElementEvent: true when the click was the button's
bool CitizenPanelEventSlow(CitizenUI* panel, int32_t id);
void CitizenPanelClosedSlow(CitizenUI* panel);            // OnClosed: forgets it
inline void CitizenPanelOpened(CitizenUI* panel) { if (g_on) CitizenPanelOpenedSlow(panel); }
inline void CitizenPanelLayout(CitizenUI* panel) { if (g_on) CitizenPanelLayoutSlow(panel); }
inline bool CitizenPanelEvent(CitizenUI* panel, int32_t id) { return g_on
        && CitizenPanelEventSlow(panel, id); }
inline void CitizenPanelClosed(CitizenUI* panel) { if (g_on) CitizenPanelClosedSlow(panel); }

// Picks the person on the Family page (toolbar_page.cpp): the tree and the lists follow at the next tick, or
// when the page opens. False when the tree has no such person.
bool ShowPerson(uint32_t id);
// The picked person (the first living person when none is picked or the pick is gone), and a counter that
// changes whenever the pick does (the tree window redraws on it).
uint32_t SelectedPerson();
uint32_t SelectionStamp();
// A person's name as the page and the tree show it: a repeated name carries its number ("Anna (2)"); "?" for
// unknown.
std::string PersonLabel(uint32_t id);

// The town summary window (summary_window.cpp): opens it, or brings it to the front.
void OpenSummaryWindow(UI::UserInterface* ui);

// The tree window (tree_window.cpp): opens it, or brings it to the front. `UiOfElement`: the UserInterface an
// element of an open dialog belongs to (null when it is not in one).
void OpenTreeWindow(UI::UserInterface* ui);
UI::UserInterface* UiOfElement(UI::Element* e);
// The month stamp of now (year * 12 + month), -1 when there is no game.
int32_t NowStamp();

}  // namespace FamilyTree
}  // namespace Ext
