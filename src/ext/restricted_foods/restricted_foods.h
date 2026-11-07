#pragma once
// ============================================================================
// src/ext/restricted_foods/restricted_foods.h -- ext.restricted_foods (class C, default OFF)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/RESTRICTEDFOODS.md. Foods the player checks stop being EATEN by
// citizens while staying Edible for everything else (storage by category, markets, recipes, trade). The
// motivating case: raw Wheat is directly edible, so citizens eat it from the barn instead of the Bread a
// mod's bakery makes from it.
//
// WHERE. Retail decides "may a citizen eat this" in exactly two places, both a test of
// RawMaterialDescription::_flags bit 5 (Edible), and every eating decision goes through one of them:
//   * CitizenComp_Entity_impl_ef40 (retail 0x14008ef40), category 0: the storage item picker the eating
//     action uses to take an item out of a storage (citizencomponent.cpp, two passes over the same test);
//   * ResidenceComponent::GetFoodCount (retail 0x14012a9d0): the Edible count of a home's storage, which
//     gates "does my home have food" (the go-eat decision, the starving icon, the home's food check).
// With the row active for the game, a blocked material counts as NOT Edible in those two tests and nowhere
// else.
//
// THE LIST. The state is the list of blocked foods, each named by the template path of its
// RawMaterialDescription (External::GetPath, the resource path of the object). That identifies the object;
// `_name` is a sprite / string key that mods reuse. Paths compare case-insensitively. A path the running game
// does not have is kept in the list and matches nothing.
//
// IN GAME. The Foods page of the extension window (toolbar_page.cpp, hosted by src/ext/toolbar/: one toolbar
// button, one tab per active extension) lists the edible materials of the running town with a check box each;
// a checked food is not eaten from the moment it is checked, with no Apply.
//
// CLASS C. A blocked food is eaten later, from another source, or not at all, so a game under a list diverges
// from retail, and the list is per-town state a reload must restore: DESIGN.md "Which class, decided by one
// test" makes the row class C. The list lives in the save's tail record (save_state.cpp, wire format v1),
// never in settings.cfg. The lock is on the row's activation (turned on before the town starts); the page
// edits the list live (DESIGN.md "What the lock covers").
//
// SELECTION (DESIGN.md hook rule 4). The seams never touch a string or a map. BeginFrame resolves
// the saved paths once, when the list or the lock epoch changes, into `g_blocked`: a short flat
// array of RawMaterialDescription pointers. The seams test `g_on` once above their loops and scan
// that array per storage row; with the row off, or nothing resolved, `g_on` is false and each pays
// one load and a not-taken branch.
//
// The list, the matching and the codec are standard C++ only: tests/unit/ext_restricted_foods_test.cpp links
// restricted_foods.cpp and save_state.cpp on their own. The game side (the factory walk that finds the
// materials, BeginFrame) is restricted_foods_town.cpp.
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

namespace Ext { namespace SaveTail { struct StateCodec; } }

namespace Ext {
namespace RestrictedFoods {

// The page this extension contributes to the extension window (toolbar_page.cpp builds its Contribution
// from these, and the unit tests check them against the registry and the .rsc).
constexpr const char* kId          = "restricted_foods";
constexpr const char* kPageCaption = "RestrictedFoodsTab";
constexpr const char* kPageRsc     = "Dialog/RestrictedFoodsPage.rsc:pageRestrictedFoods";

// ---- the run-time state the two game seams read ----------------------------------------------------------

// This frame: the row is active for the game AND at least one blocked food resolved to a material.
extern bool g_on;

// The blocked materials (RawMaterialDescription pointers), rebuilt by Resolve. Short: a player blocks
// a handful of foods.
extern std::vector<const void*> g_blocked;

// The seams' test, called only after they checked `g_on`: true = treat this Edible material as not edible.
inline bool IsBlocked(const void* material) {
    const void* const* p = g_blocked.data();
    const void* const* end = p + g_blocked.size();
    for (; p != end; ++p) {
        if (*p == material) {
            return true;
        }
    }
    return false;
}

// ---- the list (the town's state) -------------------------------------------------------------------------

// The template paths of the blocked foods, in the order they were checked.
const std::vector<std::string>& BlockedPaths();
bool IsPathBlocked(const std::string& path);
// Check (`blocked` true) or uncheck a food. Returns whether the list changed.
bool SetPathBlocked(const std::string& path, bool blocked);
// Replace the whole list (the codec's decode). Duplicates are the caller's to refuse.
void ReplaceBlockedPaths(const std::vector<std::string>& paths);
// A new game, or the game ends: an empty list, nothing resolved, `g_on` false.
void ResetState();

// ---- resolving the list against the running game ---------------------------------------------------------

// One Edible RawMaterialDescription of the running game and its template path.
struct Candidate {
    const void* material = nullptr;
    std::string path;
};
// True when `g_blocked` is stale: the list changed, or the lock epoch is not the one it was built in.
bool NeedsResolve(uint32_t epoch);
// Rebuild `g_blocked` from the Edible materials the game has, for the lock epoch `epoch`.
void Resolve(uint32_t epoch, const std::vector<Candidate>& edibleMaterials);
// `g_on` for this frame: the game's locked row value and something resolved.
void SetRowActive(bool rowOn);

// ---- the game side (restricted_foods_town.cpp; not in the unit build) ------------------------------------

// GameUpdateLoop::Update, once per frame beside SpreadWarmth::BeginFrame.
void BeginFrame();
// The template path of a loaded resource object (an `Object::External*`): what the list stores.
std::string TemplatePath(const void* external);

// The save-tail codec for this extension's per-town list (save_state.cpp; registered in state_codecs.cpp).
const SaveTail::StateCodec& Codec();

}  // namespace RestrictedFoods
}  // namespace Ext
