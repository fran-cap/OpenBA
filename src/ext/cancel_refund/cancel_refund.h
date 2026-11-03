// ============================================================================
// src/ext/cancel_refund/cancel_refund.h -- fix.cancel_refund (fix tier, default ON)
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §26.
//
// A cancelled construction site (and a demolished building's refund) is turned into
// ground piles by DestroyComponent::ScatterContents (0x14011c530). Retail lays them only
// on the site's own footprint cells that hold no blocker, are buildable (terrain 2) and
// pass a height test, one pile of at most 100 / weight per cell, while the site's zone
// stamp (Map::StampFootprint 0x14009d360) still marks every non-'.' cell with the site.
// Whatever does not fit when the cursor runs off the footprint is discarded, so a large
// site refunds a few piles and a site on uneven ground refunds nothing.
//
// With the row on, that remainder is laid in piles of the same size on the free cells
// Map::FindPlacementCell (0x14009bd40, the search DropOffRawMaterial::DropOnGround uses
// for a citizen's dropped load) finds around the footprint. The piles are ordinary piles:
// their OnAdded posts their Pickup work and they go to storage when there is room.
// ============================================================================
#pragma once

class ComponentDescription;

namespace Ext {
namespace CancelRefund {

// ScatterContents, after its footprint walk for one material: lay `remaining` units of
// `material` (its RawMaterialDescription weight `weight`) around the footprint
// (x0, y0, w, h). Returns the units it could not place (no free cell within
// FindPlacementCell's reach), which stay discarded as in retail.
int PlaceOverflow(ComponentDescription* material, int weight, int remaining, int x0, int y0, int w, int h);

}  // namespace CancelRefund
}  // namespace Ext
