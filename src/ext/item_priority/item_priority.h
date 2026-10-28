#pragma once
// ============================================================================
// src/ext/item_priority/item_priority.h -- ext.item_priority (class C, default OFF)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/ITEMPRIO.md. Item priority: for the four item categories a citizen
// fetches from a storage (Fuel, Tool, Health, Clothing) the town ranks the items, best first, and a citizen
// takes the best-ranked item the storages hold. Retail takes the item flagged `_preferred`
// (RawMaterialDescription +0x58), else a random one of those it may take; a town with many tools (a mod's
// Steel, Iron and Bronze tools in one barn) cannot say which first.
//
// WHERE. Every pick of an item of one of the four categories is a two-pass choice over a storage's rows:
// count the rows whose RawMaterialDescription `_flags` category bit is set and that have stock, remember the
// last `_preferred` one, else roll a random one of them, then walk to it. There are three copies of that
// loop: * CitizenComp_Entity_impl_ef40 (retail 0x14008ef40), categories 1..4 (citizencomponent.cpp): the
// storage item picker. Tools (2) and clothing (4) are taken through it (the finder's returned item); the
// finder also asks it "does this storage hold one" for every category while it searches for the nearest
// storage (CitizenBehavior::FindBestTarget). * SelectAndTransfer (citizenai_gather.cpp), Fuel (bit 6): the
// fuel fetch runs its OWN inline copy after the finder named the storage, so the picker alone never decides
// which fuel is carried. * the Health fetch (citizenai_growup.cpp, bit 13): the same, inline. Category
// numbers are the finder's / ef40's: 1 Fuel (flag bit 6), 2 Tool (9), 3 Health (13), 4 Clothing (14).
//
// WHAT THE ROW DOES. Each of the three loops adds ONE step before the `_preferred` / random choice: of the
// rows it would consider, only those of the best rank stay eligible (a rank is the item's place in the town's
// list for that category; an item the list does not name is ranked last). Items that tie keep retail's choice
// among them. With no list for a category every item ties, so nothing changes.
//
// NEVER. An item can instead be set to Never: no citizen takes it, whatever else the storages hold (section
// 9 of the hand-in). It has the rank kNever, which no tier ceiling and no best rank lets through
// (Eligible); a category with only a Never list still narrows its picks.
//
// ACROSS STORAGES. The finder picks the NEAREST storage that holds any item of the category, so a better item
// in a farther storage would lose. The four call sites of the finder for these categories (FindTiered below)
// therefore run it once per tier, best first: with a rank CEILING that makes ef40 skip every item ranked
// worse than the tier, and take the first tier that finds a storage; the last tier has no ceiling (every
// item, ranked or not).
//
// THE LIST is the town's own state (class C): ItemPriority::Lists, saved in the row's record in the save tail
// (save_state.cpp, wire format v1), edited live from the Priority page of the extension window
// (toolbar_page.cpp). It never lives in settings.cfg. An item is named by the template path of its
// RawMaterialDescription (External::GetPath), not by `_name`, which is a sprite / string key that mods reuse.
//
// SELECTION (DESIGN.md hook rule 4). The seams never touch a string or a map. BeginFrame resolves the saved
// paths once, when a list or the lock epoch changes, into `g_ranked`: per category a short flat array of
// RawMaterialDescription pointers, best first, so an item's rank is its index. A seam tests Active(category)
// once above its loops and scans that array only for a row that passed retail's own flag test. With the row
// off, or a category with no list resolved, Active is false and each seam pays one load and a not-taken
// branch.
//
// The lists, the matching and the codec are standard C++ only: tests/unit/ext_item_priority_test.cpp links
// item_priority.cpp and save_state.cpp on their own. The game side (the factory walk, BeginFrame) is
// item_priority_town.cpp.
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

namespace Ext { namespace SaveTail { struct StateCodec; } }

namespace Ext {
namespace ItemPriority {

// The page this extension contributes to the extension window (toolbar_page.cpp builds its Contribution from
// these, and the unit tests check them against the registry and the .rsc).
constexpr const char* kId          = "item_priority";
constexpr const char* kPageCaption = "ItemPriorityTab";
constexpr const char* kPageRsc     = "Dialog/ItemPriorityPage.rsc:pageItemPriority";

constexpr int kFuel = 1;
constexpr int kTool = 2;
constexpr int kHealth = 3;
constexpr int kClothing = 4;
constexpr int kCategories = 4;
constexpr int kUnranked = 0x7fff;   // an item the list does not name: ranked after every named one
constexpr int kNever = 0x7fffffff;  // an item marked Never: no tier, ceiling or best rank lets it through

// The `_flags` bit that marks an item of a category (the ef40 table: 0 -> 5, 1 -> 6, 2 -> 9, 3 -> 13, 4 ->
// 14).
int FlagBit(int category);   // -1 for a category outside 1..4
bool IsCategory(int category);

// ---- the lists (the town's state) ------------------------------------------------------------------------

// One ranked list per category, best first, and one Never list: template paths, no duplicates (compared
// without case). An item is in at most one of the two lists of its category.
struct Lists {
    std::vector<std::string> paths[kCategories];   // index category - 1
    std::vector<std::string> never[kCategories];   // items citizens never take
    bool Empty() const;
    const std::vector<std::string>& Of(int category) const { return paths[category - 1]; }
    std::vector<std::string>& Of(int category) { return paths[category - 1]; }
    const std::vector<std::string>& NeverOf(int category) const { return never[category - 1]; }
    std::vector<std::string>& NeverOf(int category) { return never[category - 1]; }
};

// The rank of an item in the list: its index, or kUnranked.
int RankOf(const Lists& lists, int category, const std::string& path);
// Whether the item is on the category's Never list.
bool IsNever(const Lists& lists, int category, const std::string& path);
// Edits (each returns whether the lists changed). `Rank` appends an unranked item at the bottom (and takes
// it off the Never list); `Unrank` removes it; `Move` swaps it with its neighbour (-1 up, +1 down; an item
// at an end stays). `SetNever` puts an item on the Never list (and off the ranked one); `ClearNever` takes
// it off, leaving it unranked.
bool SetNever(Lists& lists, int category, const std::string& path);
bool ClearNever(Lists& lists, int category, const std::string& path);
bool Rank(Lists& lists, int category, const std::string& path);
bool Unrank(Lists& lists, int category, const std::string& path);
bool Move(Lists& lists, int category, const std::string& path, int direction);

// The live lists (what the Priority page edits through).
const Lists& Current();
void Apply(const Lists& lists);   // replace at once; resolved again at the next frame
// A new game, or the game ends: empty lists, nothing resolved, `g_on` false, no ceiling.
void ResetState();

// ---- resolving the lists against the running game --------------------------------------------------------

// One RawMaterialDescription of the running game and its template path.
struct Candidate {
    const void* material = nullptr;
    std::string path;
};
// True when `g_ranked` is stale: a list changed, or the lock epoch is not the one it was built in.
bool NeedsResolve(uint32_t epoch);
// Rebuild `g_ranked` from the materials the game has, for the lock epoch `epoch`. A path the game does not
// have stays in the list and ranks nothing.
void Resolve(uint32_t epoch, const std::vector<Candidate>& materials);
// `g_on` for this frame: the game's locked row value and something resolved.
void SetRowActive(bool rowOn);

// ---- run time (the seams) --------------------------------------------------------------------------------

// This frame: the row is active for the game AND at least one category has a resolved list.
extern bool g_on;
// The ranked materials per category (index category - 1), best first.
extern std::vector<const void*> g_ranked[kCategories];
// The materials marked Never per category (index category - 1).
extern std::vector<const void*> g_never[kCategories];

// Tested once by a seam, above its loops: item priority narrows this category (a ranked or a Never list).
inline bool Active(int category) {
    return g_on && category >= 1 && category <= kCategories &&
           (!g_ranked[category - 1].empty() || !g_never[category - 1].empty());
}

// The rank of a material (a RawMaterialDescription) in `category`'s resolved list: its index, or kUnranked;
// kNever for an item on the Never list. Called only after Active(category), for a row that passed retail's
// flag test.
inline int RankOfMat(int category, const void* material) {
    for (const void* never : g_never[category - 1]) {
        if (never == material) {
            return kNever;
        }
    }
    const std::vector<const void*>& ranked = g_ranked[category - 1];
    const int n = static_cast<int>(ranked.size());
    for (int i = 0; i < n; ++i) {
        if (ranked[static_cast<size_t>(i)] == material) {
            return i;
        }
    }
    return kUnranked;
}

// Whether a row of `category` stays eligible when the best rank among the rows is `bestRank`: it has
// exactly that rank, and it is not Never. (A Never row ties with the "nothing eligible" bestRank of
// 0x7fffffff, so the rank alone cannot say.)
inline bool Eligible(int category, const void* material, int bestRank) {
    const int r = RankOfMat(category, material);
    return r == bestRank && r != kNever;
}

// The rank ceiling of the tier being searched: an item ranked worse than this is not eligible (kUnranked = no
// ceiling).
int Ceiling();

// Runs `once` (one finder call, which returns whether it found a storage) for `category`: a single call when
// the category has no list or the row is off, else once per rank of the resolved list, best first, with the
// ceiling set to that rank, then once with no ceiling. Stops at the first that finds one. Returns whether any
// did.
bool FindTiered(int category, bool (*once)(void* context), void* context);

template <class F>
bool FindTiered(int category, F&& once) {
    return FindTiered(
        category, [](void* c) -> bool { return (*static_cast<F*>(c))(); }, static_cast<void*>(&once));
}

// ---- the game side (item_priority_town.cpp; not in the unit build) ---------------------------------------

// GameUpdateLoop::Update, once per frame beside the other BeginFrame calls.
void BeginFrame();
// The template path of a loaded resource object (an `Object::External*`): what the lists store.
std::string TemplatePath(const void* external);

// The save-tail codec for this extension's per-town lists (save_state.cpp; registered in state_codecs.cpp).
const SaveTail::StateCodec& Codec();

}  // namespace ItemPriority
}  // namespace Ext
