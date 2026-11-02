// ============================================================================
// src/ext/boat_reach_notice/boat_reach_notice.h -- fix.boat_reach_notice (fix tier, default ON)
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §28.
//
// A trading post's idle state (TradeComponent::TickIdle, 0x14005aa60) spawns a merchant boat when
// its timer runs out and hands it to BoatComponent::SetDestination (0x1400456e0). That builds the
// route as the river polyline plus two water A* legs (Map 0x14009ffc0, river entry -> dock and
// dock -> river exit). When either leg comes back empty, the gate at 0x140045e2e..0x140045e38 drops
// both and the boat follows the bare polyline: it docks only if that passes near the dock, else it
// sails past and is deleted at the route's end (state 0, 0x140045250), and the post re-arms its
// timer. Placement (0x14009f5d0) never tests that the dock's water joins the river. Nothing tells
// the player: the post simply never trades.
//
// With the row on, SetDestination's failed-leg arm calls OnLegsFailed, which adds one line to the
// event log (and so the event ribbon), at the post's tile so a click jumps to it: a merchant's boat
// could not reach the dock. Whether and where the boat sails is unchanged.
// ============================================================================
#pragma once

class ComponentEntity;

namespace Ext {
namespace BoatReachNotice {

// The failed-leg arm of BoatComponent::SetDestination: `post` is the destination (the trading
// post's entity). Adds the event-log line; nothing else.
void OnLegsFailed(ComponentEntity* post);

}  // namespace BoatReachNotice
}  // namespace Ext
