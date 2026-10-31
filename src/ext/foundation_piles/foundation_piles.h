// ============================================================================
// src/ext/foundation_piles/foundation_piles.h -- fix.foundation_piles (fix tier, default ON)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/FOUNDATIONPILES.md, docs/retail_bugs.md §16.
//
// A building placed over trees and rocks finishes clearing only when its footprint
// holds no occupant (ClearAreaComponent::IsDone 0x140118440 -> Map::IsAreaEmpty
// 0x14009dac0). The felled logs and cut stone left there move only into storage:
// the site's ClearArea work (0x1400e5ee0, phase 3) offers each pile's Pickup work, and
// Pickup::Execute (0x1400f2f00) gives up when CitizenBehavior::HasStorageFor
// (0x14008d3c0) finds no store with room. With every store full the site never
// finishes, and a new stockpile among the piles cannot help: it joins the storage
// list only once built (StorageLocationComponent state 2, 0x14012d230). Retail
// recovers only when the piles rot a game year later (RawMaterialComponent::Update
// 0x140129130).
//
// With the row on, a site whose footprint holds nothing but piles, at the point in
// ClearArea phase 3 where retail found no taker for a pile, has the citizen carry that
// pile to the nearest free cell outside the footprint (foundation_piles_rule.h), with
// retail's own actions: the pile-haul chain (WorkAct_PerformWorkCtor9_c0ad0 0x1400c0ad0:
// walk, pick up), a MoveTo to the chosen cell, and a DropOffRawMaterial with no
// destination (0x1400c7300 -> SpillAllStorage 0x1400c7570 -> DropOnGround 0x1400c78c0),
// the path a citizen with nowhere to put its load already takes. The new pile is an
// ordinary pile: its OnAdded (0x1401291d0) posts its own Pickup work, and it goes to
// storage when there is room. Loose piles are not in the town's totals or capacity
// (CitizenBehavior::CountResources 0x140089ed0 sums the storage lists only), so
// the move changes no count.
// ============================================================================
#pragma once

class CitizenComponent;
class ComponentEntity;
struct GridTile;

namespace Ext {
namespace FoundationPiles {

// Is `site` a building waiting on an empty footprint (it carries a
// ClearAreaComponent, the clear-tool markers do not) whose occupied tiles
// (`tiles[0..count)`, ClearArea's GetTilesInArea list) are all piles?
bool WaitsOnPilesOnly(ComponentEntity* site, const GridTile* tiles, int count);

// ClearArea phase 3, after the scheduler found no taker for `pile` (inside `site`):
// when no storage takes the pile, its Pickup work is unclaimed, the citizen carries
// nothing and a free cell outside the footprint is reachable, the relocation goes on
// the citizen's action stack, the Pickup work is claimed as Pickup::Execute claims it,
// and the result is 2 (dispatched). Otherwise 0 and nothing changed.
int Relocate(ComponentEntity* site, ComponentEntity* pile, CitizenComponent** citizenRef);

}  // namespace FoundationPiles
}  // namespace Ext
