// ============================================================================
// src/ext/family_tree/toolbar_page.cpp -- ext.family_tree's page in the extension window
// ============================================================================
// docs/extensions/handins/FAMILYTREE.md, src/ext/toolbar/toolbar.h. Two lists. The first is the town's
// people: the living, by name, and after them the dead when "Show people who have died" is checked. Pressing
// a person picks them. The second is the picked person's family, one row each: their own dates, mother and
// father, the four grandparents, spouses, children and siblings. A related person's name is a button:
// pressing it picks that person, so the page walks up and down the tree.
//
// A control inside a cell is handled while the dialog is still dispatching ITS event, so it must not destroy
// cells (item_priority/toolbar_page.cpp). Picking a person changes what the family list holds, which destroys
// its cells, and the pick may come from a button inside that very list. So a pick is only written down
// (g_pending); the per-frame tick (Contribution::tick) applies it. The same tick rebuilds the lists when the
// tree has grown or someone has died.
//
// The page's objects are ours (src/ext/ui/Dialog/FamilyTreePage.rsc); the texts are in
// ExtensionsStringTable.rsc (the Family* keys). Element ids are the host's (toolbar.h): the page gets an
// `idBase` and answers local ids.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/Font.h"
#include "ui/ButtonElement.h"
#include "ui/EditElement.h"
#include "ui/CheckElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/RibbonElement.h"
#include "ui/TableElement.h"
#include "ui/StringTable.h"
#include "ui/String.h"
#include "ui/Dialog.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"

#include "Game/EventLog.h"
#include "Game/GameUpdateLoop.h"

#include "ext/family_tree/family_picture.h"
#include "ext/family_tree/family_raster.h"
#include "ext/family_tree/family_tree.h"
#include "ext/registry.h"
#include "ext/toolbar/toolbar.h"
#include "ext/town_screenshot/town_screenshot.h"
#include "ext/ui/find_element.h"
#include "ext/ui/list_width.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ext {
namespace FamilyTree {

namespace {

// Local element ids (the host adds `idBase`): the check box 10, a people row 100 + its position, a family row
// 5000 + its position.
constexpr int32_t kShowDeadId = 10;
constexpr int32_t kImageJobsId = 20;           // the picture's check box: the main job under each name
// the picture's check box: outline the picked person's descendants
constexpr int32_t kImageHighlightId = 18;
constexpr int32_t kShowInTownId = 19;          // the button that moves the camera to the picked person
constexpr int32_t kShowSummaryId = 17;         // the button that opens the town summary window
constexpr int32_t kShowTreeId = 13;            // the button that opens the tree window
constexpr int32_t kImageId = 14;               // the button that saves the whole-town picture (a PNG)
constexpr int32_t kImageSvgId = 16;            // the button that saves it as an SVG
constexpr int32_t kImageLivingId = 15;         // its check box: only the living and their ancestors
constexpr int32_t kPersonId = 100;
constexpr int32_t kRelatedId = 5000;
constexpr int32_t kMaxPeopleRows = kRelatedId - kPersonId;   // 4900
constexpr int32_t kMaxDetailRows = 400;
// the search box's fill (ARGB): the people rows use 0x10ffffff
constexpr uint32_t kSearchFill = 0x38ffffffu;
constexpr int kJobsRecheckFrames = 180;         // a person's time at their jobs grows while the page is open

constexpr const char* kWindowRsc = "Dialog/ExtensionsWindow.rsc";
constexpr const char* kPersonCellRsc = "Dialog/FamilyTreePage.rsc:ribbonFamilyPersonCell";
constexpr const char* kRelationCellRsc = "Dialog/FamilyTreePage.rsc:ribbonFamilyRelationCell";
UI::TextElement* AsText(UI::Element* e) {
    if (!::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId())) {
        return nullptr;
    }
    return static_cast<UI::TextElement*>(e);
}

void SetParameterText(UI::Element* e, const std::string& text) {
    if (UI::TextElement* t = AsText(e)) {
        t->SetParameter(0, System::String(text.c_str()));
    }
}

struct PersonCell {
    Object::InternalPtr<UI::Element> root;
    Object::InternalPtr<UI::Element> button;
    Object::InternalPtr<UI::Element> name;     // textFamilyPickName
    Object::InternalPtr<UI::Element> border;
};

struct RelationCell {
    Object::InternalPtr<UI::Element> root;
    Object::InternalPtr<UI::Element> relation;
    Object::InternalPtr<UI::Element> button;
    Object::InternalPtr<UI::Element> name;     // textFamilyRelatedName
    Object::InternalPtr<UI::Element> info;
};

struct DetailRow {
    std::string relation;
    uint32_t person = 0;       // 0: a note only
    std::string info;
};

std::vector<PersonCell> g_peopleCells;
std::vector<RelationCell> g_detailCells;
std::vector<uint32_t> g_people;                 // the person at each people row
std::vector<DetailRow> g_detail;                // the family rows of the picked person
std::unordered_map<uint32_t, std::string> g_label;   // a person's name, with its number when the name repeats
int32_t g_idBase = 0;
int32_t g_peopleWidth = 0;
int32_t g_detailWidth = 0;
uint32_t g_selected = 0;
// the tree's size when nobody was living: SelectedPerson's scan waits for it to grow
size_t g_noLivingAt = ~size_t(0);
uint32_t g_pending = 0;                         // a pick waiting for the tick
bool g_showDead = false;
uint32_t g_town = ~0u;                          // the lock epoch g_selected and g_showDead belong to
size_t g_seenSize = 0;                          // the tree when the lists were built
size_t g_seenLiving = 0;
bool g_open = false;
// the picture shows each person's main job (on by default; the check box turns it off)
bool g_imageJobs = true;
Object::InternalPtr<UI::Element> g_imageJobsCheck;
bool g_imageHighlight = false;                  // the picture outlines the picked person's descendants
Object::InternalPtr<UI::Element> g_inTownButton;
Object::InternalPtr<UI::Element> g_imageHighlightCheck;
int g_inTownShown = -1;                         // the button as last set: 1 showing, 0 hidden, -1 not set yet
bool g_imageLiving = false;                     // the picture leaves out the dead who are nobody's ancestor
std::string g_query;                            // what the search box holds, as the list was last built
Object::InternalPtr<UI::Element> g_search;
Object::InternalPtr<UI::Element> g_countText;     // "42 people": the people list as shown
std::string g_jobsShown;                        // the jobs row as the detail table shows it
int g_jobsCountdown = 0;                        // frames until the jobs row is looked at again
std::string g_imageStatus;                      // how the last picture went
Object::InternalPtr<UI::Element> g_imageCheck;
Object::InternalPtr<UI::Element> g_imageStatusText;
uint32_t g_selStamp = 0;                        // counts the changes of g_selected, for the tree window
uint32_t g_labelEpoch = ~0u;                    // the town and size g_label was built for
size_t g_labelSize = 0;
Object::InternalPtr<UI::Element> g_page;
Object::InternalPtr<UI::Element> g_showDeadCheck;
Object::InternalPtr<UI::Element> g_peopleTable;
Object::InternalPtr<UI::Element> g_detailTable;
Object::InternalPtr<UI::Element> g_title;

// The name of a calendar month (0..11), read from the game's own "seasons" table
// (Dialog/StringTable.rsc:seasons, keys Month0..Month11) that the status bar and the event log read, so a mod
// that renames the months renames them here too. Empty when the table is not there or the entry is blank.
std::string MonthName(int month) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_eventNotifier == nullptr) return std::string();
    UI::StringTable* seasons = g->m_eventNotifier->seasons;
    if (seasons == nullptr
        || !::Object::IsDerivedFromNullSafe(seasons, UI::StringTable::TypeId())) return std::string();
    const System::String key = System::String::Format("Month%i", static_cast<uint64_t>(month));
    int32_t index = seasons->GetIndexFromName(key);
    if (index < 0) index = month;   // the event log reads the table by position
    if (index >= seasons->GetCount()) return std::string();
    const System::String text = seasons->GetText(index);
    return text._data != nullptr ? std::string(text._data) : std::string();
}

std::string Stamp(int32_t s) {
    if (s < 0) return "date not known";
    const std::string month = MonthName(s % 12);
    return "Year " + std::to_string(s / 12) + ", " + (month.empty() ? "month " + std::to_string(s % 12 + 1)
                                                      : month);
}

// Names repeat, so a repeated name carries its number among those with it: "Anna", "Anna (2)".
void BuildLabels(const Tree& tree) {
    g_label.clear();
    std::unordered_map<std::string, int> total;
    for (const Person& p : tree.People()) ++total[p.name];
    std::unordered_map<std::string, int> seen;
    for (const Person& p : tree.People()) {
        const int n = ++seen[p.name];
        g_label[p.id] = total[p.name] > 1 ? p.name + " (" + std::to_string(n) + ")" : p.name;
    }
}

std::string LabelOf(uint32_t id) { return PersonLabel(id); }

void SetSelected(uint32_t id) {
    if (g_selected == id) return;
    g_selected = id;
    ++g_selStamp;
}

std::string LowerOf(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string PersonLine(const Person& p) {
    std::string s = LabelOf(p.id);
    s += p.gender == 0 ? "   woman" : "   man";
    if (p.Alive()) {
        LiveView v;
        if (LiveViewOf(p.id, v)) s += ", age " + std::to_string(v.ageYears);
        else s += ", living";
    } else {
        s += ", died at " + std::to_string(p.ageAtDeath);
    }
    return s;
}

size_t CountLiving(const Tree& tree) {
    size_t n = 0;
    for (const Person& p : tree.People())
        if (p.Alive()) ++n;
    return n;
}

// The people the first list shows: the living by name, then (when asked) the dead by name.
std::vector<uint32_t> PeopleToShow(const Tree& tree) {
    // each name is lowered once, not twice per comparison
    struct Keyed {
        std::string key;
        const Person* person;
    };
    std::vector<Keyed> alive;
    std::vector<Keyed> dead;
    for (const Person& p : tree.People()) (p.Alive() ? alive : dead).push_back(Keyed{LowerOf(p.name), &p});
    auto byName = [](const Keyed& a, const Keyed& b) {
        return a.key != b.key ? a.key < b.key : a.person->id < b.person->id;
    };
    std::sort(alive.begin(), alive.end(), byName);
    std::sort(dead.begin(), dead.end(), byName);
    std::vector<uint32_t> out;
    for (const Keyed& k : alive)
        if (NameMatches(LabelOf(k.person->id), g_query)) out.push_back(k.person->id);
    if (g_showDead)
        for (const Keyed& k : dead)
            if (NameMatches(LabelOf(k.person->id), g_query)) out.push_back(k.person->id);
    if (out.size() > static_cast<size_t>(kMaxPeopleRows)) out.resize(static_cast<size_t>(kMaxPeopleRows));
    return out;
}

void AddParentRow(const Tree& tree, std::vector<DetailRow>& rows, const char* relation, uint32_t id,
                  const std::string& side) {
    DetailRow r;
    r.relation = relation;
    r.person = tree.Find(id) != nullptr ? id : 0;
    if (r.person == 0) {
        r.info = "not known";
    } else {
        const Person* p = tree.Find(id);
        r.info = side + (p->Alive() ? "" : (side.empty() ? "died " : ", died ") + Stamp(p->died));
    }
    rows.push_back(r);
}

// "Farmer 12 y, Woodcutter 4 y": the jobs row of a person, in the game's words for the jobs.
std::string JobsLineOf(const Tree& tree, uint32_t id) {
    return tree.JobsText(id, 4, [](const std::string& key) { return JobText(key); });
}

std::vector<DetailRow> DetailOf(const Tree& tree, uint32_t id) {
    std::vector<DetailRow> rows;
    const Person* p = tree.Find(id);
    if (p == nullptr) return rows;
    {
        DetailRow r;
        r.relation = "Person";
        r.info = PersonLine(*p);
        rows.push_back(r);
    }
    {
        DetailRow r;
        r.relation = p->bornHere ? "Born" : "Arrived";
        r.info = Stamp(p->joined) + (p->bornHere ? "" : ", age " + std::to_string(p->joinedAge));
        rows.push_back(r);
    }
    if (!p->Alive()) {
        DetailRow r;
        r.relation = "Died";
        r.info = Stamp(p->died) + ", age " + std::to_string(p->ageAtDeath) +
                 (CauseText(p->cause)[0] != 0 ? std::string(", ") + CauseText(p->cause) : std::string());
        rows.push_back(r);
    }
    {
        const std::string jobs = JobsLineOf(tree, id);
        if (!jobs.empty()) {
            DetailRow r;
            r.relation = "Jobs";
            r.info = jobs;
            rows.push_back(r);
        }
    }
    {
        const Tree::Descendants d = tree.DescendantsOf(id);
        if (d.total > 0) {
            DetailRow r;
            r.relation = "Descendants";
            r.info = std::to_string(d.children) + (d.children == 1 ? " child, "
                                                   : " children, ") + std::to_string(d.total) +
                     " in all, " + std::to_string(d.living) + " living";
            rows.push_back(r);
        }
    }
    AddParentRow(tree, rows, "Mother", p->mother, "");
    AddParentRow(tree, rows, "Father", p->father, "");
    const Person* mother = tree.Find(p->mother);
    const Person* father = tree.Find(p->father);
    if (mother != nullptr) {
        if (tree.Find(mother->mother)) AddParentRow(tree, rows, "Grandmother", mother->mother,
                                                    "mother's side");
        if (tree.Find(mother->father)) AddParentRow(tree, rows, "Grandfather", mother->father,
                                                    "mother's side");
    }
    if (father != nullptr) {
        if (tree.Find(father->mother)) AddParentRow(tree, rows, "Grandmother", father->mother,
                                                    "father's side");
        if (tree.Find(father->father)) AddParentRow(tree, rows, "Grandfather", father->father,
                                                    "father's side");
    }
    for (const Marriage& m : p->marriages) {
        const Person* s = tree.Find(m.spouse);
        if (s == nullptr) continue;
        DetailRow r;
        r.relation = s->gender == 0 ? "Wife" : "Husband";
        r.person = s->id;
        r.info = "married " + Stamp(m.when) + (s->Alive() ? "" : ", died " + Stamp(s->died));
        rows.push_back(r);
    }
    for (const uint32_t cid : tree.Children(id)) {
        const Person* c = tree.Find(cid);
        DetailRow r;
        r.relation = c->gender == 0 ? "Daughter" : "Son";
        r.person = cid;
        r.info = "born " + Stamp(c->joined) + (c->Alive() ? "" : ", died " + Stamp(c->died));
        rows.push_back(r);
    }
    for (const uint32_t sid : tree.Siblings(id)) {
        const Person* s = tree.Find(sid);
        DetailRow r;
        const bool full = tree.FullSiblings(id, sid);
        r.relation = std::string(full ? "" : "Half ") + (s->gender == 0 ? (full ? "Sister" : "sister")
                                                         : (full ? "Brother" : "brother"));
        r.person = sid;
        r.info = "born " + Stamp(s->joined) + (s->Alive() ? "" : ", died " + Stamp(s->died));
        rows.push_back(r);
    }
    if (rows.size() > static_cast<size_t>(kMaxDetailRows)) rows.resize(static_cast<size_t>(kMaxDetailRows));
    return rows;
}

const UI::Dialog* WindowDialog() {
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kWindowRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) return nullptr;
    return static_cast<const UI::Dialog*>(ext);
}

UI::ElementDescription* CellTemplate(const char* rsc) {
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(rsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) return nullptr;
    return const_cast<UI::ElementDescription*>(static_cast<const UI::ElementDescription*>(ext));
}

void TintPeople() {
    for (size_t k = 0; k < g_peopleCells.size() && k < g_people.size(); ++k) {
        if (g_peopleCells[k].border.Get() != nullptr) {
            g_peopleCells[k].border->SetColor(g_people[k] == g_selected ? 0x30a0c8ffu : 0x10ffffffu);
        }
    }
}

void BuildDetail() {
    g_detailCells.clear();
    g_detail.clear();
    const Tree& tree = Town();
    UI::Element* title = g_title.Get();
    if (g_selected == 0 || tree.Find(g_selected) == nullptr) {
        SetParameterText(title, tree.Size() == 0 ? "Nobody has been recorded yet." : "Pick a person above.");
    } else {
        SetParameterText(title, "Family of " + LabelOf(g_selected));
        g_detail = DetailOf(tree, g_selected);
        g_jobsShown = JobsLineOf(tree, g_selected);
    }
    g_jobsCountdown = kJobsRecheckFrames;
    UI::Element* t = g_detailTable.Get();
    if (!::Object::IsDerivedFromNullSafe(t, UI::TableElement::TypeId())) return;
    UI::TableElement* table = static_cast<UI::TableElement*>(t);
    const UI::Dialog* dialog = WindowDialog();
    UI::ElementDescription* tmpl = CellTemplate(kRelationCellRsc);
    if (dialog == nullptr || tmpl == nullptr) {
        std::fprintf(stderr, "[FamilyTree] %s / %s did not load: no family rows\n", kWindowRsc,
                     kRelationCellRsc);
        table->SetSize(0, 1);
        return;
    }
    g_detailWidth = Ext::ListWidth(table);
    table->SetSize(static_cast<int>(g_detail.size()), 1);   // clears the old cells
    for (size_t k = 0; k < g_detail.size(); ++k) {
        Object::InternalPtr<UI::Element> cell;
        tmpl->CreateInstance(&cell, dialog);
        if (cell.Get() == nullptr) break;
        table->SetChild(&cell, static_cast<int32_t>(k));
        RelationCell c;
        c.root = cell;
        c.relation = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(cell.Get(),
                "textFamilyRelation"));
        c.name = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(cell.Get(),
                "textFamilyRelatedName"));
        c.info = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(cell.Get(),
                "textFamilyInfo"));
        c.button = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::ButtonElement>(cell.Get(),
                "buttonFamilyRelated"));
        const DetailRow& r = g_detail[k];
        SetParameterText(c.relation.Get(), r.relation);
        SetParameterText(c.info.Get(), r.info);
        if (c.button.Get() != nullptr) {
            c.button->SetElementId(g_idBase + kRelatedId + static_cast<int32_t>(k));
            if (r.person != 0) {
                SetParameterText(c.name.Get(), LabelOf(r.person));
            } else {
                c.button->Hide(true);
            }
        }
        if (g_detailWidth > 0) {
            cell->_minWidth = g_detailWidth;
        }
        g_detailCells.push_back(c);
    }
}

void BuildPeople() {
    g_peopleCells.clear();
    g_people.clear();
    const Tree& tree = Town();
    BuildLabels(tree);
    g_people = PeopleToShow(tree);
    g_seenSize = tree.Size();
    g_seenLiving = CountLiving(tree);
    SetParameterText(g_countText.Get(),
                     std::to_string(g_people.size()) + (g_people.size() == 1 ? " person" : " people"));
    // the picked person is still valid; with none picked, the first of the list
    if (tree.Find(g_selected) == nullptr) SetSelected(0);
    if (g_selected == 0 && !g_people.empty()) SetSelected(g_people.front());
    UI::Element* t = g_peopleTable.Get();
    if (!::Object::IsDerivedFromNullSafe(t, UI::TableElement::TypeId())) return;
    UI::TableElement* table = static_cast<UI::TableElement*>(t);
    const UI::Dialog* dialog = WindowDialog();
    UI::ElementDescription* tmpl = CellTemplate(kPersonCellRsc);
    if (dialog == nullptr || tmpl == nullptr) {
        std::fprintf(stderr, "[FamilyTree] %s / %s did not load: no people rows\n", kWindowRsc,
                     kPersonCellRsc);
        table->SetSize(0, 1);
        return;
    }
    g_peopleWidth = Ext::ListWidth(table);
    table->SetSize(static_cast<int>(g_people.size()), 1);   // clears the old cells
    for (size_t k = 0; k < g_people.size(); ++k) {
        Object::InternalPtr<UI::Element> cell;
        tmpl->CreateInstance(&cell, dialog);
        if (cell.Get() == nullptr) break;
        table->SetChild(&cell, static_cast<int32_t>(k));
        PersonCell c;
        c.root = cell;
        c.name = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(cell.Get(),
                "textFamilyPickName"));
        c.button = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::ButtonElement>(cell.Get(),
                "buttonFamilyPick"));
        if (c.button.Get() != nullptr) {
            c.button->SetElementId(g_idBase + kPersonId + static_cast<int32_t>(k));
        }
        SetParameterText(c.name.Get(), PersonLine(*tree.Find(g_people[k])));
        Object::InternalPtr<UI::Element> border;
        c.border = *cell->GetBorder(&border);
        if (g_peopleWidth > 0) {
            cell->_minWidth = g_peopleWidth;
        }
        g_peopleCells.push_back(c);
    }
    TintPeople();
}

// What is typed in the search box now.
std::string SearchText() {
    UI::Element* e = g_search.Get();
    if (!::Object::IsDerivedFromNullSafe(e, UI::EditElement::TypeId())) return std::string();
    const System::String& text = static_cast<UI::EditElement*>(e)->GetText();
    return text._data != nullptr ? std::string(text._data) : std::string();
}

void RefreshShowDead() {
    UI::Element* check = g_showDeadCheck.Get();
    if (::Object::IsDerivedFromNullSafe(check, UI::CheckElement::TypeId())) {
        static_cast<UI::CheckElement*>(check)->_checked = g_showDead ? 1 : 0;
    }
}

// The file name without its folder.
std::string LeafOf(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// "Saved FamilyTree3.png in Screenshots (12 people, 4 generations)": the end of every success line. The
// status line is one line in a box that cannot grow (a longer text would make the window wider), so it is
// kept short.
constexpr size_t kStatusMaxChars = 84;

std::string FitStatus(std::string text) {
    if (text.size() > kStatusMaxChars) text = text.substr(0, kStatusMaxChars - 3) + "...";
    return text;
}

std::string SavedLine(const std::string& path, const PictureLayout& layout, const std::string& note) {
    return FitStatus("Saved " + LeafOf(path) + " (" + std::to_string(layout.people) + " people, " +
                     std::to_string(layout.gens) + " generations" + (layout.trimmed ? "; not everyone shown"
                             : "") + note + ")");
}

// The whole-town picture into Screenshots, and the status line: a PNG, or an SVG when asked (and when the PNG
// cannot be drawn because the system has no font).
void SaveImage(bool png) {
    PictureOptions options;
    options.livingOnly = g_imageLiving;
    options.now = NowStamp();
    if (g_imageHighlight && g_selected != 0 && Town().Find(g_selected) != nullptr) {
        options.highlight = g_selected;
        options.highlightName = LabelOf(g_selected);
    }
    if (GameUpdateLoop::_singleton != nullptr && GameUpdateLoop::_singleton->m_saveName._data != nullptr) {
        options.village = GameUpdateLoop::_singleton->m_saveName._data;   // the village's name: its save name
    }
    PictureJobs pictureJobs;
    pictureJobs.nameOf = [](const std::string& key) { return JobText(key); };
    const PictureLayout layout = BuildPictureLayout(Town(), options.livingOnly, g_imageJobs ? &pictureJobs
                                                    : nullptr);
    if (layout.boxes.empty()) {
        g_imageStatus = "Nobody has been recorded yet.";
        SetParameterText(g_imageStatusText.Get(), g_imageStatus);
        return;
    }
    // Why the SVG is saved instead of the PNG that was asked for: "" none, "font", "names" (the picture is
    // too big for its names to be drawn: coloured boxes only), "memory" (the PNG could not be built).
    std::string noPng;
    if (png) {
        PictureRaster raster;
        bool rendered = false;
        try {
            rendered = RenderPictureRaster(Town(), layout, options, SystemGlyphs(), raster);
        } catch (const std::bad_alloc&) {
            raster = PictureRaster();
            noPng = "memory";
            std::fprintf(stderr, "[FamilyTree] no PNG: not enough memory for the picture\n");
        }
        if (rendered && !raster.namesDrawn) {
            noPng = "names";
            std::fprintf(stderr, "[FamilyTree] no PNG: at scale %.3f the names cannot be drawn\n",
                         raster.scale);
        } else if (rendered) {
            std::string path;
            try {
                path = TownScreenshot::SavePngInScreenshots("FamilyTree", raster.rgba, raster.width,
                                                            raster.height);
            } catch (const std::bad_alloc&) {
                noPng = "memory";
                std::fprintf(stderr, "[FamilyTree] no PNG: not enough memory to encode it\n");
            }
            if (noPng.empty() && path.empty()) {
                g_imageStatus = "The picture could not be saved.";
                std::fprintf(stderr, "[FamilyTree] could not write the PNG (%d x %d)\n", raster.width,
                             raster.height);
            } else if (noPng.empty()) {
                std::string note;
                if (raster.scale < 1.0) {
                    note = "; drawn at " + std::to_string(static_cast<int>(raster.scale * 100.0)) +
                           "% size";
                }
                g_imageStatus = SavedLine(path, layout, note);
                std::fprintf(stderr, "[FamilyTree] wrote %s (%d x %d, scale %.3f, %d people)\n", path.c_str(),
                             raster.width,
                             raster.height, raster.scale, layout.people);
            }
            if (noPng.empty()) {
                SetParameterText(g_imageStatusText.Get(), g_imageStatus);
                return;
            }
        } else if (noPng.empty()) {
            noPng = "font";
            std::fprintf(stderr, "[FamilyTree] no PNG: %s\n", raster.why.c_str());
        }
    }
    const std::string svg = RenderPictureSvg(Town(), layout, options);
    const std::string path = TownScreenshot::SaveInScreenshots("FamilyTree", "svg", svg.data(), svg.size());
    if (path.empty()) {
        g_imageStatus = "The picture could not be saved.";
        std::fprintf(stderr, "[FamilyTree] could not write the picture (%zu bytes)\n", svg.size());
    } else {
        std::string note;
        if (noPng == "names") note = "; a PNG this big has no names you could read";
        else if (noPng == "memory") note = "; not enough memory for a PNG";
        g_imageStatus = FitStatus((noPng == "font" ? std::string("SVG instead: ") : std::string()) +
                                  SavedLine(path, layout, note));
        std::fprintf(stderr, "[FamilyTree] wrote %s (%zu bytes, %d people)\n", path.c_str(), svg.size(),
                     layout.people);
    }
    SetParameterText(g_imageStatusText.Get(), g_imageStatus);
}

void PageOpen(UI::Element* page, int32_t idBase) {
    g_idBase = idBase;
    g_open = true;
    // a new game, a load or a reload starts at the first person, the dead hidden
    if (g_town != LockEpoch()) {
        g_town = LockEpoch();
        SetSelected(0);
        g_pending = 0;
        g_showDead = false;
        g_imageStatus.clear();   // the last picture belonged to the old town
    }
    g_page = Object::InternalPtr<UI::Element>(page);
    g_query.clear();   // a window that opens now has an empty search box
    g_search = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::EditElement>(page, "editFamilySearch"));
    if (UI::Element* field = g_search.Get()) {
        // the border has no color of its own: a light fill shows where to type
        Object::InternalPtr<UI::Element> slot;
        Object::InternalPtr<UI::Element> border = *field->GetBorder(&slot);
        if (border.Get() != nullptr) border->SetColor(kSearchFill);
    }
    g_countText = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(page,
            "textFamilyCount"));
    g_showDeadCheck = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::CheckElement>(page,
            "checkFamilyShowDead"));
    if (g_showDeadCheck.Get() != nullptr) g_showDeadCheck->SetElementId(idBase + kShowDeadId);
    g_peopleTable = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TableElement>(page,
            "tableFamilyPeople"));
    g_detailTable = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TableElement>(page,
            "tableFamilyDetail"));
    g_title = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(page,
            "textFamilyDetailTitle"));
    g_imageCheck = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::CheckElement>(page,
            "checkFamilyImageLiving"));
    if (g_imageCheck.Get() != nullptr) {
        g_imageCheck->SetElementId(idBase + kImageLivingId);
        static_cast<UI::CheckElement*>(g_imageCheck.Get())->_checked = g_imageLiving ? 1 : 0;
    }
    if (UI::Element* image = Ext::FindElement<UI::ButtonElement>(page, "buttonFamilyImage")) {
        image->SetElementId(idBase + kImageId);
    }
    if (UI::Element* svg = Ext::FindElement<UI::ButtonElement>(page, "buttonFamilyImageSvg")) {
        svg->SetElementId(idBase + kImageSvgId);
    }
    if (UI::Element* jobs = Ext::FindElement<UI::CheckElement>(page, "checkFamilyImageJobs")) {
        jobs->SetElementId(idBase + kImageJobsId);
        static_cast<UI::CheckElement*>(jobs)->_checked = g_imageJobs ? 1 : 0;
        g_imageJobsCheck = Object::InternalPtr<UI::Element>(jobs);
    }
    if (UI::Element* highlight = Ext::FindElement<UI::CheckElement>(page, "checkFamilyImageHighlight")) {
        highlight->SetElementId(idBase + kImageHighlightId);
        static_cast<UI::CheckElement*>(highlight)->_checked = g_imageHighlight ? 1 : 0;
        g_imageHighlightCheck = Object::InternalPtr<UI::Element>(highlight);
    }
    g_inTownButton = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::ButtonElement>(page,
            "buttonFamilyShowInTown"));
    if (g_inTownButton.Get() != nullptr) g_inTownButton->SetElementId(idBase + kShowInTownId);
    g_inTownShown = -1;
    g_imageStatusText = Object::InternalPtr<UI::Element>(Ext::FindElement<UI::TextElement>(page,
            "textFamilyImageStatus"));
    SetParameterText(g_imageStatusText.Get(), g_imageStatus);
    if (UI::Element* showTree = Ext::FindElement<UI::ButtonElement>(page, "buttonFamilyShowTree")) {
        showTree->SetElementId(idBase + kShowTreeId);
    }
    if (UI::Element* showSummary = Ext::FindElement<UI::ButtonElement>(page, "buttonFamilyShowSummary")) {
        showSummary->SetElementId(idBase + kShowSummaryId);
    }
    if (g_peopleTable.Get() == nullptr || g_detailTable.Get() == nullptr) {
        std::fprintf(stderr,
                     "[FamilyTree] tableFamilyPeople / tableFamilyDetail not in the page: empty page\n");
        return;
    }
    RefreshShowDead();
    BuildPeople();
    BuildDetail();
}

bool PageChanged(UI::Element* page, int32_t id) {
    if (id == kShowDeadId) {
        UI::Element* check = g_showDeadCheck.Get();
        g_showDead = ::Object::IsDerivedFromNullSafe(check, UI::CheckElement::TypeId())
                         ? static_cast<UI::CheckElement*>(check)->_checked != 0
                         : !g_showDead;
        BuildPeople();   // the check box sits outside the tables: safe to rebuild the people list here
        BuildDetail();
        return true;
    }
    if (id == kImageLivingId) {
        UI::Element* check = g_imageCheck.Get();
        g_imageLiving = ::Object::IsDerivedFromNullSafe(check, UI::CheckElement::TypeId())
                            ? static_cast<UI::CheckElement*>(check)->_checked != 0
                            : !g_imageLiving;
        return true;
    }
    if (id == kImageJobsId) {
        UI::Element* check = g_imageJobsCheck.Get();
        g_imageJobs = ::Object::IsDerivedFromNullSafe(check, UI::CheckElement::TypeId())
                          ? static_cast<UI::CheckElement*>(check)->_checked != 0
                          : !g_imageJobs;
        return true;
    }
    if (id == kImageHighlightId) {
        UI::Element* check = g_imageHighlightCheck.Get();
        g_imageHighlight = ::Object::IsDerivedFromNullSafe(check, UI::CheckElement::TypeId())
                               ? static_cast<UI::CheckElement*>(check)->_checked != 0
                               : !g_imageHighlight;
        return true;
    }
    if (id == kShowInTownId) {
        if (g_selected != 0) ShowInTown(g_selected);
        return true;
    }
    if (id == kImageId || id == kImageSvgId) {
        SaveImage(id == kImageId);
        return true;
    }
    if (id == kShowTreeId) {
        OpenTreeWindow(UiOfElement(page));
        return true;
    }
    if (id == kShowSummaryId) {
        OpenSummaryWindow(UiOfElement(page));
        return true;
    }
    if (id >= kPersonId && id < kPersonId + static_cast<int32_t>(g_people.size())) {
        g_pending = g_people[static_cast<size_t>(id - kPersonId)];
        return true;
    }
    if (id >= kRelatedId && id < kRelatedId + static_cast<int32_t>(g_detail.size())) {
        const uint32_t who = g_detail[static_cast<size_t>(id - kRelatedId)].person;
        if (who != 0) g_pending = who;
        return true;
    }
    return false;
}

// Every frame while the page is showing: apply a pick, and rebuild when the tree has changed under the lists.
void PageTick() {
    if (!g_open) return;
    if (g_pending != 0) {
        SetSelected(g_pending);
        g_pending = 0;
        TintPeople();
        BuildDetail();
        return;
    }
    const Tree& tree = Town();
    // "Show in town" only for a person living in the town now
    if (g_inTownButton.Get() != nullptr) {
        const int want = g_selected != 0 && CanShowInTown(g_selected) ? 1 : 0;
        if (want != g_inTownShown) {
            g_inTownShown = want;
            g_inTownButton->Hide(want == 0);
        }
    }
    const std::string typed = SearchText();
    if (typed != g_query) {   // the search box is outside the tables: safe to rebuild the people list here
        g_query = typed;
        BuildPeople();
        return;
    }
    if (tree.Size() != g_seenSize || CountLiving(tree) != g_seenLiving) {
        BuildPeople();
        BuildDetail();
        return;
    }
    if (--g_jobsCountdown <= 0) {
        g_jobsCountdown = kJobsRecheckFrames;
        if (g_selected != 0 && JobsLineOf(tree, g_selected) != g_jobsShown) BuildDetail();
    }
}

void PageClose() {
    g_open = false;
    g_search = Object::InternalPtr<UI::Element>();
    g_countText = Object::InternalPtr<UI::Element>();
    g_inTownButton = Object::InternalPtr<UI::Element>();
    g_imageHighlightCheck = Object::InternalPtr<UI::Element>();
    g_imageJobsCheck = Object::InternalPtr<UI::Element>();
    g_imageCheck = Object::InternalPtr<UI::Element>();
    g_imageStatusText = Object::InternalPtr<UI::Element>();
    g_peopleCells.clear();
    g_detailCells.clear();
    g_people.clear();
    g_detail.clear();
    g_page = Object::InternalPtr<UI::Element>();
    g_showDeadCheck = Object::InternalPtr<UI::Element>();
    g_peopleTable = Object::InternalPtr<UI::Element>();
    g_detailTable = Object::InternalPtr<UI::Element>();
    g_title = Object::InternalPtr<UI::Element>();
}

}  // namespace

std::string PersonLabel(uint32_t id) {
    // names repeat: the numbers depend on everyone
    if (g_labelEpoch != LockEpoch() || g_labelSize != Town().Size()) {
        BuildLabels(Town());
        g_labelEpoch = LockEpoch();
        g_labelSize = Town().Size();
    }
    const auto it = g_label.find(id);
    return it == g_label.end() ? std::string("?") : it->second;
}

uint32_t SelectedPerson() {
    // a new game, a load or a reload starts at the first person, the dead hidden
    if (g_town != LockEpoch()) {
        g_town = LockEpoch();
        SetSelected(0);
        g_pending = 0;
        g_showDead = false;
        g_imageStatus.clear();   // the last picture belonged to the old town
        g_noLivingAt = ~size_t(0);
    }
    const Tree& tree = Town();
    if (tree.Find(g_selected) == nullptr) {
        // Nobody to pick stays so until the tree grows (a birth or an arrival), so the scan is not repeated
        // every frame the tree window is open. g_noLivingAt is reset with the town, above.
        if (tree.Size() != g_noLivingAt) {
            const uint32_t first = tree.FirstLivingId();
            g_noLivingAt = first == 0 ? tree.Size() : ~size_t(0);
            SetSelected(first);
        }
    }
    return g_selected;
}

uint32_t SelectionStamp() { return g_selStamp; }

bool ShowPerson(uint32_t id) {
    if (Town().Find(id) == nullptr) return false;
    g_town = LockEpoch();   // the pick belongs to this game (PageOpen would otherwise start over)
    SetSelected(id);
    // an open page rebuilds in its tick; a page that opens later builds from g_selected
    g_pending = g_open ? id : 0;
    return true;
}
}  // namespace FamilyTree

namespace Toolbar {

const Contribution& FamilyTreePage() {
    static const Contribution c = { FamilyTree::kId, FamilyTree::kPageCaption, FamilyTree::kPageRsc,
                                    FamilyTree::PageOpen, FamilyTree::PageChanged, FamilyTree::PageClose,
                                    FamilyTree::PageTick };
    return c;
}

}  // namespace Toolbar
}  // namespace Ext
