#pragma once
// ============================================================================
// src/ext/storage_filter/storage_filter.h -- ext.storage_filter (class C, default OFF)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/STORAGEFILTER.md. A storage building can refuse items its flags allow,
// or take them only up to a number of units.
//
// RETAIL. A storage building takes a material when the material's RawMaterialDescription flags meet the
// building's accept mask (StorageComponent::AcceptsMaterial, 0x14012c560) and the stock fits its capacity
// (CanAdd, 0x14012cd80). Every drop-off, the choice of the storage a carrier walks to, the absorbing of a
// cleared site's piles and the pickup gate go through CanAdd. The player cannot choose what a building takes.
//
// THE EXTENSION. Per storage building, a set of refused items and a set of caps (an item it takes only up to
// a number of units). A refused item is turned away by AcceptsMaterial, an item at its cap by CanAdd, so
// everything behind CanAdd follows. Flags still rule: the lists can only take away from what the building's
// flags allow, never add to it (a barn without the Logs flag takes no logs, listed or not). Stock already
// inside a building stays and citizens still take it; only new deliveries stop. A building with no refusal
// and no cap, and a town with none at all, is retail's answer: the seam tests one flag first (g_active;
// AnyRefusals is the list's emptiness test).
//
// CLASS C. The lists are the town's own state: they travel in the row's record in the save tail (Codec;
// format version 1, the refusals and the caps). A building is named by its footprint's corner tile
// (MapComponent tileX0, tileY0), which does not change for as long as the building stands. Its entity id
// would not do: it is the entity's index in the master array, and removing any entity renumbers the last
// one. BeginFrame's resolve drops the entry of a tile that no longer holds a storage building (one with a
// StorageLocationComponent, as the tab requires), so a later building on that tile does not inherit it; until
// that frame the lookup applies an entry only to a storage building on its tile.
// An item is named by the template path of its RawMaterialDescription, never by index: mods change the load
// order between sessions, and a path the game no longer has simply refuses nothing.
//
// The list and the codec are standard C++ only: tests/unit/ext_storage_filter_test.cpp links
// storage_filter.cpp and save_state.cpp on their own. The game side (the seam, the entity lookup, the tab) is
// in the exe only.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ext/registry.h"   // Ext::Ext().storage_filter (class C: the game's locked value)
#include "ui/EventId.h"     // UI::EventId (PanelEvent's event id)

namespace Ext { namespace SaveTail { struct StateCodec; } }

class StorageComponent;
class RawMaterialDescription;
class StorageUI;

namespace Ext {
namespace StorageFilter {

constexpr const char* kId = "storage_filter";

// more storage buildings than any town has; the decoder's limit
constexpr std::size_t kMaxBuildings = 65536;
constexpr std::size_t kMaxItemsPerBuilding = 4096;
constexpr std::size_t kMaxPathBytes = 1024;

// ---- the list (the town's state) -------------------------------------------------------------------------

struct Entry {
    int32_t tileX = 0;                  // the storage building's footprint corner tile: the entry's key
    int32_t tileY = 0;
    // template paths of the items it turns away, in the order they were set
    std::vector<std::string> refused;
    struct Cap {
        std::string path;               // an item it takes only up to a number of units
        int32_t limit = 0;              // 1..kMaxCap: it takes no more once it holds this many
    };
    std::vector<Cap> caps;
};
constexpr int32_t kMaxCap = 999999;

// The live list, in the order the buildings were first given a refusal. A building with nothing refused
// has no entry.
const std::vector<Entry>& Entries();
// Whether the list has any entry at all, a refusal or a cap (an emptiness test).
bool AnyRefusals();
// Bumped by every change to the list; the game side re-resolves its paths to descriptions when it moves.
uint32_t Revision();
// Whether the building on footprint corner (`tileX`, `tileY`) turns `path` away. Paths compare without case.
bool IsRefused(int32_t tileX, int32_t tileY, const std::string& path);
// The building's refused paths (empty when none).
std::vector<std::string> RefusedOf(int32_t tileX, int32_t tileY);
// Refuses (`refuse` true) or accepts again (false) one item for the building on that corner tile. A negative
// tile or an invalid path is refused; so is a list already holding kMaxItemsPerBuilding items or
// kMaxBuildings buildings. Accepting the last refused item removes the building's entry. Returns whether the
// list changed.
bool SetRefused(int32_t tileX, int32_t tileY, const std::string& path, bool refuse);
// Removes the building's whole entry (the building is demolished, or the player resets it). Returns whether
// the list changed.
bool ClearBuilding(int32_t tileX, int32_t tileY);
// The building's cap on `path`: 0 when it has none.
int32_t CapOf(int32_t tileX, int32_t tileY, const std::string& path);
// Sets the cap of one item for one building (`limit` 0 or less removes it; above kMaxCap is clamped). An
// entry with neither a refusal nor a cap is removed. Returns whether the list changed.
bool SetCap(int32_t tileX, int32_t tileY, const std::string& path, int32_t limit);
// Replaces the whole list (the codec's decode).
void ReplaceEntries(const std::vector<Entry>& entries);
// A new game, or the game ends: no refusals.
void ResetState();

// Whether an item path can be stored: 1..kMaxPathBytes bytes, none below 0x20.
bool PathIsValid(const std::string& path);

// ---- the game side (storage_filter_town.cpp; not in the unit build)
// ---------------------------------------- GameUpdateLoop::Update, once per frame: resolves the saved paths
// to RawMaterialDescription pointers when the list or the lock epoch changed, drops the entries of buildings
// that no longer stand, and sets g_active (the row is on and some building has a refusal or a cap).
void BeginFrame();
// StorageComponent::AcceptsMaterial, after retail's own flag test would run: whether this building turns the
// material away. False unless the entry for this building's corner tile names the material and the building
// is a storage building with a StorageLocationComponent (the one the tab is for).
bool RefusesSlow(const StorageComponent* storage, const RawMaterialDescription* material);
// StorageComponent::CanAdd, after retail's weight test: whether adding `count` units of `material` would take
// the building past its cap on that item (units already inside counted; false when it has no cap on it).
bool OverCapSlow(const StorageComponent* storage, const RawMaterialDescription* material, int count);
// The seam's first test: one load and a not-taken branch with the row off or no refusal and no cap anywhere.
extern bool g_active;
inline bool Refuses(const StorageComponent* storage, const RawMaterialDescription* material) {
    return g_active && RefusesSlow(storage, material);
}
inline bool OverCap(const StorageComponent* storage, const RawMaterialDescription* material, int count) {
    return g_active && OverCapSlow(storage, material, count);
}

// The Accepts tab of a storage building's window (panel_tab.cpp). One EXT SEAM line per
// site in storageui.cpp.
void PanelOpenedSlow(StorageUI* ui);                            // OnCreated, at its end: adds the tab
// OnUpdateLayout: moves the capacity bar clear of our button
void PanelLayoutSlow(StorageUI* ui);
// OnElementEvent: true when the event was a cell's
bool PanelEventSlow(StorageUI* ui, UI::EventId msg, int32_t id);
// the window's controller is destroyed: forget its tab
void PanelGone(const StorageUI* ui);
inline void PanelOpened(StorageUI* ui) {
    if (Ext::Ext().storage_filter) {
        PanelOpenedSlow(ui);
    }
}
inline void PanelLayout(StorageUI* ui) {
    if (Ext::Ext().storage_filter) {
        PanelLayoutSlow(ui);
    }
}
inline bool PanelEvent(StorageUI* ui, UI::EventId msg, int32_t id) {
    return Ext::Ext().storage_filter && PanelEventSlow(ui, msg, id);
}

// The save-tail codec for this extension's per-town list (save_state.cpp; registered in state_codecs.cpp).
const SaveTail::StateCodec& Codec();

}  // namespace StorageFilter
}  // namespace Ext
