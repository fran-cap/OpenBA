#pragma once
// ============================================================================
// src/ext/item_limits/item_limits.h -- ext.item_limits (class C, default OFF)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/ITEMLIMITS.md. A limit for ONE item, beside retail's limit for its
// category: a workshop stops making an item once the town holds that many of it, even while the category
// (Tools, Clothing, Food ...) is under its own limit. The motivating case: a mod's Steel tools and Iron tools
// share the Tools limit, so retail cannot say "50 steel tools, then stop".
//
// ONE LIMIT PER ITEM, TOWN-WIDE. The limit belongs to the item, not to a building: every workshop that makes
// the item reads the same number, and setting it in one building's panel sets it for all of them (the user,
// 2026-11-03). An item is named by the template path of its RawMaterialDescription (External::GetPath), the
// key ext.restricted_foods and ext.item_priority use; never by `_name` (a sprite / string key mods reuse),
// never by index (mods change the load order between sessions).
//
// WHERE. Retail reads a recipe's limit in one place, ConsumeProduce::Execute (consumeproduce.cpp, the
// workshop work; retail's depletion test against CitizenBehavior::professionResources[recipe.materialType]).
// With the row active, a recipe whose product's item limit is reached counts as depleted there too, in
// retail's shape: the active recipe in single-product mode, any recipe in the random mode. Everything that
// follows (the status icon, the idle branch) is retail's own.
//
// COUNTING. Retail's CitizenBehavior::CountResources tallies the stock per category every tick. Right after
// it, CountTown walks the same two storage lists and tallies only the limited items (the flat array
// `g_items`, a short linear scan per stored row, adding into the row's path in `g_counts`). Nothing is
// counted while the row is off or no limit is set.
//
// SELECTION (DESIGN.md hook rule 4). BeginFrame resolves the saved paths once, when the list or the lock
// epoch changes, into `g_items`: per limited item the item templates (ComponentDescription) that carry its
// RawMaterialDescription and its limit; `g_counts` keeps one count per path. The seams test `g_on` once and
// never touch a string or a map.
//
// THE PANEL. A workshop's panel (ConsumeProduceUI) gets one more row under its Product row: a check box,
// the product's name and a spinner like the category's. Checked = the item has its own limit. It follows the
// product the panel's combo selects. Every panel showing the same item shows the same value.
//
// The list, the matching and the codec are standard C++ only: tests/unit/ext_item_limits_test.cpp links
// item_limits.cpp and save_state.cpp on their own. The game side is item_limits_town.cpp (the factory walk,
// BeginFrame, the count, the Execute test) and panel_row.cpp (the panel).
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

#include "ui/EventId.h"   // UI::EventId (PanelEvent's event id)

namespace Ext { namespace SaveTail { struct StateCodec; } }

class CitizenBehavior;
class ConsumeProduceDescription;
class ConsumeProduceUI;
class PastureComponent;
class CitizenComponent;
class ComponentDescription;
class HerbalistDescription;
class PastureUI;
class ResourceLimitUI;
class AnimalDescription;

namespace Ext {
namespace ItemLimits {

constexpr const char* kId = "item_limits";
constexpr int32_t kMaxLimit = 999999999;   // ext.high_limits' ceiling; the spinner's own range is lower without it

// ---- the list (the town's state) -------------------------------------------------------------------------

struct Limit {
    std::string path;    // the item's RawMaterialDescription template path
    int32_t limit = 0;   // 0..kMaxLimit
};

// The live list, in the order the limits were first set. Paths are unique, compared without case.
const std::vector<Limit>& Limits();
// The item's limit into `out`; false when it has none.
bool LimitOf(const std::string& path, int32_t& out);
// Sets (or changes) the item's limit; the value is clamped to 0..kMaxLimit. Returns whether the list changed.
bool SetLimit(const std::string& path, int32_t limit);
// Removes the item's limit. Returns whether the list changed.
bool ClearLimit(const std::string& path);
// Replaces the whole list (the codec's decode). Resolved again at the next frame.
void ReplaceLimits(const std::vector<Limit>& limits);
// A new game, or the game ends: no limits, nothing resolved, `g_on` false, every panel row forgotten.
void ResetState();

// ---- resolving the list against the running game ---------------------------------------------------------

// One item template of the running game (a ComponentDescription with a RawMaterialDescription) and the path
// of that RawMaterialDescription.
struct Candidate {
    const void* item = nullptr;
    std::string path;
};
// True when `g_items` is stale: the list changed, or the lock epoch is not the one it was built in.
bool NeedsResolve(uint32_t epoch);
// True when it needs the factory walk (Resolve): a path joined or left the list, or the epoch changed. When
// only values changed, RefreshLimits copies them into `g_items` without the walk (a spinner held down
// changes the value every few frames, and the walk visits every loaded resource).
bool NeedsWalk(uint32_t epoch);
void RefreshLimits();
// Rebuilds `g_items` for the lock epoch `epoch`. A path the game does not have stays in the list and limits
// nothing. A path that stays limited keeps its last count, so a value edited mid-tick never reads 0.
void Resolve(uint32_t epoch, const std::vector<Candidate>& items);
// `g_on` for this frame: the game's locked row value and something resolved.
void SetRowActive(bool rowOn);

// ---- run time (the seams) --------------------------------------------------------------------------------

// One limited item template and the entry of Limits() it belongs to. Several templates can carry one
// RawMaterialDescription (a mod's variant of an item): they share that path's limit AND its count.
struct Resolved {
    const void* item = nullptr;   // the ComponentDescription a StorageComponent::Item and a recipe name
    int32_t limit = 0;            // Limits()[slot].limit
    int32_t slot = -1;            // its entry in Limits() (stable until a path joins or leaves the list)
};

// This frame: the row is active for the game AND at least one limit resolved.
extern bool g_on;
extern std::vector<Resolved> g_items;
// Per entry of Limits() (by `slot`): how many items of that path the town's storages held at the last count,
// every template of the path together.
extern std::vector<int32_t> g_counts;

// The count: zero every path, then add each stored row of a limited item to its path.
inline void BeginCount() {
    for (int32_t& c : g_counts) {
        c = 0;
    }
}
inline void Tally(const void* item, int32_t count) {
    for (const Resolved& r : g_items) {
        if (r.item == item) {
            g_counts[static_cast<size_t>(r.slot)] += count;
            return;
        }
    }
}
// Whether the town holds as many of `item`'s path as its limit allows (retail's `limit <= stock`), counting
// every template of that path: the number the panel shows is the town's ceiling for the item. False for an
// item with no limit.
inline bool AtLimit(const void* item) {
    for (const Resolved& r : g_items) {
        if (r.item == item) {
            return r.limit <= g_counts[static_cast<size_t>(r.slot)];
        }
    }
    return false;
}

// ---- the producers' tests, without the game types (the seams below call them; unit-tested) -------------

// Whether every one of `count` items (`itemAt(i)`, a template pointer or null) has reached its item limit: a
// slaughter's yields, a gatherer's plants. False with no item, and a null item (a plant that names none) is
// never at its limit, so it keeps the producer going, as retail.
template <class ItemAt>
inline bool AllAtLimit(int32_t count, ItemAt itemAt) {
    if (count <= 0) {
        return false;
    }
    for (int32_t i = 0; i < count; ++i) {
        if (!AtLimit(itemAt(i))) {
            return false;   // something left to make
        }
    }
    return true;
}
// Whether a slaughter leaves `item` out: only for a pasture's herder (`forPasture`; a hunter's kill yields
// everything, as retail) and only an item at its limit.
inline bool SkipsYield(bool forPasture, const void* item) {
    return forPasture && AtLimit(item);
}
// The panel rows' items: the index of the first occurrence of each non-null item of `items` (null = not an
// item: no RawMaterialDescription), in order, at most `max` of them. A product two recipes make, or wild and
// planted sunflowers, get one row: the limit is the item's.
std::vector<int32_t> FirstOfEach(const std::vector<const void*>& items, size_t max);

// ---- the game side (item_limits_town.cpp, panel_row.cpp; not in the unit build) --------------------------

// GameUpdateLoop::Update, once per frame beside the other BeginFrame calls.
void BeginFrame();
// The template path of a loaded resource object (an `Object::External*`): what the list stores.
std::string TemplatePath(const void* external);

// CitizenBehavior::Update, right after retail's CountResources: the limited items' stock.
void CountTownSlow(CitizenBehavior* town);
inline void CountTown(CitizenBehavior* town) {
    if (g_on) {
        CountTownSlow(town);
    }
}

// ConsumeProduce::Execute, after retail's depletion test: whether a product of the recipes it tests has
// reached its item limit. `allRecipes` is retail's random mode (every recipe is tested), else only `active`.
bool RecipeAtLimitSlow(const ConsumeProduceDescription* desc, bool allRecipes, int active);

// The AnimalDescription of the animal PastureWork::Execute would slaughter now: the one retail picks
// (FindOldestLivestock, a const scan) when the pasture holds more animals than its capacity, if its
// AnimalComponent is not flagged for removal. Null when there is none. It is the animal's own description, not
// the pasture's selected livestock: after the player switches the livestock the old herd is slaughtered first.
const AnimalDescription* SlaughterCandidate(const PastureComponent* pasture);
// Whether every item a slaughter of `animal` yields (AnimalDescription::_rawMaterials) has reached its item
// limit (AllAtLimit). Then the herder does not slaughter; hauling and grazing go on. While one of them is still
// under its limit the slaughter goes on and yields only those (SlaughterSkipsYield).
bool SlaughterAtLimitSlow(const AnimalDescription* animal);
// PastureWork::Execute, before a slaughter (retail slaughters only while the pasture's category is under its
// limit): SlaughterAtLimit for the animal the slaughter would take (SlaughterCandidate), the same animal whose
// yields RemoveAnimalActions skips.
inline bool SlaughterHeld(const PastureComponent* pasture) {
    return g_on && SlaughterAtLimitSlow(SlaughterCandidate(pasture));
}
// RemoveAnimalActions (the slaughter's CreateRawMaterial per yield): SkipsYield for `citizen` (a pasture's
// herder: its workplace has a PastureComponent). Then that yield is not created: a leather limit leaves the meat.
bool SlaughterSkipsYieldSlow(const CitizenComponent* citizen, const void* item);
inline bool SlaughterSkipsYield(const CitizenComponent* citizen, const void* item) {
    return g_on && SlaughterSkipsYieldSlow(citizen, item);
}
// The item a gatherer gets from a plant it gathers (`herb`: one of HerbalistDescription::_herbs, a natural
// resource template): its NaturalResourceDescription::_building, the template the harvest creates. Null when
// the plant names none.
const ComponentDescription* HerbItem(const ComponentDescription* herb);
// HerbalistWork::Execute, for a plant of the gatherer's list it found: whether the plant's item (HerbItem) has
// reached its item limit. Then the gatherer passes it by and looks on; the plants of other items are still
// gathered.
bool HerbAtLimitSlow(const ComponentDescription* herb);
inline bool HerbAtLimit(const ComponentDescription* herb) {
    return g_on && HerbAtLimitSlow(herb);
}
// HerbalistWork::Execute, after retail's category test passed: whether every item of the gatherer's list has
// reached its item limit (AllAtLimit over HerbItem of each plant). Then the gatherer does no gathering, as at
// the category limit.
bool HerbsAtLimitSlow(const HerbalistDescription* desc);
inline bool HerbsAtLimit(const HerbalistDescription* desc) {
    return g_on && HerbsAtLimitSlow(desc);
}
// PastureProduce (the herd's byproduct: wool, milk, eggs ...): whether the byproduct has reached its item
// limit. Then the production is spent with no pile, as retail does when the category is at its limit.
inline bool ByproductAtLimit(const void* item) {
    return g_on && AtLimit(item);
}

// The workshop panel's row (panel_row.cpp). One EXT SEAM line per site in consumeproduceui.cpp. The row is
// built while the row is active for the game, whether or not a limit is set yet (`g_on` needs one).
extern bool g_rowOn;   // the game's locked row value, this frame (SetRowActive)
inline bool PanelActive() {
    return g_rowOn;
}
void PanelOpenedSlow(ConsumeProduceUI* panel);   // OnCreated, at its end: adds the row
void PanelLayoutSlow(ConsumeProduceUI* panel);   // OnUpdateLayout: follows the combo and the other panels
// OnElementEvent: true when the event was the row's own
bool PanelEventSlow(ConsumeProduceUI* panel, UI::EventId msg, int32_t id);
void PanelsForget();                             // the game ends or the row goes off: forget every row
// A workshop panel with no ConsumeProduceUI (a mod's butcher) gets its rows from its category limit's
// controller, decided at that controller's first layout (panel_row.cpp). Seams in resourcelimitui.cpp.
void LimitRowCreatedSlow(ResourceLimitUI* limit);                        // OnCreated: forget its old rows
void LimitRowLayoutSlow(ResourceLimitUI* limit);                         // OnUpdateLayout
bool LimitRowEventSlow(ResourceLimitUI* limit, UI::EventId msg, int32_t id);   // OnElementEvent: the rows' own
inline void LimitRowCreated(ResourceLimitUI* limit) {
    if (PanelActive()) {
        LimitRowCreatedSlow(limit);
    }
}
inline void LimitRowLayout(ResourceLimitUI* limit) {
    if (PanelActive()) {
        LimitRowLayoutSlow(limit);
    }
}
inline bool LimitRowEvent(ResourceLimitUI* limit, UI::EventId msg, int32_t id) {
    return PanelActive() && LimitRowEventSlow(limit, msg, id);
}
// The pasture panel's rows (pasture_rows.cpp), one per item its livestock gives. Seams in pastureui.cpp.
void PastureOpenedSlow(PastureUI* panel);        // OnCreated, at its end: adds the rows
void PastureLayoutSlow(PastureUI* panel);        // OnUpdateLayout: follows the combo and the other panels
bool PastureEventSlow(PastureUI* panel, UI::EventId msg, int32_t id);   // OnElementEvent: the rows' own
inline void PastureOpened(PastureUI* panel) {
    if (PanelActive()) {
        PastureOpenedSlow(panel);
    }
}
inline void PastureLayout(PastureUI* panel) {
    if (PanelActive()) {
        PastureLayoutSlow(panel);
    }
}
inline bool PastureEvent(PastureUI* panel, UI::EventId msg, int32_t id) {
    return PanelActive() && PastureEventSlow(panel, msg, id);
}
inline void PanelOpened(ConsumeProduceUI* panel) {
    if (PanelActive()) {
        PanelOpenedSlow(panel);
    }
}
inline void PanelLayout(ConsumeProduceUI* panel) {
    if (PanelActive()) {
        PanelLayoutSlow(panel);
    }
}
inline bool PanelEvent(ConsumeProduceUI* panel, UI::EventId msg, int32_t id) {
    return PanelActive() && PanelEventSlow(panel, msg, id);
}

// The save-tail codec for this extension's per-town list (save_state.cpp; registered in state_codecs.cpp).
const SaveTail::StateCodec& Codec();

}  // namespace ItemLimits
}  // namespace Ext
