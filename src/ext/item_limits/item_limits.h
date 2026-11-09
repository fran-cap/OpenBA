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
